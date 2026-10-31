// Test-only private pipe adapter. No production signing or peer transport entry point.
#include "../BchPrivateSuccessProtocol.h"
#include "../OfferIdentityStore.h"
#include "../BchAdmissionProtocol.h"
#include "../LitecoinJournalProtocol.h"
#include <QCoreApplication>
#include <cstdio>
namespace {
bool run(const QStringList &args) {
    // Input: raw private frame response. Output: raw approved retention request.
    if (args.size()!=7 || args[1]!="--private-retain-request-fixture-v1") return false;
    using namespace BchPrivateSuccessProtocol;
    struct Input {
        std::array<char,131073> bytes{};
        ~Input() { sodium_memzero(bytes.data(),bytes.size()); }
    } input;
    const auto size=std::fread(input.bytes.data(),1,input.bytes.size(),stdin);
    Material material;
    if (std::ferror(stdin) || !response(input.bytes.data(),int(size),Action::Frame,args[3],args[4],material)) return false;
    QJsonObject config;
    if (!BchAdmissionProtocol::canonicalObject(LitecoinJournalProtocol::privateConfig(args[2]),&config)
        || config.size()!=4 || config.value("schema")!="monzero-private-bch-success-sign-fixture-v1") return false;
    auto password=config.value("passphrase").toString().toUtf8();
    OfferIdentityStore identity(config.value("identityFile").toString(),password,OfferIdentityStore::Mode::OpenExisting);
    sodium_memzero(password.data(),password.size());
    if (!identity.ready() || QString::fromLatin1(identity.identity()->publicKey().toHex())!=config.value("publicKey").toString()) return false;
    auto signature=identity.identity()->signBchSuccess(material.frameView());
    Request encoded;
    const bool valid=request(Action::Retain,args[5],args[6],material.frameView(),signature,encoded);
    sodium_memzero(signature.data(),signature.size());material.clear();
    if (!valid) return false;
    const auto view=encoded.view();
    const bool written=std::fwrite(view.constData(),1,size_t(view.size()),stdout)==size_t(view.size()) && std::fflush(stdout)==0;
    encoded.clear();return written;
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if (run(app.arguments())) return 0;
    std::fputs("BCH private request fixture failed\n",stderr);return 1;
}
