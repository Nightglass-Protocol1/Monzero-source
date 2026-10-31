#ifndef MONZERO_DEX_TRADE_FOLDERS_H
#define MONZERO_DEX_TRADE_FOLDERS_H
#include <QString>
#include <QVariantList>

// One private folder per trade under <app data>/dex/trades/<tradeId>/ holding
// trade.json:
//   {"schema":"monzero-dex-trade-v1","tradeId":"<uuid>","pair":"LTC/XMZ",
//    "network":"ltc-testnet","appConfig":"<file in this folder or absolute>",
//    "state":"active"|"closed"}
// The app reopens active trades at startup instead of requiring per-trade
// command-line configuration. Folders are never deleted by the app.
namespace DexTradeFolders {
struct Trade {QString tradeId,pair,network,appConfig,state,folder,setupDigest;};
QString defaultRoot();
// Valid manifests only, sorted by trade id. Invalid folders are reported with
// state "invalid" so the user is told rather than silently skipping them.
QList<Trade> scan(const QString &root);
QVariantList describe(const QList<Trade> &trades,const QString &loadedTradeId);
// Creates <root>/<tradeId>/trade.json for an existing private app config.
bool create(const QString &root,const QString &tradeId,const QString &pair,const QString &network,const QString &appConfig,const QString &setupDigest=QString());
// active -> closed only; atomic rewrite.
bool close(const QString &root,const QString &tradeId);
}
#endif
