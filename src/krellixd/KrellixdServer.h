#pragma once

#include <QHostAddress>
#include <QSet>
#include <QTcpServer>

class ClientSession;

// QTcpServer subclass that enforces an IP allow-list and a max-client cap
// on the *acceptance* side, before we read a single byte from the new
// socket. Failed checks close the socket immediately with no protocol
// chatter so an outside attacker learns nothing about the daemon's state.
class KrellixdServer : public QTcpServer
{
    Q_OBJECT

public:
    KrellixdServer(int intervalMs,
                   int idleTimeoutMs,
                   int maxClients,
                   const QSet<QHostAddress> &allowed,
                   QObject *parent = nullptr);
    ~KrellixdServer() override;

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private slots:
    void onSessionDestroyed(QObject *obj);

private:
    bool isAllowed(const QHostAddress &addr) const;

    int                  m_intervalMs;
    int                  m_idleTimeoutMs;
    int                  m_maxClients;
    QSet<QHostAddress>   m_allowed;
    QSet<ClientSession *> m_sessions;
};
