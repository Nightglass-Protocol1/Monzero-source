#ifndef MONZERO_LIGHT_WALLET_RECOVERY_PROTOCOL_H
#define MONZERO_LIGHT_WALLET_RECOVERY_PROTOCOL_H
#include "LightWalletProfile.h"
#include <QByteArray>
#include <QUuid>

namespace LightWalletRecoveryProtocol {
// Exact private response parser. Do not construct a QJsonDocument/QVariantMap
// containing the secret. The caller erases both wire and returned seed buffers.
inline bool parse(const QByteArray &wire,const QString &coin,const QString &network,
                  const QString &requestId,const QString &walletId,int account,QByteArray &seed) {
    if (!seed.isEmpty()) {volatile char *p=seed.data();for (int i=0;i<seed.size();++i) p[i]=0;seed.clear();}
    const auto canonical=[](const QString &id) {const QUuid parsed(id);return !parsed.isNull() && parsed.toString(QUuid::WithoutBraces)==id;};
    if (!LightWalletProfile::networkAllowed(coin,network) || !canonical(requestId) || !canonical(walletId) || account<0) return false;
    const QByteArray prefix=(QString("{\"schema\":\"monzero-%1-light-wallet-recovery-response-v1\",\"requestId\":\"%2\",\"coin\":\"%1\",\"network\":\"%3\",\"walletId\":\"%4\",\"account\":%5,\"seedEncoding\":\"raw-bip32-seed-hex-v1\",\"seedHex\":\"")
        .arg(coin,requestId,network,walletId).arg(account)).toLatin1();
    const QByteArray suffix="\",\"activeSwapRecovery\":false}";
    if (wire.size()!=prefix.size()+64+suffix.size() || !wire.startsWith(prefix) || !wire.endsWith(suffix)) return false;
    for (int i=prefix.size();i<prefix.size()+64;++i) if (!((wire[i]>='0' && wire[i]<='9') || (wire[i]>='a' && wire[i]<='f'))) return false;
    seed=wire.mid(prefix.size(),64);return true;
}
inline void erasePrivate(QByteArray &value) {
    volatile char *p=value.data();for (int i=0;i<value.size();++i) p[i]=0;value.clear();
}
inline bool parseMnemonic(const QByteArray &wire,const QString &coin,const QString &network,
    const QString &requestId,const QString &walletId,int account,QByteArray &phrase,QByteArray &passphrase) {
    erasePrivate(phrase);erasePrivate(passphrase);
    const auto canonical=[](const QString &id) {const QUuid parsed(id);return !parsed.isNull() && parsed.toString(QUuid::WithoutBraces)==id;};
    if (!LightWalletProfile::networkAllowed(coin,network) || !canonical(requestId) || !canonical(walletId) || account<0 || wire.size()>4096) return false;
    const QByteArray prefix=(QString("{\"schema\":\"monzero-%1-light-wallet-mnemonic-recovery-response-v1\",\"requestId\":\"%2\",\"coin\":\"%1\",\"network\":\"%3\",\"walletId\":\"%4\",\"account\":%5,\"seedEncoding\":\"bip39-english-v1\",\"phraseHex\":\"")
        .arg(coin,requestId,network,walletId).arg(account)).toLatin1();
    const QByteArray middle="\",\"passphraseHex\":\"",suffix="\",\"activeSwapRecovery\":false}";
    if (!wire.startsWith(prefix) || !wire.endsWith(suffix)) return false;
    const int split=wire.indexOf(middle,prefix.size()),end=wire.size()-suffix.size();
    if (split<0 || split+middle.size()>end) return false;
    const auto reject=[&] {erasePrivate(phrase);erasePrivate(passphrase);return false;};
    const auto decode=[&](int from,int to,int limit,QByteArray &out) {
        const int length=to-from;if (length<0 || length%2 || length/2>limit) return false;
        out.resize(length/2);
        const auto digit=[](char c) {return c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:-1;};
        for (int i=0;i<out.size();++i) {const int high=digit(wire[from+2*i]),low=digit(wire[from+2*i+1]);
            if (high<0 || low<0) return false;out[i]=char(high*16+low);}
        return true;
    };
    if (!decode(prefix.size(),split,256,phrase) || !decode(split+middle.size(),end,1024,passphrase)) return reject();
    int words=1,wordLength=0;
    for (char c:phrase) {
        if (c==' ') {if (wordLength<1 || wordLength>8) return reject();++words;wordLength=0;}
        else {if (c<'a' || c>'z') return reject();++wordLength;}
    }
    if (wordLength<1 || wordLength>8 || (words!=12 && words!=15 && words!=18 && words!=21 && words!=24)) return reject();
    // Validate UTF-8 without temporary QString copies of the private passphrase.
    for (int i=0;i<passphrase.size();) {
        const auto first=static_cast<unsigned char>(passphrase[i++]);
        if (first<0x80) continue;
        int remaining=0;unsigned value=0,minimum=0;
        if (first>=0xc2 && first<=0xdf) {remaining=1;value=first&0x1f;minimum=0x80;}
        else if (first>=0xe0 && first<=0xef) {remaining=2;value=first&0x0f;minimum=0x800;}
        else if (first>=0xf0 && first<=0xf4) {remaining=3;value=first&7;minimum=0x10000;}
        else return reject();
        if (i+remaining>passphrase.size()) return reject();
        while (remaining--) {const auto next=static_cast<unsigned char>(passphrase[i++]);if ((next&0xc0)!=0x80) return reject();value=(value<<6)|(next&0x3f);}
        if (value<minimum || value>0x10ffff || (value>=0xd800 && value<=0xdfff)) return reject();
    }
    return true;
}

}
#endif
