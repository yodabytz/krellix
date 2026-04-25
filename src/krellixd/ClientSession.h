#pragma once

#include <QObject>
#include <QPointer>

class QTcpSocket;
class QTimer;

// One per accepted client connection. Sends a one-time hello, then a
// JSON-lines "sample" message every intervalMs. Disconnects on idle/IO
// timeout, error, or read of more than kMaxClientSendBytes from the
// client (clients shouldn't be sending us anything substantive).
class ClientSession : public QObject
{
    Q_OBJECT

public:
    // Takes ownership of an already-constructed (and connected) socket.
    // The socket should already be peerAddress-validated by the caller.
    ClientSession(QTcpSocket *socket,
                  int intervalMs,
                  int idleTimeoutMs,
                  QObject *parent = nullptr);
    ~ClientSession() override;

    QString peerAddressString() const;

private slots:
    void onTick();
    void onReadyRead();
    void onDisconnected();
    void onIdleTimeout();

private:
    void sendHello();
    void sendSample();
    void closeWithError(const char *reason);

    QTcpSocket *m_socket;
    QTimer     *m_sendTimer;
    QTimer     *m_idleTimer;
    int         m_intervalMs;
    int         m_idleTimeoutMs;

    Q_DISABLE_COPY_MOVE(ClientSession)
};
