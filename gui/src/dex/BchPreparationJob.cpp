#include "BchPreparationJob.h"
#include "BchAdmissionProtocol.h"
#include "LitecoinJournalProtocol.h"
#include <QDateTime>
#include <QPointer>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
using namespace BchAdmissionProtocol;
namespace {
QByteArray privateCandidate(const QString &path) {
#ifndef Q_OS_LINUX
    Q_UNUSED(path)
    return {};
#else
    const QFileInfo info(path);
    if (!info.isAbsolute() || info.canonicalFilePath()!=path) return {};
    const int fd=::open(QFile::encodeName(path).constData(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    if (fd<0) return {};
    QFile file;if (!file.open(fd,QIODevice::ReadOnly,QFileDevice::AutoCloseHandle)) {::close(fd);return {};}
    struct stat before{},after{},named{};
    if (::fstat(fd,&before)!=0 || !S_ISREG(before.st_mode) || before.st_uid!=::geteuid()
        || before.st_nlink!=1 || (before.st_mode&07777)!=0600 || before.st_size<=0 || before.st_size>1000000) return {};
    const auto bytes=file.read(1000001);
    if (::fstat(fd,&after)!=0 || ::lstat(QFile::encodeName(path).constData(),&named)!=0
        || !S_ISREG(named.st_mode) || named.st_dev!=before.st_dev || named.st_ino!=before.st_ino
        || bytes.size()!=before.st_size || before.st_size!=after.st_size
        || before.st_mtim.tv_sec!=after.st_mtim.tv_sec || before.st_mtim.tv_nsec!=after.st_mtim.tv_nsec
        || before.st_ctim.tv_sec!=after.st_ctim.tv_sec || before.st_ctim.tv_nsec!=after.st_ctim.tv_nsec) return {};
    return bytes;
#endif
}
QString candidateCommitment(const QByteArray &raw) {
    QByteArray binding("monzero-bch-signed-funding-v1");binding.append('\0');binding+=raw;
    return QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex());
}
QString candidateTxid(const QByteArray &raw) {
    auto digest=QCryptographicHash::hash(QCryptographicHash::hash(raw,QCryptographicHash::Sha256),QCryptographicHash::Sha256);
    std::reverse(digest.begin(),digest.end());return QString::fromLatin1(digest.toHex());
}
constexpr quint64 MaxMoney=2100000000000000ULL;
bool decimal(const QJsonValue &value,quint64 maximum,quint64 *result=nullptr) {
    if (!value.isString()) return false;
    const auto text=value.toString();bool ok=false;const auto number=text.toULongLong(&ok);
    if (!ok || QString::number(number)!=text || number>maximum) return false;
    if (result) *result=number;return true;
}
bool destination(const QString &script) {
    return hex(script,script.size()) && ((script.size()==50 && script.startsWith("76a914") && script.endsWith("88ac"))
        || (script.size()==46 && script.startsWith("a914") && script.endsWith("87"))
        || (script.size()==70 && script.startsWith("aa20") && script.endsWith("87")));
}
bool policyValid(const QJsonObject &policy) {
    if (policy.size()!=6 || !destination(policy.value("change_script").toString())
        || !QRegularExpression("\\Abchreg:[a-z0-9]{1,121}\\z").match(policy.value("change_address").toString()).hasMatch()) return false;
    for (const auto key:{"fee_rate_atoms_per_kb","maximum_fee","minimum_change","minimum_confirmations"}) {
        quint64 value=0;if (!integer(policy.value(key),MaxMoney,&value) || !value) return false;
    }
    return true;
}
}
BchPreparationJob::BchPreparationJob(QObject *parent):QObject(parent) {
    m_deadline.setSingleShot(true);
    connect(&m_deadline,&QTimer::timeout,this,[this]{finish(false);});
    connect(&m_process,&QProcess::started,this,[this]{
        if (!m_busy || m_process.write(m_input)!=m_input.size()) {finish(false);return;}
        m_process.closeWriteChannel();m_input.clear();
    });
    connect(&m_process,&QProcess::readyReadStandardOutput,this,&BchPreparationJob::drain);
    connect(&m_process,&QProcess::readyReadStandardError,this,&BchPreparationJob::drain);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){finish(false);});
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status){
        QPointer<BchPreparationJob> guard(this);const auto request=m_request;
        drain();if (guard && request==m_request) finish(code==0 && status==QProcess::NormalExit);
    });
}
BchPreparationJob::~BchPreparationJob() {
    disconnect(&m_process,nullptr,this,nullptr);
    if (m_process.state()!=QProcess::NotRunning) {m_process.kill();m_process.waitForFinished(1000);}
}
bool BchPreparationJob::configure(const QString &path) {
    if (!m_path.isEmpty() || m_busy) return false;
    QJsonObject wrapper,funding,config,quote,admission;
    const auto bytes=LitecoinJournalProtocol::privateConfig(path);
    if (!canonicalObject(bytes,&wrapper) || wrapper.size()!=4 || wrapper.value("schema")!="monzero-private-bch-preparation-v1"
        || !policyValid(wrapper.value("policy").toObject())) return false;
    const auto rpc=wrapper.value("rpcFile").toString();
    if (!QDir::isAbsolutePath(rpc) || QDir::cleanPath(rpc)!=rpc) return false;
    const auto fundingPath=wrapper.value("fundingConfigFile").toString();
    const auto fundingBytes=LitecoinJournalProtocol::privateConfig(fundingPath);
    if (!canonicalObject(fundingBytes,&funding) || funding.size()!=3 || funding.value("schema")!="monzero-private-bch-funding-v1") return false;
    const auto deployment=funding.value("deploymentCommitment").toString();
    if (!hex(deployment,64) || deployment==QString(64,'0')) return false;
    const auto admissionPath=funding.value("admissionConfigFile").toString();
    const auto admissionBytes=LitecoinJournalProtocol::privateConfig(admissionPath);
    if (!canonicalObject(admissionBytes,&config) || config.value("role")!="bch-funder") return false;
    const auto quotePath=config.value("quoteFile").toString();
    const auto quoteBytes=LitecoinJournalProtocol::privateConfig(quotePath);
    if (!canonicalObject(quoteBytes,&quote) || !expectedReview(config,quote,&admission)
        || !BackendExecutable::matches(config.value("helper").toString(),config.value("helperDigest").toString())) return false;
    m_path=path;m_wrapperBytes=bytes;m_fundingPath=fundingPath;m_fundingBytes=fundingBytes;
    m_admissionPath=admissionPath;m_admissionBytes=admissionBytes;m_quotePath=quotePath;m_quoteBytes=quoteBytes;
    m_rpcPath=rpc;m_program=config.value("helper").toString();m_digest=config.value("helperDigest").toString();
    m_trade=config.value("tradeId").toString();m_deployment=deployment;m_policy=wrapper.value("policy").toObject();
    m_terms=quote.value("terms").toObject();m_config=config;m_valid=true;m_state="ready";emit changed();return true;
}
bool BchPreparationJob::configureSigningRecovery(const QString &path) {
    if (m_path.isEmpty() || !m_recoveryPath.isEmpty() || m_busy || !sourcesMatch()) return false;
    const auto bytes=LitecoinJournalProtocol::privateConfig(path);QJsonObject config;
    if (!canonicalObject(bytes,&config) || config.size()!=3 || config.value("schema")!="monzero-private-bch-signing-recovery-v1"
        || config.value("preparationConfigFile")!=m_path) return false;
    const auto candidate=config.value("recoveredTransactionFile").toString();
    if (!QDir::isAbsolutePath(candidate) || QDir::cleanPath(candidate)!=candidate) return false;
    m_recoveryPath=path;m_recoveryBytes=bytes;m_candidatePath=candidate;emit changed();return true;
}
bool BchPreparationJob::sourcesMatch() const {
    return m_valid && (m_recoveryPath.isEmpty() || LitecoinJournalProtocol::privateConfig(m_recoveryPath)==m_recoveryBytes)
        && BackendExecutable::matches(m_program,m_digest)
        && LitecoinJournalProtocol::privateConfig(m_path)==m_wrapperBytes
        && LitecoinJournalProtocol::privateConfig(m_fundingPath)==m_fundingBytes
        && LitecoinJournalProtocol::privateConfig(m_admissionPath)==m_admissionBytes
        && LitecoinJournalProtocol::privateConfig(m_quotePath)==m_quoteBytes;
}
QVariantMap BchPreparationJob::status() const {
    quint64 expiry=0;const bool fresh=decimal(m_terms.value("expiresAt"),9007199254740991ULL,&expiry)
        && expiry>quint64(QDateTime::currentSecsSinceEpoch());
    return {{"configured",!m_path.isEmpty()},{"busy",m_busy},{"state",m_state},{"tradeId",m_trade},
        {"prepareAvailable",m_valid && !m_busy && fresh},{"checkAvailable",m_valid && !m_busy},
        {"lockReviewAvailable",m_valid && !m_busy && fresh},{"lockCheckAvailable",m_valid && !m_busy},
        {"lockSubmitAvailable",m_valid && !m_busy && fresh && !m_lockReview.isEmpty() && validLockReview(m_lockReview)},
        {"lockReview",m_lockReview.toVariantMap()},{"lockIntent",m_lockIntent.toVariantMap()},
        {"lockStatusKnown",m_lockKnown},{"lockObservation",m_lockObservation.toVariantMap()},
        {"signingReviewAvailable",m_valid && !m_busy && fresh},{"signingCheckAvailable",m_valid && !m_busy},
        {"signingSubmitAvailable",m_valid && !m_busy && fresh && !m_signingReview.isEmpty()
            && validSigningReview(m_signingReview) && (m_signingIntent.isEmpty() || !m_signedFunding.isEmpty())},
        {"recoveryConfigured",!m_recoveryPath.isEmpty()},
        {"recoveryReviewAvailable",m_valid && !m_busy && !m_recoveryPath.isEmpty()},
        {"recoverySubmitAvailable",m_valid && !m_busy && !m_recoveryReview.isEmpty()},
        {"recoveryReview",m_recoveryReview.toVariantMap()},
        {"signingReview",m_signingReview.toVariantMap()},{"signingIntent",m_signingIntent.toVariantMap()},
        {"signedFunding",m_signedFunding.toVariantMap()},{"signingStatusKnown",m_signingKnown},
        {"result",m_result.toVariantMap()},{"executable",false},{"chainObserved",!m_lockObservation.isEmpty()}};
}
bool BchPreparationJob::prepare() {if (!status().value("prepareAvailable").toBool()) return false;return run("prepare");}
bool BchPreparationJob::refresh() {return run("preparation-status");}
bool BchPreparationJob::reviewLocks() {if (!status().value("lockReviewAvailable").toBool()) return false;return run("lock-review");}
bool BchPreparationJob::checkLocks() {return run("lock-status");}
bool BchPreparationJob::lockInputs(const QString &commitment) {
    if (!status().value("lockSubmitAvailable").toBool() || commitment!=m_lockReview.value("commitment").toString()) return false;
    return run("lock-submit",commitment);
}
bool BchPreparationJob::reviewSigning() {
    if (!status().value("signingReviewAvailable").toBool()) return false;return run("signing-review");
}
bool BchPreparationJob::checkSigning() {return run("signing-status");}
bool BchPreparationJob::sign(const QString &commitment) {
    if (!status().value("signingSubmitAvailable").toBool() || commitment!=m_signingReview.value("commitment").toString()) return false;
    return run("signing-submit",commitment);
}
bool BchPreparationJob::reviewSigningRecovery() {
    if (!status().value("recoveryReviewAvailable").toBool()) return false;return run("signing-recovery-review");
}
bool BchPreparationJob::importSigningRecovery(const QString &commitment) {
    if (!status().value("recoverySubmitAvailable").toBool() || commitment!=m_recoveryReview.value("commitment").toString()) return false;
    return run("signing-recovery-submit",commitment);
}
bool BchPreparationJob::run(const QString &action,const QString &commitment) {
    if (m_busy || m_path.isEmpty() || m_process.state()!=QProcess::NotRunning) return false;
    const bool recovery=action.startsWith("signing-recovery-"),recoverySubmit=action=="signing-recovery-submit";
    const auto reviewed=m_reviewedCandidate;
    m_approvedRecovery=recoverySubmit?m_recoveryReview:QJsonObject{};
    m_recoveryReview={};m_reviewedCandidate.clear();m_pendingCandidate.clear();m_pendingTxid.clear();
    m_approvedLock=action=="lock-submit"?m_lockReview:QJsonObject{};
    m_approvedSigning=action=="signing-submit"?m_signingReview:QJsonObject{};
    m_signingReview={};m_signingIntent={};m_signedFunding={};m_signingKnown=false;
    m_result={};m_lockReview={};m_lockIntent={};m_lockObservation={};m_lockKnown=false;
    if (!sourcesMatch()) {m_valid=false;m_state="unknown";emit changed();return false;}
    if (recovery) {
        const auto raw=privateCandidate(m_candidatePath);
        if (raw.isEmpty() || (recoverySubmit && candidateCommitment(raw)!=reviewed)) {
            m_state="unknown";emit changed();return false;
        }
        m_pendingCandidate=candidateCommitment(raw);m_pendingTxid=candidateTxid(raw);
    }
    m_rpcBytes.clear();
    if (action=="prepare" || action=="lock-submit" || action=="signing-submit") {
        QJsonObject rpc;m_rpcBytes=LitecoinJournalProtocol::privateConfig(m_rpcPath);
        if (!canonicalObject(m_rpcBytes,&rpc) || rpc.size()!=9 || rpc.value("schema")!="monzero-private-bch-funding-rpc-v1") {
            m_state="unknown";emit changed();return false;
        }
    }
    m_operation=action;m_request=QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject request{{"schema","monzero-bch-funding-"+action+"-request-v1"},{"requestId",m_request}};
    if (action=="lock-submit" || action=="signing-submit" || recoverySubmit) request["commitment"]=commitment;
    m_input=QJsonDocument(request).toJson(QJsonDocument::Compact);
    m_startedAt=QDateTime::currentSecsSinceEpoch();
    m_output.clear();m_errors=0;m_busy=true;m_state=action=="prepare"?"preparing":action=="lock-submit"?"locking":action=="signing-submit"?"signing":"checking";
    QProcessEnvironment environment;environment.insert("LANG","C");m_process.setProcessEnvironment(environment);
    m_process.setProcessChannelMode(QProcess::SeparateChannels);m_deadline.start(180000);
    m_process.start(m_program,{"--funding-"+action+"-stdio-v1",recovery?m_recoveryPath:m_path});emit changed();return true;
}
bool BchPreparationJob::validReview(const QJsonObject &review) const {
    if (review.size()!=2 || !review.value("details").isObject() || !hex(review.value("commitment").toString(),64)) return false;
    const auto d=review.value("details").toObject();
    if (d.size()!=16 || d.value("schema")!="monzero-bch-wallet-preparation-review-v1"
        || d.value("action")!="inspect-unsigned-preparation" || d.value("swapId")!=m_trade
        || d.value("quote")!=m_config.value("quoteCommitment") || d.value("deployment")!=m_deployment
        || d.value("network")!=m_terms.value("bchNetwork") || d.value("genesis")!=m_terms.value("bchGenesis")
        || d.value("expiresAt")!=m_terms.value("expiresAt") || d.value("policy").toObject()!=m_policy) return false;
    for (const auto key:{"admission","preparation"})
        if (!hex(d.value(key).toString(),64) || d.value(key).toString()==QString(64,'0')) return false;
    quint64 prepared=0,expiry=0,fee=0,cap=0,principal=0,vout=0;
    if (!decimal(d.value("preparedAt"),9007199254740991ULL,&prepared)
        || prepared<quint64(m_config.value("admittedAt").toDouble()) || prepared>quint64(QDateTime::currentSecsSinceEpoch())
        || !decimal(d.value("expiresAt"),9007199254740991ULL,&expiry) || prepared>=expiry
        || !decimal(d.value("feeAtoms"),MaxMoney,&fee) || !fee
        || !decimal(m_terms.value("bchFundingFeeMaxAtoms"),MaxMoney,&cap) || fee>cap
        || fee>quint64(m_policy.value("maximum_fee").toDouble())
        || !decimal(m_terms.value("bchAtoms"),MaxMoney,&principal) || !principal
        || !integer(d.value("contractVout"),0,&vout) || !d.value("inputs").isArray() || !d.value("outputs").isArray()) return false;
    const auto inputs=d.value("inputs").toArray(),outputs=d.value("outputs").toArray();
    if (inputs.isEmpty() || inputs.size()>128 || outputs.isEmpty() || outputs.size()>2) return false;
    quint64 inputTotal=0,outputTotal=0;QSet<QString> seen;
    for (const auto value:inputs) {
        const auto input=value.toObject();const auto point=input.value("outpoint").toString();
        const auto script=input.value("script").toString();quint64 atoms=0;
        const auto parts=point.split(':');quint64 index=0;
        if (input.size()!=3 || parts.size()!=2 || !hex(parts[0],64) || parts[0]==QString(64,'0')
            || !decimal(parts[1],4294967295ULL,&index) || seen.contains(point)
            || !decimal(input.value("valueAtoms"),MaxMoney,&atoms) || !atoms
            || script.size()!=50 || !destination(script)) return false;
        seen.insert(point);inputTotal+=atoms;if (inputTotal>MaxMoney) return false;
    }
    for (int i=0;i<outputs.size();++i) {
        const auto output=outputs[i].toObject();const auto script=output.value("script").toString();quint64 atoms=0,index=0;
        if (output.size()!=3 || !integer(output.value("vout"),1,&index) || index!=quint64(i)
            || !decimal(output.value("valueAtoms"),MaxMoney,&atoms) || !atoms
            || script.isEmpty() || script.size()>10000 || script.size()%2 || !hex(script,script.size())
            || (i==0 && (atoms!=principal || script==m_policy.value("change_script").toString()))
            || (i!=0 && (script!=m_policy.value("change_script").toString()
                || atoms<quint64(m_policy.value("minimum_change").toDouble())))) return false;
        outputTotal+=atoms;if (outputTotal>MaxMoney) return false;
    }
    if (inputTotal<outputTotal || inputTotal-outputTotal!=fee) return false;
    QByteArray binding("monzero-bch-wallet-preparation-review-v1");binding.append('\0');binding+=QJsonDocument(d).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex())==review.value("commitment").toString();
}
bool BchPreparationJob::validRecoveryReview(const QJsonObject &review) const {
    const auto d=review.value("details").toObject(),nested=d.value("signingReview").toObject();
    QByteArray binding("monzero-bch-funding-signing-review-v1");binding.append('\0');binding+=QJsonDocument(nested).toJson(QJsonDocument::Compact);
    const auto nestedDigest=QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex());
    quint64 time=0,expiry=0,vout=0;
    if (review.size()!=2 || d.size()!=10 || d.value("schema")!="monzero-bch-signing-reconciliation-v1"
        || d.value("action")!="retain-recovered-signature-only" || d.value("swapId")!=m_trade
        || !validSigningReview(QJsonObject{{"details",nested},{"commitment",nestedDigest}})
        || d.value("admission")!=nested.value("admission")
        || pin(d.value("originalSigningIntent")).isEmpty() || pin(d.value("originalSigningIntent"))==QString(64,'0')
        || pin(d.value("signedBytes"))!=m_pendingCandidate || d.value("fundingTxid")!=m_pendingTxid
        || !integer(d.value("contractVout"),0,&vout)
        || !integer(d.value("originalApprovedAt"),9007199254740991ULL,&time)
        || !decimal(m_terms.value("expiresAt"),9007199254740991ULL,&expiry)
        || time<quint64(m_config.value("admittedAt").toDouble()) || time>=expiry
        || time>quint64(QDateTime::currentSecsSinceEpoch())) return false;
    binding=QByteArray("monzero-bch-signing-reconciliation-v1");binding.append('\0');binding+=QJsonDocument(d).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex())==review.value("commitment").toString();
}
bool BchPreparationJob::validSigningReview(const QJsonObject &review) const {
    if (review.size()!=2 || !review.value("details").isObject() || !hex(review.value("commitment").toString(),64)) return false;
    const auto d=review.value("details").toObject();
    if (d.size()!=17 || d.value("schema")!="monzero-bch-funding-signing-review-v1" || d.value("action")!="sign-only"
        || d.value("swapId")!=m_trade || d.value("network")!=m_terms.value("bchNetwork")
        || d.value("genesis")!=m_terms.value("bchGenesis") || d.value("expiresAt")!=m_terms.value("expiresAt")
        || d.value("changeAddress")!=m_policy.value("change_address")
        || d.value("minimumConfirmations")!=m_policy.value("minimum_confirmations")) return false;
    for (const auto key:{"quote","admission","preparation","walletLockIntent","deployment"}) {
        if (!d.value(key).isArray() || d.value(key).toArray().size()!=32) return false;
        QByteArray bytes;for (const auto item:d.value(key).toArray()) {
            quint64 byte=0;if (!integer(item,255,&byte)) return false;bytes.append(char(byte));
        }
        const auto digest=QString::fromLatin1(bytes.toHex());
        if (digest==QString(64,'0') || (QString(key)=="quote" && digest!=m_config.value("quoteCommitment").toString())
            || (QString(key)=="deployment" && digest!=m_deployment)) return false;
    }
    quint64 fee=0,cap=0,principal=0,vout=0;
    if (!integer(d.value("feeAtoms"),MaxMoney,&fee) || !fee
        || !decimal(m_terms.value("bchFundingFeeMaxAtoms"),MaxMoney,&cap) || fee>cap
        || fee>quint64(m_policy.value("maximum_fee").toDouble())
        || !decimal(m_terms.value("bchAtoms"),MaxMoney,&principal) || !principal
        || !integer(d.value("contractVout"),0,&vout) || !d.value("inputs").isArray() || !d.value("outputs").isArray()) return false;
    const auto inputs=d.value("inputs").toArray(),outputs=d.value("outputs").toArray();
    if (inputs.isEmpty() || inputs.size()>128 || outputs.isEmpty() || outputs.size()>2) return false;
    quint64 inputTotal=0,outputTotal=0;QSet<QString> seen;
    for (const auto value:inputs) {
        const auto input=value.toObject();const auto txid=input.value("txid").toString();
        const auto script=input.value("script").toString();quint64 atoms=0,index=0;
        if (input.size()!=4 || !hex(txid,64) || txid==QString(64,'0') || !integer(input.value("vout"),4294967295ULL,&index)
            || !integer(input.value("atoms"),MaxMoney,&atoms) || !atoms || script.size()!=50 || !destination(script)) return false;
        const auto point=txid+":"+QString::number(index);if (seen.contains(point)) return false;seen.insert(point);
        inputTotal+=atoms;if (inputTotal>MaxMoney) return false;
    }
    for (int i=0;i<outputs.size();++i) {
        const auto output=outputs[i].toObject();const auto script=output.value("script").toString();quint64 atoms=0,index=0;
        if (output.size()!=3 || !integer(output.value("vout"),1,&index) || index!=quint64(i)
            || !integer(output.value("atoms"),MaxMoney,&atoms) || !atoms
            || script.isEmpty() || script.size()>10000 || script.size()%2 || !hex(script,script.size())
            || (i==0 && (atoms!=principal || script==m_policy.value("change_script").toString()))
            || (i!=0 && (script!=m_policy.value("change_script").toString()
                || atoms<quint64(m_policy.value("minimum_change").toDouble())))) return false;
        outputTotal+=atoms;if (outputTotal>MaxMoney) return false;
    }
    if (inputTotal<outputTotal || inputTotal-outputTotal!=fee) return false;
    QByteArray binding("monzero-bch-funding-signing-review-v1");binding.append('\0');binding+=QJsonDocument(d).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex())==review.value("commitment").toString();
}
bool BchPreparationJob::validSigningState(const QJsonObject &response) const {
    const auto intent=response.value("signingIntent"),signedValue=response.value("signedFunding");
    if (intent.isNull()) return signedValue.isNull() && m_operation!="signing-submit" && m_operation!="signing-recovery-submit";
    const auto record=intent.toObject();quint64 time=0,expiry=0;
    if (record.size()!=2 || !hex(record.value("commitment").toString(),64) || record.value("commitment").toString()==QString(64,'0')
        || !decimal(record.value("approvedAt"),9007199254740991ULL,&time)
        || !decimal(m_terms.value("expiresAt"),9007199254740991ULL,&expiry)
        || time<quint64(m_config.value("admittedAt").toDouble()) || time>=expiry
        || time>quint64(QDateTime::currentSecsSinceEpoch())) return false;
    if (signedValue.isNull()) return m_operation!="signing-submit" && m_operation!="signing-recovery-submit";
    const auto signedFunding=signedValue.toObject();quint64 vout=0;
    return signedFunding.size()==2 && hex(signedFunding.value("txid").toString(),64)
        && signedFunding.value("txid").toString()!=QString(64,'0') && integer(signedFunding.value("contractVout"),0,&vout);
}
bool BchPreparationJob::validLockReview(const QJsonObject &review) const {
    if (review.size()!=2 || !review.value("details").isObject() || !hex(review.value("commitment").toString(),64)) return false;
    const auto details=review.value("details").toObject();quint64 expiry=0;
    if (details.size()!=4 || details.value("schema")!="monzero-bch-wallet-lock-review-v1"
        || details.value("action")!="lock-retained-wallet-inputs"
        || !validReview(QJsonObject{{"details",details.value("preparationReview")},{"commitment",details.value("preparationReviewCommitment")}})
        || !decimal(m_terms.value("expiresAt"),9007199254740991ULL,&expiry) || expiry<=quint64(QDateTime::currentSecsSinceEpoch())) return false;
    QByteArray binding("monzero-bch-wallet-lock-review-v1");binding.append('\0');binding+=QJsonDocument(details).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex())==review.value("commitment").toString();
}
bool BchPreparationJob::validLockIntent(const QJsonValue &value,const QJsonObject &preparation) const {
    if (value.isNull()) return m_operation!="lock-submit";
    const auto intent=value.toObject();quint64 time=0,prepared=0,expiry=0;
    return intent.size()==3 && intent.value("state")=="recorded"
        && hex(intent.value("commitment").toString(),64) && intent.value("commitment").toString()!=QString(64,'0')
        && decimal(intent.value("recordedAt"),9007199254740991ULL,&time)
        && decimal(preparation.value("preparedAt"),9007199254740991ULL,&prepared)
        && decimal(preparation.value("expiresAt"),9007199254740991ULL,&expiry)
        && time>=prepared && time<expiry && time<=quint64(QDateTime::currentSecsSinceEpoch());
}
bool BchPreparationJob::validLockObservation(const QJsonObject &observation) const {
    quint64 time=0;
    if (observation.size()!=4 || !observation.value("walletInputsLocked").isBool() || !observation.value("walletInputsLocked").toBool()
        || !decimal(observation.value("observedAt"),9007199254740991ULL,&time)
        || time<quint64(m_startedAt) || time>quint64(QDateTime::currentSecsSinceEpoch())) return false;
    for (const auto key:{"tip","templateHash"})
        if (!hex(observation.value(key).toString(),64) || observation.value(key).toString()==QString(64,'0')) return false;
    return true;
}
void BchPreparationJob::drain() {
    if (!m_busy) return;
    m_process.setReadChannel(QProcess::StandardOutput);m_output+=m_process.read(65537-m_output.size());
    m_process.setReadChannel(QProcess::StandardError);m_errors+=m_process.read(1025-m_errors).size();
    if (m_output.size()>65536 || m_errors>1024) finish(false);
}
void BchPreparationJob::finish(bool success) {
    if (!m_busy) return;m_busy=false;m_deadline.stop();
    if (m_process.state()!=QProcess::NotRunning) m_process.kill();
    QJsonObject response;const bool same=sourcesMatch();m_valid=same;
    const auto action=m_operation;const bool locking=action.startsWith("lock-");
    const bool approved=action=="lock-submit",lockReview=approved || action=="lock-review";
    const bool signing=action.startsWith("signing-"),signingSubmit=action=="signing-submit";
    const bool recovery=action.startsWith("signing-recovery-"),recoverySubmit=action=="signing-recovery-submit";
    const auto candidate=recovery?privateCandidate(m_candidatePath):QByteArray{};
    const bool rpc=action=="prepare" || approved || signingSubmit;
    bool valid=success && same && (!recovery || (!candidate.isEmpty() && candidateCommitment(candidate)==m_pendingCandidate)) && (!rpc || LitecoinJournalProtocol::privateConfig(m_rpcPath)==m_rpcBytes)
        && m_errors==0 && canonicalObject(m_output,&response) && response.size()==(approved || signing?8:locking?7:6)
        && response.value("schema")=="monzero-bch-funding-"+action+"-response-v1"
        && response.value("requestId")==m_request && response.value("tradeId")==m_trade
        && response.value("executable").isBool() && !response.value("executable").toBool()
        && response.value("chainObserved").isBool() && response.value("chainObserved").toBool()==approved
        && response.value("review").isObject();
    const auto review=response.value("review").toObject();
    valid=valid && (recovery?validRecoveryReview(review):signing?validSigningReview(review):lockReview?validLockReview(review):validReview(review));
    const auto details=review.value("details").toObject();
    if (locking) valid=valid && validLockIntent(response.value("lockIntent"),
        lockReview?details.value("preparationReview").toObject():details);
    if (approved) valid=valid && review==m_approvedLock && response.value("observation").isObject()
        && validLockObservation(response.value("observation").toObject());
    if (signing) valid=valid && validSigningState(response) && (!signingSubmit || review==m_approvedSigning);
    if (recovery) {
        const auto intent=response.value("signingIntent").toObject(),signedFunding=response.value("signedFunding").toObject();
        valid=valid && intent.value("commitment")==pin(details.value("originalSigningIntent"))
            && intent.value("approvedAt").toString()==QString::number(quint64(details.value("originalApprovedAt").toDouble()))
            && (response.value("signedFunding").isNull() || signedFunding.value("txid")==m_pendingTxid)
            && (!recoverySubmit || review==m_approvedRecovery);
    }
    m_result=valid?response:QJsonObject{};m_state=valid?(approved?"locks-checked":action=="lock-review"?"lock-reviewed":action=="lock-status"?"lock-status":"prepared"):"unknown";
    if (valid && locking) {
        m_lockIntent=response.value("lockIntent").toObject();m_lockKnown=true;
        if (action=="lock-review") m_lockReview=review;
        if (approved) m_lockObservation=response.value("observation").toObject();
    }
    if (valid && signing) {
        m_signingIntent=response.value("signingIntent").toObject();m_signedFunding=response.value("signedFunding").toObject();
        m_signingKnown=true;m_state=signingSubmit?"signed":action=="signing-review"?"signing-reviewed":"signing-status";
        if (action=="signing-review") m_signingReview=review;
    }
    if (valid && recovery) {
        m_state=recoverySubmit?"signing-recovered":"recovery-reviewed";
        if (!recoverySubmit) {m_recoveryReview=review;m_reviewedCandidate=m_pendingCandidate;}
    }
    m_approvedRecovery={};m_pendingCandidate.clear();m_pendingTxid.clear();
    m_approvedSigning={};
    m_approvedLock={};m_output.clear();m_input.clear();emit changed();
}
