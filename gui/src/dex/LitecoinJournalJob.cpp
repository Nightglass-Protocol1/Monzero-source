#include "DexDeployment.h"
#include "LitecoinJournalJob.h"
#include "LitecoinJournalProtocol.h"
#include "BackendExecutable.h"
#include "DexUtils.h"
#include <QJsonDocument>
#include <QDateTime>
#include <limits>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QPointer>
#include <QProcessEnvironment>
#include <QTimer>
#include <QUuid>
#include <QRegularExpression>

namespace {
// The XMZ seller may replace its node fields with light Electrum servers
// (SELLER_LIGHT_REDEEM.md). The helper re-checks every rule; this is the shape.
bool lightSellerValid(const QJsonObject &rpc) {
    for (const auto key:{"ltcPort","ltcWallet","ltcAnchor","ltcCookieFile"}) if (rpc.contains(key)) return false;
    const auto light=rpc.value("ltcLight").toObject();const auto servers=light.value("servers").toArray();
    const auto age=light.value("maximumTipAge").toDouble(-1);
    if (light.size()!=2 || servers.size()<2 || servers.size()>8 || age<600 || age>10800 || age!=int(age)) return false;
    for (const auto &value:servers) {
        const auto server=value.toObject();const auto port=server.value("port").toDouble(-1);
        if (server.size()!=2 || !server.value("host").isString() || server.value("host").toString().isEmpty()
            || port<1 || port>65535 || port!=int(port)) return false;
    }
    return true;
}
using LitecoinJournalProtocol::privateConfig;
using LitecoinJournalProtocol::snapshotValid;
bool refundReviewValid(const QJsonObject &review,const QJsonObject &snapshot,const QString &action) {
    if (review.size()!=2 || !review.value("commitment").isString() || !review.value("details").isObject()) return false;
    const auto d=review.value("details").toObject();
    const QStringList strings{"schema","tradeId","quoteCommitment","journalCommitment","preparedCommitment",
        "action","txid","destination","refundAtoms","feeAtoms","maximumFeeAtoms","lockedAtoms"};
    if (d.size()!=strings.size()+2) return false;
    for (const auto &key:strings) if (!d.value(key).isString()) return false;
    if (d.value("schema")!="monzero-ltc-native-refund-review-v1" || d.value("tradeId")!=snapshot.value("tradeId")
        || d.value("quoteCommitment")!=snapshot.value("quoteCommitment") || d.value("destination")!=snapshot.value("xmzRefund")
        || d.value("lockedAtoms")!=snapshot.value("xmzAtoms") || d.value("action")!=action
        || !d.value("chainObserved").isBool() || d.value("chainObserved").toBool()
        || snapshot.value("role")!="native-funder") return false;
    static const QRegularExpression hash("\\A[0-9a-f]{64}\\z"),atoms("\\A(?:0|[1-9][0-9]{0,19})\\z");
    for (const auto &key:{"quoteCommitment","journalCommitment","preparedCommitment","txid"})
        if (!hash.match(d.value(key).toString()).hasMatch()) return false;
    const auto dispatch=d.value("dispatchCommitment");
    const auto payment=snapshot.value("nativeRefundPayout").toObject();
    if (!payment.value("prepared").toBool()) return false;
    if (action=="send") {if (!dispatch.isNull() || payment.value("attempted").toBool()) return false;}
    else if (action!="retransmit" || !dispatch.isString() || !hash.match(dispatch.toString()).hasMatch()
        || !payment.value("attempted").toBool()) return false;
    QMap<QString,quint64> values;
    for (const auto &key:{"refundAtoms","feeAtoms","maximumFeeAtoms","lockedAtoms"}) {
        bool ok=false;const auto text=d.value(key).toString();const auto value=text.toULongLong(&ok);
        if (!ok || !atoms.match(text).hasMatch()) return false;
        values.insert(key,value);
    }
    if (!values["refundAtoms"] || values["feeAtoms"]>values["maximumFeeAtoms"]
        || values["feeAtoms"]>values["lockedAtoms"] || values["refundAtoms"]!=values["lockedAtoms"]-values["feeAtoms"]) return false;
    QByteArray binding=QByteArrayLiteral("MONZERO-LTC-NATIVE-REFUND-APPROVAL-V1");binding.append('\0');
    binding+=QJsonDocument(d).toJson(QJsonDocument::Compact);
    return review.value("commitment").toString()==QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex());
}
bool payoutReviewValid(const QJsonObject &review,const QJsonObject &snapshot,const QString &action) {
    if (review.size()!=2 || !review.value("commitment").isString() || !review.value("details").isObject()) return false;
    const auto d=review.value("details").toObject();
    const QStringList strings{"schema","tradeId","quoteCommitment","journalCommitment","preparedCommitment",
        "action","txid","destination","payoutAtoms","reserveDestination","reserveReturnAtoms","feeAtoms","maximumFeeAtoms","lockedAtoms"};
    if (d.size()!=strings.size()+2) return false;
    for (const auto &key:strings) if (!d.value(key).isString()) return false;
    if (d.value("schema")!="monzero-ltc-native-payout-review-v1" || d.value("tradeId")!=snapshot.value("tradeId")
        || d.value("quoteCommitment")!=snapshot.value("quoteCommitment") || d.value("destination")!=snapshot.value("xmzPayout")
        || d.value("reserveDestination")!=snapshot.value("xmzRefund")
        || d.value("lockedAtoms")!=snapshot.value("xmzAtoms") || d.value("action")!=action
        || !d.value("chainObserved").isBool() || d.value("chainObserved").toBool()
        || snapshot.value("role")!="litecoin-funder") return false;
    static const QRegularExpression hash("\\A[0-9a-f]{64}\\z"),atoms("\\A(?:0|[1-9][0-9]{0,19})\\z");
    for (const auto &key:{"quoteCommitment","journalCommitment","preparedCommitment","txid"})
        if (!hash.match(d.value(key).toString()).hasMatch()) return false;
    const auto dispatch=d.value("dispatchCommitment");
    const auto payment=snapshot.value("nativePayout").toObject();
    if (!payment.value("prepared").toBool()) return false;
    if (action=="send") {if (!dispatch.isNull() || payment.value("attempted").toBool()) return false;}
    else if (action!="retransmit" || !dispatch.isString() || !hash.match(dispatch.toString()).hasMatch()
        || !payment.value("attempted").toBool()) return false;
    QMap<QString,quint64> values;
    for (const auto &key:{"payoutAtoms","reserveReturnAtoms","feeAtoms","maximumFeeAtoms","lockedAtoms"}) {
        bool ok=false;const auto text=d.value(key).toString();const auto value=text.toULongLong(&ok);
        if (!ok || !atoms.match(text).hasMatch()) return false;
        values.insert(key,value);
    }
    if (!values["payoutAtoms"] || values["feeAtoms"]>values["maximumFeeAtoms"]
        || values["feeAtoms"]>values["lockedAtoms"]
        || values["reserveReturnAtoms"]>values["lockedAtoms"]-values["feeAtoms"]
        || values["payoutAtoms"]!=values["lockedAtoms"]-values["feeAtoms"]-values["reserveReturnAtoms"]) return false;
    QByteArray binding=QByteArrayLiteral("MONZERO-LTC-NATIVE-PAYOUT-APPROVAL-V1");binding.append('\0');
    binding+=QJsonDocument(d).toJson(QJsonDocument::Compact);
    return review.value("commitment").toString()==QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex());
}

bool fundingReviewValid(const QJsonObject &review,const QJsonObject &snapshot,const QString &action,quint32 safety) {
    if (review.size()!=2 || !review.value("commitment").isString() || !review.value("details").isObject()) return false;
    const auto d=review.value("details").toObject();
    const QStringList strings{"schema","tradeId","quoteCommitment","journalCommitment","preparedCommitment",
        "action","txid","wallet","destination","amountAtoms","feeAtoms","maximumFeeAtoms","totalDebitAtoms",
        "ltcContract","ltcAtoms","safetyBlocks","expiresAt"};
    if (d.size()!=strings.size()+4) return false;
    for (const auto &key:strings) if (!d.value(key).isString()) return false;
    if (d.value("schema")!="monzero-ltc-native-funding-review-v1" || d.value("tradeId")!=snapshot.value("tradeId")
        || d.value("quoteCommitment")!=snapshot.value("quoteCommitment") || d.value("amountAtoms")!=snapshot.value("xmzAtoms")
        || d.value("ltcAtoms")!=snapshot.value("ltcAtoms") || d.value("action")!=action || !safety
        || d.value("safetyBlocks").toString()!=QString::number(safety) || snapshot.value("role")!="native-funder"
        || !d.value("chainObserved").isBool() || d.value("chainObserved").toBool()) return false;
    static const QRegularExpression hash("\\A[0-9a-f]{64}\\z"),atoms("\\A(?:0|[1-9][0-9]{0,19})\\z"),
        address("\\A[1-9A-HJ-NP-Za-km-z]{26,128}\\z");
    // The pinned helper validates actual address encodings and their association
    // with the quote/key exchange. These checks only bound public presentation.
    for (const auto &key:{"wallet","destination"})
        if (!address.match(d.value(key).toString()).hasMatch()) return false;
    const QString network=snapshot.value("ltcNetwork").toString();
    const QString hrp=LitecoinJournalProtocol::bech32Prefix(network);
    const auto contract=d.value("ltcContract").toString();
    static const QRegularExpression witness("\\A[a-z0-9]{14,90}\\z");
    if (hrp.isEmpty() || !contract.startsWith(hrp) || !witness.match(contract).hasMatch()) return false;
    for (const auto &key:{"quoteCommitment","journalCommitment","preparedCommitment","txid"})
        if (!hash.match(d.value(key).toString()).hasMatch()) return false;
    for (const auto &key:{"account","priority"}) {
        const auto field=d.value(key);const auto value=field.toDouble(-1);
        if (!field.isDouble() || value<0 || value>std::numeric_limits<quint32>::max() || value!=static_cast<quint32>(value)) return false;
    }
    if (d.value("priority").toInt()<1 || d.value("priority").toInt()>4) return false;
    const auto payment=snapshot.value("nativeFunding").toObject();const auto dispatch=d.value("dispatchCommitment");
    if (!payment.value("prepared").toBool()) return false;
    if (action=="send") {if (!dispatch.isNull() || payment.value("attempted").toBool()) return false;}
    else if (action!="retransmit" || !dispatch.isString() || !hash.match(dispatch.toString()).hasMatch()
        || !payment.value("attempted").toBool()) return false;
    QMap<QString,quint64> values;
    for (const auto &key:{"amountAtoms","feeAtoms","maximumFeeAtoms","totalDebitAtoms","expiresAt"}) {
        bool ok=false;const auto text=d.value(key).toString();const auto value=text.toULongLong(&ok);
        if (!ok || !atoms.match(text).hasMatch()) return false;values.insert(key,value);
    }
    if (!values["amountAtoms"] || values["feeAtoms"]>values["maximumFeeAtoms"]
        || values["amountAtoms"]>std::numeric_limits<quint64>::max()-values["feeAtoms"]
        || values["totalDebitAtoms"]!=values["amountAtoms"]+values["feeAtoms"]
        || !values["expiresAt"] || values["expiresAt"]>static_cast<quint64>(std::numeric_limits<qint64>::max())
        || !QDateTime::fromSecsSinceEpoch(static_cast<qint64>(values["expiresAt"]),Qt::UTC).isValid()) return false;
    QByteArray binding=QByteArrayLiteral("MONZERO-LTC-NATIVE-FUNDING-APPROVAL-V1");binding.append('\0');
    binding+=QJsonDocument(d).toJson(QJsonDocument::Compact);
    return review.value("commitment").toString()==QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex());
}

bool startReviewValid(const QJsonObject &review,const QString &trade,const QString &role,
    const QVariantMap &terms,const QVariantMap &policy,bool fresh) {
    const auto details=review.value("details").toObject();
    const auto quoted=QJsonObject::fromVariantMap(terms);
    static const QRegularExpression hash("\\A[0-9a-f]{64}\\z"),number("\\A(?:0|[1-9][0-9]{0,19})\\z");
    if (review.size()!=2 || !review.value("details").isObject() || !review.value("commitment").isString()
        || details.size()!=7 || details.value("schema")!="monzero-ltc-trade-start-review-v1"
        || details.value("role")!=role || details.value("terms")!=quoted
        || details.value("policy")!=QJsonObject::fromVariantMap(policy)
        || !hash.match(details.value("quoteCommitment").toString()).hasMatch()
        || !details.value("chainObserved").isBool() || details.value("chainObserved").toBool()
        || !details.value("executable").isBool() || details.value("executable").toBool()
        || quoted.value("schema")!="monzero-native-ltc-xmz-quote-v2" || quoted.value("swapId")!=trade
        || !DexDeployment::litecoinNetworkAllowed(quoted.value("ltcNetwork").toString())) return false;
    for (const auto &key:quoted.keys()) {
        if (!quoted.value(key).isString()) return false;
        if (key.endsWith("Atoms") || key=="expiresAt") {
            bool ok=false;quoted.value(key).toString().toULongLong(&ok);
            if (!ok || !number.match(quoted.value(key).toString()).hasMatch()) return false;
        }
    }
    const QStringList amounts{"ltcAtoms","xmzAtoms","xmzPayoutAtoms","xmzClaimFeeMaxAtoms","xmzRefundFeeMaxAtoms",
        "ltcFundingFeeMaxAtoms","xmzNativeFeeMaxAtoms","ltcCancelFeeAtoms","ltcRefundFeeAtoms","ltcRedeemFeeAtoms",
        "ltcPunishFeeAtoms","ltcEarlyRefundFeeAtoms","ltcMinimumOutputAtoms","ltcMinimumRelayFeeAtomsPerKb","appFeeXmzAtoms"};
    for (const auto &key:amounts) if (!quoted.contains(key)) return false;
    bool expiryOk=false;const auto expiry=quoted.value("expiresAt").toString().toLongLong(&expiryOk);
    if (!expiryOk || expiry<=0 || !QDateTime::fromSecsSinceEpoch(expiry,Qt::UTC).isValid()
        || (fresh && expiry<=QDateTime::currentSecsSinceEpoch())) return false;
    QByteArray binding=QByteArrayLiteral("MONZERO-LTC-TRADE-START-APPROVAL-V1");binding.append('\0');
    binding+=QJsonDocument(details).toJson(QJsonDocument::Compact);
    return review.value("commitment").toString()==QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex());
}

}
LitecoinJournalJob::LitecoinJournalJob(QObject *parent):QObject(parent),m_process(new QProcess(this)),m_timer(new QTimer(this)) {
    m_timer->setSingleShot(true);
    connect(m_timer,&QTimer::timeout,this,[this]{finish(false);});
    connect(m_process,&QProcess::started,this,[this]{
        if (m_process->write(m_input)!=m_input.size()) {finish(false);return;}m_process->closeWriteChannel();
    });
    connect(m_process,&QProcess::readyReadStandardOutput,this,&LitecoinJournalJob::drain);
    connect(m_process,&QProcess::readyReadStandardError,this,&LitecoinJournalJob::drain);
    connect(m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){finish(false);});
    connect(m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus exit){
        QPointer<LitecoinJournalJob> guard(this);
        const auto request=m_request;
        drain();if (guard && m_request==request) finish(code==0 && exit==QProcess::NormalExit && m_errors==0);
    });
}
LitecoinJournalJob::~LitecoinJournalJob() {
    disconnect(m_process,nullptr,this,nullptr);
    if (m_process->state()!=QProcess::NotRunning) {m_process->kill();m_process->waitForFinished(1000);}
}
bool LitecoinJournalJob::configure(const QString &path) {
    if (!m_path.isEmpty() || m_busy) return false;
    const auto bytes=privateConfig(path);const auto doc=QJsonDocument::fromJson(bytes);
    if (!doc.isObject() || doc.toJson(QJsonDocument::Compact)!=bytes) return false;
    const auto c=doc.object();
    const bool startup=c.value("schema")=="monzero-private-ltc-journal-v5";
    const bool payoutExecution=c.value("schema")=="monzero-private-ltc-journal-v3";
    const bool fundingExecution=c.value("schema")=="monzero-private-ltc-journal-v4";
    const bool execution=fundingExecution || payoutExecution || c.value("schema")=="monzero-private-ltc-journal-v2";
    if ((!execution && !startup && c.value("schema")!="monzero-private-ltc-journal-v1") || c.size()!=((execution || startup)?10:9)) return false;
    QString rpcPath;QByteArray rpcConfig;
    if (execution) {
        const QString rpcKey=fundingExecution?"fundingRpcFile":payoutExecution?"payoutRpcFile":"refundRpcFile";
        if (!c.value(rpcKey).isString() || c.value("role")!=(payoutExecution?"litecoin-funder":"native-funder")
            || !DexDeployment::litecoinNetworkAllowed(c.value("policy").toObject().value("ltc_network").toString())) return false;
        const auto minimum=c.value("policy").toObject().value("xmz_confirmations");
        const auto value=minimum.toDouble();
        if (!minimum.isDouble() || value<1 || value>std::numeric_limits<quint32>::max()
            || value!=static_cast<quint32>(value)) return false;
        m_nativeMinimum=static_cast<quint32>(value);
        rpcPath=c.value(rpcKey).toString();rpcConfig=privateConfig(rpcPath);
        if (rpcConfig.isEmpty()) return false;
    }
    for (const auto &key:{"helper","helperDigest","metadataDirectory","snapshotDirectory","tradeId","role"}) if (!c.value(key).isString()) return false;
    if (!c.value("pins").isArray() || !c.value("policy").isObject()) return false;
    const QString trade=c.value("tradeId").toString(),role=c.value("role").toString();
    if (QUuid(trade).isNull() || QUuid(trade).toString(QUuid::WithoutBraces)!=trade
        || !QStringList{"litecoin-funder","native-funder"}.contains(role)
        || !BackendExecutable::matches(c.value("helper").toString(),c.value("helperDigest").toString())) return false;
    if (startup) {
        if (!c.value("startQuoteFile").isString() || !DexDeployment::litecoinNetworkAllowed(c.value("policy").toObject().value("ltc_network").toString())) return false;
        const QString quotePath=c.value("startQuoteFile").toString();
        const auto quoteBytes=privateConfig(quotePath);const auto envelope=QJsonDocument::fromJson(quoteBytes);
        const auto e=envelope.object();const auto payload=QJsonDocument::fromJson(e.value("payload").toString().toUtf8());
        if (!envelope.isObject() || envelope.toJson(QJsonDocument::Compact)!=quoteBytes || e.size()!=4
            || e.value("schema")!="monzero-ltc-signed-quote-envelope-v1" || !e.value("payload").isString()
            || !e.value("ltcSignature").isString() || !e.value("xmzSignature").isString() || !payload.isObject()
            || payload.object().value("schema")!="monzero-native-ltc-xmz-quote-v2" || payload.object().value("swapId")!=trade) return false;
        m_startQuotePath=quotePath;m_startQuote=quoteBytes;m_startTerms=payload.object().toVariantMap();
        m_startPolicy=c.value("policy").toObject().toVariantMap();m_canStart=true;m_startState="ready";
    }
    if (fundingExecution) {
        const auto rpc=QJsonDocument::fromJson(rpcConfig).object();
        if (rpc.value("schema")=="monzero-private-ltc-funding-rpc-v2") {
            const auto source=rpc.value("source").toObject();
            if (source.size()!=3 || !source.value("wallet").isString() || source.value("wallet").toString().isEmpty()
                || source.value("wallet").toString().size()>128) return false;
            for (const auto key:{"account","priority"}) {
                const auto field=source.value(key);const auto value=field.toDouble(-1);
                if (!field.isDouble() || value<0 || value>std::numeric_limits<quint32>::max() || value!=static_cast<quint32>(value)) return false;
            }
            if (source.value("priority").toInt()<1 || source.value("priority").toInt()>4) return false;
            m_fundingSource=source.toVariantMap();m_canPrepareFunding=true;m_preparationState="not-prepared";
        }
    }
    if (execution && !fundingExecution) {
        const auto rpc=QJsonDocument::fromJson(rpcConfig).object();
        const QString prefix=payoutExecution?"monzero-private-ltc-payout-rpc":"monzero-private-ltc-refund-rpc";
        if (rpc.value("schema")==prefix+"-v2" || rpc.value("schema")==prefix+"-v3" || rpc.value("schema")==prefix+"-v4") {
            const auto observer=rpc.value("preparationObserver").toObject();
            const auto port=observer.value("walletPort").toDouble(-1);
            const bool indexed=rpc.value("schema")==prefix+"-v4";
            const bool recovery=indexed || rpc.value("schema")==prefix+"-v3";
            const bool light=rpc.contains("ltcLight");
            if (light && (payoutExecution || !lightSellerValid(rpc))) return false;
            if ((indexed && (rpc.contains("ltcRedeemBlock") || rpc.contains("ltcRefundBlock")))
                || rpc.size()!=(recovery && !indexed?7:6)+(light?1:4) || observer.size()!=2 || port<1 || port>65535 || port!=int(port)
                || !observer.value("loginFile").isString() || observer.value("loginFile").toString().isEmpty()) return false;
            for (const auto key:{"ltcPort","nativeDaemonPort","nativeWalletPort"}) if (rpc.value(key).toDouble()==port) return false;
            if (recovery) {
                if (!rpc.value("walletPasswordFile").isString() || rpc.value("walletPasswordFile").toString().isEmpty()) return false;
                m_canRecoverWallet=true;
            }
            m_canPreparePayment=true;m_preparationState="not-prepared";
        }
    }
    m_canSubmit=execution && !fundingExecution;m_canFund=fundingExecution;m_rpcPath=rpcPath;m_rpcConfig=rpcConfig;
    m_path=path;m_config=bytes;m_program=c.value("helper").toString();m_digest=c.value("helperDigest").toString();m_trade=trade;m_role=role;
    emit changed();return true;
}
QVariantMap LitecoinJournalJob::status() const {
    QVariantMap amounts;
    if (!m_snapshot.isEmpty()) {
        // Parse validated integer strings natively: QML numbers cannot preserve
        // all 64-bit atomic amounts. XMZ here is the lock, not net proceeds.
        amounts.insert("ltc",DexUtils::formatAtomic(m_snapshot.value("ltcAtoms").toString().toULongLong(),8));
        amounts.insert("xmzLock",DexUtils::formatAtomic(m_snapshot.value("xmzAtoms").toString().toULongLong(),11));
    }
    const bool payout=m_role=="litecoin-funder";
    const bool walletAvailable=m_canRecoverWallet && !m_busy && m_snapshot.value("keysReady").toBool()
        && m_snapshot.value("recoveryIntentRetained").toBool();
    return {{"walletRecoveryConfigured",m_canRecoverWallet},{"walletRecoveryState",m_walletState},{"recoveredWallet",m_walletResult},
        {"walletRestoreAvailable",walletAvailable && !m_walletRestoreAttempted},{"walletOpenAvailable",walletAvailable},{"walletCheckAvailable",walletAvailable},
        {"paymentPreparationConfigured",m_canPreparePayment},{"paymentPreparationState",m_preparationState},
        {"payoutPreparationAvailable",m_canPreparePayment && payout && !m_busy
            && m_snapshot.value("nativeClaimRetained").toBool() && !m_snapshot.value("nativePayout").toMap().value("prepared").toBool()},
        {"refundPreparationAvailable",m_canPreparePayment && !payout && !m_busy
            && m_snapshot.value("nativeRefundRetained").toBool() && !m_snapshot.value("nativeRefundPayout").toMap().value("prepared").toBool()},
        {"fundingPreparationConfigured",m_canPrepareFunding},{"fundingPreparationState",m_preparationState},
        {"fundingPreparationSource",m_fundingSource},{"fundingPreparationAvailable",m_canPrepareFunding && !m_busy
            && m_snapshot.value("keysReady").toBool() && m_snapshot.value("recoveryIntentRetained").toBool()
            && !m_snapshot.value("nativeFunding").toMap().value("prepared").toBool()},
        {"startConfigured",m_canStart},{"startState",m_startState},{"startReview",m_startReview},
        {"startReviewAvailable",m_canStart && !m_busy && !m_startAttempted},
        {"startSubmissionAvailable",m_canStart && !m_busy && !m_startAttempted && !m_startReview.isEmpty()},
        {"attached",!m_path.isEmpty()},{"busy",m_busy},{"valid",!m_snapshot.isEmpty()},
        {"executable",false},{"chainObserved",false},{"snapshot",m_snapshot},{"amounts",amounts},{"fundingReview",m_fundingReview},{"payoutReview",m_payoutReview},{"refundReview",m_review},{"fundingSubmission",m_canFund?m_submission:QVariantMap{}},{"refundSubmission",payout || m_canFund?QVariantMap{}:m_submission},{"payoutSubmission",payout?m_submission:QVariantMap{}},
        {"fundingSubmissionConfigured",m_canFund},{"fundingSubmissionAvailable",m_canFund && !m_busy && !m_fundingReview.isEmpty()},
        {"refundSubmissionConfigured",m_canSubmit && !payout},{"payoutSubmissionConfigured",m_canSubmit && payout},
        {"payoutSubmissionAvailable",m_canSubmit && payout && !m_busy && !m_payoutReview.isEmpty()},{"refundSubmissionAvailable",m_canSubmit && !payout && !m_busy && !m_review.isEmpty()},
        {"fundingConfirmation",m_canFund?m_confirmation:QVariantMap{}},{"fundingCheckState",m_canFund?m_checkState:QString("not-checked")},
        {"fundingCheckAvailable",m_canFund && !m_busy && m_snapshot.value("nativeFunding").toMap().value("attempted").toBool()},
        {"refundConfirmation",payout || m_canFund?QVariantMap{}:m_confirmation},{"refundCheckState",payout || m_canFund?QString("not-checked"):m_checkState},
        {"payoutConfirmation",payout?m_confirmation:QVariantMap{}},{"payoutCheckState",payout?m_checkState:QString("not-checked")},
        {"payoutCheckAvailable",m_canSubmit && payout && !m_busy && m_snapshot.value("nativePayout").toMap().value("attempted").toBool()},
        {"refundCheckAvailable",m_canSubmit && !payout && !m_busy && m_snapshot.value("nativeRefundPayout").toMap().value("attempted").toBool()}};
}
bool LitecoinJournalJob::refresh() {return start({});}
bool LitecoinJournalJob::reviewStart() {
    if (!m_canStart || m_startAttempted || m_busy) return false;
    return start({}, {}, false, false, 0, 1);
}
bool LitecoinJournalJob::submitStart(const QString &commitment) {
    if (!m_canStart || m_startAttempted || m_busy || m_startReview.isEmpty() || commitment.isEmpty()
        || commitment!=m_startReview.value("commitment").toString()) return false;
    if (m_startTerms.value("expiresAt").toString().toLongLong()<=QDateTime::currentSecsSinceEpoch()) {
        m_startReview.clear();m_startState="expired";emit changed();return false;
    }
    m_startPending={{"details",m_startReview.value("details")},{"commitment",commitment}};
    return start({},commitment,false,false,0,2);
}
bool LitecoinJournalJob::reviewRefund(bool retransmit) {
    if (m_role!="native-funder" || m_snapshot.isEmpty()) return false;
    const auto payment=m_snapshot.value("nativeRefundPayout").toMap();
    if (!payment.value("prepared").toBool() || payment.value("attempted").toBool()!=retransmit) return false;
    return start(retransmit?QStringLiteral("retransmit"):QStringLiteral("send"));
}
bool LitecoinJournalJob::reviewPayout(bool retransmit) {
    if (m_role!="litecoin-funder" || m_snapshot.isEmpty()) return false;
    const auto payment=m_snapshot.value("nativePayout").toMap();
    if (!payment.value("prepared").toBool() || payment.value("attempted").toBool()!=retransmit) return false;
    return start(retransmit?QStringLiteral("retransmit"):QStringLiteral("send"),{},false,true);
}
bool LitecoinJournalJob::restoreRecoveredWallet() {return status().value("walletRestoreAvailable").toBool() && runWalletOperation("restore");}
bool LitecoinJournalJob::openRecoveredWallet() {return status().value("walletOpenAvailable").toBool() && runWalletOperation("open");}
bool LitecoinJournalJob::checkRecoveredWallet() {return status().value("walletCheckAvailable").toBool() && runWalletOperation("check");}
bool LitecoinJournalJob::runWalletOperation(const QString &operation) {
    m_walletSnapshot=m_snapshot;
    return start({}, {}, false,m_role=="litecoin-funder",0,0,false,operation);
}
bool LitecoinJournalJob::preparePayout() {
    if (!status().value("payoutPreparationAvailable").toBool()) return false;
    m_prepareSnapshot=m_snapshot;return start("send",{},false,true,0,0,true);
}
bool LitecoinJournalJob::prepareRefund() {
    if (!status().value("refundPreparationAvailable").toBool()) return false;
    m_prepareSnapshot=m_snapshot;return start("send",{},false,false,0,0,true);
}
bool LitecoinJournalJob::prepareFunding(quint32 safetyBlocks) {
    if (!status().value("fundingPreparationAvailable").toBool() || !safetyBlocks) return false;
    m_prepareSnapshot=m_snapshot;
    return start("send",{},false,false,safetyBlocks,0,true);
}
bool LitecoinJournalJob::reviewFunding(bool retransmit,quint32 safetyBlocks) {
    if (m_role!="native-funder" || m_snapshot.isEmpty() || !safetyBlocks) return false;
    const auto payment=m_snapshot.value("nativeFunding").toMap();
    if (!payment.value("prepared").toBool() || payment.value("attempted").toBool()!=retransmit) return false;
    return start(retransmit?QStringLiteral("retransmit"):QStringLiteral("send"),{},false,false,safetyBlocks);
}
bool LitecoinJournalJob::checkFunding() {
    if (m_role!="native-funder" || !m_canFund || m_snapshot.isEmpty()
        || !m_snapshot.value("nativeFunding").toMap().value("attempted").toBool()) return false;
    return start({}, {}, true,false,2);
}
bool LitecoinJournalJob::submitFunding(const QString &commitment) {
    if (m_role!="native-funder" || !m_canFund || m_busy || m_fundingReview.isEmpty() || m_snapshot.isEmpty()
        || m_process->state()!=QProcess::NotRunning || commitment!=m_fundingReview.value("commitment").toString()) return false;
    const auto details=m_fundingReview.value("details").toMap();
    bool ok=false;const auto safety=details.value("safetyBlocks").toString().toUInt(&ok);
    if (!ok || !safety) return false;
    m_pendingReview=m_fundingReview;
    return start(details.value("action").toString(),commitment,false,false,safety);
}
bool LitecoinJournalJob::submitRefund(const QString &commitment) {
    if (m_role!="native-funder" || !m_canSubmit || m_busy || m_review.isEmpty() || m_snapshot.isEmpty()
        || m_process->state()!=QProcess::NotRunning || commitment!=m_review.value("commitment").toString()) return false;
    m_pendingReview=m_review;
    return start(m_review.value("details").toMap().value("action").toString(),commitment);
}
bool LitecoinJournalJob::checkRefund() {
    if (m_role!="native-funder" || !m_canSubmit || m_snapshot.isEmpty() || !m_snapshot.value("nativeRefundPayout").toMap().value("attempted").toBool()) return false;
    return start({}, {}, true);
}
bool LitecoinJournalJob::submitPayout(const QString &commitment) {
    if (m_role!="litecoin-funder" || !m_canSubmit || m_busy || m_payoutReview.isEmpty() || m_snapshot.isEmpty()
        || m_process->state()!=QProcess::NotRunning || commitment!=m_payoutReview.value("commitment").toString()) return false;
    m_pendingReview=m_payoutReview;
    return start(m_payoutReview.value("details").toMap().value("action").toString(),commitment,false,true);
}
bool LitecoinJournalJob::checkPayout() {
    if (m_role!="litecoin-funder" || !m_canSubmit || m_snapshot.isEmpty() || !m_snapshot.value("nativePayout").toMap().value("attempted").toBool()) return false;
    return start({}, {}, true,true);
}
bool LitecoinJournalJob::start(const QString &action,const QString &commitment,bool checking,bool payoutOperation,quint32 fundingSafety,int startupOperation,bool preparing,const QString &walletOperation) {
    if (m_path.isEmpty() || m_busy || m_process->state()!=QProcess::NotRunning) return false;
    m_walletOperation=walletOperation;m_walletResult.clear();
    if (!walletOperation.isEmpty()) {
        m_walletState="unknown";
        if (walletOperation=="restore") m_walletRestoreAttempted=true;
    } else {m_walletSnapshot.clear();m_walletState="not-checked";}
    m_preparing=preparing;if (preparing) m_preparationState="preparing";else m_prepareSnapshot.clear();
    m_startReview.clear();m_startOperation=startupOperation;
    if (m_canStart) m_startState=startupOperation==1?"reviewing":startupOperation==2?"starting":"checking";
    m_snapshot.clear();m_review.clear();m_payoutReview.clear();m_fundingReview.clear();m_action=action;m_submitting=!commitment.isEmpty();m_checking=checking;m_payoutOperation=payoutOperation;m_fundingSafety=fundingSafety;
    m_confirmation.clear();m_checkState=checking?"checking":"not-checked";
    if (m_submitting) m_submission={{"state","not-started"},{"chainObserved",false},{"executable",false}};
    if (privateConfig(m_path)!=m_config || ((m_canSubmit || m_canFund) && privateConfig(m_rpcPath)!=m_rpcConfig)
        || (m_canStart && privateConfig(m_startQuotePath)!=m_startQuote)
        || !BackendExecutable::matches(m_program,m_digest)) {if (preparing) m_preparationState="unknown";m_startReview.clear();m_startPending.clear();if (m_canStart) m_startState=m_startAttempted?"unknown":"unverified";m_pendingReview.clear();if (checking) m_checkState="unverified";emit changed();return false;}
    m_request=QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject request{{"schema",fundingSafety?(checking?"monzero-ltc-funding-check-request-v1":m_submitting?"monzero-ltc-funding-submit-request-v1":"monzero-ltc-funding-review-request-v1"):checking?(payoutOperation?"monzero-ltc-payout-check-request-v1":"monzero-ltc-refund-check-request-v1"):m_submitting?(payoutOperation?"monzero-ltc-payout-submit-request-v1":"monzero-ltc-refund-submit-request-v1"):
        action.isEmpty()?"monzero-ltc-journal-request-v1":payoutOperation?"monzero-ltc-payout-review-request-v1":"monzero-ltc-refund-review-request-v1"},{"requestId",m_request}};
    if (!action.isEmpty()) request.insert("action",action);
    if (fundingSafety) request.insert("safetyBlocks",static_cast<double>(fundingSafety));
    if (m_submitting) {
        request.insert("commitment",commitment);
        m_submission={{"state","submitting"},{"txid",m_pendingReview.value("details").toMap().value("txid")},
            {"chainObserved",false},{"executable",false}};
    }
    if (startupOperation) {
        request=QJsonObject{{"schema",startupOperation==1?"monzero-ltc-start-review-request-v1":"monzero-ltc-start-submit-request-v1"},{"requestId",m_request}};
        if (startupOperation==2) {request.insert("commitment",commitment);m_startAttempted=true;}
        m_submission.clear();
    }
    if (preparing) {
        request=QJsonObject{{"schema",fundingSafety?"monzero-ltc-funding-prepare-request-v1":payoutOperation?"monzero-ltc-payout-prepare-request-v1":"monzero-ltc-refund-prepare-request-v1"},{"requestId",m_request}};
        if (fundingSafety) request.insert("safetyBlocks",static_cast<double>(fundingSafety));
        else request.insert("priority",1);
    }
    QString walletMode;
    if (!walletOperation.isEmpty()) {
        walletMode=QString(payoutOperation?"payout-wallet-":"refund-wallet-")+walletOperation;
        request=QJsonObject{{"schema","monzero-ltc-"+walletMode+"-request-v1"},{"requestId",m_request}};
        m_walletState=walletOperation=="restore"?"restoring":walletOperation=="open"?"opening":"checking";
    }
    m_input=QJsonDocument(request).toJson(QJsonDocument::Compact);
    m_output.clear();m_errors=0;m_busy=true;
    // A seller using light servers verifies them against the platform trust store.
    QProcessEnvironment environment;environment.insert("LANG","C");const auto system=QProcessEnvironment::systemEnvironment();
    for (const auto name:{"SSL_CERT_FILE","SSL_CERT_DIR"}) if (system.contains(name)) environment.insert(name,system.value(name));
    m_process->setProcessEnvironment(environment);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);m_timer->start(m_submitting || checking || preparing || !walletMode.isEmpty()?300000:180000);
    m_process->start(m_program,{!walletMode.isEmpty()?"--"+walletMode+"-stdio-v1":preparing?(fundingSafety?"--funding-prepare-stdio-v1":payoutOperation?"--payout-prepare-stdio-v1":"--refund-prepare-stdio-v1"):startupOperation?(startupOperation==1?"--start-review-stdio-v1":"--start-submit-stdio-v1"):fundingSafety?(checking?"--funding-check-stdio-v1":m_submitting?"--funding-submit-stdio-v1":"--funding-review-stdio-v1"):checking?(payoutOperation?"--payout-check-stdio-v1":"--refund-check-stdio-v1"):m_submitting?(payoutOperation?"--payout-submit-stdio-v1":"--refund-submit-stdio-v1"):action.isEmpty()?"--snapshot-stdio-v1":payoutOperation?"--payout-review-stdio-v1":"--refund-review-stdio-v1","--config-file",m_path});
    emit changed();return true;
}
void LitecoinJournalJob::drain() {
    if (!m_busy) return;
    m_process->setReadChannel(QProcess::StandardOutput);m_output+=m_process->read(8193-m_output.size());
    m_process->setReadChannel(QProcess::StandardError);m_errors+=m_process->read(1025-m_errors).size();
    if (m_output.size()>8192 || m_errors>1024) finish(false);
}
void LitecoinJournalJob::finish(bool success) {
    if (!m_busy) return;
    m_busy=false;m_timer->stop();
    if (m_process->state()!=QProcess::NotRunning) m_process->kill();
    m_snapshot.clear();m_review.clear();m_payoutReview.clear();m_fundingReview.clear();
    const auto doc=QJsonDocument::fromJson(m_output);const auto response=doc.object();
    const auto snapshot=response.value("snapshot").toObject();
    if (!m_walletOperation.isEmpty()) {
        m_walletState="unknown";m_walletResult.clear();
        const bool payout=m_role=="litecoin-funder";
        const QString purpose=payout?"payout":"refund";
        auto expected=QJsonObject::fromVariantMap(m_walletSnapshot);
        expected.insert(payout?"nativeClaimRetained":"nativeRefundRetained",true);
        const auto wallet=response.value("wallet").toObject();
        const auto address=wallet.value("address").toString();
        QString filename="swap-"+m_trade;filename.remove('-');filename.insert(4,'-');
        const QRegularExpression addressPattern("\\A[123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz]{95}\\z");
        if (success && doc.isObject() && doc.toJson(QJsonDocument::Compact)==m_output && response.size()==4
            && response.value("schema")=="monzero-ltc-"+purpose+"-wallet-"+m_walletOperation+"-response-v1"
            && response.value("requestId")==m_request && snapshotValid(snapshot,m_trade,m_role) && snapshot==expected
            && privateConfig(m_path)==m_config && privateConfig(m_rpcPath)==m_rpcConfig && BackendExecutable::matches(m_program,m_digest)
            && wallet.size()==8 && wallet.value("schema")=="monzero-ltc-recovered-wallet-v1"
            && wallet.value("operation")==m_walletOperation && wallet.value("purpose")==purpose
            && wallet.value("filename")==filename && addressPattern.match(address).hasMatch()
            && (m_walletAddress.isEmpty() || address==m_walletAddress) && wallet.value("spendKeyVerified")==true
            && wallet.value("fundingImported")==true && wallet.value("executable")==false) {
            m_snapshot=snapshot.toVariantMap();m_walletResult=wallet.toVariantMap();m_walletAddress=address;
            m_walletState="verified";m_walletRestoreAttempted=true;
        }
        m_walletSnapshot.clear();m_pendingReview.clear();m_output.clear();m_input.clear();emit changed();return;
    }
    if (m_startOperation) {
        const bool submitting=m_startOperation==2;
        const auto review=response.value("review").toObject();
        m_startReview.clear();m_startState=submitting?"unknown":"unverified";
        bool boundSnapshot=snapshotValid(snapshot,m_trade,m_role)
            && snapshot.value("quoteCommitment")==review.value("details").toObject().value("quoteCommitment")
            && !snapshot.value("keysReady").toBool() && !snapshot.value("nativeFunding").toObject().value("prepared").toBool();
        for (const auto &key:{"ltcNetwork","xmzGenesis","ltcAtoms","xmzAtoms","ltcPayout","ltcRefund","xmzPayout","xmzRefund"})
            boundSnapshot=boundSnapshot && snapshot.value(key)==QJsonValue::fromVariant(m_startTerms.value(key));
        for (const auto &key:{"nativeFunding","litecoinRedemption","nativePayout","nativeRefundPayout"})
            boundSnapshot=boundSnapshot && !snapshot.value(key).toObject().value("prepared").toBool();
        if (success && doc.isObject() && doc.toJson(QJsonDocument::Compact)==m_output
            && response.size()==(submitting?4:3) && response.value("requestId")==m_request
            && response.value("schema")==(submitting?"monzero-ltc-start-submit-response-v1":"monzero-ltc-start-review-response-v1")
            && privateConfig(m_path)==m_config && privateConfig(m_startQuotePath)==m_startQuote
            && startReviewValid(review,m_trade,m_role,m_startTerms,m_startPolicy,!submitting)
            && (!submitting || (boundSnapshot && review.toVariantMap()==m_startPending))) {
            if (submitting) {m_startState="saved";m_snapshot=snapshot.toVariantMap();}
            else {
                m_startState="reviewed";m_startReview=review.toVariantMap();QVariantMap amounts;
                for (const auto &key:m_startTerms.keys()) if (key.endsWith("Atoms"))
                    amounts.insert(key,DexUtils::formatAtomic(m_startTerms.value(key).toString().toULongLong(),key.startsWith("ltc")?8:11));
                m_startReview.insert("amounts",amounts);
                m_startReview.insert("expiresAtUtc",QDateTime::fromSecsSinceEpoch(m_startTerms.value("expiresAt").toString().toLongLong(),Qt::UTC).toString(Qt::ISODate));
            }
        }
        m_startPending.clear();m_pendingReview.clear();emit changed();return;
    }
    auto preparationSnapshot=QJsonObject::fromVariantMap(m_prepareSnapshot);
    preparationSnapshot.insert(m_fundingSafety?"nativeFunding":m_payoutOperation?"nativePayout":"nativeRefundPayout",QJsonObject{{"prepared",true},{"attempted",false},{"acknowledged",false}});
    const auto sourceReview=response.value("review").toObject().value("details").toObject();
    const bool boundPreparation=!m_preparing || (snapshot==preparationSnapshot
        && (!m_fundingSafety || (sourceReview.value("wallet")==QJsonValue::fromVariant(m_fundingSource.value("wallet"))
        && sourceReview.value("account")==QJsonValue::fromVariant(m_fundingSource.value("account"))
        && sourceReview.value("priority")==QJsonValue::fromVariant(m_fundingSource.value("priority")))));
    if (m_preparing) m_preparationState="unknown";
    const bool reviewing=!m_action.isEmpty() && !m_submitting;
    if (m_checking) m_checkState="unverified";
    const auto confirmation=response.value("confirmation").toObject();
    const auto confirmationReview=response.value("review").toObject();
    const auto confirmationDetails=confirmationReview.value("details").toObject();
    static const QRegularExpression depthPattern("\\A[1-9][0-9]{0,19}\\z");
    bool depthOk=false;
    const auto depth=confirmation.value("confirmations").toString().toULongLong(&depthOk);
    const bool confirmed=confirmation.size()==4 && confirmation.value("confirmations").isString()
        && depthOk && depth>=m_nativeMinimum && depthPattern.match(confirmation.value("confirmations").toString()).hasMatch()
        && confirmation.value("chainObserved").isBool() && confirmation.value("chainObserved").toBool()
        && confirmation.value("executable").isBool() && !confirmation.value("executable").toBool()
        && confirmation.value("txid").isString() && confirmation.value("txid")==confirmationDetails.value("txid")
        && (m_fundingSafety?fundingReviewValid(confirmationReview,snapshot,"retransmit",m_fundingSafety)
            :(m_payoutOperation?payoutReviewValid:refundReviewValid)(confirmationReview,snapshot,"retransmit"));
    if (m_submitting) m_submission["state"]="unknown";
    const auto outcome=response.value("outcome").toObject();
    const auto expected=m_pendingReview.value("details").toMap();
    const auto payment=snapshot.value(m_fundingSafety?"nativeFunding":m_payoutOperation?"nativePayout":"nativeRefundPayout").toObject();
    const bool submitted=outcome.size()==5 && outcome.value("state")=="acknowledged"
        && outcome.value("txid").isString() && outcome.value("txid").toString()==expected.value("txid").toString()
        && outcome.value("feeAtoms").isString() && outcome.value("feeAtoms").toString()==expected.value("feeAtoms").toString()
        && outcome.value("chainObserved").isBool() && !outcome.value("chainObserved").toBool()
        && outcome.value("executable").isBool() && !outcome.value("executable").toBool()
        && payment.value("acknowledged").toBool();
    if (success && boundPreparation && doc.isObject() && doc.toJson(QJsonDocument::Compact)==m_output && response.size()==(m_checking?5:reviewing || m_submitting?4:3)
        && response.value("schema")== (m_preparing?(m_fundingSafety?"monzero-ltc-funding-prepare-response-v1":m_payoutOperation?"monzero-ltc-payout-prepare-response-v1":"monzero-ltc-refund-prepare-response-v1"):m_fundingSafety?(m_checking?"monzero-ltc-funding-check-response-v1":m_submitting?"monzero-ltc-funding-submit-response-v1":"monzero-ltc-funding-review-response-v1"):m_checking?(m_payoutOperation?"monzero-ltc-payout-check-response-v1":"monzero-ltc-refund-check-response-v1"):m_submitting?(m_payoutOperation?"monzero-ltc-payout-submit-response-v1":"monzero-ltc-refund-submit-response-v1"):reviewing?(m_payoutOperation?"monzero-ltc-payout-review-response-v1":"monzero-ltc-refund-review-response-v1"):"monzero-ltc-journal-response-v1")
        && response.value("requestId")==m_request && snapshotValid(snapshot,m_trade,m_role)
        && (!reviewing || (m_fundingSafety?fundingReviewValid(response.value("review").toObject(),snapshot,m_action,m_fundingSafety)
            :(m_payoutOperation?payoutReviewValid:refundReviewValid)(response.value("review").toObject(),snapshot,m_action))) && (!m_submitting || submitted) && (!m_checking || confirmed)) {
        m_snapshot=snapshot.toVariantMap();
        if (m_submitting) m_submission=outcome.toVariantMap();
        if (m_checking) {
            m_checkState="confirmed";m_confirmation=confirmation.toVariantMap();m_submission.clear();
            m_confirmation.insert("checkedAt",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
            m_confirmation.insert("destination",confirmationDetails.value("destination").toString());
            m_confirmation.insert(m_fundingSafety?"fundingAmount":m_payoutOperation?"payoutAmount":"refundAmount",DexUtils::formatAtomic(confirmationDetails.value(m_fundingSafety?"amountAtoms":m_payoutOperation?"payoutAtoms":"refundAtoms").toString().toULongLong(),11));
            if (m_payoutOperation) {
                m_confirmation.insert("reserveDestination",confirmationDetails.value("reserveDestination").toString());
                m_confirmation.insert("reserveAmount",DexUtils::formatAtomic(confirmationDetails.value("reserveReturnAtoms").toString().toULongLong(),11));
            }
        }
        if (reviewing) {
            auto &review=m_fundingSafety?m_fundingReview:m_payoutOperation?m_payoutReview:m_review;
            review=response.value("review").toObject().toVariantMap();
            const auto details=review.value("details").toMap();
            QVariantMap amounts;
            const QStringList keys=m_fundingSafety?QStringList{"amountAtoms","feeAtoms","maximumFeeAtoms","totalDebitAtoms"}:m_payoutOperation?QStringList{"payoutAtoms","reserveReturnAtoms","feeAtoms","maximumFeeAtoms"}
                :QStringList{"refundAtoms","feeAtoms","maximumFeeAtoms"};
            for (const auto &key:keys)
                amounts.insert(key,DexUtils::formatAtomic(details.value(key).toString().toULongLong(),11));
            review.insert("amounts",amounts);
            if (m_fundingSafety) review.insert("expiresAtUtc",QDateTime::fromSecsSinceEpoch(details.value("expiresAt").toString().toLongLong(),Qt::UTC).toString(Qt::ISODate));
        }
    }
    if (m_canStart) {
        m_startState=m_snapshot.isEmpty()?(m_startAttempted?"unknown":"unverified"):"saved";
        if (!m_snapshot.isEmpty()) m_startAttempted=true;
    }
    if (m_canPrepareFunding && !m_snapshot.isEmpty())
        m_preparationState=m_snapshot.value("nativeFunding").toMap().value("prepared").toBool()?"prepared":"not-prepared";
    if (m_canPreparePayment && !m_snapshot.isEmpty())
        m_preparationState=m_snapshot.value(m_role=="litecoin-funder"?"nativePayout":"nativeRefundPayout").toMap().value("prepared").toBool()?"prepared":"not-prepared";
    m_prepareSnapshot.clear();
    m_startPending.clear();m_pendingReview.clear();
    emit changed();
}
