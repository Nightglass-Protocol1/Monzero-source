#include "../CrossCurveProofEvidence.h"
#include "../CryptoVerifierClient.h"
#include "../SignedQuoteAcceptor.h"
#include "../OfferIdentity.h"

#include <QtTest>
#include <QFile>
#include <QJsonDocument>

namespace {
const QString verifierHash =
    "eda07a44e9ce9dd2d5a3a3265081e266ea4da73350921a9c64c7f5a81ee6d9a5";
const QByteArray makerProof("maker-proof");
const QByteArray takerProof("taker-proof");

SettlementTerms::Terms terms()
{
    SettlementTerms::Terms t;
    t.protocolId = "btc-xmz-adaptor-draft-v1";
    t.payAsset = "BTC"; t.receiveAsset = "XMZ";
    t.payGenesis = QString(64, 'a');
    t.receiveGenesis = t.xmzGenesis = QString(64, 'b');
    t.payNetwork = "btc-regtest"; t.receiveNetwork = t.xmzNetwork = "xmz-fakechain";
    t.payAmountAtomic = "100"; t.receiveAmountAtomic = "1000"; t.feeAtomic = "1";
    t.payNativeFeeMaxAtomic = "10"; t.receiveNativeFeeMaxAtomic = "11";
    t.xmzNativeFeeMaxAtomic = "12";
    t.payRecipient = "pay"; t.receiveRecipient = "receive"; t.feeRecipient = "fee";
    t.payRefundRecipient = "pay-refund"; t.receiveRefundRecipient = "receive-refund";
    t.xmzRefundRecipient = "fee-refund";
    t.payAnchorHeight = "100"; t.receiveAnchorHeight = "200"; t.xmzAnchorHeight = "200";
    t.payRefundHeight = "120"; t.receiveRefundHeight = "230"; t.xmzRefundHeight = "240";
    t.payConfirmations = 3; t.receiveConfirmations = 5; t.xmzConfirmations = 5;
    t.payLockCommitment = QByteArray(32, 'l'); t.payRefundCommitment = QByteArray(32, 'r');
    t.receiveLockCommitment = QByteArray(32, 'm'); t.receiveRefundCommitment = QByteArray(32, 's');
    t.xmzFeeLockCommitment = QByteArray(32, 'f'); t.xmzFeeRefundCommitment = QByteArray(32, 't');
    t.payRedeemCommitment = QByteArray(32, 'u');
    t.payCancelCommitment = QByteArray(32, 'v');
    t.payPunishCommitment = QByteArray(32, 'w');
    t.payCancelBlocks = 72; t.payPunishBlocks = 72;
    t.payContractKeyA = QByteArray::fromHex("0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798");
    t.payContractKeyB = QByteArray::fromHex("02c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5");
    t.makerAdaptorSecpKey = t.payContractKeyA; t.takerAdaptorSecpKey = t.payContractKeyB;
    t.makerSwapEd25519Key = QByteArray::fromHex("5866666666666666666666666666666666666666666666666666666666666666");
    t.takerSwapEd25519Key = QByteArray::fromHex("c9a3f86aae465f0e56513864510f3997561fa2c9e85ea21dc2292309f3cd6022");
    t.makerDleqProofCommitment = CryptoVerifierProtocol::dleqProofCommitment(makerProof);
    t.takerDleqProofCommitment = CryptoVerifierProtocol::dleqProofCommitment(takerProof);
    t.makerKey = QByteArray(32, 'a'); t.takerKey = QByteArray(32, 'b');
    t.payerKey = t.takerKey;
    t.nonce = QByteArray(32, 'c');
    t.offerFillReservationCommitment = QByteArray(32, 'f');
    t.quoteExpiry = 200; t.refundDeadline = 300;
    t.feeFromAcquiredXmz = true;
    return t;
}
}

class CrossCurveProofEvidenceTest : public QObject
{
    Q_OBJECT
private slots:
    void verifiesLocalGeneratedPositiveProof()
    {
        QFile file(QFINDTESTDATA("fixtures/local-cross-curve-dleq.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto fixture = QJsonDocument::fromJson(file.readAll()).object();
        const auto proof = QByteArray::fromBase64(fixture.value("proofBase64").toString().toLatin1());
        QVERIFY(!proof.isEmpty());
        auto t = terms();
        t.makerAdaptorSecpKey = QByteArray::fromHex(fixture.value("secp256k1PublicKeyHex").toString().toLatin1());
        t.makerSwapEd25519Key = QByteArray::fromHex(fixture.value("ed25519PublicKeyHex").toString().toLatin1());
        t.makerDleqProofCommitment = CryptoVerifierProtocol::dleqProofCommitment(proof);
        const auto prepared = CrossCurveProofEvidence::prepare(t,
            CrossCurveProofEvidence::Party::Maker, proof, QByteArray(32, 'n'), 100);
        QVERIFY2(prepared.valid, qPrintable(prepared.error));
        const QString verifier = qEnvironmentVariable("MONZERO_DEX_CRYPTO_VERIFIER");
        if (verifier.isEmpty()) QSKIP("Pinned Rust verifier path was not supplied");
        CryptoVerifierClient client(verifier, verifierHash);
        const auto evidence = CrossCurveProofEvidence::verifyEvidence(t,
            CrossCurveProofEvidence::Party::Maker, proof, QByteArray(32, 'n'), 100, client);
        QVERIFY2(evidence.accepted(), qPrintable(evidence.details().value("error").toString()));
        QCOMPARE(evidence.settlementTermsCommitment(), prepared.settlementTermsCommitment);
        QCOMPARE(evidence.journalCommitment().size(), 32);
        QVERIFY(evidence.details().value("cryptographicallyVerified").toBool());
        QVERIFY(evidence.details().value("responseAuthenticated").toBool());
        QVERIFY(evidence.details().value("sandboxed").toBool());
        QVERIFY(!evidence.details().value("executable").toBool());

        // Recovery must survive a process-style reopen and quote expiry without
        // allowing the expired terms through the fresh verification path.
        OfferIdentity maker, taker;
        t.makerKey = maker.publicKey(); t.takerKey = taker.publicKey();
        t.payerKey = t.takerKey;
        QTemporaryDir state;
        QVERIFY(state.isValid());
        const QByteArray domain("dleq-recovery-test");
        const auto built = SettlementTerms::build(t, 100);
        QVERIFY(built.valid);
        const auto makerSignature = maker.sign(built.payload, domain);
        const auto takerSignature = taker.sign(
            SignedQuoteAcceptor::takerAcceptancePayload(t, 100), domain);
        {
            SignedQuoteAcceptor acceptor(state.filePath("quotes.json"), t.protocolId,
                t.payNetwork, t.receiveNetwork, t.xmzNetwork, domain);
            QVERIFY(acceptor.acceptMutualEvidence(t, makerSignature, takerSignature,
                100).mutuallyAuthorized());
        }
        SignedQuoteAcceptor reopened(state.filePath("quotes.json"), t.protocolId,
            t.payNetwork, t.receiveNetwork, t.xmzNetwork, domain);
        const auto recovered = reopened.recoverMutualQuote(t.makerKey, t.nonce, 400);
        QVERIFY(recovered.valid());
        QVERIFY(!reopened.acceptMutualEvidence(t, makerSignature, takerSignature, 400).accepted());
        QVERIFY(!CrossCurveProofEvidence::verifyEvidence(t,
            CrossCurveProofEvidence::Party::Maker, proof, QByteArray(32, 'r'), 400, client).accepted());
        const auto recovery = CrossCurveProofEvidence::verifyRecoveredEvidence(recovered,
            CrossCurveProofEvidence::Party::Maker, proof, QByteArray(32, 'r'), 400, client);
        QVERIFY2(recovery.accepted(), qPrintable(recovery.details().value("error").toString()));
        QCOMPARE(recovery.settlementTermsCommitment(), recovered.commitment());
        QVERIFY(recovery.details().value("recoveryOnly").toBool());
        QVERIFY(!recovery.details().value("executable").toBool());
        QVERIFY(!CrossCurveProofEvidence::verifyRecoveredEvidence(recovered,
            CrossCurveProofEvidence::Party::Taker, proof, QByteArray(32, 'r'), 400, client).accepted());
        QVERIFY(!CrossCurveProofEvidence::verifyRecoveredEvidence(recovered,
            CrossCurveProofEvidence::Party::Maker, proof, t.nonce, 400, client).accepted());
        QVERIFY(!CrossCurveProofEvidence::verifyRecoveredEvidence(recovered,
            CrossCurveProofEvidence::Party::Maker, proof, QByteArray(32, 'r'), 99, client).accepted());
        QVERIFY(!CrossCurveProofEvidence::verifyRecoveredEvidence({},
            CrossCurveProofEvidence::Party::Maker, proof, QByteArray(32, 'r'), 400, client).accepted());
        auto changed = proof;
        changed[changed.size() - 1] = char(changed.at(changed.size() - 1) ^ 1);
        QVERIFY(!CrossCurveProofEvidence::verifyRecoveredEvidence(recovered,
            CrossCurveProofEvidence::Party::Maker, changed, QByteArray(32, 'r'), 400, client).accepted());
        t.makerDleqProofCommitment = CryptoVerifierProtocol::dleqProofCommitment(changed);
        QVERIFY(!CrossCurveProofEvidence::verifyEvidence(t,
            CrossCurveProofEvidence::Party::Maker, changed, QByteArray(32, 'o'), 100, client).accepted());
    }

    void bindsEachPartyToSignedTerms()
    {
        const auto t = terms();
        const auto maker = CrossCurveProofEvidence::prepare(t,
            CrossCurveProofEvidence::Party::Maker, makerProof, QByteArray(32, 'n'), 100);
        const auto taker = CrossCurveProofEvidence::prepare(t,
            CrossCurveProofEvidence::Party::Taker, takerProof, QByteArray(32, 'o'), 100);
        QVERIFY2(maker.valid, qPrintable(maker.error));
        QVERIFY2(taker.valid, qPrintable(taker.error));
        QCOMPARE(maker.party, QString("maker")); QCOMPARE(taker.party, QString("taker"));
        QCOMPARE(maker.settlementTermsCommitment, taker.settlementTermsCommitment);
        QVERIFY(maker.request.commitment != taker.request.commitment);
        QVERIFY(!maker.request.message.value("proofBase64").toString().isEmpty());
    }
    void rejectsSubstitutionExpiryAndNonceReuse()
    {
        auto t = terms();
        QVERIFY(!CrossCurveProofEvidence::prepare(t,
            static_cast<CrossCurveProofEvidence::Party>(99), takerProof,
            QByteArray(32, 'n'), 100).valid);
        QVERIFY(!CrossCurveProofEvidence::prepare(t,
            CrossCurveProofEvidence::Party::Maker, takerProof, QByteArray(32, 'n'), 100).valid);
        QVERIFY(!CrossCurveProofEvidence::prepare(t,
            CrossCurveProofEvidence::Party::Maker, makerProof, t.nonce, 100).valid);
        QVERIFY(!CrossCurveProofEvidence::prepare(t,
            CrossCurveProofEvidence::Party::Maker, makerProof, QByteArray(32, 'n'), 200).valid);
        t.makerAdaptorSecpKey = t.takerAdaptorSecpKey;
        QVERIFY(!CrossCurveProofEvidence::prepare(t,
            CrossCurveProofEvidence::Party::Maker, makerProof, QByteArray(32, 'n'), 100).valid);
    }
    void remainsNonExecutableWhenVerifierUnavailable()
    {
        CryptoVerifierClient unavailable("/does/not/exist", QString(64, '0'));
        const auto evidence = CrossCurveProofEvidence::verifyEvidence(terms(),
            CrossCurveProofEvidence::Party::Maker, makerProof, QByteArray(32, 'n'),
            100, unavailable);
        const QVariantMap result = evidence.details();
        QVERIFY(!evidence.accepted());
        QVERIFY(result.value("termsBound").toBool());
        QVERIFY(!result.value("responseAuthenticated").toBool());
        QVERIFY(!result.value("cryptographicallyVerified").toBool());
        QVERIFY(!result.value("executable").toBool());
        QCOMPARE(result.value("settlementTermsCommitment").toString().size(), 64);
    }
    void authenticatesRejectedProofWithoutLosingTermsBinding()
    {
        const QString verifier = qEnvironmentVariable("MONZERO_DEX_CRYPTO_VERIFIER");
        if (verifier.isEmpty()) QSKIP("Pinned Rust verifier path was not supplied");
        CryptoVerifierClient client(verifier, verifierHash);
        const auto evidence = CrossCurveProofEvidence::verifyEvidence(terms(),
            CrossCurveProofEvidence::Party::Maker, makerProof, QByteArray(32, 'n'),
            100, client);
        const QVariantMap result = evidence.details();
        QVERIFY(!evidence.accepted());
        QVERIFY2(result.value("valid").toBool(), qPrintable(result.value("error").toString()));
        QVERIFY(result.value("termsBound").toBool());
        QVERIFY(result.value("responseAuthenticated").toBool());
        QVERIFY(result.value("sandboxed").toBool());
        QVERIFY(!result.value("cryptographicallyVerified").toBool());
        QVERIFY(!result.value("executable").toBool());
        QCOMPARE(result.value("party").toString(), QString("maker"));
        QCOMPARE(result.value("proofCommitment").toString(),
                 QString::fromLatin1(terms().makerDleqProofCommitment.toHex()));
    }
};

QTEST_GUILESS_MAIN(CrossCurveProofEvidenceTest)
#include "test_cross_curve_proof_evidence.moc"
