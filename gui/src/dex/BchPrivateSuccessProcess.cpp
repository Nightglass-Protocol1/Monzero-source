#include "BchPrivateSuccessProcess.h"
#include "BackendExecutable.h"
#include <QElapsedTimer>
#ifdef Q_OS_LINUX
#include <cerrno>
#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
extern char **environ;
namespace {
struct Descriptor {
    int value=-1;
    ~Descriptor() { close(); }
    void close() { if (value>=0) { ::close(value);value=-1; } }
};
bool socket(Descriptor &parent,Descriptor &child) {
    int pair[2];if (::socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pair)!=0) return false;
    // Keep child dup sources above stdio and the fixed executable descriptor.
    parent.value=::fcntl(pair[0],F_DUPFD_CLOEXEC,10);
    child.value=::fcntl(pair[1],F_DUPFD_CLOEXEC,10);
    ::close(pair[0]);::close(pair[1]);
    return parent.value>=0 && child.value>=0 && ::fcntl(parent.value,F_SETFL,O_NONBLOCK)==0;
}
struct Child {
    pid_t pid=-1;bool exited=false;
    ~Child() {
        if (pid<=0) return;
        ::kill(-pid,SIGKILL);
        int status;while (::waitpid(pid,&status,0)<0 && errno==EINTR) {}
    }
};
struct PrivateOutput {
    std::array<char,131073> bytes{};
    std::array<char,1024> errors{};
    int size=0;
    ~PrivateOutput() {
        sodium_memzero(bytes.data(),bytes.size());
        sodium_memzero(errors.data(),errors.size());
    }
};
}
#endif
bool BchPrivateSuccessProcess::exchange(const QString &program,const QString &digest,const QString &configuration,
    BchPrivateSuccessProtocol::Action action,const QString &requestId,const QString &tradeId,
    BchPrivateSuccessProtocol::Request &input,BchPrivateSuccessProtocol::Material &result,
    int timeoutMs,const std::atomic_bool *cancelled) {
    result.clear();
    struct Erase { BchPrivateSuccessProtocol::Request &input;~Erase() { input.clear(); } } erase{input};
#ifndef Q_OS_LINUX
    return false;
#else
    using namespace BchPrivateSuccessProtocol;
    if (timeoutMs<=0 || timeoutMs>180000 || input.view().isEmpty()
        || !canonicalId(requestId) || !canonicalId(tradeId)
        || !QFileInfo(configuration).isAbsolute() || (cancelled && cancelled->load())) return false;
    QByteArray mode;
    switch (action) {
    case Action::Frame: mode="--success-private-frame-stdio-v1";break;
    case Action::Retain: mode="--success-private-retain-stdio-v1";break;
    case Action::Release: mode="--success-private-release-stdio-v1";break;
    default: return false;
    }
    if (!BackendExecutable::matches(program,digest)) return false;
    Descriptor executable;
    const auto executableName=QFile::encodeName(program);
    const int opened=::open(executableName.constData(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if (opened<0) return false;
    executable.value=::fcntl(opened,F_DUPFD_CLOEXEC,10);::close(opened);
    if (executable.value<0) return false;
    // Hash the actual execution descriptor as well as validating the installation.
    QFile pinned;if (!pinned.open(executable.value,QIODevice::ReadOnly,QFileDevice::DontCloseHandle)) return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&pinned) || QString::fromLatin1(hash.result().toHex())!=digest) return false;
    pinned.close();
    Descriptor writer,reader,errors,childInput,childOutput,childErrors;
    if (!socket(writer,childInput) || !socket(reader,childOutput) || !socket(errors,childErrors)) return false;
    posix_spawn_file_actions_t actions;
    if (::posix_spawn_file_actions_init(&actions)!=0) return false;
    struct Actions { posix_spawn_file_actions_t *p;~Actions() { ::posix_spawn_file_actions_destroy(p); } } actionGuard{&actions};
    if (::posix_spawn_file_actions_adddup2(&actions,childInput.value,0)!=0
        || ::posix_spawn_file_actions_adddup2(&actions,childOutput.value,1)!=0
        || ::posix_spawn_file_actions_adddup2(&actions,childErrors.value,2)!=0
        || ::posix_spawn_file_actions_adddup2(&actions,executable.value,3)!=0
        || ::posix_spawn_file_actions_addclosefrom_np(&actions,4)!=0) return false;
    posix_spawnattr_t attributes;
    if (::posix_spawnattr_init(&attributes)!=0) return false;
    struct Attributes { posix_spawnattr_t *p;~Attributes() { ::posix_spawnattr_destroy(p); } } attributeGuard{&attributes};
    if (::posix_spawnattr_setflags(&attributes,POSIX_SPAWN_SETPGROUP)!=0 || ::posix_spawnattr_setpgroup(&attributes,0)!=0) return false;
    auto config=QFile::encodeName(configuration);auto name=executableName;
    char *arguments[]={name.data(),mode.data(),config.data(),nullptr};Child child;
    if (::posix_spawn(&child.pid,"/proc/self/fd/3",&actions,&attributes,arguments,environ)!=0) { child.pid=-1;return false; }
    childInput.close();childOutput.close();childErrors.close();executable.close();
    QElapsedTimer deadline;deadline.start();PrivateOutput output;int written=0;bool successfulExit=false;
    while (deadline.elapsed()<timeoutMs && !(cancelled && cancelled->load())) {
        pollfd descriptors[]={{writer.value,POLLOUT,0},{reader.value,POLLIN,0},{errors.value,POLLIN,0}};
        const int ready=::poll(descriptors,3,qMax(0,qMin(50,timeoutMs-int(deadline.elapsed()))));
        if (ready<0 && errno!=EINTR) return false;
        if (writer.value>=0 && descriptors[0].revents) {
            const auto view=input.view();
            const auto count=::send(writer.value,view.constData()+written,size_t(view.size()-written),MSG_NOSIGNAL);
            if (count>0) written+=int(count);
            else if (count<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR) return false;
            if (written==view.size()) { writer.close();input.clear(); }
        }
        if (reader.value>=0 && descriptors[1].revents) {
            const auto count=::read(reader.value,output.bytes.data()+output.size,output.bytes.size()-size_t(output.size));
            if (count>0) { output.size+=int(count);if (output.size>131072) return false; }
            else if (!count) reader.close();
            else if (errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR) return false;
        }
        if (errors.value>=0 && descriptors[2].revents) {
            const auto count=::read(errors.value,output.errors.data(),output.errors.size());
            if (count>0) return false;
            if (!count) errors.close();
            else if (errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR) return false;
        }
        if (!child.exited) {
            siginfo_t information{};
            // Keep the child unreaped until cleanup so its process-group ID
            // cannot be reused while draining output or cancelling descendants.
            if (::waitid(P_PID,child.pid,&information,WEXITED|WNOHANG|WNOWAIT)<0) {
                if (errno!=EINTR) return false;
            } else if (information.si_pid==child.pid) {
                child.exited=true;successfulExit=information.si_code==CLD_EXITED && information.si_status==0;
            }
        }
        if (child.exited && reader.value<0 && errors.value<0) {
            if (writer.value>=0 || !successfulExit || deadline.elapsed()>=timeoutMs
                || (cancelled && cancelled->load())) return false;
            return response(output.bytes.data(),output.size,action,requestId,tradeId,result);
        }
    }
    return false;
#endif
}
