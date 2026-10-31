#include "../LightWalletRecoveryProtocol.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);int checks=0;
    const auto check=[&](bool ok) {++checks;if (!ok) {std::cerr<<"Recovery protocol check failed: "<<checks<<'\n';std::exit(1);}};
    const QString request="11111111-2222-4333-8444-555555555555",wallet="22222222-3333-4444-8555-666666666666";
    for (const QString coin:{"ltc","btc","bch","doge"}) {
        const QByteArray wire=(QString("{\"schema\":\"monzero-%1-light-wallet-recovery-response-v1\",\"requestId\":\"%2\",\"coin\":\"%1\",\"network\":\"regtest\",\"walletId\":\"%3\",\"account\":7,\"seedEncoding\":\"raw-bip32-seed-hex-v1\",\"seedHex\":\"").arg(coin,request,wallet)).toLatin1()+QByteArray(64,'a')+"\",\"activeSwapRecovery\":false}";
        QByteArray seed;
        const auto parse=[&](const QByteArray &input) {return LightWalletRecoveryProtocol::parse(input,coin,"regtest",request,wallet,7,seed);};
        check(parse(wire));check(seed==QByteArray(64,'a'));
        for (int length=0;length<wire.size();++length) {seed="previous-secret";check(!parse(wire.left(length)));check(seed.isEmpty());}
        check(!parse(wire+" "));check(!parse(wire+wire));
        auto altered=wire;altered.replace("raw-bip32-seed-hex-v1","bip39");check(!parse(altered));
        altered=wire;altered.replace(QByteArray(64,'a'),QByteArray(64,'A'));check(!parse(altered));
        altered=wire;altered.replace("false}","true}");check(!parse(altered));
        check(!LightWalletRecoveryProtocol::parse(wire,coin,"regtest",wallet,wallet,7,seed));
        check(!LightWalletRecoveryProtocol::parse(wire,coin,"regtest",request,request,7,seed));
        check(!LightWalletRecoveryProtocol::parse(wire,coin,"regtest",request,wallet,8,seed));
        check(!LightWalletRecoveryProtocol::parse(wire,coin,"main",request,wallet,7,seed));
        check(!LightWalletRecoveryProtocol::parse(wire,"unknown","regtest",request,wallet,7,seed));
    }
    for (const QString coin:{"ltc","btc","bch","doge"}) {
        QByteArray expected;for (int i=0;i<11;++i) expected+="abandon ";expected+="about";
        const QByteArray prefix=(QString("{\"schema\":\"monzero-%1-light-wallet-mnemonic-recovery-response-v1\",\"requestId\":\"%2\",\"coin\":\"%1\",\"network\":\"regtest\",\"walletId\":\"%3\",\"account\":7,\"seedEncoding\":\"bip39-english-v1\",\"phraseHex\":\"").arg(coin,request,wallet)).toLatin1();
        const QByteArray middle="\",\"passphraseHex\":\"",suffix="\",\"activeSwapRecovery\":false}";
        const QByteArray password=QByteArray::fromHex("636166c3a9"),wire=prefix+expected.toHex()+middle+password.toHex()+suffix;
        QByteArray phrase,passphrase;
        const auto parse=[&](const QByteArray &input) {return LightWalletRecoveryProtocol::parseMnemonic(input,coin,"regtest",request,wallet,7,phrase,passphrase);};
        check(parse(wire));check(phrase==expected);check(passphrase==password);
        check(parse(prefix+expected.toHex()+middle+suffix));check(passphrase.isEmpty());
        for (int length=0;length<wire.size();++length) {phrase="old phrase";passphrase="old passphrase";
            check(!parse(wire.left(length)));check(phrase.isEmpty() && passphrase.isEmpty());}
        check(!parse(wire+" "));check(!parse(wire+wire));
        check(!parse(prefix+expected.toHex()+middle+"c0af"+suffix));
        check(!parse(prefix+expected.toHex()+middle+"eda080"+suffix));
        check(!parse(prefix+expected.toHex()+middle+"f4908080"+suffix));
        check(!parse(prefix+expected.toHex()+middle+"c3"+suffix));
        check(!parse(prefix+expected.toHex()+middle+QByteArray(2050,'0')+suffix));
        check(!parse(prefix+expected.toUpper().toHex()+middle+suffix));
        check(!parse(prefix+QByteArray("abandon").toHex()+middle+suffix));
        check(!LightWalletRecoveryProtocol::parseMnemonic(wire,coin,"regtest",wallet,wallet,7,phrase,passphrase));
        check(phrase.isEmpty() && passphrase.isEmpty());
        check(!LightWalletRecoveryProtocol::parseMnemonic(wire,coin,"regtest",request,wallet,8,phrase,passphrase));
    }
    std::cout<<checks<<" recovery protocol checks passed\n";
}
