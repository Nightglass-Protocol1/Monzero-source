#include "DexTestDriver.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMetaMethod>

DexTestDriver::DexTestDriver(QObject *engine,QObject *parent):QObject(parent),m_engine(engine) {}

bool DexTestDriver::listen(const QString &directory) {
    if (!QDir::isAbsolutePath(directory) || !QDir().mkpath(directory)
        || !QFile::setPermissions(directory,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner)) return false;
    const auto path=QDir(directory).filePath("driver.sock");
    QLocalServer::removeServer(path);
    m_server=new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(path)) return false;
    connect(m_server,&QLocalServer::newConnection,this,[this]{
        while (auto *s=m_server->nextPendingConnection()) serve(s);
    });
    qWarning("DEX test driver listening at %s",qPrintable(path));
    return true;
}

void DexTestDriver::serve(QLocalSocket *socket) {
    connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
    connect(socket,&QLocalSocket::readyRead,this,[this,socket]{
        while (socket->canReadLine()) {
            const auto reply=answer(socket->readLine(1<<20).trimmed());
            socket->write(reply+"\n");socket->flush();
        }
    });
}

QByteArray DexTestDriver::answer(const QByteArray &line) {
    const auto fail=[](const QString &why){return QJsonDocument(QJsonObject{{"ok",false},{"error",why}}).toJson(QJsonDocument::Compact);};
    const auto done=[](const QVariant &v){
        return QJsonDocument(QJsonObject{{"ok",true},{"result",QJsonValue::fromVariant(v)}}).toJson(QJsonDocument::Compact);
    };
    if (!m_engine) return fail("no engine");
    const auto r=QJsonDocument::fromJson(line).object();
    const auto *meta=m_engine->metaObject();
    if (r.contains("get")) {
        const auto name=r.value("get").toString().toLatin1();
        const int index=meta->indexOfProperty(name.constData());
        if (index<0) return fail("no such property");
        return done(meta->property(index).read(m_engine));
    }
    const auto name=r.value("call").toString().toLatin1();
    const auto args=r.value("args").toArray();
    if (args.size()>8) return fail("too many arguments");
    for (int i=0;i<meta->methodCount();++i) {
        const auto m=meta->method(i);
        if (m.methodType()!=QMetaMethod::Method || m.access()!=QMetaMethod::Public || m.name()!=name || m.parameterCount()!=args.size()) continue;
        // Convert each JSON argument to the parameter's type
        QVariantList values;
        for (int p=0;p<args.size();++p) {
            QVariant v=args.at(p).toVariant();
            if (!v.convert(m.parameterType(p))) return fail(QStringLiteral("argument %1 has the wrong type").arg(p));
            values<<v;
        }
        QGenericArgument a[8];
        for (int p=0;p<values.size();++p) a[p]=QGenericArgument(values[p].typeName(),values[p].constData());
        const int returnType=m.returnType();
        if (returnType==QMetaType::Void) {
            if (!m.invoke(m_engine,Qt::DirectConnection,a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7])) return fail("the call failed");
            return done({});
        }
        QVariant result(returnType,nullptr);
        if (!m.invoke(m_engine,Qt::DirectConnection,QGenericReturnArgument(m.typeName(),result.data()),a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7]))
            return fail("the call failed");
        return done(result);
    }
    return fail("no such invokable");
}
