#include "src/dex/OfferIdentity.h"
#include <QCoreApplication>
#include <QDebug>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    OfferIdentity maker, impostor;
    const QByteArray payload("{\"pair\":\"BTC/XMZ\",\"amount\":\"1\"}");
    const QByteArray network("isolated-test-genesis");
    const auto signature = maker.sign(payload, network);
    int failures = 0, checks = 0;
    const auto expect = [&](bool ok) { ++checks; if (!ok) ++failures; };
    expect(OfferIdentity::verify(payload, network, maker.publicKey(), signature));
    expect(!OfferIdentity::verify(payload + " ", network, maker.publicKey(), signature));
    expect(!OfferIdentity::verify(payload, "mainnet", maker.publicKey(), signature));
    expect(!OfferIdentity::verify(payload, network, impostor.publicKey(), signature));
    expect(!OfferIdentity::verify(payload, network, maker.publicKey(), signature.left(63)));
    expect(!OfferIdentity::verify(payload, network, maker.publicKey().left(31), signature));
    expect(maker.sign(QByteArray(16385, 'x'), network).isEmpty());
    expect(maker.sign(payload, {}).isEmpty());
    const QByteArray passphrase("test-only-long-identity-passphrase");
    const auto encrypted = maker.encrypt(passphrase);
    expect(!encrypted.isEmpty());
    expect(encrypted != maker.encrypt(passphrase));
    auto restored = OfferIdentity::decrypt(encrypted, passphrase);
    expect(bool(restored));
    if (restored) {
        expect(restored->publicKey() == maker.publicKey());
        expect(restored->sign(payload, network) == signature);
    }
    expect(!OfferIdentity::decrypt(encrypted, "wrong-passphrase"));
    expect(!OfferIdentity::decrypt(encrypted, {}));
    expect(maker.encrypt({}).isEmpty());
    expect(maker.encrypt(QByteArray(1025, 'a')).isEmpty());
    expect(!OfferIdentity::decrypt(encrypted + 'x', passphrase));
    for (int size = 0; size < encrypted.size(); ++size)
        expect(!OfferIdentity::decrypt(encrypted.left(size), passphrase));
    // Header, salt, nonce, and encrypted seed/tag must all resist mutation.
    for (const int index : {0, encrypted.size() - 49, encrypted.size() - 88,
                           encrypted.size() - 1}) {
        auto changed = encrypted;
        changed[index] = char(changed.at(index) ^ 1);
        expect(!OfferIdentity::decrypt(changed, passphrase));
    }
    qInfo() << checks << "signature checks," << failures << "failures";
    return failures ? 1 : 0;
}
