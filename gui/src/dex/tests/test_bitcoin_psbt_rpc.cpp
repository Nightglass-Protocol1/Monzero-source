#include "../BitcoinPsbtRpcVerifier.h"
#include "../BitcoinLockPsbtRpcVerifier.h"
#include "../BitcoinSettlementTemplate.h"
#include "../OfferIdentity.h"
#include "../SettlementJournal.h"
#include "../SettlementTerms.h"
#include "../SignedQuoteAcceptor.h"

#include <QJsonArray>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
#include <functional>
#include <limits>
#include <memory>

namespace {
const QString bestBlock = QString(64, 'e');
const QString keyA = "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798";
const QString keyB = "02c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5";
const QString witnessScript = "21" + keyA + "ad21" + keyB + "ac";
const QString sharedScript = "0020" + QString::fromLatin1(QCryptographicHash::hash(
    QByteArray::fromHex(witnessScript.toLatin1()), QCryptographicHash::Sha256).toHex());
const QString fundingScript = "0014" + QString(40, 'b');

QJsonObject transaction()
{
    return {{"version", 2}, {"locktime", 0}, {"vsize", 200},
            {"vin", QJsonArray{QJsonObject{
                {"txid", QString(64, 'd')}, {"vout", 1},
                {"scriptSig", QJsonObject{{"asm", ""}, {"hex", ""}}},
                {"sequence", static_cast<double>(std::numeric_limits<quint32>::max())}}}},
            {"vout", QJsonArray{
                QJsonObject{{"value", 0.00010000}, {"n", 0},
                    {"scriptPubKey", QJsonObject{{"address", "shared"},
                        {"hex", sharedScript}, {"type", "witness_v0_scripthash"}}}},
                QJsonObject{{"value", 0.00002000}, {"n", 1},
                    {"scriptPubKey", QJsonObject{{"address", "change"},
                        {"hex", "0014" + QString(40, 'c')},
                        {"type", "witness_v0_keyhash"}}}}}}};
}

QJsonObject decodedPsbt()
{
    const QJsonObject witness{{"amount", 0.00012250},
        {"scriptPubKey", QJsonObject{{"hex", fundingScript},
            {"type", "witness_v0_keyhash"}}}};
    return {{"tx", transaction()}, {"psbt_version", 0},
            {"inputs", QJsonArray{QJsonObject{{"witness_utxo", witness},
                                               {"sighash", "ALL"}}}},
            {"outputs", QJsonArray{QJsonObject{}, QJsonObject{}}},
            {"fee", 0.00000250}};
}

QJsonObject txout()
{
    return {{"bestblock", bestBlock}, {"confirmations", 6},
            {"value", 0.00012250}, {"coinbase", false},
            {"scriptPubKey", QJsonObject{{"hex", fundingScript},
                {"type", "witness_v0_keyhash"}}}};
}

QByteArray psbtBytes()
{
    return QByteArray("psbt\xff\x01\x00", 7).toBase64();
}

SettlementTerms::Terms terms(const OfferIdentity &maker)
{
    SettlementTerms::Terms t;
    t.protocolId = "btc-xmz-adaptor-draft-v1";
    t.payAsset = "BTC"; t.receiveAsset = "XMZ";
    t.payGenesis = QString(64, '1');
    t.receiveGenesis = t.xmzGenesis = QString(64, '2');
    t.payNetwork = "btc-regtest";
    t.receiveNetwork = t.xmzNetwork = "xmz-regtest";
    t.payAmountAtomic = "10000"; t.receiveAmountAtomic = "1000";
    t.feeAtomic = "1"; t.payNativeFeeMaxAtomic = "300";
    t.receiveNativeFeeMaxAtomic = t.xmzNativeFeeMaxAtomic = "10";
    t.payRecipient = "pay"; t.receiveRecipient = "receive"; t.feeRecipient = "fee";
    t.payRefundRecipient = "pay-refund";
    t.receiveRefundRecipient = "receive-refund";
    t.xmzRefundRecipient = "fee-refund";
    t.payAnchorHeight = t.receiveAnchorHeight = t.xmzAnchorHeight = "100";
    t.payRefundHeight = "300"; t.receiveRefundHeight = "200";
    t.xmzRefundHeight = "210";
    t.payConfirmations = 6; t.receiveConfirmations = t.xmzConfirmations = 5;
    t.payLockCommitment = BitcoinSettlementTemplate::commitment(transaction());
    t.payRedeemCommitment = QByteArray(32, 'b');
    t.payCancelCommitment = QByteArray(32, 'c');
    t.payRefundCommitment = QByteArray(32, 'd');
    t.payPunishCommitment = QByteArray(32, 'e');
    t.receiveLockCommitment = QByteArray(32, 'f');
    t.receiveRefundCommitment = QByteArray(32, 'g');
    t.xmzFeeLockCommitment = QByteArray(32, 'h');
    t.xmzFeeRefundCommitment = QByteArray(32, 'i');
    t.payCancelBlocks = 72; t.payPunishBlocks = 72;
    t.payContractKeyA = QByteArray::fromHex(keyA.toLatin1());
    t.payContractKeyB = QByteArray::fromHex(keyB.toLatin1());
    t.makerAdaptorSecpKey = t.payContractKeyA;
    t.takerAdaptorSecpKey = t.payContractKeyB;
    t.makerSwapEd25519Key = QByteArray(32, 'm');
    t.takerSwapEd25519Key = QByteArray(32, 'n');
    t.makerDleqProofCommitment = QByteArray(32, 'o');
    t.takerDleqProofCommitment = QByteArray(32, 'p');
    t.makerKey = maker.publicKey(); t.takerKey = QByteArray(32, 't');
    t.payerKey = t.takerKey;
    t.nonce = QByteArray(32, 'u');
    t.offerFillReservationCommitment = QByteArray(32, 'v'); t.quoteExpiry = 200;
    t.refundDeadline = 400; t.feeFromAcquiredXmz = true;
    return t;
}
}

class BitcoinPsbtRpcTest : public QObject
{
    Q_OBJECT
private:
    QTcpServer server;
    QUrl endpoint() const
    {
        return QUrl(QString("http://127.0.0.1:%1/").arg(server.serverPort()));
    }
    void serve(const std::function<QJsonValue(const QJsonObject &)> &builder)
    {
        connect(&server, &QTcpServer::newConnection, &server, [this, builder] {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, builder] {
                QByteArray request = socket->property("requestData").toByteArray();
                request += socket->readAll();
                socket->setProperty("requestData", request);
                const int split = request.indexOf("\r\n\r\n");
                if (split < 0) return;
                const QRegularExpression lengthPattern("Content-Length: ([0-9]+)",
                    QRegularExpression::CaseInsensitiveOption);
                const auto match = lengthPattern.match(QString::fromLatin1(request.left(split)));
                if (!match.hasMatch() || request.size() - split - 4 < match.captured(1).toInt())
                    return;
                const QJsonObject sent = QJsonDocument::fromJson(request.mid(split + 4)).object();
                const QJsonObject response{{"jsonrpc", "2.0"}, {"id", sent.value("id")},
                                           {"result", builder(sent)}};
                const QByteArray body = QJsonDocument(response).toJson(QJsonDocument::Compact);
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                    + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
    }
    ReadOnlyRpcClient client() const
    {
        return ReadOnlyRpcClient(endpoint(),
            {"decodepsbt", "estimatesmartfee", "getbestblockhash",
             "getblockchaininfo", "getblockhash", "gettxout"});
    }
private slots:
    void init() { QVERIFY(server.listen(QHostAddress::LocalHost)); }
    void cleanup() { server.close(); server.disconnect(); }

    void validatesStableRpcSnapshot()
    {
        QCOMPARE(QString::fromLatin1(
                     BitcoinSettlementTemplate::commitment(transaction()).toHex()),
                 QString("84c03f0b4da834201c1e6425b540fd82a5651dc1f25c268aca1e887415e7705e"));
        serve([](const QJsonObject &request) -> QJsonValue {
            const QString method = request.value("method").toString();
            if (method == "getbestblockhash") return bestBlock;
            if (method == "getblockchaininfo")
                return QJsonObject{{"chain", "regtest"}, {"initialblockdownload", false},
                    {"blocks", 105}, {"headers", 105}, {"bestblockhash", bestBlock}};
            if (method == "getblockhash") return QString(64, '1');
            if (method == "decodepsbt") return decodedPsbt();
            if (method == "estimatesmartfee")
                return QJsonObject{{"feerate", 0.00001000}, {"blocks", 6}};
            if (method == "gettxout") return txout();
            return QJsonValue(QJsonValue::Null);
        });
        const QVariantMap result = BitcoinPsbtRpcVerifier::validateLock(
            client(), psbtBytes(), {}, BitcoinSettlementTemplate::commitment(transaction()),
            "shared", sharedScript, keyA, keyB, "10000", "300", 6);
        QVERIFY(result.value("valid").toBool());
        QVERIFY(result.value("rpcVerified").toBool());
        QVERIFY(result.value("feeEstimateVerified").toBool());
        QCOMPARE(result.value("networkPolicyCeilingAtomic").toString(), QString("400"));
        QCOMPARE(result.value("bestBlock").toString(), bestBlock);
        QVERIFY(!result.value("executable").toBool());

        OfferIdentity maker;
        const SettlementTerms::Terms signedTerms = terms(maker);
        const auto opaque = BitcoinLockPsbtRpcVerifier::validate(client(), signedTerms,
            BitcoinLockPsbtRpcVerifier::Leg::Pay, 100, psbtBytes(), {},
            "shared", sharedScript);
        QVERIFY2(opaque.accepted(),
                 qPrintable(opaque.details().value("error").toString()));
        QVERIFY(opaque.details().value("termsVerified").toBool());
        QVERIFY(opaque.details().value("nodeIdentityVerified").toBool());
        QVERIFY(!opaque.details().value("executable").toBool());
        auto changedTerms = signedTerms;
        changedTerms.payAmountAtomic = "9999";
        QVERIFY(!BitcoinLockPsbtRpcVerifier::validate(client(), changedTerms,
            BitcoinLockPsbtRpcVerifier::Leg::Pay, 100, psbtBytes(), {},
            "shared", sharedScript).accepted());
        changedTerms = signedTerms;
        changedTerms.payGenesis = QString(64, '9');
        QVERIFY(!BitcoinLockPsbtRpcVerifier::validate(client(), changedTerms,
            BitcoinLockPsbtRpcVerifier::Leg::Pay, 100, psbtBytes(), {},
            "shared", sharedScript).accepted());

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray domain("bitcoin-lock-test-domain");
        const auto built = SettlementTerms::build(signedTerms, 100);
        SignedQuoteAcceptor acceptor(directory.filePath("quotes.json"),
            signedTerms.protocolId, signedTerms.payNetwork,
            signedTerms.receiveNetwork, signedTerms.xmzNetwork, domain);
        const auto quote = acceptor.acceptEvidence(
            signedTerms, maker.sign(built.payload, domain), 100);
        QVERIFY(quote.accepted());
        SettlementJournal journal(directory.filePath("journal.json"));
        QVERIFY(!journal.appendBitcoinLockPrepared("swap-lock", opaque, 101));
        QVERIFY(journal.appendQuoteAccepted("swap-lock", quote, 100));
        QVERIFY(journal.appendBitcoinLockPrepared("swap-lock", opaque, 101));
        QVERIFY(journal.appendBitcoinLockPrepared("swap-lock", opaque, 101));
        QCOMPARE(journal.records().last().toObject().value("event").toString(),
                 QString("pay_lock_prepared"));
    }

    void rejectsTipChangeAndSpentInput()
    {
        auto tipCalls = std::make_shared<int>(0);
        serve([tipCalls](const QJsonObject &request) -> QJsonValue {
            const QString method = request.value("method").toString();
            if (method == "getbestblockhash")
                return (++*tipCalls == 1) ? bestBlock : QString(64, 'f');
            if (method == "decodepsbt") return decodedPsbt();
            if (method == "estimatesmartfee")
                return QJsonObject{{"feerate", 0.00001000}, {"blocks", 6}};
            return txout();
        });
        QVERIFY(!BitcoinPsbtRpcVerifier::validateLock(
            client(), psbtBytes(), {}, BitcoinSettlementTemplate::commitment(transaction()),
            "shared", sharedScript, keyA, keyB, "10000", "300", 6).value("valid").toBool());

        server.close(); server.disconnect(); QVERIFY(server.listen(QHostAddress::LocalHost));
        serve([](const QJsonObject &request) -> QJsonValue {
            if (request.value("method") == QJsonValue("getbestblockhash")) return bestBlock;
            if (request.value("method") == QJsonValue("decodepsbt")) return decodedPsbt();
            if (request.value("method") == QJsonValue("estimatesmartfee"))
                return QJsonObject{{"feerate", 0.00001000}, {"blocks", 6}};
            return QJsonValue(QJsonValue::Null);
        });
        QVERIFY(!BitcoinPsbtRpcVerifier::validateLock(
            client(), psbtBytes(), {}, BitcoinSettlementTemplate::commitment(transaction()),
            "shared", sharedScript, keyA, keyB, "10000", "300", 6).value("valid").toBool());
    }

    void rejectsMalformedPsbtBeforeRpc()
    {
        ReadOnlyRpcClient rpc = client();
        QVERIFY(!BitcoinPsbtRpcVerifier::validateLock(
            rpc, "not base64", {}, QByteArray(32, 1), "shared", sharedScript, keyA, keyB,
            "10000", "300", 6).value("valid").toBool());
    }

    void rejectsUnsafeFeeEstimate()
    {
        serve([](const QJsonObject &request) -> QJsonValue {
            const QString method = request.value("method").toString();
            if (method == "getbestblockhash") return bestBlock;
            if (method == "decodepsbt") return decodedPsbt();
            if (method == "gettxout") return txout();
            if (method == "estimatesmartfee")
                return QJsonObject{{"feerate", 0.00000100}, {"blocks", 6}};
            return QJsonValue(QJsonValue::Null);
        });
        const QVariantMap result = BitcoinPsbtRpcVerifier::validateLock(
            client(), psbtBytes(), {}, BitcoinSettlementTemplate::commitment(transaction()),
            "shared", sharedScript, keyA, keyB, "10000", "300", 6);
        QVERIFY(!result.value("valid").toBool());
        QVERIFY(!result.value("executable").toBool());
    }
};

QTEST_GUILESS_MAIN(BitcoinPsbtRpcTest)
#include "test_bitcoin_psbt_rpc.moc"
