#include "KrellixdServer.h"

#include "ClientSession.h"

#include <QLoggingCategory>
#include <QTcpSocket>

#include <unistd.h>

Q_LOGGING_CATEGORY(lcServer, "krellixd.server")

KrellixdServer::KrellixdServer(int intervalMs,
                               int idleTimeoutMs,
                               int maxClients,
                               const QSet<QHostAddress> &allowed,
                               QObject *parent)
    : QTcpServer(parent)
    , m_intervalMs(intervalMs)
    , m_idleTimeoutMs(idleTimeoutMs)
    , m_maxClients(qMax(1, maxClients))
    , m_allowed(allowed)
{
}

KrellixdServer::~KrellixdServer() = default;

void KrellixdServer::incomingConnection(qintptr socketDescriptor)
{
    // Quick, transparent allow-list check using a throwaway QTcpSocket so
    // we know the peer's address before committing to a session.
    QTcpSocket probe;
    if (!probe.setSocketDescriptor(socketDescriptor,
                                   QTcpSocket::ConnectedState,
                                   QTcpSocket::ReadOnly)) {
        // Couldn't even bind the descriptor; just close it.
        ::close(static_cast<int>(socketDescriptor));
        return;
    }
    const QHostAddress peer = probe.peerAddress();
    const QString peerStr   = peer.toString();

    if (!isAllowed(peer)) {
        qCWarning(lcServer) << "rejecting connection from" << peerStr
                            << "(not in allow-list)";
        probe.abort();    // RST instead of FIN — leak no banner
        return;
    }

    if (m_sessions.size() >= m_maxClients) {
        qCWarning(lcServer) << "rejecting" << peerStr
                            << "(client cap" << m_maxClients << "reached)";
        probe.abort();
        return;
    }

    // Allow-listed and within cap. Re-arm the descriptor on a real
    // ClientSession by detaching from the probe (so the FD doesn't get
    // closed when probe goes out of scope) and re-using it.
    probe.setSocketDescriptor(-1);

    auto *session = new ClientSession(socketDescriptor,
                                      m_intervalMs,
                                      m_idleTimeoutMs,
                                      this);
    connect(session, &QObject::destroyed,
            this, &KrellixdServer::onSessionDestroyed);
    m_sessions.insert(session);

    qCInfo(lcServer) << "accepted" << peerStr
                     << "(now" << m_sessions.size() << "/" << m_maxClients
                     << "clients)";
}

void KrellixdServer::onSessionDestroyed(QObject *obj)
{
    m_sessions.remove(static_cast<ClientSession *>(obj));
}

bool KrellixdServer::isAllowed(const QHostAddress &addr) const
{
    if (m_allowed.isEmpty()) return false;  // empty allow-list = deny all
    if (m_allowed.contains(addr)) return true;

    // Normalize IPv4-in-IPv6 (::ffff:1.2.3.4) so an admin who allow-listed
    // 1.2.3.4 still matches a peer that arrives as ::ffff:1.2.3.4.
    bool ok = false;
    const quint32 v4 = addr.toIPv4Address(&ok);
    if (ok) {
        const QHostAddress mapped(v4);
        if (m_allowed.contains(mapped)) return true;
    }
    return false;
}
