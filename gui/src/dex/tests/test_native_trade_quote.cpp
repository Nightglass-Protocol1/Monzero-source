#include "../NativeTradeQuote.h"
#include "../OfferIdentity.h"
#include "../NativeQuoteReplay.h"
#include "../NativeDestinationJob.h"
#include "../NativeQuoteCoordinator.h"
#include "../NativeQuoteConfig.h"
#include <QDateTime>
#include <QUuid>
#include <QEventLoop>
#include <QTimer>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QProcess>
#ifdef Q_OS_LINUX
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifdef NATIVE_QUOTE_FSYNC_FAULT_TEST
#include <sys/stat.h>
#include <cerrno>
static bool failNextDirectorySync = false;
extern "C" int __real_fsync(int fd);
extern "C" int __wrap_fsync(int fd)
{
    struct stat metadata{};
    if (failNextDirectorySync && ::fstat(fd, &metadata) == 0 && S_ISDIR(metadata.st_mode)) {
        failNextDirectorySync = false;
        errno = EIO;
        return -1;
    }
    return __real_fsync(fd);
}
#endif
#include <QCoreApplication>
#include <QJsonDocument>
#include <QDebug>
#include <cstdio>

static void requireAt(bool ok, int line) { if (!ok) qFatal("native quote assertion failed at line %d", line); }
#define require(condition) requireAt((condition), __LINE__)
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // Test-only bridge: supplied public pins and signatures are fixture inputs.
    // No private signing material, production wallet or network is involved.
    if (app.arguments().size() == 5 && app.arguments().at(1) == "--reserve-signed-fixture") {
        QFile input;
        require(input.open(stdin, QIODevice::ReadOnly));
        const auto bytes = input.read(16385);
        require(!bytes.isEmpty() && bytes.size() <= 16384);
        const auto record = QJsonDocument::fromJson(bytes).object();
        const auto acceptedText = record.value("acceptedAt").toString();
        bool validTime = false;
        const auto accepted = acceptedText.toLongLong(&validTime);
        require(validTime && accepted > 0 && QString::number(accepted) == acceptedText);
        const auto payload = NativeTradeQuote::encode(record.value("terms").toObject(), accepted);
        require(!payload.isEmpty());
        require(NativeQuoteReplay::reserve(app.arguments().at(2), payload, accepted,
            QByteArray::fromHex(record.value("btcSignature").toString().toLatin1()),
            QByteArray::fromHex(record.value("xmzSignature").toString().toLatin1()),
            QByteArray::fromHex(app.arguments().at(3).toLatin1()),
            QByteArray::fromHex(app.arguments().at(4).toLatin1())));
        return 0;
    }
    if (app.arguments().size() == 6 && app.arguments().at(1) == "--retry") {
        QFile saved(app.arguments().at(2) + "/quote-" + app.arguments().at(3) + ".json");
        require(saved.open(QIODevice::ReadOnly));
        const auto record = QJsonDocument::fromJson(saved.read(16384)).object();
        const auto terms = record.value("terms").toObject();
        require(!terms.isEmpty());
        require(!NativeQuoteReplay::reserve(app.arguments().at(2), NativeTradeQuote::encode(terms, 100), 100,
            QByteArray::fromHex(record.value("btcSignature").toString().toLatin1()),
            QByteArray::fromHex(record.value("xmzSignature").toString().toLatin1()),
            QByteArray::fromHex(app.arguments().at(4).toLatin1()),
            QByteArray::fromHex(app.arguments().at(5).toLatin1())));
        return 0;
    }
    QJsonObject t{
        {"schema", QString::fromLatin1(NativeTradeQuote::signatureDomain())},
        {"quoteId", "11111111-1111-4111-8111-111111111111"},
        {"swapId", "22222222-2222-4222-8222-222222222222"},
        {"btcFunder", QString(64, 'a')}, {"xmzFunder", QString(64, 'b')},
        {"btcGenesis", QString(64, 'c')}, {"xmzGenesis", QString(64, 'd')},
        {"btcNetwork", "btc-regtest"}, {"xmzNetwork", "xmz-fakechain"},
        {"btcSatoshis", "500000"}, {"xmzAtoms", "200000000000"},
        {"appFeeXmzAtoms", "0"}, {"btcNativeFeeMaxSatoshis", "10000"},
        {"xmzNativeFeeMaxAtoms", "1000000000"},
        {"btcPayout", "fixture-btc-payout"}, {"btcRefund", "fixture-btc-refund"},
        {"xmzPayout", "fixture-xmz-payout"}, {"xmzRefund", "fixture-xmz-refund"},
        {"btcConfirmations", "3"}, {"xmzConfirmations", "10"},
        {"cancelBlocks", "72"}, {"punishBlocks", "72"}, {"expiresAt", "200"}
    };
    const auto bytes = NativeTradeQuote::encode(t, 100);
    require(!bytes.isEmpty());
    const auto display = NativeTradeQuote::displayAmounts(t, 100);
    require(display.value("btcAmount") == "0.005");
    require(display.value("xmzAmount") == "2");
    require(display.value("btcPerXmz") == "0.0025");
    require(display.value("btcFeeLimit") == "0.0001");
    require(display.value("xmzFeeLimit") == "0.01");
    require(NativeTradeQuote::displayAmounts(t, 200).isEmpty());
    auto priceTerms = t;
    priceTerms["btcSatoshis"] = "1"; priceTerms["xmzAtoms"] = "300000000000";
    require(NativeTradeQuote::displayAmounts(priceTerms, 100).value("btcPerXmz") == QString::fromUtf8("≈ 0.000000003333"));
    priceTerms["xmzAtoms"] = "18446744073709551615";
    require(NativeTradeQuote::displayAmounts(priceTerms, 100).value("btcPerXmz") == "< 0.000000000001");
    priceTerms["btcSatoshis"] = "2100000000000000"; priceTerms["xmzAtoms"] = "1";
    require(NativeTradeQuote::displayAmounts(priceTerms, 100).value("btcPerXmz") == "2100000000000000000");
    QJsonObject output;
    require(NativeTradeQuote::decode(bytes, 100, output) && output == t);
    const auto commitment = NativeTradeQuote::commitment(t, 100);
    require(commitment.size() == 32);
    for (auto it = t.begin(); it != t.end(); ++it) {
        auto changed = t;
        changed[it.key()] = it.value().toString() + "x";
        require(NativeTradeQuote::commitment(changed, 100) != commitment);
        changed = t; changed.remove(it.key());
        require(NativeTradeQuote::encode(changed, 100).isEmpty());
        changed = t; changed[it.key()] = it.value().toString() + "\n";
        require(NativeTradeQuote::encode(changed, 100).isEmpty());
        changed = t; changed[it.key()] = 1;
        require(NativeTradeQuote::encode(changed, 100).isEmpty());
    }
    auto changed = t;
    changed["btcFunder"] = t["xmzFunder"];
    changed["xmzFunder"] = t["btcFunder"];
    require(!NativeTradeQuote::encode(changed, 100).isEmpty());
    require(NativeTradeQuote::commitment(changed, 100) != commitment);
    for (const auto bad : {"0", "01", "-1", "18446744073709551616"}) {
        changed = t; changed["xmzAtoms"] = bad;
        require(NativeTradeQuote::encode(changed, 100).isEmpty());
    }
    changed = t; changed["xmzAtoms"] = "18446744073709551615";
    require(!NativeTradeQuote::encode(changed, 100).isEmpty());
    changed = t; changed["appFeeXmzAtoms"] = "1";
    require(NativeTradeQuote::encode(changed, 100).isEmpty());
    changed = t; changed["extra"] = "value";
    require(NativeTradeQuote::encode(changed, 100).isEmpty());
    require(NativeTradeQuote::encode(t, 200).isEmpty());
    require(NativeTradeQuote::encode(t, -1).isEmpty());
    changed = t; changed["expiresAt"] = "3701";
    require(NativeTradeQuote::encode(changed, 100).isEmpty());
    for (const auto &bad : {bytes + " ", QByteArray("{\"appFeeXmzAtoms\":\"0\",") + bytes.mid(1),
                           QByteArray(8193, 'x')}) {
        output = {{"unchanged", true}};
        require(!NativeTradeQuote::decode(bad, 100, output));
        require(output == QJsonObject({{"unchanged", true}}));
    }
    OfferIdentity btc, xmz, stranger;
    t["btcFunder"] = QString::fromLatin1(btc.publicKey().toHex());
    t["xmzFunder"] = QString::fromLatin1(xmz.publicKey().toHex());
    if ((app.arguments().size() == 2 || app.arguments().size() == 3)
        && app.arguments().at(1) == "--export-reservation-fixture") {
        // Public disposable fixture only: no private keys or real wallets exported.
        auto terms = t;
        terms["btcNetwork"] = "btc-mainnet";
        terms["btcPayout"] = "1BoatSLRHtKNngkdXEeobR76b53LETtpyT";
        terms["btcRefund"] = terms["btcPayout"];
        terms["xmzPayout"] = "FSf9UZS3uZwJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmJ7suhUXwdrDJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmCYGLw3H";
        terms["xmzRefund"] = terms["xmzPayout"];
        const auto payload = NativeTradeQuote::encode(terms, 100);
        QTemporaryDir directory;
        require(directory.isValid());
        const auto path = app.arguments().size() == 3 ? app.arguments().at(2)
            : QDir(directory.path()).canonicalPath();
        require(NativeQuoteReplay::reserve(path, payload, 100,
            btc.sign(payload, NativeTradeQuote::btcSignatureDomain()),
            xmz.sign(payload, NativeTradeQuote::xmzSignatureDomain()), btc.publicKey(), xmz.publicKey()));
        QFile record(path + "/quote-" + terms.value("quoteId").toString() + ".json");
        require(record.open(QIODevice::ReadOnly) && record.size() <= 16384);
        const auto envelope = QJsonDocument(QJsonObject{
            {"record", QString::fromUtf8(record.readAll())},
            {"btcPeer", QString::fromLatin1(btc.publicKey().toHex())},
            {"xmzPeer", QString::fromLatin1(xmz.publicKey().toHex())},
            {"commitment", QString::fromLatin1(NativeTradeQuote::commitment(terms, 100).toHex())}
        }).toJson(QJsonDocument::Compact);
        QFile stream;
        require(stream.open(stdout, QIODevice::WriteOnly));
        require(stream.write(envelope) == envelope.size() && stream.flush());
        return 0;
    }
    const auto signedBytes = NativeTradeQuote::encode(t, 100);
    const auto btcSig = btc.sign(signedBytes, NativeTradeQuote::btcSignatureDomain());
    const auto xmzSig = xmz.sign(signedBytes, NativeTradeQuote::xmzSignatureDomain());
    require(NativeTradeQuote::verifySignatures(signedBytes, 100, btcSig, xmzSig,
        btc.publicKey(), xmz.publicKey(), output) && output == t);
    NativeTradeQuote::DeploymentPolicy policy;
    policy.btcGenesis = t.value("btcGenesis").toString();
    policy.xmzGenesis = t.value("xmzGenesis").toString();
    policy.btcNetwork = "btc-regtest"; policy.xmzNetwork = "xmz-fakechain";
    policy.btcConfirmations = 3; policy.xmzConfirmations = 10;
    policy.cancelBlocks = 72; policy.punishBlocks = 72;
    policy.maximumBtcPrincipal = 500000; policy.maximumXmzPrincipal = 200000000000ULL;
    policy.maximumBtcNativeFee = 10000; policy.maximumXmzNativeFee = 1000000000;
    require(NativeTradeQuote::verifyForDeployment(signedBytes, 100, btcSig, xmzSig,
        btc.publicKey(), xmz.publicKey(), policy, output));
    for (int mutation = 0; mutation < 13; ++mutation) {
        auto wrong = policy;
        switch (mutation) {
        case 0: wrong.btcGenesis = QString(64, 'e'); break;
        case 1: wrong.xmzGenesis = QString(64, 'e'); break;
        case 2: wrong.btcNetwork = "btc-mainnet"; break;
        case 3: wrong.xmzNetwork = "xmz-mainnet"; break;
        case 4: ++wrong.btcConfirmations; break;
        case 5: ++wrong.xmzConfirmations; break;
        case 6: ++wrong.cancelBlocks; break;
        case 7: ++wrong.punishBlocks; break;
        case 8: --wrong.maximumBtcPrincipal; break;
        case 9: --wrong.maximumXmzPrincipal; break;
        case 10: --wrong.maximumBtcNativeFee; break;
        case 11: --wrong.maximumXmzNativeFee; break;
        case 12: wrong = {}; break;
        }
        QJsonObject preserved{{"sentinel", true}};
        require(!NativeTradeQuote::verifyForDeployment(signedBytes, 100, btcSig, xmzSig,
            btc.publicKey(), xmz.publicKey(), wrong, preserved));
        require(preserved == QJsonObject({{"sentinel", true}}));
    }
    const auto reject = [&](const QByteArray &payload, qint64 now,
                            const QByteArray &b, const QByteArray &x,
                            const QByteArray &expectedB, const QByteArray &expectedX) {
        QJsonObject unchanged{{"sentinel", true}};
        require(!NativeTradeQuote::verifySignatures(payload, now, b, x, expectedB, expectedX, unchanged));
        require(unchanged == QJsonObject({{"sentinel", true}}));
    };
    reject(signedBytes, 200, btcSig, xmzSig, btc.publicKey(), xmz.publicKey());
    reject(signedBytes, 100, xmzSig, btcSig, btc.publicKey(), xmz.publicKey());
    reject(signedBytes, 100, {}, xmzSig, btc.publicKey(), xmz.publicKey());
    reject(signedBytes, 100, btcSig, xmzSig, stranger.publicKey(), xmz.publicKey());
    reject(signedBytes, 100, btcSig, xmzSig, {}, xmz.publicKey());
    reject(signedBytes, 100, btc.sign(signedBytes, NativeTradeQuote::xmzSignatureDomain()),
        xmzSig, btc.publicKey(), xmz.publicKey());
    reject(signedBytes, 100, btcSig, xmz.sign(signedBytes, "legacy-domain"),
        btc.publicKey(), xmz.publicKey());
    changed = t; changed["xmzAtoms"] = "200000000001";
    reject(NativeTradeQuote::encode(changed, 100), 100, btcSig, xmzSig,
        btc.publicKey(), xmz.publicKey());
#ifdef Q_OS_LINUX
    QTemporaryDir directory;
    require(directory.isValid());
    const QString root = QDir(directory.path()).canonicalPath();
    require(!NativeQuoteReplay::reserve(root, signedBytes, 100, {}, xmzSig,
        btc.publicKey(), xmz.publicKey()));
    require(QDir(root).entryList(QDir::Files).isEmpty());
    require(NativeQuoteReplay::reserve(root, signedBytes, 100, btcSig, xmzSig,
        btc.publicKey(), xmz.publicKey()));
    require(!NativeQuoteReplay::reserve(root, signedBytes, 100, btcSig, xmzSig,
        btc.publicKey(), xmz.publicKey()));
    const auto reserveChanged = [&](const QJsonObject &candidate) {
        const auto wire = NativeTradeQuote::encode(candidate, 100);
        return NativeQuoteReplay::reserve(root, wire, 100,
            btc.sign(wire, NativeTradeQuote::btcSignatureDomain()),
            xmz.sign(wire, NativeTradeQuote::xmzSignatureDomain()), btc.publicKey(), xmz.publicKey());
    };
    changed = t; changed["quoteId"] = "33333333-3333-4333-8333-333333333333";
    require(!reserveChanged(changed)); // Same swap, newly signed quote.
    changed = t; changed["swapId"] = "44444444-4444-4444-8444-444444444444";
    require(!reserveChanged(changed)); // Same quote, newly signed swap.
    changed["quoteId"] = "33333333-3333-4333-8333-333333333333";
    require(reserveChanged(changed));
    require(!reserveChanged(changed));
    require(QDir(root).entryList(QDir::Files).size() == 4);
    QProcess restarted;
    restarted.start(app.applicationFilePath(), {"--retry", root, t.value("quoteId").toString(),
        QString::fromLatin1(btc.publicKey().toHex()), QString::fromLatin1(xmz.publicKey().toHex())});
    require(restarted.waitForFinished(10000) && restarted.exitStatus() == QProcess::NormalExit
        && restarted.exitCode() == 0);
    // Independent processes race the same signed quote, released together.
    QTemporaryDir raceDirectory;
    require(raceDirectory.isValid());
    const auto raceRoot = QDir(raceDirectory.path()).canonicalPath();
    int barrier[2];
    require(::pipe(barrier) == 0);
    QList<pid_t> children;
    for (int i = 0; i < 8; ++i) {
        const pid_t child = ::fork();
        require(child >= 0);
        if (child == 0) {
            ::close(barrier[1]);
            char start;
            if (::read(barrier[0], &start, 1) != 1) ::_exit(3);
            const bool reserved = NativeQuoteReplay::reserve(raceRoot, signedBytes, 100,
                btcSig, xmzSig, btc.publicKey(), xmz.publicKey());
            ::_exit(reserved ? 0 : 2);
        }
        children.append(child);
    }
    ::close(barrier[0]);
    require(::write(barrier[1], "12345678", 8) == 8);
    ::close(barrier[1]);
    int winners = 0;
    for (const pid_t child : children) {
        int status = 0;
        require(::waitpid(child, &status, 0) == child && WIFEXITED(status));
        require(WEXITSTATUS(status) == 0 || WEXITSTATUS(status) == 2);
        if (WEXITSTATUS(status) == 0) ++winners;
    }
    require(winners == 1);
    require(QDir(raceRoot).entryList(QDir::Files).size() == 2);
    QJsonObject history;
    require(NativeQuoteReplay::recover(raceRoot, t.value("quoteId").toString(), 10000,
        btc.publicKey(), xmz.publicKey(), history)); // Expired, but historically signed.
    require(history.value("terms").toObject() == t);
    const auto unchangedHistory = history;
    require(!NativeQuoteReplay::recover(raceRoot, t.value("quoteId").toString(), 99,
        btc.publicKey(), xmz.publicKey(), history));
    require(history == unchangedHistory);
    require(!NativeQuoteReplay::recover(raceRoot, t.value("quoteId").toString(), 10000,
        stranger.publicKey(), xmz.publicKey(), history));
    require(history == unchangedHistory);
    require(!NativeQuoteReplay::reserve(raceRoot, signedBytes, 100, btcSig, xmzSig,
        btc.publicKey(), xmz.publicKey())); // Historical read never rearms acceptance.
    const auto quoteRecord = raceRoot + "/quote-" + t.value("quoteId").toString() + ".json";
    const auto originalRecord = QJsonDocument(history).toJson(QJsonDocument::Compact);
    auto tamperedHistory = history;
    auto tamperedTerms = t;
    tamperedTerms["xmzAtoms"] = "1";
    tamperedHistory["terms"] = tamperedTerms;
    const QList<QByteArray> damagedRecords{
        QJsonDocument(tamperedHistory).toJson(QJsonDocument::Compact),
        QByteArray("{\"acceptedAt\":\"100\",") + originalRecord.mid(1),
        QByteArray(16385, 'x'), QByteArray("{")};
    for (const auto &damaged : damagedRecords) {
        QFile artifact(quoteRecord);
        require(artifact.open(QIODevice::WriteOnly | QIODevice::Truncate));
        require(artifact.write(damaged) == damaged.size());
        artifact.close();
        require(!NativeQuoteReplay::recover(raceRoot, t.value("quoteId").toString(), 10000,
            btc.publicKey(), xmz.publicKey(), history));
        require(history == unchangedHistory);
        require(!NativeQuoteReplay::reserve(raceRoot, signedBytes, 100, btcSig, xmzSig,
            btc.publicKey(), xmz.publicKey()));
    }
    QFile restored(quoteRecord);
    require(restored.open(QIODevice::WriteOnly | QIODevice::Truncate));
    require(restored.write(originalRecord) == originalRecord.size());
    restored.close(); // Test-only restoration of disposable public signed terms.
    require(NativeQuoteReplay::recover(raceRoot, t.value("quoteId").toString(), 10000,
        btc.publicKey(), xmz.publicKey(), history));
    const auto swapRecord = raceRoot + "/swap-" + t.value("swapId").toString() + ".json";
    require(QFile::remove(swapRecord)); // Disposable fixture: simulate incomplete pair.
    require(!NativeQuoteReplay::recover(raceRoot, t.value("quoteId").toString(), 10000,
        btc.publicKey(), xmz.publicKey(), history));
    require(history == unchangedHistory);
    // A partial record cannot be mistaken for a fresh ID or repaired silently.
    QTemporaryDir partialDirectory;
    require(partialDirectory.isValid());
    const auto partialRoot = QDir(partialDirectory.path()).canonicalPath();
    const auto partialPath = partialRoot + "/quote-" + t.value("quoteId").toString() + ".json";
    QFile partial(partialPath);
    require(partial.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    require(partial.write("{", 1) == 1);
    partial.close();
    require(!NativeQuoteReplay::reserve(partialRoot, signedBytes, 100, btcSig, xmzSig,
        btc.publicKey(), xmz.publicKey()));
    require(partial.open(QIODevice::ReadOnly) && partial.readAll() == "{");
    require(QDir(partialRoot).entryList(QDir::Files).size() == 1);
    require(!NativeQuoteReplay::recover(partialRoot, t.value("quoteId").toString(), 10000,
        btc.publicKey(), xmz.publicKey(), history));
#ifdef NATIVE_QUOTE_FSYNC_FAULT_TEST
    QTemporaryDir syncFailureDirectory;
    require(syncFailureDirectory.isValid());
    const auto syncRoot = QDir(syncFailureDirectory.path()).canonicalPath();
    failNextDirectorySync = true;
    require(!NativeQuoteReplay::reserve(syncRoot, signedBytes, 100, btcSig, xmzSig,
        btc.publicKey(), xmz.publicKey()));
    require(!failNextDirectorySync); // Failure reached the final directory sync.
    require(QDir(syncRoot).entryList(QDir::Files).size() == 2);
    require(NativeQuoteReplay::recover(syncRoot, t.value("quoteId").toString(), 10000,
        btc.publicKey(), xmz.publicKey(), history));
    require(history.value("terms").toObject() == t); // History is not success/authority.
    require(!NativeQuoteReplay::reserve(syncRoot, signedBytes, 100, btcSig, xmzSig,
        btc.publicKey(), xmz.publicKey()));
#endif
#endif
    if (app.arguments().size() == 2) {
        QTemporaryDir helperDirectory;
        require(helperDirectory.isValid());
        const auto helper = QDir(helperDirectory.path()).canonicalPath() + "/native-helper";
        require(QFile::copy(QFileInfo(app.arguments().at(1)).canonicalFilePath(), helper));
        require(QFile::setPermissions(helper, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        QFile executable(helper);
        require(executable.open(QIODevice::ReadOnly));
        QCryptographicHash hasher(QCryptographicHash::Sha256);
        require(hasher.addData(&executable));
        const auto digest = QString::fromLatin1(hasher.result().toHex());
        auto destinations = t;
        destinations["btcPayout"] = "1BoatSLRHtKNngkdXEeobR76b53LETtpyT";
        destinations["btcRefund"] = destinations["btcPayout"];
        destinations["xmzPayout"] = "FSf9UZS3uZwJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmJ7suhUXwdrDJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmCYGLw3H";
        destinations["xmzRefund"] = destinations["xmzPayout"];
        for (int mode = 0; mode < 3; ++mode) {
            NativeDestinationJob job;
            QEventLoop loop;
            bool completed = false, valid = false;
            QObject::connect(&job, &NativeDestinationJob::completed, &loop, [&](bool value) {
                completed = true; valid = value; loop.quit();
            });
            require(job.start(helper, digest, destinations, 100, mode == 1 ? "regtest" : "mainnet", "mainnet"));
            require(!job.start(helper, digest, destinations, 100, "mainnet", "mainnet"));
            if (mode == 2) job.cancel();
            if (!completed) { QTimer::singleShot(12000, &loop, &QEventLoop::quit); loop.exec(); }
            require(completed && valid == (mode == 0));
            require(job.result().isEmpty() == (mode != 0));
            job.cancel(); require(job.result().isEmpty());
        }
        qInfo("PASS: async Qt destination job validates the real native helper and rejects wrong-network/cancelled operations");
        const auto now = QDateTime::currentSecsSinceEpoch();
        destinations["expiresAt"] = QString::number(now + 60);
        destinations["btcNetwork"] = "btc-mainnet";
        auto selectedPolicy = policy;
        selectedPolicy.btcNetwork = "btc-mainnet";
        const auto quote = NativeTradeQuote::encode(destinations, now);
        const auto signedBtc = btc.sign(quote, NativeTradeQuote::btcSignatureDomain());
        const auto signedXmz = xmz.sign(quote, NativeTradeQuote::xmzSignatureDomain());
        QTemporaryDir reservations;
        require(reservations.isValid());
        const auto replay = QDir(reservations.path()).canonicalPath();
        QJsonObject policyJson{
            {"btcGenesis", selectedPolicy.btcGenesis}, {"xmzGenesis", selectedPolicy.xmzGenesis},
            {"btcNetwork", selectedPolicy.btcNetwork}, {"xmzNetwork", selectedPolicy.xmzNetwork},
            {"btcConfirmations", "3"}, {"xmzConfirmations", "10"}, {"cancelBlocks", "72"}, {"punishBlocks", "72"},
            {"maximumBtcPrincipal", "500000"}, {"maximumXmzPrincipal", "200000000000"},
            {"maximumBtcNativeFee", "10000"}, {"maximumXmzNativeFee", "1000000000"}};
        QJsonObject configJson{{"schema", "monzero-private-native-quote-v1"}, {"helper", helper},
            {"helperDigest", digest}, {"directory", replay},
            {"btcPeer", QString::fromLatin1(btc.publicKey().toHex())},
            {"xmzPeer", QString::fromLatin1(xmz.publicKey().toHex())}, {"policy", policyJson},
            {"terms", destinations}, {"btcSignature", QString::fromLatin1(signedBtc.toHex())},
            {"xmzSignature", QString::fromLatin1(signedXmz.toHex())}};
        const auto configPath = helperDirectory.path() + "/quote-config.json";
        const auto writeConfig = [&](const QJsonObject &value) {
            QFile configFile(configPath);
            require(configFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
            require(configFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
            const auto encoded = QJsonDocument(value).toJson(QJsonDocument::Compact);
            require(configFile.write(encoded) == encoded.size());
        };
        writeConfig(configJson);
        NativeQuoteConfig loaded;
        require(NativeQuoteConfig::load(configPath, now, loaded));
        require(loaded.payload == quote && loaded.btcPeer == btc.publicKey());
        auto badConfig = configJson; badConfig["unexpected"] = true;
        writeConfig(badConfig);
        require(!NativeQuoteConfig::load(configPath, now, loaded) && loaded.payload.isEmpty());
        badConfig = configJson; badConfig["helperDigest"] = QString(64, '0');
        writeConfig(badConfig);
        require(!NativeQuoteConfig::load(configPath, now, loaded));
        writeConfig(configJson);
        require(QFile::setPermissions(configPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadOther));
        require(!NativeQuoteConfig::load(configPath, now, loaded));
        writeConfig(configJson);
        require(NativeQuoteConfig::load(configPath, now, loaded));
        for (int attempt = 0; attempt < 2; ++attempt) {
            NativeQuoteCoordinator coordinator(helper, digest, replay, selectedPolicy,
                btc.publicKey(), xmz.publicKey());
            QEventLoop loop;
            QObject::connect(&coordinator, &NativeQuoteCoordinator::changed, &loop, [&] {
                if (coordinator.state() != NativeQuoteCoordinator::Checking) loop.quit();
            });
            require(coordinator.begin(quote, signedBtc, signedXmz));
            QTimer::singleShot(12000, &loop, &QEventLoop::quit);
            if (coordinator.state() == NativeQuoteCoordinator::Checking) loop.exec();
            require(coordinator.state() == NativeQuoteCoordinator::Ready);
            require(coordinator.status().value("state") == "review");
            require(coordinator.status().value("executable") == false);
            require(!coordinator.review().value("executable").toBool());
            require(!coordinator.approve(QByteArray(32, 'x')));
            if (attempt == 0) require(QDir(replay).entryList(QDir::Files).isEmpty());
            const auto reviewed = QByteArray::fromHex(coordinator.review().value("commitment").toString().toLatin1());
            require(coordinator.approve(reviewed) == (attempt == 0));
            require(coordinator.state() == (attempt == 0 ? NativeQuoteCoordinator::Reserved : NativeQuoteCoordinator::Failed));
            require(coordinator.status().value("state") == (attempt == 0 ? "reserved" : "failed"));
            require(coordinator.status().value("executable") == false);
            require(coordinator.review().isEmpty());
            require(!coordinator.approve(reviewed));
        }
        qInfo("PASS: signed quote coordinator checks native destinations and reserves only exact approved terms once");
        auto expiring = destinations;
        const auto expiryNow = QDateTime::currentSecsSinceEpoch();
        expiring["expiresAt"] = QString::number(expiryNow + 3);
        expiring["quoteId"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        expiring["swapId"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto expiryPayload = NativeTradeQuote::encode(expiring, expiryNow);
        const auto expiryBtc = btc.sign(expiryPayload, NativeTradeQuote::btcSignatureDomain());
        const auto expiryXmz = xmz.sign(expiryPayload, NativeTradeQuote::xmzSignatureDomain());
        const auto expiryCommitment = NativeTradeQuote::commitment(expiring, expiryNow);
        NativeQuoteCoordinator expired(helper, digest, replay, selectedPolicy, btc.publicKey(), xmz.publicKey());
        QEventLoop expiryLoop;
        QObject::connect(&expired, &NativeQuoteCoordinator::changed, &expiryLoop, [&] {
            if (expired.state() != NativeQuoteCoordinator::Checking) expiryLoop.quit();
        });
        require(expired.begin(expiryPayload, expiryBtc, expiryXmz));
        QTimer::singleShot(12000, &expiryLoop, &QEventLoop::quit);
        if (expired.state() == NativeQuoteCoordinator::Checking) expiryLoop.exec();
        require(expired.state() == NativeQuoteCoordinator::Ready);
        QEventLoop expiryWait;
        QTimer::singleShot(3500, &expiryWait, &QEventLoop::quit);
        expiryWait.exec();
        require(expired.state() == NativeQuoteCoordinator::Failed && expired.review().isEmpty());
        require(!expired.approve(expiryCommitment));
        require(QDir(replay).entryList(QDir::Files).size() == 2);
        NativeQuoteCoordinator cancelled(helper, digest, replay, selectedPolicy, btc.publicKey(), xmz.publicKey());
        require(cancelled.begin(quote, signedBtc, signedXmz));
        cancelled.cancel();
        require(cancelled.state() == NativeQuoteCoordinator::Cancelled && cancelled.review().isEmpty());
        require(!cancelled.approve(NativeTradeQuote::commitment(destinations, QDateTime::currentSecsSinceEpoch())));
        require(QDir(replay).entryList(QDir::Files).size() == 2);
        qInfo("PASS: expired and cancelled coordinator reviews cannot reserve a quote");
    }
    qInfo("PASS: native quote signatures and persistent quote/swap replay reservations");
}
