#ifndef MONZERO_LITECOIN_LIGHT_FUNDING_SETUP_H
#define MONZERO_LITECOIN_LIGHT_FUNDING_SETUP_H
#include "LitecoinLightFundingContext.h"
namespace LitecoinLightFundingSetup {
inline QString retain(QJsonObject context,const QString &wallet) {
#ifdef Q_OS_LINUX
    const auto trade=context.value("tradeId").toString();const QUuid id(trade);
    if(context.size()!=6 || context.value("schema")!="monzero-private-ltc-light-dex-v1" || id.isNull()
        || id.toString(QUuid::WithoutBraces)!=trade || LitecoinJournalProtocol::privateConfig(wallet,4096).isEmpty())return {};
    const auto directory=context.value("metadataDirectory").toString();const QFileInfo info(directory);
    if(!info.isAbsolute() || info.canonicalFilePath()!=directory)return {};
    LitecoinLightFundingContext::Fd root{::open(QFile::encodeName(directory).constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)};
    struct stat st{};if(root.value<0 || ::fstat(root.value,&st)!=0 || st.st_uid!=::geteuid()
        || (st.st_mode&0077)!=0 || ::flock(root.value,LOCK_EX|LOCK_NB)!=0)return {};
    context.insert("walletConfigFile",wallet);const auto bytes=LitecoinLightFundingContext::json(context);
    const auto name=trade.toLatin1()+".ltc-light-wallet.json";
    if(!LitecoinLightFundingContext::retain(root.value,name,bytes))return {};
    return directory+"/"+QString::fromLatin1(name);
#else
    Q_UNUSED(context) Q_UNUSED(wallet)
    return {};
#endif
}
}
#endif
