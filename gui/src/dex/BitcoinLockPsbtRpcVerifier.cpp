#include "BitcoinLockPsbtRpcVerifier.h"

#include "BitcoinPsbtRpcVerifier.h"
#include "BitcoinSharedOutputScript.h"
#include "NodeRpcValidator.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QJsonArray>
#include <QRegularExpression>

namespace {
const QRegularExpression hash64("^[0-9a-f]{64}$");
QByteArray field(const QByteArray &value)
{
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint32(value.size());
    encoded += value;
    return encoded;
}

QVariantMap rejected(const QString &error)
{
    return {{"valid", false}, {"termsVerified", false},
            {"nodeIdentityVerified", false}, {"executable", false},
            {"error", error}};
}
}

namespace BitcoinLockPsbtRpcVerifier {
Evidence validate(const ReadOnlyRpcClient &client, const SettlementTerms::Terms &terms,
                  Leg leg, qint64 now, const QByteArray &psbtBase64,
                  const QByteArray &authorization, const QString &expectedAddress,
                  const QString &expectedScriptHex, int feeConfirmationTarget,
                  int maximumFeeHeadroomBasisPoints)
{
    Evidence evidence;
    evidence.m_details = rejected("Invalid signed Bitcoin lock policy");
    const SettlementTerms::Result signedTerms = SettlementTerms::build(terms, now);
    const bool pay = leg == Leg::Pay;
    const QString asset = pay ? terms.payAsset : terms.receiveAsset;
    const QString genesis = pay ? terms.payGenesis : terms.receiveGenesis;
    const QString chain = pay ? terms.payNetwork : terms.receiveNetwork;
    const QByteArray keyA = pay ? terms.payContractKeyA : terms.receiveContractKeyA;
    const QByteArray keyB = pay ? terms.payContractKeyB : terms.receiveContractKeyB;
    const QByteArray lockCommitment = pay ? terms.payLockCommitment
                                          : terms.receiveLockCommitment;
    const QString amount = pay ? terms.payAmountAtomic : terms.receiveAmountAtomic;
    const QString maximumFee = pay ? terms.payNativeFeeMaxAtomic
                                    : terms.receiveNativeFeeMaxAtomic;
    const int confirmations = pay ? terms.payConfirmations : terms.receiveConfirmations;
    const QString keyAHex = QString::fromLatin1(keyA.toHex());
    const QString keyBHex = QString::fromLatin1(keyB.toHex());
    const QVariantMap script = BitcoinSharedOutputScript::validateProgram(
        expectedScriptHex, keyAHex, keyBHex);
    if (!signedTerms.valid || asset != "BTC" || chain != "btc-regtest"
        || lockCommitment.size() != 32 || script.value("valid") != QVariant(true))
        return evidence;

    const auto info = client.call("getblockchaininfo", QJsonArray{}, authorization);
    const auto observedGenesis = client.call("getblockhash", QJsonArray{0}, authorization);
    if (!info.ok || !info.payload.isObject() || !observedGenesis.ok
        || !observedGenesis.payload.isString()
        || !hash64.match(info.payload.toObject().value("bestblockhash").toString()).hasMatch()
        || !NodeRpcValidator::bitcoin(info.payload.toObject(),
                                      observedGenesis.payload.toString(), genesis)
                .value("ready").toBool()) {
        evidence.m_details = rejected("Bitcoin lock node identity or synchronization mismatch");
        return evidence;
    }

    evidence.m_details = BitcoinPsbtRpcVerifier::validateLock(client, psbtBase64,
        authorization, lockCommitment, expectedAddress, expectedScriptHex,
        keyAHex, keyBHex, amount, maximumFee, confirmations,
        feeConfirmationTarget, maximumFeeHeadroomBasisPoints);
    if (!evidence.m_details.value("valid").toBool()
        || !evidence.m_details.value("rpcVerified").toBool()
        || !evidence.m_details.value("provenanceVerified").toBool()
        || !evidence.m_details.value("scriptVerified").toBool()
        || !evidence.m_details.value("feeEstimateVerified").toBool()
        || evidence.m_details.value("executable").toBool()) return Evidence{};

    const auto finalInfo = client.call("getblockchaininfo", QJsonArray{}, authorization);
    const auto finalGenesis = client.call("getblockhash", QJsonArray{0}, authorization);
    if (!finalInfo.ok || !finalInfo.payload.isObject() || !finalGenesis.ok
        || !finalGenesis.payload.isString()
        || !NodeRpcValidator::bitcoin(finalInfo.payload.toObject(),
                                      finalGenesis.payload.toString(), genesis)
                .value("ready").toBool()) return Evidence{};

    const QByteArray bestBlock = QByteArray::fromHex(
        evidence.m_details.value("bestBlock").toString().toLatin1());
    const QByteArray actualTemplate = QByteArray::fromHex(
        evidence.m_details.value("templateCommitment").toString().toLatin1());
    if (bestBlock.size() != 32 || actualTemplate != lockCommitment
        || info.payload.toObject().value("bestblockhash")
            != evidence.m_details.value("bestBlock").toString()
        || finalInfo.payload.toObject().value("bestblockhash")
            != evidence.m_details.value("bestBlock").toString()
        || finalGenesis.payload != observedGenesis.payload) return Evidence{};
    evidence.m_termsCommitment = signedTerms.commitment;
    evidence.m_leg = pay ? "pay" : "receive";
    evidence.m_chain = chain;
    QByteArray preimage("MONZERO-DEX-BTC-LOCK-PREPARED-V1\n");
    for (const QByteArray &value : {evidence.m_termsCommitment,
             evidence.m_leg.toUtf8(), evidence.m_chain.toUtf8(), lockCommitment,
             bestBlock, expectedAddress.toUtf8(), expectedScriptHex.toLatin1(),
             keyA, keyB, amount.toLatin1(), maximumFee.toLatin1(),
             QByteArray::number(confirmations),
             evidence.m_details.value("feeAtomic").toString().toLatin1(),
             evidence.m_details.value("networkPolicyCeilingAtomic").toString().toLatin1()})
        preimage += field(value);
    evidence.m_journalCommitment =
        QCryptographicHash::hash(preimage, QCryptographicHash::Sha256);
    evidence.m_details["termsVerified"] = true;
    evidence.m_details["nodeIdentityVerified"] = true;
    evidence.m_details["executable"] = false;
    return evidence;
}
}
