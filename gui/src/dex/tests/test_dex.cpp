#include "../DexDeployment.h"
#include "../DexRiskControl.h"
#include "../LightWalletProfile.h"
#include "../DexTradeFolders.h"
#include "../LitecoinAppConfig.h"
#include <sodium.h>
#include "../BchRefundWalletJob.h"
#include "BchPublicationRepairTestFixture.h"
#include "BchPunishmentWorkerTestFixture.h"
#include "BchRefundWalletWorkerTestFixture.h"
#include "../DexEngine.h"
#include "../LightWalletHelperUpgrade.h"
#include "../LitecoinLightJob.h"
#include "../LitecoinLightFundingContext.h"
#include "../LitecoinLightFundingSetup.h"
#include "../LitecoinLightFundingJob.h"
#include "../BchLightFundingSetup.h"
#include "BchAdmissionTestFixture.h"
#include "BchLightEngineProbe.h"
#include "BchCancellationTestFixture.h"
#include "LitecoinAppFixture.h"
#include "../OfferIdentityStore.h"
#include "../LitecoinJournalJob.h"
#include "../LitecoinReservationJob.h"
#include "../LitecoinNegotiationSession.h"
#include "../LitecoinPeerStartup.h"
#include "../PrivateQuoteServer.h"
#include <QSslKey>
#include <QJsonArray>
#include "../DexUtils.h"
#include "../OfferBookStore.h"
#include "../NativeFeePreviewReceiver.h"
#include "../NativeRecoveryJob.h"
#include "../NativeFeePreviewController.h"
#include "../NativeFeePreviewJob.h"
#include "../SwapWalletRpcProcess.h"
#include "../NativeWalletControlJob.h"
#include "../SwapWalletSession.h"
#include "../BackendExecutable.h"
#include "../NativeQuoteCoordinator.h"
#include <QDir>
#include <QSaveFile>
#include <QSettings>
#include <QTimer>
#include <QLockFile>
#include <QFile>
#include <QThread>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QDateTime>
#include <QThreadPool>
#include <QSignalSpy>
#include <QCoreApplication>
#include <QTest>
#include <QUuid>
#include <functional>

class DexEngineTest : public QObject
{
    Q_OBJECT
private slots:
    void litecoinDeploymentStageIsExplicitAndExclusive() {
        DexDeployment::resetForTesting();
        QCOMPARE(DexDeployment::litecoinNetwork(),QString("ltc-regtest"));QCOMPARE(DexDeployment::litecoinLightNetwork(),QString("regtest"));
        QVERIFY(DexDeployment::litecoinNetworkAllowed("ltc-regtest"));
        for (const auto &other:{"ltc-testnet","ltc-mainnet","","LTC-REGTEST"}) QVERIFY(!DexDeployment::litecoinNetworkAllowed(other));
        for (const auto &bad:{"mainnet","main","ltc-testnet","","Testnet"}) QVERIFY(!DexDeployment::selectLitecoinStage(bad));
        QVERIFY(DexDeployment::selectLitecoinStage("testnet"));
        QCOMPARE(DexDeployment::litecoinNetwork(),QString("ltc-testnet"));QCOMPARE(DexDeployment::litecoinLightNetwork(),QString("test"));
        QVERIFY(DexDeployment::litecoinNetworkAllowed("ltc-testnet"));
        QVERIFY(!DexDeployment::litecoinNetworkAllowed("ltc-regtest"));QVERIFY(!DexDeployment::litecoinNetworkAllowed("ltc-mainnet"));
        QVERIFY(!DexDeployment::selectLitecoinStage("regtest")); // locked after first selection
        QCOMPARE(DexDeployment::litecoinNetwork(),QString("ltc-testnet"));
        DexDeployment::resetForTesting();DexDeployment::lockLitecoinStage();
        QVERIFY(!DexDeployment::selectLitecoinStage("testnet"));QCOMPARE(DexDeployment::litecoinNetwork(),QString("ltc-regtest"));
        // Configurations bound to the other network are refused end to end.
        QTemporaryDir root;QVERIFY(root.isValid());const QString wallet=root.filePath("wallet.json");
        {QFile f(wallet);QVERIFY(f.open(QIODevice::WriteOnly));f.write("{}");QVERIFY(f.setPermissions(QFile::ReadOwner|QFile::WriteOwner));}
        const auto trade=QUuid::createUuid().toString(QUuid::WithoutBraces);
        QJsonObject context{{"schema","monzero-private-ltc-light-dex-v1"},{"tradeId",trade},{"metadataDirectory",root.path()},
            {"snapshotDirectory",root.path()},{"pins",QJsonArray{}},{"policy",QJsonObject{{"ltc_network","ltc-testnet"}}}};
        const auto path=LitecoinLightFundingSetup::retain(context,wallet);QVERIFY(!path.isEmpty());
        LitecoinLightFundingJob job;const auto helper=QCoreApplication::applicationFilePath();
        const auto digest=QString::fromLatin1(QCryptographicHash::hash(read(helper),QCryptographicHash::Sha256).toHex());
        QVERIFY(!job.configure(path,helper,digest));
        DexDeployment::resetForTesting();
    }
    void bitcoinDeploymentStagesUseBitcoinIdentities() {
        DexDeployment::resetForTesting();
        QCOMPARE(DexDeployment::litecoinLightCoin(),QString("ltc"));QVERIFY(!DexDeployment::bitcoinStage());
        for (const auto &bad:{"btc-testnet","btc-mainnet","bitcoin","BTC-REGTEST","btc-testnet3"}) QVERIFY(!DexDeployment::selectLitecoinStage(bad));
        QVERIFY(DexDeployment::selectLitecoinStage("btc-regtest"));
        QVERIFY(DexDeployment::bitcoinStage());QVERIFY(!DexDeployment::publicTestStage());
        QCOMPARE(DexDeployment::litecoinNetwork(),QString("btc-regtest"));QCOMPARE(DexDeployment::litecoinLightNetwork(),QString("regtest"));
        QCOMPARE(DexDeployment::litecoinLightCoin(),QString("btc"));QCOMPARE(DexDeployment::swapCoinName(),QString("Bitcoin"));
        QCOMPARE(DexDeployment::litecoinGenesis(),QString("0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206"));
        QVERIFY(DexDeployment::litecoinNetworkAllowed("btc-regtest"));QVERIFY(!DexDeployment::litecoinNetworkAllowed("ltc-regtest"));
        QVERIFY(!DexDeployment::selectLitecoinStage("regtest"));
        DexDeployment::resetForTesting();
        QVERIFY(DexDeployment::selectLitecoinStage("btc-testnet4"));QVERIFY(DexDeployment::publicTestStage());
        QCOMPARE(DexDeployment::litecoinNetwork(),QString("btc-testnet4"));QCOMPARE(DexDeployment::litecoinLightNetwork(),QString("testnet4"));
        QCOMPARE(DexDeployment::litecoinGenesis(),QString("00000000da84f2bafbbc53dee25a72ae507ff4914b867c565be350b0da8bf043"));
        QVERIFY(LitecoinPeerStartup::addressAllowed("connect",QHostAddress(QString("192.0.2.1"))));
        // Address prefixes are bound to the quoted chain.
        QCOMPARE(LitecoinJournalProtocol::bech32Prefix("btc-testnet4"),QString("tb1"));
        QCOMPARE(LitecoinJournalProtocol::bech32Prefix("btc-regtest"),QString("bcrt1"));
        QCOMPARE(LitecoinJournalProtocol::bech32Prefix("btc-mainnet"),QString("bc1"));
        QVERIFY(LitecoinJournalProtocol::bech32Prefix("btc-testnet").isEmpty());
        QVERIFY(LitecoinJournalProtocol::witnessAddress("tb1q"+QString(58,'q'),"btc-testnet4","q[02-9ac-hj-np-z]{58}"));
        QVERIFY(!LitecoinJournalProtocol::witnessAddress("tltc1q"+QString(58,'q'),"btc-testnet4","q[02-9ac-hj-np-z]{58}"));
        QVERIFY(LightWalletProfile::networkAllowed("btc","testnet4"));QVERIFY(!LightWalletProfile::networkAllowed("ltc","testnet4"));
        QCOMPARE(LightWalletProfile::witnessPrefix("btc","testnet4"),QString("tb"));
        QCOMPARE(LightWalletProfile::receivePath("btc","testnet4",0),QString("84'/1'/0'/0/"));
        // Risk limits treat Bitcoin regtest like Litecoin regtest and name the chain.
        DexRiskControl control;control.setNetwork("btc-regtest");
        QVERIFY(control.refusal(QUuid::createUuid().toString(QUuid::WithoutBraces),1,1).isEmpty());
        QVERIFY(!control.status().value("controlRequired").toBool());
        control.setNetwork("btc-testnet4");QVERIFY(control.status().value("controlRequired").toBool());
        // Signet: a second public Bitcoin test network, sharing testnet4's "tb" prefix
        // but never its genesis, quote name or light-network name.
        DexDeployment::resetForTesting();
        QVERIFY(DexDeployment::selectLitecoinStage("btc-signet"));
        QVERIFY(DexDeployment::publicTestStage());QVERIFY(DexDeployment::bitcoinStage());
        QCOMPARE(DexDeployment::litecoinNetwork(),QString("btc-signet"));QCOMPARE(DexDeployment::litecoinLightNetwork(),QString("signet"));
        QCOMPARE(DexDeployment::litecoinGenesis(),QString("00000008819873e925422c1ff0f99f7cc9bbb232af63a077a480a3633bee1ef6"));
        QVERIFY(DexDeployment::litecoinNetworkAllowed("btc-signet"));QVERIFY(!DexDeployment::litecoinNetworkAllowed("btc-testnet4"));
        QCOMPARE(LitecoinJournalProtocol::bech32Prefix("btc-signet"),QString("tb1"));
        QVERIFY(LightWalletProfile::networkAllowed("btc","signet"));QVERIFY(!LightWalletProfile::networkAllowed("ltc","signet"));
        QVERIFY(!LightWalletProfile::networkAllowed("doge","signet"));
        QCOMPARE(LightWalletProfile::witnessPrefix("btc","signet"),QString("tb"));
        control.setNetwork("btc-signet");QVERIFY(control.status().value("controlRequired").toBool());
        DexDeployment::resetForTesting();
    }
    void litecoinPeerAddressesFollowTheStage() {
        using A=QHostAddress;
        const auto allowed=[](const char *mode,const char *address){return LitecoinPeerStartup::addressAllowed(mode,A(QString(address)));};
        DexDeployment::resetForTesting();  // regtest: local peers only
        QVERIFY(allowed("listen","127.0.0.1"));QVERIFY(allowed("connect","127.0.0.1"));QVERIFY(allowed("connect","::1"));
        for (const auto *address:{"192.0.2.1","10.0.0.5","0.0.0.0","::"}) {QVERIFY(!allowed("listen",address));QVERIFY(!allowed("connect",address));}
        QVERIFY(DexDeployment::selectLitecoinStage("testnet"));  // testnet: our own other machines
        QVERIFY(allowed("connect","192.0.2.1"));QVERIFY(allowed("listen","192.0.2.1"));QVERIFY(allowed("connect","2001:db8::1"));
        QVERIFY(allowed("listen","0.0.0.0"));QVERIFY(allowed("listen","::"));
        QVERIFY(!allowed("connect","0.0.0.0"));QVERIFY(!allowed("connect","::"));
        for (const auto *address:{"224.0.0.1","255.255.255.255","ff02::1","not-an-address",""}) {QVERIFY(!allowed("listen",address));QVERIFY(!allowed("connect",address));}
        QVERIFY(allowed("connect","127.0.0.1"));
        DexDeployment::resetForTesting();
    }
    void riskControlKillSwitchAndCaps() {
        QVERIFY(sodium_init()>=0);
        unsigned char pk[crypto_sign_PUBLICKEYBYTES],sk[crypto_sign_SECRETKEYBYTES];crypto_sign_keypair(pk,sk);
        unsigned char otherPk[crypto_sign_PUBLICKEYBYTES],otherSk[crypto_sign_SECRETKEYBYTES];crypto_sign_keypair(otherPk,otherSk);
        const qint64 now=1800000000;
        auto document=[&](QJsonObject changes,const unsigned char *key){
            QJsonObject payload{{"schema","monzero-dex-control-v1"},{"network","ltc-testnet"},{"sequence",1},{"issuedAt",double(now-60)},
                {"expiresAt",double(now+86400)},{"newTradesEnabled",true},{"maxLtcAtomsPerTrade","100000000"},
                {"maxXmzAtomsPerTrade","50000000000000"},{"maxLtcAtomsPerDay","250000000"}};
            for (auto it=changes.begin();it!=changes.end();++it) {if (it.value()==QJsonValue("__remove__")) payload.remove(it.key());else payload[it.key()]=it.value();}
            const auto body=QJsonDocument(payload).toJson(QJsonDocument::Compact);
            QByteArray message("monzero-dex-control-v1");message.append('\0');message+=body;
            QByteArray signature(crypto_sign_BYTES,'\0');
            crypto_sign_detached(reinterpret_cast<unsigned char *>(signature.data()),nullptr,reinterpret_cast<const unsigned char *>(message.constData()),message.size(),key);
            return QJsonDocument(QJsonObject{{"payload",QString::fromUtf8(body)},{"signature",QString::fromLatin1(signature.toHex())}}).toJson(QJsonDocument::Compact);
        };
        const QString t1="11111111-1111-4111-8111-111111111111",t2="22222222-2222-4222-8222-222222222222",t3="33333333-3333-4333-8333-333333333333";
        QTemporaryDir root;QVERIFY(root.isValid());
        {
            DexRiskControl regtest;QVERIFY(regtest.refusal(t1,1,1,now).isEmpty()); // local regtest: nothing to enforce
            QVERIFY(regtest.recordStart(t1,1,now));
            DexRiskControl unledgered;unledgered.setNetwork("ltc-testnet");
            QVERIFY(!unledgered.refusal(t1,1,1,now).isEmpty());QVERIFY(!unledgered.recordStart(t1,1,now)); // no ledger configured
            QVERIFY(regtest.setLedgerFile(root.filePath("regtest.json")));
            QVERIFY(regtest.refusal(t1,~0ULL,~0ULL,now).isEmpty()); // local regtest needs no operator document
            QVERIFY(!regtest.refusal("not-a-trade",1,1,now).isEmpty());
        }
        DexRiskControl control;control.setNetwork("ltc-testnet");QVERIFY(control.setLedgerFile(root.filePath("ledger.json")));
        QVERIFY(!control.refusal(t1,1,1,now).isEmpty()); // fail closed without a document
        QVERIFY(!control.acceptControlDocument(document({},sk),now)); // no key yet
        QVERIFY(!control.setPublicKey("xyz"));
        QVERIFY(control.setPublicKey(QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(pk),32).toHex())));
        QVERIFY(!control.acceptControlDocument(document({},otherSk),now));
        QVERIFY(!control.acceptControlDocument(document({{"network","ltc-mainnet"}},sk),now));
        QVERIFY(!control.acceptControlDocument(document({{"expiresAt",double(now-1)}},sk),now));
        QVERIFY(!control.acceptControlDocument(document({{"expiresAt",double(now+40*86400)}},sk),now));
        QVERIFY(!control.acceptControlDocument(document({{"issuedAt",double(now+3600)}},sk),now));
        QVERIFY(!control.acceptControlDocument(document({{"extra",1}},sk),now));
        QVERIFY(!control.acceptControlDocument(document({{"maxLtcAtomsPerDay","__remove__"}},sk),now));
        QVERIFY(!control.acceptControlDocument(document({{"maxLtcAtomsPerTrade",100}},sk),now));
        auto tampered=document({},sk);tampered.replace("100000000","900000000");QVERIFY(!control.acceptControlDocument(tampered,now));
        QVERIFY(!control.acceptControlDocument(document({},sk)+" ",now));
        QVERIFY(!control.refusal(t1,1,1,now).isEmpty());
        QVERIFY(control.acceptControlDocument(document({{"sequence",5},{"expiresAt",double(now+10*86400)}},sk),now));
        QVERIFY(control.status().value("controlValid").toBool());
        QVERIFY(control.refusal(t1,100000000,50000000000000,now).isEmpty());
        QVERIFY(!control.refusal(t1,100000001,1,now).isEmpty());
        QVERIFY(!control.refusal(t1,1,50000000000001,now).isEmpty());
        QVERIFY(control.recordStart(t1,100000000,now));QVERIFY(control.recordStart(t1,100000000,now)); // idempotent
        QVERIFY(control.recordStart(t2,100000000,now+10));
        QVERIFY(!control.refusal(t3,50000001,1,now+20).isEmpty()); // 24 h cap 250,000,000
        QVERIFY(control.refusal(t3,50000000,1,now+20).isEmpty());
        QVERIFY(control.refusal(t3,100000000,1,now+86400+11).isEmpty()); // window rolled
        // Kill switch: replay of an older "enabled" document is ignored.
        QVERIFY(control.acceptControlDocument(document({{"sequence",6},{"newTradesEnabled",false}},sk),now));
        QVERIFY(!control.refusal(t3,1,1,now).isEmpty());
        QVERIFY(!control.status().value("newTradesEnabled").toBool());
        QVERIFY(!control.acceptControlDocument(document({{"sequence",5}},sk),now));
        QVERIFY(!control.refusal(t3,1,1,now).isEmpty());
        QVERIFY(control.refusal(t1,100000000,1,now).isEmpty()); // started trade can always resume
        QVERIFY(!control.refusal(t3,1,1,now+86400+1).isEmpty()); // expired document pauses new trades
        // Unreadable ledger pauses new trades instead of resetting limits.
        QVERIFY(control.acceptControlDocument(document({{"sequence",7}},sk),now));
        QVERIFY(save(root.filePath("ledger.json"),"{broken"));
        QVERIFY(!control.refusal(t3,1,1,now).isEmpty());QVERIFY(!control.recordStart(t3,1,now));
        QCOMPARE(read(root.filePath("ledger.json")),QByteArray("{broken"));
        sodium_memzero(sk,sizeof sk);sodium_memzero(otherSk,sizeof otherSk);
    }
    void tradeFoldersDiscoverOneActiveTrade() {
        DexDeployment::resetForTesting();DexDeployment::lockLitecoinStage(); // ltc-regtest
        QTemporaryDir base;QVERIFY(base.isValid());const auto root=base.filePath("trades");
        const QString a="11111111-1111-4111-8111-111111111111",b="22222222-2222-4222-8222-222222222222",c="33333333-3333-4333-8333-333333333333";
        {DexEngine empty;QVERIFY(empty.openTradeFolders(root));QVERIFY(empty.tradeFolders().isEmpty());QVERIFY(empty.loadedTradeId().isEmpty());}
        QVERIFY(!DexTradeFolders::create(root,"../escape","LTC/XMZ","ltc-regtest","app.json"));
        QVERIFY(!DexTradeFolders::create(root,a,"BTC/XMZ","ltc-regtest","app.json"));
        QVERIFY(!DexTradeFolders::create(root,a,"LTC/XMZ","ltc-regtest","../app.json"));
        QVERIFY(DexTradeFolders::create(root,a,"LTC/XMZ","ltc-regtest","app.json"));
        QVERIFY(!DexTradeFolders::create(root,a,"LTC/XMZ","ltc-regtest","app.json")); // never overwrite
        QVERIFY(!(QFileInfo(QDir(root).filePath(a)).permissions() & (QFile::ReadGroup|QFile::ReadOther)));
        // The single active trade is opened; its (missing) app config is a hard failure, not a silent skip.
        {
            DexEngine one;QVERIFY(one.openTradeFolders(root));QVERIFY(one.loadedTradeId().isEmpty());QCOMPARE(one.tradeFolders().size(),1);
            QCOMPARE(one.tradeFolders().first().toMap().value("state").toString(),QString("load-failed"));
        }
        {DexEngine explicitly;QVERIFY(!explicitly.openTradeFolders(root,a));}
        QVERIFY(DexTradeFolders::create(root,b,"LTC/XMZ","ltc-testnet","app.json")); // other network ignored
        {DexEngine one;QVERIFY(one.openTradeFolders(root));QVERIFY(one.loadedTradeId().isEmpty());}
        QVERIFY(DexTradeFolders::create(root,c,"LTC/XMZ","ltc-regtest","app.json"));
        {
            DexEngine several;QVERIFY(several.openTradeFolders(root)); // two candidates: load nothing, list all
            QVERIFY(several.loadedTradeId().isEmpty());QCOMPARE(several.tradeFolders().size(),3);
            QVERIFY(!several.closeLoadedTrade(true));
        }
        {DexEngine wrong;QVERIFY(!wrong.openTradeFolders(root,b));} // requested trade on another network
        {DexEngine unknown;QVERIFY(!unknown.openTradeFolders(root,"44444444-4444-4444-8444-444444444444"));}
        QVERIFY(!DexTradeFolders::close(root,b+"x"));QVERIFY(DexTradeFolders::close(root,c));QVERIFY(!DexTradeFolders::close(root,c));
        QVERIFY(read(QDir(root).filePath(c+"/trade.json")).contains("\"state\":\"closed\""));
        QVERIFY(save(QDir(root).filePath(a+"/trade.json"),"{broken"));
        {
            DexEngine invalid;QVERIFY(invalid.openTradeFolders(root));QVERIFY(invalid.loadedTradeId().isEmpty());
            QStringList states;for (const auto &t:invalid.tradeFolders()) states<<t.toMap().value("state").toString();
            QCOMPARE(states,(QStringList{"invalid","active","closed"}));
        }
        QVERIFY(QDir(root).exists(a) && QDir(root).exists(c)); // nothing deleted
        DexDeployment::resetForTesting();
    }
    void preparedTradeReviewRejectsWithoutSideEffects() {
        QTemporaryDir temporary;QVERIFY(temporary.isValid());
        const auto root=temporary.filePath("trades");
        DexEngine engine;QVERIFY(engine.openTradeFolders(root));
        QVERIFY(engine.reviewLitecoinTradeSetup(temporary.filePath("missing.json")).isEmpty());
        QVERIFY(engine.reviewLitecoinTradeSetup("relative.json").isEmpty());
        QVERIFY(!engine.importLitecoinTradeSetup(temporary.filePath("missing.json"),QString(64,'a'),true));
        QVERIFY(!engine.importLitecoinTradeSetup(temporary.filePath("missing.json"),QString(64,'a'),false));
        QVERIFY(!QFileInfo::exists(root));
        QVERIFY(!engine.litecoinAppStatus().value("configured").toBool());
    }
    void preparedTradeBindingsSurviveCloseAndRejectChangedSetup() {
        QTemporaryDir temporary;QVERIFY(temporary.isValid());
        LitecoinAppConfig original,reordered,changed;
        original.sources.insert("/local/app.json","app");original.sources.insert("/local/quote.json","quote");
        reordered.sources.insert("/local/quote.json","quote");reordered.sources.insert("/local/app.json","app");
        QCOMPARE(original.digest(),reordered.digest());
        changed=original;changed.sources["/local/quote.json"]="changed";QVERIFY(changed.digest()!=original.digest());
        changed=original;changed.sources.insert("/local/other.json","app");QVERIFY(changed.digest()!=original.digest());
        const auto root=temporary.filePath("trades"),app=temporary.filePath("app.json");
        const QString id="11111111-1111-4111-8111-111111111111";
        QVERIFY(!DexTradeFolders::create(root,id,"LTC/XMZ","ltc-regtest",app,"invalid"));
        QVERIFY(!QFileInfo::exists(root));
        QVERIFY(DexTradeFolders::create(root,id,"LTC/XMZ","ltc-regtest",app,original.digest()));
        QVERIFY(!DexTradeFolders::create(root,id,"LTC/XMZ","ltc-regtest",app,changed.digest()));
        QCOMPARE(DexTradeFolders::scan(root).first().setupDigest,original.digest());
        {DexEngine engine;QVERIFY(engine.openTradeFolders(root));QVERIFY(engine.loadedTradeId().isEmpty());
         QCOMPARE(engine.tradeFolders().first().toMap().value("state").toString(),QString("load-failed"));}
        QVERIFY(DexTradeFolders::close(root,id));
        const auto closed=DexTradeFolders::scan(root).first();
        QCOMPARE(closed.setupDigest,original.digest());QCOMPARE(closed.state,QString("closed"));
    }
    void lightFundingSetupIsImmutableAndUnconfiguredActionsReject() {
        DexEngine engine;QVERIFY(!engine.useLitecoinLightWallet());QVERIFY(!engine.queryLitecoinLightTerms());
        QVERIFY(!engine.prepareLitecoinLightFunding("1000",1));QVERIFY(!engine.reopenLitecoinLightFunding());
        QVERIFY(!engine.reviewLitecoinLightSignature());QVERIFY(!engine.signLitecoinLightFunding(QString(64,'a'),"fixture"));
        QVERIFY(!engine.reviewLitecoinLightSend());QVERIFY(!engine.sendLitecoinLightFunding(QString(64,'a')));
        QVERIFY(!engine.observeLitecoinLightFunding());QVERIFY(!engine.reviewLitecoinLightRetry());QVERIFY(!engine.retryLitecoinLightFunding(QString(64,'a')));
        QTemporaryDir root;QVERIFY(root.isValid());const QString wallet=root.filePath("wallet.json"),other=root.filePath("other.json");
        for(const auto &path:{wallet,other}){QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write("{}"),qint64(2));QVERIFY(f.setPermissions(QFile::ReadOwner|QFile::WriteOwner));}
        const auto trade=QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QJsonObject context{{"schema","monzero-private-ltc-light-dex-v1"},{"tradeId",trade},{"metadataDirectory",root.path()},
            {"snapshotDirectory",root.path()},{"pins",QJsonArray{}},{"policy",QJsonObject{{"ltc_network","ltc-regtest"}}}};
        const auto path=LitecoinLightFundingSetup::retain(context,wallet);QVERIFY(!path.isEmpty());
        const auto bytes=LitecoinJournalProtocol::privateConfig(path,4096);QVERIFY(!bytes.isEmpty());
        QCOMPARE(LitecoinLightFundingSetup::retain(context,wallet),path);
        QVERIFY(LitecoinLightFundingSetup::retain(context,other).isEmpty());
        QCOMPARE(LitecoinJournalProtocol::privateConfig(path,4096),bytes);
        auto bad=context;bad.insert("tradeId","../elsewhere");QVERIFY(LitecoinLightFundingSetup::retain(bad,wallet).isEmpty());
    }
    void lightFundingContextRetainsIntentAndClaim() {
        using namespace LitecoinLightFundingContext;
        QTemporaryDir root;QVERIFY(root.isValid());const auto trade=QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString digest(64,'a'),txid(64,'b');Retained state;
        const QJsonObject payment{{"address","public-fixture"},{"amountAtomic","1000"},{"feeAtomic","10"},
            {"minimumChangeAtomic","100"},{"minimumConfirmations",1}};
        const QJsonObject review{{"signingCommitment",QString(64,'c')},{"changeAddress",QJsonValue::Null}};
        QVERIFY(access(root.path(),trade,digest,state));QVERIFY(state.payment.isEmpty());
        QVERIFY(!access(root.path(),trade,digest,state,payment,review,txid)); // Intent must predate the result.
        QVERIFY(access(root.path(),trade,digest,state,payment));QCOMPARE(state.payment,payment);QVERIFY(state.review.isEmpty());
        QVERIFY(access(root.path(),trade,digest,state,payment));
        auto different=payment;different.insert("feeAtomic","11");
        QVERIFY(!access(root.path(),trade,digest,state,different));
        QVERIFY(!access(root.path(),trade,QString(64,'d'),state));
        QVERIFY(access(root.path(),trade,digest,state,payment,review,txid));
        Retained restarted;QVERIFY(access(root.path(),trade,digest,restarted));
        QCOMPARE(restarted.payment,payment);QCOMPARE(restarted.review,review);QCOMPARE(restarted.txid,txid);
        QVERIFY(access(root.path(),trade,digest,state,payment,review,txid));
        QVERIFY(!access(root.path(),trade,digest,state,payment,review,QString(64,'e')));
        auto changed=review;changed.insert("signingCommitment",QString(64,'f'));
        QVERIFY(!access(root.path(),trade,digest,state,payment,changed,txid));
        const auto base=root.path()+"/ltc-light-dex-ui-"+trade;
        QFile record(base+"-payment.json");QVERIFY(record.open(QIODevice::ReadOnly));const auto bytes=record.readAll();record.close();
        QVERIFY(record.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ReadGroup));
        QVERIFY(!access(root.path(),trade,digest,state));QVERIFY(record.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
        QVERIFY(record.open(QIODevice::WriteOnly|QIODevice::Truncate));record.close();
        QVERIFY(!access(root.path(),trade,digest,state,payment)); // Partial records are never repaired silently.
        QVERIFY(record.open(QIODevice::WriteOnly));QCOMPARE(record.write(bytes),qint64(bytes.size()));record.close();
        QVERIFY(access(root.path(),trade,digest,state));
        QVERIFY(record.remove());QVERIFY(!access(root.path(),trade,digest,state)); // Orphan review.
        QVERIFY(!access(root.path(),trade,digest,state,payment));
        QCOMPARE(state.payment,payment); // A failed read never replaces the last known context.
    }
    void bchDesktopWalletBindingRequiresAdmissionAndImmutableSource() {
        Fixture fixture;QVERIFY(fixture.create());fixture.config["role"]="bch-funder";QVERIFY(fixture.writeConfig());
        const auto root=fixture.directory.path(),metadata=fixture.config.value("metadataDirectory").toString();
        QVERIFY(QDir().mkpath(metadata));QVERIFY(QFile::setPermissions(metadata,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        const auto walletPath=fixture.directory.filePath("wallet.json"),launcher=fixture.directory.filePath("launcher.json");
        const QJsonObject account{{"schema","monzero-bch-public-account-v1"},{"network","regtest"},{"account",0},
            {"masterFingerprint","00000000"},{"accountXpub","metadata-only-fixture"}};
        const QJsonObject wallet{{"schema","monzero-private-bch-light-v1"},{"network","regtest"},{"cacheDirectory",root},
            {"maximumHeight",1000},{"endpoint",QJsonObject{{"host","localhost"},{"port",12345}}},
            {"wallet",QJsonObject{{"account",account},{"vault",QJsonObject{{"directory",root},{"walletId",QUuid::createUuid().toString(QUuid::WithoutBraces)}}},
                {"spendDirectory",root},{"addressBookDirectory",root}}}};
        QVERIFY(save(walletPath,QJsonDocument(wallet).toJson(QJsonDocument::Compact)));
        const QJsonObject wrapper{{"schema","monzero-private-bch-light-launcher-v1"},{"helper",fixture.helper},
            {"helperDigest",fixture.config.value("helperDigest")},{"configFile",walletPath}};
        QVERIFY(save(launcher,QJsonDocument(wrapper).toJson(QJsonDocument::Compact)));
        DexEngine engine;QVERIFY(!engine.useBchLightWallet());QVERIFY(!engine.selectBchLightFundingSource());
        QVERIFY(!engine.queryBchLightTerms());QVERIFY(!engine.prepareBchLightFunding("1000",1));
        QVERIFY(!engine.reviewBchLightSignature());QVERIFY(!engine.signBchLightFunding(QString(64,'a'),"fixture"));
        QVERIFY(!engine.reviewBchLightRecovery());QVERIFY(!engine.retainBchLightRecovery(QString(64,'a')));
        QVERIFY(!engine.reviewBchLightSend());QVERIFY(!engine.sendBchLightFunding(QString(64,'a')));
        QVERIFY(!engine.observeBchLightFunding());QVERIFY(!engine.reviewBchLightRetry());
        QVERIFY(!engine.retryBchLightFunding(QString(64,'a')));QVERIFY(!engine.reconcileBchLightAcknowledgement());
        QVERIFY(engine.configureLightWallet("bch",launcher));
        QVERIFY(engine.configureLightWalletSetup(fixture.helper,fixture.config.value("helperDigest").toString()));
        QVERIFY(engine.configureBchAdmission(fixture.path));QVERIFY(!engine.useBchLightWallet());
        QVERIFY(engine.requestBchAdmissionCheck());QVERIFY(!engine.useBchLightWallet());
        QTRY_COMPARE(engine.bchAdmissionStatus().value("state").toString(),QString("admitted"));
        QVERIFY(engine.bchLightFundingStatus().value("selectionAvailable").toBool());
        bool reentered=false;QObject::connect(&engine,&DexEngine::bchLightFundingChanged,&engine,[&]{
            if(engine.bchLightFundingStatus().value("busy").toBool() && engine.selectBchLightFundingSource())reentered=true;
        });
        QVERIFY(engine.useBchLightWallet());QVERIFY(!reentered);QVERIFY(!engine.useBchLightWallet());
        QVERIFY(engine.bchLightFundingStatus().value("contextReady").toBool());
        const auto selected=metadata+"/"+fixture.config.value("tradeId").toString()+".bch-light-wallet.json";
        const auto bytes=read(selected);QVERIFY(!bytes.isEmpty());
        auto context=QJsonDocument::fromJson(bytes).object();QCOMPARE(context.take("walletConfigFile").toString(),walletPath);
        QCOMPARE(BchLightFundingSetup::retain(context,walletPath),selected);
        const auto other=fixture.directory.filePath("other-wallet.json");QVERIFY(save(other,read(walletPath)));
        QVERIFY(BchLightFundingSetup::retain(context,other).isEmpty());QCOMPARE(read(selected),bytes);
        auto wrong=context;wrong.remove("admittedAt");wrong["extra"]=1;
        QVERIFY(BchLightFundingSetup::retain(wrong,walletPath).isEmpty());
        const auto walletBytes=read(walletPath);QVERIFY(save(walletPath,walletBytes+" "));
        QVERIFY(!engine.bchLightFundingStatus().value("contextReady").toBool());
        QCOMPARE(engine.bchLightFundingStatus().value("state").toString(),QString("context-mismatch"));
        QVERIFY(!engine.selectBchLightFundingSource());QVERIFY(save(walletPath,walletBytes));
        QVERIFY(engine.bchLightFundingStatus().value("contextReady").toBool());
        const auto admission=read(fixture.path);QVERIFY(save(fixture.path,admission+" "));
        QVERIFY(!engine.bchLightFundingStatus().value("contextReady").toBool());QVERIFY(!engine.selectBchLightFundingSource());
        QVERIFY(!engine.observeBchLightFunding());QCOMPARE(read(selected),bytes);
        QVERIFY(save(fixture.path,admission));QVERIFY(engine.bchLightFundingStatus().value("contextReady").toBool());
        // Admission refresh temporarily disables all funding calls. A successful
        // recheck restores the existing binding without creating a second one.
        QVERIFY(engine.requestBchAdmissionCheck());QVERIFY(!engine.selectBchLightFundingSource());
        QTRY_COMPARE(engine.bchAdmissionStatus().value("state").toString(),QString("admitted"));
        QVERIFY(engine.bchLightFundingStatus().value("contextReady").toBool());QCOMPARE(read(selected),bytes);
    }
    void bchLightContextRequiresValidatedFunderStatus() {
        Fixture fixture;QVERIFY(fixture.create());fixture.config["role"]="bch-funder";QVERIFY(fixture.writeConfig());
        BchAdmissionJob job;QVERIFY(job.configure(fixture.path));QVERIFY(job.lightFundingContext().isEmpty());
        QVERIFY(job.review());QVERIFY(job.lightFundingContext().isEmpty());
        QTRY_COMPARE(job.status().value("state").toString(),QString("reviewed"));
        QVERIFY(job.lightFundingContext().isEmpty());
        QVERIFY(job.submit(job.status().value("review").toMap().value("commitment").toString()));
        QTRY_COMPARE(job.status().value("state").toString(),QString("admitted"));
        QVERIFY(job.lightFundingContext().isEmpty()); // Submit is not a checked historical status.
        QVERIFY(job.refresh());QVERIFY(job.lightFundingContext().isEmpty());
        QTRY_COMPARE(job.status().value("state").toString(),QString("admitted"));
        auto expected=fixture.config;expected.remove("helper");expected.remove("helperDigest");expected.remove("role");
        expected["schema"]="monzero-private-bch-light-dex-v1";
        QCOMPARE(job.lightFundingContext(),expected);QCOMPARE(expected.size(),10);
        const auto original=read(fixture.path);QVERIFY(save(fixture.path,original+" "));
        QVERIFY(job.lightFundingContext().isEmpty());QVERIFY(save(fixture.path,original));QCOMPARE(job.lightFundingContext(),expected);
        const auto quotePath=fixture.config.value("quoteFile").toString();const auto quoteBytes=read(quotePath);
        QVERIFY(save(quotePath,quoteBytes+" "));QVERIFY(job.lightFundingContext().isEmpty());QVERIFY(save(quotePath,quoteBytes));
        Fixture native;QVERIFY(native.create());BchAdmissionJob nativeJob;QVERIFY(nativeJob.configure(native.path));
        QVERIFY(nativeJob.refresh());QTRY_COMPARE(nativeJob.status().value("state").toString(),QString("admitted"));
        QVERIFY(nativeJob.lightFundingContext().isEmpty());
    }
    void lightWalletFundingSelectionAndLease_data() {
        QTest::addColumn<QString>("coin");
        QTest::newRow("ltc")<<QString("ltc");
        QTest::newRow("bch")<<QString("bch");
    }
    void lightWalletFundingSelectionAndLease() {
        QFETCH(QString,coin);
        QTemporaryDir root;QVERIFY(root.isValid());
        const auto write=[&](const QString &path,const QByteArray &bytes,QFile::Permissions mode) {
            QFile f(path);return f.open(QIODevice::WriteOnly|QIODevice::Truncate) && f.write(bytes)==bytes.size() && f.setPermissions(mode);
        };
        const auto privateMode=QFile::ReadOwner|QFile::WriteOwner;
        const QString helper=root.filePath("helper");const QByteArray executable("#!/bin/sh\nexit 1\n");
        QVERIFY(write(helper,executable,privateMode|QFile::ExeOwner));
        const auto digest=QString::fromLatin1(QCryptographicHash::hash(executable,QCryptographicHash::Sha256).toHex());
        const QString path=root.filePath("wallet.json"),launcher=root.filePath("launcher.json");
        const QJsonObject account{{"schema",QString("monzero-%1-public-account-v1").arg(coin)},{"network","regtest"},{"account",0},
            {"masterFingerprint","00000000"},{"accountXpub","metadata-only-fixture"}};
        const QJsonObject vault{{"directory",root.path()},{"walletId",QUuid::createUuid().toString(QUuid::WithoutBraces)}};
        const QJsonObject config{{"schema",QString("monzero-private-%1-light-v1").arg(coin)},{"network","regtest"},{"cacheDirectory",root.path()},
            {"maximumHeight",1000},{"endpoint",QJsonObject{{"host","localhost"},{"port",12345}}},
            {"wallet",QJsonObject{{"account",account},{"vault",vault},{"spendDirectory",root.path()},{"addressBookDirectory",root.path()}}}};
        const auto bytes=QJsonDocument(config).toJson(QJsonDocument::Compact);
        QVERIFY(write(path,bytes,privateMode));
        const auto wrapper=QJsonDocument(QJsonObject{{"schema",QString("monzero-private-%1-light-launcher-v1").arg(coin)},
            {"helper",helper},{"helperDigest",digest},{"configFile",path}}).toJson(QJsonDocument::Compact);
        QVERIFY(write(launcher,wrapper,privateMode));
        LitecoinLightJob job(coin);QVERIFY(job.configure(launcher,helper,digest));
        QString selected,program,pin;QByteArray selectedBytes;
        QVERIFY(job.fundingConfiguration(selected,selectedBytes,program,pin,coin));
        QCOMPARE(selected,path);QCOMPARE(selectedBytes,bytes);QCOMPARE(program,helper);QCOMPARE(pin,digest);
        QVERIFY(!job.fundingConfiguration(selected,selectedBytes,program,pin,coin=="ltc"?"bch":"ltc"));
        QVERIFY(!job.fundingConfiguration(selected,selectedBytes,program,pin,"btc"));
        QCOMPARE(job.fundingConfiguration(selected,selectedBytes,program,pin),coin=="ltc");
        QObject owner,other;QVERIFY(job.acquireFunding(&owner,coin));QVERIFY(!job.acquireFunding(&other,coin));
        QVERIFY(job.status().value("busy").toBool());QVERIFY(job.status().value("dexFundingBusy").toBool());
        QVERIFY(!job.status().value("walletReceiveAvailable").toBool());
        QVERIFY(!job.receiveCurrentWallet());QVERIFY(!job.refreshWallet(2,10,50));QVERIFY(!job.loadPaymentRequests());
        QVERIFY(job.fundingConfiguration(selected,selectedBytes,program,pin,coin));
        job.releaseFunding(&other);QVERIFY(job.status().value("busy").toBool());
        job.releaseFunding(&owner);QVERIFY(!job.status().value("busy").toBool());
        QVERIFY(write(launcher,wrapper+" ",privateMode));
        QVERIFY(!job.fundingConfiguration(selected,selectedBytes,program,pin,coin));
        QVERIFY(selected.isEmpty() && selectedBytes.isEmpty() && program.isEmpty() && pin.isEmpty());
        QVERIFY(!job.acquireFunding(&owner,coin));QVERIFY(write(launcher,wrapper,privateMode));
        QVERIFY(write(path,bytes+" ",privateMode));QVERIFY(!job.fundingConfiguration(selected,selectedBytes,program,pin,coin));
        QVERIFY(write(path,bytes,privateMode));QVERIFY(job.fundingConfiguration(selected,selectedBytes,program,pin,coin));
        QVERIFY(write(helper,executable+"# changed\n",privateMode|QFile::ExeOwner));
        QVERIFY(!job.fundingConfiguration(selected,selectedBytes,program,pin,coin));
    }
    void helperUpgradeRequiresBoundReviewAndPreservesOriginal() {
        QTemporaryDir root;QVERIFY(root.isValid());
        QSettings originalSettings(QSettings::IniFormat,QSettings::UserScope,"Monzero","LightWalletFolders");
        auto oldSettingsRoot=QFileInfo(originalSettings.fileName()).dir();QVERIFY(oldSettingsRoot.cdUp());
        struct RestoreSettings {QString path;~RestoreSettings(){QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,path);}} restoreSettings{oldSettingsRoot.absolutePath()};
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root.path()+"/preferences");
        const auto makeHelper=[&](const QString &name,const QByteArray &bytes) {
            QFile file(root.filePath(name));if(!file.open(QIODevice::WriteOnly))return QString{};
            file.write(bytes);file.close();file.setPermissions(QFile::ReadOwner|QFile::ExeOwner);return file.fileName();
        };
        const auto old=makeHelper("old","#!/bin/sh\nexit 91\n");
        const auto installed=makeHelper("installed","#!/bin/sh\nexit 92\n");
        const auto fingerprint=[](const QString &p){QFile f(p);f.open(QIODevice::ReadOnly);return LightWalletHelperUpgrade::hash(f.readAll());};
        const auto oldDigest=fingerprint(old),newDigest=fingerprint(installed);
        for(const auto &coin:QStringList{"ltc","btc","bch","doge"}) {
            const auto launcher=LightWalletSetup::prepare(coin,"regtest",root.path(),old,oldDigest,{},0);
            QVERIFY(!launcher.isEmpty());const auto directory=QFileInfo(launcher).absolutePath();
            const auto original=LitecoinJournalProtocol::privateConfig(launcher);
            const auto config=QJsonDocument::fromJson(original).object().value("configFile").toString();
            const auto settings=LitecoinJournalProtocol::privateConfig(config);
            const auto review=LightWalletHelperUpgrade::review(coin,directory,installed,newDigest);
            QVERIFY(!review.isEmpty());
            QVERIFY(LightWalletHelperUpgrade::apply(coin,directory,installed,newDigest,QString(64,'0')).isEmpty());
            QCOMPARE(LightWalletHelperUpgrade::selected(directory,installed,newDigest),launcher);
            QFile changed(config);QVERIFY(changed.open(QIODevice::WriteOnly|QIODevice::Truncate));
            auto value=QJsonDocument::fromJson(settings).object();value["accountIndex"]=1;changed.write(QJsonDocument(value).toJson(QJsonDocument::Compact));changed.close();
            QVERIFY(LightWalletHelperUpgrade::apply(coin,directory,installed,newDigest,review.value("commitment").toString()).isEmpty());
            QVERIFY(changed.open(QIODevice::WriteOnly|QIODevice::Truncate));changed.write(settings);changed.close();
            const auto migrated=LightWalletHelperUpgrade::apply(coin,directory,installed,newDigest,review.value("commitment").toString());
            QVERIFY(!migrated.isEmpty());QCOMPARE(LightWalletHelperUpgrade::selected(directory,installed,newDigest),migrated);
            QCOMPARE(LightWalletHelperUpgrade::apply(coin,directory,installed,newDigest,review.value("commitment").toString()),migrated);
            LitecoinLightJob job(coin);QVERIFY(job.configure(migrated,installed,newDigest));
            QCOMPARE(job.status().value("state").toString(),QString("setup-ready"));QVERIFY(!job.status().value("busy").toBool());
            QCOMPARE(LitecoinJournalProtocol::privateConfig(launcher),original);QCOMPARE(LitecoinJournalProtocol::privateConfig(config),settings);
            {
                DexEngine engine;QVERIFY(engine.configureLightWalletSetup(installed,newDigest));
                QCOMPARE(engine.reviewLightWalletHelperUpgrade(coin,directory),review.toVariantMap());
                QVERIFY(!engine.upgradeLightWalletHelper(coin,directory,QString(64,'0')));
                QVERIFY(engine.upgradeLightWalletHelper(coin,directory,review.value("commitment").toString()));
                QCOMPARE(engine.lightWallets().value(coin).toMap().value("walletFolder").toString(),directory);
            }
            {
                DexEngine restarted;QVERIFY(restarted.configureLightWalletSetup(installed,newDigest));
                QCOMPARE(restarted.lightWallets().value(coin).toMap().value("state").toString(),QString("setup-ready"));
                QVERIFY(!restarted.lightWalletSetupNotices().contains(coin));
            }
            QFile damaged(migrated);QVERIFY(damaged.open(QIODevice::WriteOnly|QIODevice::Truncate));damaged.write("{}");damaged.close();
            QVERIFY(LightWalletHelperUpgrade::apply(coin,directory,installed,newDigest,review.value("commitment").toString()).isEmpty());
            QCOMPARE(LightWalletHelperUpgrade::selected(directory,installed,newDigest),migrated);
            LitecoinLightJob reject(coin);QVERIFY(!reject.configure(migrated,installed,newDigest));
        }
    }
    void localWalletFoldersPersistAndReattachWithoutExecutingHelper() {
        QTemporaryDir root;QVERIFY(root.isValid());
        QSettings original(QSettings::IniFormat,QSettings::UserScope,"Monzero","LightWalletFolders");
        auto oldRoot=QFileInfo(original.fileName()).dir();QVERIFY(oldRoot.cdUp());
        struct RestoreSettingsPath {QString path;~RestoreSettingsPath(){QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,path);}} restore{oldRoot.absolutePath()};
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root.path()+"/preferences");
        const auto helper=root.filePath("helper");QFile executable(helper);QVERIFY(executable.open(QIODevice::WriteOnly));
        const QByteArray bytes="#!/bin/sh\nexit 91\n";QCOMPARE(executable.write(bytes),qint64(bytes.size()));executable.close();
        QVERIFY(executable.setPermissions(QFile::ReadOwner|QFile::ExeOwner));
        const auto digest=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
        QVariantMap folders;
        {
            DexEngine first;QVERIFY(first.configureLightWalletSetup(helper,digest));
            for(const auto &coin:QStringList{"ltc","btc","bch","doge"}) {
                QVERIFY(first.prepareLocalLightWallet(coin,"regtest",root.path(),QString{},0));
                const auto state=first.lightWallets().value(coin).toMap();
                QVERIFY(state.value("setupConfigured").toBool());QVERIFY(!state.value("busy").toBool());
                folders.insert(coin,state.value("walletFolder"));
            }
            QVERIFY(first.lightWalletSetupNotices().isEmpty());
        }
        QSettings saved(QSettings::IniFormat,QSettings::UserScope,"Monzero","LightWalletFolders");saved.sync();
        QCOMPARE(saved.allKeys().size(),4);
        for(auto it=folders.cbegin();it!=folders.cend();++it) QCOMPARE(saved.value("wallets/"+it.key()),it.value());
        {
            DexEngine restarted;QVERIFY(restarted.configureLightWalletSetup(helper,digest));
            for(auto it=folders.cbegin();it!=folders.cend();++it) {
                const auto state=restarted.lightWallets().value(it.key()).toMap();
                QCOMPARE(state.value("walletFolder"),it.value());QCOMPARE(state.value("state").toString(),QString("setup-ready"));
                QVERIFY(!state.value("busy").toBool());QVERIFY(!state.value("recoveryVisible").toBool());
                QVERIFY(QDir(it.value().toString()+"/vaultDirectory").isEmpty());
            }
            QVERIFY(restarted.lightWalletSetupNotices().isEmpty());
        }
        // Reopen from disk in a separate process, not just a new QObject.
        QProcess fresh;
        fresh.start(QCoreApplication::applicationFilePath(),{"--light-wallet-folders-fixture",root.path()+"/preferences",helper,digest});
        QVERIFY(fresh.waitForFinished(10000));QCOMPARE(fresh.exitCode(),0);QVERIFY(fresh.readAllStandardError().isEmpty());
        const auto restored=QJsonDocument::fromJson(fresh.readAllStandardOutput()).object();
        QCOMPARE(restored.size(),4);
        for(auto it=folders.cbegin();it!=folders.cend();++it) {
            const auto state=restored.value(it.key()).toObject();
            QCOMPARE(state.value("walletFolder").toString(),it.value().toString());
            QCOMPARE(state.value("state").toString(),QString("setup-ready"));
            QVERIFY(!state.value("busy").toBool());QVERIFY(!state.value("recoveryVisible").toBool());
        }
        saved.setValue("wallets/btc",root.filePath("missing"));saved.sync();
        {
            DexEngine missing;QVERIFY(missing.configureLightWalletSetup(helper,digest));
            QVERIFY(!missing.lightWallets().value("btc").toMap().value("configured").toBool());
            QVERIFY(missing.lightWalletSetupNotices().contains("btc"));
            QVERIFY(missing.lightWallets().value("ltc").toMap().value("configured").toBool());
            QVERIFY(missing.openLocalLightWallet("btc",folders.value("btc").toString()));
            QVERIFY(!missing.lightWalletSetupNotices().contains("btc"));
        }
        // Explicit startup selection wins over even an invalid remembered path.
        saved.setValue("wallets/btc",root.filePath("missing"));saved.sync();
        {
            DexEngine explicitWallet;QVERIFY(explicitWallet.configureLightWallet("btc",folders.value("btc").toString()+"/launcher.json"));
            QVERIFY(explicitWallet.configureLightWalletSetup(helper,digest));QVERIFY(!explicitWallet.lightWalletSetupNotices().contains("btc"));
        }
        const auto launcher=folders.value("doge").toString()+"/launcher.json";
        QFile config(launcher);QVERIFY(config.open(QIODevice::ReadOnly));auto wrapper=QJsonDocument::fromJson(config.readAll()).object();config.close();
        const auto alternate=root.filePath("alternate-helper");QVERIFY(QFile::copy(helper,alternate));
        wrapper.insert("helper",alternate);QVERIFY(config.open(QIODevice::WriteOnly|QIODevice::Truncate));config.write(QJsonDocument(wrapper).toJson(QJsonDocument::Compact));config.close();
        DexEngine substituted;QVERIFY(substituted.configureLightWalletSetup(helper,digest));
        QVERIFY(!substituted.lightWallets().value("doge").toMap().value("configured").toBool());
        QVERIFY(substituted.lightWalletSetupNotices().contains("doge"));
    }
    void bchNativeObservationRequiresSavedProofAndFreshCheck() {
        Fixture fixture;QVERIFY(fixture.create());fixture.config["role"]="bch-funder";
        const QByteArray helper=R"PY(#!/usr/bin/python3
import json,pathlib,sys,time,hashlib
request=json.load(sys.stdin);root=pathlib.Path(__file__).parent
assert set(request)=={'schema','requestId'}
with (root/'calls').open('a') as f:f.write(sys.argv[1]+'\n')
c=json.loads((root/'config.json').read_text());terms=json.loads(pathlib.Path(c['quoteFile']).read_text())['terms']
context={'bchFundingOutpoint':'a'*64+':0','destination':'B'*95,'admission':'1'*64,'agreement':'4'*64}
details={'schema':'monzero-bch-native-proof-review-v1','action':'retain-native-payment-proof',
 'swapId':c['tradeId'],'quote':list(bytes.fromhex(c['quoteCommitment'])),'agreement':[68]*32,'admission':[17]*32,
 'bchFundingOutpoint':context['bchFundingOutpoint'],'genesis':terms['xmzGenesis'],'destination':context['destination'],
 'amountAtoms':terms['xmzAtoms'],'txid':'b'*64,'message':[2]*32,'signature':[3]*32}
compact=lambda v:json.dumps(v,sort_keys=True,separators=(',',':')).encode()
review={'details':details,'commitment':hashlib.sha256(b'monzero-bch-native-proof-review-v1\0'+compact(details)).hexdigest()}
base={'requestId':request['requestId'],'tradeId':c['tradeId'],'executable':False,'chainObserved':False}
time.sleep(0.05)
if sys.argv[1]=='--native-proof-context-stdio-v1':base.update(schema='monzero-bch-native-proof-context-response-v1',context=context)
elif sys.argv[1]=='--native-proof-status-stdio-v1':base.update(schema='monzero-bch-native-proof-status-response-v1',review=review,receipt={'state':'retained','receivedAt':str(c['admittedAt'])})
elif sys.argv[1]=='--native-observation-stdio-v1':
 base.update(schema='monzero-bch-native-observation-response-v1',chainObserved=True,review=review,
  observation={'state':'confirmed','txid':details['txid'],'destination':details['destination'],
   'amountAtoms':details['amountAtoms'],'genesis':details['genesis'],'tip':'c'*64,'height':'8','tipHeight':'10',
   'confirmations':'3','observedAt':str(int(time.time()))})
else:sys.exit(1)
sys.stdout.buffer.write(compact(base))
)PY";
        QVERIFY(save(fixture.helper,helper,true));fixture.config["helperDigest"]=QString::fromLatin1(QCryptographicHash::hash(helper,QCryptographicHash::Sha256).toHex());QVERIFY(fixture.writeConfig());
        const auto proof=fixture.directory.filePath("proof.json"),observer=fixture.directory.filePath("observer.json");
        QVERIFY(save(proof,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-native-proof-v1"},
            {"admissionConfigFile",fixture.path},{"peerMessageFile",fixture.directory.filePath("absent-peer")}}).toJson(QJsonDocument::Compact)));
        QVERIFY(save(observer,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-native-observation-v1"},
            {"admissionConfigFile",fixture.path},{"nativeRpcFile",fixture.directory.filePath("absent-rpc")}}).toJson(QJsonDocument::Compact)));
        DexEngine engine;QVERIFY(engine.configureBchNativeProof(proof));QVERIFY(engine.configureBchNativeObservation(observer));
        QVERIFY(!engine.requestBchNativeObservation());QVERIFY(!QFile::exists(fixture.log));
        engine.requestBchStartupChecks();QVERIFY(!engine.requestBchNativeObservation());
        QTRY_COMPARE(engine.bchNativeProofStatus().value("state").toString(),QString("saved"));
        QCOMPARE(read(fixture.log),QByteArray("--native-proof-context-stdio-v1\n--native-proof-status-stdio-v1\n"));
        QVERIFY(engine.requestBchNativeObservation());QVERIFY(!engine.requestBchNativeProofCheck());
        QTRY_COMPARE(engine.bchNativeObservationStatus().value("state").toString(),QString("confirmed"));
        QVERIFY(engine.bchNativeObservationStatus().value("chainObserved").toBool());
        QVERIFY(engine.requestBchNativeProofCheck());QVERIFY(!engine.bchNativeObservationStatus().value("chainObserved").toBool());
        QTRY_COMPARE(engine.bchNativeProofStatus().value("state").toString(),QString("saved"));
        QVERIFY(engine.bchNativeObservationStatus().value("observation").toMap().isEmpty());
        QVERIFY(engine.requestBchNativeObservation());QTRY_COMPARE(engine.bchNativeObservationStatus().value("state").toString(),QString("confirmed"));
    }
    void bchPublicationRepairRequiresReviewAndExcludesOtherOperations() {
        PublicationRepairFixture fixture;QVERIFY(fixture.createRepair());DexEngine engine;
        QVERIFY(engine.configureBchPublicationRepair(fixture.wrapper));
        QVERIFY(!engine.submitBchPublicationRepair(QString(64,'0')));QVERIFY(read(fixture.log).isEmpty());
        QVERIFY(engine.requestBchPublicationRepairReview("prepared"));
        QVERIFY(!engine.requestBchPublicationRepairReview("signed"));
        QVERIFY(!engine.requestBchNativeProofCheck());
        QTRY_COMPARE(engine.bchPublicationRepairStatus().value("state").toString(),QString("reviewed"));
        const auto approval=engine.bchPublicationRepairStatus().value("review").toMap().value("commitment").toString();
        QVERIFY(engine.submitBchPublicationRepair(approval));
        QVERIFY(!engine.submitBchPublicationRepair(approval));QVERIFY(!engine.requestBchNativeProofCheck());
        QTRY_COMPARE(engine.bchPublicationRepairStatus().value("state").toString(),QString("repaired"));
        QVERIFY(!engine.bchPublicationRepairStatus().value("chainObserved").toBool());
        QCOMPARE(read(fixture.log),QByteArray("review\nsubmit\n"));
        QVERIFY(engine.requestBchPublicationRepairReview("signed"));
        QTRY_COMPARE(engine.bchPublicationRepairStatus().value("state").toString(),QString("reviewed"));
        QVERIFY(!engine.submitBchPublicationRepair(approval));
        QCOMPARE(engine.bchPublicationRepairStatus().value("state").toString(),QString("unknown"));
    }
    void bchSuccessReleaseReviewRequiresSavedProofAndExplicitRequest() {
        Context fixture;QVERIFY(fixture.create());
        QVERIFY(save(fixture.config.value("quoteFile").toString(),QJsonDocument(fixture.record).toJson(QJsonDocument::Compact)));
        const QByteArray helper=R"PY(#!/usr/bin/python3
import json,pathlib,sys,time,hashlib
request=json.load(sys.stdin);root=pathlib.Path(__file__).parent
assert set(request)==({'schema','requestId','commitment'} if sys.argv[1]=='--success-preparation-stdio-v1' else {'schema','requestId'})
with (root/'calls').open('a') as f:f.write(sys.argv[1]+'\n')
c=json.loads((root/'config.json').read_text());terms=json.loads(pathlib.Path(c['quoteFile']).read_text())['terms']
context={'bchFundingOutpoint':'a'*64+':0','destination':'B'*95,'admission':'1'*64,'agreement':'4'*64}
details={'schema':'monzero-bch-native-proof-review-v1','action':'retain-native-payment-proof',
 'swapId':c['tradeId'],'quote':list(bytes.fromhex(c['quoteCommitment'])),'agreement':[68]*32,'admission':[17]*32,
 'bchFundingOutpoint':context['bchFundingOutpoint'],'genesis':terms['xmzGenesis'],'destination':context['destination'],
 'amountAtoms':terms['xmzAtoms'],'txid':'b'*64,'message':[2]*32,'signature':[3]*32}
compact=lambda v:json.dumps(v,sort_keys=True,separators=(',',':')).encode()
review={'details':details,'commitment':hashlib.sha256(b'monzero-bch-native-proof-review-v1\0'+compact(details)).hexdigest()}
base={'requestId':request['requestId'],'tradeId':c['tradeId'],'executable':False,'chainObserved':False}
time.sleep(0.05)
if sys.argv[1]=='--native-proof-context-stdio-v1':base.update(schema='monzero-bch-native-proof-context-response-v1',context=context)
elif sys.argv[1]=='--native-proof-status-stdio-v1':base.update(schema='monzero-bch-native-proof-status-response-v1',review=review,receipt={'state':'retained','receivedAt':str(c['admittedAt'])})
elif sys.argv[1] in ('--success-release-review-stdio-v1','--success-preparation-stdio-v1','--success-preparation-status-stdio-v1','--success-signature-review-stdio-v1','--success-signed-release-review-stdio-v1'):
 d={'schema':'monzero-bch-success-release-review-v1','action':'prepare-bch-success-release',
  'swapId':c['tradeId'],'quote':details['quote'],'agreement':details['agreement'],'admission':details['admission'],
  'nativeProof':[9]*32,'bchDeployment':[119]*32,'bchFundingOutpoint':details['bchFundingOutpoint'],
  'bchAtoms':terms['bchAtoms'],'bchPayoutScript':terms['bchPayoutScript'],
  'bchPayoutAtoms':str(int(terms['bchAtoms'])-int(terms['bchLockFeeAtoms'])),'bchFeeAtoms':terms['bchLockFeeAtoms'],
  'nativeTxid':details['txid'],'nativeDestination':details['destination'],'nativeAmountAtoms':details['amountAtoms'],
  'minimumBchConfirmations':int(terms['bchConfirmations']),'minimumNativeConfirmations':int(terms['xmzConfirmations']),
  'cancellationMarginBlocks':5}
 operation='signature' if sys.argv[1]=='--success-signature-review-stdio-v1' else 'signed-release' if sys.argv[1]=='--success-signed-release-review-stdio-v1' else 'release'
 if operation!='release':d['action']='retain-bch-success-signature' if operation=='signature' else 'release-signed-bch-success'
 base.update(schema=f'monzero-bch-success-{operation}-review-response-v1',review={'details':d,
  'commitment':hashlib.sha256(b'monzero-bch-success-release-review-v1\0'+compact(d)).hexdigest()})
 if sys.argv[1]=='--success-preparation-stdio-v1':
  assert request['commitment']==base['review']['commitment']
  (root/'prepared').write_text('prepared')
  base.update(schema='monzero-bch-success-preparation-response-v1',state='prepared',chainObserved=True)
 elif sys.argv[1]=='--success-preparation-status-stdio-v1':
  base.update(schema='monzero-bch-success-preparation-status-response-v1',preparation=
   {'state':'prepared','preparedAt':str(int(time.time())),'signatureRetained':False} if (root/'prepared').exists() else None)
elif sys.argv[1]=='--native-observation-stdio-v1':
 base.update(schema='monzero-bch-native-observation-response-v1',chainObserved=True,review=review,
  observation={'state':'confirmed','txid':details['txid'],'destination':details['destination'],
   'amountAtoms':details['amountAtoms'],'genesis':details['genesis'],'tip':'c'*64,'height':'8','tipHeight':'10',
   'confirmations':'3','observedAt':str(int(time.time()))})
else:sys.exit(1)
sys.stdout.buffer.write(compact(base))
)PY";
        QVERIFY(save(fixture.helper,helper,true));fixture.config["helperDigest"]=QString::fromLatin1(QCryptographicHash::hash(helper,QCryptographicHash::Sha256).toHex());QVERIFY(fixture.writeConfig());
        const auto proof=fixture.directory.filePath("proof.json"),observer=fixture.directory.filePath("observer.json");
        QVERIFY(save(proof,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-native-proof-v1"},
            {"admissionConfigFile",fixture.path},{"peerMessageFile",fixture.directory.filePath("absent-peer")}}).toJson(QJsonDocument::Compact)));
        QVERIFY(save(observer,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-native-observation-v1"},
            {"admissionConfigFile",fixture.path},{"nativeRpcFile",fixture.directory.filePath("absent-rpc")}}).toJson(QJsonDocument::Compact)));
        const auto release=fixture.directory.filePath("release.json");
        QVERIFY(save(release,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-success-release-review-v1"},
            {"admissionConfigFile",fixture.path},{"deploymentCommitment",QString(64,'7')},{"safetyBlocks",5}}).toJson(QJsonDocument::Compact)));
        const auto preparation=fixture.directory.filePath("success-preparation.json");
        const auto bchRpc=fixture.directory.filePath("bch-rpc.json"),nativeRpc=fixture.directory.filePath("native-rpc.json");
        QVERIFY(save(bchRpc,"{}"));QVERIFY(save(nativeRpc,"{}"));
        QVERIFY(save(preparation,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-success-preparation-v1"},
            {"admissionConfigFile",fixture.path},{"deploymentCommitment",QString(64,'7')},{"safetyBlocks",5},
            {"bchRpcFile",bchRpc},{"nativeRpcFile",nativeRpc}}).toJson(QJsonDocument::Compact)));
        DexEngine engine;QVERIFY(engine.configureBchNativeProof(proof));QVERIFY(engine.configureBchNativeObservation(observer));
        QVERIFY(engine.configureBchSuccessPreparation(preparation));QVERIFY(!engine.requestBchSuccessPreparationCheck());QVERIFY(!engine.submitBchSuccessPreparation(QString(64,'0')));
        QVERIFY(engine.configureBchSuccessReleaseReview(release));QVERIFY(!engine.requestBchSuccessReleaseReview());
        QVERIFY(engine.configureBchSuccessSignatureReview(release));QVERIFY(!engine.requestBchSuccessSignatureReview());
        QVERIFY(engine.configureBchSuccessSignedReleaseReview(release));QVERIFY(!engine.requestBchSuccessSignedReleaseReview());
        QSignalSpy signingChanges(&engine,&DexEngine::bchSuccessSignatureReviewChanged);
        QSignalSpy releaseChanges(&engine,&DexEngine::bchSuccessSignedReleaseReviewChanged);
        QVERIFY(!engine.requestBchNativeObservation());QVERIFY(!QFile::exists(fixture.log));
        engine.requestBchStartupChecks();QVERIFY(!engine.requestBchNativeObservation());
        QTRY_COMPARE(engine.bchNativeProofStatus().value("state").toString(),QString("saved"));
        QCOMPARE(read(fixture.log),QByteArray("--native-proof-context-stdio-v1\n--native-proof-status-stdio-v1\n"));
        QVERIFY(engine.requestBchSuccessPreparationCheck());QVERIFY(!engine.requestBchSuccessReleaseReview());
        QTRY_COMPARE(engine.bchSuccessPreparationStatus().value("state").toString(),QString("checked"));
        QVERIFY(engine.bchSuccessPreparationStatus().value("statusKnown").toBool());
        QVERIFY(!engine.bchSuccessPreparationStatus().value("attempted").toBool());
        QVERIFY(engine.requestBchSuccessReleaseReview());QVERIFY(!engine.requestBchNativeProofCheck());
        QVERIFY(!engine.requestBchNativeObservation());
        QTRY_COMPARE(engine.bchSuccessReleaseReviewStatus().value("state").toString(),QString("reviewed"));
        const auto commitment=engine.bchSuccessReleaseReviewStatus().value("review").toMap().value("commitment").toString();
        const auto calls=read(fixture.log);QVERIFY(!engine.submitBchSuccessPreparation(QString(64,'0')));QCOMPARE(read(fixture.log),calls);
        QVERIFY(engine.submitBchSuccessPreparation(commitment));QVERIFY(!engine.requestBchNativeProofCheck());
        QVERIFY(!engine.requestBchSuccessReleaseReview());QVERIFY(!engine.requestBchNativeObservation());
        QTRY_COMPARE(engine.bchSuccessPreparationStatus().value("state").toString(),QString("prepared"));
        QVERIFY(engine.bchSuccessPreparationStatus().value("attempted").toBool());
        QVERIFY(engine.requestBchSuccessSignatureReview());
        QVERIFY(!engine.requestBchSuccessSignedReleaseReview());QVERIFY(!engine.requestBchSuccessReleaseReview());
        QVERIFY(!engine.requestBchNativeProofCheck());QVERIFY(!engine.requestBchSuccessPreparationCheck());
        QVERIFY(!engine.bchSuccessSignedReleaseReviewStatus().value("reviewAvailable").toBool());
        QTRY_COMPARE(engine.bchSuccessSignatureReviewStatus().value("state").toString(),QString("reviewed"));
        const auto signingCommitment=engine.bchSuccessSignatureReviewStatus().value("review").toMap().value("commitment").toString();
        QVERIFY(signingCommitment!=commitment);
        QVERIFY(engine.requestBchSuccessSignedReleaseReview());QVERIFY(!engine.requestBchSuccessSignatureReview());
        QVERIFY(!engine.requestBchNativeObservation());QVERIFY(!engine.submitBchSuccessPreparation(commitment));
        QTRY_COMPARE(engine.bchSuccessSignedReleaseReviewStatus().value("state").toString(),QString("reviewed"));
        const auto signedCommitment=engine.bchSuccessSignedReleaseReviewStatus().value("review").toMap().value("commitment").toString();
        QVERIFY(signedCommitment!=commitment && signedCommitment!=signingCommitment);
        QVERIFY(!engine.bchSuccessSignedReleaseReviewStatus().value("chainObserved").toBool());
        QVERIFY(signingChanges.count()>0 && releaseChanges.count()>0);
        QVERIFY(engine.requestBchSuccessPreparationCheck());QVERIFY(!engine.requestBchNativeObservation());
        QVERIFY(engine.bchSuccessSignatureReviewStatus().value("review").toMap().isEmpty());
        QVERIFY(engine.bchSuccessSignedReleaseReviewStatus().value("review").toMap().isEmpty());
        QTRY_COMPARE(engine.bchSuccessPreparationStatus().value("state").toString(),QString("saved"));
        QVERIFY(!engine.bchSuccessPreparationStatus().value("chainObserved").toBool());
        QVERIFY(engine.requestBchNativeObservation());QVERIFY(!engine.requestBchNativeProofCheck());
        QVERIFY(!engine.requestBchSuccessReleaseReview());
        QTRY_COMPARE(engine.bchNativeObservationStatus().value("state").toString(),QString("confirmed"));
        QVERIFY(engine.bchNativeObservationStatus().value("chainObserved").toBool());
        QVERIFY(engine.requestBchSuccessSignatureReview());
        QTRY_COMPARE(engine.bchSuccessSignatureReviewStatus().value("state").toString(),QString("reviewed"));
        QVERIFY(engine.requestBchSuccessSignedReleaseReview());
        QTRY_COMPARE(engine.bchSuccessSignedReleaseReviewStatus().value("state").toString(),QString("reviewed"));
        QVERIFY(engine.requestBchNativeProofCheck());QVERIFY(!engine.bchNativeObservationStatus().value("chainObserved").toBool());
        QVERIFY(engine.bchSuccessSignatureReviewStatus().value("review").toMap().isEmpty());
        QVERIFY(engine.bchSuccessSignedReleaseReviewStatus().value("review").toMap().isEmpty());
        QVERIFY(engine.bchSuccessReleaseReviewStatus().value("review").toMap().isEmpty());
        QVERIFY(engine.bchSuccessPreparationStatus().value("review").toMap().isEmpty());
        QVERIFY(!engine.bchSuccessPreparationStatus().value("chainObserved").toBool());
        QVERIFY(!engine.submitBchSuccessPreparation(commitment));
        QTRY_COMPARE(engine.bchNativeProofStatus().value("state").toString(),QString("saved"));
        QVERIFY(engine.bchNativeObservationStatus().value("observation").toMap().isEmpty());
        QVERIFY(engine.requestBchNativeObservation());QTRY_COMPARE(engine.bchNativeObservationStatus().value("state").toString(),QString("confirmed"));
        QVERIFY(engine.requestBchSuccessReleaseReview());QTRY_COMPARE(engine.bchSuccessReleaseReviewStatus().value("state").toString(),QString("reviewed"));
        QVERIFY(QFile::remove(bchRpc));QVERIFY(QFile::remove(nativeRpc));
        QVERIFY(engine.requestBchSuccessPreparationCheck());
        QTRY_COMPARE(engine.bchSuccessPreparationStatus().value("state").toString(),QString("saved"));
        const auto restoredCalls=read(fixture.log);QVERIFY(!engine.submitBchSuccessPreparation(commitment));QCOMPARE(read(fixture.log),restoredCalls);
    }
    void bchPunishmentSubmissionRequiresFreshReview() {
        PunishmentWorkerContext c;QVERIFY(c.create());QVERIFY(c.submissionFiles());DexEngine engine;
        QVERIFY(!engine.configureBchPunishmentSubmission(c.submissionPath));QVERIFY(engine.configureBchPunishment(c.wrapper));
        QVERIFY(engine.configureBchPunishmentObservation(c.observationPath));QVERIFY(engine.configureBchPunishmentSubmission(c.submissionPath));
        engine.requestBchStartupChecks();QTRY_COMPARE(engine.bchPunishmentStatus().value("state").toString(),QString("checked"));
        QVERIFY(!engine.submitBchPunishment(QString(64,'0')));QVERIFY(engine.requestBchPunishmentReview());
        QTRY_COMPARE(engine.bchPunishmentStatus().value("state").toString(),QString("reviewed"));
        const auto approval=engine.bchPunishmentStatus().value("review").toMap().value("commitment").toString();
        QVERIFY(!engine.submitBchPunishment(QString(64,'0')));QVERIFY(engine.submitBchPunishment(approval));
        QVERIFY(!engine.submitBchPunishment(approval));QVERIFY(!engine.observeBchPunishment());
        QTRY_COMPARE(engine.bchPunishmentStatus().value("state").toString(),QString("acknowledged"));
        QVERIFY(!engine.submitBchPunishment(approval));QVERIFY(!engine.bchPunishmentStatus().value("chainObserved").toBool());
        QCOMPARE(read(c.log).count("--punish-submit-stdio-v1"),1);
    }
    void bchPunishmentObservationIsExplicit() {
        PunishmentWorkerContext c;QVERIFY(c.create());QVERIFY(c.observationFiles());DexEngine engine;
        QVERIFY(!engine.configureBchPunishmentObservation(c.observationPath));
        QVERIFY(engine.configureBchPunishment(c.wrapper));QVERIFY(engine.configureBchPunishmentObservation(c.observationPath));
        engine.requestBchStartupChecks();QTRY_COMPARE(engine.bchPunishmentStatus().value("state").toString(),QString("checked"));
        QCOMPARE(read(c.log),QByteArray("--punish-status-stdio-v1\n"));QVERIFY(!engine.bchPunishmentStatus().value("chainObserved").toBool());
        QVERIFY(engine.observeBchPunishment());QVERIFY(!engine.requestBchPunishmentCheck());
        QTRY_VERIFY(!engine.bchPunishmentStatus().value("busy").toBool());QVERIFY(engine.bchPunishmentStatus().value("chainObserved").toBool());
        QVERIFY(!engine.bchPunishmentStatus().value("submitAvailable").toBool());
    }
    void boundWalletServicePreservesTradeDirectory() {
        QTemporaryDir directory;QVERIFY(directory.isValid());
        QVERIFY(QFile::setPermissions(directory.path(),QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const auto program=QCoreApplication::applicationFilePath();
        const auto digest=QString::fromLatin1(QCryptographicHash::hash(read(program),QCryptographicHash::Sha256).toHex());
        const QString trade="11111111-1111-4111-8111-111111111111",other="22222222-2222-4222-8222-222222222222";
        const auto binding=directory.filePath("service.binding"),state=directory.filePath("service.state");
        {
            SwapWalletRpcProcess first;QSignalSpy stopped(&first,&SwapWalletRpcProcess::stopped);
            QVERIFY(!first.startBoundWithDigest(program,digest,directory.path(),"invalid",1,6));
            QVERIFY(first.startBoundWithDigest(program,digest,directory.path(),trade,1,6));
            QTRY_VERIFY(first.walletOperationAvailable());
            QVERIFY(!LitecoinJournalProtocol::privateConfig(binding).isEmpty());
            QVERIFY(save(directory.filePath("wallets/retained-wallet"),"fixture-wallet"));
            SwapWalletRpcProcess concurrent;QVERIFY(!concurrent.startBoundWithDigest(program,digest,directory.path(),trade,1,6));
            QTRY_COMPARE(stopped.count(),1);QVERIFY(stopped.at(0).at(0).toBool());
        }
        const auto original=read(binding);
        {
            SwapWalletRpcProcess wrong,changed,unbound;
            QVERIFY(!wrong.startBoundWithDigest(program,digest,directory.path(),other,1,6));
            QVERIFY(!changed.startBoundWithDigest(program,digest,directory.path(),trade,1,7));
            QVERIFY(!unbound.resumeCleanWithDigest(program,digest,directory.path(),1,6));
            QCOMPARE(read(binding),original);
        }
        {
            SwapWalletRpcProcess resumed;QSignalSpy stopped(&resumed,&SwapWalletRpcProcess::stopped);
            QVERIFY(resumed.startBoundWithDigest(program,digest,directory.path(),trade,1,6));
            QTRY_VERIFY(resumed.walletOperationAvailable());
            QCOMPARE(resumed.dataDirectory(),directory.path());
            QCOMPARE(read(directory.filePath("wallets/retained-wallet")),QByteArray("fixture-wallet"));
            QVERIFY(!resumed.beginWalletOperation().isEmpty());
            QTRY_COMPARE(stopped.count(),1);QVERIFY(!stopped.at(0).at(0).toBool());
        }
        SwapWalletRpcProcess uncertain;QVERIFY(!uncertain.startBoundWithDigest(program,digest,directory.path(),trade,1,6));
        QCOMPARE(read(binding),original);QVERIFY(read(state).contains("recovery-required"));
        for (const auto &interrupted:{QByteArray("active"),QByteArray("operation-pending"),QByteArray("invalid")}) {
            QVERIFY(save(state,"monzero-swap-rpc-v1\n"+interrupted+"\n"));
            SwapWalletRpcProcess blocked;QVERIFY(!blocked.startBoundWithDigest(program,digest,directory.path(),trade,1,6));
            QCOMPARE(read(directory.filePath("wallets/retained-wallet")),QByteArray("fixture-wallet"));
        }
        QVERIFY(QFile::remove(state));
        SwapWalletRpcProcess partial;QVERIFY(!partial.startBoundWithDigest(program,digest,directory.path(),trade,1,6));
        QVERIFY(save(state,"monzero-swap-rpc-v1\nstopped\n"));QVERIFY(save(binding,original+" "));
        SwapWalletRpcProcess damaged;QVERIFY(!damaged.startBoundWithDigest(program,digest,directory.path(),trade,1,6));
        QCOMPARE(read(binding),original+" ");
        QTemporaryDir occupied;QVERIFY(occupied.isValid());
        QVERIFY(save(occupied.filePath("existing-wallet"),"retained"));
        SwapWalletRpcProcess fresh;QVERIFY(!fresh.startBoundWithDigest(program,digest,occupied.path(),trade,1,6));
        QVERIFY(!QFileInfo::exists(occupied.filePath("service.binding")));
        QCOMPARE(read(occupied.filePath("existing-wallet")),QByteArray("retained"));
    }
    void bchRefundWalletServiceCoordinator() {
        RefundWalletWorkerContext c;QVERIFY(c.create());
        const auto trade=c.config.value("tradeId").toString();QVERIFY(!QUuid(trade).isNull());
        const auto program=QCoreApplication::applicationFilePath();
        const auto digest=QString::fromLatin1(QCryptographicHash::hash(read(program),QCryptographicHash::Sha256).toHex());
        auto write=[&](const QString &path,QJsonObject value){return save(path,QJsonDocument(value).toJson(QJsonDocument::Compact));};
        const auto bch=c.directory.filePath("bch.json"),observer=c.directory.filePath("observer.json");
        QVERIFY(write(bch,{{"schema","monzero-private-bch-observer-rpc-v1"},{"port",18443},{"cookieFile",bch+".cookie"},{"checkpointHeight",1},{"checkpoint",QString(64,'7')},{"subversion","/Bitcoin Cash Node:29.0.0/"}}));
        QVERIFY(write(observer,{{"schema","monzero-private-bch-native-rpc-v1"},{"daemonPort",1},{"walletPort",7},{"loginFile",observer+".login"}}));
        const auto directory=c.directory.filePath("refund-service"),config=c.directory.filePath("refund-service.json");
        auto service=[&](const QJsonObject &changes){
            QJsonObject value{{"schema","monzero-private-bch-refund-wallet-service-v1"},{"tradeId",trade},{"program",program},
                {"programSha256",digest},{"directory",directory},{"daemonPort",1},{"walletPort",6},{"bchRpcFile",bch},{"observerRpcFile",observer}};
            for (auto it=changes.begin();it!=changes.end();++it) value[it.key()]=it.value();
            return write(config,value);
        };
        auto state=[](DexEngine &engine){return engine.bchRefundWalletStatus().value("serviceState").toString();};
        {
            DexEngine engine;QVERIFY(service({}));
            QVERIFY(!engine.configureBchRefundWalletService(config)); // no status configuration yet
            QVERIFY(engine.configureBchRefundWallet(c.wrapper));
            QCOMPARE(state(engine),QString("unconfigured"));
            for (const auto &bad:{QJsonObject{{"tradeId","22222222-2222-4222-8222-222222222222"}},QJsonObject{{"walletPort",1}},
                QJsonObject{{"programSha256","x"}},QJsonObject{{"directory","relative"}},QJsonObject{{"observerRpcFile",bch}},
                QJsonObject{{"directory",directory+"/"}},QJsonObject{{"extra",true}}}) {
                QVERIFY(service(bad));QVERIFY(!engine.configureBchRefundWalletService(config));
            }
            QVERIFY(!QFileInfo::exists(directory));
            QVERIFY(service({}));QVERIFY(engine.configureBchRefundWalletService(config));
            QVERIFY(!engine.configureBchRefundWalletService(config));
            QTRY_COMPARE(state(engine),QString("attached"));
            QVERIFY(engine.bchRefundWalletStatus().value("serviceOwned").toBool());
            QVERIFY(engine.bchRefundWalletStatus().value("reviewConfigured").toBool());
            QVERIFY(!QFileInfo::exists(c.log)); // provisioning starts no helper
            QVERIFY(save(QDir(directory).filePath("wallets/retained-wallet"),"fixture-wallet"));
            QTRY_COMPARE(state(engine),QString("stopped")); // fixture service exits cleanly
            QVERIFY(!engine.bchRefundWalletStatus().value("serviceOwned").toBool());
        }
        const auto binding=read(QDir(directory).filePath("service.binding"));QVERIFY(binding.contains(trade.toLatin1()));
        {
            DexEngine engine;QVERIFY(engine.configureBchRefundWallet(c.wrapper));
            QVERIFY(engine.configureBchRefundWalletService(config));
            QTRY_COMPARE(state(engine),QString("attached"));
            QCOMPARE(read(QDir(directory).filePath("wallets/retained-wallet")),QByteArray("fixture-wallet"));
            engine.shutdownBchRefundWalletService(); // returns once the service has stopped
            QVERIFY(state(engine)=="stopped" || state(engine)=="recovery-required");
        }
        QCOMPARE(read(QDir(directory).filePath("service.binding")),binding);
        for (const auto &interrupted:{QByteArray("recovery-required"),QByteArray("operation-pending"),QByteArray("active")}) {
            const auto bytes="monzero-swap-rpc-v1\n"+interrupted+"\n";
            QVERIFY(save(QDir(directory).filePath("service.state"),bytes));
            DexEngine engine;QVERIFY(engine.configureBchRefundWallet(c.wrapper));
            QVERIFY(engine.configureBchRefundWalletService(config));
            QCOMPARE(state(engine),QString("start-refused"));
            QVERIFY(!engine.bchRefundWalletStatus().value("reviewConfigured").toBool());
            QTest::qWait(300);QCOMPARE(state(engine),QString("start-refused"));
            QCOMPARE(read(QDir(directory).filePath("service.state")),bytes);
            QCOMPARE(read(QDir(directory).filePath("wallets/retained-wallet")),QByteArray("fixture-wallet"));
        }
    }
    void bchRefundWalletProvisionedConfigLifetime() {
        RefundWalletWorkerContext c;QVERIFY(c.create());SwapWalletRpcProcess owner;
        QVERIFY(owner.startFresh(QCoreApplication::applicationFilePath(),c.directory.path(),1,6));
        QString native;QTRY_VERIFY(!(native=owner.provisionBchNativeSession()).isEmpty());
        const auto bch=c.directory.filePath("bch.json"),observer=c.directory.filePath("observer.json");
        auto write=[&](const QString &path,QJsonObject value){return save(path,QJsonDocument(value).toJson(QJsonDocument::Compact));};
        QVERIFY(write(bch,{{"schema","monzero-private-bch-observer-rpc-v1"},{"port",18443},{"cookieFile",bch+".cookie"},{"checkpointHeight",1},{"checkpoint",QString(64,'7')},{"subversion","/Bitcoin Cash Node:29.0.0/"}}));
        QVERIFY(write(observer,{{"schema","monzero-private-bch-native-rpc-v1"},{"daemonPort",1},{"walletPort",7},{"loginFile",observer+".login"}}));
        QString generated;
        {
            BchRefundWalletJob job;QVERIFY(job.configure(c.wrapper));
            QVERIFY(job.provisionOwnedReview(&owner,bch,observer));
            auto files=QDir(owner.dataDirectory()).entryList({"bch-refund-wallet-review-*"},QDir::Files);QCOMPARE(files.size(),1);
            generated=QDir(owner.dataDirectory()).filePath(files.first());
            QVERIFY(!LitecoinJournalProtocol::privateConfig(generated).isEmpty());
            QVERIFY(!job.status().value("busy").toBool());QVERIFY(!QFileInfo::exists(c.log));
            job.discardProvisionedReview();QVERIFY(!QFileInfo::exists(generated));
            QVERIFY(!job.status().value("reviewConfigured").toBool());
            QVERIFY(job.provisionOwnedReview(&owner,bch,observer));
            files=QDir(owner.dataDirectory()).entryList({"bch-refund-wallet-review-*"},QDir::Files);QCOMPARE(files.size(),1);
            generated=QDir(owner.dataDirectory()).filePath(files.first());
        }
        QVERIFY(!QFileInfo::exists(generated));QVERIFY(owner.ownsBchNativeSession(native));
    }
    void bchRefundWalletOwnedSubmission() {
        for (const bool lost:{false,true}) {
            RefundWalletWorkerContext c;QVERIFY(c.create());
            auto policy=c.config.value("policy").toObject();policy["xmz_confirmations"]=1;c.config["policy"]=policy;
            auto details=refundWalletStatusValue(c);details.remove("chainObserved");details.remove("executable");
            details["refund"]=details.take("refundHint");details["schema"]="monzero-bch-native-refund-wallet-review-v1";
            details["action"]="restore";details["restoreHeight"]=0;details["minimumBchConfirmations"]=2;details["minimumXmzConfirmations"]=1;
            details["walletBackend"]=QJsonObject{{"schema","monzero-local-wallet-endpoint-v1"},{"host","127.0.0.1"},{"daemonPort",1},{"walletPort",6}};
            QByteArray bytes="monzero-bch-native-refund-wallet-review-v1";bytes.append('\0');bytes+=QJsonDocument(details).toJson(QJsonDocument::Compact);
            const auto commitment=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
            const QJsonObject review{{"details",details},{"commitment",commitment}};
            const QByteArray program=R"PY(#!/usr/bin/python3
import json,sys,pathlib,time
config_path=pathlib.Path(sys.argv[2]);r=json.load(sys.stdin)
config=json.loads(config_path.read_text())
root=pathlib.Path(config.get('statusConfigFile',str(config_path))).parent
kind=sys.argv[1].split('-wallet-')[1].split('-stdio-')[0]
with (root/'controller-calls').open('a') as log:log.write(kind+'\n')
wallet=json.loads((root/'status.json').read_text())
if kind=='status':v=dict(schema='monzero-bch-native-refund-wallet-status-response-v1',requestId=r['requestId'],tradeId=wallet['swapId'],executable=False,chainObserved=False,wallet=wallet)
elif kind=='review':v=dict(schema='monzero-bch-native-refund-wallet-review-response-v1',requestId=r['requestId'],tradeId=wallet['swapId'],executable=False,chainObserved=True,observedAt=int(time.time()),review=json.loads((root/'controller-review').read_text()))
else:
 assert kind=='submit' and r['password']=='fixture' and r['action']=='restore'
 assert r['commitment']==json.loads((root/'controller-review').read_text())['commitment']
 if (root/'mode').read_text()=='lost':sys.exit(1)
 wallet['state']='ready';wallet['walletIntent']=[2]*32
 v=dict(schema='monzero-bch-native-refund-wallet-submit-response-v1',requestId=r['requestId'],tradeId=wallet['swapId'],action='restore',approvedCommitment=r['commitment'],wallet=wallet,executable=False,chainObserved=False)
print(json.dumps(v,sort_keys=True,separators=(',',':')),end='')
)PY";
            QVERIFY(save(c.helper,program,true));c.config["helperDigest"]=QString::fromLatin1(QCryptographicHash::hash(program,QCryptographicHash::Sha256).toHex());QVERIFY(c.writeConfig());
            QVERIFY(save(c.mode,lost?QByteArray("lost"):QByteArray("success")));
            QVERIFY(save(c.directory.filePath("controller-review"),QJsonDocument(review).toJson(QJsonDocument::Compact)));
            SwapWalletRpcProcess owner,unrelated;DexEngine engine;
            QSignalSpy stopped(&owner,&SwapWalletRpcProcess::stopped),changed(&engine,&DexEngine::bchRefundWalletChanged);
            QVERIFY(!engine.attachBchRefundWalletService(&owner));QVERIFY(!engine.attachBchRefundWalletService(nullptr));
            QVERIFY(owner.startFresh(QCoreApplication::applicationFilePath(),c.directory.path(),1,6));
            QString native;QTRY_VERIFY(!(native=owner.provisionBchNativeSession()).isEmpty());
            auto write=[&](const QString &path,QJsonObject value){return save(path,QJsonDocument(value).toJson(QJsonDocument::Compact));};
            const auto bch=c.directory.filePath("bch.json"),observer=c.directory.filePath("observer.json"),wrapper=c.directory.filePath("controller-config.json");
            QVERIFY(write(bch,{{"schema","monzero-private-bch-observer-rpc-v1"},{"port",18443},{"cookieFile",bch+".cookie"},{"checkpointHeight",1},{"checkpoint",QString(64,'7')},{"subversion","/Bitcoin Cash Node:29.0.0/"}}));
            QVERIFY(write(observer,{{"schema","monzero-private-bch-native-rpc-v1"},{"daemonPort",1},{"walletPort",7},{"loginFile",observer+".login"}}));
            QVERIFY(write(wrapper,{{"schema","monzero-private-bch-native-refund-wallet-review-v1"},{"statusConfigFile",c.wrapper},{"bchRpcFile",bch},{"nativeRpcFile",native},{"observerRpcFile",observer}}));
            QVERIFY(engine.configureBchRefundWallet(c.wrapper));
            if (lost) {
                QVERIFY(engine.configureBchRefundWalletReview(wrapper));
                QVERIFY(!engine.attachBchRefundWalletService(&unrelated));QVERIFY(engine.attachBchRefundWalletService(&owner));
            } else {
                QVERIFY(!engine.provisionBchRefundWalletService(nullptr,bch,observer));
                QVERIFY(!engine.provisionBchRefundWalletService(&unrelated,bch,observer));
                QVERIFY(!engine.provisionBchRefundWalletService(&owner,bch,native));
                QCOMPARE(QDir(owner.dataDirectory()).entryList({"bch-refund-wallet-review-*"},QDir::Files).size(),0);
                QVERIFY(engine.provisionBchRefundWalletService(&owner,bch,observer));
                const auto files=QDir(owner.dataDirectory()).entryList({"bch-refund-wallet-review-*"},QDir::Files);
                QCOMPARE(files.size(),1);
                const auto generated=QDir(owner.dataDirectory()).filePath(files.first());
                QCOMPARE(read(generated),read(wrapper));
                QVERIFY(!engine.provisionBchRefundWalletService(&owner,bch,observer));
                QCOMPARE(QDir(owner.dataDirectory()).entryList({"bch-refund-wallet-review-*"},QDir::Files).size(),1);
            }
            QVERIFY(!engine.attachBchRefundWalletService(&owner));QVERIFY(engine.bchRefundWalletStatus().value("serviceOwned").toBool());
            QVERIFY(!engine.bchRefundWalletStatus().value("submitAvailable").toBool());
            engine.requestBchStartupChecks();QTRY_VERIFY(engine.bchRefundWalletStatus().value("reviewAvailable").toBool());
            QCOMPARE(read(c.directory.filePath("controller-calls")),QByteArray("status\n"));
            QVERIFY(engine.requestBchRefundWalletReview("restore"));QTRY_VERIFY(engine.bchRefundWalletStatus().value("submitAvailable").toBool());
            const auto count=changed.count();const auto held=owner.beginWalletOperation();QVERIFY(!held.isEmpty());
            QTRY_VERIFY(changed.count()>count);QVERIFY(!engine.bchRefundWalletStatus().value("submitAvailable").toBool());
            QByteArray secret("fixture");QVERIFY(!engine.submitBchRefundWallet(commitment,std::move(secret)));QVERIFY(secret.isEmpty());
            QVERIFY(owner.completeWalletOperation(held,true));QVERIFY(engine.bchRefundWalletStatus().value("submitAvailable").toBool());
            QVERIFY(!engine.submitBchRefundWallet(QString(64,'0'),"fixture"));
            QVERIFY(!engine.approveBchRefundWallet(commitment,"fixture","fixture",false));
            QVERIFY(!engine.approveBchRefundWallet(commitment,"fixture","wrong",true));
            QVERIFY(!engine.approveBchRefundWallet(QString(64,'0'),"fixture","fixture",true));
            QVERIFY(engine.approveBchRefundWallet(commitment,"fixture","fixture",true));
            QVERIFY(!engine.requestBchRefundWalletCheck());QVERIFY(!engine.requestBchRefundWalletReview("restore"));
            QVERIFY(!engine.submitBchRefundWallet(commitment,"fixture"));
            QTRY_VERIFY(!engine.bchRefundWalletStatus().value("busy").toBool());
            QCOMPARE(engine.bchRefundWalletStatus().value("state").toString(),lost?QString("recovery-required"):QString("ready"));
            QCOMPARE(engine.bchRefundWalletStatus().value("submissionUncertain").toBool(),lost);
            QVERIFY(!engine.bchRefundWalletStatus().value("submitAvailable").toBool());
            QCOMPARE(read(c.directory.filePath("controller-calls")),QByteArray("status\nreview\nsubmit\n"));
            QTRY_COMPARE(stopped.count(),1);QCOMPARE(stopped.at(0).at(0).toBool(),!lost);
            QVERIFY(!engine.bchRefundWalletStatus().value("serviceOwned").toBool());
        }
    }
    void bchRefundWalletReviewDispatchGuard() {
        RefundWalletWorkerContext fixture;QVERIFY(fixture.create());PunishmentWorkerContext other;QVERIFY(other.create());
        auto write=[&](QString path,QJsonObject value){return save(path,QJsonDocument(value).toJson(QJsonDocument::Compact));};
        const auto native=fixture.directory.filePath("native.json"),bch=fixture.directory.filePath("bch.json"),wrapper=fixture.directory.filePath("review.json");
        QVERIFY(write(native,{{"schema","monzero-private-bch-native-rpc-v1"},{"daemonPort",18081},{"walletPort",18083},{"loginFile",fixture.directory.filePath("never-read-login")}}));
        QVERIFY(write(bch,{{"schema","monzero-private-bch-observer-rpc-v1"},{"port",18443},{"cookieFile",bch+".cookie"},{"checkpointHeight",1},{"checkpoint",QString(64,'7')},{"subversion","/Bitcoin Cash Node:29.0.0/"}}));
        const auto observer=fixture.directory.filePath("observer.json");
        QVERIFY(write(observer,{{"schema","monzero-private-bch-native-rpc-v1"},{"daemonPort",18081},{"walletPort",18084},{"loginFile",fixture.directory.filePath("observer-login")}}));
        QVERIFY(write(wrapper,{{"schema","monzero-private-bch-native-refund-wallet-review-v1"},{"statusConfigFile",fixture.wrapper},{"bchRpcFile",bch},{"nativeRpcFile",native},{"observerRpcFile",observer}}));
        DexEngine engine;QVERIFY(!engine.configureBchRefundWalletReview(wrapper));QVERIFY(!engine.requestBchRefundWalletReview("restore"));
        QVERIFY(engine.configureBchRefundWallet(fixture.wrapper));QVERIFY(engine.configureBchRefundWalletReview(wrapper));
        QVERIFY(engine.configureBchPunishment(other.wrapper));engine.requestBchStartupChecks();
        QTRY_VERIFY(engine.bchRefundWalletStatus().value("reviewAvailable").toBool());
        QTRY_COMPARE(engine.bchRefundWalletStatus().value("state").toString(),QString("not-attempted"));
        QCOMPARE(read(fixture.log),QByteArray("--native-refund-wallet-status-stdio-v1\n"));
        QVERIFY(!engine.requestBchRefundWalletReview("send"));
        QVERIFY(engine.requestBchPunishmentCheck());QVERIFY(!engine.requestBchRefundWalletReview("restore"));
        QVERIFY(!engine.bchRefundWalletStatus().value("reviewAvailable").toBool());
        QTRY_VERIFY(!engine.bchPunishmentStatus().value("busy").toBool());
        QVERIFY(engine.requestBchRefundWalletReview("restore"));QVERIFY(!engine.requestBchPunishmentCheck());
        // Status-only synthetic helper intentionally rejects the review command.
        QTRY_COMPARE(engine.bchRefundWalletStatus().value("state").toString(),QString("unknown"));
        QVERIFY(engine.bchRefundWalletStatus().value("review").toMap().isEmpty());
        QVERIFY(!engine.bchRefundWalletStatus().value("chainObserved").toBool());
    }
    void bchRefundWalletStatusStartupAndGuard() {
        RefundWalletWorkerContext fixture;QVERIFY(fixture.create());PunishmentWorkerContext other;QVERIFY(other.create());
        DexEngine engine;QVERIFY(!engine.requestBchRefundWalletCheck());
        QVERIFY(engine.configureBchRefundWallet(fixture.wrapper));QVERIFY(engine.configureBchPunishment(other.wrapper));
        QVERIFY(engine.requestBchRefundWalletCheck());QVERIFY(!engine.requestBchPunishmentCheck());
        QVERIFY(!engine.requestBchRefundWalletCheck());QVERIFY(!engine.bchPunishmentStatus().value("checkAvailable").toBool());
        QTRY_COMPARE(engine.bchRefundWalletStatus().value("state").toString(),QString("not-attempted"));
        QVERIFY(engine.requestBchPunishmentCheck());QVERIFY(!engine.requestBchRefundWalletCheck());
        QVERIFY(!engine.bchRefundWalletStatus().value("checkAvailable").toBool());
        QTRY_VERIFY(!engine.bchPunishmentStatus().value("busy").toBool());
        QVERIFY(engine.bchRefundWalletStatus().value("checkAvailable").toBool());
        RefundWalletWorkerContext startup;QVERIFY(startup.create());DexEngine reopened;
        QVERIFY(reopened.configureBchRefundWallet(startup.wrapper));reopened.requestBchStartupChecks();
        QTRY_COMPARE(reopened.bchRefundWalletStatus().value("state").toString(),QString("not-attempted"));
        QCOMPARE(read(startup.log),QByteArray("--native-refund-wallet-status-stdio-v1\n"));
        QVERIFY(!reopened.bchRefundWalletStatus().value("chainObserved").toBool());
        QVERIFY(!reopened.bchRefundWalletStatus().value("executable").toBool());
    }
    void bchPunishmentStartupAndOperationGuard() {
        PunishmentWorkerContext fixture;QVERIFY(fixture.create());DexEngine engine;
        QVERIFY(!engine.requestBchPunishmentReview());QVERIFY(engine.configureBchPunishment(fixture.wrapper));
        QVERIFY(engine.configureBchAdmission(fixture.path));
        QVERIFY(engine.requestBchPunishmentReview());
        QVERIFY(!engine.requestBchAdmissionCheck());
        QVERIFY(!engine.requestBchPunishmentCheck());
        QTRY_VERIFY(!engine.bchPunishmentStatus().value("busy").toBool());
        QCOMPARE(engine.bchPunishmentStatus().value("state").toString(),QString("reviewed"));
        QVERIFY(!engine.bchPunishmentStatus().value("submitAvailable").toBool());
        QVERIFY(engine.requestBchAdmissionCheck());
        QVERIFY(!engine.requestBchPunishmentCheck());
        QTRY_VERIFY(!engine.bchAdmissionStatus().value("busy").toBool());
        QVERIFY(engine.requestBchPunishmentCheck());QTRY_VERIFY(!engine.bchPunishmentStatus().value("busy").toBool());
        QCOMPARE(engine.bchPunishmentStatus().value("state").toString(),QString("checked"));
        PunishmentWorkerContext startup;QVERIFY(startup.create());DexEngine reopened;
        QVERIFY(reopened.configureBchPunishment(startup.wrapper));reopened.requestBchStartupChecks();
        QTRY_COMPARE(reopened.bchPunishmentStatus().value("state").toString(),QString("checked"));
        QCOMPARE(read(startup.log),QByteArray("--punish-status-stdio-v1\n"));
    }
    void bchStartupChecksSerializeAndNeverPrepare() {
        Fixture fixture;QVERIFY(fixture.create());
        const QByteArray helper=R"PY(#!/usr/bin/python3
import pathlib,sys,json,time
request=json.load(sys.stdin)
assert set(request)=={'schema','requestId'}
root=pathlib.Path(__file__).parent
try:(root/'inflight').open('x').close()
except FileExistsError:
 (root/'overlap').write_text('overlap');sys.exit(1)
with (root/'calls').open('a') as f:f.write(sys.argv[1]+'\n')
time.sleep(0.05)
(root/'inflight').unlink()
if sys.argv[1]=='--native-proof-context-stdio-v1':
 c=json.loads((root/'config.json').read_text())
 context={'bchFundingOutpoint':'a'*64+':0','destination':'B'*95,'admission':'1'*64,'agreement':'4'*64}
 response={'schema':'monzero-bch-native-proof-context-response-v1','requestId':request['requestId'],
  'tradeId':c['tradeId'],'executable':False,'chainObserved':False,'context':context}
 sys.stdout.write(json.dumps(response,sort_keys=True,separators=(',',':')));sys.exit(0)
sys.exit(1)
)PY";
        QVERIFY(save(fixture.helper,helper,true));fixture.config["role"]="bch-funder";
        fixture.config["helperDigest"]=QString::fromLatin1(QCryptographicHash::hash(helper,QCryptographicHash::Sha256).toHex());
        QVERIFY(fixture.writeConfig());
        const auto funding=fixture.directory.filePath("funding.json"),preparation=fixture.directory.filePath("preparation.json");
        QVERIFY(save(funding,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-funding-v1"},
            {"admissionConfigFile",fixture.path},{"deploymentCommitment",QString(64,'5')}}).toJson(QJsonDocument::Compact)));
        const QJsonObject policy{{"change_address","bchreg:qchange"},{"change_script","76a914"+QString(40,'3')+"88ac"},
            {"fee_rate_atoms_per_kb",1},{"maximum_fee",1},{"minimum_change",1},{"minimum_confirmations",3}};
        QVERIFY(save(preparation,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-preparation-v1"},
            {"fundingConfigFile",funding},{"rpcFile",fixture.directory.filePath("absent")},{"policy",policy}}).toJson(QJsonDocument::Compact)));
        const auto recovery=fixture.directory.filePath("recovery.json");
        QVERIFY(save(recovery,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-signing-recovery-v1"},
            {"preparationConfigFile",preparation},{"recoveredTransactionFile",fixture.directory.filePath("candidate-absent")}}).toJson(QJsonDocument::Compact)));
        const auto nativeAdmission=fixture.directory.filePath("native-admission.json"),receipt=fixture.directory.filePath("receipt.json");
        auto nativeConfig=fixture.config;nativeConfig["role"]="native-funder";
        QVERIFY(save(nativeAdmission,QJsonDocument(nativeConfig).toJson(QJsonDocument::Compact)));
        QVERIFY(save(receipt,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-funding-receipt-v1"},
            {"admissionConfigFile",nativeAdmission},{"peerMessageFile",fixture.directory.filePath("absent-peer.json")}}).toJson(QJsonDocument::Compact)));
        const auto nativePreparation=fixture.directory.filePath("native-preparation.json");
        QVERIFY(save(nativePreparation,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-native-preparation-v1"},
            {"admissionConfigFile",nativeAdmission},{"bchRpcFile",fixture.directory.filePath("absent-bch.json")},
            {"nativeRpcFile",fixture.directory.filePath("absent-native.json")},{"deploymentCommitment",QString(64,'7')},
            {"wallet",QString(95,'A')},{"account",0},{"priority",1},{"safetyBlocks",1}}).toJson(QJsonDocument::Compact)));
        DexEngine engine;QVERIFY(engine.configureBchAdmission(fixture.path));QVERIFY(engine.configureBchPreparation(preparation));
        QVERIFY(engine.configureBchFundingReceipt(receipt));
        QVERIFY(engine.configureBchNativePreparation(nativePreparation));
        QVERIFY(engine.configureBchNativeRelay(nativePreparation));
        const auto proof=fixture.directory.filePath("proof.json");
        QVERIFY(save(proof,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-native-proof-v1"},
            {"admissionConfigFile",fixture.path},{"peerMessageFile",fixture.directory.filePath("absent-proof.json")}}).toJson(QJsonDocument::Compact)));
        QVERIFY(engine.configureBchNativeProof(proof));
        QVERIFY(engine.configureBchSigningRecovery(recovery));
        QVERIFY(engine.configureBchFunding(funding));QVERIFY(!QFile::exists(fixture.log));engine.requestBchStartupChecks();
        QVERIFY(engine.bchAdmissionStatus().value("busy").toBool());QVERIFY(!engine.prepareBchFunding());
        QVERIFY(!engine.requestBchFundingReceiptCheck());QVERIFY(!engine.requestBchFundingReceiptReview());
        QVERIFY(!engine.submitBchFundingReceipt(QString(64,'1')));
        QVERIFY(!engine.requestBchNativePreparationCheck());QVERIFY(!engine.requestBchNativePreparationReview());
        QVERIFY(!engine.submitBchNativePreparation(QString(64,'1')));
        QVERIFY(!engine.requestBchNativeRelayCheck());QVERIFY(!engine.requestBchNativeRelayReview());
        QVERIFY(!engine.submitBchNativeRelay(QString(64,'1')));
        QVERIFY(!engine.requestBchNativeProofCheck());QVERIFY(!engine.requestBchNativeProofReview());
        QVERIFY(!engine.submitBchNativeProof(QString(64,'1')));
        QVERIFY(!engine.requestBchFundingCheck());QVERIFY(!engine.requestBchLockReview());
        QVERIFY(!engine.requestBchLockCheck());QVERIFY(!engine.submitBchLock(QString(64,'1')));
        QVERIFY(!engine.requestBchSigningReview());QVERIFY(!engine.requestBchSigningCheck());QVERIFY(!engine.submitBchSigning(QString(64,'1')));
        QVERIFY(!engine.requestBchSigningRecoveryReview());QVERIFY(!engine.importBchSigningRecovery(QString(64,'1')));
        QTRY_COMPARE_WITH_TIMEOUT(read(fixture.log).count('\n'),10,10000);
        QTRY_VERIFY_WITH_TIMEOUT(!engine.bchFundingStatus().value("busy").toBool(),10000);
        QCOMPARE(read(fixture.log),QByteArray("--admission-status-stdio-v1\n--funding-receipt-status-stdio-v1\n--native-preparation-status-stdio-v1\n--native-relay-status-stdio-v1\n--native-proof-context-stdio-v1\n--native-proof-status-stdio-v1\n--funding-preparation-status-stdio-v1\n--funding-lock-status-stdio-v1\n--funding-signing-status-stdio-v1\n--funding-status-stdio-v1\n"));
        QCOMPARE(engine.bchPreparationStatus().value("state").toString(),QString("unknown"));
        QVERIFY(engine.bchPreparationStatus().value("result").toMap().isEmpty());
        QCOMPARE(engine.bchFundingReceiptStatus().value("state").toString(),QString("unknown"));
        QVERIFY(engine.bchFundingReceiptStatus().value("review").toMap().isEmpty());
        QCOMPARE(engine.bchNativePreparationStatus().value("state").toString(),QString("unknown"));
        QVERIFY(engine.bchNativePreparationStatus().value("review").toMap().isEmpty());
        QCOMPARE(engine.bchNativeProofStatus().value("state").toString(),QString("unknown"));
        QVERIFY(engine.requestBchNativeProofCheck());
        QVERIFY(!engine.requestBchNativeRelayCheck());QVERIFY(!engine.requestBchFundingCheck());
        QVERIFY(!engine.bchNativeRelayStatus().value("checkAvailable").toBool());
        QTRY_VERIFY(!engine.bchNativeProofStatus().value("busy").toBool());
        QVERIFY(!QFile::exists(fixture.directory.filePath("overlap")));
    }
    void litecoinNativeRecoveryBindsRoleTermsAndConfirmation() {
#ifdef Q_OS_LINUX
        for (const int scenario:{0,1,2,3,4,5,6,7,8,9,10,12,13,14,15,16,17,18}) {
            QTemporaryDir dir;QVERIFY(dir.isValid());
            const auto program=dir.path()+"/helper",quotePath=dir.path()+"/quote",rpcPath=dir.path()+"/rpc";
            const auto startPath=dir.path()+"/startup",fundingPath=dir.path()+"/funding",casePath=dir.path()+"/case";
            const auto write=[](const QString &path,const QByteArray &data) {
                QFile file(path);return file.open(QIODevice::WriteOnly|QIODevice::Truncate)
                    && file.setPermissions(QFile::ReadOwner|QFile::WriteOwner) && file.write(data)==data.size();
            };
            const auto json=[](const QJsonObject &value) {return QJsonDocument(value).toJson(QJsonDocument::Compact);};
            const auto trade=QUuid::createUuid().toString(QUuid::WithoutBraces);
            const QJsonObject terms{{"schema","monzero-native-ltc-xmz-quote-v2"},{"swapId",trade},
                {"ltcNetwork","ltc-regtest"},{"xmzGenesis",QString(64,'b')},{"ltcAtoms","100"},{"xmzAtoms","200"},
                {"ltcPayout","rltc1q"+QString(38,'q')},{"ltcRefund","rltc1q"+QString(38,'p')},
                {"xmzPayout","fixture-payout"},{"xmzRefund","fixture-refund"},{"ltcConfirmations","2"},
                {"ltcCancelFeeAtoms","1"},{"ltcPunishFeeAtoms","2"},{"ltcRefundFeeAtoms","3"},
                // An existing trade must remain recoverable after commercial quote expiry.
                {"expiresAt",QString::number(QDateTime::currentSecsSinceEpoch()+(scenario==10?-3600:3600))}};
            QVERIFY(write(quotePath,json({{"schema","monzero-ltc-signed-quote-envelope-v1"},
                {"payload",QString::fromUtf8(json(terms))},{"ltcSignature",QString(128,'a')},{"xmzSignature",QString(128,'b')}})));
            QVERIFY(write(rpcPath,json({{"schema","monzero-private-ltc-funding-rpc-v1"}})));
            QVERIFY(write(casePath,QByteArray::number(scenario)));
            // Synthetic helper replies test app validation, not signatures or node execution.
            const QByteArray script=R"PY(#!/usr/bin/python3
import json,sys,hashlib,os
root=os.path.dirname(sys.argv[0])
r=json.load(sys.stdin)
c=json.load(open(root+'/startup'))
e=json.load(open(c['startQuoteFile']))
t=json.loads(e['payload'])
q=hashlib.sha256(t['schema'].encode()+b'\0'+e['payload'].encode()).hexdigest()
mode=sys.argv[1].removeprefix('--').split('-')
assert mode[:2]==['native','recovery']
kind,op=mode[2:4]
assert kind in ['cancel','punish'] and op in ['review','submit','check']
assert r['schema']=='monzero-ltc-native-recovery-'+kind+'-'+op+'-request-v1'
case=int(open(root+'/case').read())
s={k:t[k] for k in ['ltcNetwork','xmzGenesis','ltcAtoms','xmzAtoms','ltcPayout','ltcRefund','xmzPayout','xmzRefund']}
s.update(schema='monzero-ltc-journal-status-v1',tradeId=c['tradeId'],role='native-funder',quoteCommitment=q,keysReady=True,recoveryIntentRetained=True,nativeClaimRetained=False,nativeRefundRetained=False,journalValidated=True,chainObserved=False,executable=False)
for k in ['nativeFunding','litecoinRedemption','nativePayout','nativeRefundPayout']: s[k]={'prepared':False,'attempted':False,'acknowledged':False}
s['litecoinRecovery']={k:{'attempted':False,'acknowledged':False} for k in ['cancel','punish']}
d={'schema':'monzero-ltc-recovery-spend-review-v1','tradeId':c['tradeId'],'quoteCommitment':q,'journalCommitment':'c'*64,'recoveryCommitment':'d'*64,'transactionCommitment':'e'*64,'dispatchCommitment':None,'operation':kind,'action':'send','txid':'f'*64,'wtxid':'1'*64,'inputTxid':'2'*64,'inputVout':'0','sequence':'10','destination':t['ltcPayout'],'inputAtoms':'99' if kind=='punish' else '100','amountAtoms':'97' if kind=='punish' else '99','feeAtoms':'2' if kind=='punish' else '1','chainObserved':False}
if case==1: d['destination']=t['ltcRefund']
if case==2: d['amountAtoms']='96'
if case==3: d.update(amountAtoms='98',feeAtoms='1')
if case==4: d['operation']='refund'
if case==5: s['role']='litecoin-funder'
if case==9: s['quoteCommitment']='9'*64
h=hashlib.sha256(b'MONZERO-LTC-RECOVERY-SPEND-APPROVAL-V1\0'+json.dumps(d,sort_keys=True,separators=(',',':')).encode()).hexdigest()
response={'schema':'monzero-ltc-native-recovery-'+kind+'-'+op+'-response-v1','requestId':r['requestId'],'snapshot':s}
if case==6: response['schema']=response['schema'].replace('native-recovery','recovery')
if op=='review': response['review']={'details':d,'commitment':'0'*64 if case==7 else h}
if op=='submit':
 assert r['commitment']==h
 open(root+'/submitted','w').close()
 s['litecoinRecovery'][kind]={'attempted':True,'acknowledged':True}
 response['outcome']={'state':'acknowledged','txid':d['txid']}
 if case==12: response['outcome']['txid']='8'*64
 if case==13: s['litecoinRecovery'][kind]['acknowledged']=False
 if case in [14,15]:
  response['outcome']['state']='uncertain' if case==14 else 'mempool'
  s['litecoinRecovery'][kind]['acknowledged']=False
 if case==16: sys.exit(1) # Simulate lost reply after the helper accepted submission.
 if case==17: s['litecoinRecovery'][kind]={'attempted':False,'acknowledged':False}
 if case==18: response['requestId']='wrong'
if op=='check':
 if case==11: sys.exit(1)
 response['observation']={'schema':'monzero-ltc-recovery-confirmation-v1','tradeId':c['tradeId'],'quoteCommitment':q,'operation':kind,'txid':d['txid'],'wtxid':d['wtxid'],'block':'3'*64,'tip':'4'*64,'confirmations':'2','requiredConfirmations':'2','chainObserved':True}
sys.stdout.write(json.dumps(response,sort_keys=True,separators=(',',':')))
)PY";
            QVERIFY(write(program,script));QVERIFY(QFile::setPermissions(program,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
            QJsonObject config{{"schema","monzero-private-ltc-journal-v5"},{"helper",program},
                {"helperDigest",QString::fromLatin1(QCryptographicHash::hash(script,QCryptographicHash::Sha256).toHex())},
                {"tradeId",trade},{"role","native-funder"},{"metadataDirectory",dir.path()},{"snapshotDirectory",dir.path()},
                {"pins",QJsonArray{}},{"policy",QJsonObject{{"ltc_network","ltc-regtest"},{"ltc_confirmations",2},{"xmz_confirmations",10}}},
                {"startQuoteFile",quotePath}};
            QVERIFY(write(startPath,json(config)));
            config.remove("startQuoteFile");config.insert("schema","monzero-private-ltc-journal-v4");config.insert("fundingRpcFile",rpcPath);
            for (const auto key:{"tradeId","role","metadataDirectory","helperDigest"}) {
                auto changed=config;changed.insert(key,"changed");QVERIFY(write(fundingPath,json(changed)));
                LitecoinReservationJob rejected;QVERIFY(!rejected.configure(fundingPath,startPath));
            }
            QVERIFY(write(fundingPath,json(config)));
            LitecoinReservationJob job;QVERIFY(job.configure(fundingPath,startPath));
            QCOMPARE(job.status().value("role").toString(),QString("native-funder"));
            QVERIFY(!job.status().value("reservationConfigured").toBool());
            QVERIFY(!job.status().value("fundingReviewAvailable").toBool());
            QVERIFY(!job.prepare());QVERIFY(!job.reviewFunding());QVERIFY(!job.reviewRecovery("refund"));QVERIFY(!job.checkRecovery("refund"));
            QVERIFY(!job.submitRecovery(QString(64,'0')));
            QVERIFY(job.reviewRecovery("punish"));QVERIFY(!job.reviewRecovery("cancel"));
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            if (scenario!=0 && scenario!=8 && scenario<10) {
                QVERIFY(!job.status().value("recoverySubmissionAvailable").toBool());
                QCOMPARE(job.status().value("recoveryState").toString(),QString("unknown"));continue;
            }
            QCOMPARE(job.status().value("recoveryState").toString(),QString("reviewed"));
            const auto commitment=job.status().value("recoveryReview").toMap().value("commitment").toString();
            QVERIFY(!job.submitRecovery(QString(64,'0')));
            if (scenario==8) {
                QVERIFY(write(rpcPath,"{}"));QVERIFY(!job.submitRecovery(commitment));
                QVERIFY(!job.status().value("recoverySubmissionAvailable").toBool());QVERIFY(!QFile::exists(dir.path()+"/submitted"));continue;
            }
            QVERIFY(job.submitRecovery(commitment));QVERIFY(!job.submitRecovery(commitment));
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QVERIFY(QFile::exists(dir.path()+"/submitted"));
            if (scenario>=12) {
                const QString expected=scenario==14?"uncertain":scenario==15?"mempool":"unknown";
                QCOMPARE(job.status().value("recoveryState").toString(),expected);
                QVERIFY(!job.status().value("recoverySubmissionAvailable").toBool());
                QVERIFY(!job.submitRecovery(commitment));
                QVERIFY(job.status().value("recoveryObservation").toMap().isEmpty());
                QCOMPARE(job.status().value("recoveryCheckState").toString(),QString("not-checked"));
                const auto submission=job.status().value("recoverySubmission").toMap();
                if (expected=="unknown") QVERIFY(submission.isEmpty());
                else QCOMPARE(submission.value("state").toString(),expected);
                // A transport failure is not permission to replay the old approval.
                // Retained recovery remains accessible through a fresh review.
                QVERIFY(job.reviewRecovery("punish"));
                QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
                QCOMPARE(job.status().value("recoveryState").toString(),QString("reviewed"));
                continue;
            }
            QCOMPARE(job.status().value("recoveryState").toString(),QString("acknowledged"));
            QVERIFY(job.checkRecovery("punish"));
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QCOMPARE(job.status().value("recoveryCheckState").toString(),QString("confirmed"));
            QVERIFY(write(casePath,"11"));QVERIFY(job.checkRecovery("punish"));
            QVERIFY(job.status().value("recoveryObservation").toMap().isEmpty());
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QCOMPARE(job.status().value("recoveryCheckState").toString(),QString("unknown"));
            QVERIFY(write(casePath,"0"));QVERIFY(job.reviewRecovery("cancel"));
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QCOMPARE(job.status().value("recoveryState").toString(),QString("reviewed"));
            const auto cancelCommitment=job.status().value("recoveryReview").toMap().value("commitment").toString();
            QVERIFY(job.submitRecovery(cancelCommitment));
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QCOMPARE(job.status().value("recoveryState").toString(),QString("acknowledged"));
            QVERIFY(job.checkRecovery("cancel"));
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QCOMPARE(job.status().value("recoveryCheckState").toString(),QString("confirmed"));
        }
#endif
    }
    void litecoinStartupBindsReviewAndPreservesUnknownOutcomes() {
#ifdef Q_OS_LINUX
        const QString trade="12345678-1234-4234-8234-123456789abc";
        for (int testCase=0;testCase<14;++testCase) {
            QTemporaryDir dir;QVERIFY(dir.isValid());
            QJsonObject terms{{"schema","monzero-native-ltc-xmz-quote-v2"},{"swapId",trade},{"ltcNetwork","ltc-regtest"},
                {"xmzGenesis",QString(64,'b')},{"ltcAtoms","18446744073709551615"},{"xmzAtoms","200000000000"},
                {"xmzPayoutAtoms","100000000000"},{"ltcPayout","fixture-ltc-payout"},{"ltcRefund","fixture-ltc-refund"},
                {"xmzPayout","fixture-xmz-payout"},{"xmzRefund","fixture-xmz-refund"},
                {"expiresAt",QString::number(QDateTime::currentSecsSinceEpoch()+(testCase==13?-3600:testCase==11?3:3600))}};
            for (const auto &key:{"xmzClaimFeeMaxAtoms","xmzRefundFeeMaxAtoms","ltcFundingFeeMaxAtoms","xmzNativeFeeMaxAtoms",
                "ltcCancelFeeAtoms","ltcRefundFeeAtoms","ltcRedeemFeeAtoms","ltcPunishFeeAtoms","ltcEarlyRefundFeeAtoms",
                "ltcMinimumOutputAtoms","ltcMinimumRelayFeeAtomsPerKb","appFeeXmzAtoms"}) terms.insert(key,"0");
            const QJsonObject policy{{"ltc_network","ltc-regtest"}};
            const QString quotePath=dir.path()+"/quote.json",program=dir.path()+"/helper",path=dir.path()+"/config.json";
            const auto writePrivate=[](const QString &name,const QByteArray &bytes) {
                QFile file(name);return file.open(QIODevice::WriteOnly|QIODevice::Truncate)
                    && file.setPermissions(QFile::ReadOwner|QFile::WriteOwner) && file.write(bytes)==bytes.size();
            };
            QVERIFY(writePrivate(quotePath,QJsonDocument(QJsonObject{{"schema","monzero-ltc-signed-quote-envelope-v1"},
                {"payload",QString::fromUtf8(QJsonDocument(terms).toJson(QJsonDocument::Compact))},
                {"ltcSignature",QString(128,'a')},{"xmzSignature",QString(128,'b')}}).toJson(QJsonDocument::Compact)));
            // Synthetic transport replies: signature verification is tested with
            // the real native helper in actualLitecoinStartup.
            QByteArray script=R"PY(#!/usr/bin/python3
import json,sys,hashlib
r=json.load(sys.stdin)
c=json.load(open(sys.argv[-1]))
t=json.loads(json.load(open(c['startQuoteFile']))['payload'])
d={'schema':'monzero-ltc-trade-start-review-v1','role':c['role'],'terms':t,'policy':c['policy'],'quoteCommitment':'a'*64,'chainObserved':False,'executable':False}
case=$CASE
if case==2: d['terms']=dict(t,ltcAtoms='2')
if case==3: d['policy']=dict(c['policy'],extra=1)
if case==4: d['role']='litecoin-funder'
if case==6: d['chainObserved']=True
h=hashlib.sha256(b'MONZERO-LTC-TRADE-START-APPROVAL-V1\0'+json.dumps(d,sort_keys=True,separators=(',',':')).encode()).hexdigest()
review={'details':d,'commitment':h if case!=1 else '0'*64}
s={k:t[k] for k in ['ltcNetwork','xmzGenesis','ltcAtoms','xmzAtoms','ltcPayout','ltcRefund','xmzPayout','xmzRefund']}
s.update(schema='monzero-ltc-journal-status-v1',tradeId=c['tradeId'],role=c['role'],quoteCommitment='a'*64,keysReady=False,recoveryIntentRetained=False,nativeClaimRetained=False,nativeRefundRetained=False,journalValidated=True,chainObserved=False,executable=False)
for k in ['nativeFunding','litecoinRedemption','nativePayout','nativeRefundPayout']: s[k]={'prepared':False,'attempted':False,'acknowledged':False}
s['litecoinRecovery']={k:{'attempted':False,'acknowledged':False} for k in ['cancel','punish']}
response={'schema':'monzero-ltc-start-review-response-v1','requestId':r['requestId'],'review':review}
if case==5: response['requestId']='wrong'
if r['schema']=='monzero-ltc-start-submit-request-v1':
 open(sys.argv[0]+'.submitted','w').close()
 if case==7: sys.exit(1)
 response.update(schema='monzero-ltc-start-submit-response-v1',snapshot=s)
 if case==8: response['review']['details']['quoteCommitment']='c'*64
 if case==9: response['snapshot']['ltcAtoms']='3'
 if case==12:
  with open(c['startQuoteFile'],'a') as out: out.write(' ')
if r['schema']=='monzero-ltc-journal-request-v1': response={'schema':'monzero-ltc-journal-response-v1','requestId':r['requestId'],'snapshot':s}
sys.stdout.write(json.dumps(response,sort_keys=True,separators=(',',':')))
)PY";
            script.replace("$CASE",QByteArray::number(testCase));
            QVERIFY(writePrivate(program,script));QVERIFY(QFile::setPermissions(program,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
            const auto digest=QString::fromLatin1(QCryptographicHash::hash(script,QCryptographicHash::Sha256).toHex());
            QVERIFY(writePrivate(path,QJsonDocument(QJsonObject{{"schema","monzero-private-ltc-journal-v5"},{"startQuoteFile",quotePath},
                {"helper",program},{"helperDigest",digest},{"tradeId",trade},{"role","native-funder"},
                {"metadataDirectory",dir.path()},{"snapshotDirectory",dir.path()},{"pins",QJsonArray{}},{"policy",policy}}).toJson(QJsonDocument::Compact)));
            LitecoinJournalJob job;QVERIFY(job.configure(path));QVERIFY(!job.submitStart(QString(64,'0')));
            QVERIFY(job.reviewStart());QVERIFY(!job.reviewStart());
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            if ((testCase>=1 && testCase<=6) || testCase==13) {
                QCOMPARE(job.status().value("startState").toString(),QString("unverified"));
                QVERIFY(!job.status().value("startSubmissionAvailable").toBool());continue;
            }
            QCOMPARE(job.status().value("startState").toString(),QString("reviewed"));
            const auto review=job.status().value("startReview").toMap();
            QCOMPARE(review.value("amounts").toMap().value("ltcAtoms").toString(),QString("184467440737.09551615"));
            const auto commitment=review.value("commitment").toString();
            QVERIFY(!job.submitStart(QString(64,'0')));QVERIFY(!job.submitFunding(commitment));QVERIFY(!job.submitPayout(commitment));
            if (testCase==10) {QFile quote(quotePath);QVERIFY(quote.open(QIODevice::Append));quote.write(" ");quote.close();}
            if (testCase==11) QTest::qWait(3100);
            if (testCase==10 || testCase==11) {
                QVERIFY(!job.submitStart(commitment));QVERIFY(!QFile::exists(program+".submitted"));
                QVERIFY(!job.status().value("startSubmissionAvailable").toBool());continue;
            }
            QVERIFY(job.submitStart(commitment));QVERIFY(!job.submitStart(commitment));
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QVERIFY(QFile::exists(program+".submitted"));
            QCOMPARE(job.status().value("startState").toString(),testCase==0?QString("saved"):QString("unknown"));
            QVERIFY(!job.reviewStart());QVERIFY(!job.submitStart(commitment));
            QVERIFY(!job.status().value("startSubmissionAvailable").toBool());QVERIFY(!job.status().value("executable").toBool());
            if (testCase==0) {
                LitecoinJournalJob resumed;QVERIFY(resumed.configure(path));QVERIFY(resumed.refresh());
                QTRY_VERIFY_WITH_TIMEOUT(!resumed.status().value("busy").toBool(),10000);
                QCOMPARE(resumed.status().value("startState").toString(),QString("saved"));QVERIFY(!resumed.reviewStart());
            }
        }
#endif
    }
    void litecoinIdentitySignaturesStayInTheirProtocol() {
        OfferIdentity identity;
        const QByteArray domain("monzero-ltc-xmz-key-exchange-v1");
        QCOMPARE(identity.signLitecoinNegotiation("fixture"),identity.sign("fixture",domain));
        QVERIFY(identity.signLitecoinNegotiation({}).isEmpty());
        QVERIFY(identity.signLitecoinNegotiation(QByteArray(200001,'x')).isEmpty());
        const QByteArray frame(200000,'x');
        const auto signature=identity.signLitecoinNegotiation(frame);
        QCOMPARE(signature.size(),64);QVERIFY(identity.sign(frame,domain).isEmpty());
        const auto message=QByteArray("MONZERO-DEX-OFFER-V1\n")+QByteArray::number(domain.size())+":"+domain
            +QByteArray::number(frame.size())+":"+frame;
        const auto key=identity.publicKey();
        QCOMPARE(crypto_sign_verify_detached(reinterpret_cast<const unsigned char *>(signature.constData()),
            reinterpret_cast<const unsigned char *>(message.constData()),message.size(),
            reinterpret_cast<const unsigned char *>(key.constData())),0);
        QVERIFY(!OfferIdentity::verify("fixture","wrong-protocol",key,identity.signLitecoinNegotiation("fixture")));
    }
    void actualLitecoinAppSetup() {
        const auto fixture=QString::fromLocal8Bit(qgetenv("MONZERO_LTC_APP_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("Requires owned paired-chain app phase configurations");
        QFile input(fixture);QVERIFY(input.open(QIODevice::ReadOnly));
        const auto request=QJsonDocument::fromJson(input.read(16385)).object();input.close();
        // Live-chain driver: choose the stage before any engine reads configuration.
        if (request.contains("ltcNetwork")) {
            DexDeployment::resetForTesting();
            QVERIFY(DexDeployment::selectLitecoinStage(request.value("ltcNetwork").toString()));
        }
        if (request.contains("appConfigFile")) {
            // A ready-made app configuration, as the trade kit writes it: one
            // trader, configured only from its own files, nothing provisioned here.
            const auto only=request.value("residentRole").toString();QVERIFY(only=="alice" || only=="bob");
            DexEngine alice,bob;auto &engine=only=="alice"?alice:bob;
            if (request.contains("controlPublicKey")) {
                QVERIFY(engine.riskControl()->setLedgerFile(QDir(request.value("residentDirectory").toString()).filePath(only+"-started-trades.json")));
                QVERIFY(engine.riskControl()->setPublicKey(request.value("controlPublicKey").toString()));
                QVERIFY(engine.riskControl()->loadControlFile(request.value("controlFile").toString()));
            }
            QVERIFY(engine.configureLitecoinApp(request.value("appConfigFile").toString()));
            runLitecoinAppFixture(alice,bob,QDir(request.value("residentDirectory").toString()),{only});
            return;
        }
        const bool initial=request.value("initialStartup").toBool();
        QTemporaryDir temporary;QVERIFY(temporary.isValid());
        QDir tls(request.value("appDirectory").toString(temporary.path()));QVERIFY(tls.exists());
        const bool provision=!QFile::exists(tls.filePath("alice-app.json"));
        const auto write=[](const QString &path,const QByteArray &bytes) {
            QSaveFile file(path);return file.open(QIODevice::WriteOnly) && file.setPermissions(QFile::ReadOwner|QFile::WriteOwner)
                && file.write(bytes)==bytes.size() && file.commit();};
        const auto read=[](const QString &path) {QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray{};};
        QList<QSslCertificate> certificates;const QStringList names{"alice","bob"};
        for (const auto &name:names) {
            if (provision) {
                QProcess openssl;openssl.start("openssl",{"req","-x509","-newkey","rsa:2048","-nodes","-keyout",tls.filePath(name+".key"),
                    "-out",tls.filePath(name+".crt"),"-days","1","-subj","/CN=localhost","-addext","subjectAltName=DNS:localhost"});
                QVERIFY(openssl.waitForFinished(15000));QCOMPARE(openssl.exitCode(),0);
                QVERIFY(QFile::setPermissions(tls.filePath(name+".key"),QFile::ReadOwner|QFile::WriteOwner));
                QVERIFY(QFile::setPermissions(tls.filePath(name+".crt"),QFile::ReadOwner|QFile::WriteOwner));
                QVERIFY(write(tls.filePath(name+".password"),request.value(name+"Passphrase").toString().toUtf8()));
            }
            certificates.append(QSslCertificate(read(tls.filePath(name+".crt"))));
        }
        if (provision) QVERIFY(write(tls.filePath("ca.pem"),certificates[0].toPem()+certificates[1].toPem()));
        QTcpServer probe;QVERIFY(probe.listen(QHostAddress::LocalHost,0));
        const auto port=provision?probe.serverPort():static_cast<quint16>(QJsonDocument::fromJson(read(tls.filePath("alice-peer.json"))).object().value("port").toInt());
        probe.close();
        QMap<QString,QByteArray> manifests;
        for (int index=0;index<2;++index) {
            const auto name=names[index];
            const QJsonObject peer{{"schema","monzero-private-ltc-peer-v2"},{"mode",index==0?"listen":"connect"},
                {"address",index==0?request.value("listenAddress").toString("127.0.0.1"):request.value("peerAddress").toString("127.0.0.1")},
                {"port",port},{"peerName","localhost"},
                {"peerCertificateSha256",QString::fromLatin1(certificates[1-index].digest(QCryptographicHash::Sha256).toHex())},
                {"certificateFile",tls.filePath(name+".crt")},{"privateKeyFile",tls.filePath(name+".key")},{"caFile",tls.filePath("ca.pem")},
                {"identityFile",request.value(name+"Identity")},{"passphraseFile",tls.filePath(name+".password")},
                {"settlementConfigFile",request.value(name+"SettlementConfig")}};
            const auto peerBytes=QJsonDocument(peer).toJson(QJsonDocument::Compact);
            if (provision) QVERIFY(write(tls.filePath(name+"-peer.json"),peerBytes));
            else QCOMPARE(read(tls.filePath(name+"-peer.json")),peerBytes);
            const QJsonObject manifest{{"schema","monzero-private-ltc-app-v1"},{"journalConfigFile",request.value(name+"Config")},
                {"peerConfigFile",tls.filePath(name+"-peer.json")},{"paymentConfigFile",request.value(name+"PaymentConfig")},
                {index==0?"fundingConfigFile":"reservationConfigFile",request.value(index==0?"aliceSettlementConfig":"reservationConfig")}};
            const auto bytes=QJsonDocument(manifest).toJson(QJsonDocument::Compact);manifests.insert(name,bytes);
            if (provision) QVERIFY(write(tls.filePath(name+"-app.json"),bytes));
            else QCOMPARE(read(tls.filePath(name+"-app.json")),bytes);
            // Other valid-looking local phase data must not cross the trade boundary.
            const auto payment=QJsonDocument::fromJson(read(request.value(name+"PaymentConfig").toString())).object();
            for (const auto key:{"tradeId","role","policy","pins","metadataDirectory","snapshotDirectory","helperDigest"}) {
                auto changed=payment;changed.insert(key,QString("different-configuration"));
                QVERIFY(write(tls.filePath("changed-payment.json"),QJsonDocument(changed).toJson(QJsonDocument::Compact)));
                auto bad=manifest;bad.insert("paymentConfigFile",tls.filePath("changed-payment.json"));
                QVERIFY(write(tls.filePath("bad-app.json"),QJsonDocument(bad).toJson(QJsonDocument::Compact)));
                DexEngine rejected;QVERIFY(!rejected.configureLitecoinApp(tls.filePath("bad-app.json")));
                QVERIFY(!rejected.litecoinAppStatus().value("configured").toBool());
            }
        }
        const auto retained=[&] {
            QMap<QString,QByteArray> files;
            for (const auto &name:names) {
                const auto config=QJsonDocument::fromJson(read(request.value(name+"Config").toString())).object();
                for (const auto key:{"metadataDirectory","snapshotDirectory"}) {
                    const QDir dir(config.value(key).toString());
                    for (const auto &file:dir.entryInfoList(QDir::Files)) files.insert(file.absoluteFilePath(),read(file.absoluteFilePath()));
                }
            }return files;
        };
        if (request.contains("residentDirectory")) {
            QCOMPARE(retained().isEmpty(),!request.value("resumeResident").toBool());
            DexEngine alice,bob;
            if (request.contains("controlPublicKey")) {
                // Testnet starts need the signed operator document, exactly as the app.
                for (auto *engine:{&alice,&bob}) {
                    const auto name=engine==&alice?QString("alice"):QString("bob");
                    QVERIFY(engine->riskControl()->setLedgerFile(QDir(request.value("residentDirectory").toString()).filePath(name+"-started-trades.json")));
                    QVERIFY(engine->riskControl()->setPublicKey(request.value("controlPublicKey").toString()));
                    // A reopened trade may outlive its document; the app checks it only before new trades.
                    const bool loaded=engine->riskControl()->loadControlFile(request.value("controlFile").toString());
                    QVERIFY(loaded || request.value("resumeResident").toBool());
                }
            }
            // residentRole: host one trader per process (two processes or machines).
            const auto only=request.value("residentRole").toString();
            QVERIFY(only.isEmpty() || only=="alice" || only=="bob");
            const QStringList roles=only.isEmpty()?QStringList{"alice","bob"}:QStringList{only};
            if (roles.contains("alice")) QVERIFY(alice.configureLitecoinApp(tls.filePath("alice-app.json")));
            if (roles.contains("bob")) QVERIFY(bob.configureLitecoinApp(tls.filePath("bob-app.json")));
            runLitecoinAppFixture(alice,bob,QDir(request.value("residentDirectory").toString()),roles);
            return;
        }
        if (initial) {
            QVERIFY(retained().isEmpty());
            QMap<QString,QByteArray> started;
            for (int round=0;round<2;++round) {
                DexEngine alice,bob;
                QVERIFY(alice.configureLitecoinApp(tls.filePath("alice-app.json")));
                QVERIFY(bob.configureLitecoinApp(tls.filePath("bob-app.json")));
                QTest::qWait(20);
                QVERIFY(probe.listen(QHostAddress::LocalHost,port));probe.close();
                for (auto *engine:{&alice,&bob}) {
                    QCOMPARE(engine->litecoinNegotiationStatus().value("state").toString(),QString("configured"));
                    if (round==0) {
                        QVERIFY(!engine->submitLitecoinStart(QString(64,'0')));
                        QVERIFY(!engine->prepareLitecoinFunding());
                        QVERIFY(!engine->prepareLitecoinPayout());QVERIFY(!engine->prepareLitecoinRefund());
                        QVERIFY(!engine->submitLitecoinFunding(QString(64,'0')));
                        QVERIFY(!engine->submitLitecoinPayout(QString(64,'0')));QVERIFY(!engine->submitLitecoinRefund(QString(64,'0')));
                        QVERIFY(engine->requestLitecoinStartReview());
                        QVERIFY(!engine->requestLitecoinPhaseCheck("payment"));
                        QTRY_VERIFY_WITH_TIMEOUT(!engine->litecoinAppStatus().value("busy").toBool(),180000);
                        const auto review=engine->litecoinJournalStatus().value("startReview").toMap();QVERIFY(!review.isEmpty());
                        QCOMPARE(engine->litecoinNegotiationStatus().value("state").toString(),QString("configured"));
                        if (engine==&alice) {QVERIFY(retained().isEmpty());QVERIFY(probe.listen(QHostAddress::LocalHost,port));probe.close();}
                        QVERIFY(engine->submitLitecoinStart(review.value("commitment").toString()));
                    } else QVERIFY(engine->requestLitecoinJournalCheck());
                    QTRY_COMPARE_WITH_TIMEOUT(engine->litecoinJournalStatus().value("startState").toString(),QString("saved"),180000);
                    if (engine==&alice) QTRY_COMPARE_WITH_TIMEOUT(alice.litecoinNegotiationStatus().value("state").toString(),QString("waiting-for-connection"),10000);
                }
                QTRY_VERIFY_WITH_TIMEOUT((alice.litecoinNegotiationStatus().value("complete").toBool() && bob.litecoinNegotiationStatus().value("complete").toBool())
                    || alice.litecoinNegotiationStatus().value("state")=="unknown" || bob.litecoinNegotiationStatus().value("state")=="unknown",300000);
                QVERIFY(alice.litecoinNegotiationStatus().value("complete").toBool());QVERIFY(bob.litecoinNegotiationStatus().value("complete").toBool());
                QVERIFY(!alice.requestLitecoinStartReview());QVERIFY(!bob.requestLitecoinStartReview());
                QVERIFY(!retained().isEmpty());
                if (round==0) started=retained();else QCOMPARE(retained(),started);
            }
            QFile result(QString::fromLocal8Bit(qgetenv("MONZERO_LTC_APP_RESULT")));QVERIFY(result.open(QIODevice::WriteOnly|QIODevice::NewOnly));
            QVERIFY(result.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
            const auto bytes=QJsonDocument(QJsonObject{{"schema","monzero-ltc-app-startup-fixture-v1"},{"deferredUntilSaved",true},
                {"startedAfterApproval",true},{"prematurePaymentsRejected",true},{"restartVerified",true},{"executable",false}}).toJson(QJsonDocument::Compact);
            QCOMPARE(result.write(bytes),qint64(bytes.size()));return;
        }
        const auto saved=retained();
        for (int round=0;round<2;++round) {
            DexEngine alice,bob;
            QVERIFY(alice.configureLitecoinApp(tls.filePath("alice-app.json")));
            QVERIFY(bob.configureLitecoinApp(tls.filePath("bob-app.json")));
            QVERIFY(!alice.configureLitecoinApp(tls.filePath("alice-app.json")));
            for (auto *engine:{&alice,&bob}) {
                QVERIFY(engine->litecoinAppStatus().value("configured").toBool());
                QVERIFY(engine->requestLitecoinPhaseCheck("payment"));
                QVERIFY(!engine->requestLitecoinPhaseCheck("funding"));
                QVERIFY(!engine->requestLitecoinJournalCheck());
                QTRY_VERIFY_WITH_TIMEOUT(!engine->litecoinAppStatus().value("busy").toBool(),180000);
                QVERIFY(engine->litecoinPaymentStatus().value("valid").toBool());
                QVERIFY(engine->litecoinPaymentStatus().value("walletRecoveryConfigured").toBool());
                QVERIFY(!engine->litecoinJournalStatus().value("valid").toBool());
            }
            QVERIFY(alice.requestLitecoinPhaseCheck("funding"));
            QTRY_VERIFY_WITH_TIMEOUT(!alice.litecoinAppStatus().value("busy").toBool(),180000);
            QVERIFY(alice.litecoinFundingStatus().value("valid").toBool());
            QVERIFY(alice.requestLitecoinFundingReview(true));
            QTRY_VERIFY_WITH_TIMEOUT(!alice.litecoinAppStatus().value("busy").toBool(),180000);
            QVERIFY(!alice.litecoinFundingStatus().value("fundingReview").toMap().isEmpty());
            QVERIFY(!bob.requestLitecoinPhaseCheck("funding"));
            for (auto *engine:{&alice,&bob}) {
                QVERIFY(engine->requestLitecoinJournalCheck());
                QTRY_COMPARE_WITH_TIMEOUT(engine->litecoinJournalStatus().value("startState").toString(),QString("saved"),180000);
                if (engine==&alice) QTRY_COMPARE_WITH_TIMEOUT(alice.litecoinNegotiationStatus().value("state").toString(),QString("waiting-for-connection"),10000);
            }
            QTRY_VERIFY_WITH_TIMEOUT((alice.litecoinNegotiationStatus().value("complete").toBool() && bob.litecoinNegotiationStatus().value("complete").toBool())
                || alice.litecoinNegotiationStatus().value("state")=="unknown" || bob.litecoinNegotiationStatus().value("state")=="unknown",300000);
            QVERIFY(alice.litecoinNegotiationStatus().value("complete").toBool());QVERIFY(bob.litecoinNegotiationStatus().value("complete").toBool());
            QTRY_VERIFY_WITH_TIMEOUT(!alice.litecoinAppStatus().value("busy").toBool() && !bob.litecoinAppStatus().value("busy").toBool(),180000);
            const bool refund=request.value("refundPath").toBool();auto &owner=refund?alice:bob;
            QVERIFY(refund?owner.requestLitecoinRefundReview(true):owner.requestLitecoinPayoutReview(true));
            QTRY_VERIFY_WITH_TIMEOUT(!owner.litecoinAppStatus().value("busy").toBool(),180000);
            QVERIFY(!owner.litecoinPaymentStatus().value(refund?"refundReview":"payoutReview").toMap().isEmpty());
            QVERIFY(refund?owner.checkLitecoinRefund():owner.checkLitecoinPayout());
            QTRY_VERIFY_WITH_TIMEOUT(!owner.litecoinAppStatus().value("busy").toBool(),180000);
            QCOMPARE(owner.litecoinPaymentStatus().value(refund?"refundCheckState":"payoutCheckState").toString(),QString("confirmed"));
            QCOMPARE(retained(),saved);
            QVERIFY(write(tls.filePath("alice-app.json"),"{}"));
            QVERIFY(!alice.requestLitecoinPhaseCheck("funding"));
            QCOMPARE(alice.litecoinAppStatus().value("state").toString(),QString("unknown"));
            QVERIFY(!alice.litecoinFundingStatus().value("valid").toBool());
            QVERIFY(alice.litecoinFundingStatus().value("fundingReview").toMap().isEmpty());
            QVERIFY(!alice.submitLitecoinFunding(QString(64,'0')));
            QCOMPARE(retained(),saved);QVERIFY(write(tls.filePath("alice-app.json"),manifests.value("alice")));
        }
        QFile result(QString::fromLocal8Bit(qgetenv("MONZERO_LTC_APP_RESULT")));QVERIFY(result.open(QIODevice::WriteOnly|QIODevice::NewOnly));
        QVERIFY(result.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
        const auto bytes=QJsonDocument(QJsonObject{{"schema","monzero-ltc-app-fixture-v1"},{"bothRolesVerified",true},
            {"phaseIsolationVerified",true},{"changedSetupRejected",true},{"restartVerified",true},{"executable",false}}).toJson(QJsonDocument::Compact);
        QCOMPARE(result.write(bytes),qint64(bytes.size()));
    }
    void actualLitecoinPeerStartup() {
        const auto fixture=QString::fromLocal8Bit(qgetenv("MONZERO_LTC_PEER_STARTUP_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("Requires private peer startup configuration and unstarted isolated trades");
        QFile input(fixture);QVERIFY(input.open(QIODevice::ReadOnly));
        const auto request=QJsonDocument::fromJson(input.read(16385)).object();input.close();
        QTemporaryDir tls;QVERIFY(tls.isValid());
        auto write=[](const QString &path,const QByteArray &bytes) {
            QSaveFile file(path);return file.open(QIODevice::WriteOnly) && file.setPermissions(QFile::ReadOwner|QFile::WriteOwner)
                && file.write(bytes)==bytes.size() && file.commit();};
        QList<QSslCertificate> certificates;
        QStringList names{"alice","bob"};
        for (const auto &name:names) {
            const auto cert=tls.filePath(name+".crt"),key=tls.filePath(name+".key");
            QProcess openssl;openssl.start("openssl",{"req","-x509","-newkey","rsa:2048","-nodes",
                "-keyout",key,"-out",cert,"-days","1","-subj","/CN=localhost","-addext","subjectAltName=DNS:localhost"});
            QVERIFY(openssl.waitForFinished(15000));QCOMPARE(openssl.exitCode(),0);
            QVERIFY(QFile::setPermissions(cert,QFile::ReadOwner|QFile::WriteOwner));
            QVERIFY(QFile::setPermissions(key,QFile::ReadOwner|QFile::WriteOwner));
            QFile file(cert);QVERIFY(file.open(QIODevice::ReadOnly));certificates.append(QSslCertificate(file.readAll()));
            QVERIFY(write(tls.filePath(name+".password"),request.value(name+"Passphrase").toString().toUtf8()));
        }
        const auto ca=tls.filePath("ca.pem");QVERIFY(write(ca,certificates.at(0).toPem()+certificates.at(1).toPem()));
        QTcpServer portProbe;QVERIFY(portProbe.listen(QHostAddress::LocalHost,0));const auto port=portProbe.serverPort();portProbe.close();
        QMap<QString,QJsonObject> configs;
        for (int index=0;index<2;++index) {
            const auto name=names.at(index);
            QJsonObject c{{"schema","monzero-private-ltc-peer-v1"},{"mode",index==0?"listen":"connect"},
                {"address","127.0.0.1"},{"port",port},{"peerName","localhost"},
                {"peerCertificateSha256",QString::fromLatin1(certificates.at(1-index).digest(QCryptographicHash::Sha256).toHex())},
                {"certificateFile",tls.filePath(name+".crt")},{"privateKeyFile",tls.filePath(name+".key")},{"caFile",ca},
                {"identityFile",request.value(name+"Identity")},{"passphraseFile",tls.filePath(name+".password")}};
            configs.insert(name,c);QVERIFY(write(tls.filePath(name+".json"),QJsonDocument(c).toJson(QJsonDocument::Compact)));
        }
        // Reject endpoint expansion, excess authority and incorrect identity
        // before opening any listener or accepting the startup configuration.
        const QList<QPair<QString,QJsonValue>> changes{{"address","0.0.0.0"},{"address","192.0.2.1"},{"address","localhost"},
            {"port",0},{"port",65536},{"port",1.5},{"mode","automatic"},{"peerName",""},
            {"peerCertificateSha256",QString(64,'0')},{"unexpected","authority"}};
        for (const auto &change:changes) {
            auto c=configs.value("alice");c.insert(change.first,change.second);
            const auto path=tls.filePath("invalid.json");QVERIFY(write(path,QJsonDocument(c).toJson(QJsonDocument::Compact)));
            DexEngine engine;LitecoinPeerStartup startup(engine);
            QVERIFY(!startup.configure(path,request.value("aliceConfig").toString()));
        }
        auto mismatched=configs.value("alice");mismatched["identityFile"]=request.value("bobIdentity");
        mismatched["passphraseFile"]=tls.filePath("bob.password");
        const auto invalid=tls.filePath("invalid.json");QVERIFY(write(invalid,QJsonDocument(mismatched).toJson(QJsonDocument::Compact)));
        {DexEngine engine;LitecoinPeerStartup startup(engine);QVERIFY(!startup.configure(invalid,request.value("aliceConfig").toString()));}
        auto retained=[&]() {QMap<QString,QByteArray> result;
            for (const auto &name:names) {
                QFile file(request.value(name+"Config").toString());if (!file.open(QIODevice::ReadOnly)) return QMap<QString,QByteArray>{};
                const auto c=QJsonDocument::fromJson(file.readAll()).object();
                for (const auto field:{"metadataDirectory","snapshotDirectory"}) {
                    QDir dir(c.value(field).toString());for (const auto &entry:dir.entryList(QDir::Files|QDir::Hidden|QDir::NoDotAndDotDot)) {
                        QFile item(dir.filePath(entry));if (!item.open(QIODevice::ReadOnly)) return QMap<QString,QByteArray>{};
                        result.insert(item.fileName(),item.readAll());
                    }
                }
            }return result;};
        QMap<QString,QByteArray> saved;
        for (int round=0;round<3;++round) {
            DexEngine alice,bob;LitecoinPeerStartup aliceStartup(alice),bobStartup(bob);
            QVERIFY(alice.configureLitecoinJournal(request.value("aliceConfig").toString()));
            QVERIFY(bob.configureLitecoinJournal(request.value("bobConfig").toString()));
            QVERIFY(aliceStartup.configure(tls.filePath("alice.json"),request.value("aliceConfig").toString()));
            QVERIFY(bobStartup.configure(tls.filePath("bob.json"),request.value("bobConfig").toString()));
            QTest::qWait(20);
            QCOMPARE(alice.litecoinNegotiationStatus().value("state").toString(),QString("configured"));
            QCOMPARE(bob.litecoinNegotiationStatus().value("state").toString(),QString("configured"));
            QVERIFY(portProbe.listen(QHostAddress::LocalHost,port));portProbe.close();
            if (round==1) {
                QVERIFY(write(tls.filePath("alice.password"),"changed after configuration"));
                QVERIFY(alice.requestLitecoinJournalCheck());
                QTRY_COMPARE_WITH_TIMEOUT(alice.litecoinNegotiationStatus().value("state").toString(),QString("unknown"),180000);
                QCOMPARE(retained(),saved);QVERIFY(portProbe.listen(QHostAddress::LocalHost,port));portProbe.close();
                QVERIFY(write(tls.filePath("alice.password"),request.value("alicePassphrase").toString().toUtf8()));
                continue;
            }
            for (auto *engine:{&alice,&bob}) {
                if (round==0) {
                    QVERIFY(engine->requestLitecoinStartReview());
                    QTRY_VERIFY_WITH_TIMEOUT(!engine->litecoinJournalStatus().value("busy").toBool(),180000);
                    const auto review=engine->litecoinJournalStatus().value("startReview").toMap();QVERIFY(!review.isEmpty());
                    QCOMPARE(engine->litecoinNegotiationStatus().value("state").toString(),QString("configured"));
                    QVERIFY(engine->submitLitecoinStart(review.value("commitment").toString()));
                } else QVERIFY(engine->requestLitecoinJournalCheck());
                QTRY_COMPARE_WITH_TIMEOUT(engine->litecoinJournalStatus().value("startState").toString(),QString("saved"),180000);
                if (engine==&alice) QTRY_COMPARE_WITH_TIMEOUT(alice.litecoinNegotiationStatus().value("state").toString(),QString("waiting-for-connection"),10000);
            }
            QTRY_VERIFY_WITH_TIMEOUT((alice.litecoinNegotiationStatus().value("complete").toBool() && bob.litecoinNegotiationStatus().value("complete").toBool())
                || alice.litecoinNegotiationStatus().value("state").toString()=="unknown" || bob.litecoinNegotiationStatus().value("state").toString()=="unknown",300000);
            QVERIFY2(alice.litecoinNegotiationStatus().value("complete").toBool(),qPrintable(alice.litecoinNegotiationStatus().value("state").toString()));
            QVERIFY2(bob.litecoinNegotiationStatus().value("complete").toBool(),qPrintable(bob.litecoinNegotiationStatus().value("state").toString()));
            QVERIFY(!alice.litecoinNegotiationStatus().value("executable").toBool());
            QVERIFY(!bob.litecoinNegotiationStatus().value("executable").toBool());
            QVERIFY(!alice.requestLitecoinStartReview());QVERIFY(!bob.requestLitecoinStartReview());
            if (round==0) saved=retained();else QCOMPARE(retained(),saved);
            QVERIFY(!saved.isEmpty());
        }
        QFile result(QString::fromLocal8Bit(qgetenv("MONZERO_LTC_PEER_STARTUP_RESULT")));
        QVERIFY(result.open(QIODevice::WriteOnly|QIODevice::NewOnly));QVERIFY(result.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
        const auto bytes=QJsonDocument(QJsonObject{{"schema","monzero-ltc-peer-startup-fixture-v1"},{"deferredUntilSaved",true},
            {"startedAfterApproval",true},{"changedCredentialsRejected",true},{"restartResumeVerified",true},{"executable",false}}).toJson(QJsonDocument::Compact);
        QCOMPARE(result.write(bytes),qint64(bytes.size()));
    }
    void actualLitecoinNegotiationSession() {
        const auto fixture=QString::fromLocal8Bit(qgetenv("MONZERO_LTC_SESSION_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("Requires owned isolated trade journals and encrypted identities");
        QFile input(fixture);QVERIFY(input.open(QIODevice::ReadOnly));
        const auto request=QJsonDocument::fromJson(input.read(16385)).object();input.close();
        const auto reservationConfig=request.value("reservationConfig").toString();
        const bool settlement=!request.value("aliceSettlementConfig").toString().isEmpty();
        const auto recoveryPsbt=request.value("recoveryPsbt").toString().toLatin1();
        QTemporaryDir tls;QVERIFY(tls.isValid());
        QList<QSslCertificate> certs;QList<QSslKey> keys;
        for (const auto name:{"alice","bob"}) {
            const auto certPath=tls.filePath(QString(name)+".crt"),keyPath=tls.filePath(QString(name)+".key");
            QProcess openssl;openssl.start("openssl",{"req","-x509","-newkey","rsa:2048","-nodes",
                "-keyout",keyPath,"-out",certPath,"-days","1","-subj","/CN=localhost","-addext","subjectAltName=DNS:localhost"});
            QVERIFY(openssl.waitForFinished(15000));QCOMPARE(openssl.exitCode(),0);
            QFile cert(certPath),key(keyPath);QVERIFY(cert.open(QIODevice::ReadOnly));QVERIFY(key.open(QIODevice::ReadOnly));
            certs.append(QSslCertificate(cert.readAll()));keys.append(QSslKey(key.readAll(),QSsl::Rsa));
        }
        auto configuration=[&](int index) {auto c=QSslConfiguration::defaultConfiguration();
            c.setCaCertificates(certs);c.setLocalCertificate(certs.at(index));c.setPrivateKey(keys.at(index));return c;};
        if (settlement) for (int index=0;index<2;++index) {
            const QString role=index==0?"alice":"bob",peer=index==0?"bob":"alice";
            const auto certificate=tls.filePath(role+".crt"),key=tls.filePath(role+".key"),ca=tls.filePath(peer+".crt");
            for (const auto &path:{certificate,key,ca}) QVERIFY(QFile::setPermissions(path,QFile::ReadOwner|QFile::WriteOwner));
            const auto passPath=tls.filePath(role+".password"),peerPath=tls.filePath(role+".peer.json");
            QFile pass(passPath);QVERIFY(pass.open(QIODevice::WriteOnly|QIODevice::NewOnly));
            QVERIFY(pass.setPermissions(QFile::ReadOwner|QFile::WriteOwner));pass.write(request.value(role+"Passphrase").toString().toUtf8());pass.close();
            const auto bytes=QJsonDocument(QJsonObject{{"schema","monzero-private-ltc-peer-v2"},{"mode","listen"},
                {"address","127.0.0.1"},{"port",1},{"peerName","localhost"},
                {"peerCertificateSha256",QString::fromLatin1(certs.at(1-index).digest(QCryptographicHash::Sha256).toHex())},
                {"certificateFile",certificate},{"privateKeyFile",key},{"caFile",ca},
                {"identityFile",request.value(role+"Identity")},{"passphraseFile",passPath},
                {"settlementConfigFile",request.value(role+"SettlementConfig")}}).toJson(QJsonDocument::Compact);
            QFile file(peerPath);QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::NewOnly));
            QVERIFY(file.setPermissions(QFile::ReadOwner|QFile::WriteOwner));QCOMPARE(file.write(bytes),qint64(bytes.size()));file.close();
            DexEngine deferred;LitecoinPeerStartup startup(deferred);
            QVERIFY(startup.configure(peerPath,request.value(role+"Config").toString()));
            QVERIFY(deferred.litecoinNegotiationStatus().value("settlementConfigured").toBool());
            QVERIFY(!deferred.litecoinNegotiationStatus().value("transportAuthenticated").toBool());
            QVERIFY(!deferred.startLitecoinRedemption(""));
        }
        auto retained=[&]() {QMap<QString,QByteArray> result;
            for (const auto role:{"alice","bob"}) {
                QFile config(request.value(QString(role)+"Config").toString());if (!config.open(QIODevice::ReadOnly)) return QMap<QString,QByteArray>{};
                const auto c=QJsonDocument::fromJson(config.readAll()).object();
                for (const auto field:{"metadataDirectory","snapshotDirectory"}) {
                    QDir dir(c.value(field).toString());for (const auto &name:dir.entryList(QDir::Files|QDir::Hidden|QDir::NoDotAndDotDot)) {
                        QFile file(dir.filePath(name));if (!file.open(QIODevice::ReadOnly)) return QMap<QString,QByteArray>{};
                        result.insert(file.fileName(),file.readAll());
                    }
                }
            }
            if (!reservationConfig.isEmpty()) {
                QFile wrapper(reservationConfig);if (!wrapper.open(QIODevice::ReadOnly)) return QMap<QString,QByteArray>{};
                const auto w=QJsonDocument::fromJson(wrapper.readAll()).object();
                QFile rpc(w.value("rpcFile").toString());if (!rpc.open(QIODevice::ReadOnly)) return QMap<QString,QByteArray>{};
                QDir dir(QJsonDocument::fromJson(rpc.readAll()).object().value("reservationDirectory").toString());
                for (const auto &name:dir.entryList(QDir::Files|QDir::Hidden|QDir::NoDotAndDotDot)) {
                    QFile file(dir.filePath(name));if (!file.open(QIODevice::ReadOnly)) return QMap<QString,QByteArray>{};
                    result.insert(file.fileName(),file.readAll());
                }
            }
            return result;};
        QMap<QString,QByteArray> saved;
        for (int round=0;round<3;++round) {
            const auto before=retained();QVERIFY(!before.isEmpty());
            LitecoinNegotiationSession alice,bob;DexEngine aliceEngine,bobEngine;
            QVERIFY(alice.configure(request.value("aliceConfig").toString(),request.value("aliceIdentity").toString(),request.value("alicePassphrase").toString().toUtf8()));
            QVERIFY(bob.configure(request.value("bobConfig").toString(),request.value("bobIdentity").toString(),request.value("bobPassphrase").toString().toUtf8()));
            if (settlement) {
                QVERIFY(!bob.configureSettlement(request.value("aliceSettlementConfig").toString()));
                QVERIFY(alice.configureSettlement(request.value("aliceSettlementConfig").toString()));
                QVERIFY(bob.configureSettlement(request.value("bobSettlementConfig").toString()));
                QVERIFY(!bob.startRedemption(QString(64,'0')));
            }
            if (!reservationConfig.isEmpty()) {
                QVERIFY(!aliceEngine.configureLitecoinReservation(reservationConfig,request.value("aliceConfig").toString()));
                QVERIFY(bobEngine.configureLitecoinReservation(reservationConfig,request.value("bobConfig").toString()));
                QVERIFY(!bobEngine.prepareLitecoinReservation());
            }
            QVERIFY(aliceEngine.attachLitecoinNegotiation(&alice));QVERIFY(bobEngine.attachLitecoinNegotiation(&bob));
            PrivateQuoteServer server(configuration(0),certs.at(1).digest(QCryptographicHash::Sha256),nullptr,PrivateQuoteChannel::Profile::LitecoinNegotiation);
            connect(&server,&PrivateQuoteServer::authenticated,&alice,[&alice](PrivateQuoteChannel *channel) {
                QVERIFY(alice.attach(*channel));QVERIFY(alice.start());
            });
            QVERIFY(server.start(QHostAddress::LocalHost,0));
            PrivateQuoteChannel client(configuration(1),round==0?QByteArray(32,'x'):certs.at(0).digest(QCryptographicHash::Sha256),nullptr,PrivateQuoteChannel::Profile::LitecoinNegotiation);
            QVERIFY(!bobEngine.startLitecoinRecovery(recoveryPsbt));
            QVERIFY(bob.attach(client));QVERIFY(bob.start());
            QVERIFY(client.connectToPeer("127.0.0.1",server.serverPort(),"localhost"));
            if (round==0) {
                QTRY_COMPARE_WITH_TIMEOUT(bob.status().value("state").toString(),QString("unknown"),20000);
                QCOMPARE(retained(),before);server.stop();continue;
            }
            QTRY_VERIFY_WITH_TIMEOUT((alice.status().value("complete").toBool() && bob.status().value("complete").toBool())
                || alice.status().value("state").toString()=="unknown" || bob.status().value("state").toString()=="unknown",300000);
            QVERIFY2(alice.status().value("complete").toBool(),qPrintable(alice.status().value("state").toString()));
            QVERIFY2(bob.status().value("complete").toBool(),qPrintable(bob.status().value("state").toString()));
            for (const auto *engine:{&aliceEngine,&bobEngine}) {
                const auto status=engine->litecoinNegotiationStatus();
                QVERIFY(status.value("keysReady").toBool());QVERIFY(status.value("peerAcknowledged").toBool());
                QVERIFY(status.value("transportAuthenticated").toBool());QVERIFY(!status.value("executable").toBool());
            }
            if (!recoveryPsbt.isEmpty() || !reservationConfig.isEmpty()) {
                QVERIFY(!aliceEngine.startLitecoinRecovery(recoveryPsbt));
                QVERIFY(!bobEngine.startLitecoinRecovery("not-a-psbt"));
                if (reservationConfig.isEmpty()) {
                    QVERIFY(bobEngine.startLitecoinRecovery(recoveryPsbt));
                    QVERIFY(!bobEngine.startLitecoinRecovery(recoveryPsbt));
                } else {
                    if (round==1) {
                        QFile config(reservationConfig);QVERIFY(config.open(QIODevice::ReadOnly));const auto original=config.readAll();config.close();
                        QVERIFY(config.open(QIODevice::WriteOnly|QIODevice::Truncate));QCOMPARE(config.write(original+" "),qint64(original.size()+1));config.close();
                        QVERIFY(!bobEngine.prepareLitecoinReservation());
                        QCOMPARE(bobEngine.litecoinReservationStatus().value("state").toString(),QString("unknown"));
                        QCOMPARE(retained(),before);
                        QVERIFY(config.open(QIODevice::WriteOnly|QIODevice::Truncate));QCOMPARE(config.write(original),qint64(original.size()));config.close();
                    }
                    QVERIFY(bobEngine.prepareLitecoinReservation());QVERIFY(!bobEngine.prepareLitecoinReservation());
                }
                QTRY_VERIFY_WITH_TIMEOUT((alice.status().value("recoveryComplete").toBool() && bob.status().value("recoveryComplete").toBool())
                    || alice.status().value("state").toString()=="unknown" || bob.status().value("state").toString()=="unknown",300000);
                QVERIFY2(alice.status().value("recoveryComplete").toBool(),qPrintable(alice.status().value("recoveryState").toString()));
                QVERIFY2(bob.status().value("recoveryComplete").toBool(),qPrintable(bob.status().value("recoveryState").toString()));
                if (!reservationConfig.isEmpty()) {
                    const auto reservation=bobEngine.litecoinReservationStatus();
                    QCOMPARE(reservation.value("state").toString(),QString("reserved"));
                    QVERIFY(!reservation.value("review").toMap().contains("psbt"));
                    QVERIFY(!reservation.value("canPrepare").toBool());
                }
                for (const auto *session:{&alice,&bob}) {
                    const auto status=session->status();
                    QVERIFY(!status.contains("frameHex") && !status.contains("psbt") && !status.contains("signatureHex"));
                    QVERIFY(!status.value("executable").toBool());
                }
            }
            if (settlement) {
                QVERIFY(!bobEngine.startLitecoinRedemption(""));
                QVERIFY(!aliceEngine.startLitecoinRedemption(QString(64,'0')));
                QVERIFY(aliceEngine.startLitecoinRedemption(""));QVERIFY(!aliceEngine.startLitecoinRedemption(""));
                QTRY_VERIFY_WITH_TIMEOUT((bob.status().value("canReleaseRedemption").toBool()
                    && alice.status().value("settlementState").toString()=="waiting-for-redemption")
                    || bob.status().value("state").toString()=="unknown" || alice.status().value("state").toString()=="unknown",300000);
                QVERIFY2(bob.status().value("canReleaseRedemption").toBool(),qPrintable(bob.status().value("settlementState").toString()));
                const auto funding=bob.status().value("nativeFundingTxid").toString();QCOMPARE(funding.size(),64);
                QCOMPARE(funding,alice.status().value("nativeFundingTxid").toString());
                QVERIFY(!bobEngine.startLitecoinRedemption(QString(64,'0')));
                QVERIFY(!bob.status().value("settlementComplete").toBool());
                const auto beforeApproval=retained();
                if (round==1) for (auto it=beforeApproval.cbegin();it!=beforeApproval.cend();++it)
                    QVERIFY(!it.key().endsWith(".ltc-redeem-outgoing") && !it.key().endsWith(".ltc-redeem-completed") && !it.key().endsWith(".ltc-redeem-unsigned"));
                QTest::qWait(100);QCOMPARE(retained(),beforeApproval);
                QVERIFY(bobEngine.startLitecoinRedemption(funding));QVERIFY(!bobEngine.startLitecoinRedemption(funding));
                QTRY_VERIFY_WITH_TIMEOUT((alice.status().value("settlementComplete").toBool() && bob.status().value("settlementComplete").toBool())
                    || alice.status().value("state").toString()=="unknown" || bob.status().value("state").toString()=="unknown",300000);
                QVERIFY2(alice.status().value("settlementComplete").toBool(),qPrintable(alice.status().value("settlementState").toString()));
                QVERIFY2(bob.status().value("settlementComplete").toBool(),qPrintable(bob.status().value("settlementState").toString()));
                QCOMPARE(alice.status().value("redemptionTxid"),bob.status().value("redemptionTxid"));
                for (const auto *session:{&alice,&bob}) {
                    const auto status=session->status();
                    QVERIFY(!status.contains("proofHex") && !status.contains("frameHex") && !status.contains("signatureHex"));
                    QVERIFY(!status.value("executable").toBool());
                }
            }
            if (round==1) saved=retained();else QCOMPARE(retained(),saved);
            if (round==2 && (!recoveryPsbt.isEmpty() || !reservationConfig.isEmpty())) {
                // A wrong delivery acknowledgement must not claim completion,
                // even after valid signatures were durably accepted.
                const auto beforeInvalid=retained();
                const auto status=bob.status();
                const auto bad=QJsonDocument(QJsonObject{{"schema","monzero-ltc-private-negotiation-v1"},
                    {"kind",settlement?"settlement-proof-ack":"recovery-ack"},{"tradeId",status.value("tradeId").toString()},
                    {"quoteCommitment",status.value("quoteCommitment").toString()},{"digest",QString(64,'0')}}).toJson(QJsonDocument::Compact);
                QVERIFY(client.send(bad));
                QTRY_COMPARE_WITH_TIMEOUT(alice.status().value("recoveryState").toString(),QString("unknown"),10000);
                QVERIFY(!alice.status().value("recoveryComplete").toBool());
                if (settlement) QCOMPARE(alice.status().value("settlementState").toString(),QString("unknown"));
                QCOMPARE(retained(),beforeInvalid);
            }
            client.close();server.stop();
        }
        QFile result(QString::fromLocal8Bit(qgetenv("MONZERO_LTC_SESSION_RESULT")));
        QVERIFY(result.open(QIODevice::WriteOnly|QIODevice::NewOnly));QVERIFY(result.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
        const auto bytes=QJsonDocument(QJsonObject{{"schema","monzero-ltc-session-fixture-v1"},{"mutualTlsVerified",true},
            {"wrongPinRejected",true},{"restartRedeliveryVerified",true},{"redemptionSessionVerified",settlement},{"executable",false}}).toJson(QJsonDocument::Compact);
        QCOMPARE(result.write(bytes),qint64(bytes.size()));
    }
    void litecoinRecoverySignatureUsesDistinctDomainAndBound() {
        OfferIdentity identity;
        const auto frame=QByteArray("fixture");
        const auto signature=identity.signLitecoinRecovery(frame);
        QCOMPARE(signature.size(),64);
        auto expected=QByteArray("monzero-ltc-xmz-recovery-v1");
        expected.append('\0');expected.append(QByteArray::fromHex("0000000000000007"));expected.append(frame);
        const auto key=identity.publicKey();
        QCOMPARE(crypto_sign_verify_detached(reinterpret_cast<const unsigned char *>(signature.constData()),
            reinterpret_cast<const unsigned char *>(expected.constData()),expected.size(),
            reinterpret_cast<const unsigned char *>(key.constData())),0);
        QVERIFY(!OfferIdentity::verify(frame,"monzero-ltc-xmz-recovery-v1",key,signature));
        QVERIFY(!OfferIdentity::verify(frame,"monzero-ltc-xmz-key-exchange-v1",identity.publicKey(),signature));
        QVERIFY(identity.signLitecoinRecovery({}).isEmpty());
        QCOMPARE(identity.signLitecoinRecovery(QByteArray(16384,'x')).size(),64);
        QVERIFY(identity.signLitecoinRecovery(QByteArray(16385,'x')).isEmpty());
    }
    void litecoinRedeemSignatureUsesDistinctDomainAndBound() {
        OfferIdentity identity;
        const auto frame=QByteArray("fixture");
        const auto signature=identity.signLitecoinRedeem(frame);
        QCOMPARE(signature.size(),64);
        auto expected=QByteArray("monzero-ltc-xmz-redeem-v1");
        expected.append('\0');expected.append(QByteArray::fromHex("0000000000000007"));expected.append(frame);
        const auto key=identity.publicKey();
        QCOMPARE(crypto_sign_verify_detached(reinterpret_cast<const unsigned char *>(signature.constData()),
            reinterpret_cast<const unsigned char *>(expected.constData()),expected.size(),
            reinterpret_cast<const unsigned char *>(key.constData())),0);
        QVERIFY(!OfferIdentity::verify(frame,"monzero-ltc-xmz-redeem-v1",key,signature));
        QVERIFY(!OfferIdentity::verify(frame,"monzero-ltc-xmz-key-exchange-v1",identity.publicKey(),signature));
        QVERIFY(signature!=identity.signLitecoinRecovery(frame));
        QVERIFY(identity.signLitecoinRedeem({}).isEmpty());
        QCOMPARE(identity.signLitecoinRedeem(QByteArray(16384,'x')).size(),64);
        QVERIFY(identity.signLitecoinRedeem(QByteArray(16385,'x')).isEmpty());
    }
    void bchSuccessSignatureMatchesControllerFramingAndBound() {
        OfferIdentity identity;
        const QByteArray frame("fixture"),expected("MONZERO-DEX-OFFER-V1\n30:monzero-bch-success-adaptor-v17:fixture");
        const auto signature=identity.signBchSuccess(frame),key=identity.publicKey();
        QCOMPARE(signature.size(),64);
        QCOMPARE(crypto_sign_verify_detached(reinterpret_cast<const unsigned char *>(signature.constData()),
            reinterpret_cast<const unsigned char *>(expected.constData()),expected.size(),
            reinterpret_cast<const unsigned char *>(key.constData())),0);
        QVERIFY(OfferIdentity::verify(frame,"monzero-bch-success-adaptor-v1",key,signature));
        QVERIFY(!OfferIdentity::verify(frame,"monzero-ltc-xmz-key-exchange-v1",key,signature));
        QVERIFY(!OfferIdentity::verify(frame,"monzero-bch-native-proof-v1",key,signature));
        QVERIFY(!OfferIdentity::verify("changed","monzero-bch-success-adaptor-v1",key,signature));
        QVERIFY(signature!=identity.signLitecoinRedeem(frame));QVERIFY(signature!=identity.signLitecoinRecovery(frame));
        QVERIFY(identity.signBchSuccess({}).isEmpty());
        QCOMPARE(identity.signBchSuccess(QByteArray(8192,'x')).size(),64);
        QVERIFY(identity.signBchSuccess(QByteArray(8193,'x')).isEmpty());
        const auto binary=QByteArray::fromHex("00ff3a0a80");
        QVERIFY(OfferIdentity::verify(binary,"monzero-bch-success-adaptor-v1",key,identity.signBchSuccess(binary)));
    }
    void actualLitecoinOutgoingSignature() {
        const auto fixture=QString::fromLocal8Bit(qgetenv("MONZERO_LTC_SIGN_FIXTURE"));
        if (fixture.isEmpty()) QSKIP("Requires an owned isolated outgoing-signature fixture");
        QFile file(fixture);QVERIFY(file.open(QIODevice::ReadOnly));
        const auto request=QJsonDocument::fromJson(file.read(402049)).object();file.close();
        const auto frame=QByteArray::fromHex(request.value("frameHex").toString().toLatin1());
        QVERIFY(!frame.isEmpty() && frame.size()<=200000);
        auto password=request.value("passphrase").toString().toUtf8();
        OfferIdentityStore store(request.value("identityFile").toString(),password,OfferIdentityStore::Mode::OpenExisting);
        sodium_memzero(password.data(),password.size());QVERIFY(store.ready());
        QCOMPARE(store.identity()->publicKey().toHex(),request.value("publicKey").toString().toLatin1());
        const auto protocol=request.value("protocol").toString();
        QVERIFY(protocol.isEmpty() || protocol=="recovery" || protocol=="redeem");
        const auto signature=protocol=="redeem"?store.identity()->signLitecoinRedeem(frame)
            :protocol=="recovery"?store.identity()->signLitecoinRecovery(frame)
            :store.identity()->signLitecoinNegotiation(frame);QCOMPARE(signature.size(),64);
        QFile result(QString::fromLocal8Bit(qgetenv("MONZERO_LTC_SIGN_RESULT")));
        QVERIFY(result.open(QIODevice::WriteOnly|QIODevice::NewOnly));
        QVERIFY(result.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
        QCOMPARE(result.write(signature.toHex()),qint64(128));result.close();
    }
    void actualLitecoinStartup() {
        const auto config=QString::fromLocal8Bit(qgetenv("MONZERO_LTC_START_FIXTURE"));
        if (config.isEmpty()) QSKIP("Requires an owned isolated signed-quote startup fixture");
        LitecoinJournalJob job;QVERIFY(job.configure(config));
        QVERIFY(!job.submitStart(QString(64,'0')));QVERIFY(job.reviewStart());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),180000);
        QCOMPARE(job.status().value("startState").toString(),QString("reviewed"));
        const auto review=job.status().value("startReview").toMap();
        const auto commitment=review.value("commitment").toString();
        QVERIFY(job.status().value("startSubmissionAvailable").toBool());
        QVERIFY(!job.submitStart(QString(64,'0')));QVERIFY(!job.submitFunding(commitment));
        QVERIFY(!job.submitPayout(commitment));QVERIFY(!job.submitRefund(commitment));
        QVERIFY(job.submitStart(commitment));QVERIFY(!job.submitStart(commitment));
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),180000);
        QCOMPARE(job.status().value("startState").toString(),QString("saved"));
        QVERIFY(job.status().value("valid").toBool());QVERIFY(!job.reviewStart());
        QVERIFY(!job.status().value("startSubmissionAvailable").toBool());
        const auto snapshot=job.status().value("snapshot").toMap();
        QVERIFY(!snapshot.value("keysReady").toBool());
        QVERIFY(!snapshot.value("nativeFunding").toMap().value("prepared").toBool());
        LitecoinJournalJob resumed;QVERIFY(resumed.configure(config));QVERIFY(resumed.refresh());
        QTRY_VERIFY_WITH_TIMEOUT(!resumed.status().value("busy").toBool(),180000);
        QCOMPARE(resumed.status().value("startState").toString(),QString("saved"));
        QVERIFY(!resumed.reviewStart());QVERIFY(!resumed.submitStart(commitment));
        QCOMPARE(resumed.status().value("snapshot").toMap(),snapshot);
        const auto resultPath=QString::fromLocal8Bit(qgetenv("MONZERO_LTC_START_RESULT"));
        QVERIFY(!resultPath.isEmpty());QFile result(resultPath);QVERIFY(result.open(QIODevice::WriteOnly|QIODevice::NewOnly));
        QVERIFY(result.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
        const auto bytes=QJsonDocument(QJsonObject{{"review",QJsonObject{{"details",QJsonObject::fromVariantMap(review.value("details").toMap())},
            {"commitment",commitment}}},{"snapshot",QJsonObject::fromVariantMap(snapshot)}}).toJson(QJsonDocument::Compact);
        QCOMPARE(result.write(bytes),qint64(bytes.size()));result.close();
    }
    void actualLitecoinJournalHelper() {
        const auto config=QString::fromLocal8Bit(qgetenv("MONZERO_LTC_JOURNAL_FIXTURE"));
        if (config.isEmpty()) QSKIP("Requires an owned isolated paired-chain journal fixture");
        LitecoinJournalJob job;
        QVERIFY(job.configure(config));QVERIFY(job.refresh());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),180000);
        QVERIFY(job.status().value("valid").toBool());
        QVERIFY(!job.status().value("executable").toBool());
        const auto snapshot=job.status().value("snapshot").toMap();
        QVERIFY(snapshot.value("journalValidated").toBool());
        QVERIFY(!snapshot.value("chainObserved").toBool());
        QCOMPARE(snapshot.value("role").toString(),QString("native-funder"));
        const auto refund=snapshot.value("nativeRefundPayout").toMap();
        QVERIFY(refund.value("prepared").toBool());QVERIFY(refund.value("attempted").toBool());
        QVERIFY(refund.value("acknowledged").toBool());
        QVERIFY(job.reviewRefund(true));
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),180000);
        QVERIFY(job.status().value("valid").toBool());
        const auto review=job.status().value("refundReview").toMap();
        QCOMPARE(review.value("details").toMap().value("destination"),snapshot.value("xmzRefund"));
        QCOMPARE(review.value("details").toMap().value("action").toString(),QString("retransmit"));
        QVERIFY(!review.value("commitment").toString().isEmpty());
        QVERIFY(!job.status().value("refundSubmissionAvailable").toBool());
        QVERIFY(!job.submitRefund(review.value("commitment").toString()));
    }
    void actualLitecoinFundingReview() {
        const auto config=QString::fromLocal8Bit(qgetenv("MONZERO_LTC_FUNDING_REVIEW_FIXTURE"));
        if (config.isEmpty()) QSKIP("Requires an owned isolated native-funding fixture");
        LitecoinJournalJob job;QVERIFY(job.configure(config));QVERIFY(job.refresh());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),180000);
        QVERIFY(job.status().value("valid").toBool());QVERIFY(job.reviewFunding(true));
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),180000);
        QVERIFY(job.status().value("valid").toBool());
        const auto review=job.status().value("fundingReview").toMap();
        const auto details=review.value("details").toMap();
        const auto snapshot=job.status().value("snapshot").toMap();
        QCOMPARE(details.value("amountAtoms"),snapshot.value("xmzAtoms"));
        QCOMPARE(details.value("ltcAtoms"),snapshot.value("ltcAtoms"));
        QCOMPARE(details.value("safetyBlocks").toString(),QString("2"));
        QCOMPARE(details.value("action").toString(),QString("retransmit"));
        QVERIFY(!review.value("commitment").toString().isEmpty());
        QVERIFY(!review.value("expiresAtUtc").toString().isEmpty());
        QVERIFY(job.status().value("refundReview").toMap().isEmpty());
        QVERIFY(!job.submitRefund(review.value("commitment").toString()));
        QVERIFY(!job.submitPayout(review.value("commitment").toString()));
        QVERIFY(!job.status().value("executable").toBool());
    }
    void actualLitecoinFundingPreparation() {
        const auto config=QString::fromLocal8Bit(qgetenv("MONZERO_LTC_FUNDING_PREPARE_FIXTURE"));
        if (config.isEmpty()) QSKIP("Requires isolated native-funding preparation fixture");
        LitecoinJournalJob job;QVERIFY(job.configure(config));QVERIFY(!job.prepareFunding());
        QVERIFY(job.refresh());QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),180000);
        QVERIFY(job.status().value("fundingPreparationAvailable").toBool());
        QVERIFY(job.prepareFunding());QVERIFY(!job.prepareFunding());QVERIFY(!job.submitFunding(QString(64,'0')));
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),300000);
        QVERIFY(job.status().value("valid").toBool());
        QCOMPARE(job.status().value("fundingPreparationState").toString(),QString("prepared"));
        const auto review=job.status().value("fundingReview").toMap();
        QVERIFY(!review.value("commitment").toString().isEmpty());
        QVERIFY(!review.value("amounts").toMap().isEmpty());
        const auto snapshot=job.status().value("snapshot").toMap();
        QVERIFY(!snapshot.value("nativeFunding").toMap().value("attempted").toBool());
        QVERIFY(!job.prepareFunding());
        LitecoinJournalJob resumed;QVERIFY(resumed.configure(config));QVERIFY(resumed.refresh());
        QTRY_VERIFY_WITH_TIMEOUT(!resumed.status().value("busy").toBool(),180000);
        QVERIFY(!resumed.prepareFunding());QVERIFY(resumed.reviewFunding(false));
        QTRY_VERIFY_WITH_TIMEOUT(!resumed.status().value("busy").toBool(),180000);
        QCOMPARE(resumed.status().value("fundingReview").toMap(),review);
        QFile output(QString::fromLocal8Bit(qgetenv("MONZERO_LTC_FUNDING_PREPARE_RESULT")));
        QVERIFY(output.open(QIODevice::WriteOnly|QIODevice::NewOnly));
        QVERIFY(output.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner));
        const auto bytes=QJsonDocument(QJsonObject{{"snapshot",QJsonObject::fromVariantMap(snapshot)},
            {"review",QJsonObject{{"details",QJsonObject::fromVariantMap(review.value("details").toMap())},
                {"commitment",review.value("commitment").toString()}}}}).toJson(QJsonDocument::Compact);
        QCOMPARE(output.write(bytes),qint64(bytes.size()));
    }
    void litecoinRedemptionRejectsMalformedResponsesAndChangedSources() {
        const auto trade=QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QJsonObject payment{{"prepared",true},{"attempted",true},{"acknowledged",true}},
            retained{{"prepared",true},{"attempted",false},{"acknowledged",false}},recovery{{"attempted",false},{"acknowledged",false}};
        const QJsonObject snapshot{{"schema","monzero-ltc-journal-status-v1"},{"tradeId",trade},{"quoteCommitment",QString(64,'a')},
            {"role","native-funder"},{"ltcNetwork","ltc-regtest"},{"xmzGenesis",QString(64,'b')},{"ltcAtoms","100"},{"xmzAtoms","200"},
            {"ltcPayout","rltc1q"+QString(38,'q')},{"ltcRefund","fixture-refund"},{"xmzPayout","fixture-payout"},{"xmzRefund","fixture-refund"},
            {"keysReady",true},{"recoveryIntentRetained",true},{"nativeClaimRetained",false},{"nativeRefundRetained",false},
            {"journalValidated",true},{"chainObserved",false},{"executable",false},{"nativeFunding",payment},{"litecoinRedemption",retained},
            {"nativePayout",retained},{"nativeRefundPayout",retained},{"litecoinRecovery",QJsonObject{{"cancel",recovery},{"punish",recovery}}}};
        // Synthetic responses exercise the launcher boundary, not chain evidence.
        for (int testCase=0;testCase<20;++testCase) {
            QTemporaryDir dir;QVERIFY(dir.isValid());
            const auto program=dir.path()+"/helper",rpcPath=dir.path()+"/rpc.json",path=dir.path()+"/config.json";
            QByteArray script="#!/usr/bin/python3\nimport json,sys,base64,hashlib\nr=json.load(sys.stdin)\ns=json.loads(base64.b64decode('"
                +QJsonDocument(snapshot).toJson(QJsonDocument::Compact).toBase64()+"'))\n";
            script+="d={'schema':'monzero-ltc-redemption-review-v1','tradeId':s['tradeId'],'quoteCommitment':s['quoteCommitment'],'journalCommitment':'c'*64,'completedCommitment':'d'*64,'transactionCommitment':'e'*64,'dispatchCommitment':None,'action':'send','txid':'f'*64,'wtxid':'1'*64,'inputTxid':'2'*64,'inputVout':'0','sequence':'0','destination':s['ltcPayout'],'inputAtoms':'100','amountAtoms':'99','feeAtoms':'1','chainObserved':False}\n";
            if (testCase==1) script+="d['amountAtoms']='98'\n";
            if (testCase==2) script+="d['destination']='rltc1q'+'p'*38\n";
            if (testCase==3) script+="d['quoteCommitment']='0'*64\n";
            if (testCase==4) script+="d['txid']='F'*64\n";
            if (testCase==5) script+="d['feeAtoms']='18446744073709551616'\n";
            if (testCase==7) script+="d['privateKey']='unexpected'\n";
            if (testCase==8) script+="s['litecoinRedemption']['attempted']=True\n";
            if (testCase==9) script+="d['chainObserved']=True\n";
            if (testCase==12) script+="s['xmzGenesis']='0'*64\n";
            script+="c=hashlib.sha256(b'MONZERO-LTC-REDEMPTION-APPROVAL-V1\\0'+json.dumps(d,sort_keys=True,separators=(',',':')).encode()).hexdigest()\n";
            if (testCase==6) script+="c='0'*64\n";
            script+="response={'schema':'monzero-ltc-redemption-review-response-v1','requestId':r['requestId'],'snapshot':s,'review':{'details':d,'commitment':c}}\n";
            if (testCase==10) script+="response['requestId']='wrong-request'\n";
            if (testCase==11) script+="response['extra']=True\n";
            script+="if r['schema']=='monzero-ltc-redemption-check-request-v1':\n response.pop('review');response['schema']='monzero-ltc-redemption-check-response-v1'\n response['observation']={'schema':'monzero-ltc-redemption-confirmation-v1','tradeId':s['tradeId'],'quoteCommitment':s['quoteCommitment'],'txid':'f'*64,'wtxid':'1'*64,'block':'2'*64,'tip':'3'*64,'confirmations':'2','requiredConfirmations':'2','chainObserved':True}\n";
            if (testCase==14) script+=" response['observation']['confirmations']='1'\n";
            if (testCase==15) script+=" response['observation']['requiredConfirmations']='1'\n";
            if (testCase==16) script+=" response['observation']['confirmations']='02'\n";
            if (testCase==17) script+=" response['observation']['block']='not-a-block'\n";
            if (testCase==18) script+=" response['observation']['chainObserved']=False\n";
            if (testCase==19) script+=" response['observation']['extra']=True\n";
            script+="sys.stdout.write(json.dumps(response,sort_keys=True,separators=(',',':')))\n";
            QFile helper(program);QVERIFY(helper.open(QIODevice::WriteOnly));QCOMPARE(helper.write(script),qint64(script.size()));helper.close();
            QVERIFY(helper.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
            QFile rpcFile(rpcPath);QVERIFY(rpcFile.open(QIODevice::WriteOnly));QCOMPARE(rpcFile.write("{}"),qint64(2));rpcFile.close();
            QVERIFY(rpcFile.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
            const auto digest=QString::fromLatin1(QCryptographicHash::hash(script,QCryptographicHash::Sha256).toHex());
            const auto bytes=QJsonDocument(QJsonObject{{"schema","monzero-private-ltc-journal-v4"},{"fundingRpcFile",rpcPath},
                {"helper",program},{"helperDigest",digest},{"tradeId",trade},{"role","native-funder"},
                {"metadataDirectory",dir.path()},{"snapshotDirectory",dir.path()},{"pins",QJsonArray{}},
                {"policy",QJsonObject{{"ltc_network","ltc-regtest"},{"xmz_confirmations",10},{"ltc_confirmations",2},{"xmz_genesis",QString(64,'b')}}}}).toJson(QJsonDocument::Compact);
            QFile config(path);QVERIFY(config.open(QIODevice::WriteOnly));QCOMPARE(config.write(bytes),qint64(bytes.size()));config.close();
            QVERIFY(config.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
            DexEngine engine;QVERIFY(engine.configureLitecoinJournal(path));
            QVERIFY(!engine.submitLitecoinRedemptionTransaction(QString(64,'0')));
            if (testCase<13) {
                QVERIFY(engine.reviewLitecoinRedemptionTransaction());
                QTRY_VERIFY_WITH_TIMEOUT(!engine.litecoinRedemptionStatus().value("busy").toBool(),10000);
                QCOMPARE(engine.litecoinRedemptionStatus().value("state").toString(),testCase==0?QString("reviewed"):QString("unknown"));
                QCOMPARE(engine.litecoinRedemptionStatus().value("submissionAvailable").toBool(),testCase==0);
                if (testCase==0) {
                    const auto consent=engine.litecoinRedemptionStatus().value("review").toMap().value("commitment").toString();
                    QVERIFY(rpcFile.open(QIODevice::WriteOnly|QIODevice::Truncate));rpcFile.write("{\"changed\":true}");rpcFile.close();
                    QVERIFY(!engine.submitLitecoinRedemptionTransaction(consent));
                    QVERIFY(engine.litecoinRedemptionStatus().value("review").toMap().isEmpty());
                    QCOMPARE(engine.litecoinRedemptionStatus().value("state").toString(),QString("unknown"));
                }
            } else {
                QVERIFY(engine.checkLitecoinRedemptionTransaction());
                QTRY_VERIFY_WITH_TIMEOUT(!engine.litecoinRedemptionStatus().value("busy").toBool(),10000);
                QCOMPARE(engine.litecoinRedemptionStatus().value("checkState").toString(),testCase==13?QString("confirmed"):QString("unknown"));
                QCOMPARE(engine.litecoinRedemptionStatus().value("observation").toMap().isEmpty(),testCase!=13);
            }
        }
    }
    void actualLitecoinRedemptionTransaction() {
        const auto config=QString::fromLocal8Bit(qgetenv("MONZERO_LTC_REDEMPTION_CONFIG"));
        if (config.isEmpty()) QSKIP("Requires an owned completed Litecoin redemption fixture");
        const bool checking=qgetenv("MONZERO_LTC_REDEMPTION_OPERATION")=="check";
        const bool retry=qgetenv("MONZERO_LTC_REDEMPTION_RETRY")=="1",reject=qgetenv("MONZERO_LTC_REDEMPTION_REJECT")=="1";
        const auto txid=QString::fromLatin1(qgetenv("MONZERO_LTC_REDEMPTION_TXID"));
        const auto gate=QString::fromLocal8Bit(qgetenv("MONZERO_LTC_REDEMPTION_REORG_GATE"));
        DexEngine engine;QVERIFY(engine.configureLitecoinJournal(config));
        QVERIFY(engine.litecoinRedemptionStatus().value("configured").toBool());
        QVERIFY(!engine.submitLitecoinRedemptionTransaction(QString(64,'0')));
        if (checking) {
            QVERIFY(engine.checkLitecoinRedemptionTransaction());QVERIFY(!engine.checkLitecoinRedemptionTransaction());
            QTRY_VERIFY_WITH_TIMEOUT(!engine.litecoinRedemptionStatus().value("busy").toBool(),180000);
            const auto status=engine.litecoinRedemptionStatus(),observation=status.value("observation").toMap();
            QCOMPARE(status.value("checkState").toString(),reject?QString("unknown"):QString("confirmed"));
            if (reject) QVERIFY(observation.isEmpty());else {
                QCOMPARE(observation.value("txid").toString(),txid);
                QVERIFY(observation.value("confirmations").toString().toULongLong()>=observation.value("requiredConfirmations").toString().toULongLong());
            }
        } else {
            QVERIFY(engine.reviewLitecoinRedemptionTransaction());QVERIFY(!engine.reviewLitecoinRedemptionTransaction());
            QTRY_VERIFY_WITH_TIMEOUT(!engine.litecoinRedemptionStatus().value("busy").toBool(),180000);
            const auto review=engine.litecoinRedemptionStatus().value("review").toMap(),details=review.value("details").toMap();
            QVERIFY(!review.isEmpty());QCOMPARE(details.value("txid").toString(),txid);
            QCOMPARE(details.value("action").toString(),retry?QString("retransmit"):QString("send"));
            QVERIFY(!details.value("chainObserved").toBool());
            QVERIFY(!engine.submitLitecoinRedemptionTransaction(QString(64,'0')));
            const auto approval=review.value("commitment").toString();
            QVERIFY(engine.submitLitecoinRedemptionTransaction(approval));
            QVERIFY(!engine.submitLitecoinRedemptionTransaction(approval));
            QVERIFY(engine.litecoinRedemptionStatus().value("review").toMap().isEmpty());
            QTRY_VERIFY_WITH_TIMEOUT(!engine.litecoinRedemptionStatus().value("busy").toBool(),180000);
            const auto status=engine.litecoinRedemptionStatus();
            QCOMPARE(status.value("state").toString(),reject?QString("unknown"):retry?QString("mempool"):QString("acknowledged"));
            QVERIFY(!engine.submitLitecoinRedemptionTransaction(approval));
            if (!reject) QCOMPARE(status.value("submission").toMap().value("txid").toString(),txid);
        }
        QFile output(QString::fromLocal8Bit(qgetenv("MONZERO_LTC_REDEMPTION_RESULT")));QVERIFY(output.open(QIODevice::WriteOnly|QIODevice::NewOnly));
        QVERIFY(output.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
        const auto bytes=QJsonDocument(QJsonObject::fromVariantMap(engine.litecoinRedemptionStatus())).toJson(QJsonDocument::Compact);
        QCOMPARE(output.write(bytes),qint64(bytes.size()));QVERIFY(output.flush());output.close();
        if (!gate.isEmpty()) {
            QVERIFY(checking && !reject);QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(gate),180000);
            QVERIFY(engine.checkLitecoinRedemptionTransaction());
            QVERIFY(engine.litecoinRedemptionStatus().value("observation").toMap().isEmpty());
            QCOMPARE(engine.litecoinRedemptionStatus().value("checkState").toString(),QString("checking"));
            QTRY_VERIFY_WITH_TIMEOUT(!engine.litecoinRedemptionStatus().value("busy").toBool(),180000);
            QCOMPARE(engine.litecoinRedemptionStatus().value("checkState").toString(),QString("unknown"));
            QVERIFY(engine.litecoinRedemptionStatus().value("observation").toMap().isEmpty());
            QVERIFY(!engine.litecoinRedemptionStatus().value("submissionAvailable").toBool());
        }
    }
    void actualLitecoinRecoveryConfirmation() {
        const auto config=QString::fromLocal8Bit(qgetenv("MONZERO_LTC_RECOVERY_CHECK_CONFIG"));
        if (config.isEmpty()) QSKIP("Requires an owned Litecoin recovery confirmation fixture");
        const auto kind=QString::fromLatin1(qgetenv("MONZERO_LTC_RECOVERY_CHECK_KIND"));
        const auto gate=QString::fromLocal8Bit(qgetenv("MONZERO_LTC_RECOVERY_CHECK_REORG_GATE"));
        const bool reject=qgetenv("MONZERO_LTC_RECOVERY_CHECK_REJECT")=="1";
        DexEngine engine;QVERIFY(engine.configureLitecoinReservation(config,QString::fromLocal8Bit(qgetenv("MONZERO_LTC_RECOVERY_CHECK_JOURNAL"))));
        const bool native=engine.litecoinReservationStatus().value("role")=="native-funder";
        QVERIFY(!engine.checkLitecoinRecoverySpend(native?"refund":"punish"));
        QVERIFY(engine.checkLitecoinRecoverySpend(kind));QVERIFY(!engine.checkLitecoinRecoverySpend(kind));
        QTRY_VERIFY_WITH_TIMEOUT(!engine.litecoinReservationStatus().value("busy").toBool(),180000);
        const auto status=engine.litecoinReservationStatus(),observation=status.value("recoveryObservation").toMap();
        QCOMPARE(status.value("recoveryCheckState").toString(),reject?QString("unknown"):QString("confirmed"));
        if (reject) QVERIFY(observation.isEmpty());else {
            QCOMPARE(observation.value("operation").toString(),kind);
            QCOMPARE(observation.value("txid").toString(),QString::fromLatin1(qgetenv("MONZERO_LTC_RECOVERY_CHECK_TXID")));
            QVERIFY(observation.value("confirmations").toString().toULongLong()>=observation.value("requiredConfirmations").toString().toULongLong());
        }
        QVERIFY(!engine.submitLitecoinRecoverySpend(QString(64,'0')));
        QFile output(QString::fromLocal8Bit(qgetenv("MONZERO_LTC_RECOVERY_CHECK_RESULT")));QVERIFY(output.open(QIODevice::WriteOnly|QIODevice::NewOnly));
        QVERIFY(output.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
        const auto bytes=QJsonDocument(QJsonObject::fromVariantMap(status)).toJson(QJsonDocument::Compact);
        QCOMPARE(output.write(bytes),qint64(bytes.size()));QVERIFY(output.flush());output.close();
        if (!gate.isEmpty()) {
            QVERIFY(!reject);QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(gate),180000);
            QVERIFY(engine.checkLitecoinRecoverySpend(kind));
            QVERIFY(engine.litecoinReservationStatus().value("recoveryObservation").toMap().isEmpty());
            QCOMPARE(engine.litecoinReservationStatus().value("recoveryCheckState").toString(),QString("checking"));
            QTRY_VERIFY_WITH_TIMEOUT(!engine.litecoinReservationStatus().value("busy").toBool(),180000);
            QCOMPARE(engine.litecoinReservationStatus().value("recoveryCheckState").toString(),QString("unknown"));
            QVERIFY(engine.litecoinReservationStatus().value("recoveryObservation").toMap().isEmpty());
            QVERIFY(!engine.litecoinReservationStatus().value("recoverySubmissionAvailable").toBool());
        }
    }
    void actualLitecoinRecoverySpend() {
        const auto config=QString::fromLocal8Bit(qgetenv("MONZERO_LTC_RECOVERY_SPEND_CONFIG"));
        if (config.isEmpty()) QSKIP("Requires an owned Litecoin recovery transaction fixture");
        const auto kind=QString::fromLatin1(qgetenv("MONZERO_LTC_RECOVERY_SPEND_KIND"));
        const bool retry=qgetenv("MONZERO_LTC_RECOVERY_SPEND_RETRY")=="1",reject=qgetenv("MONZERO_LTC_RECOVERY_SPEND_REJECT")=="1";
        DexEngine engine;QVERIFY(engine.configureLitecoinReservation(config,QString::fromLocal8Bit(qgetenv("MONZERO_LTC_RECOVERY_SPEND_JOURNAL"))));
        const bool native=engine.litecoinReservationStatus().value("role")=="native-funder";
        QVERIFY(!engine.reviewLitecoinRecoverySpend(native?"refund":"punish"));QVERIFY(!engine.submitLitecoinRecoverySpend(QString(64,'0')));
        QVERIFY(engine.reviewLitecoinRecoverySpend(kind));
        QTRY_VERIFY_WITH_TIMEOUT(!engine.litecoinReservationStatus().value("busy").toBool(),180000);
        const auto review=engine.litecoinReservationStatus().value("recoveryReview").toMap();QVERIFY(!review.isEmpty());
        const auto details=review.value("details").toMap();QCOMPARE(details.value("operation").toString(),kind);
        QCOMPARE(details.value("action").toString(),retry?QString("retransmit"):QString("send"));
        QVERIFY(!engine.submitLitecoinLockFunding(review.value("commitment").toString()));
        QVERIFY(!engine.submitLitecoinRecoverySpend(QString(64,'0')));
        QVERIFY(engine.submitLitecoinRecoverySpend(review.value("commitment").toString()));
        QVERIFY(!engine.submitLitecoinRecoverySpend(review.value("commitment").toString()));
        QVERIFY(engine.litecoinReservationStatus().value("recoveryReview").toMap().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!engine.litecoinReservationStatus().value("busy").toBool(),180000);
        const auto status=engine.litecoinReservationStatus(),outcome=status.value("recoverySubmission").toMap();
        QCOMPARE(status.value("recoveryState").toString(),reject?QString("unknown"):retry?QString("mempool"):QString("acknowledged"));
        if (reject) QVERIFY(outcome.isEmpty());else QCOMPARE(outcome.value("txid"),details.value("txid"));
        QFile output(QString::fromLocal8Bit(qgetenv("MONZERO_LTC_RECOVERY_SPEND_RESULT")));QVERIFY(output.open(QIODevice::WriteOnly));
        QVERIFY(output.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
        output.write(QJsonDocument(QJsonObject{{"review",QJsonObject::fromVariantMap(review)},{"outcome",QJsonObject::fromVariantMap(outcome)}}).toJson(QJsonDocument::Compact));
    }
    void actualLitecoinLockFunding() {
        const auto config=QString::fromLocal8Bit(qgetenv("MONZERO_LTC_LOCK_CONFIG"));
        if (config.isEmpty()) QSKIP("Requires an owned isolated Litecoin lock fixture");
        DexEngine engine;
        QVERIFY(engine.configureLitecoinReservation(config,QString::fromLocal8Bit(qgetenv("MONZERO_LTC_LOCK_JOURNAL"))));
        QVERIFY(!engine.submitLitecoinLockFunding(QString(64,'0')));
        QVERIFY(engine.reviewLitecoinLockFunding());
        QTRY_VERIFY_WITH_TIMEOUT(!engine.litecoinReservationStatus().value("busy").toBool(),180000);
        const auto review=engine.litecoinReservationStatus().value("fundingReview").toMap();
        QVERIFY2(!review.isEmpty(),qPrintable(engine.litecoinReservationStatus().value("fundingState").toString()));
        const bool retry=qgetenv("MONZERO_LTC_LOCK_RETRY")=="1";
        QCOMPARE(review.value("details").toMap().value("action").toString(),retry?QString("retransmit"):QString("send"));
        const auto token=review.value("commitment").toString();
        QVERIFY(!engine.submitLitecoinLockFunding(QString(64,'0')));
        QVERIFY(engine.submitLitecoinLockFunding(token));QVERIFY(!engine.submitLitecoinLockFunding(token));
        QVERIFY(engine.litecoinReservationStatus().value("fundingReview").toMap().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!engine.litecoinReservationStatus().value("busy").toBool(),180000);
        const auto outcome=engine.litecoinReservationStatus().value("fundingSubmission").toMap();
        QCOMPARE(outcome.value("state").toString(),retry?QString("observed"):QString("submitted"));
        if (retry) QCOMPARE(outcome.value("observationState").toString(),QString("mempool"));
        QCOMPARE(outcome.value("txid"),review.value("details").toMap().value("txid"));
        QVERIFY(!engine.submitLitecoinLockFunding(token));
        QFile output(QString::fromLocal8Bit(qgetenv("MONZERO_LTC_LOCK_RESULT")));QVERIFY(output.open(QIODevice::WriteOnly));
        QVERIFY(output.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
        output.write(QJsonDocument(QJsonObject{{"review",QJsonObject::fromVariantMap(review)},{"outcome",QJsonObject::fromVariantMap(outcome)}}).toJson(QJsonDocument::Compact));
    }
    void litecoinIndexedSettlementConfigurationRejectsBlockOverrides() {
#ifdef Q_OS_LINUX
        for (const auto &purpose:QStringList{"payout","refund"}) {for (int testCase=0;testCase<5;++testCase) {
            QTemporaryDir dir;QVERIFY(dir.isValid());
            const auto write=[](const QString &path,const QByteArray &bytes,bool executable=false) {
                QFile file(path);if (!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)) return false;
                if (file.write(bytes)!=bytes.size()) return false;file.close();
                return file.setPermissions(QFile::ReadOwner|QFile::WriteOwner|(executable?QFile::ExeOwner:QFile::Permissions{}));
            };
            const auto program=dir.path()+"/helper",rpcPath=dir.path()+"/rpc.json",configPath=dir.path()+"/journal.json";
            const QByteArray script("#!/bin/sh\nexit 1\n");QVERIFY(write(program,script,true));
            QJsonObject rpc{{"schema","monzero-private-ltc-"+purpose+"-rpc-v4"},{"ltcPort",1},{"ltcWallet","fixture"},
                {"ltcAnchor","fixture"},{"ltcCookieFile","/cookie"},{"nativeDaemonPort",2},{"nativeWalletPort",3},{"nativeLoginFile","/login"},
                {"preparationObserver",QJsonObject{{"walletPort",4},{"loginFile","/observer"}}},{"walletPasswordFile","/password"}};
            if (testCase==1) rpc.insert("ltcRedeemBlock",QString(64,'a'));
            if (testCase==2) rpc.insert("ltcRefundBlock",QJsonValue::Null);
            if (testCase==3) rpc.remove("preparationObserver");
            if (testCase==4) rpc.remove("walletPasswordFile");
            QVERIFY(write(rpcPath,QJsonDocument(rpc).toJson(QJsonDocument::Compact)));
            const QJsonObject config{{"schema",purpose=="payout"?"monzero-private-ltc-journal-v3":"monzero-private-ltc-journal-v2"},
                {purpose+"RpcFile",rpcPath},{"helper",program},{"helperDigest",QString::fromLatin1(QCryptographicHash::hash(script,QCryptographicHash::Sha256).toHex())},
                {"tradeId",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"role",purpose=="payout"?"litecoin-funder":"native-funder"},
                {"metadataDirectory",dir.path()},{"snapshotDirectory",dir.path()},{"pins",QJsonArray{}},
                {"policy",QJsonObject{{"ltc_network","ltc-regtest"},{"xmz_confirmations",10}}}};
            QVERIFY(write(configPath,QJsonDocument(config).toJson(QJsonDocument::Compact)));
            LitecoinJournalJob job;QCOMPARE(job.configure(configPath),testCase==0);
            if (testCase==0) {
                QVERIFY(job.status().value("walletRecoveryConfigured").toBool());
                QVERIFY(job.status().value("paymentPreparationConfigured").toBool());
                QVERIFY(!job.status().value("walletRestoreAvailable").toBool());
                QVERIFY(!job.status().value("busy").toBool());
            }
        }}
#endif
    }
    void actualLitecoinRecoveredWallet() {
        const auto config=QString::fromLocal8Bit(qgetenv("MONZERO_LTC_RECOVERED_WALLET_CONFIG"));
        if (config.isEmpty()) QSKIP("Requires an owned isolated recovered-wallet fixture");
        const auto operation=qgetenv("MONZERO_LTC_RECOVERED_WALLET_OPERATION");
        const bool reject=qgetenv("MONZERO_LTC_RECOVERED_WALLET_REJECT")=="1";
        DexEngine engine;QVERIFY(engine.configureLitecoinJournal(config));QVERIFY(engine.requestLitecoinJournalCheck());
        QTRY_VERIFY_WITH_TIMEOUT(!engine.litecoinJournalStatus().value("busy").toBool(),180000);
        QVERIFY(engine.litecoinJournalStatus().value("valid").toBool());
        bool started=false;
        if (operation=="restore") started=engine.restoreLitecoinRecoveredWallet();
        else if (operation=="open") started=engine.openLitecoinRecoveredWallet();
        else if (operation=="check") started=engine.checkLitecoinRecoveredWallet();
        QVERIFY(started);QVERIFY(!engine.checkLitecoinRecoveredWallet());
        QTRY_VERIFY_WITH_TIMEOUT(!engine.litecoinJournalStatus().value("busy").toBool(),300000);
        const auto status=engine.litecoinJournalStatus();
        QCOMPARE(status.value("walletRecoveryState").toString(),reject?QString("unknown"):QString("verified"));
        const auto result=status.value("recoveredWallet").toMap();
        if (reject) {QVERIFY(result.isEmpty());return;}
        QVERIFY(status.value("valid").toBool());QVERIFY(!status.value("walletRestoreAvailable").toBool());
        QCOMPARE(result.value("operation").toString(),QString::fromLatin1(operation));
        QVERIFY(result.value("spendKeyVerified").toBool());QVERIFY(result.value("fundingImported").toBool());
        QVERIFY(status.value("payoutReview").toMap().isEmpty());QVERIFY(status.value("refundReview").toMap().isEmpty());
        QFile output(QString::fromLocal8Bit(qgetenv("MONZERO_LTC_RECOVERED_WALLET_RESULT")));QVERIFY(output.open(QIODevice::WriteOnly));
        QVERIFY(output.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
        output.write(QJsonDocument(QJsonObject::fromVariantMap(result)).toJson(QJsonDocument::Compact));
    }
    void actualLitecoinPaymentPreparation() {
        const auto config=QString::fromLocal8Bit(qgetenv("MONZERO_LTC_PAYMENT_PREPARE_CONFIG"));
        if (config.isEmpty()) QSKIP("Requires an owned isolated recovered-wallet fixture");
        const bool payout=qgetenv("MONZERO_LTC_PAYMENT_PREPARE_ROLE")=="payout";
        DexEngine engine;QVERIFY(engine.configureLitecoinJournal(config));QVERIFY(engine.requestLitecoinJournalCheck());
        QTRY_VERIFY_WITH_TIMEOUT(!engine.litecoinJournalStatus().value("busy").toBool(),180000);
        const QString availability=payout?"payoutPreparationAvailable":"refundPreparationAvailable";
        QVERIFY(engine.litecoinJournalStatus().value(availability).toBool());
        QVERIFY(!(payout?engine.prepareLitecoinRefund():engine.prepareLitecoinPayout()));
        QVERIFY(payout?engine.prepareLitecoinPayout():engine.prepareLitecoinRefund());
        QVERIFY(!(payout?engine.prepareLitecoinPayout():engine.prepareLitecoinRefund()));
        QTRY_VERIFY_WITH_TIMEOUT(!engine.litecoinJournalStatus().value("busy").toBool(),300000);
        const auto status=engine.litecoinJournalStatus();QVERIFY(status.value("valid").toBool());
        QCOMPARE(status.value("paymentPreparationState").toString(),QString("prepared"));
        const auto review=status.value(payout?"payoutReview":"refundReview").toMap();QVERIFY(!review.isEmpty());
        QVERIFY(status.value(payout?"payoutSubmissionAvailable":"refundSubmissionAvailable").toBool());
        QVERIFY(!status.value("snapshot").toMap().value(payout?"nativePayout":"nativeRefundPayout").toMap().value("attempted").toBool());
        QFile output(QString::fromLocal8Bit(qgetenv("MONZERO_LTC_PAYMENT_PREPARE_RESULT")));QVERIFY(output.open(QIODevice::WriteOnly));
        QVERIFY(output.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
        output.write(QJsonDocument(QJsonObject::fromVariantMap(review)).toJson(QJsonDocument::Compact));
    }
    void actualLitecoinFundingSubmission() {
        const auto config=QString::fromLocal8Bit(qgetenv("MONZERO_LTC_FUNDING_SUBMIT_FIXTURE"));
        if (config.isEmpty()) QSKIP("Requires an owned isolated native-funding submission fixture");
        LitecoinJournalJob job;QVERIFY(job.configure(config));QVERIFY(job.refresh());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),180000);
        QVERIFY(job.status().value("valid").toBool());
        const bool retry=job.status().value("snapshot").toMap().value("nativeFunding").toMap().value("attempted").toBool();
        QCOMPARE(retry,qgetenv("MONZERO_LTC_FUNDING_RETRY")=="1");
        QVERIFY(job.reviewFunding(retry));
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),180000);
        QVERIFY(job.status().value("valid").toBool());
        const auto review=job.status().value("fundingReview").toMap();
        const auto token=review.value("commitment").toString();
        QVERIFY(job.status().value("fundingSubmissionAvailable").toBool());
        QVERIFY(!job.submitRefund(token));QVERIFY(!job.submitPayout(token));
        QVERIFY(!job.submitFunding(QString(64,'0')));
        QVERIFY(job.submitFunding(token));QVERIFY(!job.submitFunding(token));
        QVERIFY(job.status().value("fundingReview").toMap().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),300000);
        QVERIFY(job.status().value("valid").toBool());
        const auto sent=job.status().value("fundingSubmission").toMap();
        QCOMPARE(sent.value("state").toString(),QString("acknowledged"));
        QCOMPARE(sent.value("txid"),review.value("details").toMap().value("txid"));
        QCOMPARE(sent.value("feeAtoms"),review.value("details").toMap().value("feeAtoms"));
        QVERIFY(!sent.value("chainObserved").toBool());
        QVERIFY(!job.status().value("executable").toBool());
        QVERIFY(job.status().value("refundSubmission").toMap().isEmpty());
        QVERIFY(!job.submitFunding(token));
    }
    void actualLitecoinFundingConfirmation() {
        const auto config=QString::fromLocal8Bit(qgetenv("MONZERO_LTC_FUNDING_CONFIRM_FIXTURE"));
        if (config.isEmpty()) QSKIP("Requires an owned isolated native-funding confirmation fixture");
        const bool reject=qgetenv("MONZERO_LTC_FUNDING_CONFIRM_EXPECT")=="reject";
        LitecoinJournalJob job;QVERIFY(job.configure(config));QVERIFY(job.refresh());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),180000);
        QVERIFY(job.status().value("valid").toBool());QVERIFY(job.checkFunding());
        QVERIFY(job.status().value("fundingConfirmation").toMap().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),300000);
        QCOMPARE(job.status().value("fundingCheckState").toString(),reject?QString("unverified"):QString("confirmed"));
        QCOMPARE(job.status().value("valid").toBool(),!reject);
        const auto confirmation=job.status().value("fundingConfirmation").toMap();
        if (reject) {QVERIFY(confirmation.isEmpty());}
        else {
            QVERIFY(confirmation.value("confirmations").toString().toULongLong()>=10);
            QVERIFY(!confirmation.value("checkedAt").toString().isEmpty());
            QVERIFY(!confirmation.value("txid").toString().isEmpty());
            QCOMPARE(confirmation.value("fundingAmount").toString(),QString("2"));
        }
        QVERIFY(job.status().value("fundingReview").toMap().isEmpty());
        QVERIFY(!job.status().value("fundingSubmissionAvailable").toBool());
        QVERIFY(!job.status().value("executable").toBool());
    }
    void actualLitecoinPayoutReview() {
        const auto config=QString::fromLocal8Bit(qgetenv("MONZERO_LTC_PAYOUT_REVIEW_FIXTURE"));
        if (config.isEmpty()) QSKIP("Requires an owned isolated completed-payout fixture");
        LitecoinJournalJob job;QVERIFY(job.configure(config));QVERIFY(job.refresh());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),180000);
        QVERIFY(job.status().value("valid").toBool());QVERIFY(!job.reviewRefund(true));
        QVERIFY(job.reviewPayout(true));
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),180000);
        QVERIFY(job.status().value("valid").toBool());
        const auto review=job.status().value("payoutReview").toMap();
        const auto details=review.value("details").toMap();
        const auto snapshot=job.status().value("snapshot").toMap();
        QCOMPARE(details.value("destination"),snapshot.value("xmzPayout"));
        QCOMPARE(details.value("reserveDestination"),snapshot.value("xmzRefund"));
        QCOMPARE(details.value("action").toString(),QString("retransmit"));
        QVERIFY(!review.value("commitment").toString().isEmpty());
        QVERIFY(job.status().value("refundReview").toMap().isEmpty());
        QVERIFY(!job.submitRefund(review.value("commitment").toString()));
        QVERIFY(!job.status().value("executable").toBool());
    }
    void actualLitecoinRefundSubmission() {
        const auto config=QString::fromLocal8Bit(qgetenv("MONZERO_LTC_REFUND_SUBMIT_FIXTURE"));
        if (config.isEmpty()) QSKIP("Requires an owned isolated prepared-refund fixture");
        LitecoinJournalJob job;QVERIFY(job.configure(config));QVERIFY(job.refresh());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),180000);
        QVERIFY(job.status().value("valid").toBool());
        QVERIFY(job.reviewRefund(false));
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),180000);
        QVERIFY(job.status().value("valid").toBool());
        const auto review=job.status().value("refundReview").toMap();
        const auto commitment=review.value("commitment").toString();
        QVERIFY(!commitment.isEmpty());QVERIFY(!job.submitRefund(QString(64,'0')));
        QVERIFY(job.submitRefund(commitment));QVERIFY(!job.submitRefund(commitment));
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),300000);
        const auto outcome=job.status().value("refundSubmission").toMap();
        QCOMPARE(outcome.value("state").toString(),QString("acknowledged"));
        QCOMPARE(outcome.value("txid"),review.value("details").toMap().value("txid"));
        QVERIFY(!outcome.value("chainObserved").toBool());
        QVERIFY(!job.status().value("executable").toBool());
        QVERIFY(job.status().value("refundReview").toMap().isEmpty());
        QVERIFY(!job.submitRefund(commitment));
    }
    void actualLitecoinRefundConfirmation() {
        const auto config=QString::fromLocal8Bit(qgetenv("MONZERO_LTC_REFUND_SUBMIT_FIXTURE"));
        if (config.isEmpty()) QSKIP("Requires an owned isolated paid-refund fixture");
        const bool rejected=qgetenv("MONZERO_LTC_REFUND_EXPECT_REJECTED")=="1";
        LitecoinJournalJob job;QVERIFY(job.configure(config));QVERIFY(job.refresh());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),180000);
        QVERIFY(job.status().value("valid").toBool());QVERIFY(job.checkRefund());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),300000);
        QCOMPARE(job.status().value("refundCheckState").toString(),rejected?QString("unverified"):QString("confirmed"));
        const auto result=job.status().value("refundConfirmation").toMap();
        if (rejected) QVERIFY(result.isEmpty());
        else {
            QVERIFY(result.value("chainObserved").toBool());
            QVERIFY(result.value("confirmations").toString().toULongLong()>=10);
            QVERIFY(!result.value("txid").toString().isEmpty());
        }
        QVERIFY(!job.status().value("executable").toBool());
        QVERIFY(job.status().value("refundReview").toMap().isEmpty());
    }
    void actualLitecoinPayoutSubmission() {
        const auto config=QString::fromLocal8Bit(qgetenv("MONZERO_LTC_PAYOUT_SUBMIT_FIXTURE"));
        if (config.isEmpty()) QSKIP("Requires an owned isolated prepared-payout fixture");
        LitecoinJournalJob job;QVERIFY(job.configure(config));QVERIFY(job.refresh());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),180000);
        QVERIFY(job.status().value("valid").toBool());
        QVERIFY(job.reviewPayout(false));
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),180000);
        QVERIFY(job.status().value("valid").toBool());
        const auto review=job.status().value("payoutReview").toMap();
        const auto commitment=review.value("commitment").toString();
        QVERIFY(!commitment.isEmpty());QVERIFY(!job.submitPayout(QString(64,'0')));
        QVERIFY(job.submitPayout(commitment));QVERIFY(!job.submitPayout(commitment));
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),300000);
        const auto outcome=job.status().value("payoutSubmission").toMap();
        QCOMPARE(outcome.value("state").toString(),QString("acknowledged"));
        QCOMPARE(outcome.value("txid"),review.value("details").toMap().value("txid"));
        QVERIFY(!outcome.value("chainObserved").toBool());
        QVERIFY(!job.status().value("executable").toBool());
        QVERIFY(job.status().value("payoutReview").toMap().isEmpty());
        QVERIFY(!job.submitPayout(commitment));
    }
    void actualLitecoinPayoutConfirmation() {
        const auto config=QString::fromLocal8Bit(qgetenv("MONZERO_LTC_PAYOUT_SUBMIT_FIXTURE"));
        if (config.isEmpty()) QSKIP("Requires an owned isolated paid-payout fixture");
        const bool rejected=qgetenv("MONZERO_LTC_PAYOUT_EXPECT_REJECTED")=="1";
        LitecoinJournalJob job;QVERIFY(job.configure(config));QVERIFY(job.refresh());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),180000);
        QVERIFY(job.status().value("valid").toBool());QVERIFY(job.checkPayout());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),300000);
        QCOMPARE(job.status().value("payoutCheckState").toString(),rejected?QString("unverified"):QString("confirmed"));
        const auto result=job.status().value("payoutConfirmation").toMap();
        if (rejected) QVERIFY(result.isEmpty());
        else {
            QVERIFY(result.value("chainObserved").toBool());
            QVERIFY(result.value("confirmations").toString().toULongLong()>=10);
            QVERIFY(!result.value("txid").toString().isEmpty());
        }
        QVERIFY(!job.status().value("executable").toBool());
        QVERIFY(job.status().value("payoutReview").toMap().isEmpty());
    }
    void litecoinJournalTransportRejectsAuthorityAndContextChanges() {
#ifdef Q_OS_LINUX
        const QString trade="12345678-1234-4234-8234-123456789abc";
        const QJsonObject payment{{"prepared",true},{"attempted",true},{"acknowledged",true}};
        const QJsonObject recovery{{"attempted",false},{"acknowledged",false}};
        QJsonObject snapshot{{"schema","monzero-ltc-journal-status-v1"},{"tradeId",trade},
            {"quoteCommitment",QString(64,'a')},{"role","native-funder"},{"ltcNetwork","ltc-regtest"},
            {"xmzGenesis",QString(64,'b')},{"ltcAtoms","18446744073709551615"},{"xmzAtoms","18446744073709551615"},
            {"ltcPayout","fixture-ltc-payout"},{"ltcRefund","fixture-ltc-refund"},
            {"xmzPayout","fixture-xmz-payout"},{"xmzRefund","fixture-xmz-refund"},
            {"keysReady",true},{"recoveryIntentRetained",true},{"nativeClaimRetained",false},{"nativeRefundRetained",true},
            {"journalValidated",true},{"chainObserved",false},{"executable",false},
            {"nativeFunding",payment},{"litecoinRedemption",payment},{"nativePayout",payment},{"nativeRefundPayout",payment},
            {"litecoinRecovery",QJsonObject{{"cancel",recovery},{"punish",recovery}}}};
        // Synthetic helper output tests the GUI boundary, not chain or signature evidence.
        for (int testCase=0;testCase<7;++testCase) {
            QTemporaryDir dir;QVERIFY(dir.isValid());
            auto result=snapshot;
            if (testCase==1) result["executable"]=true;
            if (testCase==2) result["chainObserved"]=true;
            if (testCase==3) result["tradeId"]="87654321-1234-4234-8234-123456789abc";
            if (testCase==5) result["privateKey"]="unexpected";
            if (testCase==6) result["nativeRefundPayout"]=QJsonObject{{"prepared",false},{"attempted",true},{"acknowledged",true}};
            const auto encoded=QJsonDocument(result).toJson(QJsonDocument::Compact).toBase64();
            const auto program=dir.path()+"/helper";
            QFile helper(program);QVERIFY(helper.open(QIODevice::WriteOnly));
            QByteArray script="#!/usr/bin/python3\nimport json,sys,base64\nr=json.load(sys.stdin)\ns=json.loads(base64.b64decode('"+encoded+"'))\n";
            script+="sys.stdout.write(json.dumps({'schema':'monzero-ltc-journal-response-v1','requestId':";
            script+=testCase==4?"'wrong-request'":"r['requestId']";
            script+=",'snapshot':s},sort_keys=True,separators=(',',':')))\n";
            QCOMPARE(helper.write(script),qint64(script.size()));helper.close();
            QVERIFY(helper.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
            const auto digest=QString::fromLatin1(QCryptographicHash::hash(script,QCryptographicHash::Sha256).toHex());
            const auto path=dir.path()+"/config.json";
            QFile config(path);QVERIFY(config.open(QIODevice::WriteOnly));
            const auto bytes=QJsonDocument(QJsonObject{{"schema","monzero-private-ltc-journal-v1"},
                {"helper",program},{"helperDigest",digest},{"tradeId",trade},{"role","native-funder"},
                {"metadataDirectory",dir.path()},{"snapshotDirectory",dir.path()},
                {"pins",QJsonArray{}},{"policy",QJsonObject{}}}).toJson(QJsonDocument::Compact);
            QCOMPARE(config.write(bytes),qint64(bytes.size()));config.close();
            QVERIFY(config.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
            LitecoinJournalJob job;
            QVERIFY(!job.refresh());QVERIFY(job.configure(path));QVERIFY(!job.configure(path));
            QVERIFY(job.refresh());QVERIFY(!job.refresh());
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QCOMPARE(job.status().value("valid").toBool(),testCase==0);
            const auto amounts=job.status().value("amounts").toMap();
            if (testCase==0) {
                QCOMPARE(amounts.value("ltc").toString(),QString("184467440737.09551615"));
                QCOMPARE(amounts.value("xmzLock").toString(),QString("184467440.73709551615"));
            } else QVERIFY(amounts.isEmpty());
            QVERIFY(!job.status().value("executable").toBool());
            QVERIFY(config.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ReadOther));
            QVERIFY(!job.refresh());QVERIFY(!job.status().value("valid").toBool());
            QVERIFY(job.status().value("amounts").toMap().isEmpty());
        }
#endif
    }
    void litecoinRefundReviewBindsRetainedPaymentAndClearsStaleData() {
#ifdef Q_OS_LINUX
        const QString trade="12345678-1234-4234-8234-123456789abc";
        const QJsonObject payment{{"prepared",true},{"attempted",true},{"acknowledged",true}};
        const QJsonObject recovery{{"attempted",false},{"acknowledged",false}};
        QJsonObject snapshot{{"schema","monzero-ltc-journal-status-v1"},{"tradeId",trade},
            {"quoteCommitment",QString(64,'a')},{"role","native-funder"},{"ltcNetwork","ltc-regtest"},
            {"xmzGenesis",QString(64,'b')},{"ltcAtoms","18446744073709551615"},{"xmzAtoms","18446744073709551615"},
            {"ltcPayout","fixture-ltc-payout"},{"ltcRefund","fixture-ltc-refund"},
            {"xmzPayout","fixture-xmz-payout"},{"xmzRefund","fixture-xmz-refund"},
            {"keysReady",true},{"recoveryIntentRetained",true},{"nativeClaimRetained",false},{"nativeRefundRetained",true},
            {"journalValidated",true},{"chainObserved",false},{"executable",false},
            {"nativeFunding",payment},{"litecoinRedemption",payment},{"nativePayout",payment},{"nativeRefundPayout",payment},
            {"litecoinRecovery",QJsonObject{{"cancel",recovery},{"punish",recovery}}}};
        // Synthetic helper output tests the GUI boundary, not chain or signature evidence.
        for (int testCase=0;testCase<9;++testCase) {
            QTemporaryDir dir;QVERIFY(dir.isValid());
            auto result=snapshot;
            const bool retransmit=testCase!=8;
            if (!retransmit) result["nativeRefundPayout"]=QJsonObject{{"prepared",true},{"attempted",false},{"acknowledged",false}};
            const auto encoded=QJsonDocument(result).toJson(QJsonDocument::Compact).toBase64();
            const auto program=dir.path()+"/helper";
            QFile helper(program);QVERIFY(helper.open(QIODevice::WriteOnly));
            QByteArray script="#!/usr/bin/python3\nimport json,sys,base64\nr=json.load(sys.stdin)\ns=json.loads(base64.b64decode('"+encoded+"'))\n";
            script+="import hashlib\nresponse={'schema':'monzero-ltc-journal-response-v1','requestId':r['requestId'],'snapshot':s}\n";
            script+="if r['schema']=='monzero-ltc-refund-review-request-v1':\n";
            script+=" d={'schema':'monzero-ltc-native-refund-review-v1','tradeId':s['tradeId'],'quoteCommitment':s['quoteCommitment'],'journalCommitment':'c'*64,'preparedCommitment':'d'*64,'dispatchCommitment':('e'*64 if r['action']=='retransmit' else None),'action':r['action'],'txid':'f'*64,'destination':s['xmzRefund'],'refundAtoms':str(int(s['xmzAtoms'])-2),'feeAtoms':'2','maximumFeeAtoms':'3','lockedAtoms':s['xmzAtoms'],'chainObserved':False}\n";
            if (testCase==1) script+=" d['destination']='wrong-recipient'\n";
            if (testCase==2) script+=" d['feeAtoms']='4'\n";
            if (testCase==3) script+=" d['refundAtoms']='1'\n";
            if (testCase==4) script+=" d['action']='send'\n";
            if (testCase==5) script+=" d['chainObserved']=True\n";
            if (testCase==6) script+=" d['privateKey']='unexpected'\n";
            script+=" c=hashlib.sha256(b'MONZERO-LTC-NATIVE-REFUND-APPROVAL-V1\\0'+json.dumps(d,sort_keys=True,separators=(',',':')).encode()).hexdigest()\n";
            if (testCase==7) script+=" c='0'*64\n";
            script+=" response.update(schema='monzero-ltc-refund-review-response-v1',review={'details':d,'commitment':c})\n";
            script+="sys.stdout.write(json.dumps(response,sort_keys=True,separators=(',',':')))\n";
            QCOMPARE(helper.write(script),qint64(script.size()));helper.close();
            QVERIFY(helper.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
            const auto digest=QString::fromLatin1(QCryptographicHash::hash(script,QCryptographicHash::Sha256).toHex());
            const auto path=dir.path()+"/config.json";
            QFile config(path);QVERIFY(config.open(QIODevice::WriteOnly));
            const auto bytes=QJsonDocument(QJsonObject{{"schema","monzero-private-ltc-journal-v1"},
                {"helper",program},{"helperDigest",digest},{"tradeId",trade},{"role","native-funder"},
                {"metadataDirectory",dir.path()},{"snapshotDirectory",dir.path()},
                {"pins",QJsonArray{}},{"policy",QJsonObject{}}}).toJson(QJsonDocument::Compact);
            QCOMPARE(config.write(bytes),qint64(bytes.size()));config.close();
            QVERIFY(config.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
            LitecoinJournalJob job;
            QVERIFY(!job.refresh());QVERIFY(job.configure(path));QVERIFY(!job.configure(path));
            QVERIFY(job.refresh());QVERIFY(!job.refresh());
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QVERIFY(job.status().value("valid").toBool());
            QVERIFY(!job.reviewRefund(!retransmit)); // The review action must match the retained attempt.
            QVERIFY(job.reviewRefund(retransmit));QVERIFY(!job.reviewRefund(retransmit));
            QVERIFY(job.status().value("refundReview").toMap().isEmpty());
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QCOMPARE(job.status().value("valid").toBool(),(testCase==0 || testCase==8));
            const auto review=job.status().value("refundReview").toMap();
            if ((testCase==0 || testCase==8)) {
                QCOMPARE(review.value("amounts").toMap().value("refundAtoms").toString(),QString("184467440.73709551613"));
                QCOMPARE(review.value("amounts").toMap().value("feeAtoms").toString(),QString("0.00000000002"));
            } else QVERIFY(review.isEmpty());
            QVERIFY(!job.status().value("executable").toBool());
            QVERIFY(config.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ReadOther));
            QVERIFY(!job.refresh());QVERIFY(!job.status().value("valid").toBool());
            QVERIFY(job.status().value("amounts").toMap().isEmpty());
            QVERIFY(job.status().value("refundReview").toMap().isEmpty());
        }
#endif
    }
    void litecoinFundingReviewBindsRetainedSourceAndSafety() {
#ifdef Q_OS_LINUX
        const QString trade="12345678-1234-4234-8234-123456789abc";
        const QJsonObject payment{{"prepared",true},{"attempted",true},{"acknowledged",true}};
        const QJsonObject recovery{{"attempted",false},{"acknowledged",false}};
        QJsonObject snapshot{{"schema","monzero-ltc-journal-status-v1"},{"tradeId",trade},
            {"quoteCommitment",QString(64,'a')},{"role","native-funder"},{"ltcNetwork","ltc-regtest"},
            {"xmzGenesis",QString(64,'b')},{"ltcAtoms","18446744073709551615"},{"xmzAtoms","18446744073709551613"},
            {"ltcPayout","fixture-ltc-payout"},{"ltcRefund","fixture-ltc-refund"},
            {"xmzPayout","fixture-xmz-payout"},{"xmzRefund","fixture-xmz-refund"},
            {"keysReady",true},{"recoveryIntentRetained",true},{"nativeClaimRetained",false},{"nativeRefundRetained",true},
            {"journalValidated",true},{"chainObserved",false},{"executable",false},
            {"nativeFunding",payment},{"litecoinRedemption",payment},{"nativePayout",payment},{"nativeRefundPayout",payment},
            {"litecoinRecovery",QJsonObject{{"cancel",recovery},{"punish",recovery}}}};
        // Synthetic helper output tests the GUI boundary, not chain or signature evidence.
        for (int testCase=0;testCase<26;++testCase) {
            QTemporaryDir dir;QVERIFY(dir.isValid());
            const bool preparing=testCase>=19;
            auto result=snapshot;
            const bool retransmit=testCase!=8 && !preparing;
            if (!retransmit) result["nativeFunding"]=QJsonObject{{"prepared",true},{"attempted",false},{"acknowledged",false}};
            if (preparing) result["nativeFunding"]=QJsonObject{{"prepared",false},{"attempted",false},{"acknowledged",false}};
            const auto encoded=QJsonDocument(result).toJson(QJsonDocument::Compact).toBase64();
            const auto program=dir.path()+"/helper";
            QFile helper(program);QVERIFY(helper.open(QIODevice::WriteOnly));
            QByteArray script="#!/usr/bin/python3\nimport json,sys,base64\nr=json.load(sys.stdin)\ns=json.loads(base64.b64decode('"+encoded+"'))\n";
            script+="import hashlib\nresponse={'schema':'monzero-ltc-journal-response-v1','requestId':r['requestId'],'snapshot':s}\n";
            if (preparing) script+="if r['schema']=='monzero-ltc-funding-prepare-request-v1':\n r['schema']='monzero-ltc-funding-review-request-v1';r['action']='send';s['nativeFunding']['prepared']=True\n";
            script+="if r['schema']=='monzero-ltc-funding-review-request-v1':\n";
            script+=" d={'schema':'monzero-ltc-native-funding-review-v1','tradeId':s['tradeId'],'quoteCommitment':s['quoteCommitment'],'journalCommitment':'c'*64,'preparedCommitment':'d'*64,'dispatchCommitment':('e'*64 if r['action']=='retransmit' else None),'action':r['action'],'txid':'f'*64,'wallet':'F'*95,'account':0,'priority':1,'destination':'G'*95,'amountAtoms':s['xmzAtoms'],'feeAtoms':'2','maximumFeeAtoms':'3','totalDebitAtoms':str(int(s['xmzAtoms'])+2),'ltcContract':'rltc1'+'q'*58,'ltcAtoms':s['ltcAtoms'],'safetyBlocks':str(r['safetyBlocks']),'expiresAt':'2000000000','chainObserved':False}\n";
            if (testCase==20) script+=" d['wallet']='H'*95\n";
            if (testCase==21) script+=" d['account']=1\n";
            if (testCase==22) script+=" d['priority']=2\n";
            if (testCase==23) script+=" s['xmzRefund']='changed-refund'\n";
            if (testCase==24) script+=" response['requestId']='wrong-request'\n";
            if (testCase==25) script+=" sys.exit(1)\n";
            if (testCase==1) script+=" d['destination']='wrong-recipient'\n";
            if (testCase==2) script+=" d['feeAtoms']='4'\n";
            if (testCase==3) script+=" d['amountAtoms']='1'\n";
            if (testCase==4) script+=" d['action']='send'\n";
            if (testCase==5) script+=" d['chainObserved']=True\n";
            if (testCase==6) script+=" d['privateKey']='unexpected'\n";
            if (testCase==9) script+=" d['safetyBlocks']='3'\n";
            if (testCase==10) script+=" d['totalDebitAtoms']='1'\n";
            if (testCase==11) script+=" d['wallet']='0'*95\n";
            if (testCase==12) script+=" d['priority']=1.5\n";
            if (testCase==13) script+=" d['account']=-1\n";
            if (testCase==14) script+=" d['expiresAt']='0'\n";
            if (testCase==15) script+=" d['ltcContract']='ltc1'+'q'*58\n";
            if (testCase==16) script+=" d['feeAtoms']='3';d['totalDebitAtoms']='0'\n";
            if (testCase==18) script+=" d['ltcAtoms']='1'\n";
            script+=" c=hashlib.sha256(b'MONZERO-LTC-NATIVE-FUNDING-APPROVAL-V1\\0'+json.dumps(d,sort_keys=True,separators=(',',':')).encode()).hexdigest()\n";
            if (testCase==17) script+=" c=hashlib.sha256(b'MONZERO-LTC-NATIVE-REFUND-APPROVAL-V1\\0'+json.dumps(d,sort_keys=True,separators=(',',':')).encode()).hexdigest()\n";
            if (testCase==7) script+=" c='0'*64\n";
            script+=" response.update(schema='monzero-ltc-funding-review-response-v1',review={'details':d,'commitment':c})\n";
            if (preparing) script+=" response['schema']='monzero-ltc-funding-prepare-response-v1'\n";
            script+="sys.stdout.write(json.dumps(response,sort_keys=True,separators=(',',':')))\n";
            QCOMPARE(helper.write(script),qint64(script.size()));helper.close();
            QVERIFY(helper.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
            const auto digest=QString::fromLatin1(QCryptographicHash::hash(script,QCryptographicHash::Sha256).toHex());
            const auto rpcPath=dir.path()+"/refund-rpc.json";
            QFile rpcFile(rpcPath);QVERIFY(rpcFile.open(QIODevice::WriteOnly));rpcFile.write(preparing?QJsonDocument(QJsonObject{{"schema","monzero-private-ltc-funding-rpc-v2"},
                {"source",QJsonObject{{"wallet",QString(95,'F')},{"account",0},{"priority",1}}}}).toJson(QJsonDocument::Compact):QByteArray("{}"));rpcFile.close();
            QVERIFY(rpcFile.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
            const auto path=dir.path()+"/config.json";
            QFile config(path);QVERIFY(config.open(QIODevice::WriteOnly));
            const auto bytes=QJsonDocument(QJsonObject{{"schema",preparing?"monzero-private-ltc-journal-v4":"monzero-private-ltc-journal-v2"},{preparing?"fundingRpcFile":"refundRpcFile",rpcPath},
                {"helper",program},{"helperDigest",digest},{"tradeId",trade},{"role","native-funder"},
                {"metadataDirectory",dir.path()},{"snapshotDirectory",dir.path()},
                {"pins",QJsonArray{}},{"policy",QJsonObject{{"ltc_network","ltc-regtest"},{"xmz_confirmations",10},{"ltc_confirmations",2},{"xmz_genesis",QString(64,'b')}}}}).toJson(QJsonDocument::Compact);
            QCOMPARE(config.write(bytes),qint64(bytes.size()));config.close();
            QVERIFY(config.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
            LitecoinJournalJob job;
            QVERIFY(!job.refresh());QVERIFY(job.configure(path));QVERIFY(!job.configure(path));
            QVERIFY(job.refresh());QVERIFY(!job.refresh());
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QVERIFY(job.status().value("valid").toBool());
            if (preparing) {
                QVERIFY(job.prepareFunding());QVERIFY(!job.prepareFunding());
                QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
                QCOMPARE(job.status().value("valid").toBool(),testCase==19);
                QCOMPARE(job.status().value("fundingPreparationState").toString(),QString(testCase==19?"prepared":"unknown"));
                QCOMPARE(job.status().value("fundingReview").toMap().isEmpty(),testCase!=19);
                QVERIFY(!job.prepareFunding());
                continue;
            }
            QVERIFY(!job.reviewPayout(retransmit));QVERIFY(!job.reviewFunding(retransmit,0));
            QVERIFY(!job.reviewFunding(!retransmit)); // The review action must match the retained attempt.
            QVERIFY(job.reviewFunding(retransmit));QVERIFY(!job.reviewFunding(retransmit));
            QVERIFY(job.status().value("fundingReview").toMap().isEmpty());
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QCOMPARE(job.status().value("valid").toBool(),(testCase==0 || testCase==8));
            const auto review=job.status().value("fundingReview").toMap();
            if ((testCase==0 || testCase==8)) {
                QCOMPARE(review.value("amounts").toMap().value("amountAtoms").toString(),QString("184467440.73709551613"));
                QCOMPARE(review.value("amounts").toMap().value("totalDebitAtoms").toString(),QString("184467440.73709551615"));
                QVERIFY(!review.value("expiresAtUtc").toString().isEmpty());
                QCOMPARE(review.value("amounts").toMap().value("feeAtoms").toString(),QString("0.00000000002"));
            } else QVERIFY(review.isEmpty());
            QVERIFY(!job.submitRefund(review.value("commitment").toString()));
            QVERIFY(!job.submitPayout(review.value("commitment").toString()));
            QVERIFY(job.status().value("refundReview").toMap().isEmpty());
            QVERIFY(!job.status().value("refundSubmissionAvailable").toBool());
            QVERIFY(!job.status().value("executable").toBool());
            QVERIFY(config.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ReadOther));
            QVERIFY(!job.refresh());QVERIFY(!job.status().value("valid").toBool());
            QVERIFY(job.status().value("amounts").toMap().isEmpty());
            QVERIFY(job.status().value("fundingReview").toMap().isEmpty());
        }
#endif
    }
    void litecoinPayoutReviewBindsRetainedPaymentAndClearsStaleData() {
#ifdef Q_OS_LINUX
        const QString trade="12345678-1234-4234-8234-123456789abc";
        const QJsonObject payment{{"prepared",true},{"attempted",true},{"acknowledged",true}};
        const QJsonObject recovery{{"attempted",false},{"acknowledged",false}};
        QJsonObject snapshot{{"schema","monzero-ltc-journal-status-v1"},{"tradeId",trade},
            {"quoteCommitment",QString(64,'a')},{"role","litecoin-funder"},{"ltcNetwork","ltc-regtest"},
            {"xmzGenesis",QString(64,'b')},{"ltcAtoms","18446744073709551615"},{"xmzAtoms","18446744073709551615"},
            {"ltcPayout","fixture-ltc-payout"},{"ltcRefund","fixture-ltc-refund"},
            {"xmzPayout","fixture-xmz-payout"},{"xmzRefund","fixture-xmz-refund"},
            {"keysReady",true},{"recoveryIntentRetained",true},{"nativeClaimRetained",false},{"nativeRefundRetained",true},
            {"journalValidated",true},{"chainObserved",false},{"executable",false},
            {"nativeFunding",payment},{"litecoinRedemption",payment},{"nativePayout",payment},{"nativeRefundPayout",payment},
            {"litecoinRecovery",QJsonObject{{"cancel",recovery},{"refund",recovery}}}};
        // Synthetic helper output tests the GUI boundary, not chain or signature evidence.
        for (int testCase=0;testCase<15;++testCase) {
            QTemporaryDir dir;QVERIFY(dir.isValid());
            auto result=snapshot;
            const bool retransmit=testCase!=8;
            if (!retransmit) result["nativePayout"]=QJsonObject{{"prepared",true},{"attempted",false},{"acknowledged",false}};
            const auto encoded=QJsonDocument(result).toJson(QJsonDocument::Compact).toBase64();
            const auto program=dir.path()+"/helper";
            QFile helper(program);QVERIFY(helper.open(QIODevice::WriteOnly));
            QByteArray script="#!/usr/bin/python3\nimport json,sys,base64\nr=json.load(sys.stdin)\ns=json.loads(base64.b64decode('"+encoded+"'))\n";
            script+="import hashlib\nresponse={'schema':'monzero-ltc-journal-response-v1','requestId':r['requestId'],'snapshot':s}\n";
            script+="if r['schema']=='monzero-ltc-payout-review-request-v1':\n";
            script+=" d={'schema':'monzero-ltc-native-payout-review-v1','tradeId':s['tradeId'],'quoteCommitment':s['quoteCommitment'],'journalCommitment':'c'*64,'preparedCommitment':'d'*64,'dispatchCommitment':('e'*64 if r['action']=='retransmit' else None),'action':r['action'],'txid':'f'*64,'destination':s['xmzPayout'],'reserveDestination':s['xmzRefund'],'reserveReturnAtoms':'7','payoutAtoms':str(int(s['xmzAtoms'])-9),'feeAtoms':'2','maximumFeeAtoms':'3','lockedAtoms':s['xmzAtoms'],'chainObserved':False}\n";
            if (testCase==1) script+=" d['destination']='wrong-recipient'\n";
            if (testCase==2) script+=" d['feeAtoms']='4'\n";
            if (testCase==3) script+=" d['payoutAtoms']='1'\n";
            if (testCase==4) script+=" d['action']='send'\n";
            if (testCase==5) script+=" d['chainObserved']=True\n";
            if (testCase==6) script+=" d['privateKey']='unexpected'\n";
            if (testCase==9) script+=" d['reserveReturnAtoms']='8'\n";
            if (testCase==10) script+=" d['reserveDestination']='wrong-reserve-recipient'\n";
            if (testCase==11) script+=" d['reserveReturnAtoms']='18446744073709551615'\n";
            if (testCase==12) script+=" d['payoutAtoms']='01'\n";
            if (testCase==14) script+=" d['reserveReturnAtoms']='0';d['payoutAtoms']=str(int(s['xmzAtoms'])-2)\n";
            script+=" c=hashlib.sha256(b'MONZERO-LTC-NATIVE-PAYOUT-APPROVAL-V1\\0'+json.dumps(d,sort_keys=True,separators=(',',':')).encode()).hexdigest()\n";
            if (testCase==13) script+=" c=hashlib.sha256(b'MONZERO-LTC-NATIVE-REFUND-APPROVAL-V1\\0'+json.dumps(d,sort_keys=True,separators=(',',':')).encode()).hexdigest()\n";
            if (testCase==7) script+=" c='0'*64\n";
            script+=" response.update(schema='monzero-ltc-payout-review-response-v1',review={'details':d,'commitment':c})\n";
            script+="sys.stdout.write(json.dumps(response,sort_keys=True,separators=(',',':')))\n";
            QCOMPARE(helper.write(script),qint64(script.size()));helper.close();
            QVERIFY(helper.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
            const auto digest=QString::fromLatin1(QCryptographicHash::hash(script,QCryptographicHash::Sha256).toHex());
            const auto path=dir.path()+"/config.json";
            QFile config(path);QVERIFY(config.open(QIODevice::WriteOnly));
            const auto bytes=QJsonDocument(QJsonObject{{"schema","monzero-private-ltc-journal-v1"},
                {"helper",program},{"helperDigest",digest},{"tradeId",trade},{"role","litecoin-funder"},
                {"metadataDirectory",dir.path()},{"snapshotDirectory",dir.path()},
                {"pins",QJsonArray{}},{"policy",QJsonObject{}}}).toJson(QJsonDocument::Compact);
            QCOMPARE(config.write(bytes),qint64(bytes.size()));config.close();
            QVERIFY(config.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
            LitecoinJournalJob job;
            QVERIFY(!job.refresh());QVERIFY(job.configure(path));QVERIFY(!job.configure(path));
            QVERIFY(job.refresh());QVERIFY(!job.refresh());
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QVERIFY(job.status().value("valid").toBool());
            QVERIFY(!job.reviewRefund(retransmit));
            QVERIFY(!job.reviewPayout(!retransmit)); // The review action must match the retained attempt.
            QVERIFY(job.reviewPayout(retransmit));QVERIFY(!job.reviewPayout(retransmit));
            QVERIFY(job.status().value("payoutReview").toMap().isEmpty());
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QCOMPARE(job.status().value("valid").toBool(),(testCase==0 || testCase==8 || testCase==14));
            const auto review=job.status().value("payoutReview").toMap();
            if ((testCase==0 || testCase==8 || testCase==14)) {
                QCOMPARE(review.value("amounts").toMap().value("payoutAtoms").toString(),testCase==14?QString("184467440.73709551613"):QString("184467440.73709551606"));
                QCOMPARE(review.value("amounts").toMap().value("feeAtoms").toString(),QString("0.00000000002"));
            } else QVERIFY(review.isEmpty());
            QVERIFY(job.status().value("refundReview").toMap().isEmpty());
            QVERIFY(!job.submitRefund(review.value("commitment").toString()));
            QVERIFY(!job.status().value("executable").toBool());
            QVERIFY(config.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ReadOther));
            QVERIFY(!job.refresh());QVERIFY(!job.status().value("valid").toBool());
            QVERIFY(job.status().value("amounts").toMap().isEmpty());
            QVERIFY(job.status().value("payoutReview").toMap().isEmpty());
        }
#endif
    }
    void litecoinRefundSubmissionRequiresCurrentReviewAndKeepsUnknownOutcomes() {
#ifdef Q_OS_LINUX
        const QString trade="12345678-1234-4234-8234-123456789abc";
        const QJsonObject payment{{"prepared",true},{"attempted",true},{"acknowledged",true}};
        const QJsonObject recovery{{"attempted",false},{"acknowledged",false}};
        QJsonObject snapshot{{"schema","monzero-ltc-journal-status-v1"},{"tradeId",trade},
            {"quoteCommitment",QString(64,'a')},{"role","native-funder"},{"ltcNetwork","ltc-regtest"},
            {"xmzGenesis",QString(64,'b')},{"ltcAtoms","18446744073709551615"},{"xmzAtoms","18446744073709551615"},
            {"ltcPayout","fixture-ltc-payout"},{"ltcRefund","fixture-ltc-refund"},
            {"xmzPayout","fixture-xmz-payout"},{"xmzRefund","fixture-xmz-refund"},
            {"keysReady",true},{"recoveryIntentRetained",true},{"nativeClaimRetained",false},{"nativeRefundRetained",true},
            {"journalValidated",true},{"chainObserved",false},{"executable",false},
            {"nativeFunding",payment},{"litecoinRedemption",payment},{"nativePayout",payment},{"nativeRefundPayout",payment},
            {"litecoinRecovery",QJsonObject{{"cancel",recovery},{"punish",recovery}}}};
        // Synthetic helper output tests the GUI boundary, not chain or signature evidence.
        for (int testCase=0;testCase<6;++testCase) {
            QTemporaryDir dir;QVERIFY(dir.isValid());
            auto result=snapshot;
            const bool retransmit=false;
            if (!retransmit) result["nativeRefundPayout"]=QJsonObject{{"prepared",true},{"attempted",false},{"acknowledged",false}};
            const auto encoded=QJsonDocument(result).toJson(QJsonDocument::Compact).toBase64();
            const auto program=dir.path()+"/helper";
            QFile helper(program);QVERIFY(helper.open(QIODevice::WriteOnly));
            QByteArray script="#!/usr/bin/python3\nimport json,sys,base64\nr=json.load(sys.stdin)\ns=json.loads(base64.b64decode('"+encoded+"'))\n";
            script+="import hashlib\nresponse={'schema':'monzero-ltc-journal-response-v1','requestId':r['requestId'],'snapshot':s}\n";
            script+="if r['schema']=='monzero-ltc-refund-review-request-v1':\n";
            script+=" d={'schema':'monzero-ltc-native-refund-review-v1','tradeId':s['tradeId'],'quoteCommitment':s['quoteCommitment'],'journalCommitment':'c'*64,'preparedCommitment':'d'*64,'dispatchCommitment':('e'*64 if r['action']=='retransmit' else None),'action':r['action'],'txid':'f'*64,'destination':s['xmzRefund'],'refundAtoms':str(int(s['xmzAtoms'])-2),'feeAtoms':'2','maximumFeeAtoms':'3','lockedAtoms':s['xmzAtoms'],'chainObserved':False}\n";
            script+=" c=hashlib.sha256(b'MONZERO-LTC-NATIVE-REFUND-APPROVAL-V1\\0'+json.dumps(d,sort_keys=True,separators=(',',':')).encode()).hexdigest()\n";
            script+=" response.update(schema='monzero-ltc-refund-review-response-v1',review={'details':d,'commitment':c})\n";
            script+="if r['schema']=='monzero-ltc-refund-submit-request-v1':\n";
            script+=" import pathlib\n pathlib.Path(__file__+'.submitted').write_text('attempted')\n";
            if (testCase==1) script+=" sys.exit(2)\n";
            script+=" s['nativeRefundPayout']={'prepared':True,'attempted':True,'acknowledged':True}\n response.update(schema='monzero-ltc-refund-submit-response-v1',outcome={'state':'acknowledged','txid':'f'*64,'feeAtoms':'2','chainObserved':False,'executable':False})\n";
            if (testCase==2) script+=" response['outcome']['txid']='0'*64\n";
            if (testCase==3) script+=" response['outcome']['feeAtoms']='3'\n";
            if (testCase==4) script+=" response['outcome']['chainObserved']=True\n";
            script+="sys.stdout.write(json.dumps(response,sort_keys=True,separators=(',',':')))\n";
            QCOMPARE(helper.write(script),qint64(script.size()));helper.close();
            QVERIFY(helper.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
            const auto digest=QString::fromLatin1(QCryptographicHash::hash(script,QCryptographicHash::Sha256).toHex());
            const auto rpcPath=dir.path()+"/rpc.json";
            QFile rpcFile(rpcPath);QVERIFY(rpcFile.open(QIODevice::WriteOnly));rpcFile.write("{}");rpcFile.close();
            QVERIFY(rpcFile.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
            const auto path=dir.path()+"/config.json";
            QFile config(path);QVERIFY(config.open(QIODevice::WriteOnly));
            const auto bytes=QJsonDocument(QJsonObject{{"schema","monzero-private-ltc-journal-v2"},{"refundRpcFile",rpcPath},
                {"helper",program},{"helperDigest",digest},{"tradeId",trade},{"role","native-funder"},
                {"metadataDirectory",dir.path()},{"snapshotDirectory",dir.path()},
                {"pins",QJsonArray{}},{"policy",QJsonObject{{"ltc_network","ltc-regtest"},{"xmz_confirmations",10},{"ltc_confirmations",2},{"xmz_genesis",QString(64,'b')}}}}).toJson(QJsonDocument::Compact);
            QCOMPARE(config.write(bytes),qint64(bytes.size()));config.close();
            QVERIFY(config.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
            LitecoinJournalJob job;
            QVERIFY(!job.refresh());QVERIFY(job.configure(path));QVERIFY(!job.configure(path));
            QVERIFY(job.refresh());QVERIFY(!job.refresh());
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QVERIFY(job.status().value("valid").toBool());
            QVERIFY(!job.reviewRefund(!retransmit)); // The review action must match the retained attempt.
            QVERIFY(job.reviewRefund(retransmit));QVERIFY(!job.reviewRefund(retransmit));
            QVERIFY(job.status().value("refundReview").toMap().isEmpty());
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QVERIFY(job.status().value("valid").toBool());
            const auto review=job.status().value("refundReview").toMap();
            const auto commitment=review.value("commitment").toString();
            QVERIFY(job.status().value("refundSubmissionAvailable").toBool());
            QVERIFY(!job.submitRefund(QString(64,'0')));
            QVERIFY(!QFile::exists(program+".submitted"));
            if (testCase==5) {
                QVERIFY(rpcFile.open(QIODevice::WriteOnly|QIODevice::Truncate));rpcFile.write("{ } ");rpcFile.close();
                QVERIFY(!job.submitRefund(commitment));
                QCOMPARE(job.status().value("refundSubmission").toMap().value("state").toString(),QString("not-started"));
                QVERIFY(!QFile::exists(program+".submitted"));
            } else {
                QVERIFY(job.submitRefund(commitment));QVERIFY(!job.submitRefund(commitment));
                QVERIFY(job.status().value("refundReview").toMap().isEmpty());
                QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
                QVERIFY(QFile::exists(program+".submitted"));
                QCOMPARE(job.status().value("refundSubmission").toMap().value("state").toString(),
                    testCase==0?QString("acknowledged"):QString("unknown"));
                QCOMPARE(job.status().value("valid").toBool(),testCase==0);
            }
            QVERIFY(!job.status().value("refundSubmissionAvailable").toBool());
            QVERIFY(!job.status().value("executable").toBool());
            QVERIFY(!job.submitRefund(commitment));
        }
#endif
    }
    void litecoinRefundConfirmationRejectsInvalidEvidenceAndClearsEarlierResult() {
#ifdef Q_OS_LINUX
        const QString trade="12345678-1234-4234-8234-123456789abc";
        const QJsonObject payment{{"prepared",true},{"attempted",true},{"acknowledged",true}};
        const QJsonObject recovery{{"attempted",false},{"acknowledged",false}};
        QJsonObject snapshot{{"schema","monzero-ltc-journal-status-v1"},{"tradeId",trade},
            {"quoteCommitment",QString(64,'a')},{"role","native-funder"},{"ltcNetwork","ltc-regtest"},
            {"xmzGenesis",QString(64,'b')},{"ltcAtoms","18446744073709551615"},{"xmzAtoms","18446744073709551615"},
            {"ltcPayout","fixture-ltc-payout"},{"ltcRefund","fixture-ltc-refund"},
            {"xmzPayout","fixture-xmz-payout"},{"xmzRefund","fixture-xmz-refund"},
            {"keysReady",true},{"recoveryIntentRetained",true},{"nativeClaimRetained",false},{"nativeRefundRetained",true},
            {"journalValidated",true},{"chainObserved",false},{"executable",false},
            {"nativeFunding",payment},{"litecoinRedemption",payment},{"nativePayout",payment},{"nativeRefundPayout",payment},
            {"litecoinRecovery",QJsonObject{{"cancel",recovery},{"punish",recovery}}}};
        // Synthetic helper output tests the GUI boundary, not chain or signature evidence.
        for (int testCase=0;testCase<8;++testCase) {
            QTemporaryDir dir;QVERIFY(dir.isValid());
            auto result=snapshot;
            const bool retransmit=true;
            if (!retransmit) result["nativeRefundPayout"]=QJsonObject{{"prepared",true},{"attempted",false},{"acknowledged",false}};
            const auto encoded=QJsonDocument(result).toJson(QJsonDocument::Compact).toBase64();
            const auto program=dir.path()+"/helper";
            QFile helper(program);QVERIFY(helper.open(QIODevice::WriteOnly));
            QByteArray script="#!/usr/bin/python3\nimport json,sys,base64\nr=json.load(sys.stdin)\ns=json.loads(base64.b64decode('"+encoded+"'))\n";
            script+="import hashlib\nresponse={'schema':'monzero-ltc-journal-response-v1','requestId':r['requestId'],'snapshot':s}\n";
            script+="if r['schema']=='monzero-ltc-refund-check-request-v1':\n";
            script+=" d={'schema':'monzero-ltc-native-refund-review-v1','tradeId':s['tradeId'],'quoteCommitment':s['quoteCommitment'],'journalCommitment':'c'*64,'preparedCommitment':'d'*64,'dispatchCommitment':'e'*64,'action':'retransmit','txid':'f'*64,'destination':s['xmzRefund'],'refundAtoms':str(int(s['xmzAtoms'])-2),'feeAtoms':'2','maximumFeeAtoms':'3','lockedAtoms':s['xmzAtoms'],'chainObserved':False}\n";
            script+=" c=hashlib.sha256(b'MONZERO-LTC-NATIVE-REFUND-APPROVAL-V1\\0'+json.dumps(d,sort_keys=True,separators=(',',':')).encode()).hexdigest()\n";
            script+=" response.update(schema='monzero-ltc-refund-review-response-v1',review={'details':d,'commitment':c})\n";
            script+="if r['schema']=='monzero-ltc-refund-check-request-v1':\n";
            script+=" import pathlib\n marker=pathlib.Path(__file__+'.checked')\n if marker.exists(): sys.exit(2)\n marker.write_text('checked')\n";
            script+=" response.update(schema='monzero-ltc-refund-check-response-v1',confirmation={'txid':'f'*64,'confirmations':'10','chainObserved':True,'executable':False})\n";
            if (testCase==1) script+=" response['confirmation']['txid']='0'*64\n";
            if (testCase==2) script+=" response['confirmation']['confirmations']='9'\n";
            if (testCase==3) script+=" response['confirmation']['confirmations']='010'\n";
            if (testCase==4) script+=" response['confirmation']['chainObserved']=False\n";
            if (testCase==5) script+=" response['confirmation']['executable']=True\n";
            if (testCase==6) script+=" response['confirmation']['unknownField']=True\n";
            if (testCase==7) script+=" response['review']['details']['tradeId']='different-trade'\n";
            script+="sys.stdout.write(json.dumps(response,sort_keys=True,separators=(',',':')))\n";
            QCOMPARE(helper.write(script),qint64(script.size()));helper.close();
            QVERIFY(helper.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
            const auto digest=QString::fromLatin1(QCryptographicHash::hash(script,QCryptographicHash::Sha256).toHex());
            const auto rpcPath=dir.path()+"/rpc.json";
            QFile rpcFile(rpcPath);QVERIFY(rpcFile.open(QIODevice::WriteOnly));rpcFile.write("{}");rpcFile.close();
            QVERIFY(rpcFile.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
            const auto path=dir.path()+"/config.json";
            QFile config(path);QVERIFY(config.open(QIODevice::WriteOnly));
            const auto bytes=QJsonDocument(QJsonObject{{"schema","monzero-private-ltc-journal-v2"},{"refundRpcFile",rpcPath},
                {"helper",program},{"helperDigest",digest},{"tradeId",trade},{"role","native-funder"},
                {"metadataDirectory",dir.path()},{"snapshotDirectory",dir.path()},
                {"pins",QJsonArray{}},{"policy",QJsonObject{{"ltc_network","ltc-regtest"},{"xmz_confirmations",10},{"ltc_confirmations",2},{"xmz_genesis",QString(64,'b')}}}}).toJson(QJsonDocument::Compact);
            QCOMPARE(config.write(bytes),qint64(bytes.size()));config.close();
            QVERIFY(config.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
            LitecoinJournalJob job;
            QVERIFY(!job.refresh());QVERIFY(job.configure(path));QVERIFY(!job.configure(path));
            QVERIFY(job.refresh());QVERIFY(!job.refresh());
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QVERIFY(job.status().value("valid").toBool());
            QVERIFY(job.status().value("refundCheckAvailable").toBool());
            QVERIFY(job.checkRefund());QVERIFY(!job.checkRefund());
            QCOMPARE(job.status().value("refundCheckState").toString(),QString("checking"));
            QVERIFY(job.status().value("refundConfirmation").toMap().isEmpty());
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QCOMPARE(job.status().value("refundCheckState").toString(),testCase==0?QString("confirmed"):QString("unverified"));
            QCOMPARE(job.status().value("valid").toBool(),testCase==0);
            if (testCase==0) {
                const auto confirmed=job.status().value("refundConfirmation").toMap();
                QCOMPARE(confirmed.value("confirmations").toString(),QString("10"));
                QVERIFY(!confirmed.value("checkedAt").toString().isEmpty());
                QVERIFY(job.checkRefund()); // A subsequent failing service must revoke the displayed result.
                QVERIFY(job.status().value("refundConfirmation").toMap().isEmpty());
                QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
                QCOMPARE(job.status().value("refundCheckState").toString(),QString("unverified"));
            }
            QVERIFY(job.status().value("refundConfirmation").toMap().isEmpty());
            QVERIFY(job.status().value("refundReview").toMap().isEmpty());
            QVERIFY(!job.status().value("refundSubmissionAvailable").toBool());
            QVERIFY(!job.status().value("executable").toBool());
        }
#endif
    }
    void litecoinFundingSubmissionRequiresCurrentReviewAndKeepsUnknownOutcomes() {
#ifdef Q_OS_LINUX
        const QString trade="12345678-1234-4234-8234-123456789abc";
        const QJsonObject payment{{"prepared",true},{"attempted",true},{"acknowledged",true}};
        const QJsonObject recovery{{"attempted",false},{"acknowledged",false}};
        QJsonObject snapshot{{"schema","monzero-ltc-journal-status-v1"},{"tradeId",trade},
            {"quoteCommitment",QString(64,'a')},{"role","native-funder"},{"ltcNetwork","ltc-regtest"},
            {"xmzGenesis",QString(64,'b')},{"ltcAtoms","18446744073709551615"},{"xmzAtoms","18446744073709551613"},
            {"ltcPayout","fixture-ltc-payout"},{"ltcRefund","fixture-ltc-refund"},
            {"xmzPayout","fixture-xmz-payout"},{"xmzRefund","fixture-xmz-refund"},
            {"keysReady",true},{"recoveryIntentRetained",true},{"nativeClaimRetained",false},{"nativeRefundRetained",true},
            {"journalValidated",true},{"chainObserved",false},{"executable",false},
            {"nativeFunding",payment},{"litecoinRedemption",payment},{"nativePayout",payment},{"nativeRefundPayout",payment},
            {"litecoinRecovery",QJsonObject{{"cancel",recovery},{"punish",recovery}}}};
        // Synthetic helper output tests the GUI boundary, not chain or signature evidence.
        for (int testCase=0;testCase<6;++testCase) {
            QTemporaryDir dir;QVERIFY(dir.isValid());
            auto result=snapshot;
            const bool retransmit=false;
            if (!retransmit) result["nativeFunding"]=QJsonObject{{"prepared",true},{"attempted",false},{"acknowledged",false}};
            const auto encoded=QJsonDocument(result).toJson(QJsonDocument::Compact).toBase64();
            const auto program=dir.path()+"/helper";
            QFile helper(program);QVERIFY(helper.open(QIODevice::WriteOnly));
            QByteArray script="#!/usr/bin/python3\nimport json,sys,base64\nr=json.load(sys.stdin)\ns=json.loads(base64.b64decode('"+encoded+"'))\n";
            script+="import hashlib\nresponse={'schema':'monzero-ltc-journal-response-v1','requestId':r['requestId'],'snapshot':s}\n";
            script+="if r['schema']=='monzero-ltc-funding-review-request-v1':\n";
            script+=" d={'schema':'monzero-ltc-native-funding-review-v1','tradeId':s['tradeId'],'quoteCommitment':s['quoteCommitment'],'journalCommitment':'c'*64,'preparedCommitment':'d'*64,'dispatchCommitment':('e'*64 if r['action']=='retransmit' else None),'action':r['action'],'txid':'f'*64,'wallet':'F'*95,'account':0,'priority':1,'destination':'G'*95,'amountAtoms':s['xmzAtoms'],'feeAtoms':'2','maximumFeeAtoms':'3','totalDebitAtoms':str(int(s['xmzAtoms'])+2),'ltcContract':'rltc1'+'q'*58,'ltcAtoms':s['ltcAtoms'],'safetyBlocks':str(r['safetyBlocks']),'expiresAt':'2000000000','chainObserved':False}\n";
            script+=" c=hashlib.sha256(b'MONZERO-LTC-NATIVE-FUNDING-APPROVAL-V1\\0'+json.dumps(d,sort_keys=True,separators=(',',':')).encode()).hexdigest()\n";
            script+=" response.update(schema='monzero-ltc-funding-review-response-v1',review={'details':d,'commitment':c})\n";
            script+="if r['schema']=='monzero-ltc-funding-submit-request-v1':\n";
            script+=" assert r['safetyBlocks']==2 and r['action']=='send'\n";
            script+=" import pathlib\n pathlib.Path(__file__+'.submitted').write_text('attempted')\n";
            if (testCase==1) script+=" sys.exit(2)\n";
            script+=" s['nativeFunding']={'prepared':True,'attempted':True,'acknowledged':True}\n response.update(schema='monzero-ltc-funding-submit-response-v1',outcome={'state':'acknowledged','txid':'f'*64,'feeAtoms':'2','chainObserved':False,'executable':False})\n";
            if (testCase==2) script+=" response['outcome']['txid']='0'*64\n";
            if (testCase==3) script+=" response['outcome']['feeAtoms']='3'\n";
            if (testCase==4) script+=" response['outcome']['chainObserved']=True\n";
            script+="sys.stdout.write(json.dumps(response,sort_keys=True,separators=(',',':')))\n";
            QCOMPARE(helper.write(script),qint64(script.size()));helper.close();
            QVERIFY(helper.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
            const auto digest=QString::fromLatin1(QCryptographicHash::hash(script,QCryptographicHash::Sha256).toHex());
            const auto rpcPath=dir.path()+"/rpc.json";
            QFile rpcFile(rpcPath);QVERIFY(rpcFile.open(QIODevice::WriteOnly));rpcFile.write("{}");rpcFile.close();
            QVERIFY(rpcFile.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
            const auto path=dir.path()+"/config.json";
            QFile config(path);QVERIFY(config.open(QIODevice::WriteOnly));
            const auto bytes=QJsonDocument(QJsonObject{{"schema","monzero-private-ltc-journal-v4"},{"fundingRpcFile",rpcPath},
                {"helper",program},{"helperDigest",digest},{"tradeId",trade},{"role","native-funder"},
                {"metadataDirectory",dir.path()},{"snapshotDirectory",dir.path()},
                {"pins",QJsonArray{}},{"policy",QJsonObject{{"ltc_network","ltc-regtest"},{"xmz_confirmations",10},{"ltc_confirmations",2},{"xmz_genesis",QString(64,'b')}}}}).toJson(QJsonDocument::Compact);
            QCOMPARE(config.write(bytes),qint64(bytes.size()));config.close();
            QVERIFY(config.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
            LitecoinJournalJob job;
            QVERIFY(!job.refresh());QVERIFY(job.configure(path));QVERIFY(!job.configure(path));
            QVERIFY(job.refresh());QVERIFY(!job.refresh());
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QVERIFY(job.status().value("valid").toBool());
            QVERIFY(!job.reviewFunding(!retransmit)); // The review action must match the retained attempt.
            QVERIFY(job.reviewFunding(retransmit));QVERIFY(!job.reviewFunding(retransmit));
            QVERIFY(job.status().value("fundingReview").toMap().isEmpty());
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QVERIFY(job.status().value("valid").toBool());
            const auto review=job.status().value("fundingReview").toMap();
            const auto commitment=review.value("commitment").toString();
            QVERIFY(job.status().value("fundingSubmissionAvailable").toBool());
            QVERIFY(!job.submitRefund(commitment));QVERIFY(!job.submitPayout(commitment));
            QVERIFY(!job.checkRefund());QVERIFY(!job.checkPayout());
            QVERIFY(!job.status().value("refundSubmissionConfigured").toBool());
            QVERIFY(!job.submitFunding(QString(64,'0')));
            QVERIFY(!QFile::exists(program+".submitted"));
            if (testCase==5) {
                QVERIFY(rpcFile.open(QIODevice::WriteOnly|QIODevice::Truncate));rpcFile.write("{ } ");rpcFile.close();
                QVERIFY(!job.submitFunding(commitment));
                QCOMPARE(job.status().value("fundingSubmission").toMap().value("state").toString(),QString("not-started"));
                QVERIFY(!QFile::exists(program+".submitted"));
            } else {
                QVERIFY(job.submitFunding(commitment));QVERIFY(!job.submitFunding(commitment));
                QVERIFY(job.status().value("fundingReview").toMap().isEmpty());
                QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
                QVERIFY(QFile::exists(program+".submitted"));
                QCOMPARE(job.status().value("fundingSubmission").toMap().value("state").toString(),
                    testCase==0?QString("acknowledged"):QString("unknown"));
                QCOMPARE(job.status().value("valid").toBool(),testCase==0);
            }
            QVERIFY(!job.status().value("fundingSubmissionAvailable").toBool());
            QVERIFY(!job.status().value("executable").toBool());
            QVERIFY(!job.submitFunding(commitment));
        }
#endif
    }
    void litecoinPayoutSubmissionRequiresCurrentReviewAndKeepsUnknownOutcomes() {
#ifdef Q_OS_LINUX
        const QString trade="12345678-1234-4234-8234-123456789abc";
        const QJsonObject payment{{"prepared",true},{"attempted",true},{"acknowledged",true}};
        const QJsonObject recovery{{"attempted",false},{"acknowledged",false}};
        QJsonObject snapshot{{"schema","monzero-ltc-journal-status-v1"},{"tradeId",trade},
            {"quoteCommitment",QString(64,'a')},{"role","litecoin-funder"},{"ltcNetwork","ltc-regtest"},
            {"xmzGenesis",QString(64,'b')},{"ltcAtoms","18446744073709551615"},{"xmzAtoms","18446744073709551615"},
            {"ltcPayout","fixture-ltc-payout"},{"ltcRefund","fixture-ltc-refund"},
            {"xmzPayout","fixture-xmz-payout"},{"xmzRefund","fixture-xmz-refund"},
            {"keysReady",true},{"recoveryIntentRetained",true},{"nativeClaimRetained",false},{"nativeRefundRetained",true},
            {"journalValidated",true},{"chainObserved",false},{"executable",false},
            {"nativeFunding",payment},{"litecoinRedemption",payment},{"nativePayout",payment},{"nativeRefundPayout",payment},
            {"litecoinRecovery",QJsonObject{{"cancel",recovery},{"refund",recovery}}}};
        // Synthetic helper output tests the GUI boundary, not chain or signature evidence.
        for (int testCase=0;testCase<6;++testCase) {
            QTemporaryDir dir;QVERIFY(dir.isValid());
            auto result=snapshot;
            const bool retransmit=false;
            if (!retransmit) result["nativePayout"]=QJsonObject{{"prepared",true},{"attempted",false},{"acknowledged",false}};
            const auto encoded=QJsonDocument(result).toJson(QJsonDocument::Compact).toBase64();
            const auto program=dir.path()+"/helper";
            QFile helper(program);QVERIFY(helper.open(QIODevice::WriteOnly));
            QByteArray script="#!/usr/bin/python3\nimport json,sys,base64\nr=json.load(sys.stdin)\ns=json.loads(base64.b64decode('"+encoded+"'))\n";
            script+="import hashlib\nresponse={'schema':'monzero-ltc-journal-response-v1','requestId':r['requestId'],'snapshot':s}\n";
            script+="if r['schema']=='monzero-ltc-payout-review-request-v1':\n";
            script+=" d={'schema':'monzero-ltc-native-payout-review-v1','tradeId':s['tradeId'],'quoteCommitment':s['quoteCommitment'],'journalCommitment':'c'*64,'preparedCommitment':'d'*64,'dispatchCommitment':('e'*64 if r['action']=='retransmit' else None),'action':r['action'],'txid':'f'*64,'destination':s['xmzPayout'],'reserveDestination':s['xmzRefund'],'reserveReturnAtoms':'7','payoutAtoms':str(int(s['xmzAtoms'])-9),'feeAtoms':'2','maximumFeeAtoms':'3','lockedAtoms':s['xmzAtoms'],'chainObserved':False}\n";
            script+=" c=hashlib.sha256(b'MONZERO-LTC-NATIVE-PAYOUT-APPROVAL-V1\\0'+json.dumps(d,sort_keys=True,separators=(',',':')).encode()).hexdigest()\n";
            script+=" response.update(schema='monzero-ltc-payout-review-response-v1',review={'details':d,'commitment':c})\n";
            script+="if r['schema']=='monzero-ltc-payout-submit-request-v1':\n";
            script+=" import pathlib\n pathlib.Path(__file__+'.submitted').write_text('attempted')\n";
            if (testCase==1) script+=" sys.exit(2)\n";
            script+=" s['nativePayout']={'prepared':True,'attempted':True,'acknowledged':True}\n response.update(schema='monzero-ltc-payout-submit-response-v1',outcome={'state':'acknowledged','txid':'f'*64,'feeAtoms':'2','chainObserved':False,'executable':False})\n";
            if (testCase==2) script+=" response['outcome']['txid']='0'*64\n";
            if (testCase==3) script+=" response['outcome']['feeAtoms']='3'\n";
            if (testCase==4) script+=" response['outcome']['chainObserved']=True\n";
            script+="sys.stdout.write(json.dumps(response,sort_keys=True,separators=(',',':')))\n";
            QCOMPARE(helper.write(script),qint64(script.size()));helper.close();
            QVERIFY(helper.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
            const auto digest=QString::fromLatin1(QCryptographicHash::hash(script,QCryptographicHash::Sha256).toHex());
            const auto rpcPath=dir.path()+"/rpc.json";
            QFile rpcFile(rpcPath);QVERIFY(rpcFile.open(QIODevice::WriteOnly));rpcFile.write("{}");rpcFile.close();
            QVERIFY(rpcFile.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
            const auto path=dir.path()+"/config.json";
            QFile config(path);QVERIFY(config.open(QIODevice::WriteOnly));
            const auto bytes=QJsonDocument(QJsonObject{{"schema","monzero-private-ltc-journal-v3"},{"payoutRpcFile",rpcPath},
                {"helper",program},{"helperDigest",digest},{"tradeId",trade},{"role","litecoin-funder"},
                {"metadataDirectory",dir.path()},{"snapshotDirectory",dir.path()},
                {"pins",QJsonArray{}},{"policy",QJsonObject{{"ltc_network","ltc-regtest"},{"xmz_confirmations",10},{"ltc_confirmations",2},{"xmz_genesis",QString(64,'b')}}}}).toJson(QJsonDocument::Compact);
            QCOMPARE(config.write(bytes),qint64(bytes.size()));config.close();
            QVERIFY(config.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
            LitecoinJournalJob job;
            QVERIFY(!job.refresh());QVERIFY(job.configure(path));QVERIFY(!job.configure(path));
            QVERIFY(job.refresh());QVERIFY(!job.refresh());
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QVERIFY(job.status().value("valid").toBool());
            QVERIFY(!job.reviewPayout(!retransmit)); // The review action must match the retained attempt.
            QVERIFY(job.reviewPayout(retransmit));QVERIFY(!job.reviewPayout(retransmit));
            QVERIFY(job.status().value("payoutReview").toMap().isEmpty());
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QVERIFY(job.status().value("valid").toBool());
            const auto review=job.status().value("payoutReview").toMap();
            const auto commitment=review.value("commitment").toString();
            QVERIFY(job.status().value("payoutSubmissionAvailable").toBool());
            QVERIFY(!job.submitRefund(commitment));
            QVERIFY(!job.submitPayout(QString(64,'0')));
            QVERIFY(!QFile::exists(program+".submitted"));
            if (testCase==5) {
                QVERIFY(rpcFile.open(QIODevice::WriteOnly|QIODevice::Truncate));rpcFile.write("{ } ");rpcFile.close();
                QVERIFY(!job.submitPayout(commitment));
                QCOMPARE(job.status().value("payoutSubmission").toMap().value("state").toString(),QString("not-started"));
                QVERIFY(!QFile::exists(program+".submitted"));
            } else {
                QVERIFY(job.submitPayout(commitment));QVERIFY(!job.submitPayout(commitment));
                QVERIFY(job.status().value("payoutReview").toMap().isEmpty());
                QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
                QVERIFY(QFile::exists(program+".submitted"));
                QCOMPARE(job.status().value("payoutSubmission").toMap().value("state").toString(),
                    testCase==0?QString("acknowledged"):QString("unknown"));
                QCOMPARE(job.status().value("valid").toBool(),testCase==0);
            }
            QVERIFY(!job.status().value("payoutSubmissionAvailable").toBool());
            QVERIFY(!job.status().value("executable").toBool());
            QVERIFY(!job.submitPayout(commitment));
        }
#endif
    }
    void litecoinFundingConfirmationRejectsInvalidEvidenceAndClearsEarlierResult() {
#ifdef Q_OS_LINUX
        const QString trade="12345678-1234-4234-8234-123456789abc";
        const QJsonObject payment{{"prepared",true},{"attempted",true},{"acknowledged",true}};
        const QJsonObject recovery{{"attempted",false},{"acknowledged",false}};
        QJsonObject snapshot{{"schema","monzero-ltc-journal-status-v1"},{"tradeId",trade},
            {"quoteCommitment",QString(64,'a')},{"role","native-funder"},{"ltcNetwork","ltc-regtest"},
            {"xmzGenesis",QString(64,'b')},{"ltcAtoms","18446744073709551615"},{"xmzAtoms","18446744073709551613"},
            {"ltcPayout","fixture-ltc-payout"},{"ltcRefund","fixture-ltc-refund"},
            {"xmzPayout","fixture-xmz-payout"},{"xmzRefund","fixture-xmz-refund"},
            {"keysReady",true},{"recoveryIntentRetained",true},{"nativeClaimRetained",false},{"nativeRefundRetained",true},
            {"journalValidated",true},{"chainObserved",false},{"executable",false},
            {"nativeFunding",payment},{"litecoinRedemption",payment},{"nativePayout",payment},{"nativeRefundPayout",payment},
            {"litecoinRecovery",QJsonObject{{"cancel",recovery},{"punish",recovery}}}};
        // Synthetic helper output tests the GUI boundary, not chain or signature evidence.
        for (int testCase=0;testCase<8;++testCase) {
            QTemporaryDir dir;QVERIFY(dir.isValid());
            auto result=snapshot;
            const bool retransmit=true;
            if (!retransmit) result["nativeFunding"]=QJsonObject{{"prepared",true},{"attempted",false},{"acknowledged",false}};
            const auto encoded=QJsonDocument(result).toJson(QJsonDocument::Compact).toBase64();
            const auto program=dir.path()+"/helper";
            QFile helper(program);QVERIFY(helper.open(QIODevice::WriteOnly));
            QByteArray script="#!/usr/bin/python3\nimport json,sys,base64\nr=json.load(sys.stdin)\ns=json.loads(base64.b64decode('"+encoded+"'))\n";
            script+="import hashlib\nresponse={'schema':'monzero-ltc-journal-response-v1','requestId':r['requestId'],'snapshot':s}\n";
            script+="if r['schema']=='monzero-ltc-funding-check-request-v1':\n";
            script+=" d={'schema':'monzero-ltc-native-funding-review-v1','tradeId':s['tradeId'],'quoteCommitment':s['quoteCommitment'],'journalCommitment':'c'*64,'preparedCommitment':'d'*64,'dispatchCommitment':('e'*64 if r.get('action','retransmit')=='retransmit' else None),'action':r.get('action','retransmit'),'txid':'f'*64,'wallet':'F'*95,'account':0,'priority':1,'destination':'G'*95,'amountAtoms':s['xmzAtoms'],'feeAtoms':'2','maximumFeeAtoms':'3','totalDebitAtoms':str(int(s['xmzAtoms'])+2),'ltcContract':'rltc1'+'q'*58,'ltcAtoms':s['ltcAtoms'],'safetyBlocks':str(r['safetyBlocks']),'expiresAt':'2000000000','chainObserved':False}\n";
            script+=" c=hashlib.sha256(b'MONZERO-LTC-NATIVE-FUNDING-APPROVAL-V1\\0'+json.dumps(d,sort_keys=True,separators=(',',':')).encode()).hexdigest()\n";
            script+=" response.update(schema='monzero-ltc-funding-review-response-v1',review={'details':d,'commitment':c})\n";
            script+="if r['schema']=='monzero-ltc-funding-check-request-v1':\n";
            script+=" import pathlib\n marker=pathlib.Path(__file__+'.checked')\n if marker.exists(): sys.exit(2)\n marker.write_text('checked')\n";
            script+=" response.update(schema='monzero-ltc-funding-check-response-v1',confirmation={'txid':'f'*64,'confirmations':'10','chainObserved':True,'executable':False})\n";
            if (testCase==1) script+=" response['confirmation']['txid']='0'*64\n";
            if (testCase==2) script+=" response['confirmation']['confirmations']='9'\n";
            if (testCase==3) script+=" response['confirmation']['confirmations']='010'\n";
            if (testCase==4) script+=" response['confirmation']['chainObserved']=False\n";
            if (testCase==5) script+=" response['confirmation']['executable']=True\n";
            if (testCase==6) script+=" response['confirmation']['unknownField']=True\n";
            if (testCase==7) script+=" response['review']['details']['tradeId']='different-trade'\n";
            script+="sys.stdout.write(json.dumps(response,sort_keys=True,separators=(',',':')))\n";
            QCOMPARE(helper.write(script),qint64(script.size()));helper.close();
            QVERIFY(helper.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
            const auto digest=QString::fromLatin1(QCryptographicHash::hash(script,QCryptographicHash::Sha256).toHex());
            const auto rpcPath=dir.path()+"/rpc.json";
            QFile rpcFile(rpcPath);QVERIFY(rpcFile.open(QIODevice::WriteOnly));rpcFile.write("{}");rpcFile.close();
            QVERIFY(rpcFile.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
            const auto path=dir.path()+"/config.json";
            QFile config(path);QVERIFY(config.open(QIODevice::WriteOnly));
            const auto bytes=QJsonDocument(QJsonObject{{"schema","monzero-private-ltc-journal-v4"},{"fundingRpcFile",rpcPath},
                {"helper",program},{"helperDigest",digest},{"tradeId",trade},{"role","native-funder"},
                {"metadataDirectory",dir.path()},{"snapshotDirectory",dir.path()},
                {"pins",QJsonArray{}},{"policy",QJsonObject{{"ltc_network","ltc-regtest"},{"xmz_confirmations",10},{"ltc_confirmations",2},{"xmz_genesis",QString(64,'b')}}}}).toJson(QJsonDocument::Compact);
            QCOMPARE(config.write(bytes),qint64(bytes.size()));config.close();
            QVERIFY(config.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
            LitecoinJournalJob job;
            QVERIFY(!job.refresh());QVERIFY(job.configure(path));QVERIFY(!job.configure(path));
            QVERIFY(job.refresh());QVERIFY(!job.refresh());
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QVERIFY(job.status().value("valid").toBool());
            QVERIFY(job.status().value("fundingCheckAvailable").toBool());
            QVERIFY(!job.checkRefund());
            QVERIFY(job.checkFunding());QVERIFY(!job.checkFunding());
            QCOMPARE(job.status().value("fundingCheckState").toString(),QString("checking"));
            QVERIFY(job.status().value("fundingConfirmation").toMap().isEmpty());
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QCOMPARE(job.status().value("fundingCheckState").toString(),testCase==0?QString("confirmed"):QString("unverified"));
            QCOMPARE(job.status().value("valid").toBool(),testCase==0);
            if (testCase==0) {
                const auto confirmed=job.status().value("fundingConfirmation").toMap();
                QCOMPARE(confirmed.value("confirmations").toString(),QString("10"));
                QVERIFY(!confirmed.value("checkedAt").toString().isEmpty());
                QCOMPARE(confirmed.value("fundingAmount").toString(),QString("184467440.73709551613"));
                QCOMPARE(confirmed.value("destination").toString(),QString(95,'G'));
                QVERIFY(job.checkFunding()); // A subsequent failing service must revoke the displayed result.
                QVERIFY(job.status().value("fundingConfirmation").toMap().isEmpty());
                QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
                QCOMPARE(job.status().value("fundingCheckState").toString(),QString("unverified"));
            }
            QVERIFY(job.status().value("fundingConfirmation").toMap().isEmpty());
            QVERIFY(job.status().value("fundingReview").toMap().isEmpty());
            QVERIFY(!job.status().value("fundingSubmissionAvailable").toBool());
            QVERIFY(!job.status().value("executable").toBool());
        }
#endif
    }
    void litecoinPayoutConfirmationRejectsInvalidEvidenceAndClearsEarlierResult() {
#ifdef Q_OS_LINUX
        const QString trade="12345678-1234-4234-8234-123456789abc";
        const QJsonObject payment{{"prepared",true},{"attempted",true},{"acknowledged",true}};
        const QJsonObject recovery{{"attempted",false},{"acknowledged",false}};
        QJsonObject snapshot{{"schema","monzero-ltc-journal-status-v1"},{"tradeId",trade},
            {"quoteCommitment",QString(64,'a')},{"role","litecoin-funder"},{"ltcNetwork","ltc-regtest"},
            {"xmzGenesis",QString(64,'b')},{"ltcAtoms","18446744073709551615"},{"xmzAtoms","18446744073709551615"},
            {"ltcPayout","fixture-ltc-payout"},{"ltcRefund","fixture-ltc-refund"},
            {"xmzPayout","fixture-xmz-payout"},{"xmzRefund","fixture-xmz-refund"},
            {"keysReady",true},{"recoveryIntentRetained",true},{"nativeClaimRetained",false},{"nativeRefundRetained",true},
            {"journalValidated",true},{"chainObserved",false},{"executable",false},
            {"nativeFunding",payment},{"litecoinRedemption",payment},{"nativePayout",payment},{"nativeRefundPayout",payment},
            {"litecoinRecovery",QJsonObject{{"cancel",recovery},{"refund",recovery}}}};
        // Synthetic helper output tests the GUI boundary, not chain or signature evidence.
        for (int testCase=0;testCase<8;++testCase) {
            QTemporaryDir dir;QVERIFY(dir.isValid());
            auto result=snapshot;
            const bool retransmit=true;
            if (!retransmit) result["nativePayout"]=QJsonObject{{"prepared",true},{"attempted",false},{"acknowledged",false}};
            const auto encoded=QJsonDocument(result).toJson(QJsonDocument::Compact).toBase64();
            const auto program=dir.path()+"/helper";
            QFile helper(program);QVERIFY(helper.open(QIODevice::WriteOnly));
            QByteArray script="#!/usr/bin/python3\nimport json,sys,base64\nr=json.load(sys.stdin)\ns=json.loads(base64.b64decode('"+encoded+"'))\n";
            script+="import hashlib\nresponse={'schema':'monzero-ltc-journal-response-v1','requestId':r['requestId'],'snapshot':s}\n";
            script+="if r['schema']=='monzero-ltc-payout-check-request-v1':\n";
            script+=" d={'schema':'monzero-ltc-native-payout-review-v1','tradeId':s['tradeId'],'quoteCommitment':s['quoteCommitment'],'journalCommitment':'c'*64,'preparedCommitment':'d'*64,'dispatchCommitment':'e'*64,'action':'retransmit','txid':'f'*64,'destination':s['xmzPayout'],'reserveDestination':s['xmzRefund'],'reserveReturnAtoms':'7','payoutAtoms':str(int(s['xmzAtoms'])-9),'feeAtoms':'2','maximumFeeAtoms':'3','lockedAtoms':s['xmzAtoms'],'chainObserved':False}\n";
            script+=" c=hashlib.sha256(b'MONZERO-LTC-NATIVE-PAYOUT-APPROVAL-V1\\0'+json.dumps(d,sort_keys=True,separators=(',',':')).encode()).hexdigest()\n";
            script+=" response.update(schema='monzero-ltc-payout-review-response-v1',review={'details':d,'commitment':c})\n";
            script+="if r['schema']=='monzero-ltc-payout-check-request-v1':\n";
            script+=" import pathlib\n marker=pathlib.Path(__file__+'.checked')\n if marker.exists(): sys.exit(2)\n marker.write_text('checked')\n";
            script+=" response.update(schema='monzero-ltc-payout-check-response-v1',confirmation={'txid':'f'*64,'confirmations':'10','chainObserved':True,'executable':False})\n";
            if (testCase==1) script+=" response['confirmation']['txid']='0'*64\n";
            if (testCase==2) script+=" response['confirmation']['confirmations']='9'\n";
            if (testCase==3) script+=" response['confirmation']['confirmations']='010'\n";
            if (testCase==4) script+=" response['confirmation']['chainObserved']=False\n";
            if (testCase==5) script+=" response['confirmation']['executable']=True\n";
            if (testCase==6) script+=" response['confirmation']['unknownField']=True\n";
            if (testCase==7) script+=" response['review']['details']['tradeId']='different-trade'\n";
            script+="sys.stdout.write(json.dumps(response,sort_keys=True,separators=(',',':')))\n";
            QCOMPARE(helper.write(script),qint64(script.size()));helper.close();
            QVERIFY(helper.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
            const auto digest=QString::fromLatin1(QCryptographicHash::hash(script,QCryptographicHash::Sha256).toHex());
            const auto rpcPath=dir.path()+"/rpc.json";
            QFile rpcFile(rpcPath);QVERIFY(rpcFile.open(QIODevice::WriteOnly));rpcFile.write("{}");rpcFile.close();
            QVERIFY(rpcFile.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
            const auto path=dir.path()+"/config.json";
            QFile config(path);QVERIFY(config.open(QIODevice::WriteOnly));
            const auto bytes=QJsonDocument(QJsonObject{{"schema","monzero-private-ltc-journal-v3"},{"payoutRpcFile",rpcPath},
                {"helper",program},{"helperDigest",digest},{"tradeId",trade},{"role","litecoin-funder"},
                {"metadataDirectory",dir.path()},{"snapshotDirectory",dir.path()},
                {"pins",QJsonArray{}},{"policy",QJsonObject{{"ltc_network","ltc-regtest"},{"xmz_confirmations",10},{"ltc_confirmations",2},{"xmz_genesis",QString(64,'b')}}}}).toJson(QJsonDocument::Compact);
            QCOMPARE(config.write(bytes),qint64(bytes.size()));config.close();
            QVERIFY(config.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
            LitecoinJournalJob job;
            QVERIFY(!job.refresh());QVERIFY(job.configure(path));QVERIFY(!job.configure(path));
            QVERIFY(job.refresh());QVERIFY(!job.refresh());
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QVERIFY(job.status().value("valid").toBool());
            QVERIFY(job.status().value("payoutCheckAvailable").toBool());
            QVERIFY(!job.checkRefund());
            QVERIFY(job.checkPayout());QVERIFY(!job.checkPayout());
            QCOMPARE(job.status().value("payoutCheckState").toString(),QString("checking"));
            QVERIFY(job.status().value("payoutConfirmation").toMap().isEmpty());
            QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QCOMPARE(job.status().value("payoutCheckState").toString(),testCase==0?QString("confirmed"):QString("unverified"));
            QCOMPARE(job.status().value("valid").toBool(),testCase==0);
            if (testCase==0) {
                const auto confirmed=job.status().value("payoutConfirmation").toMap();
                QCOMPARE(confirmed.value("confirmations").toString(),QString("10"));
                QVERIFY(!confirmed.value("checkedAt").toString().isEmpty());
                QCOMPARE(confirmed.value("reserveAmount").toString(),QString("0.00000000007"));
                QCOMPARE(confirmed.value("reserveDestination").toString(),QString("fixture-xmz-refund"));
                QVERIFY(job.checkPayout()); // A subsequent failing service must revoke the displayed result.
                QVERIFY(job.status().value("payoutConfirmation").toMap().isEmpty());
                QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
                QCOMPARE(job.status().value("payoutCheckState").toString(),QString("unverified"));
            }
            QVERIFY(job.status().value("payoutConfirmation").toMap().isEmpty());
            QVERIFY(job.status().value("payoutReview").toMap().isEmpty());
            QVERIFY(!job.status().value("payoutSubmissionAvailable").toBool());
            QVERIFY(!job.status().value("executable").toBool());
        }
#endif
    }
    void multiCoinPreviewsAndExecutionGates() {
        DexEngine engine;
        QCOMPARE(engine.supportedPairs().size(), 7);
        const QMap<QString, int> coins{{"BTC",8},{"LTC",8},{"BCH",8},
            {"XMR",12},{"DOGE",8},{"ETH",18},{"RUNE",8}};
        for (auto it = coins.begin(); it != coins.end(); ++it) {
            const QString pair = it.key() + " / XMZ";
            const auto info = engine.marketInfo(pair);
            QVERIFY(info.value("valid").toBool());
            QCOMPARE(info.value("decimals").toInt(), it.value());
            QVERIFY(!info.value("executable").toBool());
            for (const QString &side : {QString("BUY"), QString("SELL")}) {
                const auto preview = engine.previewOrder(pair, side, "2.5000", "100", "Not estimated");
                QVERIFY2(preview.value("valid").toBool(), qPrintable(preview.value("error").toString()));
                QCOMPARE(preview.value("amount").toString(), QString("2.5"));
                QCOMPARE(preview.value("grossXmz").toString(), QString("250"));
                QCOMPARE(preview.value("settlementXmz").toString(),
                    side == "BUY" ? QString("250.0025") : QString("249.9975"));
                QVERIFY(!preview.value("executable").toBool());
            }
            const auto local = engine.createLocalOffer(pair, "SELL", "2", "100", 60);
            QVERIFY(local.value("valid").toBool());
            QVERIFY(!engine.signLocalOffer(local.value("id").toString()));
            QVERIFY(!engine.publishOffer(local.value("id").toString()));
            QVERIFY(!engine.activationReadiness(pair, {{"ready", true}, {"atomicSettlement", true},
                {"BTC", true}, {it.key(), true}}).value("ready").toBool());
            const QString tooPrecise = "1." + QString(it.value(), '0') + "1";
            QVERIFY(!engine.previewOrder(pair, "BUY", tooPrecise, "100", "unknown").value("valid").toBool());
        }
        // Amounts exceed uint64 wei and JS's exact integer range without rounding.
        auto eth = engine.previewOrder("ETH / XMZ", "SELL", "100.123456789012345678", "100", "unknown");
        QVERIFY(eth.value("valid").toBool());
        QCOMPARE(eth.value("amountAtomic").toString(), QString("100123456789012345678"));
        QCOMPARE(eth.value("amount").toString(), QString("100.123456789012345678"));
        QCOMPARE(eth.value("grossXmz").toString(), QString("10012.34567890123"));
        auto xmr = engine.previewOrder("XMR / XMZ", "BUY", "1.000000000001", "100", "unknown");
        QVERIFY(xmr.value("valid").toBool());
        QCOMPARE(xmr.value("amountAtomic").toString(), QString("1000000000001"));
        QCOMPARE(xmr.value("grossXmz").toString(), QString("100.0000000001"));
        for (const QString &invalid : {QString("0"), QString("-1"), QString("1e3"),
                QString("01"), QString("1\n" ) + "2", QString(101,'9'),
                QString("184467440737.09551616")})
            QVERIFY(!engine.previewOrder("DOGE / XMZ", "BUY", invalid, "100", "unknown").value("valid").toBool());
        QVERIFY(!engine.previewOrder("ETH / XMZ", "BUY", QString(78,'9'), "1", "unknown").value("valid").toBool());
        QVERIFY(!engine.previewOrder("ETH / XMZ", "BUY", "1000000000", "100", "unknown").value("valid").toBool());
        // Exercise the atomic range, not overprecision or gross overflow.
        auto boundary = engine.previewOrder("DOGE / XMZ", "BUY", "184467440737.09551615", "0.00000000001", "unknown");
        QVERIFY(boundary.value("valid").toBool());
        QCOMPARE(boundary.value("amountAtomic").toString(), QString("18446744073709551615"));
        boundary = engine.previewOrder("DOGE / XMZ", "BUY", "184467440737.09551616", "0.00000000001", "unknown");
        QCOMPARE(boundary.value("error").toString(), QString("Amount is zero or exceeds the chain atomic range"));
        // uint256 maximum parses, but cannot fit the existing XMZ total range.
        boundary = engine.previewOrder("ETH / XMZ", "BUY", "115792089237316195423570985008687907853269984665640564039457.584007913129639935", "0.00000000001", "unknown");
        QCOMPARE(boundary.value("error").toString(), QString("Order total is too large"));
        boundary = engine.previewOrder("ETH / XMZ", "BUY", "115792089237316195423570985008687907853269984665640564039457.584007913129639936", "0.00000000001", "unknown");
        QCOMPARE(boundary.value("error").toString(), QString("Amount is zero or exceeds the chain atomic range"));
        auto rounded = engine.previewOrder("ETH / XMZ", "BUY", "2.000000000009999999", "1", "unknown");
        QVERIFY(rounded.value("valid").toBool());
        QCOMPARE(rounded.value("grossXmz").toString(), QString("2"));
        QCOMPARE(rounded.value("grossRounding").toString(), QString("down-to-xmz-atom"));
        QVERIFY(!engine.marketInfo("ETH/XMZ").value("valid").toBool());
        QVERIFY(!engine.marketInfo("UNKNOWN / XMZ").value("valid").toBool());
    }

    void nativeQuoteAttachmentLifecycle() {
        DexEngine engine;
        QVERIFY(engine.nativeQuoteReview().isEmpty());
        QVERIFY(!engine.approveNativeQuote(QString(64, 'a')));
        QSignalSpy changed(&engine, &DexEngine::nativeQuoteReviewChanged);
        {
            NativeQuoteCoordinator coordinator("", "", "", {}, {}, {});
            QVERIFY(engine.attachNativeQuoteCoordinator(&coordinator));
            QVERIFY(!engine.attachNativeQuoteCoordinator(&coordinator));
            QVERIFY(engine.nativeQuoteReview().isEmpty());
            QVERIFY(!engine.approveNativeQuote("invalid"));
            QVERIFY(!engine.approveNativeQuote(QString(64, 'a')));
            coordinator.cancel();
        }
        QVERIFY(engine.nativeQuoteReview().isEmpty());
        QVERIFY(!engine.approveNativeQuote(QString(64, 'a')));
        QVERIFY(changed.count() >= 3);
    }
    void recoveryJobTracksSuccessCancellationAndShutdown() {
        QTemporaryDir binaries;
        const auto program=binaries.filePath("recovery-fixture");
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),program));
        QVERIFY(QFile::setPermissions(program,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        QFile executable(program);
        QVERIFY(executable.open(QIODevice::ReadOnly));
        QCryptographicHash hash(QCryptographicHash::Sha256);
        QVERIFY(hash.addData(&executable));
        const auto digest=QString::fromLatin1(hash.result().toHex());
        for (int mode=0; mode<6; ++mode) {
            QTemporaryDir parent;
            SwapWalletRpcProcess owner;
            QPointer<NativeRecoveryJob> job=new NativeRecoveryJob;
            QSignalSpy completed(job,&NativeRecoveryJob::completed);
            NativeRecoveryReceiver::Expected expected{QString(64,'a'),QString(95,'A'),
                QString(64,'b'),QString(64,'c'),mode==0 ? "10" : (mode==5 ? "13" : "11")};
            connect(&owner,&SwapWalletRpcProcess::processStarted,&owner,[&] {
                QTRY_VERIFY(!owner.provisionHelperSession(expected.genesis).isEmpty());
                QVERIFY(job->start(program,digest,parent.path(),&owner,expected,mode==2 ? 50 : 1000));
                QVERIFY(owner.beginWalletOperation().isEmpty());
                if (mode==1) job->cancel();
                if (mode==3) owner.stop();
                if (mode==4) delete job.data();
            });
            QVERIFY(owner.startFresh(program,parent.path(),1,6));
            if (mode==4) QTRY_VERIFY(job.isNull());
            else {
                QTRY_COMPARE(completed.count(),1);
                QCOMPARE(completed.at(0).at(0).toBool(),mode==0);
                QCOMPARE(job->result().value("valid").toBool(),mode==0);
                QVERIFY(!job->result().value("retryAllowed").toBool());
                job->cancel();
                QCOMPARE(completed.count(),1);
            }
            const auto token=owner.beginWalletOperation();
            if (mode==0) {
                QVERIFY(!token.isEmpty());
                QVERIFY(owner.completeWalletOperation(token,true));
                owner.stop();
                QTRY_VERIFY(!job->result().value("valid").toBool());
                QCOMPARE(completed.count(),1);
            }
            else QVERIFY(token.isEmpty());
            delete job.data();
            owner.stop();
        }
    }
    void recoveryJobRejectsUnownedOrUnverifiedLaunch() {
        NativeRecoveryJob job;
        QSignalSpy completed(&job,&NativeRecoveryJob::completed);
        NativeRecoveryReceiver::Expected expected{QString(64,'a'),QString(95,'A'),
            QString(64,'b'),QString(64,'c'),"10"};
        QTemporaryDir journal;
        const auto program=QCoreApplication::applicationFilePath();
        QVERIFY(!job.start(program,QString(64,'0'),journal.path(),nullptr,expected));
        SwapWalletRpcProcess owner;
        QVERIFY(!job.start(program,QString(64,'0'),journal.path(),&owner,expected));
        QVERIFY(!job.start(program,QString(64,'0'),"relative",&owner,expected));
        QVERIFY(!job.isActive());
        QVERIFY(!job.result().value("valid").toBool());
        QVERIFY(!job.result().value("retryAllowed").toBool());
        job.cancel();
        QCOMPARE(completed.count(),0);
    }
    void backendExecutableDigestBoundary() {
        QTemporaryDir directory;
        const auto path=directory.filePath("helper");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray content("disposable executable fixture");
        QCOMPARE(file.write(content),qint64(content.size()));
        file.close();
        const auto safe=QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner;
        QVERIFY(file.setPermissions(safe));
        const auto digest=QString::fromLatin1(QCryptographicHash::hash(content,QCryptographicHash::Sha256).toHex());
        QVERIFY(BackendExecutable::matches(path,digest));
        SwapWalletRpcProcess pinOwner;
        SwapWalletSession pinned(&pinOwner,path,QString(64,'1'));
        QVERIFY(!pinned.pinHelperDigest(QString(64,'0')));
        QVERIFY(pinned.pinHelperDigest(digest));
        QVERIFY(pinned.pinHelperDigest(digest));
        QVERIFY(!pinned.pinHelperDigest(QString(64,'0')));
        QVERIFY(!BackendExecutable::matches(path,QString(64,'0')));
        SwapWalletRpcProcess owner;
        const auto entries=QDir(directory.path()).entryList(QDir::AllEntries|QDir::NoDotAndDotDot);
        QVERIFY(!owner.startFreshWithDigest(path,QString(64,'0'),directory.path(),1,2));
        QCOMPARE(QDir(directory.path()).entryList(QDir::AllEntries|QDir::NoDotAndDotDot),entries);
        QVERIFY(owner.dataDirectory().isEmpty());
        QVERIFY(!BackendExecutable::matches(path,digest.toUpper()));
        QVERIFY(!BackendExecutable::matches("relative",digest));
        QVERIFY(!BackendExecutable::matches(directory.path(),digest));
        QVERIFY(file.setPermissions(safe|QFile::WriteOther));
        QVERIFY(!BackendExecutable::matches(path,digest));
        QVERIFY(!pinned.create("12345678-1234-1234-1234-123456789abc","secret"));
        QCOMPARE(pinned.state(),SwapWalletSession::Unknown);
        QVERIFY(file.setPermissions(QFile::ReadOwner|QFile::WriteOwner));
        QVERIFY(!BackendExecutable::matches(path,digest));
        QVERIFY(file.setPermissions(safe));
        const auto link=directory.filePath("link");
        QVERIFY(QFile::link(path,link));
        QVERIFY(!BackendExecutable::matches(link,digest));
#ifdef Q_OS_LINUX
        const auto hardlink=directory.filePath("hardlink");
        QCOMPARE(::link(QFile::encodeName(path).constData(),QFile::encodeName(hardlink).constData()),0);
        QVERIFY(!BackendExecutable::matches(path,digest));
#endif
    }
    void destroyingSessionAccountsForPendingWork() {
        for (bool fee : {false,true}) {
            QTemporaryDir parent;
            SwapWalletRpcProcess owner;
            const auto program=QCoreApplication::applicationFilePath();
            auto *session=new SwapWalletSession(&owner,program,QString(64,'1'));
            DexEngine engine;
            QVERIFY(engine.attachSwapWalletSession(session));
            QSignalSpy completed(session,&SwapWalletSession::operationCompleted);
            QSignalSpy stopped(&owner,&SwapWalletRpcProcess::stopped);
            connect(&owner,&SwapWalletRpcProcess::processStarted,&owner,[&] {
                QTRY_VERIFY(!owner.provisionHelperSession(QString(64,'1')).isEmpty());
                QVERIFY(session->create("12345678-1234-1234-1234-123456789abc",fee ? "success" : "timeout"));
            });
            QVERIFY(owner.startFresh(program,parent.path(),1,6));
            if (fee) {
                QTRY_COMPARE(completed.count(),1);
                QVERIFY(session->estimateFee(session->address(),"4"));
            } else QTRY_COMPARE(session->state(),SwapWalletSession::Busy);
            const int count=completed.count();
            delete session;
            QCOMPARE(completed.count(),count);
            QVERIFY(!engine.nativeWalletStatus().value("attached").toBool());
            QVERIFY(!engine.nativeFeePreview().value("valid").toBool());
            QVERIFY(owner.beginWalletOperation().isEmpty());
            QTRY_COMPARE(stopped.count(),1);
            QVERIFY(!stopped.at(0).at(0).toBool());
        }
    }
    void sessionCancellationPreservesRecovery() {
        for (int mode : {0,1,2}) {
            QTemporaryDir parent;
            SwapWalletRpcProcess owner;
            const auto program=QCoreApplication::applicationFilePath();
            SwapWalletSession session(&owner,program,QString(64,'1'));
            DexEngine engine;
            QVERIFY(engine.attachSwapWalletSession(&session));
            QVERIFY(!engine.cancelNativeWalletOperation());
            QSignalSpy completed(&session,&SwapWalletSession::operationCompleted), stopped(&owner,&SwapWalletRpcProcess::stopped);
            QVERIFY(!session.cancelPendingOperation());
            bool cancelledFromPreview=false;
            connect(&session,&SwapWalletSession::previewChanged,&session,[&] {
                if (mode==2 && session.preview().value("valid").toBool()) {
                    cancelledFromPreview=session.cancelPendingOperation();
                }
            });
            connect(&owner,&SwapWalletRpcProcess::processStarted,&owner,[&] {
                QTRY_VERIFY(!owner.provisionHelperSession(QString(64,'1')).isEmpty());
                QVERIFY(session.create("12345678-1234-1234-1234-123456789abc",mode==0 ? "timeout" : "success"));
                if (mode==0) {
                    bool cancelled=false;
                    QVERIFY(QMetaObject::invokeMethod(&engine,"cancelNativeWalletOperation",Qt::DirectConnection,
                        Q_RETURN_ARG(bool,cancelled)));
                    QVERIFY(cancelled);
                }
            });
            QVERIFY(owner.startFresh(program,parent.path(),1,6));
            QTRY_COMPARE(completed.count(),1);
            if (mode!=0) {
                QVERIFY(completed.at(0).at(0).toBool());
                QVERIFY(!session.cancelPendingOperation());
                QVERIFY(session.estimateFee(session.address(),mode==1 ? "4" : "1"));
                if (mode==1) QVERIFY(session.cancelPendingOperation());
                QTRY_COMPARE(completed.count(),2);
            }
            QVERIFY(!completed.last().at(0).toBool());
            if (mode==2) QVERIFY(cancelledFromPreview);
            QCOMPARE(session.state(),SwapWalletSession::RecoveryRequired);
            QVERIFY(!engine.cancelNativeWalletOperation());
            QVERIFY(!engine.nativeWalletStatus().value("readyForTrading").toBool());
            QVERIFY(!session.cancelPendingOperation());
            QVERIFY(!session.preview().value("valid").toBool());
            QVERIFY(owner.beginWalletOperation().isEmpty());
            QVERIFY(!session.create("12345678-1234-1234-1234-123456789abc","success"));
            QTRY_COMPARE(stopped.count(),1);
            QVERIFY(!stopped.at(0).at(0).toBool());
        }
    }
    void walletPresentationDoesNotEnableTrading() {
        DexEngine engine;
        QVERIFY(!engine.cancelNativeWalletOperation());
        QVERIFY(!engine.requestNativeFeePreview(QString(95,'1'),"1"));
        QCOMPARE(engine.nativeWalletStatus().value("state").toString(),QString("unattached"));
        QVERIFY(!engine.attachSwapWalletSession(nullptr));
        auto *session=new SwapWalletSession(nullptr,{},{});
        SwapWalletSession other(nullptr,{},{});
        QSignalSpy changed(&engine,&DexEngine::nativeWalletStatusChanged);
        QVERIFY(engine.attachSwapWalletSession(session));
        QVERIFY(engine.attachSwapWalletSession(session));
        QVERIFY(!engine.attachSwapWalletSession(&other));
        QCOMPARE(engine.nativeWalletStatus().value("state").toString(),QString("stopped"));
        QVERIFY(!engine.nativeWalletStatus().value("readyForTrading").toBool());
        delete session;
        QCOMPARE(engine.nativeWalletStatus().value("state").toString(),QString("unattached"));
        QVERIFY(!engine.nativeFeePreview().value("valid").toBool());
        QVERIFY(changed.count()>=2);
    }
    void swapWalletSessionCoordinatesControlAndPreview() {
        for (bool failPreview : {false,true}) {
            QTemporaryDir parent;
            SwapWalletRpcProcess owner;
            const auto program=QCoreApplication::applicationFilePath();
            const auto helper=parent.filePath("pinned-helper");
            QVERIFY(QFile::copy(program,helper));
            QVERIFY(QFile::setPermissions(helper,QFile::ReadOwner|QFile::ExeOwner));
            QFile helperFile(helper);
            QVERIFY(helperFile.open(QIODevice::ReadOnly));
            QCryptographicHash helperHash(QCryptographicHash::Sha256);
            QVERIFY(helperHash.addData(&helperFile));
            SwapWalletSession session(&owner,helper,QString(64,'1'));
            QVERIFY(session.pinHelperDigest(QString::fromLatin1(helperHash.result().toHex())));
            QVERIFY(session.pinRecoveryDirectory(parent.path()));
            QVERIFY(!session.pinRecoveryDirectory("/different"));
            DexEngine engine;
            QVERIFY(engine.attachSwapWalletSession(&session));
            QSignalSpy completed(&session,&SwapWalletSession::operationCompleted), stopped(&owner,&SwapWalletRpcProcess::stopped);
            QCOMPARE(session.state(),SwapWalletSession::Unknown);
            QVERIFY(!session.create("../wallet","password"));
            QVERIFY(!session.create("12345678-1234-1234-1234-123456789abc",{}));
            QVERIFY(!session.create("12345678-1234-1234-1234-123456789abc",QByteArray(1025,'x')));
            QVERIFY(!session.create("12345678-1234-1234-1234-123456789abc",QByteArray("a\0b",3)));
            QVERIFY(!session.open("12345678-1234-1234-1234-123456789abc","password","invalid"));
            QCOMPARE(session.state(),SwapWalletSession::Unknown);
            QVERIFY(!session.estimateFee("unused","1"));
            QVERIFY(!session.close());
            connect(&owner,&SwapWalletRpcProcess::processStarted,&owner,[&] {
                QTRY_VERIFY(!owner.provisionHelperSession(QString(64,'1')).isEmpty());
                QVERIFY(session.create("12345678-1234-1234-1234-123456789abc","success"));
                QCOMPARE(session.state(),SwapWalletSession::Busy);
                QVERIFY(!session.create("12345678-1234-1234-1234-123456789abc","success"));
            });
            QVERIFY(owner.startFresh(program,parent.path(),1,6));
            QTRY_COMPARE(completed.count(),1);
            QVERIFY(completed.at(0).at(0).toBool());
            QCOMPARE(session.state(),SwapWalletSession::Open);
            QCOMPARE(engine.nativeWalletStatus().value("state").toString(),QString("open"));
            QVERIFY(engine.nativeWalletStatus().value("walletIdentityVerified").toBool());
            QVERIFY(!engine.requestNativeRecoveryCheck());
            QVERIFY(!session.pinRecoveryTarget("invalid",QString(64,'c'),"10"));
            QVERIFY(session.pinRecoveryTarget(QString(64,'b'),QString(64,'c'),"10"));
            QVERIFY(!session.pinRecoveryTarget(QString(64,'d'),QString(64,'c'),"10"));
            QVERIFY(engine.nativeRecoveryStatus().value("checkAvailable").toBool());
            QVERIFY(engine.requestNativeRecoveryCheck());
            QVERIFY(!engine.nativeRecoveryStatus().value("checkAvailable").toBool());
            QVERIFY(!engine.requestNativeRecoveryCheck());
            QVERIFY(!session.checkRecovery(QString(64,'b'),QString(64,'c'),"10"));
            QTRY_COMPARE(completed.count(),2);
            QVERIFY(completed.last().at(0).toBool());
            QVERIFY(engine.nativeRecoveryStatus().value("valid").toBool());
            QVERIFY(!engine.nativeRecoveryStatus().value("retryAllowed").toBool());
            completed.removeLast(); // Keep the existing control/preview sequence counts.
            QVERIFY(!engine.nativeWalletStatus().value("readyForTrading").toBool());
            QVERIFY(!session.address().isEmpty());
            QVERIFY(!session.open("12345678-1234-1234-1234-123456789abc","success",session.address()));
            QVERIFY(!engine.requestNativeFeePreview("invalid","1"));
            for (const auto &amount : {QString("0"),QString("-1"),QString("1e3"),QString("0.000000000001"),QString(65,'9')})
                QVERIFY(!engine.requestNativeFeePreview(session.address(),amount));
            QCOMPARE(session.state(),SwapWalletSession::Open);
            bool requested=false;
            const QString destination=session.address(), amount=failPreview ? "0.00000000002" : "0.00000000001";
            QVERIFY(QMetaObject::invokeMethod(&engine,"requestNativeFeePreview",Qt::DirectConnection,
                Q_RETURN_ARG(bool,requested),Q_ARG(QString,destination),Q_ARG(QString,amount)));
            QVERIFY(requested);
            QVERIFY(!session.close());
            QTRY_COMPARE(completed.count(),2);
            QCOMPARE(completed.at(1).at(0).toBool(),!failPreview);
            if (failPreview) {
                QCOMPARE(session.state(),SwapWalletSession::RecoveryRequired);
                QCOMPARE(engine.nativeWalletStatus().value("state").toString(),QString("recovery-required"));
                QVERIFY(!session.close());
                QVERIFY(!session.estimateFee(session.address(),"1"));
            } else {
                QCOMPARE(session.state(),SwapWalletSession::Open);
                QVERIFY(session.preview().value("valid").toBool());
                QVERIFY(engine.nativeFeePreview().value("valid").toBool());
                QCOMPARE(engine.nativeFeePreview().value("destination").toString(),destination);
                QCOMPARE(engine.nativeFeePreview().value("amountXmz").toString(),amount);
                const auto preserved=session.preview();
                QVERIFY(!session.estimateFee("invalid","1"));
                QVERIFY(!session.estimateFee(destination,"0"));
                QVERIFY(!session.estimateFee(destination,"18446744073709551616"));
                QVERIFY(!session.estimateFee(destination,"1","4294967296"));
                QVERIFY(!session.estimateFee(destination,"1","0","5"));
                QCOMPARE(session.state(),SwapWalletSession::Open);
                QCOMPARE(session.preview(),preserved);
                // Replace only the private fixture helper after successful use.
                // Neither control nor preview may invoke the replacement.
                helperFile.close();
                const auto original=parent.filePath("original-pinned-helper");
                QVERIFY(QFile::rename(helper,original));
                QFile replacement(helper);
                QVERIFY(replacement.open(QIODevice::WriteOnly));
                replacement.write("not the pinned helper");
                replacement.close();
                QVERIFY(QFile::setPermissions(helper,QFile::ReadOwner|QFile::ExeOwner));
                const int beforeReject=completed.count();
                QVERIFY(!session.close());
                QVERIFY(!session.estimateFee(destination,"1"));
                QCOMPARE(completed.count(),beforeReject);
                QCOMPARE(session.state(),SwapWalletSession::Open);
                QCOMPARE(session.preview(),preserved);
                const auto token=owner.beginWalletOperation();
                QVERIFY(!token.isEmpty());
                QVERIFY(owner.completeWalletOperation(token,true));
                QVERIFY(QFile::remove(helper));
                QVERIFY(QFile::rename(original,helper));
                QVERIFY(session.close());
                QVERIFY(!session.preview().value("valid").toBool());
                QVERIFY(!engine.nativeFeePreview().value("valid").toBool());
                QTRY_COMPARE(completed.count(),3);
                QVERIFY(completed.at(2).at(0).toBool());
                QCOMPARE(session.state(),SwapWalletSession::Closed);
                QVERIFY(session.address().isEmpty());
            }
            QTRY_COMPARE(stopped.count(),1);
            QCOMPARE(session.state(),failPreview ? SwapWalletSession::RecoveryRequired : SwapWalletSession::Stopped);
        }
    }
    void ownedFeePreviewCancellationRequiresRecovery() {
        const QString address = "FSf9UZS3uZwJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmJ7suhUXwdrDJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmCYGLw3H";
        for (int mode : {0,1,2,3}) {
            QTemporaryDir parent;
            SwapWalletRpcProcess owner;
            NativeFeePreviewController controller;
            QPointer<NativeFeePreviewJob> job = new NativeFeePreviewJob(&controller);
            const NativeFeePreviewReceiver::Expected expected{address,address,mode<2 ? "4" : "1","0","1",QString(64,'1')};
            const auto program = QCoreApplication::applicationFilePath();
            QSignalSpy complete(job,&NativeFeePreviewJob::completed), stopped(&owner,&SwapWalletRpcProcess::stopped);
            QString replacement;
            bool reacted = false;
            connect(&controller,&NativeFeePreviewController::previewChanged,&controller,[&] {
                if (mode<2 || reacted || !controller.preview().value("valid").toBool()) return;
                reacted = true;
                if (mode==2) job->cancel();
                else replacement = controller.begin(expected,1000);
            });
            connect(&owner,&SwapWalletRpcProcess::processStarted,&owner,[&] {
                QTRY_VERIFY(!owner.provisionHelperSession(expected.genesis).isEmpty());
                QVERIFY(job->startWithOwner(program,&owner,expected,1000));
                if (mode<2) QTimer::singleShot(0,&controller,[&] {
                    if (mode==0) job->cancel();
                    else delete job.data();
                });
            });
            QVERIFY(owner.startFresh(program,parent.path(),1,6));
            if (mode==1) QTRY_VERIFY(job.isNull());
            else {
                QTRY_COMPARE(complete.count(),1);
                QVERIFY(!complete.at(0).at(0).toBool());
            }
            QVERIFY(owner.beginWalletOperation().isEmpty());
            QVERIFY(!controller.preview().value("valid").toBool());
            if (mode==3) {
                QVERIFY(!replacement.isEmpty());
                QVERIFY(controller.pendingTimeMs(replacement)>0);
            }
            QTRY_COMPARE(stopped.count(),1);
            QVERIFY(!stopped.at(0).at(0).toBool());
            delete job.data();
        }
    }
    void ownedFeePreviewSerializesWalletOperations() {
        const QString address = "FSf9UZS3uZwJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmJ7suhUXwdrDJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmCYGLw3H";
        for (const auto &amount : {QString("1"),QString("2"),QString("4")}) {
            QTemporaryDir parent;
            SwapWalletRpcProcess owner;
            NativeFeePreviewController controller;
            NativeFeePreviewJob job(&controller), competing(&controller);
            const NativeFeePreviewReceiver::Expected expected{address,address,amount,"0","1",QString(64,'1')};
            const auto program = QCoreApplication::applicationFilePath();
            QSignalSpy complete(&job,&NativeFeePreviewJob::completed), stopped(&owner,&SwapWalletRpcProcess::stopped);
            QVERIFY(!job.startWithOwner(program,nullptr,expected));
            connect(&owner,&SwapWalletRpcProcess::processStarted,&owner,[&] {
                QTRY_VERIFY(!owner.provisionHelperSession(expected.genesis).isEmpty());
                QVERIFY(job.startWithOwner(program,&owner,expected,amount=="4" ? 50 : 1000));
                QVERIFY(owner.beginWalletOperation().isEmpty());
                QVERIFY(!competing.startWithOwner(program,&owner,expected));
            });
            QVERIFY(owner.startFresh(program,parent.path(),1,6));
            QTRY_COMPARE(complete.count(),1);
            QCOMPARE(complete.at(0).at(0).toBool(),amount=="1");
            QCOMPARE(controller.preview().value("valid").toBool(),amount=="1");
            const auto token = owner.beginWalletOperation();
            QCOMPARE(token.isEmpty(),amount!="1");
            if (!token.isEmpty()) QVERIFY(owner.completeWalletOperation(token,true));
            QTRY_COMPARE(stopped.count(),1);
            QCOMPARE(stopped.at(0).at(0).toBool(),amount=="1");
        }
    }
    void walletControlDispatchAccountsForReplies() {
        for (const auto &mode : {QByteArray("success"),QByteArray("wrong-id"),QByteArray("extra"),QByteArray("timeout")}) {
            QTemporaryDir parent;
            QFile session(parent.filePath("private-session"));
            QVERIFY(session.open(QIODevice::WriteOnly));
            session.write("fixture"); session.close();
            SwapWalletRpcProcess owner;
            NativeWalletControlJob job(&owner);
            QSignalSpy complete(&job,&NativeWalletControlJob::completed), stopped(&owner,&SwapWalletRpcProcess::stopped);
            const auto program = QCoreApplication::applicationFilePath();
            connect(&owner,&SwapWalletRpcProcess::processStarted,&owner,[&] {
                QVERIFY(!job.start(program,session.fileName(),"create","12345678-1234-1234-1234-123456789abc",mode,{}));
                QString ownedSession;
                QTRY_VERIFY(!(ownedSession = owner.provisionHelperSession(QString(64,'1'))).isEmpty());
                QCOMPARE(owner.provisionHelperSession(QString(64,'1')),ownedSession);
                QVERIFY(owner.provisionHelperSession(QString(64,'2')).isEmpty());
                QVERIFY(job.start(program,ownedSession,"create","12345678-1234-1234-1234-123456789abc",mode,{},mode == "timeout" ? 50 : 1000));
                QVERIFY(!job.start(program,session.fileName(),"create","12345678-1234-1234-1234-123456789abc",mode,{}));
            });
            QVERIFY(owner.startFresh(program,parent.path(),1,6));
            QTRY_COMPARE(complete.count(),1);
            QCOMPARE(complete.at(0).at(0).toBool(),mode == "success");
            QCOMPARE(job.result().isEmpty(),mode != "success");
            QTRY_COMPARE(stopped.count(),1);
            QCOMPARE(stopped.at(0).at(0).toBool(),mode == "success");
        }
    }
    void walletOperationAvailabilityNotificationsAreQueued() {
        QTemporaryDir parent;SwapWalletRpcProcess owner;
        QSignalSpy changes(&owner,&SwapWalletRpcProcess::availabilityChanged),stopped(&owner,&SwapWalletRpcProcess::stopped);
        bool checked=false;
        connect(&owner,&SwapWalletRpcProcess::processStarted,&owner,[&]{
            QVERIFY(owner.walletOperationAvailable());const auto token=owner.beginWalletOperation();QVERIFY(!token.isEmpty());
            QVERIFY(!owner.walletOperationAvailable());QVERIFY(changes.isEmpty());
            QVERIFY(owner.completeWalletOperation(token,true));QVERIFY(owner.walletOperationAvailable());QVERIFY(changes.isEmpty());checked=true;
        });
        QVERIFY(owner.startFresh(QCoreApplication::applicationFilePath(),parent.path(),1,6));
        QTRY_VERIFY(checked);QTRY_VERIFY(changes.count()>=2);
        QTRY_COMPARE(stopped.count(),1);QVERIFY(!owner.walletOperationAvailable());
    }
    void bchNativeSessionRequiresLiveOwnedService() {
        for (int mode : {0,1,2}) {
            QTemporaryDir parent;
            SwapWalletRpcProcess owner;
            QVERIFY(owner.provisionBchNativeSession().isEmpty());
            QVERIFY(!owner.ownsBchNativeSession(parent.filePath("external")));
            QString descriptor;
            connect(&owner,&SwapWalletRpcProcess::processStarted,&owner,[&] {
                QTRY_VERIFY(!(descriptor=owner.provisionBchNativeSession()).isEmpty());
                QVERIFY(owner.ownsBchNativeSession(descriptor));
                QCOMPARE(owner.provisionBchNativeSession(),descriptor);
                QFile file(descriptor);QVERIFY(file.open(QIODevice::ReadOnly));
                const auto object=QJsonDocument::fromJson(file.readAll()).object();file.close();
                QCOMPARE(object.size(),4);
                QCOMPARE(object.value("schema").toString(),QString("monzero-private-bch-native-rpc-v1"));
                QCOMPARE(object.value("daemonPort").toInt(),1);
                QCOMPARE(object.value("walletPort").toInt(),6);
                QCOMPARE(object.value("loginFile").toString(),owner.loginFile());
                const auto token=owner.beginWalletOperation();QVERIFY(!token.isEmpty());
                QVERIFY(owner.provisionBchNativeSession().isEmpty());
                QVERIFY(owner.ownsBchNativeSession(descriptor));
                if (mode==1) {
                    QVERIFY(!owner.completeWalletOperation(token,false));
                    QVERIFY(!owner.ownsBchNativeSession(descriptor));
                } else {
                    QVERIFY(owner.completeWalletOperation(token,true));
                    if (mode==2) {
                        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
                        file.write("{}");file.close();
                        QVERIFY(!owner.ownsBchNativeSession(descriptor));
                        QVERIFY(owner.provisionBchNativeSession().isEmpty());
                    }
                }
                if (mode==2) {
                    owner.stop();
                    QVERIFY(!owner.ownsBchNativeSession(descriptor));
                }
            });
            QSignalSpy stopped(&owner,&SwapWalletRpcProcess::stopped);
            QVERIFY(owner.startFresh(QCoreApplication::applicationFilePath(),parent.path(),1,6));
            QTRY_COMPARE(stopped.count(),1);
            QVERIFY(!descriptor.isEmpty());
            QVERIFY(!owner.ownsBchNativeSession(descriptor));
            QVERIFY(owner.provisionBchNativeSession().isEmpty());
            QVERIFY(!QFileInfo::exists(descriptor));
            QVERIFY(!QFileInfo::exists(owner.loginFile()));
            QCOMPARE(stopped.at(0).at(0).toBool(),mode==0);
        }
    }
    void unfinishedWalletOperationPreventsCleanRestart() {
        for (int mode : {0,1,2}) {
            QTemporaryDir parent;
            SwapWalletRpcProcess service;
            QVERIFY(service.beginWalletOperation().isEmpty());
            QString token;
            connect(&service,&SwapWalletRpcProcess::processStarted,&service,[&] {
                token = service.beginWalletOperation();
                QVERIFY(!token.isEmpty());
                QVERIFY(service.beginWalletOperation().isEmpty());
                QVERIFY(!service.completeWalletOperation("obsolete",true));
                if (mode == 1) {
                    QVERIFY(!service.completeWalletOperation(token,false));
                    QVERIFY(service.beginWalletOperation().isEmpty());
                    QVERIFY(!service.completeWalletOperation(token,true));
                }
                if (mode == 2) {
                    QVERIFY(service.completeWalletOperation(token,true));
                    const auto next = service.beginWalletOperation();
                    QVERIFY(!next.isEmpty());
                    QVERIFY(!service.completeWalletOperation(token,true));
                    QVERIFY(service.completeWalletOperation(next,true));
                }
            });
            QSignalSpy stopped(&service,&SwapWalletRpcProcess::stopped);
            QVERIFY(service.startFresh(QCoreApplication::applicationFilePath(),parent.path(),1,2));
            QTRY_COMPARE(stopped.count(),1);
            QVERIFY(!token.isEmpty());
            QCOMPARE(stopped.at(0).at(0).toBool(),mode == 2);
            QVERIFY(!service.completeWalletOperation(token,true));
            QFile state(QDir(service.dataDirectory()).filePath("service.state"));
            QVERIFY(state.open(QIODevice::ReadOnly));
            QCOMPARE(state.readAll(),mode == 2 ? QByteArray("monzero-swap-rpc-v1\nstopped\n")
                : QByteArray("monzero-swap-rpc-v1\nrecovery-required\n"));
        }
    }
    void unsuccessfulRpcRunCannotResumeClean() {
        for (quint16 port : {quint16(3),quint16(4)}) {
            QTemporaryDir parent;
            SwapWalletRpcProcess service;
            QSignalSpy stopped(&service,&SwapWalletRpcProcess::stopped);
            const auto program = QCoreApplication::applicationFilePath();
            QVERIFY(service.startFresh(program,parent.path(),1,port));
            QTRY_COMPARE(stopped.count(),1);
            QVERIFY(!stopped.at(0).at(0).toBool());
            QFile state(QDir(service.dataDirectory()).filePath("service.state"));
            QVERIFY(state.open(QIODevice::ReadOnly));
            QCOMPARE(state.readAll(),QByteArray("monzero-swap-rpc-v1\nrecovery-required\n"));
            QVERIFY(QFileInfo::exists(QDir(service.dataDirectory()).filePath("wallets/retained-marker")));
            QCOMPARE(QFileInfo::exists(service.loginFile()),port == 4);
            SwapWalletRpcProcess refused;
            QVERIFY(!refused.resumeClean(program,service.dataDirectory(),1,2));
        }
    }
    void rpcCleanRestartRequiresStateAndLock() {
        QTemporaryDir parent;
        const auto program = parent.filePath("restart-helper");
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),program));
        QVERIFY(QFile::setPermissions(program,QFile::ReadOwner|QFile::ExeOwner));
        QFile executable(program);
        QVERIFY(executable.open(QIODevice::ReadOnly));
        QCryptographicHash hash(QCryptographicHash::Sha256);
        QVERIFY(hash.addData(&executable));
        const auto digest=QString::fromLatin1(hash.result().toHex());
        SwapWalletRpcProcess first;
        QSignalSpy finished(&first,&SwapWalletRpcProcess::stopped);
        QVERIFY(first.startFresh(program,parent.path(),1,2));
        QTRY_COMPARE(finished.count(),1);
        const auto directory = first.dataDirectory();
        QLockFile competing(QDir(directory).filePath("service.lock"));
        QVERIFY(competing.tryLock(0));
        SwapWalletRpcProcess next;
        QVERIFY(!next.resumeCleanWithDigest(program,digest,directory,1,2));
        competing.unlock();
        QFile state(QDir(directory).filePath("service.state"));
        QVERIFY(state.open(QIODevice::WriteOnly));
        state.write("monzero-swap-rpc-v1\nactive\n");
        state.close();
        QVERIFY(!next.resumeCleanWithDigest(program,digest,directory,1,2));
        QVERIFY(state.open(QIODevice::WriteOnly));
        state.write("monzero-swap-rpc-v1\nstopped\n");
        state.close();
        QSignalSpy resumed(&next,&SwapWalletRpcProcess::stopped);
        QVERIFY(!next.resumeCleanWithDigest(program,QString(64,'0'),directory,1,2));
        QVERIFY(state.open(QIODevice::ReadOnly));
        QCOMPARE(state.readAll(),QByteArray("monzero-swap-rpc-v1\nstopped\n"));
        state.close();
        QVERIFY(next.dataDirectory().isEmpty());
        QVERIFY(next.resumeCleanWithDigest(program,digest,directory,1,2));
        QCOMPARE(next.dataDirectory(),directory);
        QTRY_COMPARE(resumed.count(),1);
        QVERIFY(resumed.at(0).at(0).toBool());
    }
    void separateRpcProcessRetainsWalletData() {
        QTemporaryDir parent;
        QVERIFY(parent.isValid());
        SwapWalletRpcProcess service;
        QSignalSpy stopped(&service,&SwapWalletRpcProcess::stopped);
        const auto program = QCoreApplication::applicationFilePath();
        QVERIFY(!service.startFresh(program,parent.path(),0,2));
        QVERIFY(!service.startFresh(program,parent.path(),2,2));
        QVERIFY(!service.startFresh(program,"relative",1,2));
        QFile executable(program);
        QVERIFY(executable.open(QIODevice::ReadOnly));
        QCryptographicHash hash(QCryptographicHash::Sha256);
        QVERIFY(hash.addData(&executable));
        // Fixture-only expected hash, not a production provenance mechanism.
        const auto digest=QString::fromLatin1(hash.result().toHex());
        const auto installed=parent.filePath("verified-test-helper");
        QVERIFY(QFile::copy(program,installed));
        QVERIFY(QFile::setPermissions(installed,QFile::ReadOwner|QFile::ExeOwner));
        QVERIFY(service.startFreshWithDigest(installed,digest,parent.path(),1,2));
        QVERIFY(service.dataDirectory() != parent.path());
        QVERIFY(!service.startFresh(program,parent.path(),1,2));
        QTRY_COMPARE(stopped.count(),1);
        QVERIFY(stopped.at(0).at(0).toBool());
        QVERIFY(!QFileInfo::exists(service.loginFile()));
        QFile marker(QDir(service.dataDirectory()).filePath("wallets/retained-marker"));
        QVERIFY(marker.open(QIODevice::ReadOnly));
        QCOMPARE(marker.readAll(),QByteArray("disposable-wallet-marker"));
        service.stop();
        QCOMPARE(stopped.count(),1);
    }
    void destroyingJobCancelsOnlyItsPreview() {
        const QString address = "FSf9UZS3uZwJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmJ7suhUXwdrDJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmCYGLw3H";
        NativeFeePreviewReceiver::Expected expected{address,address,"4","0","1",QString(64,'1')};
        NativeFeePreviewController controller;
        QSignalSpy changed(&controller,&NativeFeePreviewController::previewChanged);
        auto *job = new NativeFeePreviewJob(&controller);
        QVERIFY(job->start(QCoreApplication::applicationFilePath(),expected,3000));
        const int before = changed.count();
        delete job;
        QCOMPARE(changed.count(),before+1);
        QVERIFY(!controller.preview().value("valid").toBool());
        job = new NativeFeePreviewJob(&controller);
        QVERIFY(job->start(QCoreApplication::applicationFilePath(),expected,3000));
        const auto newer = controller.begin(expected,3000);
        QVERIFY(!newer.isEmpty());
        const int after = changed.count();
        delete job;
        QCOMPARE(changed.count(),after);
        QVERIFY(controller.pendingTimeMs(newer)>0);
        controller.cancel();
        expected.amountAtoms = "1";
        job = new NativeFeePreviewJob(&controller);
        QSignalSpy finished(job,&NativeFeePreviewJob::completed);
        QVERIFY(job->start(QCoreApplication::applicationFilePath(),expected,3000));
        QTRY_COMPARE(finished.count(),1);
        QVERIFY(controller.preview().value("valid").toBool());
        bool attempted = false;
        auto connection = connect(&controller,&NativeFeePreviewController::previewChanged,&controller,[&] {
            attempted = true;
            QVERIFY(!job->start(QCoreApplication::applicationFilePath(),expected,3000));
        });
        delete job;
        disconnect(connection);
        QVERIFY(attempted);
        QVERIFY(!controller.preview().value("valid").toBool());
    }
    void nativeFeeSessionLaunch() {
        const QString address = "FSf9UZS3uZwJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmJ7suhUXwdrDJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmCYGLw3H";
        NativeFeePreviewReceiver::Expected expected{address,address,"7","0","1",QString(64,'1')};
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile session(directory.filePath("session with spaces.json"));
        QVERIFY(session.open(QIODevice::WriteOnly));
        session.write("controlled-session-marker");
        session.close();
        NativeFeePreviewController controller;
        NativeFeePreviewJob job(&controller);
        QSignalSpy finished(&job,&NativeFeePreviewJob::completed);
        const auto program = QCoreApplication::applicationFilePath();
        QVERIFY(!job.startWithSession(program,{},expected));
        QVERIFY(!job.startWithSession(program,"relative.json",expected));
        QVERIFY(!job.startWithSession(program,directory.path(),expected));
        QVERIFY(!job.startWithSession(program,directory.filePath("missing"),expected));
        QVERIFY(QFile::link(session.fileName(),directory.filePath("link")));
        QVERIFY(!job.startWithSession(program,directory.filePath("link"),expected));
        QCOMPARE(finished.count(),0);
        QVERIFY(job.startWithSession(program,session.fileName(),expected,3000));
        QTRY_COMPARE(finished.count(),1);
        QVERIFY(finished.at(0).at(0).toBool());
        QVERIFY(controller.preview().value("valid").toBool());
        QVERIFY(!controller.preview().value("executable").toBool());
    }
    void expiredRequestDoesNotLaunchHelper() {
        const QString address = "FSf9UZS3uZwJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmJ7suhUXwdrDJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmCYGLw3H";
        NativeFeePreviewReceiver::Expected expected{address,address,"1","0","1",QString(64,'1')};
        NativeFeePreviewController controller;
        NativeFeePreviewJob job(&controller);
        bool delayed = false;
        connect(&controller,&NativeFeePreviewController::previewChanged,&job,[&] {
            if (delayed) return;
            delayed = true;
            QThread::msleep(20); // Simulate a slow synchronous UI callback, without processing timers.
        });
        QSignalSpy finished(&job,&NativeFeePreviewJob::completed);
        QVERIFY(!job.start(QCoreApplication::applicationFilePath(),expected,1));
        QCOMPARE(finished.count(),1);
        QVERIFY(!finished.at(0).at(0).toBool());
        QVERIFY(!controller.preview().value("valid").toBool());
        QCOMPARE(controller.pendingTimeMs("obsolete"),0);
    }
    void nativeFeePreviewProcessTransport() {
        const QString address = "FSf9UZS3uZwJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmJ7suhUXwdrDJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmCYGLw3H";
        for (const auto &mode : {QString("1"),QString("2"),QString("3"),QString("4"),QString("5"),QString("6")}) {
            NativeFeePreviewReceiver::Expected expected{address,address,mode,"0","1",QString(64,'1')};
            NativeFeePreviewController controller;
            NativeFeePreviewJob job(&controller);
            QSignalSpy finished(&job,&NativeFeePreviewJob::completed);
            bool attempted = false;
            connect(&controller,&NativeFeePreviewController::previewChanged,&job,[&] {
                if (attempted) return;
                attempted = true;
                QVERIFY(!job.start(QCoreApplication::applicationFilePath(),expected));
            });
            QVERIFY(job.start(QCoreApplication::applicationFilePath(),expected,mode == "4" ? 200 : 3000));
            QVERIFY(!job.start(QCoreApplication::applicationFilePath(),expected));
            QTRY_COMPARE(finished.count(),1);
            QCOMPARE(finished.at(0).at(0).toBool(),mode == "1");
            QCOMPARE(controller.preview().value("valid").toBool(),mode == "1");
            QVERIFY(!controller.preview().value("executable").toBool());
        }
    }
    void oldProcessCannotCancelNewPreview() {
        const QString address = "FSf9UZS3uZwJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmJ7suhUXwdrDJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmCYGLw3H";
        NativeFeePreviewReceiver::Expected expected{address,address,"4","0","1",QString(64,'1')};
        NativeFeePreviewController controller;
        NativeFeePreviewJob old(&controller), newer(&controller);
        QSignalSpy oldDone(&old,&NativeFeePreviewJob::completed), newDone(&newer,&NativeFeePreviewJob::completed);
        QVERIFY(old.start(QCoreApplication::applicationFilePath(),expected,300));
        expected.amountAtoms = "1";
        QVERIFY(newer.start(QCoreApplication::applicationFilePath(),expected,3000));
        QTRY_COMPARE(newDone.count(),1);
        QVERIFY(newDone.at(0).at(0).toBool());
        QTRY_COMPARE(oldDone.count(),1);
        QVERIFY(!oldDone.at(0).at(0).toBool());
        QVERIFY(controller.preview().value("valid").toBool());
        QCOMPARE(controller.preview().value("amountAtoms").toString(),QString("1"));
    }
    void nativeFeePreviewControllerExpires() {
        const QString address = "FSf9UZS3uZwJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmJ7suhUXwdrDJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmCYGLw3H";
        NativeFeePreviewReceiver::Expected expected{address,address,"100000000000","0","1",QString(64,'1')};
        DexEngine engine;
        auto &controller = *engine.feePreviewController();
        QSignalSpy exposed(&engine, &DexEngine::nativeFeePreviewChanged);
        QSignalSpy changed(&controller, &NativeFeePreviewController::previewChanged);
        const auto id = controller.begin(expected,100);
        QJsonObject object{{"schema","monzero-native-fee-preview-v1"},{"requestId",id},
            {"source","native-nonrelayed-construction"},{"executable",false},
            {"wallet",address},{"destination",address},{"amountAtoms",expected.amountAtoms},
            {"feeAtoms","250000000"},{"weight","2000"},{"account","0"},{"priority","1"},
            {"genesis",expected.genesis},{"tipHash",QString(64,'2')},{"tipHeight","100"}};
        QVERIFY(controller.accept(id,QJsonDocument(object).toJson(QJsonDocument::Compact)).value("valid").toBool());
        QCOMPARE(changed.count(),2);
        QVERIFY(engine.nativeFeePreview().value("valid").toBool());
        QTRY_COMPARE(changed.count(),3);
        QVERIFY(!controller.preview().value("valid").toBool());
        QVERIFY(!controller.preview().contains("feeAtoms"));
        QVERIFY(!engine.nativeFeePreview().value("valid").toBool());
        QCOMPARE(exposed.count(),changed.count());
        controller.begin(expected,10);
        controller.cancel();
        const int cancelled = changed.count();
        QTest::qWait(30);
        QCOMPARE(changed.count(),cancelled);
        QVERIFY(!controller.preview().value("valid").toBool());
        const auto obsolete = controller.begin(expected);
        const auto newest = controller.begin(expected);
        const int awaiting = changed.count();
        QVERIFY(!controller.accept(obsolete,"invalid old response").value("valid").toBool());
        QCOMPARE(changed.count(),awaiting);
        object["requestId"] = newest;
        QVERIFY(controller.accept(newest,QJsonDocument(object).toJson(QJsonDocument::Compact)).value("valid").toBool());
        const int displaying = changed.count();
        QVERIFY(!controller.accept(obsolete,"invalid old response").value("valid").toBool());
        QCOMPARE(changed.count(),displaying);
        QVERIFY(controller.preview().value("valid").toBool());
        QCOMPARE(controller.preview().value("requestId").toString(),newest);
        QVERIFY(!controller.cancelRequest(obsolete));
        QVERIFY(controller.preview().value("valid").toBool());
        QVERIFY(controller.cancelRequest(newest));
        QVERIFY(!controller.preview().value("valid").toBool());
        auto *owned = new NativeFeePreviewController;
        owned->begin(expected,10);
        delete owned;
        QTest::qWait(20); // Destroying a pending controller must cancel its owned timer.
    }
    void nativeFeePreviewReceiver() {
        const QString address = "FSf9UZS3uZwJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmJ7suhUXwdrDJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmCYGLw3H";
        NativeFeePreviewReceiver::Expected expected{address,address,"9007199254740993","0","1",QString(64,'1')};
        NativeFeePreviewReceiver receiver;
        const auto payload = [&](const QString &id) {
            return QJsonObject{{"schema","monzero-native-fee-preview-v1"},{"requestId",id},
                {"source","native-nonrelayed-construction"},{"executable",false},
                {"wallet",address},{"destination",address},{"amountAtoms",expected.amountAtoms},
                {"feeAtoms","250000000"},{"weight","2000"},{"account","0"},{"priority","1"},
                {"genesis",expected.genesis},{"tipHash",QString(64,'2')},{"tipHeight","100"}};
        };
        const auto encode = [](const QJsonObject &object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); };
        const auto id = receiver.begin(expected);
        QVERIFY(!id.isEmpty());
        const auto bytes = encode(payload(id));
        const auto accepted = receiver.accept(bytes);
        QVERIFY(accepted.value("valid").toBool());
        QCOMPARE(accepted.value("amountAtoms").toString(), expected.amountAtoms);
        QCOMPARE(accepted.value("feeXmz").toString(), QString("0.0025"));
        QVERIFY(!accepted.value("executable").toBool());
        QVERIFY(!accepted.value("transportAuthenticated").toBool());
        QVERIFY(receiver.current().value("valid").toBool());
        QVERIFY(!receiver.accept(bytes).value("valid").toBool());
        QVERIFY(!receiver.current().value("valid").toBool());
        auto large = payload(receiver.begin(expected));
        large["feeAtoms"] = "9007199254740995";
        const auto exactLarge = receiver.accept(encode(large));
        QVERIFY(exactLarge.value("valid").toBool());
        QCOMPARE(exactLarge.value("feeXmz").toString(), QString("90071.99254740995"));
        const QList<QPair<QString,QJsonValue>> changes{
            {"requestId","wrong"},{"wallet","wrong"},{"destination","wrong"},
            {"genesis",QString(64,'3')},{"account","1"},{"priority","2"},
            {"amountAtoms",1.0},{"feeAtoms",1.0},{"feeAtoms","025000000"},
            {"feeAtoms","18446744073709551615"},{"feeAtoms","0"},{"weight","0"},
            {"tipHeight","-1"},{"tipHash","bad"},{"executable",true},
            {"executable","false"},{"password","secret"},{"schema","wrong"}
        };
        for (const auto &change : changes) {
            auto object = payload(receiver.begin(expected));
            object[change.first] = change.second;
            QVERIFY(!receiver.accept(encode(object)).value("valid").toBool());
        }
        auto duplicate = encode(payload(receiver.begin(expected)));
        duplicate.chop(1);
        duplicate += ",\"feeAtoms\":\"250000000\"}";
        QVERIFY(!receiver.accept(duplicate).value("valid").toBool());
        auto escaped = encode(payload(receiver.begin(expected)));
        escaped.chop(1);
        escaped += ",\"fee\\u0041toms\":\"250000000\"}";
        QVERIFY(!receiver.accept(escaped).value("valid").toBool());
        auto expired = encode(payload(receiver.begin(expected,1)));
        QTest::qWait(5);
        QVERIFY(!receiver.accept(expired).value("valid").toBool());
        auto shortLived = encode(payload(receiver.begin(expected,100)));
        QVERIFY(receiver.accept(shortLived).value("valid").toBool());
        QTest::qWait(110);
        QVERIFY(!receiver.current().value("valid").toBool());
        QVERIFY(!receiver.current().contains("feeAtoms"));
        auto current = encode(payload(receiver.begin(expected)));
        QVERIFY(receiver.accept(current).value("valid").toBool());
        receiver.begin(expected);
        QVERIFY(!receiver.current().value("valid").toBool());
        const auto old = encode(payload(receiver.begin(expected)));
        receiver.begin(expected);
        QVERIFY(!receiver.accept(old).value("valid").toBool());
        receiver.begin(expected);
        QVERIFY(!receiver.accept(QByteArray(2049,' ')).value("valid").toBool());
        QVERIFY(receiver.begin(expected,0).isEmpty());
        QVERIFY(receiver.begin(expected,30001).isEmpty());
        auto cancelled = encode(payload(receiver.begin(expected)));
        receiver.cancel();
        QVERIFY(!receiver.accept(cancelled).value("valid").toBool());
        receiver.begin(expected);
        QVERIFY(!receiver.accept("{").value("valid").toBool());
    }
    void researchSnapshotCannotEnableTrading() {
        QTemporaryDir directory;
        DexEngine engine;
        const auto path = directory.filePath("offers.json");
        QVERIFY(!engine.readResearchMarket(path).value("valid").toBool());
        const auto now = QDateTime::currentSecsSinceEpoch();
        OfferIdentity maker;
        SignedOffer::Terms terms{"monzero-dex-offer-v1", "isolated-btc-xmz",
            "12345678-1234-4abc-8def-1234567890ab", "BTC/XMZ", "sell",
            "100000", "117250000000000000", now, now + 600};
        OfferBookStore book(path, terms.protocolId, terms.network);
        QVERIFY(book.insert(SignedOffer::create(terms, maker), now));
        const auto result = engine.readResearchMarket(path);
        QVERIFY(result.value("valid").toBool());
        QCOMPARE(result.value("offers").toList().size(), 1);
        QVERIFY(!result.value("executable").toBool());
        QVERIFY(!result.value("liquidityVerified").toBool());
        QVERIFY(!engine.signLocalOffer(terms.offerId));
        QVERIFY(!engine.publishOffer(terms.offerId));
        QVERIFY(engine.localOffers().isEmpty());
        QSignalSpy ready(&engine, &DexEngine::researchMarketReady);
        QVERIFY(engine.refreshResearchMarket(path));
        QVERIFY(engine.researchMarketBusy());
        QVERIFY(!engine.refreshResearchMarket(path));
        QCOMPARE(ready.size(), 0);
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(!engine.researchMarketBusy());
        const auto asyncResult = ready.at(0).at(0).toMap();
        QVERIFY(asyncResult.value("valid").toBool());
        QCOMPARE(asyncResult.value("offers").toList().size(), 1);
        QVERIFY(!asyncResult.value("executable").toBool());
        QVERIFY(engine.refreshResearchMarket(directory.filePath("missing.json")));
        QTRY_COMPARE(ready.size(), 2);
        QVERIFY(!ready.at(1).at(0).toMap().value("valid").toBool());
        QVERIFY(!engine.researchMarketBusy());
    }

    void destructionDuringSnapshotDoesNotLeaveEngineReferences() {
        QTemporaryDir directory;
        auto *engine = new DexEngine;
        QVERIFY(engine->refreshResearchMarket(directory.filePath("missing.json")));
        delete engine;
        QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
        QCoreApplication::processEvents();
    }

    void testAtomicBounds() {
        quint64 value = 0;
        QVERIFY(!DexUtils::parseAtomic("1", -1, value));
        QVERIFY(!DexUtils::parseAtomic("1", 20, value));
        QVERIFY(!DexUtils::parseAtomic("18446744073709551616", 8, value));
        QVERIFY(DexUtils::parseAtomic("0.00000001", 8, value));
        QCOMPARE(value, quint64(1));
        QVERIFY(DexUtils::parseAtomic("1", 11, value));
        QCOMPARE(value, quint64(100000000000ULL));
        QCOMPARE(DexUtils::formatAtomic(value, 11), QString("1"));
        QVERIFY(DexUtils::parseAtomic("0.00000000001", 11, value));
        QCOMPARE(value, quint64(1));
        QVERIFY(!DexUtils::parseAtomic("0.000000000001", 11, value));
    }
    void testOfferLimitAndDebit() {
        DexEngine engine;
        auto quote = engine.previewOrder("BTC / XMZ", "BUY", "0.001", "1172500", "unknown");
        QCOMPARE(quote.value("settlementXmz").toString(), QString("1172.5025"));
        QCOMPARE(quote.value("protocolFeeXmz").toString(), QString("0"));
        QVERIFY(!quote.value("executable").toBool());
        const auto sell = engine.previewOrder("BTC / XMZ", "SELL", "0.001", "1172500", "unknown");
        QCOMPARE(sell.value("settlementXmz").toString(), QString("1172.4975"));
        QCOMPARE(sell.value("protocolFeeXmz").toString(), QString("0"));
        for (int i = 0; i < 100; ++i)
            QVERIFY(engine.createLocalOffer("BTC / XMZ", "BUY", "1", "100", 3600).value("valid").toBool());
        QVERIFY(!engine.createLocalOffer("BTC / XMZ", "BUY", "1", "100", 3600).value("valid").toBool());
        QVERIFY(!engine.fundingPlan("BTC", "XMZ", "0", "1", "1", "100").value("executable").toBool());
    }
    void testPreviewFeeProvenance() {
        // Separate from executable quotes: previews must never imply measured fees.
        DexEngine previews;
        for (const auto &pair : previews.supportedPairs()) {
            for (const auto &side : {QString("BUY"), QString("SELL")}) {
                const auto offer = previews.createLocalOffer(pair, side, "1", "100", 3600);
                QVERIFY(offer.value("valid").toBool());
                QVERIFY(offer.contains("feeEstimateVerified"));
                QVERIFY(!offer.value("feeEstimateVerified").toBool());
                QCOMPARE(offer.value("feeEstimateSource").toString(), QString("fixed-research-placeholder"));
                QVERIFY(offer.value("settlementLabel").toString().startsWith("Estimated "));
                QVERIFY(!offer.value("executable").toBool());
            }
        }
        QCOMPARE(previews.localOffers().size(), previews.supportedPairs().size() * 2);
        for (const auto &entry : previews.localOffers()) {
            const auto saved = entry.toMap();
            QCOMPARE(saved.value("feeEstimateSource").toString(), QString("fixed-research-placeholder"));
            QVERIFY(saved.contains("feeEstimateVerified"));
            QVERIFY(!saved.value("feeEstimateVerified").toBool());
        }
    }
    void testFundingPlan() {
        DexEngine engine;
        auto plan = engine.fundingPlan("BTC", "XMZ", "0", "1", "1", "100");
        QVERIFY(plan.value("valid").toBool());
        QVERIFY(plan.value("funded").toBool());
        QVERIFY(!plan.value("balanceVerified").toBool());
        QVERIFY(!plan.value("executable").toBool());
        QCOMPARE(plan.value("netReceiveXmz").toString(), QString("98"));
        plan = engine.fundingPlan("BTC", "LTC", "1.99999999999", "1", "1", "0");
        QVERIFY(plan.value("valid").toBool());
        QVERIFY(!plan.value("funded").toBool());
        QCOMPARE(plan.value("shortfallXmz").toString(), QString("0.00000000001"));
        QVERIFY(engine.fundingPlan("BTC", "LTC", "2", "1", "1", "0").value("funded").toBool());
        plan = engine.fundingPlan("XMZ", "BTC", "102", "1", "1", "100");
        QVERIFY(plan.value("funded").toBool());
        QCOMPARE(plan.value("requiredUnlockedXmz").toString(), QString("102"));
        QVERIFY(!engine.fundingPlan("BTC", "XMZ", "0", "1", "1", "2.99999999999").value("valid").toBool());
        QVERIFY(!engine.fundingPlan("BTC", "LTC", "2", "1", "1", "1").value("valid").toBool());
        QVERIFY(!engine.fundingPlan("BTC", "BTC", "2", "1", "1", "0").value("valid").toBool());
        QVERIFY(!engine.fundingPlan("ETH", "XMZ", "2", "1", "1", "100").value("valid").toBool());
        QVERIFY(!engine.fundingPlan("BTC", "XMZ", "0", "0", "1", "100").value("valid").toBool());
        QVERIFY(!engine.fundingPlan("BTC", "XMZ", "-1", "1", "1", "100").value("valid").toBool());
        QVERIFY(!engine.fundingPlan("BTC", "XMZ", "0", "184467440.73709551615", "1", "100").value("valid").toBool());
        QVERIFY(!engine.fundingPlan("XMZ", "BTC", "0", "1", "1", "184467440.73709551615").value("valid").toBool());
    }
    void testValidation() {
        DexEngine engine;
        QVERIFY(engine.selfTest().value("passed").toBool());
        QVERIFY(!engine.createLocalOffer("BTC / XMZ", "BUY", "-1", "100", 3600).value("valid").toBool());
        QVERIFY(!engine.createLocalOffer("BTC / XMZ", "BUY", "1", "100", 0).value("valid").toBool());
        QVERIFY(!engine.activationReadiness("BTC / XMZ", {}).value("ready").toBool());
        const auto claimed = engine.activationReadiness("BTC / XMZ",
            {{"feeEnforcementVerified", true}, {"atomicSettlement", true}, {"ready", true}});
        QVERIFY(!claimed.value("ready").toBool());
        QVERIFY(claimed.contains("feeEnforcementVerified"));
        QVERIFY(!claimed.value("feeEnforcementVerified").toBool());
        QCOMPARE(claimed.value("checksRequired").toInt(), claimed.value("blockers").toStringList().size());
        QVERIFY(!engine.validateOffer({}));
        QVERIFY(!engine.signLocalOffer("missing"));
        QVERIFY(!engine.publishOffer("missing"));
        QVERIFY(!engine.matchOffers("a", "b"));
        QVERIFY(!engine.canTransition(OfferState::Draft, OfferState::Settled));
    }
    void testCancellation() {
        DexEngine engine;
        auto offer = engine.createLocalOffer("BTC / XMZ", "BUY", "1", "100", 3600);
        QCOMPARE(engine.localOffers().size(), 1);
        QVERIFY(engine.cancelLocalOffer(offer.value("id").toString()));
        QVERIFY(engine.localOffers().isEmpty());
        QVERIFY(!engine.cancelLocalOffer(offer.value("id").toString()));
    }
    void testOfferCreation() {
        DexEngine engine;
        auto result = engine.createLocalOffer("BTC / XMZ", "BUY", "1", "100", 3600);
        QVERIFY(result.value("valid").toBool());
    }
};

// Controlled transport fixture, not a native estimator or real wallet backend.
static int feeTransportHelper() {
    QFile input, output, errors;
    if (!input.open(stdin,QIODevice::ReadOnly) || !output.open(stdout,QIODevice::WriteOnly)
        || !errors.open(stderr,QIODevice::WriteOnly)) return 2;
    const auto bytes = input.read(2049);
    if (bytes.size() > 2048) return 2;
    auto object = QJsonDocument::fromJson(bytes).object();
    const auto mode = object.value("amountAtoms").toString();
    if (mode == "7") {
        const auto args = QCoreApplication::arguments();
        if (args.size() != 4 || args.at(2) != "--session-file" || object.size() != 8) return 2;
        QFile session(args.at(3));
        if (!session.open(QIODevice::ReadOnly) || session.readAll() != "controlled-session-marker") return 2;
        if (bytes.contains("session-file") || bytes.contains("controlled-session-marker")) return 2;
    }
    if (mode == "2") { output.write("{invalid"); return 0; }
    if (mode == "3") { output.write(QByteArray(2049,'x')); return 0; }
    if (mode == "4") { QThread::msleep(5000); return 0; }
    if (mode == "5") { errors.write(QByteArray(4097,'x')); return 0; }
    object["schema"] = "monzero-native-fee-preview-v1";
    object["source"] = "native-nonrelayed-construction";
    object["executable"] = false;
    object["feeAtoms"] = "250000000";
    object["weight"] = "2000";
    object["tipHash"] = QString(64,'2');
    object["tipHeight"] = "100";
    output.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    return mode == "6" ? 7 : 0;
}

// Optional coordinator-owned integration entry point. Public request data is
// read on stdin; this does not provision sessions or touch production wallets.
static int nativeFeeJobFixture(QCoreApplication &app) {
    const auto args = app.arguments();
    if (args.size() != 4) return 2;
    QFile input;
    if (!input.open(stdin,QIODevice::ReadOnly)) return 2;
    const auto bytes = input.read(2049);
    if (bytes.size() > 2048) return 2;
    const auto object = QJsonDocument::fromJson(bytes).object();
    if (object.size() != 8 || object.value("schema") != "monzero-native-fee-request-v1") return 2;
    NativeFeePreviewReceiver::Expected expected{object.value("wallet").toString(),
        object.value("destination").toString(),object.value("amountAtoms").toString(),
        object.value("account").toString(),object.value("priority").toString(),object.value("genesis").toString()};
    NativeFeePreviewController controller;
    NativeFeePreviewJob job(&controller);
    QObject::connect(&job,&NativeFeePreviewJob::completed,&app,[&](bool accepted) {
        if (!accepted) { app.exit(2); return; }
        QFile output;
        if (!output.open(stdout,QIODevice::WriteOnly)) { app.exit(2); return; }
        const auto response = QJsonDocument(QJsonObject::fromVariantMap(controller.preview())).toJson(QJsonDocument::Compact);
        if (output.write(response) != response.size() || !output.flush()) { app.exit(2); return; }
        app.exit(0);
    });
    if (!job.startWithSession(args.at(2),args.at(3),expected)) return 2;
    return app.exec();
}

// Funded fixture only: Python owns the offline daemon and performs setup RPCs
// before publishing the fee request. Qt owns the separate wallet RPC throughout.
static int ownedFeeJobFixture(QCoreApplication &app) {
    const auto args = app.arguments();
    const bool coordinated=args.value(1)=="--coordinated-fee-job-fixture";
    if (args.size()!=7 && !(coordinated && args.size()==8)) return 2;
    bool daemonOk=false, walletOk=false;
    const auto daemon=args.at(5).toUShort(&daemonOk), wallet=args.at(6).toUShort(&walletOk);
    if (!daemonOk || !walletOk) return 2;
    const QString genesis="84f9ebdac8924806f037482ec16fd59b271e954d3e00363dd6c7e4ce9dd659e4";
    const QDir root(args.at(4));
    SwapWalletRpcProcess owner;
    NativeFeePreviewController controller;
    NativeFeePreviewJob job(&controller);
    SwapWalletSession walletSession(&owner,args.at(2),genesis);
    if (args.size()==8 && !walletSession.pinHelperDigest(args.at(7))) return 2;
    QTimer poll;
    bool reported=false, requested=false;
    int phase=0;
    const QString fixtureWalletId=QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QByteArray fixturePassword=QUuid::createUuid().toByteArray(QUuid::WithoutBraces);
    QString fixtureAddress;
    const auto writeJson = [&](const QString &name,const QJsonObject &object) {
        QSaveFile file(root.filePath(name));
        const auto bytes=QJsonDocument(object).toJson(QJsonDocument::Compact);
        return file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size() && file.commit();
    };
    QObject::connect(&owner,&SwapWalletRpcProcess::stopped,&app,[&](bool clean) { app.exit(clean ? 0 : 2); });
    QObject::connect(&job,&NativeFeePreviewJob::completed,&app,[&](bool accepted) {
        if (!accepted || !writeJson("owned-fee-result.json",QJsonObject::fromVariantMap(controller.preview()))) owner.stop();
    });
    QObject::connect(&walletSession,&SwapWalletSession::operationCompleted,&app,[&](bool verified) {
        if (!verified) { owner.stop(); return; }
        bool written=false;
        if (phase==1 && walletSession.state()==SwapWalletSession::Open) {
            fixtureAddress=walletSession.address();
            written=writeJson("wallet-ready.json",{{"address",walletSession.address()},{"readyForTrading",false}});
        }
        if (phase==2 && walletSession.state()==SwapWalletSession::Open)
            written=writeJson("owned-fee-result.json",QJsonObject::fromVariantMap(walletSession.preview()));
        if (phase==3 && walletSession.state()==SwapWalletSession::Closed)
            written=writeJson("wallet-closed.json",{{"closed",true},{"readyForTrading",false}});
        if (phase==4 && walletSession.state()==SwapWalletSession::Open && walletSession.address()==fixtureAddress)
            written=writeJson("wallet-reopened.json",{{"address",walletSession.address()},{"readyForTrading",false}});
        if (phase==5 && walletSession.state()==SwapWalletSession::Closed)
            written=writeJson("wallet-closed-again.json",{{"closed",true},{"readyForTrading",false}});
        if (!written) owner.stop();
    });
    QObject::connect(&poll,&QTimer::timeout,&owner,[&] {
        if (QFileInfo::exists(root.filePath("service-stop"))) { owner.stop(); return; }
        if (!reported) {
            const auto session=owner.provisionHelperSession(genesis);
            if (session.isEmpty()) return;
            if (!writeJson("service-info.json",{{"directory",owner.dataDirectory()},
                {"login",owner.loginFile()},{"helperSession",session}})) { owner.stop(); return; }
            reported=true;
        }
        if (coordinated && phase==0 && QFileInfo::exists(root.filePath("wallet-create"))) {
            phase=1;
            if (!walletSession.create(fixtureWalletId,fixturePassword)) owner.stop();
            return;
        }
        if (coordinated && phase==2 && QFileInfo::exists(root.filePath("wallet-close"))) {
            phase=3;
            if (!walletSession.close()) owner.stop();
            return;
        }
        if (coordinated && phase==3 && QFileInfo::exists(root.filePath("wallet-reopen"))) {
            phase=4;
            if (!walletSession.open(fixtureWalletId,fixturePassword,fixtureAddress)) owner.stop();
            return;
        }
        if (coordinated && phase==4 && QFileInfo::exists(root.filePath("wallet-close-again"))) {
            phase=5;
            if (!walletSession.close()) owner.stop();
            return;
        }
        const QFileInfo info(root.filePath("owned-fee-request.json"));
        if (requested || !info.exists()) return;
        requested=true;
        QFile request(info.filePath());
        if (!info.isFile() || info.isSymLink() || info.size()>2048 || !request.open(QIODevice::ReadOnly)) { owner.stop(); return; }
        const auto object=QJsonDocument::fromJson(request.read(2049)).object();
        if (object.size()!=8 || object.value("schema")!="monzero-native-fee-request-v1" || object.value("genesis")!=genesis) { owner.stop(); return; }
        NativeFeePreviewReceiver::Expected expected{object.value("wallet").toString(),
            object.value("destination").toString(),object.value("amountAtoms").toString(),
            object.value("account").toString(),object.value("priority").toString(),genesis};
        if (coordinated) {
            if (phase!=1 || expected.wallet!=walletSession.address()) { owner.stop(); return; }
            phase=2;
            if (!walletSession.estimateFee(expected.destination,expected.amountAtoms,expected.account,expected.priority)) owner.stop();
        } else if (!job.startWithOwner(args.at(2),&owner,expected)) owner.stop();
    });
    if (!owner.startFresh(args.at(3),root.absolutePath(),daemon,wallet)) return 2;
    poll.start(50);
    QTimer::singleShot(180000,&owner,&SwapWalletRpcProcess::stop);
    return app.exec();
}

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if(app.arguments().value(1)=="--bch-light-engine-probe") {auto args=app.arguments();args.removeAt(1);return bchLightEngineProbe(args);}
    if (app.arguments().size()==5 && app.arguments()[1]=="--light-wallet-folders-fixture") {
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,app.arguments()[2]);
        DexEngine engine;if(!engine.configureLightWalletSetup(app.arguments()[3],app.arguments()[4]))return 2;
        QFile output;if(!output.open(stdout,QIODevice::WriteOnly))return 3;
        output.write(QJsonDocument::fromVariant(engine.lightWallets()).toJson(QJsonDocument::Compact));return 0;
    }
    if (app.arguments().value(1)=="--owned-fee-job-fixture"
        || app.arguments().value(1)=="--coordinated-fee-job-fixture") return ownedFeeJobFixture(app);
    if (app.arguments().value(1) == "--swap-wallet-process-fixture"
        || app.arguments().value(1) == "--swap-wallet-resume-fixture") {
        const auto args = app.arguments();
        const bool resume = args.value(1) == "--swap-wallet-resume-fixture";
        if (resume ? args.size()!=7 : (args.size()!=6 && args.size()!=7)) return 2;
        const QString controlHelper = !resume && args.size()==7 ? args.at(6) : QString();
        bool daemonOk = false, walletOk = false;
        const auto daemon = args.at(4).toUShort(&daemonOk), wallet = args.at(5).toUShort(&walletOk);
        if (!daemonOk || !walletOk) return 2;
        SwapWalletRpcProcess service;
        QTimer stopPoll;
        bool reported = false;
        bool controlStarted = false;
        int controlStep = 0;
        QString controlAddress;
        const QString controlId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        // Disposable fixture secret only; never supplied in arguments or metadata.
        const QByteArray controlPassword = QUuid::createUuid().toByteArray(QUuid::WithoutBraces);
        std::function<void()> nextControl;
        nextControl = [&] {
            if (controlStep==4) {
                QSaveFile done(QDir(args.at(3)).filePath("service-control-result.json"));
                const auto bytes = QJsonDocument(QJsonObject{{"schema","qt-wallet-control-fixture-v1"},
                    {"completed",4},{"address",controlAddress},{"readyForTrading",false}}).toJson(QJsonDocument::Compact);
                if (!done.open(QIODevice::WriteOnly) || done.write(bytes)!=bytes.size() || !done.commit()) service.stop();
                return;
            }
            const QString operation = QStringList{"create","close","open","close"}.at(controlStep);
            auto *job = new NativeWalletControlJob(&service,&service);
            QObject::connect(job,&NativeWalletControlJob::completed,&service,[&,job](bool verified) {
                if (!verified) { job->deleteLater(); service.stop(); return; }
                const auto result = job->result();
                if (controlStep==0) controlAddress = result.value("address").toString();
                if (result.value("address").toString()!=controlAddress || result.value("readyForTrading").toBool()) {
                    job->deleteLater(); service.stop(); return;
                }
                ++controlStep;
                job->deleteLater();
                QTimer::singleShot(0,&service,nextControl);
            });
            if (!job->start(controlHelper,service.provisionHelperSession(QString(64,'1')),operation,
                operation=="close" ? QString() : controlId,
                operation=="close" ? QByteArray() : controlPassword,
                controlStep==0 ? QString() : controlAddress)) {
                job->deleteLater(); service.stop();
            }
        };
        const auto report = [&] {
            if (reported) return;
            const auto session = service.provisionHelperSession(QString(64,QLatin1Char('1')));
            if (session.isEmpty()) return; // The child may not have written its login yet.
            QSaveFile info(QDir(args.at(3)).filePath("service-info.json"));
            const auto bytes = QJsonDocument(QJsonObject{{"directory",service.dataDirectory()},
                {"login",service.loginFile()},{"helperSession",session}}).toJson(QJsonDocument::Compact);
            if (!info.open(QIODevice::WriteOnly) || info.write(bytes) != bytes.size() || !info.commit()) {
                service.stop();
                app.exit(2);
            }
            reported = true;
        };
        QObject::connect(&service,&SwapWalletRpcProcess::processStarted,&app,report);
        QObject::connect(&service,&SwapWalletRpcProcess::stopped,&app,[&](bool clean) { app.exit(clean ? 0 : 2); });
        QObject::connect(&stopPoll,&QTimer::timeout,&service,[&] {
            if (QFileInfo::exists(QDir(args.at(3)).filePath("service-stop"))) service.stop();
            else report();
            if (reported && !controlStarted && !controlHelper.isEmpty()
                && QFileInfo::exists(QDir(args.at(3)).filePath("service-control-start"))) {
                controlStarted = true;
                nextControl();
            }
        });
        QTimer::singleShot(90000,&service,&SwapWalletRpcProcess::stop);
        stopPoll.start(100);
        if (resume ? !service.resumeClean(args.at(2),args.at(6),daemon,wallet)
            : !service.startFresh(args.at(2),args.at(3),daemon,wallet)) return 2;
        return app.exec();
    }
    if (app.arguments().value(1) == "--wallet-dir") {
        const auto args = app.arguments();
        if (args.size() != 14 || args.at(3) != "--daemon-address" || args.at(4) != "127.0.0.1:1"
            || args.at(5) != "--rpc-bind-ip" || args.at(6) != "127.0.0.1"
            || args.at(7) != "--rpc-bind-port" || !QStringList{"2","3","4","6"}.contains(args.at(8)) || args.at(9) != "--non-interactive"
            || args.at(10) != "--max-concurrency" || args.at(11) != "2" || args.at(12) != "--log-file"
            || args.at(2) != QDir::current().filePath("wallets")) return 2;
        QFile marker(QDir(args.at(2)).filePath("retained-marker"));
        const QString loginPath = QDir::current().filePath("monzero-wallet-rpc." + args.at(8) + ".login");
        if (args.at(8) == "4") {
            // Simulate a cleanup failure without changing filesystem-wide permissions.
            if (!QDir().mkdir(loginPath)) return 2;
            QFile retained(QDir(loginPath).filePath("retained"));
            if (!retained.open(QIODevice::WriteOnly) || !marker.open(QIODevice::WriteOnly)) return 2;
            retained.write("fixture");
            marker.write("disposable-wallet-marker");
            return 0;
        }
        QFile login(loginPath);
        if (!marker.open(QIODevice::WriteOnly) || !login.open(QIODevice::WriteOnly)) return 2;
        if (!login.setPermissions(QFile::ReadOwner)) return 2;
        if (marker.write("disposable-wallet-marker") < 0 || login.write("fixture:fixture") < 0) return 2;
        if (args.at(8) == "6") { marker.close(); login.close(); QTimer::singleShot(1500,&app,&QCoreApplication::quit); return app.exec(); }
        return args.at(8) == "3" ? 7 : 0;
    }
    if (app.arguments().value(1) == "--recovery-stdio-v1") {
        QFile input, output;
        if (!input.open(stdin,QIODevice::ReadOnly) || !output.open(stdout,QIODevice::WriteOnly)) return 2;
        const auto request=QJsonDocument::fromJson(input.read(2049)).object();
        if (request.value("minimumConfirmations")=="11") { QThread::msleep(5000); return 0; }
        QJsonObject reply{{"schema","monzero-native-recovery-v1"},{"requestId",request.value("requestId")},
            {"source","local-wallet-reconciliation"},{"status","confirmed"},{"retryAllowed",false},
            {"executable",false},{"confirmations","20"}};
        for (const auto field : {"genesis","wallet","lockTxid","txid"}) reply[field]=request.value(field);
        if (request.value("minimumConfirmations")=="13") reply["requestId"]="wrong-id";
        output.write(QJsonDocument(reply).toJson(QJsonDocument::Compact));
        return 0;
    }
    if (app.arguments().value(1) == "--wallet-control-stdio-v1") {
        QFile input, output;
        if (!input.open(stdin,QIODevice::ReadOnly) || !output.open(stdout,QIODevice::WriteOnly)) return 2;
        const auto request = QJsonDocument::fromJson(input.read(4097)).object();
        const auto mode = request.value("password").toString();
        if (mode == "timeout") { QThread::msleep(5000); return 0; }
        QJsonObject reply{{"schema","monzero-wallet-control-result-v1"},{"requestId",request.value("requestId")},
            {"operation",request.value("operation")},{"readyForTrading",false},
            {"address","FSf9UZS3uZwJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmJ7suhUXwdrDJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmCYGLw3H"}};
        if (mode == "wrong-id") reply["requestId"] = "obsolete";
        if (mode == "extra") reply["unexpected"] = true;
        output.write(QJsonDocument(reply).toJson(QJsonDocument::Compact));
        return 0;
    }
    if (app.arguments().value(1) == "--native-fee-job-fixture") return nativeFeeJobFixture(app);
    if (app.arguments().contains("--fee-preview-stdio-v1")) return feeTransportHelper();
    DexEngineTest test;
    return QTest::qExec(&test,argc,argv);
}
#include "test_dex.moc"
