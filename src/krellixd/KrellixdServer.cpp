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
    // Build the real socket up front and inspect the peer on it. If the
    // peer isn't allowed (or we're over cap) we abort and delete it; the
    // descriptor is closed cleanly via the QTcpSocket destructor.
    auto *socket = new QTcpSocket(this);
    if (!socket->setSocketDescriptor(socketDescriptor)) {
        qCWarning(lcServer) << "setSocketDescriptor failed:" << socket->errorString();
        delete socket;
        ::close(static_cast<int>(socketDescriptor));
        return;
    }
    const QHostAddress peer = socket->peerAddress();
    const QString peerStr   = peer.toString();

    if (!isAllowed(peer)) {
        qCWarning(lcServer) << "rejecting" << peerStr << "(not in allow-list)";
        socket->abort();      // RST: leak no banner
        socket->deleteLater();
        return;
    }

    if (m_sessions.size() >= m_maxClients) {
        qCWarning(lcServer) << "rejecting" << peerStr
                            << "(client cap" << m_maxClients << "reached)";
        socket->abort();
        socket->deleteLater();
        return;
    }

    // Hand the live socket to a ClientSession (which takes ownership).
    auto *session = new ClientSession(socket, m_intervalMs, m_idleTimeoutMs, this);
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
