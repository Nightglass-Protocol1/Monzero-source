#ifndef MONZERO_CRYPTO_VERIFIER_CLIENT_H
#define MONZERO_CRYPTO_VERIFIER_CLIENT_H

#include "CryptoVerifierProtocol.h"

#include <QString>
#include <QVariantMap>

class CryptoVerifierClient final
{
public:
    CryptoVerifierClient(QString verifierPath, QString expectedSha256,
                         int timeoutMilliseconds = 30000, int maximumResponseBytes = 4096);
    bool configured() const;
    QVariantMap verify(const CryptoVerifierProtocol::Request &request) const;

private:
    QString m_verifierPath;
    QString m_expectedSha256;
    int m_timeoutMilliseconds;
    int m_maximumResponseBytes;
};

#endif
