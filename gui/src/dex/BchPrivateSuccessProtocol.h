#ifndef MONZERO_BCH_PRIVATE_SUCCESS_PROTOCOL_H
#define MONZERO_BCH_PRIVATE_SUCCESS_PROTOCOL_H
#include <QByteArray>
#include <QString>
#include <QUuid>
#include <array>
#include <cstring>
#include <sodium.h>
// Internal private-process protocol. Never expose Material through QVariant,
// QJsonObject, signals, logs or QML. Views expire when their Material is cleared.
namespace BchPrivateSuccessProtocol {
enum class Action { Frame, Retain, Release };
class Material final {
public:
    Material() = default;
    ~Material() { clear(); }
    Material(const Material &) = delete;
    Material &operator=(const Material &) = delete;
    void clear() {
        sodium_memzero(m_frame.data(),m_frame.size());
        sodium_memzero(m_signature.data(),m_signature.size());
        m_frameSize=0;m_signatureSize=0;
    }
    QByteArray frameView() const { return QByteArray::fromRawData(m_frame.data(),m_frameSize); }
    QByteArray signatureView() const { return QByteArray::fromRawData(m_signature.data(),m_signatureSize); }
private:
    friend bool response(char *,int,Action,const QString &,const QString &,Material &);
    std::array<char,8192> m_frame{};
    std::array<char,64> m_signature{};
    int m_frameSize=0,m_signatureSize=0;
};
inline bool canonicalId(const QString &value) {
    const QUuid id(value);
    return !id.isNull() && id.toString(QUuid::WithoutBraces)==value;
}
// Owned wire buffer for a single private request. The caller must not copy its
// view into a JSON value, log or shared buffer; clear after the pipe write.
class Request final {
public:
    Request() = default;
    ~Request() { clear(); }
    Request(const Request &) = delete;
    Request &operator=(const Request &) = delete;
    void clear() { sodium_memzero(m_bytes.data(),m_bytes.size());m_size=0; }
    QByteArray view() const { return QByteArray::fromRawData(m_bytes.data(),m_size); }
private:
    friend bool request(Action,const QString &,const QString &,const QByteArray &,const QByteArray &,Request &);
    std::array<char,32768> m_bytes{};
    int m_size=0;
};
inline bool request(Action action,const QString &id,const QString &commitment,
    const QByteArray &frame,const QByteArray &signature,Request &result) {
    result.clear();
    struct Cleanup { Request &result;bool accepted=false;~Cleanup() { if (!accepted) result.clear(); } } cleanup{result};
    if (!canonicalId(id) || commitment.size()!=64) return false;
    for (const auto c : commitment) if (!((c>='0' && c<='9') || (c>='a' && c<='f'))) return false;
    QByteArray operation;
    switch (action) {
    case Action::Frame: operation="frame";break;
    case Action::Retain: operation="retain";break;
    case Action::Release: operation="release";break;
    default: return false;
    }
    if (action==Action::Retain) {
        if (frame.isEmpty() || frame.size()>8192 || signature.size()!=64) return false;
    } else if (!frame.isEmpty() || !signature.isEmpty()) return false;
    auto append=[&](const QByteArray &bytes) {
        if (bytes.size()>int(result.m_bytes.size())-result.m_size) return false;
        std::memcpy(result.m_bytes.data()+result.m_size,bytes.constData(),bytes.size());
        result.m_size+=bytes.size();return true;
    };
    auto hex=[&](const QByteArray &bytes) {
        if (bytes.size()>(int(result.m_bytes.size())-result.m_size)/2) return false;
        constexpr char digits[]="0123456789abcdef";
        for (const unsigned char c : bytes) {
            result.m_bytes[result.m_size++]=digits[c>>4];
            result.m_bytes[result.m_size++]=digits[c&15];
        }
        return true;
    };
    if (!append("{\"schema\":\"monzero-bch-success-private-"+operation+"-request-v1\",\"requestId\":\""
        +id.toUtf8()+"\",\"commitment\":\""+commitment.toUtf8()+"\"")) return false;
    if (action==Action::Retain && (!append(",\"frameHex\":\"") || !hex(frame)
        || !append("\",\"signatureHex\":\"") || !hex(signature) || !append("\""))) return false;
    if (!append("}")) return false;
    cleanup.accepted=true;return true;
}
// Consume the pinned helper's compact, fixed-order serialization directly.
// No sensitive QString, JSON tree or implicitly-shared byte-array copy is made.
// The caller must own a mutable, exclusively owned buffer of exactly size bytes.
inline bool response(char *wire,int size,Action action,const QString &request,
    const QString &trade,Material &material) {
    material.clear();
    struct Cleanup {
        char *wire;int size;Material &material;bool accepted=false;
        ~Cleanup() {
            if (wire && size>0) sodium_memzero(wire,size);
            if (!accepted) material.clear();
        }
    } cleanup{wire,size,material};
    if (!wire || size<=0 || size>131072 || !canonicalId(request) || !canonicalId(trade)) return false;
    QByteArray operation;
    switch (action) {
    case Action::Frame: operation="frame";break;
    case Action::Retain: operation="retain";break;
    case Action::Release: operation="release";break;
    default: return false;
    }
    const QByteArray prefix="{\"schema\":\"monzero-bch-success-private-"+operation
        +"-response-v1\",\"requestId\":\""+request.toUtf8()+"\",\"tradeId\":\""+trade.toUtf8()
        +"\",\"chainObserved\":true";
    int position=0;
    auto literal=[&](const QByteArray &value) {
        if (value.size()>size-position || std::memcmp(wire+position,value.constData(),value.size())!=0) return false;
        position+=value.size();return true;
    };
    auto nibble=[](unsigned char c) -> int {
        if (c>='0' && c<='9') return c-'0';
        if (c>='a' && c<='f') return c-'a'+10;
        return -1;
    };
    auto hex=[&](char *destination,int capacity,int &length) {
        while (position<size && wire[position]!='"') {
            if (position+1>=size || length>=capacity) return false;
            const int high=nibble(wire[position]),low=nibble(wire[position+1]);
            if (high<0 || low<0) return false;
            destination[length++]=char((high<<4)|low);position+=2;
        }
        return length>0 && literal("\"");
    };
    if (!literal(prefix)) return false;
    if (action!=Action::Retain && (!literal(",\"frameHex\":\"")
        || !hex(material.m_frame.data(),int(material.m_frame.size()),material.m_frameSize))) return false;
    if (action==Action::Release && (!literal(",\"signatureHex\":\"")
        || !hex(material.m_signature.data(),int(material.m_signature.size()),material.m_signatureSize)
        || material.m_signatureSize!=64)) return false;
    if (!literal("}") || position!=size) return false;
    cleanup.accepted=true;return true;
}
}
#endif
