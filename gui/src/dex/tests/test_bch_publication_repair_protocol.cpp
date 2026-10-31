#include "BchCancellationTestFixture.h"
#include "../BchPublicationRepairProtocol.h"
class BchPublicationRepairProtocolTests:public QObject {
    Q_OBJECT
    Context c;QJsonObject details,source;
    bool valid() const {return BchPublicationRepairProtocol::review(bound(details),c.config,c.record,source,"prepared",c.now);}
private slots:
    void init() {
        QVERIFY(c.create());source={{"deploymentCommitment",c.deployment},{"fundingSource","local-wallet"}};
        details={{"schema","monzero-bch-success-publication-repair-v1"},{"action","repair-private-success-publication"},
            {"swapId",c.config.value("tradeId")},{"quote",c.details.value("quote")},{"admission",pinBytes(1)},
            {"nativeReceipt",pinBytes(2)},{"candidate",pinBytes(3)},{"recordedAt",c.config.value("admittedAt")},
            {"bchDeployment",pinBytes(0x77)},{"fundingSource","local-wallet"},{"stage","prepared"},
            {"releasesPrivateMaterial",false},{"chainApproval",false}};
        QVERIFY(valid());
    }
    void rejectsReboundChanges_data() {
        QTest::addColumn<QString>("field");QTest::addColumn<QJsonValue>("value");
        const auto row=[](const char *field,const QJsonValue &value) {QTest::newRow(field)<<QString(field)<<value;};
        row("schema","wrong");row("action","release-signed-bch-success");row("swapId",QUuid::createUuid().toString(QUuid::WithoutBraces));
        row("quote",pinBytes(99));row("bchDeployment",pinBytes(99));row("fundingSource","node-wallet");row("stage","signed");
        row("admission",QJsonArray{});row("nativeReceipt",QJsonArray{});row("candidate",QJsonArray{});
        row("recordedAt",0);row("frame","secret");row("signature","secret");row("releasesPrivateMaterial",true);row("chainApproval",true);
    }
    void rejectsReboundChanges() {QFETCH(QString,field);QFETCH(QJsonValue,value);details[field]=value;QVERIFY(!valid());}
    void sourceStageTimeAndCommitmentAreBound() {
        using namespace BchPublicationRepairProtocol;
        for (const auto funding:{"node-wallet","local-wallet"}) {for (const auto current:{"prepared","signed"}) {
            source["fundingSource"]=funding;details["fundingSource"]=funding;details["stage"]=current;
            QVERIFY(review(bound(details),c.config,c.record,source,current,c.now));
            QVERIFY(!review(bound(details),c.config,c.record,source,"all",c.now));
        }}
        details["stage"]="prepared";source["fundingSource"]="local-wallet";details["fundingSource"]="local-wallet";
        details["recordedAt"]=double(c.now+1);QVERIFY(!valid());
        details["recordedAt"]=c.config.value("admittedAt").toDouble()-1;QVERIFY(!valid());
        details["recordedAt"]=c.config.value("admittedAt").toDouble()+0.5;QVERIFY(!valid());
        details["recordedAt"]=c.config.value("admittedAt");details["chainApproval"]=0;QVERIFY(!valid());details["chainApproval"]=false;
        auto wrong=bound(details);wrong["commitment"]=QString(64,'0');QVERIFY(!review(wrong,c.config,c.record,source,"prepared",c.now));
    }
    void responsesRequireExactRequestAndSubmitConsent() {
        using namespace BchPublicationRepairProtocol;
        const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces),approval=bound(details).value("commitment").toString();
        for (bool submit:{false,true}) {
            QJsonObject value{{"schema",QString("monzero-bch-success-publication-repair-%1-response-v1").arg(submit?"submit":"review")},
                {"requestId",id},{"tradeId",c.config.value("tradeId")},{"state",submit?"repaired":"reviewed"},
                {"executable",false},{"chainObserved",false},{"releasesPrivateMaterial",false},{"review",bound(details)}};
            const auto consent=submit?approval:QString();
            QVERIFY(response(value,id,c.config,c.record,source,"prepared",c.now,submit,consent));
            QVERIFY(!response(value,id,c.config,c.record,source,"prepared",c.now,submit,submit?QString(64,'0'):approval));
            QVERIFY(!response(value,QUuid::createUuid().toString(QUuid::WithoutBraces),c.config,c.record,source,"prepared",c.now,submit,consent));
            for (const auto key:{"executable","chainObserved","releasesPrivateMaterial"}) {
                auto invalid=value;invalid[key]=true;QVERIFY(!response(invalid,id,c.config,c.record,source,"prepared",c.now,submit,consent));
            }
            auto invalid=value;invalid["frame"]="secret";QVERIFY(!response(invalid,id,c.config,c.record,source,"prepared",c.now,submit,consent));
        }
    }
};
QTEST_GUILESS_MAIN(BchPublicationRepairProtocolTests)
#include "test_bch_publication_repair_protocol.moc"
