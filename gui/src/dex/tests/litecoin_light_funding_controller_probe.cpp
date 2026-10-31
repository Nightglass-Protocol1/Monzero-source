#include "../LitecoinLightFundingController.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);const auto args=app.arguments();if(args.size()!=8)return 2;
    LitecoinLightJob wallet;LitecoinLightFundingController controller;
    if(!wallet.configure(args[5],args[2],args[3]) || !controller.configure(args[1],&wallet,args[2],args[3],args[6],args[7]))return 3;
    if(!controller.setObservationLimits(2,10,50,1000))return 3;
    QFile input;if(!input.open(stdin,QIODevice::ReadOnly))return 4;
    auto bytes=input.read(8193);if(bytes.size()>8192)return 4;
    auto document=QJsonDocument::fromJson(bytes);bytes.fill(0);bytes.clear();if(!document.isObject())return 4;
    auto request=document.object();document={};const auto operation=args[4];
    if(request.value("approvedConfigDigest").toVariant()!=controller.status().value("configurationDigest"))return 4;
    if(request.contains("password") && operation!="sign")return 4;
    if(operation=="terms" && (request.size()!=3 || QUuid(request.value("requestId").toString()).isNull()))return 4;
    auto password=request.take("password").toString().toUtf8();
    QEventLoop loop;QObject::connect(&controller,&LitecoinLightFundingController::changed,&loop,[&]{if(!controller.status().value("busy").toBool())loop.quit();});
    const auto wait=[&](bool started) {
        if(!started)return false;
        if(!wallet.status().value("dexFundingBusy").toBool() || wallet.receiveCurrentWallet() || wallet.refreshWallet(2,10,50))return false;
        QTimer timer;timer.setSingleShot(true);QObject::connect(&timer,&QTimer::timeout,&controller,&LitecoinLightFundingController::cancel);timer.start(240000);
        if(controller.status().value("busy").toBool())loop.exec();
        return controller.status().value("state")=="complete" && controller.status().value("valid").toBool()
            && !wallet.status().value("busy").toBool();
    };
    bool okay=false;
    if(operation=="terms")okay=wait(controller.queryTerms());
    else if(operation=="prepare") {
        if(!wait(controller.queryTerms()))return 5;
        const auto terms=controller.status().value("terms").toMap();const auto p=request.value("payment").toObject();
        if(p.value("address").toVariant()!=terms.value("contractAddress") || p.value("amountAtomic").toVariant()!=terms.value("amountAtomic")
            || p.value("minimumChangeAtomic").toVariant()!=terms.value("minimumChangeAtomic"))return 5;
        okay=wait(controller.prepare(p.value("feeAtomic").toString(),quint32(p.value("minimumConfirmations").toInt())));
    } else if(operation=="review")okay=wait(controller.reviewSignature());
    else if(operation=="sign") {
        if(!wait(controller.reviewSignature()))return 5;
        okay=wait(controller.sign(request.value("approval").toString(),password));
    } else if(operation=="send-review")okay=wait(controller.reviewSend());
    else if(operation=="send") {
        if(!wait(controller.reviewSend()))return 5;
        okay=wait(controller.send(request.value("approval").toString()));
    } else if(operation=="observe")okay=wait(controller.observe());
    else if(operation=="retry-review")okay=wait(controller.reviewRetry());
    else if(operation=="retry") {
        if(!wait(controller.reviewRetry()))return 5;
        okay=wait(controller.retry(request.value("approval").toString()));
    }
    password.fill(0);password.clear();if(!okay)return 6;
    QFile output;if(!output.open(stdout,QIODevice::WriteOnly))return 7;
    output.write(QJsonDocument(controller.response()).toJson(QJsonDocument::Compact));return 0;
}
