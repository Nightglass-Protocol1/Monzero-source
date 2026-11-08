#include "RelayMailTransport.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <sodium.h>
#include <functional>
#include <memory>

namespace {
const QByteArray Magic("MZRL1");
constexpr int SessionBytes=16,HeaderBytes=5+16+16+8+8,TagBytes=crypto_aead_chacha20poly1305_IETF_ABYTES;
constexpr int PollMs=1500,HandshakeMs=180000,PostRetries=20;
const QByteArray Label("monzero-relay-channel-v1");

QByteArray blake(const QByteArray &key,const QByteArray &message) {
    QByteArray out(32,'\0');
    crypto_generichash(reinterpret_cast<unsigned char*>(out.data()),32,reinterpret_cast<const unsigned char*>(message.constData()),
        quint64(message.size()),reinterpret_cast<const unsigned char*>(key.constData()),size_t(key.size()));
    return out;
}
QByteArray be64(quint64 v) {QByteArray b(8,'\0');for (int i=7;i>=0;--i) {b[i]=char(v&255);v>>=8;}return b;}
quint64 readBe64(const QByteArray &b,int at) {quint64 v=0;for (int i=0;i<8;++i) v=(v<<8)|quint8(b.at(at+i));return v;}
// Letter keys are per direction and per sender session, so (key, nonce) never repeats
QByteArray sessionKey(const QByteArray &directionKey,const QByteArray &session) {return blake(directionKey,session);}
QByteArray nonceFor(quint64 letterSeq) {return QByteArray(4,'\0')+be64(letterSeq);}
bool relayUrl(const QString &url) {
    static const QRegularExpression ok("\\A(https://[A-Za-z0-9.-]+(:[0-9]{1,5})?(/[A-Za-z0-9._~/-]*)?|http://(127\\.0\\.0\\.1|localhost):[0-9]{1,5})\\z");
    return ok.match(url).hasMatch();
}
}

RelayMailTransport::RelayMailTransport(QObject *parent):QObject(parent),m_network(new QNetworkAccessManager(this)) {
    m_poll.setInterval(PollMs);
    connect(&m_poll,&QTimer::timeout,this,&RelayMailTransport::poll);
    m_handshake.setSingleShot(true);m_handshake.setInterval(HandshakeMs);
    connect(&m_handshake,&QTimer::timeout,this,[this]{if (!m_ready && !m_stopped) {stop();emit failed();}});
}
RelayMailTransport::~RelayMailTransport() {
    sodium_memzero(m_ownSecret.data(),size_t(m_ownSecret.size()));
    sodium_memzero(m_sendKey.data(),size_t(m_sendKey.size()));sodium_memzero(m_receiveKey.data(),size_t(m_receiveKey.size()));
}

bool RelayMailTransport::directionKeys(const QByteArray &ownSecret,const QByteArray &peerPublic,QByteArray *sendKey,QByteArray *receiveKey) {
    if (sodium_init()<0 || ownSecret.size()!=32 || peerPublic.size()!=32) return false;
    QByteArray own(32,'\0'),shared(32,'\0');
    crypto_scalarmult_base(reinterpret_cast<unsigned char*>(own.data()),reinterpret_cast<const unsigned char*>(ownSecret.constData()));
    if (own==peerPublic || crypto_scalarmult(reinterpret_cast<unsigned char*>(shared.data()),reinterpret_cast<const unsigned char*>(ownSecret.constData()),
            reinterpret_cast<const unsigned char*>(peerPublic.constData()))!=0) return false;
    *sendKey=blake(shared,Label+own+peerPublic);*receiveKey=blake(shared,Label+peerPublic+own);
    sodium_memzero(shared.data(),32);
    return true;
}

QByteArray RelayMailTransport::sealLetter(const QByteArray &key,const QByteArray &session,const QByteArray &ackSession,quint64 seq,const QByteArray &payload) {
    // seq: the letter's own number (nonce); the payload's first 8 bytes say which data letter it is (0 = hello)
    if (session.size()!=SessionBytes || ackSession.size()!=SessionBytes || payload.size()<8) return {};
    const QByteArray header=Magic+session+ackSession+be64(seq)+payload.left(8);
    const QByteArray body=payload.mid(8);
    QByteArray out(header.size()+body.size()+TagBytes,'\0');
    std::copy(header.cbegin(),header.cend(),out.begin());
    unsigned long long written=0;
    const auto k=sessionKey(key,session);const auto nonce=nonceFor(seq);
    crypto_aead_chacha20poly1305_ietf_encrypt(reinterpret_cast<unsigned char*>(out.data()+header.size()),&written,
        reinterpret_cast<const unsigned char*>(body.constData()),quint64(body.size()),
        reinterpret_cast<const unsigned char*>(header.constData()),quint64(header.size()),nullptr,
        reinterpret_cast<const unsigned char*>(nonce.constData()),reinterpret_cast<const unsigned char*>(k.constData()));
    out.resize(header.size()+int(written));
    return out;
}

bool RelayMailTransport::openLetter(const QByteArray &key,const QByteArray &letter,QByteArray *session,QByteArray *ackSession,quint64 *seq,QByteArray *payload) {
    if (letter.size()<HeaderBytes+TagBytes || letter.size()>HeaderBytes+TagBytes+maximumPayload || !letter.startsWith(Magic)) return false;
    const QByteArray header=letter.left(HeaderBytes);
    const QByteArray s=header.mid(5,16),ack=header.mid(21,16);
    const quint64 n=readBe64(header,37);
    const auto k=sessionKey(key,s);const auto nonce=nonceFor(n);
    QByteArray body(letter.size()-HeaderBytes-TagBytes,'\0');
    unsigned long long length=0;
    if (crypto_aead_chacha20poly1305_ietf_decrypt(reinterpret_cast<unsigned char*>(body.data()),&length,nullptr,
            reinterpret_cast<const unsigned char*>(letter.constData()+HeaderBytes),quint64(letter.size()-HeaderBytes),
            reinterpret_cast<const unsigned char*>(header.constData()),quint64(header.size()),
            reinterpret_cast<const unsigned char*>(nonce.constData()),reinterpret_cast<const unsigned char*>(k.constData()))!=0) return false;
    body.resize(int(length));
    *session=s;*ackSession=ack;*seq=n;*payload=header.mid(45,8)+body;
    return true;
}

bool RelayMailTransport::start(const QStringList &relays,const QByteArray &ownSecret,const QByteArray &peerPublic) {
    if (!m_stopped || m_ready || relays.isEmpty() || relays.size()>8) return false;
    for (const auto &r:relays) if (!relayUrl(r)) return false;
    if (!directionKeys(ownSecret,peerPublic,&m_sendKey,&m_receiveKey)) return false;
    m_ownSecret=ownSecret;m_peerPublic=peerPublic;m_ownPublic=QByteArray(32,'\0');
    crypto_scalarmult_base(reinterpret_cast<unsigned char*>(m_ownPublic.data()),reinterpret_cast<const unsigned char*>(m_ownSecret.constData()));
    m_relays=relays;m_session=QByteArray(SessionBytes,'\0');
    randombytes_buf(m_session.data(),SessionBytes);
    m_stopped=false;m_ready=false;m_peerAcked=false;m_peerSession.clear();m_early.clear();m_nextSend=1;m_nextReceive=1;m_dataSent=0;
    m_handshake.start();m_poll.start();
    sendHello();
    QTimer::singleShot(0,this,&RelayMailTransport::poll);
    return true;
}

void RelayMailTransport::stop() {
    m_stopped=true;m_ready=false;m_poll.stop();m_handshake.stop();
}

void RelayMailTransport::sendHello() {
    // A hello is a letter whose data number is 0; it tells the peer our session and the one we saw
    const QByteArray ack=m_peerSession.isEmpty() ? QByteArray(SessionBytes,'\0') : m_peerSession;
    post(sealLetter(m_sendKey,m_session,ack,m_nextSend++,be64(0)));
}

bool RelayMailTransport::send(const QByteArray &payload) {
    if (!ready() || payload.isEmpty() || payload.size()>maximumPayload) return false;
    // Letter numbers count every letter (hellos too); data numbers count payloads to this peer session
    const quint64 data=++m_dataSent;
    post(sealLetter(m_sendKey,m_session,m_peerSession,m_nextSend++,be64(data)+payload));
    return true;
}

void RelayMailTransport::post(const QByteArray &letter) {
    if (letter.isEmpty() || m_stopped) return;
    const QJsonObject body{{"to","0x"+QString::fromLatin1(m_peerPublic.toHex())},{"sealed","0x"+QString::fromLatin1(letter.toHex())}};
    const auto bytes=QJsonDocument(body).toJson(QJsonDocument::Compact);
    auto attempt=std::make_shared<int>(0);
    auto delivered=std::make_shared<bool>(false);
    auto outstanding=std::make_shared<int>(0);
    std::shared_ptr<std::function<void()>> tryAll=std::make_shared<std::function<void()>>();
    QPointer<RelayMailTransport> self(this);
    *tryAll=[self,bytes,attempt,delivered,outstanding,tryAll]() {
        if (!self || self->m_stopped) return;
        *outstanding=self->m_relays.size();
        for (const auto &relay:self->m_relays) {
            QNetworkRequest request(QUrl(relay+"/v1/mail"));
            request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
            request.setTransferTimeout(30000);
            auto *reply=self->m_network->post(request,bytes);
            connect(reply,&QNetworkReply::finished,self,[self,reply,attempt,delivered,outstanding,tryAll]() {
                reply->deleteLater();
                if (!self) return;
                if (reply->error()==QNetworkReply::NoError) *delivered=true;
                if (--*outstanding>0 || *delivered || self->m_stopped) return;
                // No relay took it: try again a little later, then give up as a lost connection would
                if (++*attempt>=PostRetries) {self->stop();emit self->failed();return;}
                QTimer::singleShot(3000,self,[tryAll]{(*tryAll)();});
            });
        }
    };
    (*tryAll)();
}

void RelayMailTransport::poll() {
    if (m_stopped) return;
    for (const auto &relay:m_relays) {
        if (m_pending>=m_relays.size()) return;  // the previous round is still out
        QUrl url(relay+"/v1/mail");QUrlQuery q;
        q.addQueryItem("to","0x"+QString::fromLatin1(m_ownPublic.toHex()));
        q.addQueryItem("since",QString::number(m_since.value(relay,0)));
        url.setQuery(q);
        QNetworkRequest request(url);request.setTransferTimeout(20000);
        auto *reply=m_network->get(request);++m_pending;
        connect(reply,&QNetworkReply::finished,this,[this,reply,relay]() {
            reply->deleteLater();--m_pending;
            if (m_stopped || reply->error()!=QNetworkReply::NoError) return;
            const auto body=reply->read(64*1024*1024);
            const auto mail=QJsonDocument::fromJson(body).object().value("mail").toArray();
            qint64 newest=m_since.value(relay,0);
            QPointer<RelayMailTransport> guard(this);
            for (const auto &v:mail) {
                const auto o=v.toObject();
                newest=qMax(newest,qint64(o.value("at").toDouble()));
                auto hex=o.value("sealed").toString();
                if (hex.startsWith("0x")) hex=hex.mid(2);
                handleLetter(QByteArray::fromHex(hex.toLatin1()));
                if (!guard || m_stopped) return;
            }
            m_since.insert(relay,newest);
        });
    }
}

void RelayMailTransport::handleLetter(const QByteArray &letter) {
    const auto digest=QCryptographicHash::hash(letter,QCryptographicHash::Sha256);
    if (m_seen.contains(digest)) return;
    QByteArray session,ack,payload;quint64 seq=0;
    if (!openLetter(m_receiveKey,letter,&session,&ack,&seq,&payload)) return;  // not from our peer
    m_seen.insert(digest);
    const quint64 data=readBe64(payload,0);payload=payload.mid(8);
    if (session!=m_peerSession) {
        if (m_ready) {
            // A letter from another session of the peer that saw our current session: it restarted
            if (ack==m_session) {stop();emit closed();}
            return;
        }
        // Before the handshake the newest session we hear from is the peer's
        m_peerSession=session;m_early.clear();m_nextReceive=1;m_peerAcked=false;
        sendHello();
    }
    if (ack==m_session) m_peerAcked=true;
    // Data counts only once the peer wrote to our current session, as a new connection starts clean
    if (data>0 && ack==m_session && data>=m_nextReceive && !m_early.contains(data) && m_early.size()<4096) m_early.insert(data,payload);
    QPointer<RelayMailTransport> guard(this);
    if (!m_ready && m_peerAcked) {
        m_ready=true;m_handshake.stop();
        emit authenticated();
        if (!guard || m_stopped) return;
    }
    while (m_ready && m_early.contains(m_nextReceive)) {
        const auto next=m_early.take(m_nextReceive++);
        emit received(next);
        if (!guard || m_stopped) return;
    }
}
