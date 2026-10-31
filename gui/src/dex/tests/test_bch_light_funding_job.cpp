#include "../BchLightFundingJob.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QEventLoop>
#include <iostream>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);int checks=0;
    const auto check=[&](bool ok){++checks;if(!ok){std::cerr<<"BCH job check failed: "<<checks<<'\n';std::exit(1);}};
    QTemporaryDir root;check(root.isValid());
    const auto write=[&](const QString &path,const QByteArray &bytes,bool executable=false){QFile file(path);
        check(file.open(QIODevice::WriteOnly|QIODevice::Truncate));check(file.write(bytes)==bytes.size());
        check(file.setPermissions(QFile::ReadOwner|QFile::WriteOwner|(executable?QFile::ExeOwner:QFile::Permissions{})));};
    const auto json=[](const QJsonObject &value){return QJsonDocument(value).toJson(QJsonDocument::Compact);};
    const QString trade="11111111-2222-4333-8444-555555555555",quote(64,'a'),commitment(64,'b'),unsignedId(64,'c'),signedId(64,'d');
    const auto path=root.filePath("dex.json"),walletPath=root.filePath("wallet.json"),quotePath=root.filePath("quote.json"),helper=root.filePath("helper");
    write(walletPath,json({{"schema","monzero-private-bch-light-v1"},{"network","regtest"},{"wallet",QJsonObject{}},
        {"endpoint",QJsonObject{{"host","localhost"},{"port",12345}}}}));
    write(quotePath,json({{"schema","monzero-bch-quote-record-v1"},{"commitment",quote},
        {"terms",QJsonObject{{"swapId",trade},{"bchAtoms","10000"},{"bchFundingFeeMaxAtoms","1000"},{"bchMinimumOutputAtoms","1000"}}}}));
    write(path,json({{"schema","monzero-private-bch-light-dex-v1"},{"tradeId",trade},{"quoteCommitment",quote},
        {"quoteFile",quotePath},{"walletConfigFile",walletPath},{"admittedAt",1},{"identityPins",QJsonArray{}},
        {"policy",QJsonObject{{"bch_network","bch-regtest"},{"bch_confirmations",2}}},
        {"metadataDirectory",root.path()},{"snapshotDirectory",root.path()},{"negotiationPins",QJsonObject{}}}));
    const QByteArray script="#!/usr/bin/python3\nimport json,sys\nr=json.load(sys.stdin)\nc=json.load(open(sys.argv[2]))\nx=json.load(open(sys.argv[2]+'.reply'))\nopen(sys.argv[2]+'.invoked','w').write('yes')\nm=sys.argv[1][len('--bch-dex-funding-'):-len('-stdio-v1')]\nprint(json.dumps({'schema':'monzero-bch-light-dex-funding-'+m+'-response-v1','requestId':r['requestId'],'configurationDigest':r['approvedConfigDigest'],'tradeId':c['tradeId'],'executable':False,'result':x}))\n";
    write(helper,script,true);const auto pin=QString::fromLatin1(QCryptographicHash::hash(script,QCryptographicHash::Sha256).toHex());
    const QJsonObject payment{{"address","bchreg:fixture"},{"amountAtomic","10000"},{"feeAtomic","1000"},{"minimumChangeAtomic","1000"},{"minimumConfirmations",2}};
    const QJsonObject retained{{"signingCommitment",commitment},{"changeAddress",QJsonValue::Null}};
    QJsonObject result{{"watchOnly",true},{"spendable",false},{"prepared",true},{"signed",false},
        {"paymentAddress","bchreg:fixture"},{"paymentAtomic","10000"},{"feeAtomic","1000"},{"changeAddress",QJsonValue::Null},
        {"changeAtomic","0"},{"inputCount",1},{"unsignedTxid",unsignedId},{"signingCommitment",commitment},
        {"fundingVerified",true},{"sourceTransactions",1},{"operation","review"},{"tradeId",trade},{"selectedHeight",2},{"broadcastAuthorized",false}};
    const auto execute=[&](const QString &op,const QJsonObject &reply,const QString &approval,bool expectedStart,bool expectedSuccess){
        write(path+".reply",json(reply));QFile::remove(path+".invoked");
        BchLightFundingJob job;check(job.configure(path,helper,pin));
        QJsonObject request{{"schema","monzero-bch-light-dex-funding-"+op+"-request-v1"},{"requestId",QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {"approvedConfigDigest",job.status().value("configurationDigest").toString()},{"maximumDownloadHeaders",100},
            {"walletScan",QJsonObject{{"gap",2},{"addressesPerBranch",3},{"historyEntriesPerBranch",10}}},{"payment",payment},{"retainedReview",retained}};
        if(!approval.isEmpty())request.insert("approval",approval);
        QEventLoop loop;QObject::connect(&job,&BchLightFundingJob::changed,&loop,[&]{if(!job.status().value("busy").toBool())loop.quit();});
        QTimer timer;timer.setSingleShot(true);QObject::connect(&timer,&QTimer::timeout,&job,&BchLightFundingJob::cancel);timer.start(10000);
        const bool started=job.run(op,request,op=="sign"?QByteArray("fixture password"):QByteArray{});check(started==expectedStart);
        if(job.status().value("busy").toBool())loop.exec();
        check((job.status().value("state")=="complete")==expectedSuccess);
        if(!expectedStart)check(!QFileInfo::exists(path+".invoked"));
        if(started && !expectedSuccess)check(job.response().isEmpty());
    };
    execute("review",result,{},true,true);
    auto injected=result;injected["review"]=QJsonObject{{"commitment",signedId},{"details",QJsonObject{{"tradeId",trade},{"quoteCommitment",quote}}}};
    execute("review",injected,{},true,false);
    auto signedResult=result;signedResult["watchOnly"]=false;signedResult["signed"]=true;signedResult["txid"]=signedId;
    signedResult["tradeFundingRetained"]=true;signedResult["broadcast"]=false;signedResult["operation"]="sign";
    execute("sign",signedResult,signedId,false,false);
    execute("sign",signedResult,commitment,true,true);
    signedResult["signingCommitment"]=signedId;execute("sign",signedResult,commitment,true,false);
    std::cout<<checks<<" BCH job approval checks passed\n";
}
