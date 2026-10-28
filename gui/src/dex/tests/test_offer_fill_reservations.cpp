#include "../OfferFillReservationStore.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

namespace {
SignedOffer::Terms offer(const QString &id, const QString &side,
                         const QString &amount, const QString &price)
{
    return {"monzero-dex-offer-v1", "isolated-btc-xmz", id, "BTC/XMZ", side,
        amount, price, 100, 3700};
}

AcceptedQuote acceptedQuote(const QString &path, const OfferIdentity &maker,
    const OfferIdentity &taker, const QByteArray &reservationCommitment,
    const QString &baseAmount, const QString &xmzAmount, qint64 now, bool mutual = true)
{
    SettlementTerms::Terms t;
    t.protocolId = "btc-xmz-adaptor-draft-v1";
    t.payAsset = "BTC"; t.receiveAsset = "XMZ";
    t.payGenesis = QString(64, '1');
    t.receiveGenesis = t.xmzGenesis = QString(64, '2');
    t.payNetwork = "btc-regtest";
    t.receiveNetwork = t.xmzNetwork = "xmz-fakechain";
    t.payAmountAtomic = baseAmount; t.receiveAmountAtomic = xmzAmount;
    t.feeAtomic = "1";
    t.payNativeFeeMaxAtomic = "500"; t.receiveNativeFeeMaxAtomic = "10";
    t.xmzNativeFeeMaxAtomic = "10";
    t.payRecipient = "pay"; t.receiveRecipient = "receive"; t.feeRecipient = "fee";
    t.payRefundRecipient = "pay-refund";
    t.receiveRefundRecipient = "receive-refund";
    t.xmzRefundRecipient = "fee-refund";
    t.payAnchorHeight = t.receiveAnchorHeight = t.xmzAnchorHeight = "100";
    t.payRefundHeight = "300"; t.receiveRefundHeight = "200";
    t.xmzRefundHeight = "210";
    t.payConfirmations = 6; t.receiveConfirmations = t.xmzConfirmations = 5;
    t.payLockCommitment = QByteArray(32, 'a');
    t.payRedeemCommitment = QByteArray(32, 'b');
    t.payCancelCommitment = QByteArray(32, 'c');
    t.payRefundCommitment = QByteArray(32, 'd');
    t.payPunishCommitment = QByteArray(32, 'e');
    t.receiveLockCommitment = QByteArray(32, 'f');
    t.receiveRefundCommitment = QByteArray(32, 'g');
    t.xmzFeeLockCommitment = QByteArray(32, 'h');
    t.xmzFeeRefundCommitment = QByteArray(32, 'i');
    t.payCancelBlocks = 72; t.payPunishBlocks = 72;
    t.payContractKeyA = QByteArray::fromHex(
        "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798");
    t.payContractKeyB = QByteArray::fromHex(
        "02c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5");
    t.makerAdaptorSecpKey = t.payContractKeyA;
    t.takerAdaptorSecpKey = t.payContractKeyB;
    t.makerSwapEd25519Key = QByteArray(32, 'm');
    t.takerSwapEd25519Key = QByteArray(32, 'n');
    t.makerDleqProofCommitment = QByteArray(32, 'o');
    t.takerDleqProofCommitment = QByteArray(32, 'p');
    t.makerKey = maker.publicKey(); t.takerKey = taker.publicKey();
    // payAsset is always "BTC" in this file's fixtures, and every call here
    // passes (path, buyer, seller, ...): the taker/seller is the one funding
    // the BTC leg.
    t.payerKey = t.takerKey;
    t.nonce = QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256);
    t.offerFillReservationCommitment = reservationCommitment;
    t.quoteExpiry = now + 100; t.refundDeadline = now + 300;
    t.feeFromAcquiredXmz = true;
    const QByteArray domain("fill-test-domain");
    const auto built = SettlementTerms::build(t, now);
    SignedQuoteAcceptor acceptor(path, t.protocolId, t.payNetwork,
        t.receiveNetwork, t.xmzNetwork, domain);
    if (!mutual) return acceptor.acceptEvidence(t, maker.sign(built.payload, domain), now);
    return acceptor.acceptMutualEvidence(t, maker.sign(built.payload, domain),
        taker.sign(SignedQuoteAcceptor::takerAcceptancePayload(t, now), domain), now);
}
}

class OfferFillReservationTest : public QObject
{
    Q_OBJECT
private slots:
    void rejectsSharedAndSymlinkedStorageWithoutRewrite()
    {
#ifndef Q_OS_UNIX
        QSKIP("Unix private-storage permissions and symlink policy");
#endif
        QTemporaryDir directory;
        OfferBookStore book(directory.filePath("offers.json"), "monzero-dex-offer-v1", "isolated-btc-xmz");
        const auto path = directory.filePath("fills.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray contents("{\"version\":2,\"records\":[]}");
        QCOMPARE(file.write(contents), qint64(contents.size())); file.close();
        QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup));
        {
            OfferFillReservationStore shared(path, "monzero-dex-offer-v1", "isolated-btc-xmz", book);
            QVERIFY(!shared.ready());
            QVERIFY(!QFileInfo::exists(path + ".lock"));
        }
        QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        const auto link = directory.filePath("linked.json");
        QVERIFY(QFile::link(path, link));
        {
            OfferFillReservationStore linked(link, "monzero-dex-offer-v1", "isolated-btc-xmz", book);
            QVERIFY(!linked.ready());
        }
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), contents); file.close();
        OfferFillReservationStore valid(path, "monzero-dex-offer-v1", "isolated-btc-xmz", book);
        QVERIFY(valid.ready());
    }

    void recoversExpiredCommittedFillWithoutReacceptance()
    {
        QTemporaryDir directory;
        OfferIdentity buyer, seller;
        const auto buy = SignedOffer::create(offer(
            "11111111-1111-4111-8111-111111111111", "buy", "1", "120000000000000"), buyer);
        const auto sell = SignedOffer::create(offer(
            "22222222-2222-4222-8222-222222222222", "sell", "1", "110000000000000"), seller);
        OfferBookStore book(directory.filePath("offers.json"), "monzero-dex-offer-v1", "isolated-btc-xmz");
        QVERIFY(book.insert(buy, 101)); QVERIFY(book.insert(sell, 101));
        const auto path = directory.filePath("fills.json");
        const auto quotePath = directory.filePath("quote.json");
        const QString id = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
        AcceptedQuote quote;
        {
            OfferFillReservationStore fills(path, "monzero-dex-offer-v1", "isolated-btc-xmz", book);
            const auto reserved = fills.reserve(id, buy, sell, "1", 300, 200);
            QVERIFY(reserved.accepted());
            quote = acceptedQuote(quotePath, buyer, seller, reserved.commitment(), "1", "1150000", 200);
            QVERIFY(fills.commit(id, quote, 201));
        }
        const auto terms = quote.terms();
        SignedQuoteAcceptor acceptor(quotePath, terms.protocolId, terms.payNetwork,
            terms.receiveNetwork, terms.xmzNetwork, "fill-test-domain");
        const auto recovered = acceptor.recoverMutualQuote(terms.makerKey, terms.nonce, 4000);
        QVERIFY(recovered.valid());
        OfferFillReservationStore restored(path, "monzero-dex-offer-v1", "isolated-btc-xmz", book);
        QVERIFY(restored.ready());
        const auto before = restored.records();
        const auto fill = restored.recoverCommitted(id, recovered, 4000);
        QVERIFY(fill.accepted());
        QVERIFY(fill.details().value("recoveryOnly").toBool());
        QVERIFY(!fill.details().value("executable").toBool());
        QVERIFY(!restored.recoverCommitted("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb", recovered, 4000).accepted());
        QVERIFY(!restored.recoverCommitted(id, RecoveredMutualQuote{}, 4000).accepted());
        QVERIFY(!restored.recoverCommitted(id, recovered, 199).accepted());
        QVERIFY(!restored.release(id));
        QCOMPARE(restored.records(), before);
    }

    void preservesFractionalAskAndBidLimits()
    {
        for (const QString &amount : {QString("1100000"), QString("1100001"), QString("1100002")}) {
            QTemporaryDir directory;
            OfferIdentity buyer, seller;
            const auto buy = SignedOffer::create(offer(
                "11111111-1111-4111-8111-111111111111", "buy", "1", "110000199999999"), buyer);
            const auto sell = SignedOffer::create(offer(
                "22222222-2222-4222-8222-222222222222", "sell", "1", "110000000000001"), seller);
            OfferBookStore book(directory.filePath("offers.json"),
                "monzero-dex-offer-v1", "isolated-btc-xmz");
            QVERIFY(book.insert(buy, 101));
            QVERIFY(book.insert(sell, 101));
            OfferFillReservationStore fills(directory.filePath("fills.json"),
                "monzero-dex-offer-v1", "isolated-btc-xmz", book);
            const QString id = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
            const auto reservation = fills.reserve(id, buy, sell, "1", 300, 200);
            QVERIFY(reservation.accepted());
            const auto quote = acceptedQuote(directory.filePath("quote.json"),
                buyer, seller, reservation.commitment(), "1", amount, 200);
            QVERIFY(quote.accepted());
            QCOMPARE(fills.commit(id, quote, 201), amount == "1100001");
        }
    }

    void preventsAggregateOverfillAndPersists()
    {
        QTemporaryDir directory;
        const QString bookPath = directory.filePath("offers.json");
        const QString fillsPath = directory.filePath("fills.json");
        OfferIdentity buyer, sellerA, sellerB;
        const auto buy = SignedOffer::create(offer(
            "11111111-1111-4111-8111-111111111111", "buy", "100000", "120000000000000"), buyer);
        const auto sellA = SignedOffer::create(offer(
            "22222222-2222-4222-8222-222222222222", "sell", "80000", "110000000000000"), sellerA);
        const auto sellB = SignedOffer::create(offer(
            "33333333-3333-4333-8333-333333333333", "sell", "80000", "115000000000000"), sellerB);
        OfferBookStore book(bookPath, "monzero-dex-offer-v1", "isolated-btc-xmz");
        QVERIFY(book.insert(buy, 101));
        QVERIFY(book.insert(sellA, 101));
        QVERIFY(book.insert(sellB, 101));
        {
            OfferFillReservationStore fills(fillsPath, "monzero-dex-offer-v1",
                "isolated-btc-xmz", book);
            auto first = fills.reserve("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
                buy, sellA, "60000", 300, 200);
            QVERIFY(first.accepted());
            QVERIFY(!first.details().value("executable").toBool());
            QCOMPARE(fills.reserve("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
                buy, sellA, "60000", 300, 200).commitment(), first.commitment());
            QVERIFY(!fills.reserve("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",
                buy, sellB, "50000", 301, 201).accepted());
            QVERIFY(fills.reserve("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",
                buy, sellB, "40000", 301, 201).accepted());
            QCOMPARE(QFileInfo(fillsPath).permissions() & (QFileDevice::ReadGroup
                | QFileDevice::WriteGroup | QFileDevice::ReadOther
                | QFileDevice::WriteOther), QFileDevice::Permissions{});
        }
        OfferFillReservationStore restored(fillsPath, "monzero-dex-offer-v1",
            "isolated-btc-xmz", book);
        QVERIFY(restored.ready());
        QCOMPARE(restored.records().size(), 2);
    }

    void enforcesCancellationCommitAndFailSafeCapacity()
    {
        QTemporaryDir directory;
        OfferIdentity buyer, seller;
        const auto buy = SignedOffer::create(offer(
            "11111111-1111-4111-8111-111111111111", "buy", "100000", "120000000000000"), buyer);
        const auto sell = SignedOffer::create(offer(
            "22222222-2222-4222-8222-222222222222", "sell", "100000", "110000000000000"), seller);
        OfferBookStore book(directory.filePath("offers.json"),
            "monzero-dex-offer-v1", "isolated-btc-xmz");
        QVERIFY(book.insert(buy, 101));
        QVERIFY(book.insert(sell, 101));
        OfferFillReservationStore fills(directory.filePath("fills.json"),
            "monzero-dex-offer-v1", "isolated-btc-xmz", book);
        const QString id = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
        QVERIFY(fills.reserve(id, buy, sell, "60000", 300, 200).accepted());
        QVERIFY(fills.release(id));
        const QString third = "cccccccc-cccc-4ccc-8ccc-cccccccccccc";
        const auto thirdReservation = fills.reserve(third, buy, sell, "1", 401, 301);
        QVERIFY(thirdReservation.accepted());
        const auto thirdQuote = acceptedQuote(directory.filePath("third-quote.json"),
            buyer, seller, thirdReservation.commitment(), "1", "1150000", 301);
        QVERIFY(thirdQuote.accepted());
        SignedOfferCancellation::Terms cancellation{sell.terms().protocolId,
            sell.terms().network, sell.offerId(), sell.commitment(), 302};
        QVERIFY(book.cancel(SignedOfferCancellation::create(cancellation, seller), 302));
        QVERIFY(!fills.commit(third, thirdQuote, 303));
        QVERIFY(fills.release(third));

        // Use a fresh uncancelled seller: committed and settled quantity cannot be
        // released or reused. Only the journal can mint typed settlement evidence.
        OfferIdentity sellerTwo;
        const auto sellTwo = SignedOffer::create(offer(
            "33333333-3333-4333-8333-333333333333", "sell", "100000", "110000000000000"),
            sellerTwo);
        QVERIFY(book.insert(sellTwo, 303));
        const QString committed = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
        const auto fullReservation =
            fills.reserve(committed, buy, sellTwo, "100000", 500, 303);
        QVERIFY(fullReservation.accepted());
        const auto fullQuote = acceptedQuote(directory.filePath("full-quote.json"),
            buyer, sellerTwo, fullReservation.commitment(), "100000",
            "115000000000", 303);
        QVERIFY(fullQuote.accepted());
        const auto wrongReservation = acceptedQuote(
            directory.filePath("wrong-reservation-quote.json"), buyer,
            sellerTwo, QByteArray(32, 'x'), "100000",
            "115000000000", 303);
        QVERIFY(wrongReservation.accepted());
        QVERIFY(!fills.commit(committed, wrongReservation, 304));
        const auto wrongAmount = acceptedQuote(directory.filePath("wrong-amount-quote.json"),
            buyer, sellerTwo, fullReservation.commitment(), "99999",
            "114998850000", 303);
        QVERIFY(wrongAmount.accepted());
        QVERIFY(!fills.commit(committed, wrongAmount, 304));
        const auto wrongPrice = acceptedQuote(directory.filePath("wrong-price-quote.json"),
            buyer, sellerTwo, fullReservation.commitment(), "100000",
            "100000000000", 303);
        QVERIFY(wrongPrice.accepted());
        QVERIFY(!fills.commit(committed, wrongPrice, 304));
        OfferIdentity outsider;
        const auto wrongIdentity = acceptedQuote(directory.filePath("wrong-id-quote.json"),
            outsider, sellerTwo, fullReservation.commitment(), "100000",
            "115000000000", 303);
        QVERIFY(wrongIdentity.accepted());
        QVERIFY(!fills.commit(committed, wrongIdentity, 304));
        const auto unilateral = acceptedQuote(directory.filePath("unilateral-quote.json"),
            buyer, sellerTwo, fullReservation.commitment(), "100000", "115000000000", 303, false);
        QVERIFY(unilateral.accepted());
        QVERIFY(!unilateral.mutuallyAuthorized());
        const auto before = fills.records();
        QVERIFY(!fills.commit(committed, unilateral, 304));
        QCOMPARE(fills.records(), before);
        QVERIFY(fullQuote.mutuallyAuthorized());
        QVERIFY(fills.commit(committed, fullQuote, 304));
        QVERIFY(!fills.release(committed));
        QVERIFY(!fills.reserve("dddddddd-dddd-4ddd-8ddd-dddddddddddd",
            buy, sellTwo, "1", 501, 305).accepted());
    }

    void failsClosedOnInvalidAndCorruptState()
    {
        QTemporaryDir directory;
        OfferIdentity buyer, seller;
        const auto buy = SignedOffer::create(offer(
            "11111111-1111-4111-8111-111111111111", "buy", "100000", "120000000000000"), buyer);
        const auto sell = SignedOffer::create(offer(
            "22222222-2222-4222-8222-222222222222", "sell", "100000", "110000000000000"), seller);
        OfferBookStore book(directory.filePath("offers.json"),
            "monzero-dex-offer-v1", "isolated-btc-xmz");
        QVERIFY(book.insert(buy, 101)); QVERIFY(book.insert(sell, 101));
        const QString path = directory.filePath("fills.json");
        {
            OfferFillReservationStore first(path, "monzero-dex-offer-v1",
                "isolated-btc-xmz", book);
            QVERIFY(!first.reserve("bad", buy, sell, "1", 300, 200).accepted());
            QVERIFY(!first.reserve("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
                buy, sell, "0", 300, 200).accepted());
            QVERIFY(!first.reserve("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
                buy, sell, "1", 1200, 200).accepted());
            OfferFillReservationStore second(path, "monzero-dex-offer-v1",
                "isolated-btc-xmz", book);
            QVERIFY(!second.ready());
        }
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("{broken") > 0); file.close();
        OfferFillReservationStore corrupt(path, "monzero-dex-offer-v1",
            "isolated-btc-xmz", book);
        QVERIFY(!corrupt.ready());
    }
};

QTEST_GUILESS_MAIN(OfferFillReservationTest)
#include "test_offer_fill_reservations.moc"
