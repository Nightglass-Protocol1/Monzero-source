#ifndef MONZERO_OFFER_IDENTITY_STORE_H
#define MONZERO_OFFER_IDENTITY_STORE_H

#include "OfferIdentity.h"
#include <QLockFile>
#include <QString>

class OfferIdentityStore final
{
public:
    enum class Mode { CreateNew, OpenExisting };
    OfferIdentityStore(const QString &path, const QByteArray &passphrase, Mode mode);
    bool ready() const { return bool(m_identity); }
    QString error() const { return m_error; }
    const OfferIdentity *identity() const { return m_identity.get(); }
private:
    std::unique_ptr<QLockFile> m_lock;
    std::unique_ptr<OfferIdentity> m_identity;
    QString m_error;
};
#endif
