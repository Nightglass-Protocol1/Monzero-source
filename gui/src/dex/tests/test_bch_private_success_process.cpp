#include "../BchPrivateSuccessProcess.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cstdio>
#include <thread>
#include <chrono>
#include <sys/wait.h>
#include <unistd.h>
using namespace BchPrivateSuccessProtocol;
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);const auto args=app.arguments();
    const QString trade="22222222-2222-4222-8222-222222222222";
    if (args.size()==3) {
        QFile file(args[2]);if (!file.open(QIODevice::ReadOnly)) return 90;
        const auto mode=file.readAll();
        if (mode=="early") return 1;
        char bytes[32768];const auto size=std::fread(bytes,1,sizeof bytes,stdin);
        const auto input=QJsonDocument::fromJson(QByteArray(bytes,int(size))).object();
        if (mode=="sleep") { ::usleep(5000000);return 1; }
        if (mode=="descendant") { if (::fork()==0) { ::usleep(5000000);_exit(0); }return 0; }
        if (mode=="stderr") { std::fputs("private-error-sentinel",stderr);return 1; }
        if (mode=="oversize") { const QByteArray big(131074,'x');std::fwrite(big.constData(),1,big.size(),stdout);return 0; }
        const auto operation=args[1].contains("frame")?QByteArray("frame"):args[1].contains("retain")?QByteArray("retain"):QByteArray("release");
        QByteArray output="{\"schema\":\"monzero-bch-success-private-"+operation+"-response-v1\",\"requestId\":\""
            +(mode=="wrong-id"?trade:input.value("requestId").toString()).toUtf8()+"\",\"tradeId\":\""+trade.toUtf8()+"\",\"chainObserved\":true";
        if (operation!="retain") output+=",\"frameHex\":\"0001ff\"";
        if (operation=="release") output+=",\"signatureHex\":\""+QByteArray("07").repeated(64)+"\"";
        output+='}';if (mode=="truncated") output.chop(1);
        if (mode=="fragmented") { for (char c : output) { std::fwrite(&c,1,1,stdout);std::fflush(stdout); } }
        else std::fwrite(output.constData(),1,output.size(),stdout);
        return mode=="bad-exit"?1:0;
    }
    QTemporaryDir directory;if (!directory.isValid()) return 2;
    const auto configuration=directory.path()+"/fixture";const auto program=QFileInfo(app.applicationFilePath()).canonicalFilePath();
    QFile executable(program);if (!executable.open(QIODevice::ReadOnly)) return 3;
    QCryptographicHash hasher(QCryptographicHash::Sha256);if (!hasher.addData(&executable)) return 4;
    const auto digest=QString::fromLatin1(hasher.result().toHex());executable.close();int checks=0;
    const QString requestId="11111111-1111-4111-8111-111111111111",commitment=QString("ab").repeated(32);
    auto check=[&](const QByteArray &mode,Action action,bool expected,int timeout=2000,bool cancel=false,bool badPin=false) {
        QFile file(configuration);if (!file.open(QIODevice::WriteOnly|QIODevice::Truncate) || file.write(mode)!=mode.size()) return false;file.close();
        Request input;Material output;
        if (!request(action,requestId,commitment,action==Action::Retain?QByteArray(8192,'x'):QByteArray(),action==Action::Retain?QByteArray(64,'x'):QByteArray(),input)) return false;
        const auto retained=input.view();std::atomic_bool cancelled{false};std::thread trigger;
        if (cancel) trigger=std::thread([&]{std::this_thread::sleep_for(std::chrono::milliseconds(20));cancelled=true;});
        const bool passed=BchPrivateSuccessProcess::exchange(program,badPin?QString(64,'0'):digest,configuration,action,requestId,trade,input,output,timeout,&cancelled);
        if (trigger.joinable()) trigger.join();
        if (passed!=expected || !input.view().isEmpty() || retained!=QByteArray(retained.size(),'\0')) return false;
        if (!passed && (!output.frameView().isEmpty() || !output.signatureView().isEmpty())) return false;
        if (passed && action!=Action::Retain && output.frameView()!=QByteArray::fromHex("0001ff")) return false;
        int status;errno=0;if (::waitpid(-1,&status,WNOHANG)!=-1 || errno!=ECHILD) return false;
        ++checks;return true;
    };
    for (auto action : {Action::Frame,Action::Retain,Action::Release}) if (!check("valid",action,true)) return 5;
    if (!check("fragmented",Action::Release,true)) return 6;
    for (const auto &mode : {"early","stderr","oversize","wrong-id","truncated","bad-exit"}) if (!check(mode,Action::Frame,false)) return 7;
    if (!check("sleep",Action::Frame,false,30) || !check("sleep",Action::Frame,false,2000,true)
        || !check("valid",Action::Frame,false,2000,false,true) || !check("descendant",Action::Frame,false,30)) return 8;
    std::printf("%d private process exchange checks passed\n",checks);return 0;
}
