// Test-only signer for disposable paired fixtures. Not a production release API.
#include "../OfferIdentityStore.h"
#include "../BchAdmissionProtocol.h"
#include "../LitecoinJournalProtocol.h"
#include <QCoreApplication>
#include <QFile>
#include <QUuid>
#include <cstdio>
namespace {
bool run(const QStringList &args) {
    if (args.size()!=3 || args[1]!="--success-sign-fixture-v1") return false;
    QFile input;if (!input.open(stdin,QIODevice::ReadOnly)) return false;
    auto bytes=input.read(20001);QJsonObject request,config;
    if (bytes.size()>20000 || !BchAdmissionProtocol::canonicalObject(bytes,&request) || request.size()!=3
        || request.value("schema")!="monzero-bch-success-sign-fixture-request-v1"
        || QUuid(request.value("requestId").toString()).isNull()
        || !BchAdmissionProtocol::canonicalObject(LitecoinJournalProtocol::privateConfig(args[2]),&config)
        || config.size()!=4 || config.value("schema")!="monzero-private-bch-success-sign-fixture-v1") return false;
    const auto encoded=request.value("frameHex").toString();
    auto frame=QByteArray::fromHex(encoded.toLatin1());
    if (frame.isEmpty() || frame.size()>8192 || QString::fromLatin1(frame.toHex())!=encoded) return false;
    auto password=config.value("passphrase").toString().toUtf8();
    OfferIdentityStore identity(config.value("identityFile").toString(),password,OfferIdentityStore::Mode::OpenExisting);
    sodium_memzero(password.data(),password.size());
    if (!identity.ready() || QString::fromLatin1(identity.identity()->publicKey().toHex())!=config.value("publicKey").toString()) {
        sodium_memzero(frame.data(),frame.size());return false;
    }
    const auto signature=identity.identity()->signBchSuccess(frame);
    sodium_memzero(frame.data(),frame.size());sodium_memzero(bytes.data(),bytes.size());
    if (signature.size()!=64) return false;
    const QJsonObject response{{"schema","monzero-bch-success-sign-fixture-response-v1"},
        {"requestId",request.value("requestId")},{"signatureHex",QString::fromLatin1(signature.toHex())}};
    const auto outputBytes=QJsonDocument(response).toJson(QJsonDocument::Compact);
    QFile output;if (!output.open(stdout,QIODevice::WriteOnly)) return false;
    return output.write(outputBytes)==outputBytes.size() && output.flush();
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);if (run(app.arguments())) return 0;
    std::fputs("BCH identity fixture failed\n",stderr);return 1;
}
