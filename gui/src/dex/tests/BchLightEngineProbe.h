#pragma once
#include "../DexEngine.h"
#include "../LitecoinJournalProtocol.h"
#include <QEventLoop>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <functional>

// Owned-chain test adapter. The result envelope is generated from the same
// validated status the UI consumes; protocol request/response checks stay in
// BchLightFundingJob. No alternate production funding path is introduced.
inline int bchLightEngineProbe(const QStringList &args) {
    if(args.size()!=6)return 2;
    const auto admission=QString::fromLocal8Bit(qgetenv("MONZERO_BCH_ADMISSION_CONFIG"));
    const auto config=QJsonDocument::fromJson(LitecoinJournalProtocol::privateConfig(args[1],4096)).object();
    QTemporaryDir settings;if(!settings.isValid())return 2;
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    DexEngine engine;
    if(!engine.configureLightWallet("bch",args[5]) || !engine.configureLightWalletSetup(args[2],args[3])
        || !engine.configureBchAdmission(admission))return 3;
    const auto await=[&](const std::function<bool()> &start,bool checking=false) {
        QEventLoop loop;bool timeout=false;
        const auto connection=QObject::connect(&engine,&DexEngine::bchLightFundingChanged,&loop,[&]{
            if(!engine.bchLightFundingStatus().value("busy").toBool())loop.quit();});
        QTimer timer;timer.setSingleShot(true);
        QObject::connect(&timer,&QTimer::timeout,&loop,[&]{timeout=true;loop.quit();});timer.start(240000);
        const bool started=start();
        if(started && !checking && (!engine.lightWallets().value("bch").toMap().value("dexFundingBusy").toBool()
            || engine.receiveLightWallet("bch") || engine.refreshLightWallet("bch")
            || engine.requestBchAdmissionCheck() || engine.prepareBchFunding()
            || engine.queryBchLightTerms() || engine.selectBchLightFundingSource()))return false;
        if(started && engine.bchLightFundingStatus().value("busy").toBool())loop.exec();
        QObject::disconnect(connection);
        return started && !timeout && (checking?engine.bchAdmissionStatus().value("state")=="admitted":
            engine.bchLightFundingStatus().value("state")=="complete");
    };
    if(engine.useBchLightWallet() || !await([&]{return engine.requestBchAdmissionCheck();},true)
        || !engine.useBchLightWallet())return 3;
    QFile input;if(!input.open(stdin,QIODevice::ReadOnly))return 4;
    QByteArray bytes;while(!input.atEnd() && bytes.size()<=8192)bytes+=input.read(8193-bytes.size());
    if(bytes.size()>8192)return 4;
    auto doc=QJsonDocument::fromJson(bytes);bytes.fill(0);bytes.clear();if(!doc.isObject())return 4;
    auto request=doc.object();doc={};
    const auto requestId=request.value("requestId").toString();const QUuid id(requestId);
    if(id.isNull() || id.toString(QUuid::WithoutBraces)!=requestId
        || request.value("schema")!="monzero-bch-light-dex-funding-"+args[4]+"-request-v1")return 4;
    QString password;
    struct Wipe {QString &value;~Wipe(){value.fill(QChar(0));value.clear();}} wipe{password};
    if(request.contains("password")) {
        if(args[4]!="sign" || !request.value("password").isString())return 4;
        password=request.take("password").toString();
    }
    const auto state=engine.bchLightFundingStatus();
    if(request.value("approvedConfigDigest").toString()!=state.value("configurationDigest").toString()
        || config.value("tradeId").toString()!=state.value("tradeId").toString()
        || config.value("quoteCommitment").toString()!=state.value("quoteCommitment").toString())return 5;
    const auto payment=request.value("payment").toObject();
    const auto saved=QJsonObject::fromVariantMap(state.value("retainedPayment").toMap());
    if(!saved.isEmpty() && request.contains("payment") && payment!=saved)return 5;
    if(request.contains("retainedReview") && request.value("retainedReview")!=QJsonObject::fromVariantMap(state.value("retainedReview").toMap()))return 5;
    const auto op=args[4],approval=request.value("approval").toString();bool ok=false;
    if(op=="terms")ok=await([&]{return engine.queryBchLightTerms();});
    else if(op=="source-select")ok=await([&]{return engine.selectBchLightFundingSource();});
    else if(op=="prepare") {
        if(!await([&]{return engine.selectBchLightFundingSource();}) || !await([&]{return engine.queryBchLightTerms();}))return 6;
        const auto terms=engine.bchLightFundingStatus().value("terms").toMap();
        if(payment.value("address").toString()!=terms.value("contractAddress").toString()
            || payment.value("amountAtomic").toString()!=terms.value("amountAtomic").toString()
            || payment.value("minimumChangeAtomic").toString()!=terms.value("minimumChangeAtomic").toString())return 5;
        ok=await([&]{return engine.prepareBchLightFunding(payment.value("feeAtomic").toString(),payment.value("minimumConfirmations").toInt());});
    } else if(op=="review")ok=await([&]{return engine.reviewBchLightSignature();});
    else if(op=="sign") {
        if(!await([&]{return engine.reviewBchLightSignature();}))return 6;
        ok=await([&]{return engine.signBchLightFunding(approval,password);});
    } else if(op=="recovery-review")ok=await([&]{return engine.reviewBchLightRecovery();});
    else if(op=="recovery-retain") {
        if(!await([&]{return engine.reviewBchLightRecovery();}))return 6;
        ok=await([&]{return engine.retainBchLightRecovery(approval);});
    } else if(op=="send-review")ok=await([&]{return engine.reviewBchLightSend();});
    else if(op=="send") {
        if(!await([&]{return engine.reviewBchLightSend();}))return 6;
        ok=await([&]{return engine.sendBchLightFunding(approval);});
    } else if(op=="observe")ok=await([&]{return engine.observeBchLightFunding();});
    else if(op=="retry-review")ok=await([&]{return engine.reviewBchLightRetry();});
    else if(op=="retry") {
        if(!await([&]{return engine.reviewBchLightRetry();}))return 6;
        ok=await([&]{return engine.retryBchLightFunding(approval);});
    } else if(op=="reconcile-ack")ok=await([&]{return engine.reconcileBchLightAcknowledgement();});
    password.fill(QChar(0));password.clear();if(!ok)return 6;
    const auto final=engine.bchLightFundingStatus();
    QJsonObject response{{"schema","monzero-bch-light-dex-funding-"+op+"-response-v1"},
        {"requestId",request.value("requestId")},{"configurationDigest",final.value("configurationDigest").toString()},
        {"tradeId",final.value("tradeId").toString()},{"executable",false},{"result",QJsonObject::fromVariantMap(final.value("result").toMap())}};
    QFile output;if(!output.open(stdout,QIODevice::WriteOnly))return 7;
    const auto encoded=QJsonDocument(response).toJson(QJsonDocument::Compact);
    return output.write(encoded)==encoded.size() && output.flush()?0:7;
}
