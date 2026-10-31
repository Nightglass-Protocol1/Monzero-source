#include "BchRefundWalletTestFixture.h"
class BchRefundWalletProtocolTests:public QObject {
    Q_OBJECT
    bool setup(Context &c) {
        if (!c.create(true)) return false;
        auto policy=c.config.value("policy").toObject();
        policy["xmz_confirmations"]=c.record.value("terms").toObject().value("xmzConfirmations").toString().toInt();
        c.config["policy"]=policy;return true;
    }
    QJsonObject value(const Context &c) {return refundWalletStatusValue(c);}
    bool valid(const QJsonObject &v,const Context &c) {return BchRefundWalletProtocol::status(v,c.config,c.record,QString(64,'b'),c.deployment);}
    QJsonObject backend() {return {{"schema","monzero-local-wallet-endpoint-v1"},{"host","127.0.0.1"},{"daemonPort",18081},{"walletPort",18083}};}
    QJsonObject seal(QJsonObject details) {
        QByteArray bytes="monzero-bch-native-refund-wallet-review-v1";bytes.append('\0');bytes+=QJsonDocument(details).toJson(QJsonDocument::Compact);
        return {{"details",details},{"commitment",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())}};
    }
    QJsonObject approval(const Context &c,QString action,QString state) {
        auto d=value(c);d.remove("chainObserved");d.remove("executable");d["refund"]=d.take("refundHint");
        d["schema"]="monzero-bch-native-refund-wallet-review-v1";d["action"]=action;d["state"]=state;
        if (state!="not-attempted") d["walletIntent"]=pinBytes(2);
        d["restoreHeight"]=0;d["walletBackend"]=backend();
        d["minimumBchConfirmations"]=c.config.value("policy").toObject().value("bch_confirmations");
        d["minimumXmzConfirmations"]=c.config.value("policy").toObject().value("xmz_confirmations");return seal(d);
    }
    bool approved(const QJsonObject &v,const Context &c,QString action="restore") {
        return BchRefundWalletProtocol::review(v,c.config,c.record,QString(64,'b'),c.deployment,action,backend());
    }
private slots:
    void submissionActionsAndHistory() {
        Context c;QVERIFY(setup(c));const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        for (const auto action:{"restore","open","check"}) {
            for (const auto state:{"not-attempted","attempted","ready"}) {
                const auto reviewed=approval(c,action,state);
                auto wallet=value(c);wallet["state"]="ready";wallet["walletIntent"]=pinBytes(2);
                const QJsonObject base{{"schema","monzero-bch-native-refund-wallet-submit-response-v1"},
                    {"requestId",id},{"tradeId",c.config.value("tradeId")},{"action",action},
                    {"approvedCommitment",reviewed.value("commitment")},{"wallet",wallet},
                    {"executable",false},{"chainObserved",false}};
                auto valid=[&](const QJsonObject &v){return BchRefundWalletProtocol::submissionResponse(v,id,reviewed,
                    c.config,c.record,QString(64,'b'),c.deployment,action,backend());};
                const bool allowed=QString(action)=="restore" ? QString(state)=="not-attempted" : QString(state)!="not-attempted";
                QCOMPARE(valid(base),allowed);
                if (!allowed) continue;
                for (const auto field:{"schema","requestId","tradeId","action","approvedCommitment"}) {
                    auto changed=base;changed[field]="wrong";QVERIFY(!valid(changed));
                }
                for (const auto field:{"chainObserved","executable"}) {
                    auto changed=base;changed[field]=true;QVERIFY(!valid(changed));
                    changed[field]=0;QVERIFY(!valid(changed));
                }
                for (const auto field:{"privateKey","password","spendKey","observedAt"}) {
                    auto changed=base;changed[field]="unexpected";QVERIFY(!valid(changed));
                }
                for (const auto &key:base.keys()) {auto changed=base;changed.remove(key);QVERIFY(!valid(changed));}
                for (const auto badState:{"not-attempted","attempted","confirmed"}) {
                    auto changed=base;auto bad=wallet;bad["state"]=badState;changed["wallet"]=bad;QVERIFY(!valid(changed));
                }
                auto changed=base;auto bad=wallet;bad["walletIntent"]=pinBytes(3);changed["wallet"]=bad;
                QCOMPARE(valid(changed),QString(action)=="restore");
                bad["walletIntent"]=QJsonValue::Null;changed["wallet"]=bad;QVERIFY(!valid(changed));
                bad=wallet;bad["password"]="unexpected";changed["wallet"]=bad;QVERIFY(!valid(changed));
                QVERIFY(!BchRefundWalletProtocol::submissionResponse(base,QString(),reviewed,c.config,c.record,
                    QString(64,'b'),c.deployment,action,backend()));
            }
        }
    }
    void submissionRejectsIndependentlyValidDifferentWallet() {
        Context c;QVERIFY(setup(c));const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto reviewed=approval(c,"restore","not-attempted");
        for (const auto field:{"address","nativePayment","admission"}) {
            auto wallet=value(c);wallet["state"]="ready";wallet["walletIntent"]=pinBytes(2);
            wallet[field]=QString(field)=="address" ? QJsonValue(QString(95,'5'))
                :QString(field)=="nativePayment" ? QJsonValue(QString(64,'c')) : QJsonValue(pinBytes(5));
            const QJsonObject binding{{"quote",wallet.value("quote")},{"admission",wallet.value("admission")},
                {"wallet",wallet.value("walletFile")},{"address",wallet.value("address")},
                {"nativePayment",wallet.value("nativePayment")},{"refund",wallet.value("refundHint")}};
            QByteArray bytes="monzero-bch-native-refund-wallet-intent-v1";bytes.append('\0');
            bytes+=QJsonDocument(binding).toJson(QJsonDocument::Compact);
            QJsonArray pin;for (const auto byte:QCryptographicHash::hash(bytes,QCryptographicHash::Sha256)) pin.append(int(static_cast<unsigned char>(byte)));
            wallet["walletBinding"]=pin;QVERIFY(valid(wallet,c));
            const QJsonObject response{{"schema","monzero-bch-native-refund-wallet-submit-response-v1"},
                {"requestId",id},{"tradeId",c.config.value("tradeId")},{"action","restore"},
                {"approvedCommitment",reviewed.value("commitment")},{"wallet",wallet},{"executable",false},{"chainObserved",false}};
            QVERIFY(!BchRefundWalletProtocol::submissionResponse(response,id,reviewed,c.config,c.record,
                QString(64,'b'),c.deployment,"restore",backend()));
        }
    }
    void reviewResponseFreshnessAndCorrelation() {
        Context c;QVERIFY(setup(c));const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QJsonObject base{{"schema","monzero-bch-native-refund-wallet-review-response-v1"},{"requestId",id},
            {"tradeId",c.config.value("tradeId")},{"executable",false},{"chainObserved",true},{"observedAt",1001},
            {"review",approval(c,"restore","not-attempted")}};
        auto valid=[&](const QJsonObject &v,quint64 start,quint64 now){return BchRefundWalletProtocol::reviewResponse(v,id,start,now,c.config,c.record,QString(64,'b'),c.deployment,"restore",backend());};
        QVERIFY(valid(base,1000,1001));QVERIFY(valid(base,1000,1031));QVERIFY(!valid(base,1000,1032));
        QVERIFY(!valid(base,1002,1003));QVERIFY(!valid(base,1000,1000));QVERIFY(!valid(base,1002,1001));
        for (const auto &field:QStringList{"schema","requestId","tradeId"}) {
            auto v=base;v[field]="wrong";QVERIFY(!valid(v,1000,1001));
        }
        for (const auto &field:QStringList{"executable","chainObserved"}) {
            auto v=base;v[field]=!base.value(field).toBool();QVERIFY(!valid(v,1000,1001));
            v[field]=1;QVERIFY(!valid(v,1000,1001));
        }
        for (const auto &time:QList<QJsonValue>{QJsonValue::Null,QJsonValue(true),QJsonValue("1001"),QJsonValue(1001.5),QJsonValue(-1)}) {
            auto v=base;v["observedAt"]=time;QVERIFY(!valid(v,1000,1001));
        }
        auto v=base;v["privateKey"]="unexpected";QVERIFY(!valid(v,1000,1001));
        v=base;v["review"]=approval(c,"check","ready");QVERIFY(!valid(v,1000,1001));
        v=base;v["review"]=QJsonValue::Null;QVERIFY(!valid(v,1000,1001));
    }

    void approvalActionsAndHistory() {
        Context c;QVERIFY(setup(c));QVERIFY(approved(approval(c,"restore","not-attempted"),c));
        for (const auto action:{"open","check"}) {
            for (const auto state:{"attempted","ready"}) QVERIFY(approved(approval(c,action,state),c,action));
            QVERIFY(!approved(approval(c,action,"not-attempted"),c,action));
        }
        QVERIFY(!approved(approval(c,"restore","attempted"),c));
        QVERIFY(!approved(approval(c,"restore","ready"),c));
        QVERIFY(!approved(approval(c,"restore","not-attempted"),c,"check"));
    }
    void approvalRejectsRehashedChanges_data() {
        QTest::addColumn<QString>("field");QTest::addColumn<QJsonValue>("replacement");
        QTest::newRow("height")<<QString("restoreHeight")<<QJsonValue(1);
        QTest::newRow("bch-policy")<<QString("minimumBchConfirmations")<<QJsonValue(999);
        QTest::newRow("xmz-policy")<<QString("minimumXmzConfirmations")<<QJsonValue(999);
        QTest::newRow("refund")<<QString("refund")<<QJsonValue(QString(64,'c'));
        QTest::newRow("account")<<QString("address")<<QJsonValue(QString(95,'5'));
        QTest::newRow("secret")<<QString("privateKey")<<QJsonValue("unexpected");
        auto endpoint=backend();endpoint["walletPort"]=18084;
        QTest::newRow("service")<<QString("walletBackend")<<QJsonValue(endpoint);
    }
    void approvalRejectsRehashedChanges() {
        QFETCH(QString,field);QFETCH(QJsonValue,replacement);Context c;QVERIFY(setup(c));
        auto d=approval(c,"restore","not-attempted").value("details").toObject();d[field]=replacement;QVERIFY(!approved(seal(d),c));
    }
    void approvalRejectsChangedCommitment() {
        Context c;QVERIFY(setup(c));auto v=approval(c,"restore","not-attempted");v["commitment"]=QString(64,'0');QVERIFY(!approved(v,c));
    }

    void historicalStates() {
        Context c;QVERIFY(setup(c));auto v=value(c);QVERIFY(valid(v,c));
        for (const auto state:{"attempted","ready"}) {v["state"]=state;QVERIFY(!valid(v,c));v["walletIntent"]=pinBytes(2);QVERIFY(valid(v,c));v["walletIntent"]=QJsonValue::Null;}
        v["state"]="not-attempted";v["walletIntent"]=pinBytes(2);QVERIFY(!valid(v,c));
    }
    void changedBindings_data() {
        QTest::addColumn<QString>("field");QTest::addColumn<QJsonValue>("replacement");
        for (const auto field:{"swapId","walletFile","address","nativePayment","refundHint","state","bchNetwork","bchGenesis","xmzNetwork"})
            QTest::newRow(field)<<QString(field)<<QJsonValue("changed");
        for (const auto field:{"quote","admission","walletBinding","bchDeployment"})
            QTest::newRow(field)<<QString(field)<<QJsonValue(pinBytes(3));
        QTest::newRow("chain")<<QString("chainObserved")<<QJsonValue(true);
        QTest::newRow("authority")<<QString("executable")<<QJsonValue(true);
        QTest::newRow("extra-secret")<<QString("privateKey")<<QJsonValue("unexpected");
    }
    void changedBindings() {QFETCH(QString,field);QFETCH(QJsonValue,replacement);Context c;QVERIFY(setup(c));auto v=value(c);QVERIFY(valid(v,c));v[field]=replacement;QVERIFY(!valid(v,c));}
    void wrongRoleAndMissingField() {Context c;QVERIFY(setup(c));auto v=value(c);QVERIFY(valid(v,c));
        c.config["role"]="bch-funder";QVERIFY(!valid(v,c));c.config["role"]="native-funder";
        v.remove("walletIntent");QVERIFY(!valid(v,c));}
};
QTEST_GUILESS_MAIN(BchRefundWalletProtocolTests)
#include "test_bch_refund_wallet_protocol.moc"
