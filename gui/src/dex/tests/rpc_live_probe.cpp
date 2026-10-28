#include "../BitcoinTransactionEvidence.h"
#include "../BitcoinPsbtRpcVerifier.h"
#include "../ConfirmationTracker.h"
#include "../MonzeroTransactionEvidence.h"
#include "../NodeRpcValidator.h"
#include "../ReadOnlyRpcClient.h"
#include "../XmzFundingCoordinator.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <QTemporaryDir>

namespace {
const QString btcGenesis = "0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";
const QString xmzGenesis = "84f9ebdac8924806f037482ec16fd59b271e954d3e00363dd6c7e4ce9dd659e4";
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QFile cookie("/srv/monzero-dex/data/bitcoin-regtest/regtest/.cookie");
    if (!cookie.open(QIODevice::ReadOnly)) return 2;
    const QByteArray authorization = "Basic " + cookie.readAll().trimmed().toBase64();
    ReadOnlyRpcClient btc(QUrl("http://127.0.0.1:18443/"),
                          {"decodepsbt", "estimatesmartfee", "getbestblockhash",
                           "getblockchaininfo", "getblockheader", "getrawtransaction",
                           "gettxout"});
    const auto btcInfo = btc.call("getblockchaininfo", QJsonArray{}, authorization);
    const auto btcBlock = btc.call("getblockheader", QJsonArray{btcGenesis, true}, authorization);
    if (!btcInfo.ok || !btcBlock.ok || btcBlock.value.value("hash").toString() != btcGenesis) return 3;

    ReadOnlyRpcClient xmz(QUrl("http://127.0.0.1:6175/json_rpc"), {"get_info", "get_block"});
    const auto xmzInfo = xmz.call("get_info");
    const auto xmzBlock = xmz.call("get_block", QJsonObject{{"height", 0}});
    if (!xmzInfo.ok || !xmzBlock.ok
        || xmzBlock.value.value("block_header").toObject().value("hash").toString() != xmzGenesis) return 4;
    if (!NodeRpcValidator::bitcoin(btcInfo.value, btcGenesis, btcGenesis).value("ready").toBool()
        || !NodeRpcValidator::monzero(xmzInfo.value, xmzGenesis, xmzGenesis).value("ready").toBool()) return 5;

    QFile fixtureFile("/srv/monzero-dex/recovery/bitcoin-settlement-fixture.json");
    if (!fixtureFile.open(QIODevice::ReadOnly)) return 8;
    QJsonParseError fixtureParse;
    const QJsonDocument fixtureDocument = QJsonDocument::fromJson(fixtureFile.readAll(), &fixtureParse);
    if (fixtureParse.error != QJsonParseError::NoError || !fixtureDocument.isObject()) return 8;
    const QJsonObject fixture = fixtureDocument.object();
    if (fixture.value("version").toInt() != 1 || fixture.value("status") != QJsonValue("confirmed")
        || fixture.value("network") != QJsonValue("regtest")) return 8;
    const QString fixtureTxid = fixture.value("transactionId").toString();
    const QString fixtureBlock = fixture.value("blockHash").toString();
    const auto btcTransaction = btc.call("getrawtransaction", QJsonArray{fixtureTxid, 1}, authorization);
    const auto btcHeader = btc.call("getblockheader", QJsonArray{fixtureBlock, true}, authorization);
    if (!btcTransaction.ok || !btcHeader.ok) return 9;
    const QString btcHeight = QString::number(static_cast<qulonglong>(btcInfo.value.value("blocks").toDouble()));
    const int requiredConfirmations = fixture.value("requiredConfirmations").toInt();
    const QVariantMap btcEvidence = BitcoinTransactionEvidence::validate(
        btcTransaction.value, btcHeader.value, fixtureTxid, fixture.value("destination").toString(),
        fixture.value("amountAtomic").toString(), btcHeight, requiredConfirmations);
    if (!btcEvidence.value("valid").toBool() || !btcEvidence.value("confirmed").toBool()
        || btcEvidence.value("executable").toBool()) return 10;
    QTemporaryDir confirmationDirectory;
    if (!confirmationDirectory.isValid()) return 11;
    ConfirmationTracker confirmations(confirmationDirectory.filePath("confirmations.json"));
    const QVariantMap tracked = confirmations.observe("BTC", fixtureTxid, true,
        btcEvidence.value("blockHash").toString(), btcEvidence.value("blockHeight").toString(),
        btcEvidence.value("confirmations").toString(), btcEvidence.value("nodeHeight").toString(),
        requiredConfirmations);
    if (!tracked.value("confirmed").toBool() || tracked.value("reorgDetected").toBool()
        || tracked.value("executable").toBool()) return 12;

    QFile lockFixtureFile("/srv/monzero-dex/recovery/bitcoin-lock-psbt-fixture.json");
    if (!lockFixtureFile.open(QIODevice::ReadOnly)) return 19;
    QJsonParseError lockFixtureParse;
    const QJsonDocument lockFixtureDocument = QJsonDocument::fromJson(
        lockFixtureFile.readAll(), &lockFixtureParse);
    if (lockFixtureParse.error != QJsonParseError::NoError
        || !lockFixtureDocument.isObject()) return 19;
    const QJsonObject lockFixture = lockFixtureDocument.object();
    const QByteArray templateCommitment = QByteArray::fromHex(
        lockFixture.value("templateCommitment").toString().toLatin1());
    if (lockFixture.size() != 13 || lockFixture.value("version").toInt() != 1
        || lockFixture.value("status") != QJsonValue("unsigned")
        || lockFixture.value("network") != QJsonValue("regtest")
        || templateCommitment.size() != 32) return 19;
    const QVariantMap lockEvidence = BitcoinPsbtRpcVerifier::validateLock(
        btc, lockFixture.value("psbt").toString().toLatin1(), authorization,
        templateCommitment, lockFixture.value("address").toString(),
        lockFixture.value("scriptPubKeyHex").toString(),
        lockFixture.value("firstContractKeyHex").toString(),
        lockFixture.value("secondContractKeyHex").toString(),
        lockFixture.value("amountAtomic").toString(),
        lockFixture.value("maximumFeeAtomic").toString(),
        lockFixture.value("minimumInputConfirmations").toInt());
    if (!lockEvidence.value("valid").toBool()
        || !lockEvidence.value("rpcVerified").toBool()
        || !lockEvidence.value("scriptVerified").toBool()
        || lockEvidence.value("executable").toBool()) return 20;
    ReadOnlyRpcClient wallet(QUrl("http://127.0.0.1:6176/json_rpc"),
                             {"get_balance", "get_height", "get_transfer_by_txid"});
    QFile xmzFixtureFile("/srv/monzero-dex/recovery/monzero-settlement-fixture.json");
    if (!xmzFixtureFile.open(QIODevice::ReadOnly)) return 13;
    QJsonParseError xmzFixtureParse;
    const QJsonDocument xmzFixtureDocument = QJsonDocument::fromJson(
        xmzFixtureFile.readAll(), &xmzFixtureParse);
    if (xmzFixtureParse.error != QJsonParseError::NoError || !xmzFixtureDocument.isObject()) return 13;
    const QJsonObject xmzFixture = xmzFixtureDocument.object();
    if (xmzFixture.value("version").toInt() != 1
        || xmzFixture.value("status") != QJsonValue("confirmed")
        || xmzFixture.value("network") != QJsonValue("fakechain")
        || xmzFixture.contains("transactionMetadata")) return 13;
    const QString xmzFixtureTxid = xmzFixture.value("transactionId").toString();
    bool xmzHeightOk = false;
    const qulonglong xmzFixtureHeight = xmzFixture.value("blockHeight").toString().toULongLong(&xmzHeightOk);
    if (!xmzHeightOk || xmzFixtureHeight > 9007199254740991ULL) return 13;
    const auto xmzTransfer = wallet.call("get_transfer_by_txid",
        QJsonObject{{"txid", xmzFixtureTxid}});
    const auto xmzFixtureBlock = xmz.call("get_block",
        QJsonObject{{"height", static_cast<double>(xmzFixtureHeight)}});
    if (!xmzTransfer.ok || !xmzFixtureBlock.ok
        || xmzFixtureBlock.value.value("block_header").toObject().value("hash")
            != xmzFixture.value("blockHash")) return 14;
    const int xmzRequired = xmzFixture.value("requiredConfirmations").toInt();
    const QVariantMap xmzEvidence = MonzeroTransactionEvidence::validate(
        xmzTransfer.value, xmzFixtureBlock.value, xmzInfo.value, xmzFixtureTxid,
        xmzFixture.value("destination").toString(), xmzFixture.value("amountAtomic").toString(),
        xmzRequired);
    if (!xmzEvidence.value("valid").toBool() || !xmzEvidence.value("confirmed").toBool()
        || !xmzEvidence.value("spendable").toBool() || xmzEvidence.value("executable").toBool()) return 15;
    const QVariantMap xmzTracked = confirmations.observe("XMZ", xmzFixtureTxid, true,
        xmzEvidence.value("blockHash").toString(), xmzEvidence.value("blockHeight").toString(),
        xmzEvidence.value("confirmations").toString(), xmzEvidence.value("nodeHeight").toString(),
        xmzRequired);
    if (!xmzTracked.value("confirmed").toBool() || xmzTracked.value("reorgDetected").toBool()
        || xmzTracked.value("executable").toBool()) return 16;
    const auto balance = wallet.call("get_balance"), height = wallet.call("get_height");
    QTemporaryDir directory;
    if (!balance.ok || !height.ok || !directory.isValid()) return 17;
    XmzFundingCoordinator funding(directory.filePath("reservations.json"));
    const auto reservation = funding.reserve("live-probe", "1", balance.value, height.value,
                                             xmzInfo.value, xmzGenesis, xmzGenesis);
    if (!reservation.value("balanceVerified").toBool()
        || !reservation.value("reserved").toBool()
        || reservation.value("executable").toBool()) return 18;
    QTextStream(stdout) << "Read-only C++ node probes passed\n";
    return 0;
}
