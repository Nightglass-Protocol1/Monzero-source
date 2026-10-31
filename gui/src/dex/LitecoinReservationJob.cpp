#include "LitecoinReservationJob.h"
#include "LitecoinJournalJob.h"
#include "LitecoinJournalProtocol.h"
#include "DexUtils.h"
#include <QJsonDocument>
#include <QPointer>
#include <QProcessEnvironment>
#include <QUuid>
#include <QDateTime>
#include <limits>

LitecoinReservationJob::LitecoinReservationJob(QObject *parent):QObject(parent) {
    m_deadline.setSingleShot(true);
    connect(&m_deadline,&QTimer::timeout,this,[this]{finish(false);});
    connect(&m_process,&QProcess::started,this,[this]{
        if (m_process.write(m_input)!=m_input.size()) {finish(false);return;}
        m_process.closeWriteChannel();m_input.clear();
    });
    connect(&m_process,&QProcess::readyReadStandardOutput,this,&LitecoinReservationJob::drain);
    connect(&m_process,&QProcess::readyReadStandardError,this,&LitecoinReservationJob::drain);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){finish(false);});
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus exit){
        QPointer<LitecoinReservationJob> guard(this);const auto request=m_request;
        drain();if (guard && request==m_request) finish(code==0 && exit==QProcess::NormalExit && m_errors==0);
    });
}
LitecoinReservationJob::~LitecoinReservationJob() {
    disconnect(&m_process,nullptr,this,nullptr);
    if (m_process.state()!=QProcess::NotRunning) {m_process.kill();m_process.waitForFinished(1000);}
}
bool LitecoinReservationJob::configure(const QString &path,const QString &journalPath) {
    if (!m_path.isEmpty() || m_busy) return false;
    const auto bytes=LitecoinJournalProtocol::privateConfig(path);const auto document=QJsonDocument::fromJson(bytes);
    const auto wrapper=document.object();
    if (!document.isObject() || document.toJson(QJsonDocument::Compact)!=bytes) return false;
    const bool native=wrapper.value("schema")=="monzero-private-ltc-journal-v4";
    if (!native && (wrapper.size()!=3 || wrapper.value("schema")!="monzero-private-ltc-reservation-v1"
        || wrapper.value("journalConfigFile")!=journalPath || !wrapper.value("rpcFile").isString())) return false;
    LitecoinJournalJob validator;
    if (!validator.configure(journalPath) || !validator.status().value("startConfigured").toBool()) return false;
    const auto journal=LitecoinJournalProtocol::privateConfig(journalPath);const auto c=QJsonDocument::fromJson(journal).object();
    const QString role=native?"native-funder":"litecoin-funder";
    if (c.value("role")!=role) return false;
    if (native) {
        LitecoinJournalJob funding;
        if (!funding.configure(path)) return false;
        for (const auto key:{"tradeId","role","policy","pins","metadataDirectory","snapshotDirectory","helper","helperDigest"})
            if (wrapper.value(key)!=c.value(key)) return false;
    }
    const auto quotePath=c.value("startQuoteFile").toString();
    const auto rpcPath=wrapper.value(native?"fundingRpcFile":"rpcFile").toString();
    const auto quote=LitecoinJournalProtocol::privateConfig(quotePath),rpc=LitecoinJournalProtocol::privateConfig(rpcPath);
    const auto r=QJsonDocument::fromJson(rpc).object();
    if (quote.isEmpty() || rpc.isEmpty() || (!native && (r.size()!=7 || r.value("schema")!="monzero-private-ltc-reservation-rpc-v1"))) return false;
    const auto payload=QJsonDocument::fromJson(quote).object().value("payload").toString().toUtf8();
    m_terms=QJsonDocument::fromJson(payload).object();
    m_requiredConfirmations=qMax(m_terms.value("ltcConfirmations").toString().toULongLong(),
        c.value("policy").toObject().value("ltc_confirmations").toVariant().toULongLong());
    if (!m_requiredConfirmations) return false;
    QByteArray binding=m_terms.value("schema").toString().toUtf8();binding.append('\0');binding+=payload;
    m_quoteCommitment=QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex());
    m_sources={{path,bytes},{journalPath,journal},{quotePath,quote},{rpcPath,rpc}};
    m_rpc=r;m_trade=c.value("tradeId").toString();m_program=c.value("helper").toString();m_digest=c.value("helperDigest").toString();
    m_role=role;m_path=path;m_state="ready";emit changed();return true;
}
bool LitecoinReservationJob::sourcesMatch() const {
    if (m_path.isEmpty() || !BackendExecutable::matches(m_program,m_digest)) return false;
    for (auto it=m_sources.cbegin();it!=m_sources.cend();++it) if (LitecoinJournalProtocol::privateConfig(it.key())!=it.value()) return false;
    return true;
}
QVariantMap LitecoinReservationJob::status() const {
    return {{"role",m_role},{"reservationConfigured",!m_path.isEmpty() && m_role=="litecoin-funder"},
        {"recoveryCheckState",m_recoveryCheckState},{"recoveryObservation",m_recoveryObservation},
        {"recoveryCheckAvailable",!m_path.isEmpty() && !m_busy},{"recoveryState",m_recoveryState},{"recoveryReview",m_recoveryReview},{"recoverySubmission",m_recoverySubmission},
        {"recoveryReviewAvailable",!m_path.isEmpty() && !m_busy},{"recoverySubmissionAvailable",!m_busy && !m_recoveryReview.isEmpty()},
        {"fundingState",m_fundingState},{"fundingReview",m_fundingReview},{"fundingSubmission",m_fundingSubmission},
        {"fundingReviewAvailable",!m_path.isEmpty() && m_role=="litecoin-funder" && !m_busy},{"fundingSubmissionAvailable",!m_busy && !m_fundingReview.isEmpty()},
        {"tradeId",m_trade},{"quoteCommitment",m_quoteCommitment},{"configured",!m_path.isEmpty()},{"busy",m_busy},{"state",m_state},{"review",m_review},{"executable",false}};
}
bool LitecoinReservationJob::recoveryAllowed(const QString &operation) const {
    return operation=="cancel" || (m_role=="native-funder"?operation=="punish":operation=="refund");
}
bool LitecoinReservationJob::prepare() {return run("reservation-prepare");}
bool LitecoinReservationJob::reviewRecovery(const QString &operation) {
    if (!recoveryAllowed(operation)) return false;
    return run("recovery-"+operation+"-review");
}
bool LitecoinReservationJob::checkRecovery(const QString &operation) {
    if (!recoveryAllowed(operation)) return false;
    return run("recovery-"+operation+"-check");
}
bool LitecoinReservationJob::submitRecovery(const QString &commitment) {
    if (m_busy || m_recoveryReview.isEmpty() || commitment!=m_recoveryReview.value("commitment").toString()) return false;
    m_pendingRecovery=m_recoveryReview.value("details").toMap();
    return run("recovery-"+m_pendingRecovery.value("operation").toString()+"-submit",commitment);
}
bool LitecoinReservationJob::reviewFunding() {return run("lock-funding-review");}
bool LitecoinReservationJob::submitFunding(const QString &commitment) {
    if (m_busy || m_fundingReview.isEmpty() || commitment!=m_fundingReview.value("commitment").toString()) return false;
    m_pendingFunding=m_fundingReview.value("details").toMap();
    return run("lock-funding-submit",commitment);
}
bool LitecoinReservationJob::run(const QString &operation,const QString &commitment) {
    if (m_busy || m_process.state()!=QProcess::NotRunning || m_path.isEmpty()) return false;
    if (operation.startsWith("recovery-")) {
        if (!recoveryAllowed(operation.section('-',1,1))
            || !QStringList{"review","submit","check"}.contains(operation.section('-',2,2))) return false;
    } else if (m_role!="litecoin-funder" || !QStringList{"reservation-prepare","lock-funding-review","lock-funding-submit"}.contains(operation)) return false;
    const QString wireOperation=m_role=="native-funder"?"native-"+operation:operation;
    m_recoveryObservation.clear();m_recoveryCheckState="not-checked";
    m_operation=operation;m_fundingReview.clear();m_fundingSubmission.clear();m_recoveryReview.clear();m_recoverySubmission.clear();
    if (operation=="reservation-prepare") m_review.clear();
    if (!sourcesMatch()) {m_state="unknown";m_fundingState="unknown";m_pendingFunding.clear();m_pendingRecovery.clear();m_recoveryState="unknown";emit changed();return false;}
    m_request=QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject request{{"schema","monzero-ltc-"+wireOperation+"-request-v1"},{"requestId",m_request}};
    if (!commitment.isEmpty()) request.insert("commitment",commitment);
    m_input=QJsonDocument(request).toJson(QJsonDocument::Compact);
    m_output.clear();m_errors=0;m_busy=true;
    if (operation=="reservation-prepare") m_state="preparing";
    else if (operation.startsWith("recovery-")) {
        if (operation.endsWith("-check")) {m_recoveryCheckState="checking";m_recoveryState="not-reviewed";}
        else m_recoveryState=operation.endsWith("-review")?"reviewing":"submitting";
    }
    else m_fundingState=operation=="lock-funding-review"?"reviewing":"submitting";
    m_deadline.start(180000);
    // A light-server seller needs the system trust store for Electrum TLS.
    QProcessEnvironment environment;environment.insert("LANG","C");const auto system=QProcessEnvironment::systemEnvironment();
    for (const auto name:{"SSL_CERT_FILE","SSL_CERT_DIR"}) if (system.contains(name)) environment.insert(name,system.value(name));
    m_process.setProcessEnvironment(environment);
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_process.start(m_program,{"--"+wireOperation+"-stdio-v1","--config-file",m_path});emit changed();return true;
}
void LitecoinReservationJob::drain() {
    if (!m_busy) return;
    m_process.setReadChannel(QProcess::StandardOutput);m_output+=m_process.read(110001-m_output.size());
    m_process.setReadChannel(QProcess::StandardError);m_errors+=m_process.read(1025-m_errors).size();
    if (m_output.size()>110000 || m_errors>1024) finish(false);
}
bool LitecoinReservationJob::recoveryReviewValid(const QJsonObject &review,const QString &kind) const {
    const auto d=review.value("details").toObject();
    const QStringList strings{"schema","tradeId","quoteCommitment","journalCommitment","recoveryCommitment","transactionCommitment",
        "operation","action","txid","wtxid","inputTxid","inputVout","sequence","destination","inputAtoms","amountAtoms","feeAtoms"};
    if (review.size()!=2 || d.size()!=strings.size()+2) return false;
    for (const auto &key:strings) if (!d.value(key).isString() || d.value(key).toString().isEmpty()) return false;
    const QRegularExpression hash("\\A[0-9a-f]{64}\\z");
    for (const auto key:{"quoteCommitment","journalCommitment","recoveryCommitment","transactionCommitment","txid","wtxid","inputTxid"})
        if (!hash.match(d.value(key).toString()).hasMatch()) return false;
    if (!d.value("dispatchCommitment").isNull() && !hash.match(d.value("dispatchCommitment").toString()).hasMatch()) return false;
    if (d.value("schema")!="monzero-ltc-recovery-spend-review-v1" || d.value("tradeId")!=m_trade
        || d.value("quoteCommitment")!=m_quoteCommitment || d.value("operation")!=kind || d.value("chainObserved")!=false
        || (d.value("action")!="send" && d.value("action")!="retransmit")
        || (d.value("action")=="send")!=d.value("dispatchCommitment").isNull()) return false;
    QMap<QString,quint64> n;
    for (const auto key:{"inputAtoms","amountAtoms","feeAtoms","inputVout","sequence"}) {
        bool ok=false;const auto value=d.value(key).toString().toULongLong(&ok);
        if (!ok || QString::number(value)!=d.value(key).toString()) return false;n.insert(key,value);
    }
    bool lockedOk=false,cancelOk=false,feeOk=false;
    const auto locked=m_terms.value("ltcAtoms").toString().toULongLong(&lockedOk);
    const auto cancel=m_terms.value("ltcCancelFeeAtoms").toString().toULongLong(&cancelOk);
    const auto fee=m_terms.value(kind=="cancel"?"ltcCancelFeeAtoms":kind=="punish"?"ltcPunishFeeAtoms":"ltcRefundFeeAtoms").toString().toULongLong(&feeOk);
    if (!lockedOk || !cancelOk || !feeOk || locked<cancel || !n["amountAtoms"] || n["feeAtoms"]!=fee
        || n["inputAtoms"]!=(kind=="cancel"?locked:locked-cancel) || n["inputAtoms"]<n["amountAtoms"]
        || n["inputAtoms"]-n["amountAtoms"]!=fee || n["inputVout"]>std::numeric_limits<quint32>::max()
        || n["sequence"]>std::numeric_limits<quint32>::max()) return false;
    if (kind=="refund" && d.value("destination")!=m_terms.value("ltcRefund")) return false;
    if (kind=="punish" && d.value("destination")!=m_terms.value("ltcPayout")) return false;
    if (!LitecoinJournalProtocol::witnessAddress(d.value("destination").toString(),m_terms.value("ltcNetwork").toString(),
            "[qp][023456789acdefghjklmnpqrstuvwxyz]{38}(?:[023456789acdefghjklmnpqrstuvwxyz]{20})?")) return false;
    QByteArray binding("MONZERO-LTC-RECOVERY-SPEND-APPROVAL-V1");binding.append('\0');binding+=QJsonDocument(d).toJson(QJsonDocument::Compact);
    return review.value("commitment").toString().toLatin1()==QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex();
}
bool LitecoinReservationJob::fundingReviewValid(const QJsonObject &review) const {
    const auto d=review.value("details").toObject();
    const QStringList strings{"schema","tradeId","quoteCommitment","reservationCommitment","recoveryCommitment","action","txid",
        "wallet","anchor","contract","change","changeAtoms","amountAtoms","feeAtoms","maximumFeeAtoms","totalDebitAtoms","inputCount","refundAddress","expiresAt"};
    if (review.size()!=2 || d.size()!=strings.size()+3) return false;
    for (const auto &key:strings) if (!d.value(key).isString() || d.value(key).toString().isEmpty()) return false;
    const QRegularExpression hash("\\A[0-9a-f]{64}\\z");
    for (const auto key:{"quoteCommitment","reservationCommitment","recoveryCommitment","txid"}) if (!hash.match(d.value(key).toString()).hasMatch()) return false;
    for (const auto key:{"signedCommitment","dispatchCommitment"})
        if (!d.value(key).isNull() && !hash.match(d.value(key).toString()).hasMatch()) return false;
    const bool retry=d.value("action")=="retransmit";
    if ((!retry && d.value("action")!="send") || retry==d.value("dispatchCommitment").isNull()
        || (retry && d.value("signedCommitment").isNull()) || d.value("schema")!="monzero-ltc-lock-funding-review-v1"
        || d.value("tradeId")!=m_trade || d.value("quoteCommitment")!=m_quoteCommitment
        || d.value("wallet")!=m_rpc.value("ltcWallet") || d.value("anchor")!=m_rpc.value("ltcAnchor")
        || d.value("amountAtoms")!=m_terms.value("ltcAtoms") || d.value("maximumFeeAtoms")!=m_terms.value("ltcFundingFeeMaxAtoms")
        || d.value("refundAddress")!=m_terms.value("ltcRefund") || d.value("expiresAt")!=m_terms.value("expiresAt")
        || d.value("chainObserved")!=false) return false;
    QMap<QString,quint64> values;
    for (const auto key:{"amountAtoms","feeAtoms","maximumFeeAtoms","totalDebitAtoms","changeAtoms","inputCount","expiresAt"}) {
        bool ok=false;const auto value=d.value(key).toString().toULongLong(&ok);
        if (!ok || QString::number(value)!=d.value(key).toString()) return false;values.insert(key,value);
    }
    if (!values["amountAtoms"] || !values["maximumFeeAtoms"] || values["feeAtoms"]>values["maximumFeeAtoms"]
        || values["amountAtoms"]>std::numeric_limits<quint64>::max()-values["feeAtoms"]
        || values["totalDebitAtoms"]!=values["amountAtoms"]+values["feeAtoms"]
        || !values["inputCount"] || values["inputCount"]>10000 || !values["expiresAt"]
        || values["expiresAt"]>quint64(std::numeric_limits<qint64>::max())) return false;
    const auto network=m_terms.value("ltcNetwork").toString();
    if (!LitecoinJournalProtocol::witnessAddress(d.value("contract").toString(),network,"q[023456789acdefghjklmnpqrstuvwxyz]{58}")
        || !LitecoinJournalProtocol::witnessAddress(d.value("change").toString(),network,"q[023456789acdefghjklmnpqrstuvwxyz]{38}")) return false;
    QByteArray bytes("MONZERO-LTC-LOCK-FUNDING-APPROVAL-V1");bytes.append('\0');bytes+=QJsonDocument(d).toJson(QJsonDocument::Compact);
    return review.value("commitment").toString().toLatin1()==QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();
}
void LitecoinReservationJob::finish(bool success) {
    if (!m_busy) return;
    m_busy=false;m_deadline.stop();
    if (m_operation=="reservation-prepare") {m_state="unknown";m_review.clear();}
    else if (!m_operation.startsWith("recovery-")) {m_fundingState="unknown";m_fundingReview.clear();m_fundingSubmission.clear();}
    if (m_process.state()!=QProcess::NotRunning) m_process.kill();
    const auto doc=QJsonDocument::fromJson(m_output);const auto response=doc.object();
    const auto snapshot=response.value("snapshot").toObject(),r=response.value("reservation").toObject();
    const auto psbt=r.value("psbt").toString().toLatin1(),decoded=QByteArray::fromBase64(psbt);
    bool bound=LitecoinJournalProtocol::snapshotValid(snapshot,m_trade,m_role)
        && snapshot.value("keysReady").toBool() && snapshot.value("quoteCommitment")==m_quoteCommitment;
    for (const auto key:{"ltcNetwork","xmzGenesis","ltcAtoms","xmzAtoms","ltcPayout","ltcRefund","xmzPayout","xmzRefund"})
        bound=bound && snapshot.value(key)==m_terms.value(key);
    if (m_operation.startsWith("recovery-")) {
        const QString wireOperation=m_role=="native-funder"?"native-"+m_operation:m_operation;
        m_recoveryState="unknown";m_recoveryReview.clear();m_recoverySubmission.clear();
        const auto kind=m_operation.section('-',1,1);const bool reviewing=m_operation.endsWith("-review"),checking=m_operation.endsWith("-check");
        const bool envelope=success && sourcesMatch() && doc.isObject() && doc.toJson(QJsonDocument::Compact)==m_output
            && response.size()==4 && response.value("schema")=="monzero-ltc-"+wireOperation+"-response-v1"
            && response.value("requestId")==m_request && bound && snapshot.value("recoveryIntentRetained").toBool();
        if (checking) {
            m_recoveryState="not-reviewed";m_recoveryCheckState="unknown";
            const auto o=response.value("observation").toObject();
            const QRegularExpression hash("\\A[0-9a-f]{64}\\z");
            bool valid=envelope && o.size()==11 && o.value("schema")=="monzero-ltc-recovery-confirmation-v1"
                && o.value("tradeId")==m_trade && o.value("quoteCommitment")==m_quoteCommitment
                && o.value("operation")==kind && o.value("chainObserved")==true;
            for (const auto key:{"txid","wtxid","block","tip"}) valid=valid && hash.match(o.value(key).toString()).hasMatch();
            bool depthOk=false,requiredOk=false;
            const auto depth=o.value("confirmations").toString().toULongLong(&depthOk);
            const auto required=o.value("requiredConfirmations").toString().toULongLong(&requiredOk);
            valid=valid && depthOk && requiredOk && required==m_requiredConfirmations && depth>=required
                && QString::number(depth)==o.value("confirmations").toString()
                && QString::number(required)==o.value("requiredConfirmations").toString();
            if (valid) {m_recoveryObservation=o.toVariantMap();m_recoveryCheckState="confirmed";}
        } else if (envelope && reviewing && recoveryReviewValid(response.value("review").toObject(),kind)) {
            m_recoveryReview=response.value("review").toObject().toVariantMap();m_recoveryState="reviewed";
            const auto d=m_recoveryReview.value("details").toMap();QVariantMap amounts;
            for (const auto key:{"inputAtoms","amountAtoms","feeAtoms"}) amounts.insert(key,DexUtils::formatAtomic(d.value(key).toString().toULongLong(),8));
            m_recoveryReview.insert("amounts",amounts);
        } else if (envelope && !reviewing) {
            const auto outcome=response.value("outcome").toObject();const auto state=outcome.value("state").toString();
            const auto recorded=snapshot.value("litecoinRecovery").toObject().value(kind).toObject();
            if (outcome.size()==2 && outcome.value("txid")==QJsonValue::fromVariant(m_pendingRecovery.value("txid"))
                && QStringList{"acknowledged","uncertain","mempool"}.contains(state) && recorded.value("attempted").toBool()
                && (state!="acknowledged" || recorded.value("acknowledged").toBool())) {
                m_recoverySubmission=outcome.toVariantMap();m_recoveryState=state;
            }
        }
        m_pendingRecovery.clear();m_output.clear();m_input.clear();emit changed();return;
    }
    if (m_operation!="reservation-prepare") {
        const bool envelope=success && sourcesMatch() && doc.isObject() && doc.toJson(QJsonDocument::Compact)==m_output
            && response.size()==4 && response.value("schema")=="monzero-ltc-"+m_operation+"-response-v1"
            && response.value("requestId")==m_request && bound && snapshot.value("recoveryIntentRetained").toBool();
        if (envelope && m_operation=="lock-funding-review" && fundingReviewValid(response.value("review").toObject())) {
            m_fundingReview=response.value("review").toObject().toVariantMap();m_fundingState="reviewed";
            const auto details=m_fundingReview.value("details").toMap();QVariantMap amounts;
            for (const auto key:{"amountAtoms","feeAtoms","maximumFeeAtoms","totalDebitAtoms","changeAtoms"})
                amounts.insert(key,DexUtils::formatAtomic(details.value(key).toString().toULongLong(),8));
            m_fundingReview.insert("amounts",amounts);
            m_fundingReview.insert("expiresAtUtc",QDateTime::fromSecsSinceEpoch(details.value("expiresAt").toString().toLongLong(),Qt::UTC).toString(Qt::ISODate));
        }
        if (envelope && m_operation=="lock-funding-submit") {
            const auto outcome=response.value("outcome").toObject();const auto state=outcome.value("state").toString();
            const auto expected=m_pendingFunding.value("txid").toString();bool accepted=false;
            if (state=="submitted") accepted=outcome.size()==3 && outcome.value("txid")==expected && outcome.value("receipt_persisted").isBool();
            else if (state=="uncertain" || state=="reconcile") accepted=outcome.size()==2 && outcome.value("txid")==expected;
            else if (state=="observed") {
                const auto observation=outcome.value("observation").toObject();const auto observed=observation.value("state").toString();
                const QRegularExpression hash("\\A[0-9a-f]{64}\\z");
                bool shape=false;
                if (observed=="mempool" || observed=="walletReportsConflict") shape=observation.size()==3;
                else if (observed=="unobserved") shape=observation.size()==5
                    && observation.value("inputs_available").isBool() && observation.value("wallet_has_transaction").isBool();
                else if (observed=="confirmed" || observed=="confirmedOutput") {
                    const auto confirmations=observation.value("confirmations");
                    shape=confirmations.isDouble() && confirmations.toDouble()>=1 && confirmations.toDouble()<=9007199254740991.0
                        && confirmations.toDouble()==double(quint64(confirmations.toDouble()))
                        && (observed=="confirmedOutput" ? observation.size()==4 : observation.size()==5
                            && hash.match(observation.value("block").toString()).hasMatch());
                }
                accepted=outcome.size()==2 && observation.value("txid")==expected && shape
                    && hash.match(observation.value("tip").toString()).hasMatch();
                if (accepted) m_fundingSubmission.insert("observationState",observed);
            }
            if (accepted) {m_fundingState=state;m_fundingSubmission.insert("state",state);m_fundingSubmission.insert("txid",expected);}
        }
        m_pendingFunding.clear();m_output.clear();m_input.clear();emit changed();return;
    }
    bool feeOk=false,maxOk=false;
    const auto fee=r.value("feeAtoms").toString().toULongLong(&feeOk),maximum=r.value("maximumFeeAtoms").toString().toULongLong(&maxOk);
    const bool valid=success && sourcesMatch() && doc.isObject() && doc.toJson(QJsonDocument::Compact)==m_output
        && response.size()==4 && response.value("schema")=="monzero-ltc-reservation-prepare-response-v1"
        && response.value("requestId")==m_request && bound && r.size()==11
        && r.value("reserved")==true && r.value("signed")==false && r.value("broadcast")==false
        && !psbt.isEmpty() && psbt.size()<=100000 && decoded.startsWith(QByteArray::fromHex("70736274ff")) && decoded.toBase64()==psbt
        && r.value("wallet")==m_rpc.value("ltcWallet") && r.value("anchor")==m_rpc.value("ltcAnchor")
        && r.value("amountAtoms")==m_terms.value("ltcAtoms") && r.value("maximumFeeAtoms")==m_terms.value("ltcFundingFeeMaxAtoms")
        && feeOk && maxOk && maximum>0 && fee<=maximum && QString::number(fee)==r.value("feeAtoms").toString()
        && QRegularExpression("\\A[0-9a-f]{64}\\z").match(r.value("txid").toString()).hasMatch()
        && LitecoinJournalProtocol::witnessAddress(r.value("contract").toString(),m_terms.value("ltcNetwork").toString(),"q[023456789acdefghjklmnpqrstuvwxyz]{58}");
    m_output.clear();m_input.clear();
    if (valid) {
        auto review=r;review.remove("psbt");m_review=review.toVariantMap();m_state="reserved";
        for (const auto key:{"amountAtoms","feeAtoms","maximumFeeAtoms"})
            m_review.insert(QString(key)+"Display",DexUtils::formatAtomic(r.value(key).toString().toULongLong(),8));
    }
    QPointer<LitecoinReservationJob> guard(this);emit changed();if (guard && valid) emit prepared(psbt);
}
