#include "../BchLightFundingContext.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <iostream>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);int checks=0;
    const auto check=[&](bool ok){++checks;if(!ok){std::cerr<<"BCH context check failed: "<<checks<<'\n';std::exit(1);}};
    QTemporaryDir dir;check(dir.isValid());
    const QString trade="11111111-2222-4333-8444-555555555555",config(64,'a'),unsignedId(64,'b'),signedId(64,'c');
    const QJsonObject payment{{"address","bchreg:fixture"},{"amountAtomic","10000"},{"feeAtomic","1000"},
        {"minimumChangeAtomic","1000"},{"minimumConfirmations",2}};
    const QJsonObject review{{"signingCommitment",QString(64,'d')},{"changeAddress",QJsonValue::Null}};
    BchLightFundingContext::Retained state;
    const auto access=[&](const QJsonObject &p=QJsonObject{},const QJsonObject &r=QJsonObject{},
        const QString &u=QString{},const QString &s=QString{}) {
        return BchLightFundingContext::access(dir.path(),trade,config,state,p,r,u,s);
    };
    const auto inventory=[&](){QMap<QString,QByteArray> files;for(const auto &name:QDir(dir.path()).entryList(QDir::Files)) {
        QFile file(dir.filePath(name));check(file.open(QIODevice::ReadOnly));files.insert(name,file.readAll());}return files;};
    check(access());check(state.payment.isEmpty());
    check(!access({},review,unsignedId));check(!access({},{},{},signedId));check(inventory().isEmpty());
    check(access(payment));const auto paymentFiles=inventory();
    check(!access({},{},{},signedId));check(inventory()==paymentFiles);
    check(access({},review,unsignedId));check(state.unsignedTxid==unsignedId && state.signedTxid.isEmpty());
    check(access({},{},{},signedId));check(state.unsignedTxid==unsignedId && state.signedTxid==signedId);
    const auto all=inventory();check(all.size()==3);
    check(access());check(state.payment==payment && state.review==review && state.signedTxid==signedId);
    check(access(payment,review,unsignedId,signedId));check(inventory()==all);
    auto changed=payment;changed["amountAtomic"]="10001";
    check(!access(changed,review,unsignedId,signedId));check(state.payment.isEmpty());check(inventory()==all);
    check(!access({},review,signedId));check(!access({},{},{},unsignedId));check(inventory()==all);
    check(!BchLightFundingContext::access(dir.path(),trade,QString(64,'e'),state));check(inventory()==all);
    LitecoinLightFundingContext::Retained ltc;
    check(LitecoinLightFundingContext::access(dir.path(),trade,config,ltc));check(ltc.payment.isEmpty());
    const QString prefix="bch-light-dex-ui-"+trade;
    QFile paymentFile(dir.filePath(prefix+"-payment.json"));check(paymentFile.remove());
    check(!access(payment)); // An orphan review cannot be silently repaired.
    check(paymentFile.open(QIODevice::WriteOnly));check(paymentFile.write(all.value(prefix+"-payment.json"))>0);paymentFile.close();
    check(paymentFile.setPermissions(QFile::ReadOwner|QFile::WriteOwner));check(access());
    QFile signedFile(dir.filePath(prefix+"-signed.json"));check(signedFile.open(QIODevice::WriteOnly|QIODevice::Truncate));
    check(signedFile.write("{}") == 2);signedFile.close();check(!access());
    std::cout<<checks<<" BCH funding context checks passed\n";
}
