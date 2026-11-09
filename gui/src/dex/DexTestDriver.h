#ifndef MONZERO_DEX_TEST_DRIVER_H
#define MONZERO_DEX_TEST_DRIVER_H
#include <QObject>
#include <QPointer>

class QLocalServer;
class QLocalSocket;

// Test-only remote control of the DEX engine, for end-to-end runs of two real apps without
// clicking: a local socket (owner-only, in a folder the tester names) that takes one JSON
// request per line and answers one JSON line:
//   {"call": "<invokable>", "args": [...]}  -> {"ok": true, "result": <value>}
//   {"get": "<property>"}                   -> {"ok": true, "result": <value>}
// It reaches only the engine's own invokables and properties (what the DEX screen uses).
// Built into prototype builds only, never the public edition, and started only when
// MONZERO_DEX_TEST_DRIVER names a folder and the prototype opt-in is set.
class DexTestDriver final : public QObject {
    Q_OBJECT
public:
    DexTestDriver(QObject *engine,QObject *parent=nullptr);
    bool listen(const QString &directory);
private:
    void serve(QLocalSocket *socket);
    QByteArray answer(const QByteArray &line);
    QPointer<QObject> m_engine;
    QLocalServer *m_server=nullptr;
};
#endif
