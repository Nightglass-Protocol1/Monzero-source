#ifndef MONZERO_LITECOIN_JOURNAL_PROTOCOL_H
#define MONZERO_LITECOIN_JOURNAL_PROTOCOL_H
#include "BackendExecutable.h"
#include <QHash>
#include <QJsonObject>
#include <QRegularExpression>
namespace LitecoinJournalProtocol {
// Public wallet journals can reach 2 MB; configuration limits remain separate.
inline QByteArray privateRecord(const QString &path,qint64 maximum) {
#ifndef Q_OS_LINUX
    Q_UNUSED(path)
    Q_UNUSED(maximum)
    return {};
#else
    if (maximum<=0 || maximum>2000000) return {};
    const QFileInfo info(path);
    if (!info.isAbsolute() || info.canonicalFilePath()!=path) return {};
    const int fd=::open(QFile::encodeName(path).constData(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    if (fd<0) return {};
    QFile file;
    if (!file.open(fd,QIODevice::ReadOnly,QFileDevice::AutoCloseHandle)) {::close(fd);return {};}
    struct stat before{},after{};
    if (::fstat(fd,&before)!=0 || !S_ISREG(before.st_mode) || before.st_uid!=::geteuid()
        || before.st_nlink!=1 || (before.st_mode&07777)!=0600 || before.st_size<=0 || before.st_size>maximum) return {};
    const auto bytes=file.read(maximum+1);
    if (::fstat(fd,&after)!=0 || bytes.size()!=before.st_size || before.st_size!=after.st_size
        || before.st_mtim.tv_sec!=after.st_mtim.tv_sec || before.st_mtim.tv_nsec!=after.st_mtim.tv_nsec
        || before.st_ctim.tv_sec!=after.st_ctim.tv_sec || before.st_ctim.tv_nsec!=after.st_ctim.tv_nsec) return {};
    return bytes;
#endif
}
inline QByteArray privateConfig(const QString &path,qint64 maximum=16384) {
    if (maximum<=0 || maximum>65536) return {};
    return privateRecord(path,maximum);
}
// Bech32 prefix of the trade's Litecoin or Bitcoin network; empty for an unknown network.
inline QString bech32Prefix(const QString &network) {
    static const QHash<QString,QString> prefixes{{"ltc-mainnet","ltc1"},{"ltc-testnet","tltc1"},{"ltc-regtest","rltc1"},
        {"btc-mainnet","bc1"},{"btc-testnet4","tb1"},{"btc-signet","tb1"},{"btc-regtest","bcrt1"}};
    return prefixes.value(network);
}
// `tail` is the pattern after the prefix, e.g. "q[...]{58}" for P2WSH.
inline bool witnessAddress(const QString &address,const QString &network,const QString &tail) {
    const auto prefix=bech32Prefix(network);
    return !prefix.isEmpty() && address.startsWith(prefix)
        && QRegularExpression("\\A(?:"+tail+")\\z").match(address.mid(prefix.size())).hasMatch();
}
inline bool snapshotValid(const QJsonObject &s,const QString &trade,const QString &role) {
    const QStringList strings{"schema","tradeId","quoteCommitment","role","ltcNetwork","xmzGenesis",
        "ltcAtoms","xmzAtoms","ltcPayout","ltcRefund","xmzPayout","xmzRefund"};
    const QStringList flags{"keysReady","recoveryIntentRetained","nativeClaimRetained","nativeRefundRetained",
        "journalValidated","chainObserved","executable"};
    const QStringList payments{"nativeFunding","litecoinRedemption","nativePayout","nativeRefundPayout"};
    if (s.size()!=strings.size()+flags.size()+payments.size()+1) return false;
    for (const auto &key:strings) if (!s.value(key).isString() || s.value(key).toString().isEmpty() || s.value(key).toString().size()>128) return false;
    for (const auto &key:flags) if (!s.value(key).isBool()) return false;
    if (s.value("schema")!="monzero-ltc-journal-status-v1" || s.value("tradeId")!=trade || s.value("role")!=role
        || !s.value("journalValidated").toBool() || s.value("chainObserved").toBool() || s.value("executable").toBool()) return false;
    static const QRegularExpression hash("\\A[0-9a-f]{64}\\z"),atoms("\\A[1-9][0-9]{0,19}\\z");
    for (const auto &key:{"quoteCommitment","xmzGenesis"}) if (!hash.match(s.value(key).toString()).hasMatch()) return false;
    for (const auto &key:{"ltcAtoms","xmzAtoms"}) {
        bool ok=false;s.value(key).toString().toULongLong(&ok);
        if (!ok || !atoms.match(s.value(key).toString()).hasMatch()) return false;
    }
    if (bech32Prefix(s.value("ltcNetwork").toString()).isEmpty()) return false;
    for (const auto &key:payments) {
        const auto payment=s.value(key).toObject();
        if (payment.size()!=3) return false;
        for (const auto &flag:{"prepared","attempted","acknowledged"}) if (!payment.value(flag).isBool()) return false;
        if ((payment.value("acknowledged").toBool() && !payment.value("attempted").toBool())
            || (payment.value("attempted").toBool() && !payment.value("prepared").toBool())) return false;
    }
    const auto recovery=s.value("litecoinRecovery").toObject();
    if (recovery.size()!=2) return false;
    for (const auto &key:QStringList{"cancel",role=="native-funder"?"punish":"refund"}) {
        const auto item=recovery.value(key).toObject();
        if (item.size()!=2 || !item.value("attempted").isBool() || !item.value("acknowledged").isBool()
            || (item.value("acknowledged").toBool() && !item.value("attempted").toBool())) return false;
    }
    return true;
}
}
#endif
