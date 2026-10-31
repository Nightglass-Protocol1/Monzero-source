#ifndef MONZERO_LITECOIN_APP_CONFIG_H
#define MONZERO_LITECOIN_APP_CONFIG_H
#include <QString>
#include <QMap>
#include <QByteArray>
#include <QVariantMap>

// One role's immutable phase configuration; loading never starts a process.
struct LitecoinAppConfig {
    QString journal,peer,funding,payment,reservation;
    QMap<QString,QByteArray> sources;
    QString digest() const;
    static QVariantMap review(const QString &path);
    static bool load(const QString &path,LitecoinAppConfig &result);
};
#endif
