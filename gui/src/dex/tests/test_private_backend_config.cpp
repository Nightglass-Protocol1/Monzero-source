#include "../PrivateBackendConfig.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QDebug>

static void require(bool ok) { if (!ok) qFatal("Private backend configuration assertion failed"); }
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    QTemporaryDir root;
    require(root.isValid());
    const auto binary=root.filePath("fixture");
    require(QFile::copy(app.applicationFilePath(),binary));
    require(QFile::setPermissions(binary,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
    QFile executable(binary);
    require(executable.open(QIODevice::ReadOnly));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    require(hash.addData(&executable));
    const auto digest=QString::fromLatin1(hash.result().toHex());
    const QJsonObject config{{"schema","monzero-private-backend-v1"},{"directory",root.path()},
        {"helper",binary},{"helperDigest",digest},{"rpc",binary},{"rpcDigest",digest},
        {"genesis",QString(64,'a')},{"daemonPort","18081"},{"walletPort","18082"}};
    const auto path=root.filePath("config.json");
    const auto write=[&](const QByteArray &bytes) {
        QFile file(path); require(file.open(QIODevice::WriteOnly|QIODevice::Truncate));
        require(file.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
        require(file.write(bytes)==bytes.size());
    };
    PrivateBackendConfig result;
    write(QJsonDocument(config).toJson(QJsonDocument::Compact));
    require(PrivateBackendConfig::load(path,result));
    require(result.walletPort==18082 && result.directory==root.path());
    for (const auto field : {"schema","directory","helper","helperDigest","rpc","rpcDigest","genesis","daemonPort","walletPort"}) {
        auto invalid=config; invalid[field]="invalid";
        write(QJsonDocument(invalid).toJson(QJsonDocument::Compact));
        require(!PrivateBackendConfig::load(path,result));
        require(result.helper.isEmpty() && result.walletPort==0);
    }
    for (const auto port : {"0","018082","65536","18081"}) {
        auto invalid=config; invalid["walletPort"]=port;
        write(QJsonDocument(invalid).toJson(QJsonDocument::Compact));
        require(!PrivateBackendConfig::load(path,result));
    }
    auto extra=config; extra["walletPassword"]="forbidden";
    write(QJsonDocument(extra).toJson(QJsonDocument::Compact));
    require(!PrivateBackendConfig::load(path,result));
    const auto bytes=QJsonDocument(config).toJson(QJsonDocument::Compact);
    write("{\"schema\":\"monzero-private-backend-v1\","+bytes.mid(1));
    require(!PrivateBackendConfig::load(path,result));
    write(bytes);
    require(QFile::setPermissions(path,QFile::ReadOwner|QFile::WriteOwner|QFile::ReadGroup));
    require(!PrivateBackendConfig::load(path,result));
    write(bytes);
    const auto link=root.filePath("alias");
    require(QFile::link(path,link));
    require(!PrivateBackendConfig::load(link,result));
    require(QFile::setPermissions(binary,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner|QFile::WriteGroup));
    require(!PrivateBackendConfig::load(path,result));
    qInfo("PASS: private startup config identity, permissions, executable pins and schema boundaries");
    return 0;
}
