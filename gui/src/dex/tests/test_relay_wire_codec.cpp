#include "../RelayWireCodec.h"

#include <QtTest>

namespace {
RelayEnvelope::Evidence envelope()
{
    OfferIdentity sender;
    RelayEnvelope::Terms terms;
    terms.protocol = "monzero-dex-relay-v1";
    terms.network = "isolated-btc-xmz";
    terms.type = "offer";
    terms.sequence = 1;
    terms.createdAt = 100;
    terms.expiresAt = 700;
    terms.artifact = QByteArray(32, 'a');
    return RelayEnvelope::create(terms, sender);
}
}

class RelayWireCodecTest : public QObject
{
    Q_OBJECT
private slots:
    void streamHandlesEverySplitAndCoalescedFrames()
    {
        const auto original = envelope();
        const auto frame = RelayWireCodec::encode(original);
        for (int split = 0; split <= frame.size(); ++split) {
            RelayWireCodec::StreamDecoder decoder("monzero-dex-relay-v1", "isolated-btc-xmz");
            QVector<RelayEnvelope::Evidence> output;
            QVERIFY(decoder.feed(frame.left(split), 101, output));
            int received = output.size();
            QVERIFY(decoder.feed(frame.mid(split) + frame, 101, output));
            QCOMPARE(received + output.size(), 2);
            for (const auto &item : output) QCOMPARE(item.commitment(), original.commitment());
            QCOMPARE(decoder.bufferedBytes(), 0);
            QVERIFY(decoder.finish());
            QVERIFY(!decoder.feed(frame, 101, output));
            QVERIFY(output.isEmpty());
        }
        RelayWireCodec::StreamDecoder decoder("monzero-dex-relay-v1", "isolated-btc-xmz");
        QVector<RelayEnvelope::Evidence> output;
        for (int i = 0; i < frame.size(); ++i) {
            QVERIFY(decoder.feed(frame.mid(i, 1), 101, output));
            QCOMPARE(output.size(), i == frame.size() - 1 ? 1 : 0);
            QVERIFY(decoder.bufferedBytes() <= RelayWireCodec::maximumFrameSize);
        }
    }

    void streamFailureIsTerminalAndBatchAtomic()
    {
        const auto frame = RelayWireCodec::encode(envelope());
        const int offset = QByteArray("MONZERO-DEX-RELAY-WIRE-V1\n").size();
        auto oversized = frame.left(offset + 4);
        oversized[offset] = char(0xff);
        for (const auto &invalid : {QByteArray("X"), oversized,
             QByteArray(RelayWireCodec::StreamDecoder::maximumChunkSize + 1, 'a'),
             frame + QByteArray("X")}) {
            RelayWireCodec::StreamDecoder decoder("monzero-dex-relay-v1", "isolated-btc-xmz");
            QVector<RelayEnvelope::Evidence> output;
            QVERIFY(!decoder.feed(invalid, 101, output));
            QVERIFY(output.isEmpty());
            QVERIFY(decoder.failed());
            QCOMPARE(decoder.bufferedBytes(), 0);
            QVERIFY(!decoder.feed(frame, 101, output));
        }
        RelayWireCodec::StreamDecoder truncated("monzero-dex-relay-v1", "isolated-btc-xmz");
        QVector<RelayEnvelope::Evidence> output;
        QVERIFY(truncated.feed(frame.left(frame.size() - 1), 101, output));
        QVERIFY(!truncated.finish());
        RelayWireCodec::StreamDecoder expired("monzero-dex-relay-v1", "isolated-btc-xmz");
        QVERIFY(expired.feed(frame.left(frame.size() - 1), 101, output));
        QVERIFY(!expired.feed(frame.right(1), 700, output));
        QVERIFY(output.isEmpty());
    }

    void roundTripsCanonicalAuthenticatedFrame()
    {
        const auto original = envelope();
        const QByteArray frame = RelayWireCodec::encode(original);
        QVERIFY(!frame.isEmpty());
        QVERIFY(frame.size() <= RelayWireCodec::maximumFrameSize);
        const auto decoded = RelayWireCodec::decode(frame,
            "monzero-dex-relay-v1", "isolated-btc-xmz", 101);
        QVERIFY(decoded.accepted());
        QCOMPARE(decoded.commitment(), original.commitment());
        QCOMPARE(decoded.senderPublicKey(), original.senderPublicKey());
        QCOMPARE(RelayWireCodec::encode(decoded), frame);
        QVERIFY(!decoded.details().value("executable").toBool());
    }

    void rejectsTruncationExtensionAndLengthConfusion()
    {
        const QByteArray frame = RelayWireCodec::encode(envelope());
        for (int retained = 0; retained < frame.size(); ++retained)
            QVERIFY(!RelayWireCodec::decode(frame.left(retained),
                "monzero-dex-relay-v1", "isolated-btc-xmz", 101).accepted());
        QVERIFY(!RelayWireCodec::decode(frame + 'x', "monzero-dex-relay-v1",
            "isolated-btc-xmz", 101).accepted());
        QByteArray changed = frame;
        changed[0] = 'X';
        QVERIFY(!RelayWireCodec::decode(changed, "monzero-dex-relay-v1",
            "isolated-btc-xmz", 101).accepted());
        changed = frame;
        const int lengthOffset = QByteArray("MONZERO-DEX-RELAY-WIRE-V1\n").size();
        changed[lengthOffset] = char(0x7f);
        QVERIFY(!RelayWireCodec::decode(changed, "monzero-dex-relay-v1",
            "isolated-btc-xmz", 101).accepted());
        QVERIFY(!RelayWireCodec::decode(QByteArray(
            RelayWireCodec::maximumFrameSize + 1, 'x'), "monzero-dex-relay-v1",
            "isolated-btc-xmz", 101).accepted());
    }

    void rejectsMutationDomainLifetimeAndInvalidEvidence()
    {
        const QByteArray frame = RelayWireCodec::encode(envelope());
        QByteArray changed = frame;
        changed[changed.size() - 1] = char(changed.at(changed.size() - 1) ^ 1);
        QVERIFY(!RelayWireCodec::decode(changed, "monzero-dex-relay-v1",
            "isolated-btc-xmz", 101).accepted());
        QVERIFY(!RelayWireCodec::decode(frame, "other-protocol",
            "isolated-btc-xmz", 101).accepted());
        QVERIFY(!RelayWireCodec::decode(frame, "monzero-dex-relay-v1",
            "other-network", 101).accepted());
        QVERIFY(!RelayWireCodec::decode(frame, "monzero-dex-relay-v1",
            "isolated-btc-xmz", 99).accepted());
        QVERIFY(!RelayWireCodec::decode(frame, "monzero-dex-relay-v1",
            "isolated-btc-xmz", 700).accepted());
        RelayEnvelope::Evidence empty;
        QVERIFY(RelayWireCodec::encode(empty).isEmpty());
    }
};

QTEST_GUILESS_MAIN(RelayWireCodecTest)
#include "test_relay_wire_codec.moc"
