#ifndef MONZERO_BTC_TRADE_SETUP_H
#define MONZERO_BTC_TRADE_SETUP_H
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// The steps of a BTC/XMZ or LTC/XMZ trade setup through the open relays, as the trade kit does them
// (tools/dex-trade-kit/relay_market.py), so an app and a kit can trade with each other:
// signed offers, sealed letters, the terms both sides sign, the signed quote and this side's
// app folder (the layout of `trade_kit.py configure` with a relay peer). No network, no state.
namespace BtcTradeSetup {
inline const char *ListingSchema="monzero-dex-utxo-listing/1";
inline const char *CancelSchema="monzero-dex-utxo-cancel/1";
// The pair a swap network trades ("BTC/XMZ" for btc-*, "LTC/XMZ" for ltc-*); empty if unknown
QString pairOf(const QString &network);
inline const char *TermsSchema="monzero-native-ltc-xmz-quote-v2";
inline const char *EnvelopeSchema="monzero-ltc-signed-quote-envelope-v1";
inline const char *CardSchema="monzero-ltc-trade-card-v1";
constexpr qint64 QuoteSeconds=3500;   // the XMZ seller must lock before the quote expires
// Every term of a quote, in the kit's order
QStringList fields();
// Each role's own destinations ("ltc" = pays BTC, buys XMZ; "xmz" = sells XMZ)
QStringList ownAddresses(const QString &role);
QString otherRole(const QString &role);
QString roleName(const QString &role);   // "litecoin-funder" / "native-funder"

// Compact JSON with sorted keys: what both sides sign
QByteArray canonical(const QJsonObject &object);

// Sealed letters (engine/src/mail.rs format): fresh X25519 sender key, ChaCha20-Poly1305
QByteArray mailboxPublic(const QByteArray &secret);
QByteArray seal(const QByteArray &toPublic,const QByteArray &plain);
QByteArray open(const QByteArray &secret,const QByteArray &sealed);

// The offer's fields if its maker signed it and it is well formed (and open at `now`, if given)
QJsonObject checkListing(const QJsonObject &signedListing,qint64 now=0);

// The maker's terms: the offer's amounts, both keys, both sides' destinations, the maker's other
// parameters, a fresh quote id and swap id, and an expiry QuoteSeconds from `now`
QByteArray buildTerms(const QString &makerRole,const QJsonObject &listing,const QJsonObject &params,
    const QJsonObject &makerCard,const QJsonObject &takerCard,const QJsonObject &takerAddresses,qint64 now,QString *error);
// Canonical terms with every field a string; empty object if not
QJsonObject parseTerms(const QByteArray &payload);
// What a taker must see before it signs: exactly the offer, both keys, its own destinations,
// and a policy that agrees with the terms. Empty string when acceptable.
QString termsRefusal(const QByteArray &payload,const QJsonObject &listing,const QString &ownRole,
    const QJsonObject &ownCard,const QJsonObject &ownAddresses,const QJsonObject &policy);
bool verifyTerms(const QByteArray &payload,const QString &role,const QByteArray &publicKey,const QByteArray &signature);
QString signingDomain(const QString &role);
QByteArray envelope(const QByteArray &payload,const QString &ltcSignatureHex,const QString &xmzSignatureHex);

// Writes this side's app folder `appDir` (must not exist) for a trade through `relays`.
// identityDir holds identity.bin, identity.passphrase and mailbox.key. `local` is this machine's
// services: {"litecoin": {node or {"light": ...}}, "xmz": {...}, optional feeRateAtomsPerKb, safetyBlocks}.
// Returns the app.json path, or empty with `error` set.
QString writeAppFolder(const QString &appDir,const QString &role,const QJsonObject &terms,const QString &quotePath,
    const QString &identityDir,const QJsonObject &peerCard,const QJsonObject &policy,const QJsonObject &local,
    const QString &journalHelper,const QStringList &relays,QString *error);
}
#endif
