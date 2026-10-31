#include "../LitecoinLightFundingJob.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);const auto args=app.arguments();
    if(args.size()!=5 && args.size()!=6)return 2;
    LitecoinLightFundingJob job;LitecoinLightJob wallet;
    const bool bound=args.size()==6;
    if(bound) {
        if(!wallet.configure(args[5],args[2],args[3]) || !job.configureWallet(args[1],&wallet,args[2],args[3]))return 3;
    } else if(!job.configure(args[1],args[2],args[3]))return 3;
    QFile input;if(!input.open(stdin,QIODevice::ReadOnly))return 4;
    auto bytes=input.read(8193);if(bytes.size()>8192)return 4;
    auto document=QJsonDocument::fromJson(bytes);bytes.fill(0);bytes.clear();
    if(!document.isObject())return 4;
    auto request=document.object();document={};
    QByteArray password;
    if(request.contains("password")) {
        if(args[4]!="sign" || !request.value("password").isString())return 4;
        password=request.take("password").toString().toUtf8();
    }
    QEventLoop loop;QObject::connect(&job,&LitecoinLightFundingJob::changed,&loop,[&]{if(!job.status().value("busy").toBool())loop.quit();});
    const bool started=job.run(args[4],request,password);password.fill(0);password.clear();
    if(!started)return 5;
    if(bound && (!wallet.status().value("dexFundingBusy").toBool() || !wallet.status().value("busy").toBool()
        || wallet.refreshWallet(2,10,50) || wallet.receiveCurrentWallet()))return 8;
    QTimer::singleShot(240000,&job,&LitecoinLightFundingJob::cancel);
    if(job.status().value("busy").toBool())loop.exec();
    if(bound && wallet.status().value("dexFundingBusy").toBool())return 8;
    if(job.status().value("state")!="complete")return 6;
    QFile output;if(!output.open(stdout,QIODevice::WriteOnly))return 7;
    output.write(QJsonDocument(job.response()).toJson(QJsonDocument::Compact));return 0;
}
