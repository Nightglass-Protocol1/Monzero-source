#ifndef MONZERO_NATIVE_DESTINATION_JOB_H
#define MONZERO_NATIVE_DESTINATION_JOB_H
#include <QObject>
#include <QJsonObject>
#include <QElapsedTimer>
class QProcess;
class QTimer;

// Single-use address-validation job. Native caller supplies verified commercial
// terms and trusted address-network selection. Never opens a wallet or signs.
class NativeDestinationJob : public QObject {
    Q_OBJECT
public:
    explicit NativeDestinationJob(QObject *parent = nullptr);
    ~NativeDestinationJob() override;
    bool start(const QString &helper, const QString &digest, const QJsonObject &terms,
        qint64 now, const QString &bitcoinNetwork, const QString &xmzNetwork, int timeoutMs = 10000);
    void cancel();
    QJsonObject result() const { return m_result; }
signals:
    void completed(bool valid);
private:
    void drain();
    void finish(bool success);
    QProcess *m_process;
    QTimer *m_timer;
    QElapsedTimer m_elapsed;
    QByteArray m_input, m_output, m_expected;
    QJsonObject m_result;
    qint64 m_errors = 0;
    int m_timeout = 0;
    bool m_started = false, m_done = true;
};
#endif
