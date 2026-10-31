#include "BchCancellationTestFixture.h"
#include "../BchNativeProofProtocol.h"
class BchNativeProofProtocolTests:public QObject {
    Q_OBJECT
    Context c;QJsonObject details;
    QString outpoint=QString(64,'a')+":0",destination=QString(95,'B'),admission=QString(64,'1'),agreement=QString(64,'4');
    bool valid() const {return BchNativeProofProtocol::review(bound(details),c.config,c.record,outpoint,destination,admission,agreement);}
private slots:
    void init() {
        QVERIFY(c.create());const auto t=c.record.value("terms").toObject();
        details={{"schema","monzero-bch-native-proof-review-v1"},{"action","retain-native-payment-proof"},
            {"swapId",c.config.value("tradeId")},{"quote",c.details.value("quote")},
            {"agreement",pinBytes(0x44)},
            {"admission",pinBytes(0x11)},{"bchFundingOutpoint",outpoint},{"genesis",t.value("xmzGenesis")},
            {"destination",destination},{"amountAtoms",t.value("xmzAtoms")},{"txid",QString(64,'b')},
            {"message",pinBytes(2)},{"signature",pinBytes(3)}};QVERIFY(valid());
    }
    void rejectsReboundAlterations_data() {
        QTest::addColumn<QString>("field");QTest::addColumn<QJsonValue>("value");
        QTest::newRow("trade")<<QString("swapId")<<QJsonValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
        QTest::newRow("quote")<<QString("quote")<<QJsonValue(pinBytes(99));
        QTest::newRow("agreement")<<QString("agreement")<<QJsonValue(pinBytes(99));
        QTest::newRow("admission")<<QString("admission")<<QJsonValue(pinBytes(99));
        QTest::newRow("funding-output")<<QString("bchFundingOutpoint")<<QJsonValue(QString(64,'a')+":1");
        QTest::newRow("network")<<QString("genesis")<<QJsonValue(QString(64,'c'));
        QTest::newRow("destination")<<QString("destination")<<QJsonValue(QString(95,'C'));
        QTest::newRow("amount")<<QString("amountAtoms")<<QJsonValue("2");
        QTest::newRow("numeric-amount")<<QString("amountAtoms")<<QJsonValue(1);
        QTest::newRow("overflow")<<QString("amountAtoms")<<QJsonValue("18446744073709551616");
        QTest::newRow("empty-txid")<<QString("txid")<<QJsonValue(QString(64,'0'));
        QTest::newRow("message")<<QString("message")<<QJsonValue(QJsonArray{});
        QTest::newRow("signature")<<QString("signature")<<QJsonValue(QJsonArray{});
        QTest::newRow("private-key")<<QString("tx_key")<<QJsonValue("secret");
        QTest::newRow("confirmation")<<QString("confirmed")<<QJsonValue(true);
    }
    void rejectsReboundAlterations() {QFETCH(QString,field);QFETCH(QJsonValue,value);details[field]=value;QVERIFY(!valid());}
    void checkpointIsNotAgreement() {
        details["agreement"]=c.config.value("negotiationPins").toObject().value("agreement");
        QVERIFY(!valid());
    }
    void historicalReceiptAfterExpiry() {
        const auto expired=c.record.value("terms").toObject().value("expiresAt").toString().toULongLong();
        QJsonObject receipt{{"state","retained"},{"receivedAt",QString::number(expired+1)}};
        QVERIFY(BchNativeProofProtocol::receipt(receipt,c.config,expired+2));
        QVERIFY(!BchNativeProofProtocol::receipt(receipt,c.config,expired));
        receipt["receivedAt"]=QString::number(c.config.value("admittedAt").toVariant().toULongLong()-1);
        QVERIFY(!BchNativeProofProtocol::receipt(receipt,c.config,expired+2));
    }
    void responseEnvelope() {
        const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        QJsonObject value{{"schema","monzero-bch-native-proof-status-response-v1"},{"requestId",id},
            {"tradeId",c.config.value("tradeId")},{"executable",false},{"chainObserved",false},
            {"review",QJsonValue::Null},{"receipt",QJsonValue::Null}};
        const auto check=[&](const QString &operation) {return BchNativeProofProtocol::response(value,operation,id,
            c.config,c.record,outpoint,destination,admission,agreement,c.now);};
        QVERIFY(check("status"));
        value["review"]=bound(details);QVERIFY(!check("status"));
        value["receipt"]=QJsonObject{{"state","retained"},{"receivedAt",QString::number(c.now)}};
        QVERIFY(check("status"));
        value["schema"]="monzero-bch-native-proof-submit-response-v1";QVERIFY(check("submit"));
        value["receipt"]=QJsonValue::Null;QVERIFY(!check("submit"));
        value["schema"]="monzero-bch-native-proof-review-response-v1";QVERIFY(check("review"));
        value["chainObserved"]=true;QVERIFY(!check("review"));value["chainObserved"]=false;
        value["executable"]="false";QVERIFY(!check("review"));value["executable"]=false;
        value["requestId"]=QUuid::createUuid().toString(QUuid::WithoutBraces);QVERIFY(!check("review"));
        value["requestId"]=id;value["tx_key"]="secret";QVERIFY(!check("review"));
    }
    void rejectsReceiptClaims_data() {
        QTest::addColumn<QString>("field");QTest::addColumn<QJsonValue>("value");
        QTest::newRow("confirmed")<<QString("state")<<QJsonValue("confirmed");
        QTest::newRow("acknowledged")<<QString("state")<<QJsonValue("acknowledged");
        QTest::newRow("numeric-time")<<QString("receivedAt")<<QJsonValue(1);
        QTest::newRow("overflow-time")<<QString("receivedAt")<<QJsonValue("18446744073709551616");
        QTest::newRow("extra-field")<<QString("confirmed")<<QJsonValue(true);
    }
    void rejectsReceiptClaims() {
        QFETCH(QString,field);QFETCH(QJsonValue,value);
        QJsonObject receipt{{"state","retained"},{"receivedAt",QString::number(c.now)}};
        receipt[field]=value;QVERIFY(!BchNativeProofProtocol::receipt(receipt,c.config,c.now));
    }
    void wrongRole() {c.config["role"]="native-funder";QVERIFY(!valid());}
    void alteredCommitment() {
        auto value=bound(details);value["commitment"]=QString(64,'0');
        QVERIFY(!BchNativeProofProtocol::review(value,c.config,c.record,outpoint,destination,admission,agreement));
    }
    void missingField() {details.remove("signature");QVERIFY(!valid());}
};
QTEST_GUILESS_MAIN(BchNativeProofProtocolTests)
#include "test_bch_native_proof_protocol.moc"
