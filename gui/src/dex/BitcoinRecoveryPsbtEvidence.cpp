#include "BitcoinRecoveryPsbtEvidence.h"

#include "BitcoinSettlementTemplate.h"
#include "BitcoinSpendPsbtEvidence.h"

#include <QJsonValue>
#include <limits>

namespace {
QVariantMap rejected(const QString &error)
{
    return {{"valid", false}, {"branchPolicyVerified", false},
            {"executable", false}, {"error", error}};
}
}

namespace BitcoinRecoveryPsbtEvidence {
QVariantMap validate(const SettlementTerms::Terms &terms, Leg leg, Branch branch,
                     qint64 now, const QJsonObject &psbt,
                     const QJsonObject &parentTransaction,
                     const QJsonObject &parentPrevout,
                     const QString &expectedParentTxid,
                     const QString &expectedParentVout,
                     const QString &expectedParentAmount,
                     const QString &expectedParentScript,
                     const QString &firstContractKeyHex,
                     const QString &secondContractKeyHex,
                     const QString &expectedRecipient,
                     const QString &expectedRecipientScript,
                     const QString &expectedBestBlock)
{
    if (now < 0) return rejected("Invalid recovery observation time");
    if (leg != Leg::Pay && leg != Leg::Receive)
        return rejected("Unknown recovery leg");
    if (branch != Branch::Redeem && branch != Branch::Cancel
        && branch != Branch::Refund && branch != Branch::Punish)
        return rejected("Unknown recovery branch");
    const SettlementTerms::Result signedTerms = SettlementTerms::rebuildAccepted(terms);
    if (!signedTerms.valid) return rejected("Invalid V6 settlement terms: " + signedTerms.error);
    const bool pay = leg == Leg::Pay;
    const QString asset = pay ? terms.payAsset : terms.receiveAsset;
    if (asset == "XMZ") return rejected("XMZ is not a UTXO recovery leg");

    const QByteArray lock = pay ? terms.payLockCommitment : terms.receiveLockCommitment;
    const QByteArray redeem = pay ? terms.payRedeemCommitment : terms.receiveRedeemCommitment;
    const QByteArray cancel = pay ? terms.payCancelCommitment : terms.receiveCancelCommitment;
    const QByteArray refund = pay ? terms.payRefundCommitment : terms.receiveRefundCommitment;
    const QByteArray punish = pay ? terms.payPunishCommitment : terms.receivePunishCommitment;
    const quint32 cancelBlocks = pay ? terms.payCancelBlocks : terms.receiveCancelBlocks;
    const quint32 punishBlocks = pay ? terms.payPunishBlocks : terms.receivePunishBlocks;
    const int confirmations = pay ? terms.payConfirmations : terms.receiveConfirmations;
    const QString maximumFee = pay ? terms.payNativeFeeMaxAtomic
                                   : terms.receiveNativeFeeMaxAtomic;
    const QByteArray expectedFirstKey = pay ? terms.payContractKeyA
                                            : terms.receiveContractKeyA;
    const QByteArray expectedSecondKey = pay ? terms.payContractKeyB
                                             : terms.receiveContractKeyB;
    if (QByteArray::fromHex(firstContractKeyHex.toLatin1()) != expectedFirstKey
        || QByteArray::fromHex(secondContractKeyHex.toLatin1()) != expectedSecondKey
        || QString::fromLatin1(expectedFirstKey.toHex()) != firstContractKeyHex
        || QString::fromLatin1(expectedSecondKey.toHex()) != secondContractKeyHex)
        return rejected("Recovery contract keys do not match signed V6 terms");

    QByteArray childCommitment, parentCommitment;
    QString sequence, branchName;
    switch (branch) {
    case Branch::Redeem:
        childCommitment = redeem; parentCommitment = lock;
        sequence = QString::number(std::numeric_limits<quint32>::max());
        branchName = "redeem"; break;
    case Branch::Cancel:
        childCommitment = cancel; parentCommitment = lock;
        sequence = QString::number(cancelBlocks);
        branchName = "cancel"; break;
    case Branch::Refund:
        childCommitment = refund; parentCommitment = cancel;
        sequence = QString::number(std::numeric_limits<quint32>::max());
        branchName = "refund"; break;
    case Branch::Punish:
        childCommitment = punish; parentCommitment = cancel;
        sequence = QString::number(punishBlocks);
        branchName = "punish"; break;
    }
    if (parentTransaction.value("txid") != QJsonValue(expectedParentTxid)
        || BitcoinSettlementTemplate::observedCommitment(parentTransaction) != parentCommitment)
        return rejected("Recovery branch parent does not match signed V6 template");

    QVariantMap result = BitcoinSpendPsbtEvidence::validate(psbt, parentPrevout,
        childCommitment, expectedParentTxid, expectedParentVout, expectedParentAmount,
        expectedParentScript, firstContractKeyHex, secondContractKeyHex,
        expectedRecipient, expectedRecipientScript, sequence, maximumFee,
        expectedBestBlock, confirmations);
    if (!result.value("valid").toBool()) return result;
    result["branchPolicyVerified"] = true;
    result["branch"] = branchName;
    result["leg"] = pay ? "pay" : "receive";
    result["settlementTermsCommitment"] =
        QString::fromLatin1(signedTerms.commitment.toHex());
    result["parentTemplateCommitment"] =
        QString::fromLatin1(parentCommitment.toHex());
    result["executable"] = false;
    return result;
}
}
