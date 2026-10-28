#include "BitcoinSignedRecoveryEvidence.h"

#include "BitcoinEcdsaSignatureVerifier.h"
#include "BitcoinRawTransaction.h"
#include "BitcoinSettlementTemplate.h"
#include "BitcoinSharedOutputScript.h"

#include <QCryptographicHash>
#include <QDataStream>

namespace {
QByteArray field(const QByteArray &value)
{
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint32(value.size());
    encoded += value;
    return encoded;
}

}

namespace BitcoinSignedRecoveryEvidence {
Evidence validate(const BitcoinRecoveryPsbtRpcVerifier::Evidence &prepared,
                  const QByteArray &rawTransactionHex)
{
    Evidence evidence;
    evidence.m_details = {{"valid", false}, {"signatureVerified", false},
                          {"witnessVerified", false}, {"executable", false}};
    if (!prepared.accepted()) {
        evidence.m_details["error"] = "Missing prepared Bitcoin recovery evidence";
        return evidence;
    }

    const QVariantMap preparedDetails = prepared.details();
    const QByteArray expectedTemplate = QByteArray::fromHex(
        preparedDetails.value("templateCommitment").toString().toLatin1());
    const QString digest = preparedDetails.value("sighashHex").toString();
    const BitcoinRawTransaction::Result raw =
        BitcoinRawTransaction::parseRecovery(rawTransactionHex);
    if (!raw.valid) {
        evidence.m_details["error"] = raw.error;
        return evidence;
    }
    const QJsonObject &transaction = raw.transaction;
    if (expectedTemplate.size() != 32
        || BitcoinSettlementTemplate::observedCommitment(transaction) != expectedTemplate) {
        evidence.m_details["error"] = "Signed recovery transaction does not match prepared template";
        return evidence;
    }

    const QString firstSignature = raw.witness[0];
    const QString secondSignature = raw.witness[1];
    const QString witnessScript = raw.witness[2];
    if (witnessScript != prepared.witnessScriptHex()) {
        evidence.m_details["error"] = "Recovery witness encoding or script is invalid";
        return evidence;
    }
    const QString scriptPubKey = QString::fromLatin1(
        (QByteArray("\x00\x20", 2) + QCryptographicHash::hash(
             QByteArray::fromHex(witnessScript.toLatin1()), QCryptographicHash::Sha256)).toHex());
    const QVariantMap script = BitcoinSharedOutputScript::validate(
        witnessScript, scriptPubKey, prepared.firstContractKeyHex(),
        prepared.secondContractKeyHex());
    if (!script.value("valid").toBool()) {
        evidence.m_details["error"] = "Recovery witness script does not match prepared keys";
        return evidence;
    }

    const QVariantMap first = BitcoinEcdsaSignatureVerifier::verifyAll(
        digest, prepared.firstContractKeyHex(), firstSignature);
    const QVariantMap second = BitcoinEcdsaSignatureVerifier::verifyAll(
        digest, prepared.secondContractKeyHex(), secondSignature);
    if (!first.value("valid").toBool() || !second.value("valid").toBool()) {
        evidence.m_details["error"] = "Recovery witness signatures do not match ordered contract keys";
        return evidence;
    }

    evidence.m_preparedCommitment = prepared.journalCommitment();
    evidence.m_settlementTermsCommitment = prepared.settlementTermsCommitment();
    evidence.m_leg = prepared.leg();
    evidence.m_branch = prepared.branch();
    evidence.m_chain = prepared.chain();
    evidence.m_txid = raw.txid;
    evidence.m_wtxid = raw.wtxid;
    evidence.m_rawTransactionCommitment = QCryptographicHash::hash(
        QByteArray::fromHex(rawTransactionHex), QCryptographicHash::Sha256);
    evidence.m_feeAtomic = preparedDetails.value("feeAtomic").toString();
    evidence.m_virtualSize = raw.virtualSize;
    evidence.m_requiredConfirmations = prepared.requiredConfirmations();
    evidence.m_expectedGenesis = prepared.expectedGenesis();
    bool feeOk = false;
    const quint64 fee = evidence.m_feeAtomic.toULongLong(&feeOk);
    if (!feeOk || fee == 0 || evidence.m_feeAtomic != QString::number(fee)
        || evidence.m_settlementTermsCommitment.size() != 32
        || evidence.m_requiredConfirmations < 1
        || evidence.m_requiredConfirmations > 1000
        || evidence.m_expectedGenesis.size() != 64)
        return Evidence{};
    QByteArray preimage("MONZERO-DEX-BTC-RECOVERY-SIGNED-V2\n");
    for (const QByteArray &value : {evidence.m_settlementTermsCommitment,
             evidence.m_preparedCommitment, raw.txid.toLatin1(),
             raw.wtxid.toLatin1(), expectedTemplate, digest.toLatin1(),
             firstSignature.toLatin1(), secondSignature.toLatin1(),
             witnessScript.toLatin1(), evidence.m_rawTransactionCommitment,
             evidence.m_feeAtomic.toLatin1(), QByteArray::number(evidence.m_virtualSize),
             QByteArray::number(evidence.m_requiredConfirmations),
             evidence.m_expectedGenesis.toLatin1()})
        preimage += field(value);
    evidence.m_journalCommitment =
        QCryptographicHash::hash(preimage, QCryptographicHash::Sha256);
    evidence.m_details = {{"valid", true}, {"signatureVerified", true},
                          {"witnessVerified", true}, {"templateVerified", true},
                          {"rawIdentityVerified", true},
                          {"txid", raw.txid}, {"wtxid", raw.wtxid},
                          {"vsize", QString::number(evidence.m_virtualSize)},
                          {"executable", false}};
    return evidence;
}
}
