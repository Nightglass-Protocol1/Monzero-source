#include "../BchPrivateSuccessProtocol.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
using namespace BchPrivateSuccessProtocol;
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    const QString request="11111111-1111-4111-8111-111111111111",trade="22222222-2222-4222-8222-222222222222";
    int checks=0;
    auto verify=[&](QByteArray bytes,Action action,bool expected,const QString &id) {
        Material result;
        const bool accepted=response(bytes.data(),bytes.size(),action,id,trade,result);
        if (accepted!=expected || bytes!=QByteArray(bytes.size(),'\0')) return false;
        if (!accepted && (!result.frameView().isEmpty() || !result.signatureView().isEmpty())) return false;
        if (accepted && action==Action::Retain && (!result.frameView().isEmpty() || !result.signatureView().isEmpty())) return false;
        if (accepted && action!=Action::Retain && result.frameView()!=QByteArray::fromHex("0001ff")) return false;
        if (accepted && action==Action::Release && result.signatureView()!=QByteArray(64,'\x07')) return false;
        result.clear();if (!result.frameView().isEmpty() || !result.signatureView().isEmpty()) return false;
        ++checks;return true;
    };
    for (auto action : {Action::Frame,Action::Retain,Action::Release}) {
        const QByteArray name=action==Action::Frame?"frame":action==Action::Retain?"retain":"release";
        QByteArray wire="{\"schema\":\"monzero-bch-success-private-"+name+"-response-v1\",\"requestId\":\""
            +request.toUtf8()+"\",\"tradeId\":\""+trade.toUtf8()+"\",\"chainObserved\":true";
        if (action!=Action::Retain) wire+=",\"frameHex\":\"0001ff\"";
        if (action==Action::Release) wire+=",\"signatureHex\":\""+QByteArray("07").repeated(64)+"\"";
        wire+='}';
        if (!verify(wire,action,true,request)) return 1;
        for (int length=0;length<wire.size();++length) if (!verify(wire.left(length),action,false,request)) return 2;
        for (auto wrong : {Action::Frame,Action::Retain,Action::Release})
            if (wrong!=action && !verify(wire,wrong,false,request)) return 3;
        if (!verify(wire,action,false,trade) || !verify(wire,static_cast<Action>(99),false,request)) return 4;
        if (!verify(wire+'\n',action,false,request) || !verify(wire+'}',action,false,request)) return 5;
        auto changed=wire;changed.replace("true","false");if (!verify(changed,action,false,request)) return 6;
        changed=wire;changed.insert(changed.size()-1,",\"extra\":true");if (!verify(changed,action,false,request)) return 7;
        if (action!=Action::Retain) for (const auto &bad : {QByteArray(),QByteArray("0"),QByteArray("GG"),QByteArray("0A"),QByteArray("01").repeated(8193)}) {
            changed=wire;changed.replace("0001ff",bad);if (!verify(changed,action,false,request)) return 8;
        }
        if (action==Action::Release) for (int length : {0,63,65}) {
            changed=wire;changed.replace(QByteArray("07").repeated(64),QByteArray("07").repeated(length));
            if (!verify(changed,action,false,request)) return 9;
        }
        Material reused;auto valid=wire;
        if (!response(valid.data(),valid.size(),action,request,trade,reused)) return 10;
        auto bad=wire+'x';if (response(bad.data(),bad.size(),action,request,trade,reused)
            || !reused.frameView().isEmpty() || !reused.signatureView().isEmpty()) return 11;
        ++checks;
    }
    QByteArray maximum="{\"schema\":\"monzero-bch-success-private-release-response-v1\",\"requestId\":\""
        +request.toUtf8()+"\",\"tradeId\":\""+trade.toUtf8()+"\",\"chainObserved\":true,\"frameHex\":\""
        +QByteArray("ff").repeated(8192)+"\",\"signatureHex\":\""+QByteArray("07").repeated(64)+"\"}";
    Material material;
    if (!response(maximum.data(),maximum.size(),Action::Release,request,trade,material)
        || maximum!=QByteArray(maximum.size(),'\0') || material.frameView()!=QByteArray(8192,'\xff')) return 12;
    const auto frame=material.frameView(),signature=material.signatureView();
    material.clear();
    if (frame!=QByteArray(8192,'\0') || signature!=QByteArray(64,'\0')) return 13;
    checks+=2;
    Request encoded;
    const QString commitment=QString("ab").repeated(32);
    for (auto action : {Action::Frame,Action::Retain,Action::Release}) {
        const QByteArray data=action==Action::Retain?QByteArray::fromHex("0001ff"):QByteArray();
        const QByteArray sig=action==Action::Retain?QByteArray(64,'\x07'):QByteArray();
        if (!BchPrivateSuccessProtocol::request(action,request,commitment,data,sig,encoded)) return 14;
        const auto parsed=QJsonDocument::fromJson(encoded.view()).object();
        if (parsed.size()!=(action==Action::Retain?5:3) || parsed.value("requestId")!=request
            || parsed.value("commitment")!=commitment) return 15;
        if (action==Action::Retain && (parsed.value("frameHex")!="0001ff"
            || parsed.value("signatureHex")!=QString("07").repeated(64))) return 16;
        const auto old=encoded.view();encoded.clear();
        if (!encoded.view().isEmpty() || old!=QByteArray(old.size(),'\0')) return 17;
        checks+=2;
    }
    for (int length : {0,1,8192,8193}) {
        const bool expected=length>0 && length<=8192;
        if (BchPrivateSuccessProtocol::request(Action::Retain,request,commitment,QByteArray(length,'\xff'),QByteArray(64,'\x07'),encoded)!=expected) return 18;
        if (expected) {
            const auto parsed=QJsonDocument::fromJson(encoded.view()).object();
            if (parsed.value("frameHex").toString()!=QString("ff").repeated(length)) return 19;
        } else if (!encoded.view().isEmpty()) return 20;
        ++checks;
    }
    for (int length : {0,63,65}) {
        if (BchPrivateSuccessProtocol::request(Action::Retain,request,commitment,QByteArray(1,'x'),QByteArray(length,'x'),encoded)
            || !encoded.view().isEmpty()) return 21;
        ++checks;
    }
    for (const auto &bad : {QString(),QString("AB").repeated(32),QString("0").repeated(63)}) {
        if (BchPrivateSuccessProtocol::request(Action::Frame,request,bad,{},{},encoded) || !encoded.view().isEmpty()) return 22;
        ++checks;
    }
    if (!BchPrivateSuccessProtocol::request(Action::Retain,request,commitment,QByteArray(8192,'x'),QByteArray(64,'x'),encoded)) return 23;
    const auto previous=encoded.view();
    if (BchPrivateSuccessProtocol::request(Action::Frame,request,commitment,QByteArray(1,'x'),{},encoded)
        || !encoded.view().isEmpty() || previous!=QByteArray(previous.size(),'\0')) return 24;
    ++checks;
    std::printf("%d private request/response validation and zeroization checks passed\n",checks);return 0;
}
