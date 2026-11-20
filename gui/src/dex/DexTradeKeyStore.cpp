#include "DexTradeKeyStore.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <sodium.h>

namespace {
const char *SCHEMA = "monzero-dex-trade-keys/1";
constexpr int SECRET_BYTES = 64;   // swapSeed(32) ‖ evmKey(32)

QByteArray hex(const unsigned char *data, int len) { return QByteArray(reinterpret_cast<const char *>(data), len).toHex(); }

// Argon2id of the password to a 32-byte secretbox key, with the salt and the stored (or default) limits
bool deriveKey(const QByteArray &password, const QByteArray &salt, quint64 ops, quint64 mem, unsigned char out[crypto_secretbox_KEYBYTES]) {
    if (salt.size() != crypto_pwhash_SALTBYTES || password.isEmpty() || password.size() > 1024) return false;
    return crypto_pwhash(out, crypto_secretbox_KEYBYTES, password.constData(), password.size(),
                         reinterpret_cast<const unsigned char *>(salt.constData()), ops, mem, crypto_pwhash_ALG_ARGON2ID13) == 0;
}

DexTradeKeyStore::Keys keysFromSecret(const unsigned char *secret) {
    DexTradeKeyStore::Keys k;
    k.swapSeedHex = hex(secret, 32);
    k.evmKeyHex = hex(secret + 32, 32);
    k.ok = true;
    return k;
}
}

bool DexTradeKeyStore::exists() const { return QFileInfo::exists(m_path); }

DexTradeKeyStore::Keys DexTradeKeyStore::create(const QByteArray &password) {
    if (sodium_init() < 0 || exists()) return {};
    unsigned char secret[SECRET_BYTES];
    randombytes_buf(secret, sizeof secret);   // swap seed + EVM key; 32 random bytes is a valid secp256k1 key with overwhelming probability

    unsigned char salt[crypto_pwhash_SALTBYTES];
    randombytes_buf(salt, sizeof salt);
    const quint64 ops = crypto_pwhash_OPSLIMIT_MODERATE, mem = crypto_pwhash_MEMLIMIT_MODERATE;
    unsigned char key[crypto_secretbox_KEYBYTES];
    if (!deriveKey(password, QByteArray(reinterpret_cast<char *>(salt), sizeof salt), ops, mem, key)) { sodium_memzero(secret, sizeof secret); return {}; }

    unsigned char nonce[crypto_secretbox_NONCEBYTES];
    randombytes_buf(nonce, sizeof nonce);
    QByteArray box(SECRET_BYTES + crypto_secretbox_MACBYTES, '\0');
    crypto_secretbox_easy(reinterpret_cast<unsigned char *>(box.data()), secret, SECRET_BYTES, nonce, key);
    sodium_memzero(key, sizeof key);

    const QByteArray json = QJsonDocument(QJsonObject{
        {"schema", SCHEMA}, {"kdf", "argon2id"}, {"opslimit", double(ops)}, {"memlimit", double(mem)},
        {"salt", QString::fromLatin1(hex(salt, sizeof salt))}, {"nonce", QString::fromLatin1(hex(nonce, sizeof nonce))},
        {"box", QString::fromLatin1(box.toHex())}}).toJson(QJsonDocument::Compact);

    QSaveFile file(m_path);
    const bool wrote = file.open(QIODevice::WriteOnly)
        && file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        && file.write(json) == json.size() && file.commit();
    if (!wrote) { sodium_memzero(secret, sizeof secret); return {}; }
    const auto keys = keysFromSecret(secret);
    sodium_memzero(secret, sizeof secret);
    return keys;
}

DexTradeKeyStore::Keys DexTradeKeyStore::unlock(const QByteArray &password) const {
    if (sodium_init() < 0) return {};
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 8192) return {};
    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return {};
    const auto o = doc.object();
    if (o.value("schema") != SCHEMA || o.value("kdf") != "argon2id") return {};
    const auto salt = QByteArray::fromHex(o.value("salt").toString().toLatin1());
    const auto nonce = QByteArray::fromHex(o.value("nonce").toString().toLatin1());
    auto box = QByteArray::fromHex(o.value("box").toString().toLatin1());
    const quint64 ops = quint64(o.value("opslimit").toDouble()), mem = quint64(o.value("memlimit").toDouble());
    if (nonce.size() != crypto_secretbox_NONCEBYTES || box.size() != SECRET_BYTES + crypto_secretbox_MACBYTES
        || ops < crypto_pwhash_OPSLIMIT_MIN || mem < crypto_pwhash_MEMLIMIT_MIN) return {};

    unsigned char key[crypto_secretbox_KEYBYTES];
    if (!deriveKey(password, salt, ops, mem, key)) return {};
    unsigned char secret[SECRET_BYTES];
    const bool opened = crypto_secretbox_open_easy(secret, reinterpret_cast<const unsigned char *>(box.constData()),
                                                   box.size(), reinterpret_cast<const unsigned char *>(nonce.constData()), key) == 0;
    sodium_memzero(key, sizeof key);
    sodium_memzero(box.data(), box.size());
    if (!opened) return {};   // wrong password or a tampered file
    const auto keys = keysFromSecret(secret);
    sodium_memzero(secret, sizeof secret);
    return keys;
}
