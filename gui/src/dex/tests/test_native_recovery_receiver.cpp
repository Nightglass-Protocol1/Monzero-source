#include "../NativeRecoveryReceiver.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QProcess>
#include <QDebug>
#include <cstdlib>

static void require(bool value) { if (!value) { qFatal("Recovery receiver assertion failed"); } }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    NativeRecoveryReceiver receiver;
    // Disposable integration driver only: never linked as a production launcher.
    if (argc == 9 && QString::fromLocal8Bit(argv[1]) == "--native-helper") {
        const auto args = app.arguments();
        NativeRecoveryReceiver::Expected bound{args[5],args[6],args[7],args[8],"10"};
        const auto requestId = receiver.begin(bound);
        require(!requestId.isEmpty());
        const QJsonObject request{{"schema","monzero-native-recovery-request-v1"},
            {"requestId",requestId},{"genesis",bound.genesis},{"wallet",bound.wallet},
            {"lockTxid",bound.lockTxid},{"txid",bound.txid},{"minimumConfirmations","10"}};
        QProcess helper;
        helper.start(args[2], {"--recovery-stdio-v1","--session-file",args[3],"--journal-directory",args[4]});
        require(helper.waitForStarted(5000));
        const auto payload = QJsonDocument(request).toJson(QJsonDocument::Compact);
        require(helper.write(payload) == payload.size());
        helper.closeWriteChannel();
        require(helper.waitForFinished(35000));
        require(helper.exitStatus() == QProcess::NormalExit && helper.exitCode() == 0);
        require(helper.readAllStandardError().isEmpty());
        const auto accepted = receiver.accept(helper.readAllStandardOutput());
        require(accepted.value("valid").toBool() && !accepted.value("retryAllowed").toBool());
        qInfo("PASS: native helper confirmation accepted by the Qt recovery receiver");
        return EXIT_SUCCESS;
    }
    require(argc == 1);
    NativeRecoveryReceiver::Expected expected{QString(64,'a'),QString(95,'A'),
        QString(64,'b'),QString(64,'c'),"10"};
    const auto reply = [&](const QString &id) {
        return QJsonObject{{"schema","monzero-native-recovery-v1"},{"requestId",id},
            {"source","local-wallet-reconciliation"},{"status","confirmed"},
            {"genesis",expected.genesis},{"wallet",expected.wallet},{"lockTxid",expected.lockTxid},
            {"txid",expected.txid},{"confirmations","10"},{"retryAllowed",false},{"executable",false}};
    };
    const auto bytes = [](const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); };
    auto id = receiver.begin(expected);
    require(!id.isEmpty());
    auto result = receiver.accept(bytes(reply(id)));
    require(result.value("valid").toBool());
    require(!result.value("retryAllowed").toBool() && !result.value("executable").toBool());
    require(!result.value("transportAuthenticated").toBool());
    require(!receiver.accept(bytes(reply(id))).value("valid").toBool());
    require(!receiver.current().value("valid").toBool());
    for (const auto field : {"genesis","wallet","lockTxid","txid","requestId","source","schema","status","confirmations"}) {
        id = receiver.begin(expected);
        auto invalid = reply(id); invalid[field] = "invalid";
        require(!receiver.accept(bytes(invalid)).value("valid").toBool());
    }
    for (const auto count : {"9","0","01","18446744073709551616","10\n"}) {
        id = receiver.begin(expected); auto invalid = reply(id); invalid["confirmations"] = count;
        require(!receiver.accept(bytes(invalid)).value("valid").toBool());
    }
    for (const auto field : {"retryAllowed","executable"}) {
        id = receiver.begin(expected); auto invalid = reply(id); invalid[field] = true;
        require(!receiver.accept(bytes(invalid)).value("valid").toBool());
    }
    for (int mode = 0; mode < 5; ++mode) {
        id = receiver.begin(expected); auto invalid = bytes(reply(id));
        if (mode == 0) invalid.prepend(' ');
        if (mode == 1) invalid.insert(1,"\"status\":\"confirmed\",");
        if (mode == 2) invalid = QByteArray(2049,' ');
        if (mode == 3) invalid = "{";
        if (mode == 4) invalid.replace("\"confirmations\":\"10\"","\"confirmations\":10");
        require(!receiver.accept(invalid).value("valid").toBool());
    }
    id = receiver.begin(expected); receiver.cancel();
    require(!receiver.accept(bytes(reply(id))).value("valid").toBool());
    id = receiver.begin(expected); require(!receiver.begin(expected).isEmpty());
    require(!receiver.accept(bytes(reply(id))).value("valid").toBool());
    id = receiver.begin(expected); auto extra = reply(id); extra["privateKey"] = "forbidden";
    require(!receiver.accept(bytes(extra)).value("valid").toBool());
    expected.minimumConfirmations = "9007199254740993";
    id = receiver.begin(expected); auto exact = reply(id); exact["confirmations"] = "9007199254740993";
    require(receiver.accept(bytes(exact)).value("confirmations").toString() == "9007199254740993");
    expected.minimumConfirmations = "10";
    id = receiver.begin(expected,1); QThread::msleep(5);
    require(!receiver.accept(bytes(reply(id))).value("valid").toBool());
    id = receiver.begin(expected,10); require(receiver.accept(bytes(reply(id))).value("valid").toBool());
    QThread::msleep(20); require(!receiver.current().value("valid").toBool());
    expected.minimumConfirmations = "0"; require(receiver.begin(expected).isEmpty());
    qInfo("PASS: recovery identity, authority, malformed response, replay and expiry boundaries");
    return EXIT_SUCCESS;
}
