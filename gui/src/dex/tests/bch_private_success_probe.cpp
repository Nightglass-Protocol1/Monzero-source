// Disposable-chain test adapter; not a production signing or peer-release API.
#include "../BchPrivateSuccessProtocol.h"
#include "../OfferIdentityStore.h"
#include "../BchAdmissionProtocol.h"
#include "../LitecoinJournalProtocol.h"
#include <QCoreApplication>
#include <cstdio>
namespace {
bool run(const QStringList &args) {
    if (args.size()!=6 || args[1]!="--private-success-fixture-v1") return false;
    using namespace BchPrivateSuccessProtocol;
    Action action;
    if (args[2]=="frame") action=Action::Frame;
    else if (args[2]=="retain") action=Action::Retain;
    else if (args[2]=="release") action=Action::Release;
    else return false;
    struct Input {
        std::array<char,131073> bytes{};
        ~Input() { sodium_memzero(bytes.data(),bytes.size()); }
    } input;
    const auto size=std::fread(input.bytes.data(),1,input.bytes.size(),stdin);
    Material material;
    if (std::ferror(stdin) || !response(input.bytes.data(),int(size),action,args[4],args[5],material)) return false;
    QJsonObject config;
    if (!BchAdmissionProtocol::canonicalObject(LitecoinJournalProtocol::privateConfig(args[3]),&config)
        || config.size()!=4 || config.value("schema")!="monzero-private-bch-success-sign-fixture-v1") return false;
    auto password=config.value("passphrase").toString().toUtf8();
    OfferIdentityStore identity(config.value("identityFile").toString(),password,OfferIdentityStore::Mode::OpenExisting);
    sodium_memzero(password.data(),password.size());
    if (!identity.ready() || QString::fromLatin1(identity.identity()->publicKey().toHex())!=config.value("publicKey").toString()) return false;
    QJsonObject result{{"schema","monzero-bch-private-success-fixture-response-v1"},
        {"requestId",args[4]},{"tradeId",args[5]}};
    if (action!=Action::Retain) {
        const auto signature=identity.identity()->signBchSuccess(material.frameView());
        if (signature.size()!=64) return false;
        if (action==Action::Release && (material.signatureView().size()!=64
            || sodium_memcmp(signature.constData(),material.signatureView().constData(),64)!=0)) return false;
        if (action==Action::Frame) result.insert("signatureHex",QString::fromLatin1(signature.toHex()));
    }
    material.clear();
    const auto encoded=QJsonDocument(result).toJson(QJsonDocument::Compact);
    return std::fwrite(encoded.constData(),1,size_t(encoded.size()),stdout)==size_t(encoded.size()) && std::fflush(stdout)==0;
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if (run(app.arguments())) return 0;
    std::fputs("BCH private success fixture failed\n",stderr);return 1;
}
