#include "../BchLightFundingController.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <functional>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);const auto args=app.arguments();
    if(args.size()!=6)return 2;
    LitecoinLightJob wallet("bch");BchLightFundingController controller;
    const auto config=QJsonDocument::fromJson(LitecoinJournalProtocol::privateConfig(args[1],4096)).object();
    if(!wallet.configure(args[5],args[2],args[3]) || !controller.configure(args[1],&wallet,args[2],args[3],
        config.value("tradeId").toString(),config.value("quoteCommitment").toString()))return 3;
    QFile input;if(!input.open(stdin,QIODevice::ReadOnly))return 4;
    auto bytes=input.read(8193);if(bytes.size()>8192)return 4;
    auto document=QJsonDocument::fromJson(bytes);bytes.fill(0);bytes.clear();if(!document.isObject())return 4;
    auto request=document.object();document={};QByteArray password;
    if(request.contains("password")) {
        if(args[4]!="sign" || !request.value("password").isString())return 4;
        password=request.take("password").toString().toUtf8();
    }
    const auto state=controller.status();
    if(request.value("approvedConfigDigest").toString()!=state.value("configurationDigest").toString())return 5;
    const auto savedPayment=QJsonObject::fromVariantMap(state.value("retainedPayment").toMap());
    const auto savedReview=QJsonObject::fromVariantMap(state.value("retainedReview").toMap());
    if(!savedPayment.isEmpty() && request.contains("payment") && request.value("payment")!=savedPayment)return 5;
    if(request.contains("retainedReview") && request.value("retainedReview")!=savedReview)return 5;
    const auto scan=request.value("walletScan").toObject();
    if(args[4]!="terms" && args[4]!="source-select" && !args[4].startsWith("recovery-")) {
        const bool cold=QStringList{"observe","retry-review","retry","reconcile-ack"}.contains(args[4]);
        if(!controller.setObservationLimits(cold?2:quint32(scan.value("gap").toInt()),
            cold?3:quint32(scan.value("addressesPerBranch").toInt()),
            quint32(cold?request.value("maximumHistoryEntries").toInt():scan.value("historyEntriesPerBranch").toInt()),
            quint32(request.value("maximumDownloadHeaders").toInt())))return 4;
    }
    int guardedCompletions=0;bool reentered=false;
    QObject::connect(&wallet,&LitecoinLightJob::changed,&controller,[&]{
        if(!wallet.status().value("busy").toBool() && controller.status().value("busy").toBool()) {
            ++guardedCompletions;if(controller.reviewRetry())reentered=true;
        }
    });
    const auto run=[&](const std::function<bool()> &start) {
        const auto before=guardedCompletions;QEventLoop loop;
        const auto connection=QObject::connect(&controller,&BchLightFundingController::changed,&loop,[&]{
            if(!controller.status().value("busy").toBool())loop.quit();});
        const bool started=start();
        if(started && (!wallet.status().value("dexFundingBusy").toBool() || wallet.receiveCurrentWallet() || wallet.refreshWallet(2,10,50)))return false;
        QTimer timer;timer.setSingleShot(true);QObject::connect(&timer,&QTimer::timeout,&controller,&BchLightFundingController::cancel);timer.start(240000);
        if(started && controller.status().value("busy").toBool())loop.exec();
        QObject::disconnect(connection);
        return started && !reentered && guardedCompletions>before && controller.status().value("state")=="complete" && !wallet.status().value("dexFundingBusy").toBool();
    };
    const auto op=args[4],approval=request.value("approval").toString();bool ok=false;
    if(op=="terms")ok=run([&]{return controller.queryTerms();});
    else if(op=="source-select")ok=run([&]{return controller.selectWalletSource();});
    else if(op=="prepare") {
        if(!run([&]{return controller.selectWalletSource();}))return 6;
        if(!run([&]{return controller.queryTerms();}))return 6;
        const auto terms=controller.status().value("terms").toMap();const auto payment=request.value("payment").toObject();
        if(payment.value("address").toString()!=terms.value("contractAddress").toString()
            || payment.value("amountAtomic").toString()!=terms.value("amountAtomic").toString())return 5;
        ok=run([&]{return controller.prepare(payment.value("feeAtomic").toString(),quint32(payment.value("minimumConfirmations").toInt()));});
    } else if(op=="review")ok=run([&]{return controller.reviewSignature();});
    else if(op=="sign") {
        if(!run([&]{return controller.reviewSignature();}))return 6;
        const auto shown=controller.status().value("review").toMap();
        if(shown.value("commitment").toString()!=approval)return 5;
        ok=run([&]{return controller.sign(approval,password);});
    } else if(op=="recovery-review")ok=run([&]{return controller.reviewRecovery();});
    else if(op=="recovery-retain") {
        if(!run([&]{return controller.reviewRecovery();}))return 6;
        ok=run([&]{return controller.retainRecovery(approval);});
    } else if(op=="send-review")ok=run([&]{return controller.reviewSend();});
    else if(op=="send") {
        if(!run([&]{return controller.reviewSend();}))return 6;
        ok=run([&]{return controller.send(approval);});
    } else if(op=="observe")ok=run([&]{return controller.observe();});
    else if(op=="retry-review")ok=run([&]{return controller.reviewRetry();});
    else if(op=="retry") {
        if(!run([&]{return controller.reviewRetry();}))return 6;
        ok=run([&]{return controller.retry(approval);});
    } else if(op=="reconcile-ack")ok=run([&]{return controller.reconcileAcknowledgement();});
    password.fill(0);password.clear();if(!ok)return 6;
    QFile output;if(!output.open(stdout,QIODevice::WriteOnly))return 7;
    output.write(QJsonDocument(controller.response()).toJson(QJsonDocument::Compact));return 0;
}
