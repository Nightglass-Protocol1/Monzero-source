#include "../LitecoinLightJob.h"
#include "../LightWalletSetup.h"
#include "../LightWalletHelperUpgrade.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QFile>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);const auto args=app.arguments();
    if ((args.size()==6 && args[1]=="--review-helper") || (args.size()==7 && args[1]=="--upgrade-helper")) {
        QJsonObject result;
        if (args[1]=="--review-helper") result=LightWalletHelperUpgrade::review(args[2],args[3],args[4],args[5]);
        else {
            const auto launcher=LightWalletHelperUpgrade::apply(args[2],args[3],args[4],args[5],args[6]);
            LitecoinLightJob check(args[2]);
            if(launcher.isEmpty() || !check.configure(launcher,args[4],args[5]))return 3;
            result={{"launcher",launcher},{"wallet",QJsonObject::fromVariantMap(check.status())}};
        }
        if(result.isEmpty())return 3;
        QFile output;if(!output.open(stdout,QIODevice::WriteOnly))return 6;
        output.write(QJsonDocument(result).toJson(QJsonDocument::Compact));return 0;
    }
    if (args.size()==6 && args[1]=="--attach-local") {
        const auto launcher=LightWalletHelperUpgrade::selected(args[3],args[4],args[5]);
        LitecoinLightJob check(args[2]);
        if(launcher.isEmpty() || !check.configure(launcher,args[4],args[5]))return 3;
        QFile output;if(!output.open(stdout,QIODevice::WriteOnly))return 6;
        output.write(QJsonDocument::fromVariant(check.status()).toJson(QJsonDocument::Compact));return 0;
    }
    if (args.size()==10 && args[1]=="--prepare-backup-local") {
        bool ok=false;const int port=args[9].toInt(&ok);if(!ok)return 2;
        const auto launcher=LightWalletSetup::prepare(args[2],args[3],args[4],args[5],args[6],args[8],port,0,args[7]);
        LitecoinLightJob check(args[2]);if(launcher.isEmpty() || !check.configure(launcher,args[5],args[6]))return 3;
        QFile output;if(!output.open(stdout,QIODevice::WriteOnly))return 6;output.write(launcher.toUtf8());return 0;
    }
    if (args.size()==9 && args[1]=="--prepare-local") {
        bool ok=false;const int port=args[8].toInt(&ok);if(!ok)return 2;
        const auto launcher=LightWalletSetup::prepare(args[2],args[3],args[4],args[5],args[6],args[7],port);
        if(launcher.isEmpty())return 3;
        LitecoinLightJob check(args[2]);if(!check.configure(launcher))return 4;
        QFile output;if(!output.open(stdout,QIODevice::WriteOnly))return 6;
        output.write(launcher.toUtf8());return 0;
    }
    if (args.size()!=4 && args.size()!=5) return 2;
    const auto selected=qEnvironmentVariable("MONZERO_TEST_LIGHT_COIN","ltc");
    LitecoinLightJob job(selected);if (!job.configure(args[1])) return 3;
    QEventLoop loop;QObject::connect(&job,&LitecoinLightJob::changed,&loop,[&]{if (!job.status().value("busy").toBool()) loop.quit();});
    bool budgetOk=false;const auto budget=args[3].toUInt(&budgetOk);if (!budgetOk) return 4;
    const auto recovery=args.size()==5?QJsonDocument::fromJson(args[4].toUtf8()).object():QJsonObject{};
    if (args[2]=="wallet-backup-preview" || args[2]=="wallet-backup-import") {
        QFile input;if(!input.open(stdin,QIODevice::ReadOnly))return 8;auto password=input.read(1025);if(password.size()>1024)return 8;
        if(!job.previewBackupImport(QString::fromUtf8(password)))return 5;
        QTimer::singleShot(150000,&job,&LitecoinLightJob::cancel);if(job.status().value("busy").toBool())loop.exec();
        if(job.status().value("state")!="backup-import-reviewed")return 9;
        if(args[2]=="wallet-backup-import") {
            const auto review=job.status().value("backupImportReview").toMap();
            const auto digest=recovery.contains("approvedReview")?recovery.value("approvedReview").toString():review.value("reviewDigest").toString();
            if(!job.importBackup(digest,QString::fromUtf8(password),recovery.value("approved")!=false))return 5;
            if(job.status().value("busy").toBool())loop.exec();
        }
        password.fill(0);password.clear();
        QFile output;if(!output.open(stdout,QIODevice::WriteOnly))return 6;
        output.write(QJsonDocument::fromVariant(job.status()).toJson(QJsonDocument::Compact));return 0;
    }
    if (args[2]=="wallet-import-activate") {
        QFile input;if(!input.open(stdin,QIODevice::ReadOnly))return 8;auto bytes=input.read(8193);if(bytes.size()>8192)return 8;
        auto credentials=QJsonDocument::fromJson(bytes).object();bytes.fill(0);bytes.clear();
        const auto requests=job.status().value("importRecoveryRequests").toList();if(requests.size()!=1)return 9;
        if(!job.recoverImportedWallet(requests[0].toMap().value("reviewDigest").toString(),credentials.value("recoveryPassword").toString(),true))return 5;
        QTimer::singleShot(150000,&job,&LitecoinLightJob::cancel);if(job.status().value("busy").toBool())loop.exec();
        if(job.status().value("state")!="wallet-import-recovered")return 9;
        const auto digest=recovery.contains("approvedActivation")?recovery.value("approvedActivation").toString():job.status().value("importActivationDigest").toString();
        const bool started=job.activateImportedWallet(digest,credentials.value("activationPassword").toString(),recovery.value("approved")!=false);
        credentials={};if(!started)return 5;if(job.status().value("busy").toBool())loop.exec();
        if(job.status().value("state")!="ready" || job.status().value("walletImportPending").toBool()
            || !job.status().value("importActivated").toBool() || !job.status().value("walletReceiveAvailable").toBool())return 9;
        QFile output;if(!output.open(stdout,QIODevice::WriteOnly))return 6;
        output.write(QJsonDocument::fromVariant(job.status()).toJson(QJsonDocument::Compact));return 0;
    }
    if (args[2]=="wallet-import-recover-flow" || args[2]=="wallet-import-resume") {
        QFile input;if(!input.open(stdin,QIODevice::ReadOnly))return 8;auto bytes=input.read(1025);if(bytes.isEmpty() || bytes.size()>1024)return 8;
        QString digest;
        if(args[2]=="wallet-import-recover-flow") {
            const auto known=recovery.value("knownMinimum").toArray();if(known.size()!=2)return 4;
            if(!job.discoverImportedWallet(known[0].toInt(),known[1].toInt(),2,10,50,budget,true))return 5;
            QTimer::singleShot(150000,&job,&LitecoinLightJob::cancel);if(job.status().value("busy").toBool())loop.exec();
            if(job.status().value("state")!="wallet-import-discovered")return 9;
            digest=job.status().value("importDiscovery").toMap().value("reviewDigest").toString();
        } else {
            const auto requests=job.status().value("importRecoveryRequests").toList();if(requests.size()!=1)return 9;
            if(!job.status().value("importRecovery").toMap().isEmpty() || requests[0].toMap().value("backupVerified").toBool())return 9;
            digest=requests[0].toMap().value("reviewDigest").toString();
        }
        if(recovery.contains("approvedReview"))digest=recovery.value("approvedReview").toString();
        const bool started=job.recoverImportedWallet(digest,QString::fromUtf8(bytes),recovery.value("approved")!=false);bytes.fill(0);bytes.clear();
        if(!started)return 5;if(job.status().value("busy").toBool())loop.exec();
        if(job.status().value("state")!="wallet-import-recovered" || job.status().value("walletReceiveAvailable").toBool()
            || job.status().value("walletSignAvailable").toBool())return 9;
        QFile output;if(!output.open(stdout,QIODevice::WriteOnly))return 6;
        output.write(QJsonDocument::fromVariant(job.status()).toJson(QJsonDocument::Compact));return 0;
    }
    if (args[2]=="wallet-import-discover") {
        const auto known=recovery.value("knownMinimum").toArray();if(known.size()!=2)return 4;
        if(!job.discoverImportedWallet(known[0].toInt(),known[1].toInt(),recovery.value("gap").toInt(2),
            recovery.value("addresses").toInt(10),recovery.value("entries").toInt(50),budget,recovery.value("approved")!=false))return 5;
        QTimer::singleShot(150000,&job,&LitecoinLightJob::cancel);if(job.status().value("busy").toBool())loop.exec();
        if(job.status().value("state")!="wallet-import-discovered" || job.status().value("walletReceiveAvailable").toBool()
            || job.status().value("walletSignAvailable").toBool())return 9;
        QFile output;if(!output.open(stdout,QIODevice::WriteOnly))return 6;
        output.write(QJsonDocument::fromVariant(job.status()).toJson(QJsonDocument::Compact));return 0;
    }
    if (args[2]=="wallet-import-preview" || args[2]=="wallet-import-flow") {
        QFile input;if(!input.open(stdin,QIODevice::ReadOnly))return 8;auto bytes=input.read(8193);if(bytes.size()>8192)return 8;
        auto credentials=QJsonDocument::fromJson(bytes).object();bytes.fill(0);bytes.clear();
        const auto digest=job.status().value("creationDigest").toString();
        if(!job.previewMnemonicImport(digest,credentials.value("phrase").toString(),credentials.value("passphrase").toString()))return 5;
        QTimer::singleShot(15000,&job,&LitecoinLightJob::cancel);if(job.status().value("busy").toBool())loop.exec();
        if(job.status().value("state")!="wallet-import-reviewed")return 9;
        if(args[2]=="wallet-import-flow") {
            const auto review=job.status().value("importReview").toMap();
            const auto approved=recovery.contains("approvedReview")?recovery.value("approvedReview").toString():review.value("reviewDigest").toString();
            if(!job.importMnemonic(approved,credentials.value(credentials.contains("importPhrase")?"importPhrase":"phrase").toString(),credentials.value("passphrase").toString(),
                credentials.value("password").toString(),credentials.value("confirmation").toString(),recovery.value("approved")!=false))return 5;
            if(job.status().value("busy").toBool())loop.exec();
            if(job.status().value("state")!="wallet-import-pending" || job.status().value("walletCreateAvailable").toBool()
                || job.status().value("walletReceiveAvailable").toBool() || job.status().value("walletSignAvailable").toBool())return 9;
        }
        credentials={};QFile output;if(!output.open(stdout,QIODevice::WriteOnly))return 6;
        output.write(QJsonDocument::fromVariant(job.status()).toJson(QJsonDocument::Compact));return 0;
    }
    if (args[2]=="wallet-key-restore" || args[2]=="wallet-mnemonic-restore") {
        QFile input;if (!input.open(stdin,QIODevice::ReadOnly)) return 8;
        auto wire=input.read(8193);if (wire.size()>8192) return 8;
        auto credentials=QJsonDocument::fromJson(wire).object();wire.fill(0);wire.clear();
        const auto approvedDigest=recovery.contains("approvedConfigDigest")?recovery.value("approvedConfigDigest").toString():job.status().value("configurationDigest").toString();
        const bool started=args[2]=="wallet-mnemonic-restore"
            ?job.restoreMnemonicRecovery(approvedDigest,credentials.value("phrase").toString(),credentials.value("passphrase").toString(),credentials.value("password").toString(),credentials.value("confirmation").toString(),recovery.value("approved")!=false)
            :job.restoreRecovery(approvedDigest,credentials.value("seed").toString(),credentials.value("password").toString(),credentials.value("confirmation").toString(),recovery.value("approved")!=false);
        credentials={};if (!started) return 5;
        QTimer::singleShot(15000,&job,&LitecoinLightJob::cancel);if (job.status().value("busy").toBool()) loop.exec();
        if (job.status().value("state")!="wallet-key-restored") return 9;
        QFile output;if (!output.open(stdout,QIODevice::WriteOnly)) return 6;
        output.write(QJsonDocument::fromVariant(job.status()).toJson(QJsonDocument::Compact));return 0;
    }
    QByteArray password;
    if (args[2].startsWith("wallet-recovery") || args[2]=="wallet-sign" || args[2]=="wallet-sign-direct" || args[2].startsWith("wallet-backup-") || args[2].startsWith("wallet-restore") || args[2]=="wallet-create" || args[2].startsWith("wallet-create-")) {QFile input;if (!input.open(stdin,QIODevice::ReadOnly)) return 8;password=input.read(1025);}
    const auto commitment=recovery.value("approvedCommitment").isString()?recovery.value("approvedCommitment").toString():recovery.value("retainedReview").toObject().value("signingCommitment").toString();
    const bool sending=args[2]=="wallet-broadcast" || args[2]=="wallet-broadcast-cancel";
    const auto sendTxid=recovery.value("approvedTxid").isString()?recovery.value("approvedTxid").toString():recovery.value("savedReview").toObject().value("txid").toString();
    const auto sendEndpoint=[&] {return recovery.value("approvedEndpoint").isObject()?recovery.value("approvedEndpoint").toObject():QJsonObject::fromVariantMap(job.status().value("wallet").toMap()).value("broadcastEndpoint").toObject();};
    const bool restoring=args[2]=="wallet-restore" || args[2]=="wallet-restore-cancel";
    const auto restoreDirectory=[&] {return recovery.value("approvedDirectory").isString()?recovery.value("approvedDirectory").toString():recovery.value("directory").toString();};
    const auto restoreDigest=[&] {return recovery.value("approvedDigest").isString()?recovery.value("approvedDigest").toString():QJsonObject::fromVariantMap(job.status().value("wallet").toMap()).value("backupDigest").toString();};
    const bool creating=args[2]=="wallet-create" || args[2]=="wallet-create-cancel" || args[2]=="wallet-create-open" || args[2]=="wallet-create-receive";
    const bool createdReview=args[2]=="wallet-created-review" || args[2]=="wallet-created-open" || args[2]=="wallet-created-receive";
    const auto creationDigest=recovery.value("approvedConfigDigest").isString()?recovery.value("approvedConfigDigest").toString():job.status().value("creationDigest").toString();
    const bool recovering=args[2].startsWith("wallet-recovery");
    const bool started=recovering?job.revealRecovery(recovery.contains("approvedConfigDigest")?recovery.value("approvedConfigDigest").toString():job.status().value("configurationDigest").toString(),QString::fromUtf8(password),recovery.value("approved")!=false):creating?job.createWallet(creationDigest,QString::fromUtf8(password.constData(),password.size()),QString::fromUtf8(password.constData(),password.size())+(recovery.value("confirmationMatches")==false?" mismatch":"")):createdReview?job.reviewCreatedWallet():args[2]=="wallet-created-open-direct"?job.openCreatedWallet(recovery.value("approvedDigest").toString()):restoring?job.backupWallet(recovery.value("directory").toString(),QString::fromUtf8(password.constData(),password.size()),false):args[2]=="wallet-restore-direct"?job.restoreWallet(restoreDirectory(),restoreDigest(),QString::fromUtf8(password.constData(),password.size())):args[2].startsWith("wallet-backup-")?job.backupWallet(recovery.value("directory").toString(),QString::fromUtf8(password.constData(),password.size()),args[2]=="wallet-backup-save"):(args[2]=="wallet-reconcile" || args[2]=="wallet-reconcile-cancel") && args.size()==5?job.reconcileWallet(recovery.value("requestId").toString(),recovery.value("payment").toObject(),recovery.value("savedReview").toObject(),budget,10):args[2]=="wallet-broadcast-direct"?job.broadcastReviewedWallet(commitment,sendTxid,sendEndpoint()):(sending || args[2]=="wallet-broadcast-status") && args.size()==5?job.checkBroadcastWallet(recovery.value("requestId").toString(),recovery.value("payment").toObject(),recovery.value("savedReview").toObject(),1,budget,10):args[2]=="wallet-signed" && args.size()==5?job.recoverSignedWallet(recovery.value("requestId").toString(),recovery.value("payment").toObject(),recovery.value("savedReview").toObject()):args[2]=="wallet-sign-direct"?job.signReviewedWallet(commitment,QString::fromUtf8(password.constData(),password.size())):(args[2]=="wallet-sign" || args[2]=="wallet-reopen") && args.size()==5?job.reopenWallet(recovery.value("requestId").toString(),recovery.value("payment").toObject(),recovery.value("retainedReview").toObject(),1,budget,10):args[2]=="wallet-estimate" && args.size()==5?job.estimateWallet(QJsonDocument::fromJson(args[4].toUtf8()).object(),1,budget,10):args[2]=="wallet-requests"?job.loadPaymentRequests():args[2]=="wallet-prepare-current" && args.size()==5?job.prepareCurrentWallet(QJsonDocument::fromJson(args[4].toUtf8()).object(),1,budget,10):args[2]=="wallet-prepare" && args.size()==5?job.prepareWallet(recovery.contains("payment")?recovery.value("requestId").toString():QStringLiteral("11111111-2222-4333-8444-555555555555"),recovery.contains("payment")?recovery.value("payment").toObject():recovery,1,budget,10):args[2]=="wallet-current"?job.receiveCurrentWallet():args[2]=="wallet-receive"?job.receiveWallet("11111111-2222-4333-8444-555555555555"):args[2]=="wallet-init"?job.initializeWallet():args[2].startsWith("wallet-refresh")?job.refreshWallet(1,budget,10):args[2]=="sync"?job.synchronize(budget):job.refresh();if (!started) return 5;
    if (args[2]=="cancel" || args[2]=="wallet-reconcile-cancel" || args[2]=="wallet-backup-cancel" || args[2]=="wallet-create-cancel") QTimer::singleShot(50,&job,&LitecoinLightJob::cancel);
    if (args[2].startsWith("wallet-backup-") || creating) {password.fill(0);password.clear();}
    QTimer::singleShot(args[2]=="wallet-recovery-timeout"?45000:15000,&job,&LitecoinLightJob::cancel);if (job.status().value("busy").toBool()) loop.exec();
    if (args[2]=="wallet-sign") {
        const bool accepted=job.signReviewedWallet(commitment,QString::fromUtf8(password.constData(),password.size()));password.fill(0);password.clear();
        if (!accepted) return 5;if (job.status().value("busy").toBool()) loop.exec();
    }
    if (args[2]=="wallet-create-open" || args[2]=="wallet-create-receive" || args[2]=="wallet-created-open" || args[2]=="wallet-created-receive") {
        const auto digest=recovery.value("approvedDigest").isString()?recovery.value("approvedDigest").toString():QJsonObject::fromVariantMap(job.status().value("wallet").toMap()).value("configurationDigest").toString();
        if (!job.openCreatedWallet(digest)) return 5;
        if (args[2].endsWith("-receive")) {
            if (!job.receiveCurrentWallet()) return 5;
            if (job.status().value("busy").toBool()) loop.exec();
        }
    }
    if (restoring) {
        const bool accepted=job.restoreWallet(restoreDirectory(),restoreDigest(),QString::fromUtf8(password.constData(),password.size()));
        password.fill(0);password.clear();if (!accepted) return 5;
        if (args[2]=="wallet-restore-cancel") QTimer::singleShot(50,&job,&LitecoinLightJob::cancel);
        if (job.status().value("busy").toBool()) loop.exec();
    }
    if (sending) {
        if (!job.broadcastReviewedWallet(commitment,sendTxid,sendEndpoint())) return 5;
        if (args[2]=="wallet-broadcast-cancel") QTimer::singleShot(50,&job,&LitecoinLightJob::cancel);
        if (job.status().value("busy").toBool()) loop.exec();
    }
    if (recovering) {
        password.fill(0);password.clear();
        if (!job.status().value("recoveryVisible").toBool()) return 9;
        if (job.status().value("recoveryEncoding")=="bip39-english-v1") {
            if (job.recoverySeed().split(' ').size()!=24 || !job.recoveryPassphrase().isEmpty()) return 9;
        } else if (job.recoverySeed().size()!=64) return 9;
        if (job.status().contains("seedHex") || job.status().contains("recoverySeed")) return 10;
        if (args[2]=="wallet-recovery-timeout") {
            QEventLoop expiry;
            QTimer::singleShot(25000,&expiry,&QEventLoop::quit);expiry.exec();
            if (!job.status().value("recoveryVisible").toBool() || job.recoverySeed().isEmpty()) return 12;
            QTimer::singleShot(7000,&expiry,&QEventLoop::quit);expiry.exec();
        } else job.hideRecovery();
        if (!job.recoverySeed().isEmpty() || !job.recoveryPassphrase().isEmpty() || job.status().value("recoveryVisible").toBool()) return 11;
    }
    auto result=QJsonDocument::fromVariant(job.status());
    if (args[2]=="repeat" || args[2]=="wallet-refresh-repeat") {
        const auto before=result.object();if (!(args[2]=="repeat"?job.refresh():job.refreshWallet(1,budget,10))) return 7;loop.exec();
        result=QJsonDocument(QJsonObject{{"before",before},{"after",QJsonObject::fromVariantMap(job.status())}});
    }
    QFile output;if (!output.open(stdout,QIODevice::WriteOnly)) return 6;
    output.write(result.toJson(QJsonDocument::Compact));return 0;
}
