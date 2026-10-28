#include "../SettlementJournal.h"
#include "../CrossCurveProofEvidence.h"
#include "../CryptoVerifierClient.h"
#include "TestAcceptedQuote.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

class SettlementJournalTest : public QObject
{
    Q_OBJECT
private slots:
    void fullJournalStillAcceptsExactDurableRetries_data()
    {
        QTest::addColumn<bool>("duplicateAcceptance");
        QTest::newRow("valid-full-history") << false;
        QTest::newRow("hash-valid-duplicate-acceptance") << true;
    }
    void fullJournalStillAcceptsExactDurableRetries()
    {
        QFETCH(bool, duplicateAcceptance);
        QTemporaryDir directory;
        const auto quote = TestAcceptedQuote::create(directory.filePath("quotes.json"), 100);
        QVERIFY(quote.accepted());
        // Construct a bounded on-disk fixture rather than performing 10,000
        // growing atomic file replacements. Reopen exercises normal validation.
        QJsonArray records;
        QByteArray previous(32, '\0');
        for (int sequence = 1; sequence <= 10000; ++sequence) {
            const auto swap = QByteArray("swap-")
                + QByteArray::number(duplicateAcceptance && sequence == 10000 ? 1 : sequence);
            // Different artifact avoids the duplicate-record check: rejection
            // must come from the already-accepted swap's transition rule.
            const auto artifact = duplicateAcceptance && sequence == 10000
                ? QByteArray(32, 'x') : quote.commitment();
            QByteArray input("MONZERO-DEX-SETTLEMENT-JOURNAL-V1\n");
            for (const auto &value : {QByteArray::number(sequence), swap,
                    QByteArray("quote_accepted"), QByteArray("protocol"),
                    artifact, QByteArray("100"), previous}) {
                QByteArray prefix;
                QDataStream stream(&prefix, QIODevice::WriteOnly);
                stream.setByteOrder(QDataStream::BigEndian);
                stream << quint32(value.size());
                input += prefix; input += value;
            }
            const auto hash = QCryptographicHash::hash(input, QCryptographicHash::Sha256);
            records.append(QJsonObject{{"sequence", QString::number(sequence)},
                {"swapId", QString::fromLatin1(swap)}, {"event", "quote_accepted"},
                {"chain", "protocol"}, {"artifactHash", QString::fromLatin1(artifact.toHex())},
                {"timestamp", "100"}, {"previousHash", QString::fromLatin1(previous.toHex())},
                {"recordHash", QString::fromLatin1(hash.toHex())}});
            previous = hash;
        }
        const auto path = directory.filePath("journal.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"records", records}})
            .toJson(QJsonDocument::Compact);
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
        file.close();
        SettlementJournal journal(path);
        if (duplicateAcceptance) {
            QVERIFY(!journal.ready());
            QVERIFY(journal.records().isEmpty());
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), bytes);
            return;
        }
        QVERIFY2(journal.ready(), qPrintable(journal.error()));
        QVERIFY(journal.appendQuoteAccepted("swap-1", quote, 100));
        QVERIFY(journal.appendQuoteAccepted("swap-10000", quote, 100));
        QVERIFY(!journal.appendQuoteAccepted("swap-1", quote, 101));
        QVERIFY(!journal.appendQuoteAccepted("swap-new", quote, 100));
        QCOMPARE(journal.records().size(), 10000);
        QCOMPARE(journal.headHash(), QString::fromLatin1(previous.toHex()));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), bytes);
    }

    void recoveredQuoteMustMatchExistingSwapJournal()
    {
        QTemporaryDir directory;
        const auto original = TestAcceptedQuote::create(directory.filePath("original.json"));
        OfferIdentity maker, taker;
        auto terms = original.terms();
        terms.makerKey = maker.publicKey(); terms.takerKey = taker.publicKey();
        terms.payerKey = terms.takerKey;
        const QByteArray domain("journal-test-domain");
        const auto path = directory.filePath("mutual.json");
        const auto journalPath = directory.filePath("journal.json");
        {
            SignedQuoteAcceptor acceptor(path, terms.protocolId, terms.payNetwork,
                terms.receiveNetwork, terms.xmzNetwork, domain);
            const auto quote = acceptor.acceptMutualEvidence(terms,
                maker.sign(SettlementTerms::build(terms, 100).payload, domain),
                taker.sign(SignedQuoteAcceptor::takerAcceptancePayload(terms, 100), domain), 100);
            QVERIFY(quote.mutuallyAuthorized());
            SettlementJournal journal(journalPath);
            QVERIFY(!journal.ensureMutualQuoteAccepted(original, 101));
            QVERIFY(!journal.ensureMutualQuoteAccepted(quote, 500));
            QVERIFY(journal.ensureMutualQuoteAccepted(quote, 101));
            const auto stableHead = journal.headHash();
            QVERIFY(journal.ensureMutualQuoteAccepted(quote, 102));
            QVERIFY(journal.ensureMutualQuoteAccepted(quote, 500));
            QVERIFY(!journal.ensureMutualQuoteAccepted(quote, 100));
            QCOMPARE(journal.headHash(), stableHead);
            QVERIFY(!journal.recoverQuote("swap", acceptor, terms.makerKey, terms.nonce, 500).valid());
            QVERIFY(journal.appendQuoteAccepted("swap", quote, 101));
            QVERIFY(journal.appendQuoteAccepted("different", original, 101));
        }
        SignedQuoteAcceptor restored(path, terms.protocolId, terms.payNetwork,
            terms.receiveNetwork, terms.xmzNetwork, domain);
        SettlementJournal journal(journalPath);
        const auto head = journal.headHash();
        QVERIFY(journal.recoverQuote("swap", restored, terms.makerKey, terms.nonce, 500).valid());
        QVERIFY(!journal.recoverQuote("different", restored, terms.makerKey, terms.nonce, 500).valid());
        QVERIFY(!journal.recoverQuote("missing", restored, terms.makerKey, terms.nonce, 500).valid());
        QVERIFY(!journal.recoverQuote("swap", restored, terms.makerKey, terms.nonce, 100).valid());
        QCOMPARE(journal.headHash(), head);
        QCOMPARE(journal.records().size(), 3);
    }

    void rejectsUnsafeStorageWithoutWriting()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("journal.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray original("{\"version\":1,\"records\":[]}");
        QCOMPARE(file.write(original), qint64(original.size())); file.close();
        QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        {
            SettlementJournal valid(path);
            QVERIFY(valid.ready());
        }
#ifdef Q_OS_UNIX
        const auto link = directory.filePath("linked.json");
        QVERIFY(QFile::link(path, link));
        SettlementJournal linked(link);
        QVERIFY(!linked.ready());
        QVERIFY(!QFile::exists(link + ".lock"));
        QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadOther));
        SettlementJournal sharedFile(path);
        QVERIFY(!sharedFile.ready());
        QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        const auto permissions = QFileInfo(directory.path()).permissions();
        QVERIFY(QFile::setPermissions(directory.path(), permissions | QFileDevice::ReadOther));
        SettlementJournal sharedDirectory(directory.filePath("new.json"));
        QVERIFY(!sharedDirectory.ready());
        QVERIFY(!QFile::exists(directory.filePath("new.json.lock")));
        QVERIFY(QFile::setPermissions(directory.path(), permissions));
#endif
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original);
    }

    void persistsVerifiedDleqOnlyUnderMatchingQuote_data()
    {
        QTest::addColumn<bool>("isMaker");
        QTest::addColumn<bool>("recovery");
        QTest::newRow("maker") << true << false;
        QTest::newRow("taker") << false << false;
        QTest::newRow("recovered-maker") << true << true;
        QTest::newRow("recovered-taker") << false << true;
    }

    void persistsVerifiedDleqOnlyUnderMatchingQuote()
    {
        QFETCH(bool, isMaker);
        QFETCH(bool, recovery);
        const auto verifier = qEnvironmentVariable("MONZERO_DEX_CRYPTO_VERIFIER");
        if (verifier.isEmpty()) QSKIP("Pinned Rust verifier path was not supplied");
        QFile fixtureFile(QFINDTESTDATA("fixtures/local-cross-curve-dleq.json"));
        QVERIFY(fixtureFile.open(QIODevice::ReadOnly));
        const auto fixture = QJsonDocument::fromJson(fixtureFile.readAll()).object();
        const auto proof = QByteArray::fromBase64(fixture.value("proofBase64").toString().toLatin1());
        QTemporaryDir directory;
        const auto unrelated = TestAcceptedQuote::create(directory.filePath("unrelated.json"), 100);
        QVERIFY(unrelated.accepted());
        auto terms = unrelated.terms();
        OfferIdentity maker, taker;
        terms.makerKey = maker.publicKey();
        terms.takerKey = taker.publicKey();
        terms.payerKey = terms.takerKey;
        (isMaker ? terms.makerAdaptorSecpKey : terms.takerAdaptorSecpKey) =
            QByteArray::fromHex(fixture.value("secp256k1PublicKeyHex").toString().toLatin1());
        (isMaker ? terms.makerSwapEd25519Key : terms.takerSwapEd25519Key) =
            QByteArray::fromHex(fixture.value("ed25519PublicKeyHex").toString().toLatin1());
        (isMaker ? terms.makerDleqProofCommitment : terms.takerDleqProofCommitment) =
            CryptoVerifierProtocol::dleqProofCommitment(proof);
        const auto built = SettlementTerms::build(terms, 100);
        QVERIFY2(built.valid, qPrintable(built.error));
        const QByteArray domain("journal-test-domain");
        SignedQuoteAcceptor acceptor(directory.filePath("quotes.json"), terms.protocolId,
            terms.payNetwork, terms.receiveNetwork, terms.xmzNetwork, domain);
        const auto quote = acceptor.acceptMutualEvidence(terms, maker.sign(built.payload, domain),
            taker.sign(SignedQuoteAcceptor::takerAcceptancePayload(terms, 100), domain), 100);
        QVERIFY(quote.accepted());
        CryptoVerifierClient client(verifier,
            "eda07a44e9ce9dd2d5a3a3265081e266ea4da73350921a9c64c7f5a81ee6d9a5");
        const auto party = isMaker ? CrossCurveProofEvidence::Party::Maker
                                   : CrossCurveProofEvidence::Party::Taker;
        const auto recovered = acceptor.recoverMutualQuote(terms.makerKey, terms.nonce, 400);
        QVERIFY(recovered.valid());
        const auto evidence = recovery
            ? CrossCurveProofEvidence::verifyRecoveredEvidence(recovered,
                party, proof, QByteArray(32, 'q'), 400, client)
            : CrossCurveProofEvidence::verifyEvidence(terms,
                party, proof, QByteArray(32, 'q'), 100, client);
        const qint64 evidenceTime = recovery ? 402 : 102;
        QVERIFY2(evidence.accepted(), qPrintable(evidence.details().value("error").toString()));
        QCOMPARE(evidence.settlementTermsCommitment(), quote.commitment());
        const auto path = directory.filePath("journal.json");
        QString head;
        {
            SettlementJournal journal(path);
            QVERIFY(journal.ready());
            QVERIFY(!journal.appendDleqVerification("swap-1", evidence, evidenceTime));
            QVERIFY(journal.appendQuoteAccepted("other-swap", unrelated, 100));
            QVERIFY(!journal.appendDleqVerification("other-swap", evidence, evidenceTime));
            QVERIFY(journal.appendQuoteAccepted("swap-1", quote, 101));
            QVERIFY(journal.appendDleqVerification("swap-1", evidence, evidenceTime));
            QVERIFY(journal.appendDleqVerification("swap-1", evidence, evidenceTime));
            QCOMPARE(journal.records().size(), 3);
            const auto record = journal.records().last().toObject();
            QCOMPARE(record.value("event").toString(),
                QString(isMaker ? "maker_dleq_verified" : "taker_dleq_verified"));
            QCOMPARE(record.value("artifactHash").toString(), QString::fromLatin1(evidence.journalCommitment().toHex()));
            head = journal.headHash();
        }
        SettlementJournal restored(path);
        QVERIFY(restored.ready());
        QCOMPARE(restored.headHash(), head);
        QVERIFY(restored.appendDleqVerification("swap-1", evidence, evidenceTime));
        QCOMPARE(restored.records().size(), 3);
    }

    void persistenceAndIdempotence()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("settlement.json");
        const AcceptedQuote quote = TestAcceptedQuote::create(
            directory.filePath("quotes.json"), 99);
        QVERIFY(quote.accepted());
        QString head;
        {
            SettlementJournal journal(path);
            QVERIFY2(journal.ready(), qPrintable(journal.error()));
            QCOMPARE(journal.headHash(), QString(64, '0'));
            QVERIFY(journal.appendQuoteAccepted("swap-1", quote, 99));
            QVERIFY(journal.appendQuoteAccepted("swap-1", quote, 99));
            QVERIFY(!journal.appendQuoteAccepted("swap-1", quote, 100));
            QCOMPARE(journal.records().size(), 1);
            head = journal.headHash();
            QCOMPARE(head.size(), 64);
        }
        SettlementJournal reopened(path);
        QVERIFY2(reopened.ready(), qPrintable(reopened.error()));
        QCOMPARE(reopened.records().size(), 1);
        QCOMPARE(reopened.headHash(), head);
    }

    void rejectsInvalidInputAndConcurrentWriter()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("settlement.json");
        SettlementJournal first(path);
        QVERIFY(first.ready());
        SettlementJournal second(path);
        QVERIFY(!second.ready());
        SettlementJournal missing("");
        QVERIFY(!missing.ready());
        AcceptedQuote invalid;
        QVERIFY(!first.appendQuoteAccepted("swap-1", invalid, 1));
        const AcceptedQuote quote = TestAcceptedQuote::create(
            directory.filePath("quotes.json"), 1);
        QVERIFY(quote.accepted());
        QVERIFY(!first.appendQuoteAccepted("Bad ID", quote, 1));
        QVERIFY(!first.appendQuoteAccepted("swap-1", quote, -1));
        CryptoVerifierClient unavailable("/does/not/exist", QString(64, '0'));
        SettlementTerms::Terms invalidTerms;
        const auto rejectedEvidence = CrossCurveProofEvidence::verifyEvidence(invalidTerms,
            CrossCurveProofEvidence::Party::Maker, QByteArray("proof"),
            QByteArray(32, 'n'), 1, unavailable);
        QVERIFY(!rejectedEvidence.accepted());
        QVERIFY(!first.appendDleqVerification("swap-1", rejectedEvidence, 2));
    }

    void detectsTampering()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("settlement.json");
        {
            SettlementJournal journal(path);
            const AcceptedQuote quote = TestAcceptedQuote::create(
                directory.filePath("quotes.json"), 199);
            QVERIFY(quote.accepted());
            QVERIFY(journal.appendQuoteAccepted("swap-1", quote, 199));
        }
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        file.close();
        QJsonObject root = document.object();
        QJsonArray records = root.value("records").toArray();
        QJsonObject record = records[0].toObject();
        record["event"] = "failed";
        records[0] = record;
        root["records"] = records;
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(file.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) > 0);
        file.close();
        SettlementJournal corrupted(path);
        QVERIFY(!corrupted.ready());
    }

    void survivesAbruptProcessLoss()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("settlement.json");
        const QString helper = QCoreApplication::applicationDirPath()
            + "/settlement_journal_crash_writer";
        QProcess process;
        process.start(helper, {path});
        QVERIFY(process.waitForFinished(5000));
        QCOMPARE(process.exitCode(), 0);
        SettlementJournal restored(path);
        QVERIFY2(restored.ready(), qPrintable(restored.error()));
        QCOMPARE(restored.records().size(), 1);
        QCOMPARE(restored.records().last().toObject().value("event").toString(),
                 QString("quote_accepted"));
        QCOMPARE(QFileInfo(path).permissions()
                     & (QFileDevice::ReadGroup | QFileDevice::WriteGroup
                        | QFileDevice::ReadOther | QFileDevice::WriteOther),
                 QFileDevice::Permissions{});
    }
};

QTEST_GUILESS_MAIN(SettlementJournalTest)
#include "test_settlement_journal.moc"
