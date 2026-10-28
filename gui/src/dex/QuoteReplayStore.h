#ifndef MONZERO_QUOTE_REPLAY_STORE_H
#define MONZERO_QUOTE_REPLAY_STORE_H

#include <QJsonArray>
#include <QLockFile>
#include <QString>
#include <memory>

class QuoteReplayStore final
{
public:
    explicit QuoteReplayStore(QString path);
    bool ready() const;
    QString error() const;
    bool accept(const QByteArray &makerKey, const QByteArray &nonce,
                const QByteArray &commitment, qint64 quoteExpiry, qint64 now,
                const QByteArray &signedArtifact = {});
    bool contains(const QByteArray &makerKey, const QByteArray &nonce) const;
    QJsonArray records() const;

private:
    friend class SignedQuoteAcceptor;
    bool load();
    bool save(const QJsonArray &next);
    static QString replayId(const QByteArray &makerKey, const QByteArray &nonce);
    QString m_path;
    QString m_error;
    QJsonArray m_records;
    std::unique_ptr<QLockFile> m_lock;
};

#endif
