#include "BchCancellationTestFixture.h"
#include "../BchNativeObservationProtocol.h"
class BchNativeObservationProtocolTests:public QObject {
    Q_OBJECT
    Context c;QJsonObject context,value,observed;QString request;
    bool valid() const {auto response=value;response["observation"]=observed;return BchNativeObservationProtocol::response(response,request,c.config,c.record,context,c.now,c.now+1);}
private slots:
    void init() {
        QVERIFY(c.create());request=QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto terms=c.record.value("terms").toObject();
        context={{"bchFundingOutpoint",QString(64,'a')+":0"},{"destination",QString(95,'B')},
            {"admission",QString(64,'1')},{"agreement",QString(64,'4')}};
        const QJsonObject details{{"schema","monzero-bch-native-proof-review-v1"},{"action","retain-native-payment-proof"},
            {"swapId",c.config.value("tradeId")},{"quote",c.details.value("quote")},{"agreement",pinBytes(0x44)},
            {"admission",pinBytes(0x11)},{"bchFundingOutpoint",context.value("bchFundingOutpoint")},
            {"genesis",terms.value("xmzGenesis")},{"destination",context.value("destination")},
            {"amountAtoms",terms.value("xmzAtoms")},{"txid",QString(64,'b')},{"message",pinBytes(2)},{"signature",pinBytes(3)}};
        value={{"schema","monzero-bch-native-observation-response-v1"},{"requestId",request},
            {"tradeId",c.config.value("tradeId")},{"executable",false},{"chainObserved",true},{"review",bound(details)}};
        observed={{"state","confirmed"},{"txid",details.value("txid")},{"destination",details.value("destination")},
            {"amountAtoms",details.value("amountAtoms")},{"genesis",terms.value("xmzGenesis")},{"tip",QString(64,'c')},
            {"tipHeight","10"},{"height","8"},{"confirmations","3"},{"observedAt",QString::number(c.now)}};
        QVERIFY(valid());
    }
    void rejectsAlteredEvidence_data() {
        QTest::addColumn<QString>("field");QTest::addColumn<QJsonValue>("replacement");
        QTest::newRow("txid")<<QString("txid")<<QJsonValue(QString(64,'d'));
        QTest::newRow("address")<<QString("destination")<<QJsonValue(QString(95,'C'));
        QTest::newRow("amount")<<QString("amountAtoms")<<QJsonValue("2");
        QTest::newRow("genesis")<<QString("genesis")<<QJsonValue(QString(64,'d'));
        QTest::newRow("tip")<<QString("tip")<<QJsonValue(QString(64,'0'));
        QTest::newRow("pool")<<QString("state")<<QJsonValue("mempool");
        QTest::newRow("unmined")<<QString("height")<<QJsonValue("0");
        QTest::newRow("above-tip")<<QString("height")<<QJsonValue("11");
        QTest::newRow("incorrect-depth")<<QString("confirmations")<<QJsonValue("2");
        QTest::newRow("numeric-height")<<QString("height")<<QJsonValue(8);
        QTest::newRow("overflow")<<QString("tipHeight")<<QJsonValue("18446744073709551616");
        QTest::newRow("extra")<<QString("tx_key")<<QJsonValue("secret");
    }
    void rejectsAlteredEvidence() {QFETCH(QString,field);QFETCH(QJsonValue,replacement);observed[field]=replacement;QVERIFY(!valid());}
    void rejectsOldAndFutureObservation() {
        observed["observedAt"]=QString::number(c.now-1);QVERIFY(!valid());
        observed["observedAt"]=QString::number(c.now+2);QVERIFY(!valid());
    }
    void rejectsEnvelopeChanges() {
        value["chainObserved"]=false;QVERIFY(!valid());value["chainObserved"]=true;
        value["executable"]=true;QVERIFY(!valid());value["executable"]=false;
        value["requestId"]=QUuid::createUuid().toString(QUuid::WithoutBraces);QVERIFY(!valid());
        value["requestId"]=request;value["privateKey"]="secret";QVERIFY(!valid());
    }
    void rejectsChangedReview() {
        auto review=value.value("review").toObject();review["commitment"]=QString(64,'0');value["review"]=review;QVERIFY(!valid());
    }
    void rejectsInsufficientDepth() {
        auto terms=c.record.value("terms").toObject();terms["xmzConfirmations"]="4";c.record["terms"]=terms;
        QByteArray bytes="monzero-native-bch-xmz-quote-v1";bytes.append('\0');bytes+=QJsonDocument(terms).toJson(QJsonDocument::Compact);
        const auto digest=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
        c.config["quoteCommitment"]=QString::fromLatin1(digest.toHex());c.record["commitment"]=c.config.value("quoteCommitment");
        auto details=value.value("review").toObject().value("details").toObject();QJsonArray quote;
        for (const auto byte:digest) quote.append(int(static_cast<unsigned char>(byte)));
        details["quote"]=quote;value["review"]=bound(details);
        observed["tipHeight"]="11";observed["confirmations"]="4";QVERIFY(valid());
        observed["tipHeight"]="10";observed["confirmations"]="3";QVERIFY(!valid());
    }
};
QTEST_GUILESS_MAIN(BchNativeObservationProtocolTests)
#include "test_bch_native_observation_protocol.moc"
