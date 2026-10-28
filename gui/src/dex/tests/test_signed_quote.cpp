#include "../OfferIdentity.h"
#include "../SignedQuoteAcceptor.h"
#include <QTemporaryDir>
#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>

namespace {
SignedQuoteAcceptor acceptorAt(const QString &path)
{
    return SignedQuoteAcceptor(path, "btc-xmz-adaptor-draft-v1", "btc-regtest",
                               "xmz-fakechain", "xmz-fakechain", "xmz-fakechain-lab-v1");
}
SettlementTerms::Terms termsFor(const OfferIdentity &maker)
{
    SettlementTerms::Terms terms;
    terms.protocolId = "btc-xmz-adaptor-draft-v1";
    terms.payAsset = "BTC"; terms.receiveAsset = "XMZ";
    terms.payGenesis = QString(64, 'a');
    terms.receiveGenesis = terms.xmzGenesis = QString(64, 'b');
    terms.payAmountAtomic = "100"; terms.receiveAmountAtomic = "1000"; terms.feeAtomic = "1";
    terms.payNativeFeeMaxAtomic = "10"; terms.receiveNativeFeeMaxAtomic = "11";
    terms.xmzNativeFeeMaxAtomic = "12";
    terms.payRecipient = "pay"; terms.receiveRecipient = "receive"; terms.feeRecipient = "fee";
    terms.payRefundRecipient = "refund-pay"; terms.receiveRefundRecipient = "refund-receive";
    terms.xmzRefundRecipient = "refund-fee";
    terms.payNetwork = "btc-regtest"; terms.receiveNetwork = terms.xmzNetwork = "xmz-fakechain";
    terms.payAnchorHeight = "100"; terms.receiveAnchorHeight = "200"; terms.xmzAnchorHeight = "200";
    terms.payRefundHeight = "120"; terms.receiveRefundHeight = "230"; terms.xmzRefundHeight = "240";
    terms.payConfirmations = 3; terms.receiveConfirmations = 5; terms.xmzConfirmations = 5;
    terms.payLockCommitment = QByteArray(32, 'l'); terms.payRefundCommitment = QByteArray(32, 'r');
    terms.receiveLockCommitment = QByteArray(32, 'm'); terms.receiveRefundCommitment = QByteArray(32, 's');
    terms.xmzFeeLockCommitment = QByteArray(32, 'f'); terms.xmzFeeRefundCommitment = QByteArray(32, 't');
    terms.payRedeemCommitment = QByteArray(32, 'u');
    terms.payCancelCommitment = QByteArray(32, 'v');
    terms.payPunishCommitment = QByteArray(32, 'w');
    terms.payCancelBlocks = 72; terms.payPunishBlocks = 72;
    terms.payContractKeyA = QByteArray::fromHex("0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798");
    terms.payContractKeyB = QByteArray::fromHex("02c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5");
    terms.makerAdaptorSecpKey = terms.payContractKeyA;
    terms.takerAdaptorSecpKey = terms.payContractKeyB;
    terms.makerSwapEd25519Key = QByteArray::fromHex("5866666666666666666666666666666666666666666666666666666666666666");
    terms.takerSwapEd25519Key = QByteArray::fromHex("c9a3f86aae465f0e56513864510f3997561fa2c9e85ea21dc2292309f3cd6022");
    terms.makerDleqProofCommitment = QByteArray(32, 'd');
    terms.takerDleqProofCommitment = QByteArray(32, 'e');
    terms.makerKey = maker.publicKey(); terms.takerKey = QByteArray(32, 't');
    terms.payerKey = terms.takerKey;
    terms.nonce = QByteArray(32, 'n');
    terms.offerFillReservationCommitment = QByteArray(32, 'f');
    terms.quoteExpiry = 200; terms.refundDeadline = 300;
    terms.feeFromAcquiredXmz = true;
    return terms;
}
}

class SignedQuoteTest : public QObject {
    Q_OBJECT
private slots:
    void mutualInventoryDoesNotReturnPartialHistory()
    {
        QTemporaryDir directory;
        OfferIdentity maker, taker;
        auto terms = termsFor(maker); terms.takerKey = taker.publicKey();
        terms.payerKey = terms.takerKey;
        const QByteArray domain("xmz-fakechain-lab-v1");
        auto acceptor = acceptorAt(directory.filePath("quotes.json"));
        QVERIFY(acceptor.acceptMutualEvidence(terms,
            maker.sign(SettlementTerms::build(terms, 100).payload, domain),
            taker.sign(SignedQuoteAcceptor::takerAcceptancePayload(terms, 100), domain), 100).mutuallyAuthorized());
        QVector<RecoveredMutualQuote> history;
        QVERIFY(acceptor.recoverMutualHistory(500, history));
        QCOMPARE(history.size(), 1);
        const auto originalCommitment = history.first().commitment();
        terms.nonce = QByteArray(32, 'z');
        QVERIFY(acceptor.acceptEvidence(terms,
            maker.sign(SettlementTerms::build(terms, 100).payload, domain), 100).accepted());
        // A valid mutual record followed by an unsigned-artifact legacy record
        // must not yield a partial inventory or overwrite the caller's output.
        QVERIFY(!acceptor.recoverMutualHistory(500, history));
        QCOMPARE(history.size(), 1);
        QCOMPARE(history.first().commitment(), originalCommitment);
        QCOMPARE(acceptor.store().records().size(), 2);
    }

    void recoversExpiredMutualQuoteWithoutFreshAcceptance()
    {
        QTemporaryDir directory;
        OfferIdentity maker, taker;
        auto terms = termsFor(maker); terms.takerKey = taker.publicKey();
        terms.payerKey = terms.takerKey;
        const QByteArray domain("xmz-fakechain-lab-v1");
        const auto makerSig = maker.sign(SettlementTerms::build(terms, 100).payload, domain);
        const auto takerSig = taker.sign(SignedQuoteAcceptor::takerAcceptancePayload(terms, 100), domain);
        const auto path = directory.filePath("replay.json");
        {
            auto acceptor = acceptorAt(path);
            QVector<RecoveredMutualQuote> empty;
            QVERIFY(acceptor.recoverMutualHistory(100, empty));
            QVERIFY(empty.isEmpty());
            QVERIFY(!acceptor.recoverMutualQuote(terms.makerKey, terms.nonce, 100).valid());
            QVERIFY(acceptor.acceptMutualEvidence(terms, makerSig, takerSig, 100).mutuallyAuthorized());
        }
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto original = file.readAll(); file.close();
        {
            auto restored = acceptorAt(path);
            const auto recovered = restored.recoverMutualQuote(terms.makerKey, terms.nonce, 500);
            QVERIFY(recovered.valid());
            QVector<RecoveredMutualQuote> history;
            QVERIFY(restored.recoverMutualHistory(500, history));
            QCOMPARE(history.size(), 1);
            QCOMPARE(history.first().commitment(), recovered.commitment());
            QVERIFY(!restored.recoverMutualHistory(99, history));
            QCOMPARE(history.size(), 1);
            QCOMPARE(history.first().commitment(), recovered.commitment());
            QCOMPARE(recovered.acceptedAt(), qint64(100));
            QCOMPARE(recovered.commitment(), SettlementTerms::build(terms, 100).commitment);
            QVERIFY(!restored.recoverMutualQuote(terms.makerKey, terms.nonce, 99).valid());
            QVERIFY(!restored.recoverMutualQuote(taker.publicKey(), terms.nonce, 500).valid());
            QVERIFY(!restored.acceptMutualEvidence(terms, makerSig, takerSig, 500).accepted());
        }
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), original); file.close();
        auto root = QJsonDocument::fromJson(original).object();
        auto records = root.value("records").toArray();
        auto record = records[0].toObject();
        auto artifact = QJsonDocument::fromJson(QByteArray::fromBase64(record.value("signedArtifact")
            .toString().toLatin1())).object();
        artifact["takerSignature"] = QString(128, '0');
        record["signedArtifact"] = QString::fromLatin1(QJsonDocument(artifact).toJson(QJsonDocument::Compact).toBase64());
        records[0] = record; root["records"] = records;
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const auto altered = QJsonDocument(root).toJson(QJsonDocument::Compact);
        QCOMPARE(file.write(altered), qint64(altered.size())); file.close();
        auto corrupted = acceptorAt(path);
        QVERIFY(!corrupted.recoverMutualQuote(terms.makerKey, terms.nonce, 500).valid());
        QVector<RecoveredMutualQuote> invalidHistory;
        QVERIFY(!corrupted.recoverMutualHistory(500, invalidHistory));
        QVERIFY(invalidHistory.isEmpty());
    }

    void mutualWireMessageValidatesBeforeReplayPersistence()
    {
        QTemporaryDir directory;
        OfferIdentity maker, taker;
        auto terms = termsFor(maker); terms.takerKey = taker.publicKey();
        terms.payerKey = terms.takerKey;
        const QByteArray domain("xmz-fakechain-lab-v1");
        const auto makerSig = maker.sign(SettlementTerms::build(terms, 100).payload, domain);
        const auto takerSig = taker.sign(SignedQuoteAcceptor::takerAcceptancePayload(terms, 100), domain);
        auto sender = acceptorAt(directory.filePath("sender.json"));
        auto receiver = acceptorAt(directory.filePath("receiver.json"));
        const auto wire = sender.encodeMutualMessage(terms, makerSig, takerSig, 100);
        QVERIFY(!wire.isEmpty());
        QVERIFY(!sender.store().contains(terms.makerKey, terms.nonce));
        QVERIFY(!receiver.acceptMutualMessage(wire + " ", 100).accepted());
        QVERIFY(!receiver.acceptMutualMessage(wire.left(wire.size() - 1), 100).accepted());
        auto changed = QJsonDocument::fromJson(wire).object();
        changed["takerSignature"] = QString(128, '0');
        QVERIFY(!receiver.acceptMutualMessage(QJsonDocument(changed).toJson(QJsonDocument::Compact), 100).accepted());
        QVERIFY(!receiver.acceptMutualMessage(QByteArray(98305, 'x'), 100).accepted());
        QVERIFY(!receiver.store().contains(terms.makerKey, terms.nonce));
        const auto accepted = receiver.acceptMutualMessage(wire, 100);
        QVERIFY(accepted.mutuallyAuthorized());
        QCOMPARE(accepted.commitment(), SettlementTerms::build(terms, 100).commitment);
        QVERIFY(!accepted.details().value("executable").toBool());
        QVERIFY(receiver.acceptMutualMessage(wire, 101).mutuallyAuthorized());
        QVERIFY(!receiver.acceptMutualMessage(wire, 200).accepted());
        SignedQuoteAcceptor wrong(directory.filePath("wrong.json"), terms.protocolId,
            "wrong-network", terms.receiveNetwork, terms.xmzNetwork, domain);
        QVERIFY(!wrong.acceptMutualMessage(wire, 100).accepted());
        QVERIFY(!wrong.store().contains(terms.makerKey, terms.nonce));
    }

    void canonicalTermsDecodeRejectsPartialAndAlteredEncoding()
    {
        OfferIdentity maker;
        const auto terms = termsFor(maker);
        const auto payload = SettlementTerms::build(terms, 100).payload;
        SettlementTerms::Terms decoded;
        QVERIFY(SettlementTerms::decode(payload, 100, decoded));
        QCOMPARE(SettlementTerms::build(decoded, 100).payload, payload);
        for (int size = 0; size < payload.size(); ++size) {
            decoded.protocolId = "unchanged";
            QVERIFY(!SettlementTerms::decode(payload.left(size), 100, decoded));
            QCOMPARE(decoded.protocolId, QString("unchanged"));
        }
        QVERIFY(!SettlementTerms::decode(payload + "0:", 100, decoded));
        QVERIFY(!SettlementTerms::decode(QByteArray(65537, 'x'), 100, decoded));
        QVERIFY(!SettlementTerms::decode(payload, 200, decoded));
        auto noncanonical = payload;
        noncanonical.insert(QByteArray("MONZERO-DEX-SETTLEMENT-TERMS-V6\n").size(), '0');
        QVERIFY(!SettlementTerms::decode(noncanonical, 100, decoded));
    }

    void requiresDistinctTakerAcceptanceBeforePersisting()
    {
        QTemporaryDir directory;
        OfferIdentity maker, taker, other;
        const QByteArray domain("xmz-fakechain-lab-v1");
        auto terms = termsFor(maker); terms.takerKey = taker.publicKey();
        terms.payerKey = terms.takerKey;
        const auto built = SettlementTerms::build(terms, 100);
        QVERIFY(built.valid);
        const auto makerSignature = maker.sign(built.payload, domain);
        const auto payload = SignedQuoteAcceptor::takerAcceptancePayload(terms, 100);
        QVERIFY(!payload.isEmpty());
        const auto takerSignature = taker.sign(payload, domain);
        const auto path = directory.filePath("replay.json");
        {
            auto acceptor = acceptorAt(path);
            QVERIFY(!acceptor.acceptMutualEvidence(terms, makerSignature, taker.sign(built.payload, domain), 100).accepted());
            QVERIFY(!acceptor.acceptMutualEvidence(terms, makerSignature, other.sign(payload, domain), 100).accepted());
            QVERIFY(!acceptor.acceptMutualEvidence(terms, makerSignature, taker.sign(payload, "wrong-domain"), 100).accepted());
            QVERIFY(!acceptor.acceptMutualEvidence(terms, {}, takerSignature, 100).accepted());
            QVERIFY(!acceptor.store().contains(terms.makerKey, terms.nonce));
            const auto accepted = acceptor.acceptMutualEvidence(terms, makerSignature, takerSignature, 100);
            QVERIFY(accepted.mutuallyAuthorized());
            QCOMPARE(QByteArray::fromBase64(acceptor.store().records().at(0).toObject()
                .value("signedArtifact").toString().toLatin1()),
                acceptor.encodeMutualMessage(terms, makerSignature, takerSignature, 100));
            QVERIFY(!accepted.details().value("executable").toBool());
            auto changed = terms; changed.feeRecipient = "different-fee";
            QVERIFY(!acceptor.acceptMutualEvidence(changed, makerSignature, takerSignature, 100).accepted());
        }
        auto restored = acceptorAt(path);
        QCOMPARE(QByteArray::fromBase64(restored.store().records().at(0).toObject()
            .value("signedArtifact").toString().toLatin1()),
            restored.encodeMutualMessage(terms, makerSignature, takerSignature, 101));
        QVERIFY(restored.acceptMutualEvidence(terms, makerSignature, takerSignature, 101).mutuallyAuthorized());
        QVERIFY(!restored.acceptEvidence(terms, makerSignature, 101).mutuallyAuthorized());
        QVERIFY(!restored.acceptMutualEvidence(terms, makerSignature, takerSignature, 200).accepted());
    }

    void authenticatesAndRejectsChangedReplay() {
        QTemporaryDir directory;
        OfferIdentity maker;
        const QByteArray network("xmz-fakechain-lab-v1");
        const auto terms = termsFor(maker);
        const auto built = SettlementTerms::build(terms, 100);
        QVERIFY(built.valid);
        const QByteArray signature = maker.sign(built.payload, network);
        QVERIFY(!signature.isEmpty());

        SignedQuoteAcceptor acceptor = acceptorAt(directory.filePath("replay.json"));
        const QVariantMap accepted = acceptor.accept(terms, signature, 100);
        QVERIFY(accepted.value("valid").toBool());
        QVERIFY(accepted.value("accepted").toBool());
        QVERIFY(!accepted.value("executable").toBool());
        QCOMPARE(accepted.value("commitment").toString(), QString::fromLatin1(built.commitment.toHex()));
        QVERIFY(acceptor.accept(terms, signature, 101).value("accepted").toBool());
        const AcceptedQuote acceptedEvidence = acceptor.acceptEvidence(terms, signature, 101);
        QVERIFY(acceptedEvidence.accepted());
        QCOMPARE(acceptedEvidence.commitment(), built.commitment);
        QVERIFY(!acceptedEvidence.details().value("executable").toBool());

        auto changed = terms;
        changed.feeRecipient = "changed-fee";
        const auto changedBuilt = SettlementTerms::build(changed, 101);
        const QByteArray changedSignature = maker.sign(changedBuilt.payload, network);
        QVERIFY(!acceptor.accept(changed, changedSignature, 101).value("accepted").toBool());
        changed = terms;
        changed.payContractKeyA[1] = static_cast<char>(changed.payContractKeyA.at(1) ^ 1);
        const auto changedKeyBuilt = SettlementTerms::build(changed, 101);
        QVERIFY(changedKeyBuilt.valid);
        QVERIFY(!acceptor.accept(changed, maker.sign(changedKeyBuilt.payload, network), 101)
                     .value("accepted").toBool());
        changed = terms; changed.makerDleqProofCommitment[0] = 'x';
        const auto changedProofBuilt = SettlementTerms::build(changed, 101);
        QVERIFY(changedProofBuilt.valid);
        QVERIFY(!acceptor.accept(changed, maker.sign(changedProofBuilt.payload, network), 101)
                     .value("accepted").toBool());
    }

    void rejectsTamperingWrongDomainAndExpiry() {
        QTemporaryDir directory;
        OfferIdentity maker;
        const QByteArray network("xmz-fakechain-lab-v1");
        const auto terms = termsFor(maker);
        const auto built = SettlementTerms::build(terms, 100);
        QByteArray signature = maker.sign(built.payload, network);
        SignedQuoteAcceptor acceptor = acceptorAt(directory.filePath("replay.json"));
        signature[0] = static_cast<char>(signature.at(0) ^ 1);
        QVERIFY(!acceptor.accept(terms, signature, 100).value("accepted").toBool());
        QVERIFY(!acceptor.acceptEvidence(terms, signature, 100).accepted());
        const QByteArray validSignature = maker.sign(built.payload, network);
        const QByteArray wrongDomainSignature = maker.sign(built.payload, "other-network");
        QVERIFY(!acceptor.accept(terms, wrongDomainSignature, 100).value("accepted").toBool());
        QVERIFY(!acceptor.accept(terms, validSignature, 200).value("accepted").toBool());
        auto wrongProtocol = terms; wrongProtocol.protocolId = "other-protocol";
        const auto wrongBuilt = SettlementTerms::build(wrongProtocol, 100);
        QVERIFY(!acceptor.accept(wrongProtocol, maker.sign(wrongBuilt.payload, network), 100)
            .value("accepted").toBool());
        QVERIFY(!acceptor.store().contains(terms.makerKey, terms.nonce));
    }

    void persistsAcceptanceAcrossRestart() {
        QTemporaryDir directory;
        const QString path = directory.filePath("replay.json");
        OfferIdentity maker;
        const QByteArray network("xmz-fakechain-lab-v1");
        const auto terms = termsFor(maker);
        const auto built = SettlementTerms::build(terms, 100);
        const QByteArray signature = maker.sign(built.payload, network);
        { SignedQuoteAcceptor first = acceptorAt(path); QVERIFY(first.accept(terms, signature, 100).value("accepted").toBool()); }
        SignedQuoteAcceptor restored = acceptorAt(path);
        QVERIFY(restored.store().contains(terms.makerKey, terms.nonce));
        QVERIFY(restored.accept(terms, signature, 101).value("accepted").toBool());
    }
};

QTEST_GUILESS_MAIN(SignedQuoteTest)
#include "test_signed_quote.moc"
