#include "../BitcoinRecoveryPsbtEvidence.h"
#include "../BitcoinRecoveryPsbtRpcVerifier.h"
#include "../BitcoinRawTransaction.h"
#include "../BitcoinRecoveryMempoolVerifier.h"
#include "../BitcoinRecoveryMaturityVerifier.h"
#include "../BitcoinRecoveryChainObserver.h"
#include "../BitcoinRecoveryConfirmationTracker.h"
#include "../BitcoinSignedRecoveryEvidence.h"
#include "../BitcoinSettlementTemplate.h"
#include "../CryptoVerifierProtocol.h"
#include "../ConfirmationTracker.h"
#include "../MonzeroSuccessTransactionEvidence.h"
#include "../MonzeroRefundTransactionEvidence.h"
#include "../OfferIdentity.h"
#include "../OfferBookStore.h"
#include "../OfferFillReservationStore.h"
#include "../SignedOffer.h"
#include "../SettlementJournal.h"
#include "../SignedQuoteAcceptor.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <functional>
#include <memory>

namespace {
const QString lockTxid(64, 'b'), cancelTxid(64, 'c'), fundingTxid(64, 'a');
const QString bestBlock(64, 'e');
const QString keyA = "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798";
const QString keyB = "02c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5";
const QString witnessScript = "21" + keyA + "ad21" + keyB + "ac";
const QString sharedScript = "0020" + QString::fromLatin1(QCryptographicHash::hash(
    QByteArray::fromHex(witnessScript.toLatin1()), QCryptographicHash::Sha256).toHex());
const QString destinationScript = "0014" + QString(40, 'd');
const QString cancelSignatureA = "3045022100a16f5d6a8afc79f374bd12cdccb8565bfc06f5f9d75bd03cb28f478d6f2cceb40220748b1d653dea650042683181b4b23ad1de6036dd52ca05784de42ae34dffa61d01";
const QString cancelSignatureB = "3045022100ba5d874f4c1fd32a21208d687e11cebc2d6a92c4e3a70c23b97e788ef92e9d5d02202e43eab6ab4e6c386617bc0c16db9e87e7db5709f1118df206016f18e94eefe001";
const QString redeemSignatureA = "3044022014b65f93e6119b9c998cc86238127a2676d1450b3b75090e1779d985c132c798022019d105a85605fed16e6d3ea8348256bfc94468116ecc0e94b4c63dc7bbada48e01";
const QString redeemSignatureB = "3045022100b0cb26054f42f16bbb7103c0f5fcd96a544652c751f8913a93ca5921ad14703602204d5783569063d6804665799dc01a57cec86d271b8442c20f6b894237e032895d01";
const QString refundSignatureA = "3045022100f67f7a5e2567cbd8b48ef0215eff4a6d2cd72d9a632de2dce7ca1d155767617302201a26aac389dead4b8badb2d5d6cc5e136ddec977702f471824c6cc03e9ab24e401";
const QString refundSignatureB = "30450221008de36cb2d562dcf57266c85ebf2087d5820d27a05ace3c3e2b1078358b3a794b02201d7d37452df2e5fe823163b8a0fb1b13b95f1339899ae31e57d3e234d4fad0da01";
const QString signedCancelTxid = "465f4784ee97e1105f445f94fadff7925cb05b75adf37849e97a33bac24c2a0f";
const QString signedCancelWtxid = "13fa4f81aacc796abc7d0b93ef04d9f4c16b88e32a71f8cacef26bf7319cae24";
const QString recoveryBlockHash(64, '9');
QByteArray psbtBytes() { return QByteArray("psbt\xff\x01\x00", 7).toBase64(); }

QByteArray little(quint64 value, int bytes)
{
    QByteArray result;
    for (int i = 0; i < bytes; ++i) {
        result.append(static_cast<char>(value & 0xff));
        value >>= 8;
    }
    return result;
}

QByteArray signedCancelBytes(quint64 amount = 9700,
                             const QStringList &items = {
                                 cancelSignatureA, cancelSignatureB, witnessScript})
{
    QByteArray previous = QByteArray::fromHex(lockTxid.toLatin1());
    std::reverse(previous.begin(), previous.end());
    QByteArray raw = QByteArray::fromHex("02000000000101") + previous
        + little(0, 4) + QByteArray(1, '\0') + little(72, 4)
        + QByteArray(1, '\1') + little(amount, 8)
        + QByteArray(1, static_cast<char>(sharedScript.size() / 2))
        + QByteArray::fromHex(sharedScript.toLatin1())
        + QByteArray(1, static_cast<char>(items.size()));
    for (const QString &item : items) {
        const QByteArray bytes = QByteArray::fromHex(item.toLatin1());
        raw += QByteArray(1, static_cast<char>(bytes.size())) + bytes;
    }
    raw += little(0, 4);
    return raw.toHex();
}

QByteArray signedRedeemBytes(const QString &signatureA, const QString &signatureB)
{
    QByteArray previous = QByteArray::fromHex(lockTxid.toLatin1());
    std::reverse(previous.begin(), previous.end());
    const QStringList items{signatureA, signatureB, witnessScript};
    QByteArray raw = QByteArray::fromHex("02000000000101") + previous
        + little(0, 4) + QByteArray(1, '\0') + little(0xffffffffU, 4)
        + QByteArray(1, '\1') + little(9700, 8)
        + QByteArray(1, static_cast<char>(destinationScript.size() / 2))
        + QByteArray::fromHex(destinationScript.toLatin1())
        + QByteArray(1, static_cast<char>(items.size()));
    for (const QString &item : items) {
        const QByteArray bytes = QByteArray::fromHex(item.toLatin1());
        raw += QByteArray(1, static_cast<char>(bytes.size())) + bytes;
    }
    raw += little(0, 4);
    return raw.toHex();
}

QByteArray signedRefundBytes()
{
    QByteArray previous = QByteArray::fromHex(cancelTxid.toLatin1());
    std::reverse(previous.begin(), previous.end());
    const QStringList items{refundSignatureA, refundSignatureB, witnessScript};
    QByteArray raw = QByteArray::fromHex("02000000000101") + previous
        + little(0, 4) + QByteArray(1, '\0') + little(0xffffffffU, 4)
        + QByteArray(1, '\1') + little(9400, 8)
        + QByteArray(1, static_cast<char>(destinationScript.size() / 2))
        + QByteArray::fromHex(destinationScript.toLatin1())
        + QByteArray(1, static_cast<char>(items.size()));
    for (const QString &item : items) {
        const QByteArray bytes = QByteArray::fromHex(item.toLatin1());
        raw += QByteArray(1, static_cast<char>(bytes.size())) + bytes;
    }
    raw += little(0, 4);
    return raw.toHex();
}

QJsonObject transaction(const QString &parent, quint32 sequence, double amount,
                        const QString &address, const QString &script,
                        const QString &txid = {})
{
    QJsonObject tx{{"version", 2}, {"locktime", 0},
        {"vin", QJsonArray{QJsonObject{{"txid", parent}, {"vout", 0},
            {"scriptSig", QJsonObject{{"asm", ""}, {"hex", ""}}},
            {"sequence", static_cast<double>(sequence)}}}},
        {"vout", QJsonArray{QJsonObject{{"value", amount}, {"n", 0},
            {"scriptPubKey", QJsonObject{{"address", address}, {"hex", script},
                {"type", script.startsWith("0020") ? "witness_v0_scripthash"
                                                     : "witness_v0_keyhash"}}}}}}};
    if (!txid.isEmpty()) tx["txid"] = txid;
    return tx;
}

QJsonObject psbt(const QJsonObject &tx, double parentAmount, double fee)
{
    return {{"tx", tx}, {"psbt_version", 0},
        {"inputs", QJsonArray{QJsonObject{{"witness_utxo", QJsonObject{
            {"amount", parentAmount}, {"scriptPubKey", QJsonObject{
                {"hex", sharedScript}, {"type", "witness_v0_scripthash"}}}}},
            {"sighash", "ALL"}}}},
        {"outputs", QJsonArray{QJsonObject{}}}, {"fee", fee}};
}

QJsonObject prevout(const QString &txid, double amount)
{
    return {{"txid", txid}, {"vout", 0}, {"bestblock", bestBlock},
        {"confirmations", 100}, {"value", amount}, {"coinbase", false},
        {"scriptPubKey", QJsonObject{{"hex", sharedScript},
            {"type", "witness_v0_scripthash"}}}};
}

struct Fixture {
    QJsonObject lock = transaction(fundingTxid, 0xffffffffU, 0.00010000,
        "shared", sharedScript, lockTxid);
    QJsonObject redeem = transaction(lockTxid, 0xffffffffU, 0.00009700,
        "destination", destinationScript);
    QJsonObject cancel = transaction(lockTxid, 72, 0.00009700,
        "shared", sharedScript, cancelTxid);
    QJsonObject refund = transaction(cancelTxid, 0xffffffffU, 0.00009400,
        "destination", destinationScript);
    QJsonObject punish = transaction(cancelTxid, 72, 0.00009400,
        "destination", destinationScript);
    SettlementTerms::Terms terms;

    Fixture()
    {
        auto &t = terms;
        t.protocolId = "btc-xmz-adaptor-draft-v1";
        t.payAsset = "BTC"; t.receiveAsset = "XMZ";
        t.payGenesis = QString(64, '1'); t.receiveGenesis = t.xmzGenesis = QString(64, '2');
        t.payNetwork = "btc-regtest"; t.receiveNetwork = t.xmzNetwork = "xmz-regtest";
        t.payAmountAtomic = "10000"; t.receiveAmountAtomic = "1000"; t.feeAtomic = "1";
        t.payNativeFeeMaxAtomic = "500"; t.receiveNativeFeeMaxAtomic = "10";
        t.xmzNativeFeeMaxAtomic = "10";
        t.payRecipient = "pay"; t.receiveRecipient = "receive"; t.feeRecipient = "fee";
        t.payRefundRecipient = "pay-refund"; t.receiveRefundRecipient = "receive-refund";
        t.xmzRefundRecipient = "fee-refund";
        t.payAnchorHeight = "100"; t.payRefundHeight = "300";
        t.receiveAnchorHeight = t.xmzAnchorHeight = "100";
        t.receiveRefundHeight = "200"; t.xmzRefundHeight = "210";
        t.payConfirmations = 6; t.receiveConfirmations = t.xmzConfirmations = 5;
        t.payLockCommitment = BitcoinSettlementTemplate::commitment(lock);
        t.payRedeemCommitment = BitcoinSettlementTemplate::commitment(redeem);
        t.payCancelCommitment = BitcoinSettlementTemplate::commitment(cancel);
        t.payRefundCommitment = BitcoinSettlementTemplate::commitment(refund);
        t.payPunishCommitment = BitcoinSettlementTemplate::commitment(punish);
        t.receiveLockCommitment = QByteArray(32, 'l'); t.receiveRefundCommitment = QByteArray(32, 'r');
        t.xmzFeeLockCommitment = QByteArray(32, 'f'); t.xmzFeeRefundCommitment = QByteArray(32, 'g');
        t.payCancelBlocks = 72; t.payPunishBlocks = 72;
        t.payContractKeyA = QByteArray::fromHex(keyA.toLatin1());
        t.payContractKeyB = QByteArray::fromHex(keyB.toLatin1());
        t.makerAdaptorSecpKey = t.payContractKeyA; t.takerAdaptorSecpKey = t.payContractKeyB;
        t.makerSwapEd25519Key = QByteArray(32, 'm'); t.takerSwapEd25519Key = QByteArray(32, 'n');
        t.makerDleqProofCommitment = QByteArray(32, 'o');
        t.takerDleqProofCommitment = QByteArray(32, 'p');
        t.makerKey = QByteArray(32, 'q'); t.takerKey = QByteArray(32, 's');
        t.payerKey = t.takerKey;
        t.nonce = QByteArray(32, 't');
        t.offerFillReservationCommitment = QByteArray(32, 'u');
        t.quoteExpiry = 200; t.refundDeadline = 400;
        t.feeFromAcquiredXmz = true;
    }
};

QVariantMap validate(Fixture &f, BitcoinRecoveryPsbtEvidence::Branch branch,
                     qint64 now = 100)
{
    const bool fromCancel = branch == BitcoinRecoveryPsbtEvidence::Branch::Refund
        || branch == BitcoinRecoveryPsbtEvidence::Branch::Punish;
    const QJsonObject &parent = fromCancel ? f.cancel : f.lock;
    const QJsonObject &child = branch == BitcoinRecoveryPsbtEvidence::Branch::Redeem ? f.redeem
        : branch == BitcoinRecoveryPsbtEvidence::Branch::Cancel ? f.cancel
        : branch == BitcoinRecoveryPsbtEvidence::Branch::Refund ? f.refund : f.punish;
    const QString txid = fromCancel ? cancelTxid : lockTxid;
    const double parentAmount = fromCancel ? 0.00009700 : 0.00010000;
    return BitcoinRecoveryPsbtEvidence::validate(f.terms,
        BitcoinRecoveryPsbtEvidence::Leg::Pay, branch, now,
        psbt(child, parentAmount, 0.00000300), parent, prevout(txid, parentAmount),
        txid, "0", fromCancel ? "9700" : "10000", sharedScript, keyA, keyB,
        branch == BitcoinRecoveryPsbtEvidence::Branch::Cancel ? "shared" : "destination",
        branch == BitcoinRecoveryPsbtEvidence::Branch::Cancel ? sharedScript : destinationScript,
        bestBlock);
}
}

class BitcoinRecoveryPsbtEvidenceTest : public QObject
{
    Q_OBJECT
    QTcpServer server;
    bool invalidNodeIdentity = false;
    int bitcoinNodeHeight = 105;

    void serve(const std::function<QJsonValue(const QJsonObject &)> &builder)
    {
        connect(&server, &QTcpServer::newConnection, &server, [this, builder] {
            QTcpSocket *socket = server.nextPendingConnection();
            const bool invalidIdentity = invalidNodeIdentity;
            const int nodeHeight = bitcoinNodeHeight;
            connect(socket, &QTcpSocket::readyRead, socket,
                    [socket, builder, invalidIdentity, nodeHeight] {
                QByteArray request = socket->property("requestData").toByteArray();
                request += socket->readAll(); socket->setProperty("requestData", request);
                const int split = request.indexOf("\r\n\r\n");
                if (split < 0) return;
                const auto match = QRegularExpression("Content-Length: ([0-9]+)",
                    QRegularExpression::CaseInsensitiveOption)
                    .match(QString::fromLatin1(request.left(split)));
                if (!match.hasMatch() || request.size() - split - 4 < match.captured(1).toInt())
                    return;
                const QJsonObject sent = QJsonDocument::fromJson(request.mid(split + 4)).object();
                const QString method = sent.value("method").toString();
                QJsonValue result;
                if (method == "getblockchaininfo")
                    result = QJsonObject{{"chain", invalidIdentity ? "main" : "regtest"},
                        {"initialblockdownload", false}, {"blocks", nodeHeight},
                        {"headers", nodeHeight},
                        {"bestblockhash", bestBlock}};
                else if (method == "getblockhash") result = QString(64, '1');
                else result = builder(sent);
                QJsonObject response{{"jsonrpc", "2.0"}, {"id", sent.value("id")}};
                if (result.isObject()
                    && result.toObject().contains("__testRpcErrorCode")) {
                    response["error"] = QJsonObject{
                        {"code", result.toObject().value("__testRpcErrorCode")},
                        {"message", "No such mempool or blockchain transaction"}};
                } else response["result"] = result;
                const QByteArray body = QJsonDocument(response).toJson(QJsonDocument::Compact);
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                    + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
    }

    ReadOnlyRpcClient client() const
    {
        return ReadOnlyRpcClient(QUrl(QString("http://127.0.0.1:%1/").arg(server.serverPort())),
            {"decodepsbt", "estimatesmartfee", "getbestblockhash",
             "getblockchaininfo", "getblockhash", "getrawtransaction", "gettxout",
             "getblockheader", "testmempoolaccept"});
    }

    BitcoinSignedRecoveryEvidence::Evidence signedEvidence(Fixture &fixture) const
    {
        const auto prepared = BitcoinRecoveryPsbtRpcVerifier::validateEvidence(client(),
            fixture.terms, BitcoinRecoveryPsbtEvidence::Leg::Pay,
            BitcoinRecoveryPsbtEvidence::Branch::Cancel, 100, psbtBytes(), {},
            lockTxid, "0", "10000", sharedScript, keyA, keyB,
            "shared", sharedScript);
        return BitcoinSignedRecoveryEvidence::validate(prepared, signedCancelBytes());
    }

private slots:
    void init() { QVERIFY(server.listen(QHostAddress::LocalHost)); }
    void cleanup() { server.close(); server.disconnect(); invalidNodeIdentity = false;
                     bitcoinNodeHeight = 105; }
    void rejectsLegacyUnboundTrackerStore()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("legacy-confirmations.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("{\"version\":1,\"records\":[]}"), qint64(26));
        file.close();
        BitcoinRecoveryConfirmationTracker::Tracker tracker(path);
        QVERIFY(!tracker.ready());
        QCOMPARE(tracker.error(), QString("Invalid recovery confirmation store"));
    }
    void validatesAllFourSignedBranches()
    {
        Fixture fixture;
        for (const auto branch : {BitcoinRecoveryPsbtEvidence::Branch::Redeem,
                 BitcoinRecoveryPsbtEvidence::Branch::Cancel,
                 BitcoinRecoveryPsbtEvidence::Branch::Refund,
                 BitcoinRecoveryPsbtEvidence::Branch::Punish}) {
            const QVariantMap result = validate(fixture, branch);
            QVERIFY2(result.value("valid").toBool(), qPrintable(result.value("error").toString()));
            QVERIFY(result.value("branchPolicyVerified").toBool());
            QVERIFY(!result.value("executable").toBool());
        }
        QVERIFY(validate(fixture, BitcoinRecoveryPsbtEvidence::Branch::Redeem, 1000)
                    .value("valid").toBool());
    }
    void rejectsBranchParentSequenceAndCommitmentSubstitution()
    {
        Fixture fixture;
        fixture.cancel["txid"] = lockTxid;
        QVERIFY(!validate(fixture, BitcoinRecoveryPsbtEvidence::Branch::Refund)
                     .value("valid").toBool());
        fixture = Fixture{};
        fixture.terms.payPunishCommitment = fixture.terms.payRefundCommitment;
        QVERIFY(!validate(fixture, BitcoinRecoveryPsbtEvidence::Branch::Punish)
                     .value("valid").toBool());
        fixture = Fixture{};
        QJsonArray inputs = fixture.punish.value("vin").toArray();
        QJsonObject input = inputs[0].toObject(); input["sequence"] = 71;
        inputs[0] = input; fixture.punish["vin"] = inputs;
        QVERIFY(!validate(fixture, BitcoinRecoveryPsbtEvidence::Branch::Punish)
                     .value("valid").toBool());
    }
    void rejectsXmzAsUtxoLeg()
    {
        Fixture fixture;
        QVERIFY(!BitcoinRecoveryPsbtEvidence::validate(fixture.terms,
            BitcoinRecoveryPsbtEvidence::Leg::Receive,
            BitcoinRecoveryPsbtEvidence::Branch::Refund, 100, {}, {}, {},
            lockTxid, "0", "10000", sharedScript, keyA, keyB,
            "destination", destinationScript, bestBlock).value("valid").toBool());
    }
    void rejectsUnknownRecoverySelectors()
    {
        Fixture fixture;
        using Leg = BitcoinRecoveryPsbtEvidence::Leg;
        using Branch = BitcoinRecoveryPsbtEvidence::Branch;
        for (int value : {-1, 99}) {
            for (bool unknownLeg : {false, true}) {
                const auto result = BitcoinRecoveryPsbtEvidence::validate(fixture.terms,
                    unknownLeg ? static_cast<Leg>(value) : Leg::Pay,
                    unknownLeg ? Branch::Cancel : static_cast<Branch>(value), 100,
                    psbt(fixture.cancel, 0.00010000, 0.00000300), fixture.lock,
                    prevout(lockTxid, 0.00010000), lockTxid, "0", "10000",
                    sharedScript, keyA, keyB, "shared", sharedScript, bestBlock);
                QVERIFY(!result.value("valid").toBool());
                QVERIFY(!result.value("branchPolicyVerified").toBool());
                QVERIFY(!result.value("executable").toBool());
                QCOMPARE(result.value("error").toString(),
                         QString(unknownLeg ? "Unknown recovery leg" : "Unknown recovery branch"));
            }
        }
    }
    void rejectsContractKeySubstitutionAgainstSignedTerms()
    {
        Fixture fixture;
        QVERIFY(!BitcoinRecoveryPsbtEvidence::validate(fixture.terms,
            BitcoinRecoveryPsbtEvidence::Leg::Pay,
            BitcoinRecoveryPsbtEvidence::Branch::Cancel, 100,
            psbt(fixture.cancel, 0.00010000, 0.00000300), fixture.lock,
            prevout(lockTxid, 0.00010000), lockTxid, "0", "10000",
            sharedScript, keyB, keyA, "shared", sharedScript, bestBlock)
            .value("valid").toBool());
    }
    void validatesStableRpcCancelSnapshot()
    {
        Fixture fixture;
        OfferIdentity maker, taker;
        fixture.terms.makerKey = maker.publicKey();
        fixture.terms.takerKey = taker.publicKey();
        fixture.terms.payerKey = fixture.terms.takerKey;
        auto invalidConfirmation = std::make_shared<bool>(false);
        auto replacementBlock = std::make_shared<bool>(false);
        auto missingTransaction = std::make_shared<bool>(false);
        auto parentConfirmations = std::make_shared<int>(100);
        serve([fixture, invalidConfirmation, replacementBlock,
               missingTransaction, parentConfirmations](
                  const QJsonObject &request) mutable -> QJsonValue {
            const QString method = request.value("method").toString();
            if (method == "getbestblockhash") return bestBlock;
            if (method == "decodepsbt") {
                QJsonObject child = fixture.cancel; child["vsize"] = 150;
                return psbt(child, 0.00010000, 0.00000300);
            }
            if (method == "getrawtransaction") {
                const QJsonArray params = request.value("params").toArray();
                if (params.size() >= 2 && params.at(0) == QJsonValue(signedCancelTxid)) {
                    if (*missingTransaction)
                        return QJsonObject{{"__testRpcErrorCode", -5}};
                    if (params.at(1) == QJsonValue(false))
                        return QString::fromLatin1(signedCancelBytes());
                    const QString blockHash = *replacementBlock
                        ? QString(64, '8') : recoveryBlockHash;
                    return QJsonObject{{"txid", signedCancelTxid},
                        {"hash", signedCancelWtxid}, {"blockhash", blockHash},
                        {"confirmations", 6}, {"in_active_chain", true}};
                }
                return fixture.lock;
            }
            if (method == "gettxout") {
                QJsonObject output = prevout(lockTxid, 0.00010000);
                output["confirmations"] = *parentConfirmations;
                return output;
            }
            if (method == "getblockheader")
                return QJsonObject{{"hash", *replacementBlock
                        ? QString(64, '8') : recoveryBlockHash},
                    {"height", 100}, {"confirmations", *invalidConfirmation ? 5 : 6}};
            if (method == "estimatesmartfee")
                return QJsonObject{{"feerate", 0.00001000}, {"blocks", 6}};
            if (method == "testmempoolaccept")
                return QJsonArray{QJsonObject{{"txid", signedCancelTxid},
                    {"wtxid", signedCancelWtxid}, {"allowed", true},
                    {"vsize", 149}, {"fees", QJsonObject{{"base", 0.00000300}}}}};
            return QJsonValue(QJsonValue::Null);
        });
        const auto evidence = BitcoinRecoveryPsbtRpcVerifier::validateEvidence(client(),
            fixture.terms, BitcoinRecoveryPsbtEvidence::Leg::Pay,
            BitcoinRecoveryPsbtEvidence::Branch::Cancel, 100, psbtBytes(), {},
            lockTxid, "0", "10000", sharedScript, keyA, keyB,
            "shared", sharedScript);
        const QVariantMap result = evidence.details();
        QVERIFY2(result.value("valid").toBool(), qPrintable(result.value("error").toString()));
        QVERIFY(evidence.accepted());
        QVERIFY(result.value("rpcVerified").toBool());
        QVERIFY(result.value("branchPolicyVerified").toBool());
        QVERIFY(result.value("feeEstimateVerified").toBool());
        QVERIFY(!result.value("executable").toBool());
        QCOMPARE(evidence.parentBlockHeight(), QString("6"));
        QCOMPARE(evidence.relativeBlocks(), quint32(72));

        const auto maturity = BitcoinRecoveryMaturityVerifier::validate(client(), evidence);
        QVERIFY2(maturity.accepted(),
                 qPrintable(maturity.details().value("error").toString()));
        QVERIFY(maturity.details().value("mature").toBool());
        QCOMPARE(maturity.earliestBlockHeight(), QString("78"));
        QVERIFY(!maturity.details().value("executable").toBool());

        *parentConfirmations = 6;
        const auto immaturePrepared = BitcoinRecoveryPsbtRpcVerifier::validateEvidence(client(),
            fixture.terms, BitcoinRecoveryPsbtEvidence::Leg::Pay,
            BitcoinRecoveryPsbtEvidence::Branch::Cancel, 100, psbtBytes(), {},
            lockTxid, "0", "10000", sharedScript, keyA, keyB,
            "shared", sharedScript);
        QVERIFY(immaturePrepared.accepted());
        QCOMPARE(immaturePrepared.parentBlockHeight(), QString("100"));
        const auto immature = BitcoinRecoveryMaturityVerifier::validate(
            client(), immaturePrepared);
        QVERIFY(!immature.accepted());
        QVERIFY(immature.details().value("valid").toBool());
        QVERIFY(!immature.details().value("mature").toBool());
        QCOMPARE(immature.details().value("blocksRemaining").toString(), QString("66"));
        *parentConfirmations = 100;

        QCOMPARE(result.value("sighashHex").toString(),
                 QString("16344246798e1d1c3f973a621089263a2c4f3b7486cf6dc454218164abd7c4b2"));
        const auto signedEvidence =
            BitcoinSignedRecoveryEvidence::validate(evidence, signedCancelBytes());
        QVERIFY2(signedEvidence.accepted(),
                 qPrintable(signedEvidence.details().value("error").toString()));
        QVERIFY(signedEvidence.details().value("signatureVerified").toBool());
        QVERIFY(signedEvidence.details().value("witnessVerified").toBool());
        QVERIFY(signedEvidence.details().value("rawIdentityVerified").toBool());
        QCOMPARE(signedEvidence.settlementTermsCommitment(),
                 evidence.settlementTermsCommitment());
        QCOMPARE(signedEvidence.txid(), signedCancelTxid);
        QCOMPARE(signedEvidence.wtxid(), signedCancelWtxid);
        QCOMPARE(signedEvidence.virtualSize(), quint64(149));
        QVERIFY(!signedEvidence.details().value("executable").toBool());

        const auto mempoolApproval = BitcoinRecoveryMempoolVerifier::validate(
            client(), signedEvidence, signedCancelBytes());
        QVERIFY2(mempoolApproval.accepted(),
                 qPrintable(mempoolApproval.details().value("error").toString()));
        QVERIFY(mempoolApproval.details().value("mempoolPolicyVerified").toBool());
        QVERIFY(mempoolApproval.details().value("tipPinned").toBool());
        QVERIFY(!mempoolApproval.details().value("executable").toBool());
        QCOMPARE(mempoolApproval.txid(), signedEvidence.txid());
        QVERIFY(!BitcoinRecoveryMempoolVerifier::validate(
            client(), signedEvidence, signedCancelBytes() + "00").accepted());

        const auto observation = BitcoinRecoveryChainObserver::observe(
            client(), signedEvidence, signedCancelBytes());
        QVERIFY2(observation.accepted(),
                 qPrintable(observation.details().value("error").toString()));
        QVERIFY(observation.confirmed());
        QVERIFY(observation.details().value("nodeIdentityVerified").toBool());
        QVERIFY(observation.details().value("tipPinned").toBool());
        QCOMPARE(observation.settlementTermsCommitment(),
                 evidence.settlementTermsCommitment());
        QCOMPARE(observation.details().value("confirmations").toString(), QString("6"));
        QVERIFY(!observation.details().value("executable").toBool());

        QTemporaryDir trackerDirectory;
        QVERIFY(trackerDirectory.isValid());
        const QString trackerPath = trackerDirectory.filePath("confirmations.json");
        BitcoinRecoveryConfirmationTracker::Status confirmedStatus;
        BitcoinRecoveryConfirmationTracker::Status reorgStatus;
        {
            BitcoinRecoveryConfirmationTracker::Tracker tracker(trackerPath);
            QVERIFY2(tracker.ready(), qPrintable(tracker.error()));
            confirmedStatus = tracker.observe(observation);
            QVERIFY(confirmedStatus.accepted());
            QVERIFY(confirmedStatus.confirmed());
            QVERIFY(!confirmedStatus.reorgDetected());
            QCOMPARE(confirmedStatus.settlementTermsCommitment(),
                     evidence.settlementTermsCommitment());
            QCOMPARE(confirmedStatus.confirmationBasisCommitment(),
                     confirmedStatus.commitment());
            const auto currentStatus = tracker.current(
                observation.broadcastCommitment());
            QVERIFY(currentStatus.confirmed());
            QCOMPARE(currentStatus.commitment(), confirmedStatus.commitment());
            QCOMPARE(currentStatus.settlementTermsCommitment(),
                     evidence.settlementTermsCommitment());
            QVERIFY(!tracker.current(QByteArray(32, 'x')).accepted());
            QVERIFY(!confirmedStatus.details().value("executable").toBool());

            *replacementBlock = true;
            const auto replacementObservation = BitcoinRecoveryChainObserver::observe(
                client(), signedEvidence, signedCancelBytes());
            QVERIFY(replacementObservation.accepted());
            QVERIFY(replacementObservation.confirmed());
            reorgStatus = tracker.observe(replacementObservation);
            QVERIFY(reorgStatus.accepted());
            QVERIFY(reorgStatus.reorgDetected());
            QVERIFY(!reorgStatus.confirmed());
            *replacementBlock = false;
        }
        {
            BitcoinRecoveryConfirmationTracker::Tracker reopened(trackerPath);
            QVERIFY2(reopened.ready(), qPrintable(reopened.error()));
            QVERIFY(reopened.current(observation.broadcastCommitment()).reorgDetected());
            const auto latchedStatus = reopened.observe(observation);
            QVERIFY(latchedStatus.accepted());
            QVERIFY(latchedStatus.reorgDetected());
            QVERIFY(!latchedStatus.confirmed());
        }
        *missingTransaction = true;
        const auto missingObservation = BitcoinRecoveryChainObserver::observe(
            client(), signedEvidence, signedCancelBytes());
        QVERIFY2(missingObservation.accepted(),
                 qPrintable(missingObservation.details().value("error").toString()));
        QVERIFY(!missingObservation.found());
        QVERIFY(!missingObservation.confirmed());
        QVERIFY(!missingObservation.details().value("executable").toBool());
        const QString missingTrackerPath =
            trackerDirectory.filePath("missing-confirmations.json");
        BitcoinRecoveryConfirmationTracker::Tracker missingTracker(missingTrackerPath);
        QVERIFY(missingTracker.ready());
        QVERIFY(!missingTracker.observe(missingObservation).accepted());
        *missingTransaction = false;
        QVERIFY(missingTracker.observe(observation).confirmed());
        *missingTransaction = true;
        const auto disappearedStatus = missingTracker.observe(missingObservation);
        QVERIFY(disappearedStatus.accepted());
        QVERIFY(disappearedStatus.reorgDetected());
        QVERIFY(!disappearedStatus.confirmed());
        *missingTransaction = false;
        QVERIFY(!BitcoinRecoveryChainObserver::observe(
            client(), signedEvidence, signedCancelBytes() + "00").accepted());
        *invalidConfirmation = true;
        QVERIFY(!BitcoinRecoveryChainObserver::observe(
            client(), signedEvidence, signedCancelBytes()).accepted());
        *invalidConfirmation = false;
        invalidNodeIdentity = true;
        QVERIFY(!BitcoinRecoveryChainObserver::observe(
            client(), signedEvidence, signedCancelBytes()).accepted());
        invalidNodeIdentity = false;

        QVERIFY(!BitcoinSignedRecoveryEvidence::validate(evidence,
            signedCancelBytes(9700, {cancelSignatureB, cancelSignatureA, witnessScript}))
            .accepted());
        QVERIFY(!BitcoinSignedRecoveryEvidence::validate(evidence,
            signedCancelBytes(9700, {cancelSignatureA, cancelSignatureB,
                                     witnessScript, "00"})).accepted());
        QVERIFY(!BitcoinSignedRecoveryEvidence::validate(
            evidence, signedCancelBytes(9699)).accepted());
        QVERIFY(!BitcoinSignedRecoveryEvidence::validate(
            evidence, signedCancelBytes() + "00").accepted());

        const auto parsed = BitcoinRawTransaction::parseRecovery(signedCancelBytes());
        QVERIFY(parsed.valid);
        QCOMPARE(parsed.txid, signedEvidence.txid());
        QCOMPARE(parsed.wtxid, signedEvidence.wtxid());

        QByteArray nonMinimalCount = signedCancelBytes();
        nonMinimalCount.replace(12, 2, "fd0100");
        QVERIFY(!BitcoinRawTransaction::parseRecovery(nonMinimalCount).valid);
        QVERIFY(!BitcoinRawTransaction::parseRecovery(signedCancelBytes().toUpper()).valid);
        QVERIFY(!BitcoinRawTransaction::parseRecovery(
            signedCancelBytes(9700, {"", cancelSignatureB, witnessScript})).valid);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        SettlementJournal journal(directory.filePath("settlement.json"));
        const QByteArray termsCommitment = SettlementTerms::build(fixture.terms, 100).commitment;
        SignedQuoteAcceptor acceptor(directory.filePath("quotes.json"),
            fixture.terms.protocolId, fixture.terms.payNetwork,
            fixture.terms.receiveNetwork, fixture.terms.xmzNetwork,
            "recovery-test-domain");
        const auto acceptedQuote = acceptor.acceptEvidence(fixture.terms,
            maker.sign(SettlementTerms::build(fixture.terms, 99).payload,
                       "recovery-test-domain"), 99);
        QVERIFY(acceptedQuote.accepted());
        QCOMPARE(acceptedQuote.commitment(), termsCommitment);
        QVERIFY(journal.appendQuoteAccepted("swap-rpc", acceptedQuote, 99));
        const auto mutual = acceptor.acceptMutualEvidence(fixture.terms,
            maker.sign(SettlementTerms::build(fixture.terms, 99).payload, "recovery-test-domain"),
            taker.sign(SignedQuoteAcceptor::takerAcceptancePayload(fixture.terms, 99), "recovery-test-domain"), 99);
        QVERIFY(mutual.mutuallyAuthorized());
        const auto recovered = journal.recoverQuote("swap-rpc", acceptor,
            fixture.terms.makerKey, fixture.terms.nonce, fixture.terms.quoteExpiry + 1);
        QVERIFY(recovered.valid());
        const auto recoveredPrepared = BitcoinRecoveryPsbtRpcVerifier::validateEvidence(client(),
            recovered.terms(), BitcoinRecoveryPsbtEvidence::Leg::Pay,
            BitcoinRecoveryPsbtEvidence::Branch::Cancel, fixture.terms.quoteExpiry + 1,
            psbtBytes(), {}, lockTxid, "0", "10000", sharedScript, keyA, keyB,
            "shared", sharedScript);
        QVERIFY(recoveredPrepared.accepted());
        {
            SettlementJournal recoveryJournal(directory.filePath("recovered.json"));
            QVERIFY(!recoveryJournal.appendBitcoinRecoveryPrepared("swap-recovered", recovered, evidence,
                fixture.terms.quoteExpiry + 1));
            QVERIFY(recoveryJournal.appendQuoteAccepted("swap-recovered", mutual, 99));
            QVERIFY(!recoveryJournal.appendBitcoinRecoveryPrepared("swap-recovered", RecoveredMutualQuote{}, evidence,
                fixture.terms.quoteExpiry + 1));
            QVERIFY(recoveryJournal.appendBitcoinRecoveryPrepared("swap-recovered", recovered, recoveredPrepared,
                fixture.terms.quoteExpiry + 1));
            QCOMPARE(recoveryJournal.records().size(), 2);
        }
        QVERIFY(journal.appendBitcoinRecoveryPrepared("swap-rpc", evidence, 100));
        QVERIFY(journal.appendBitcoinRecoveryPrepared("swap-rpc", evidence, 100));
        QCOMPARE(journal.records().last().toObject().value("event").toString(),
                 QString("pay_cancel_prepared"));
        QCOMPARE(journal.records().last().toObject().value("chain").toString(),
                 fixture.terms.payNetwork);
        QVERIFY(journal.appendBitcoinRecoverySigned("swap-rpc", signedEvidence, 101));
        QVERIFY(journal.appendBitcoinRecoverySigned("swap-rpc", signedEvidence, 101));
        QCOMPARE(journal.records().last().toObject().value("event").toString(),
                 QString("pay_cancel_signed"));
        QCOMPARE(journal.records().last().toObject().value("artifactHash").toString(),
                 QString::fromLatin1(signedEvidence.journalCommitment().toHex()));
        QVERIFY(!journal.appendBitcoinRecoveryObserved("swap-rpc", observation, 102));
        QVERIFY(journal.appendBitcoinRecoveryMature("swap-rpc", maturity, 102));
        QVERIFY(journal.appendBitcoinRecoveryMature("swap-rpc", maturity, 102));
        QCOMPARE(journal.records().last().toObject().value("event").toString(),
                 QString("pay_cancel_mature"));
        QVERIFY(journal.appendBitcoinRecoveryObserved("swap-rpc", observation, 102));
        QVERIFY(journal.appendBitcoinRecoveryObserved("swap-rpc", observation, 102));
        QCOMPARE(journal.records().last().toObject().value("event").toString(),
                 QString("pay_cancel_broadcast"));
        QCOMPARE(journal.records().last().toObject().value("artifactHash").toString(),
                 QString::fromLatin1(observation.broadcastCommitment().toHex()));
        const QString journalTrackerPath =
            trackerDirectory.filePath("journal-confirmations.json");
        BitcoinRecoveryConfirmationTracker::Tracker journalTracker(journalTrackerPath);
        QVERIFY(journalTracker.observe(observation).confirmed());
        QVERIFY(journal.appendBitcoinRecoveryConfirmed("swap-rpc", journalTracker,
            observation.broadcastCommitment(), 103));
        QVERIFY(journal.appendBitcoinRecoveryConfirmed("swap-rpc", journalTracker,
            observation.broadcastCommitment(), 103));
        QCOMPARE(journal.records().last().toObject().value("event").toString(),
                 QString("pay_cancel_confirmed"));
        BitcoinRecoveryConfirmationTracker::Tracker reorgedTracker(trackerPath);
        QVERIFY(reorgedTracker.current(observation.broadcastCommitment()).reorgDetected());
        QVERIFY(!journal.appendBitcoinRecoveryConfirmed("swap-rpc", reorgedTracker,
            observation.broadcastCommitment(), 104));
        QVERIFY(!journal.appendBitcoinRecoveryObserved(
            "swap-rpc", missingObservation, 104));

        SettlementJournal wrongJournal(directory.filePath("wrong.json"));
        QVERIFY(!wrongJournal.appendBitcoinRecoveryPrepared("swap-rpc", evidence, 100));
        QVERIFY(!wrongJournal.appendBitcoinRecoverySigned("swap-rpc", signedEvidence, 101));
        QVERIFY(!wrongJournal.appendBitcoinRecoveryMature("swap-rpc", maturity, 102));
        QVERIFY(!wrongJournal.appendBitcoinRecoveryObserved("swap-rpc", observation, 102));
        QVERIFY(!wrongJournal.appendBitcoinRecoveryConfirmed(
            "swap-rpc", journalTracker, observation.broadcastCommitment(), 103));
    }
    void earnsFeeOnlyForSameQuoteAndCurrentlyConfirmedRedeem()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Fixture fixture;
        OfferIdentity maker, taker;
        fixture.terms.makerKey = maker.publicKey();
        fixture.terms.takerKey = taker.publicKey();
        fixture.terms.payerKey = fixture.terms.takerKey;
        fixture.terms.receiveNetwork = fixture.terms.xmzNetwork = "xmz-fakechain";
        SignedOffer::Terms buyTerms{"monzero-dex-offer-v1",
            "isolated-btc-xmz", "11111111-1111-4111-8111-111111111111",
            "BTC/XMZ", "buy", "10000", "12000000", 90, 1000};
        SignedOffer::Terms sellTerms{"monzero-dex-offer-v1",
            "isolated-btc-xmz", "22222222-2222-4222-8222-222222222222",
            "BTC/XMZ", "sell", "10000", "10000000", 90, 1000};
        const auto buy = SignedOffer::create(buyTerms, maker);
        const auto sell = SignedOffer::create(sellTerms, taker);
        OfferBookStore book(directory.filePath("offers.json"),
            buyTerms.protocolId, buyTerms.network);
        QVERIFY(book.insert(buy, 99));
        QVERIFY(book.insert(sell, 99));
        OfferFillReservationStore fills(directory.filePath("fills.json"),
            buyTerms.protocolId, buyTerms.network, book);
        const QString reservationId =
            "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
        const auto reservation = fills.reserve(reservationId, buy, sell,
            "10000", 190, 99);
        QVERIFY(reservation.accepted());
        fixture.terms.offerFillReservationCommitment = reservation.commitment();
        auto rawHex = std::make_shared<QByteArray>();
        auto redeemTxid = std::make_shared<QString>();
        auto redeemWtxid = std::make_shared<QString>();
        auto replacementBlock = std::make_shared<bool>(false);
        serve([this, fixture, rawHex, redeemTxid, redeemWtxid, replacementBlock](
                  const QJsonObject &request) mutable -> QJsonValue {
            const QString method = request.value("method").toString();
            if (method == "getbestblockhash") return bestBlock;
            if (method == "decodepsbt") {
                QJsonObject child = fixture.redeem; child["vsize"] = 150;
                return psbt(child, 0.00010000, 0.00000300);
            }
            if (method == "getrawtransaction") {
                const QJsonArray params = request.value("params").toArray();
                if (!redeemTxid->isEmpty() && params.at(0) == QJsonValue(*redeemTxid)) {
                    if (params.at(1) == QJsonValue(false))
                        return QString::fromLatin1(*rawHex);
                    return QJsonObject{{"txid", *redeemTxid}, {"hash", *redeemWtxid},
                        {"blockhash", *replacementBlock ? QString(64, '8')
                                                        : recoveryBlockHash},
                        {"confirmations", bitcoinNodeHeight - 99},
                        {"in_active_chain", true}};
                }
                return fixture.lock;
            }
            if (method == "gettxout") return prevout(lockTxid, 0.00010000);
            if (method == "getblockheader")
                return QJsonObject{{"hash", *replacementBlock ? QString(64, '8')
                                                               : recoveryBlockHash},
                                   {"height", 100},
                                   {"confirmations", bitcoinNodeHeight - 99}};
            if (method == "estimatesmartfee")
                return QJsonObject{{"feerate", 0.00001000}, {"blocks", 6}};
            return QJsonValue(QJsonValue::Null);
        });

        const auto prepared = BitcoinRecoveryPsbtRpcVerifier::validateEvidence(client(),
            fixture.terms, BitcoinRecoveryPsbtEvidence::Leg::Pay,
            BitcoinRecoveryPsbtEvidence::Branch::Redeem, 100, psbtBytes(), {},
            lockTxid, "0", "10000", sharedScript, keyA, keyB,
            "destination", destinationScript);
        QVERIFY2(prepared.accepted(),
                 qPrintable(prepared.details().value("error").toString()));
        QVERIFY(!BitcoinRecoveryMaturityVerifier::validate(client(), prepared).accepted());
        QVERIFY(!prepared.details().value("sighashHex").toString().isEmpty());
        *rawHex = signedRedeemBytes(redeemSignatureA, redeemSignatureB);
        const auto parsed = BitcoinRawTransaction::parseRecovery(*rawHex);
        QVERIFY2(parsed.valid, qPrintable(parsed.error));
        *redeemTxid = parsed.txid; *redeemWtxid = parsed.wtxid;
        const auto signedEvidence = BitcoinSignedRecoveryEvidence::validate(
            prepared, *rawHex);
        QVERIFY2(signedEvidence.accepted(),
                 qPrintable(signedEvidence.details().value("error").toString()));
        const auto observation = BitcoinRecoveryChainObserver::observe(
            client(), signedEvidence, *rawHex);
        QVERIFY2(observation.accepted(),
                 qPrintable(observation.details().value("error").toString()));
        QVERIFY(observation.confirmed());

        BitcoinRecoveryConfirmationTracker::Tracker tracker(
            directory.filePath("confirmations.json"));
        const auto status = tracker.observe(observation);
        QVERIFY(status.confirmed());
        bitcoinNodeHeight = 106;
        const auto deeperObservation = BitcoinRecoveryChainObserver::observe(
            client(), signedEvidence, *rawHex);
        QVERIFY(deeperObservation.confirmed());
        QCOMPARE(deeperObservation.confirmations(), QString("7"));
        const auto deeperStatus = tracker.observe(deeperObservation);
        QVERIFY(deeperStatus.confirmed());
        QCOMPARE(deeperStatus.confirmationBasisCommitment(), status.commitment());
        QVERIFY(deeperStatus.commitment() != status.commitment());

        SignedQuoteAcceptor acceptor(directory.filePath("quotes.json"),
            fixture.terms.protocolId, fixture.terms.payNetwork,
            fixture.terms.receiveNetwork, fixture.terms.xmzNetwork,
            "fee-gate-domain");
        const auto quote = acceptor.acceptMutualEvidence(fixture.terms,
            maker.sign(SettlementTerms::build(fixture.terms, 99).payload,
                       "fee-gate-domain"),
            taker.sign(SignedQuoteAcceptor::takerAcceptancePayload(fixture.terms, 99),
                       "fee-gate-domain"), 99);
        QVERIFY(quote.accepted());
        QVERIFY(fills.commit(reservationId, quote, 100));
        SettlementJournal journal(directory.filePath("settlement.json"));
        QVERIFY(journal.appendQuoteAccepted("swap-fee", quote, 99));
        QVERIFY(journal.appendBitcoinRecoveryPrepared("swap-fee", prepared, 100));
        QVERIFY(journal.appendBitcoinRecoverySigned("swap-fee", signedEvidence, 101));
        QVERIFY(journal.appendBitcoinRecoveryObserved("swap-fee", observation, 102));
        QVERIFY(journal.appendBitcoinRecoveryConfirmed("swap-fee", tracker,
            observation.broadcastCommitment(), 103));

        const QString xmzTxid(64, '7'), xmzBlock(64, '6');
        const QJsonObject xmzWallet{{"transfer", QJsonObject{{"txid", xmzTxid},
            {"type", "out"}, {"amount", 1001.0}, {"fee", 3.0},
            {"height", 100.0}, {"confirmations", 6.0}, {"locked", false},
            {"double_spend_seen", false}, {"destinations", QJsonArray{
                QJsonObject{{"address", "receive"}, {"amount", 1000.0}},
                QJsonObject{{"address", "fee"}, {"amount", 1.0}}}}}}};
        const QJsonObject xmzBlockResult{{"status", "OK"}, {"untrusted", false},
            {"miner_tx_hash", ""}, {"tx_hashes", QJsonArray{xmzTxid}},
            {"block_header", QJsonObject{{"hash", xmzBlock}, {"height", 100.0},
                                         {"orphan_status", false}}}};
        const QJsonObject xmzNode{{"status", "OK"}, {"synchronized", true},
            {"untrusted", false}, {"nettype", "fakechain"}, {"height", 106.0},
            {"target_height", 106.0}};
        const auto xmzEvidence = MonzeroSuccessTransactionEvidence::validateEvidence(
            fixture.terms, 100, fixture.terms.xmzGenesis, xmzTxid,
            xmzWallet, xmzBlockResult, xmzNode);
        QVERIFY2(xmzEvidence.accepted(),
                 qPrintable(xmzEvidence.details().value("error").toString()));
        auto otherTerms = fixture.terms;
        otherTerms.nonce = QByteArray(32, 'u');
        const auto otherQuoteEvidence =
            MonzeroSuccessTransactionEvidence::validateEvidence(
                otherTerms, 100, otherTerms.xmzGenesis, xmzTxid,
                xmzWallet, xmzBlockResult, xmzNode);
        QVERIFY(otherQuoteEvidence.accepted());
        QVERIFY(!journal.appendProtocolFeeEarned("swap-fee", otherQuoteEvidence,
            tracker, observation.broadcastCommitment(), 104));
        QVERIFY(journal.appendProtocolFeeEarned("swap-fee", xmzEvidence, tracker,
                                                observation.broadcastCommitment(), 104));
        QCOMPARE(journal.records().last().toObject().value("event").toString(),
                 QString("protocol_fee_earned"));
        const auto settled = journal.settledEvidence("swap-fee", quote,
            xmzEvidence, tracker, observation.broadcastCommitment());
        QVERIFY(settled.accepted());
        QCOMPARE(settled.swapId(), QString("swap-fee"));
        QCOMPARE(settled.outcome(), QString("settled"));
        QCOMPARE(settled.quoteCommitment(), quote.commitment());
        QCOMPARE(settled.fillReservationCommitment(),
                 quote.fillReservationCommitment());
        QVERIFY(settled.commitment().size() == 32);
        ConfirmationTracker xmzTracker(directory.filePath("xmz-current.json"));
        ConfirmationTracker emptyXmzTracker(directory.filePath("xmz-empty.json"));
        QVERIFY(!fills.finalizeSettled(reservationId, "swap-fee", quote, journal,
            xmzEvidence, emptyXmzTracker, tracker,
            observation.broadcastCommitment()));
        QVERIFY(xmzTracker.observe("XMZ", xmzEvidence.transactionId(), true,
            xmzEvidence.blockHash(), "100", "6", "105", 5)
                .value("confirmed").toBool());
        QVERIFY(fills.finalizeSettled(reservationId, "swap-fee", quote, journal,
            xmzEvidence, xmzTracker, tracker, observation.broadcastCommitment()));
        QVERIFY(fills.finalizeSettled(reservationId, "swap-fee", quote, journal,
            xmzEvidence, xmzTracker, tracker, observation.broadcastCommitment()));
        QCOMPARE(fills.records().first().toObject().value("state").toString(),
                 QString("settled"));
        QVERIFY(!fills.release(reservationId));
        QVERIFY(!journal.settledEvidence("wrong-swap", quote, xmzEvidence,
            tracker, observation.broadcastCommitment()).accepted());
        QVERIFY(!journal.settledEvidence("swap-fee", quote, otherQuoteEvidence,
            tracker, observation.broadcastCommitment()).accepted());
        QVERIFY(journal.appendProtocolFeeEarned("swap-fee", xmzEvidence, tracker,
                                                observation.broadcastCommitment(), 104));
        QVERIFY(!journal.appendProtocolFeeEarned("wrong-swap", xmzEvidence, tracker,
                                                 observation.broadcastCommitment(), 104));
        QVERIFY(!journal.appendProtocolFeeEarned("swap-fee", xmzEvidence, tracker,
                                                 QByteArray(32, 'z'), 104));

        SettlementJournal reorgJournal(directory.filePath("reorg-settlement.json"));
        QVERIFY(reorgJournal.appendQuoteAccepted("swap-reorg", quote, 99));
        QVERIFY(reorgJournal.appendBitcoinRecoveryPrepared("swap-reorg", prepared, 100));
        QVERIFY(reorgJournal.appendBitcoinRecoverySigned("swap-reorg", signedEvidence, 101));
        QVERIFY(reorgJournal.appendBitcoinRecoveryObserved("swap-reorg", observation, 102));
        QVERIFY(reorgJournal.appendBitcoinRecoveryConfirmed("swap-reorg", tracker,
            observation.broadcastCommitment(), 103));
        *replacementBlock = true;
        const auto replacementObservation = BitcoinRecoveryChainObserver::observe(
            client(), signedEvidence, *rawHex);
        QVERIFY(replacementObservation.accepted());
        const auto reorgStatus = tracker.observe(replacementObservation);
        QVERIFY(reorgStatus.reorgDetected());
        QVERIFY(!reorgStatus.confirmed());
        QVERIFY(!journal.settledEvidence("swap-fee", quote, xmzEvidence,
            tracker, observation.broadcastCommitment()).accepted());
        QCOMPARE(reorgStatus.confirmationBasisCommitment(), status.commitment());
        QVERIFY(!reorgJournal.appendProtocolFeeEarned("swap-reorg", xmzEvidence,
            tracker, observation.broadcastCommitment(), 104));
        QVERIFY(!reorgJournal.appendProtocolFeeRevoked("swap-reorg", xmzEvidence,
            tracker, observation.broadcastCommitment(), 105));
        QVERIFY(journal.appendProtocolFeeRevoked("swap-fee", xmzEvidence, tracker,
                                                 observation.broadcastCommitment(), 105));
        QCOMPARE(journal.records().last().toObject().value("event").toString(),
                 QString("protocol_fee_revoked"));
        QVERIFY(journal.appendProtocolFeeRevoked("swap-fee", xmzEvidence, tracker,
                                                 observation.broadcastCommitment(), 105));
        QVERIFY(!journal.appendProtocolFeeRevoked("swap-fee", otherQuoteEvidence,
            tracker, observation.broadcastCommitment(), 105));
    }
    void refundsFillOnlyAfterAllThreeCurrentProofs()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Fixture fixture;
        OfferIdentity maker, taker;
        fixture.terms.makerKey = maker.publicKey();
        fixture.terms.takerKey = taker.publicKey();
        fixture.terms.payerKey = fixture.terms.takerKey;
        fixture.terms.receiveNetwork = fixture.terms.xmzNetwork = "xmz-fakechain";
        fixture.terms.receiveRefundCommitment =
            MonzeroRefundTransactionEvidence::intentCommitment(
                MonzeroRefundTransactionEvidence::Leg::ReceivePrincipal,
                fixture.terms.xmzNetwork, fixture.terms.xmzGenesis,
                fixture.terms.receiveRefundRecipient,
                fixture.terms.receiveAmountAtomic,
                fixture.terms.receiveNativeFeeMaxAtomic,
                fixture.terms.receiveRefundHeight);
        fixture.terms.xmzFeeRefundCommitment =
            MonzeroRefundTransactionEvidence::intentCommitment(
                MonzeroRefundTransactionEvidence::Leg::ProtocolFee,
                fixture.terms.xmzNetwork, fixture.terms.xmzGenesis,
                fixture.terms.xmzRefundRecipient, fixture.terms.feeAtomic,
                fixture.terms.xmzNativeFeeMaxAtomic,
                fixture.terms.xmzRefundHeight);
        SignedOffer::Terms buyTerms{"monzero-dex-offer-v1",
            "isolated-btc-xmz", "33333333-3333-4333-8333-333333333333",
            "BTC/XMZ", "buy", "10000", "12000000", 90, 1000};
        SignedOffer::Terms sellTerms{"monzero-dex-offer-v1",
            "isolated-btc-xmz", "44444444-4444-4444-8444-444444444444",
            "BTC/XMZ", "sell", "10000", "10000000", 90, 1000};
        const auto buy = SignedOffer::create(buyTerms, maker);
        const auto sell = SignedOffer::create(sellTerms, taker);
        OfferBookStore book(directory.filePath("refund-offers.json"),
            buyTerms.protocolId, buyTerms.network);
        QVERIFY(book.insert(buy, 99)); QVERIFY(book.insert(sell, 99));
        OfferFillReservationStore fills(directory.filePath("refund-fills.json"),
            buyTerms.protocolId, buyTerms.network, book);
        const QString reservationId = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
        const auto reservation = fills.reserve(reservationId, buy, sell,
            "10000", 190, 99);
        QVERIFY(reservation.accepted());
        fixture.terms.offerFillReservationCommitment = reservation.commitment();

        const auto parsed = BitcoinRawTransaction::parseRecovery(signedRefundBytes());
        QVERIFY2(parsed.valid, qPrintable(parsed.error));
        serve([fixture, parsed](const QJsonObject &request) -> QJsonValue {
            const QString method = request.value("method").toString();
            if (method == "getbestblockhash") return bestBlock;
            if (method == "decodepsbt") {
                QJsonObject child = fixture.refund; child["vsize"] = 150;
                return psbt(child, 0.00009700, 0.00000300);
            }
            if (method == "getrawtransaction") {
                const QJsonArray params = request.value("params").toArray();
                if (params.at(0) == QJsonValue(parsed.txid)) {
                    if (params.at(1) == QJsonValue(false))
                        return QString::fromLatin1(signedRefundBytes());
                    return QJsonObject{{"txid", parsed.txid}, {"hash", parsed.wtxid},
                        {"blockhash", recoveryBlockHash}, {"confirmations", 6},
                        {"in_active_chain", true}};
                }
                return fixture.cancel;
            }
            if (method == "gettxout") return prevout(cancelTxid, 0.00009700);
            if (method == "getblockheader")
                return QJsonObject{{"hash", recoveryBlockHash}, {"height", 100},
                                   {"confirmations", 6}};
            if (method == "estimatesmartfee")
                return QJsonObject{{"feerate", 0.00001000}, {"blocks", 6}};
            return QJsonValue(QJsonValue::Null);
        });
        const auto prepared = BitcoinRecoveryPsbtRpcVerifier::validateEvidence(client(),
            fixture.terms, BitcoinRecoveryPsbtEvidence::Leg::Pay,
            BitcoinRecoveryPsbtEvidence::Branch::Refund, 400, psbtBytes(), {},
            cancelTxid, "0", "9700", sharedScript, keyA, keyB,
            "destination", destinationScript);
        QVERIFY2(prepared.accepted(),
                 qPrintable(prepared.details().value("error").toString()));
        const auto signedRefund = BitcoinSignedRecoveryEvidence::validate(
            prepared, signedRefundBytes());
        QVERIFY2(signedRefund.accepted(),
                 qPrintable(signedRefund.details().value("error").toString()));
        const auto observation = BitcoinRecoveryChainObserver::observe(
            client(), signedRefund, signedRefundBytes());
        QVERIFY(observation.confirmed());
        BitcoinRecoveryConfirmationTracker::Tracker tracker(
            directory.filePath("refund-confirmations.json"));
        QVERIFY(tracker.observe(observation).confirmed());

        SignedQuoteAcceptor acceptor(directory.filePath("refund-quotes.json"),
            fixture.terms.protocolId, fixture.terms.payNetwork,
            fixture.terms.receiveNetwork, fixture.terms.xmzNetwork,
            "refund-domain");
        const auto quote = acceptor.acceptMutualEvidence(fixture.terms,
            maker.sign(SettlementTerms::build(fixture.terms, 99).payload,
                       "refund-domain"),
            taker.sign(SignedQuoteAcceptor::takerAcceptancePayload(fixture.terms, 99),
                       "refund-domain"), 99);
        QVERIFY(quote.accepted());
        QVERIFY(fills.commit(reservationId, quote, 100));
        SettlementJournal journal(directory.filePath("refund-journal.json"));
        QVERIFY(journal.appendQuoteAccepted("swap-refund", quote, 99));
        QVERIFY(journal.appendBitcoinRecoveryPrepared("swap-refund", prepared, 400));
        QVERIFY(journal.appendBitcoinRecoverySigned("swap-refund", signedRefund, 401));
        QVERIFY(journal.appendBitcoinRecoveryObserved("swap-refund", observation, 402));
        QVERIFY(journal.appendBitcoinRecoveryConfirmed("swap-refund", tracker,
            observation.broadcastCommitment(), 403));

        const QString principalTx(64, '7'), feeTx(64, '8'), xmzBlock(64, '6');
        const auto xmzWallet = [](const QString &txid, const QString &recipient,
                                  double amount) {
            return QJsonObject{{"transfer", QJsonObject{{"txid", txid},
                {"type", "out"}, {"amount", amount}, {"fee", 3.0},
                {"height", 214.0}, {"confirmations", 6.0}, {"locked", false},
                {"double_spend_seen", false}, {"destinations", QJsonArray{
                    QJsonObject{{"address", recipient}, {"amount", amount}}}}}}};
        };
        const auto xmzBlockResult = [&](const QString &txid) {
            return QJsonObject{{"status", "OK"}, {"untrusted", false},
                {"miner_tx_hash", ""}, {"tx_hashes", QJsonArray{txid}},
                {"block_header", QJsonObject{{"hash", xmzBlock},
                    {"height", 214.0}, {"orphan_status", false}}}};
        };
        const QJsonObject xmzNode{{"status", "OK"}, {"synchronized", true},
            {"untrusted", false}, {"nettype", "fakechain"}, {"height", 220.0},
            {"target_height", 220.0}};
        const auto principal = MonzeroRefundTransactionEvidence::validateEvidence(
            fixture.terms, MonzeroRefundTransactionEvidence::Leg::ReceivePrincipal,
            400, fixture.terms.xmzGenesis, principalTx,
            xmzWallet(principalTx, fixture.terms.receiveRefundRecipient, 1000.0),
            xmzBlockResult(principalTx), xmzNode);
        const auto fee = MonzeroRefundTransactionEvidence::validateEvidence(
            fixture.terms, MonzeroRefundTransactionEvidence::Leg::ProtocolFee,
            400, fixture.terms.xmzGenesis, feeTx,
            xmzWallet(feeTx, fixture.terms.xmzRefundRecipient, 1.0),
            xmzBlockResult(feeTx), xmzNode);
        QVERIFY(principal.accepted()); QVERIFY(fee.accepted());
        ConfirmationTracker xmzTracker(directory.filePath("refund-xmz-current.json"));
        ConfirmationTracker emptyXmzTracker(
            directory.filePath("refund-xmz-empty.json"));
        QVERIFY(xmzTracker.observe("XMZ", principal.transactionId(), true, xmzBlock,
            "214", "6", "219", 5).value("confirmed").toBool());
        QVERIFY(xmzTracker.observe("XMZ", fee.transactionId(), true, xmzBlock,
            "214", "6", "219", 5).value("confirmed").toBool());
        QVERIFY(!journal.refundedEvidence("swap-refund", quote, principal, fee,
            tracker, observation.broadcastCommitment()).accepted());
        QVERIFY(journal.appendXmzRefundConfirmed("swap-refund", principal, fee, 404));
        QVERIFY(journal.appendXmzRefundConfirmed("swap-refund", principal, fee, 404));
        QVERIFY(!journal.appendXmzRefundConfirmed("wrong-swap", principal, fee, 404));
        QVERIFY(!journal.appendXmzRefundConfirmed("swap-refund", fee, principal, 404));
        const auto refunded = journal.refundedEvidence("swap-refund", quote,
            principal, fee, tracker, observation.broadcastCommitment());
        QVERIFY(refunded.accepted());
        QVERIFY(!fills.finalizeRefunded(reservationId, "swap-refund", quote,
            journal, principal, fee, emptyXmzTracker, tracker,
            observation.broadcastCommitment()));
        QVERIFY(fills.finalizeRefunded(reservationId, "swap-refund", quote, journal,
            principal, fee, xmzTracker, tracker, observation.broadcastCommitment()));
        QVERIFY(fills.finalizeRefunded(reservationId, "swap-refund", quote, journal,
            principal, fee, xmzTracker, tracker, observation.broadcastCommitment()));
        QCOMPARE(fills.records().first().toObject().value("state").toString(),
                 QString("refunded"));
        QVERIFY(fills.reserve("cccccccc-cccc-4ccc-8ccc-cccccccccccc",
            buy, sell, "10000", 600, 500).accepted());
    }
    void acceptsSignedObservedParentButNotItsMutation()
    {
        Fixture fixture;
        QJsonArray inputs = fixture.lock.value("vin").toArray();
        QJsonObject input = inputs[0].toObject();
        input["scriptSig"] = QJsonObject{{"asm", "signed"}, {"hex", "01aa"}};
        input["txinwitness"] = QJsonArray{"signature", "script"};
        inputs[0] = input; fixture.lock["vin"] = inputs;
        QVERIFY(validate(fixture, BitcoinRecoveryPsbtEvidence::Branch::Cancel)
                    .value("valid").toBool());
        QJsonArray outputs = fixture.lock.value("vout").toArray();
        QJsonObject output = outputs[0].toObject(); output["value"] = 0.00009999;
        outputs[0] = output; fixture.lock["vout"] = outputs;
        QVERIFY(!validate(fixture, BitcoinRecoveryPsbtEvidence::Branch::Cancel)
                     .value("valid").toBool());
    }
    void rejectsRpcParentSubstitution()
    {
        Fixture fixture;
        serve([fixture](const QJsonObject &request) mutable -> QJsonValue {
            const QString method = request.value("method").toString();
            if (method == "getbestblockhash") return bestBlock;
            if (method == "decodepsbt") {
                QJsonObject child = fixture.cancel; child["vsize"] = 150;
                return psbt(child, 0.00010000, 0.00000300);
            }
            if (method == "getrawtransaction") {
                QJsonObject changed = fixture.lock;
                QJsonArray outputs = changed.value("vout").toArray();
                QJsonObject output = outputs[0].toObject(); output["value"] = 0.00009999;
                outputs[0] = output; changed["vout"] = outputs;
                return changed;
            }
            if (method == "gettxout") return prevout(lockTxid, 0.00010000);
            return QJsonObject{{"feerate", 0.00001000}, {"blocks", 6}};
        });
        QVERIFY(!BitcoinRecoveryPsbtRpcVerifier::validate(client(), fixture.terms,
            BitcoinRecoveryPsbtEvidence::Leg::Pay,
            BitcoinRecoveryPsbtEvidence::Branch::Cancel, 100, psbtBytes(), {},
            lockTxid, "0", "10000", sharedScript, keyA, keyB,
            "shared", sharedScript).value("valid").toBool());
    }
    void rejectsWrongBitcoinDeploymentIdentity()
    {
        Fixture fixture;
        invalidNodeIdentity = true;
        serve([fixture](const QJsonObject &request) mutable -> QJsonValue {
            const QString method = request.value("method").toString();
            if (method == "getbestblockhash") return bestBlock;
            if (method == "decodepsbt") {
                QJsonObject child = fixture.cancel; child["vsize"] = 150;
                return psbt(child, 0.00010000, 0.00000300);
            }
            if (method == "getrawtransaction") return fixture.lock;
            if (method == "gettxout") return prevout(lockTxid, 0.00010000);
            return QJsonObject{{"feerate", 0.00001000}, {"blocks", 6}};
        });
        QVERIFY(!BitcoinRecoveryPsbtRpcVerifier::validate(client(), fixture.terms,
            BitcoinRecoveryPsbtEvidence::Leg::Pay,
            BitcoinRecoveryPsbtEvidence::Branch::Cancel, 100, psbtBytes(), {},
            lockTxid, "0", "10000", sharedScript, keyA, keyB,
            "shared", sharedScript).value("valid").toBool());
    }
    void rejectsRpcTipChangeDuringParentSnapshot()
    {
        Fixture fixture;
        auto tipCalls = std::make_shared<int>(0);
        serve([fixture, tipCalls](const QJsonObject &request) mutable -> QJsonValue {
            const QString method = request.value("method").toString();
            if (method == "getbestblockhash")
                return ++*tipCalls == 1 ? bestBlock : QString(64, 'f');
            if (method == "decodepsbt") {
                QJsonObject child = fixture.cancel; child["vsize"] = 150;
                return psbt(child, 0.00010000, 0.00000300);
            }
            if (method == "getrawtransaction") return fixture.lock;
            return prevout(lockTxid, 0.00010000);
        });
        QVERIFY(!BitcoinRecoveryPsbtRpcVerifier::validate(client(), fixture.terms,
            BitcoinRecoveryPsbtEvidence::Leg::Pay,
            BitcoinRecoveryPsbtEvidence::Branch::Cancel, 100, psbtBytes(), {},
            lockTxid, "0", "10000", sharedScript, keyA, keyB,
            "shared", sharedScript).value("valid").toBool());
    }
    void rejectsMempoolDenialFeeAndIdentityMismatch()
    {
        Fixture fixture;
        auto mempoolCalls = std::make_shared<int>(0);
        serve([fixture, mempoolCalls](const QJsonObject &request) mutable -> QJsonValue {
            const QString method = request.value("method").toString();
            if (method == "getbestblockhash") return bestBlock;
            if (method == "decodepsbt") {
                QJsonObject child = fixture.cancel; child["vsize"] = 150;
                return psbt(child, 0.00010000, 0.00000300);
            }
            if (method == "getrawtransaction") return fixture.lock;
            if (method == "gettxout") return prevout(lockTxid, 0.00010000);
            if (method == "estimatesmartfee")
                return QJsonObject{{"feerate", 0.00001000}, {"blocks", 6}};
            if (method == "testmempoolaccept") {
                const int call = ++*mempoolCalls;
                if (call == 1)
                    return QJsonArray{QJsonObject{{"txid", signedCancelTxid},
                        {"wtxid", signedCancelWtxid}, {"allowed", false},
                        {"reject-reason", "mandatory-script-verify-flag-failed"}}};
                return QJsonArray{QJsonObject{
                    {"txid", call == 3 ? QString(64, 'a') : signedCancelTxid},
                    {"wtxid", signedCancelWtxid}, {"allowed", true},
                    {"vsize", call == 4 ? 150 : 149},
                    {"fees", QJsonObject{{"base", call == 2 ? 0.00000299
                                                               : 0.00000300}}}}};
            }
            return QJsonValue(QJsonValue::Null);
        });
        const auto signedArtifact = signedEvidence(fixture);
        QVERIFY(signedArtifact.accepted());
        for (int i = 0; i < 4; ++i)
            QVERIFY(!BitcoinRecoveryMempoolVerifier::validate(
                client(), signedArtifact, signedCancelBytes()).accepted());
    }
    void rejectsMempoolTipChange()
    {
        Fixture fixture;
        auto tipCalls = std::make_shared<int>(0);
        serve([fixture, tipCalls](const QJsonObject &request) mutable -> QJsonValue {
            const QString method = request.value("method").toString();
            if (method == "getbestblockhash")
                return ++*tipCalls <= 3 ? bestBlock : QString(64, 'f');
            if (method == "decodepsbt") {
                QJsonObject child = fixture.cancel; child["vsize"] = 150;
                return psbt(child, 0.00010000, 0.00000300);
            }
            if (method == "getrawtransaction") return fixture.lock;
            if (method == "gettxout") return prevout(lockTxid, 0.00010000);
            if (method == "estimatesmartfee")
                return QJsonObject{{"feerate", 0.00001000}, {"blocks", 6}};
            if (method == "testmempoolaccept")
                return QJsonArray{QJsonObject{{"txid", signedCancelTxid},
                    {"wtxid", signedCancelWtxid}, {"allowed", true}, {"vsize", 149},
                    {"fees", QJsonObject{{"base", 0.00000300}}}}};
            return QJsonValue(QJsonValue::Null);
        });
        const auto signedArtifact = signedEvidence(fixture);
        QVERIFY(signedArtifact.accepted());
        QVERIFY(!BitcoinRecoveryMempoolVerifier::validate(
            client(), signedArtifact, signedCancelBytes()).accepted());
    }
};

QTEST_GUILESS_MAIN(BitcoinRecoveryPsbtEvidenceTest)
#include "test_bitcoin_recovery_psbt_evidence.moc"
