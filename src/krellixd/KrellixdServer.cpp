#include "KrellixdServer.h"

#include "ClientSession.h"

#include <QLoggingCategory>
#include <QTcpSocket>
#include <QTimer>
#include <sys/socket.h>
#include <netinet/in.h>

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
    , m_maxClients(qBound(1, maxClients, 16))
    , m_allowed(allowed)
{
    m_clock.start();
    setMaxPendingConnections(16);
}

KrellixdServer::~KrellixdServer() = default;

void KrellixdServer::incomingConnection(qintptr socketDescriptor)
{
    // Inspect the kernel peer before allocating Qt buffers or a session.
    sockaddr_storage peerStorage{};
    socklen_t peerLength = sizeof(peerStorage);
    QHostAddress peer;
    if (::getpeername(static_cast<int>(socketDescriptor),
                      reinterpret_cast<sockaddr *>(&peerStorage), &peerLength) == 0) {
        if (peerStorage.ss_family == AF_INET) {
            const auto *p = reinterpret_cast<const sockaddr_in *>(&peerStorage);
            peer = QHostAddress(ntohl(p->sin_addr.s_addr));
        } else if (peerStorage.ss_family == AF_INET6) {
            const auto *p = reinterpret_cast<const sockaddr_in6 *>(&peerStorage);
            peer = QHostAddress(p->sin6_addr.s6_addr);
            if (p->sin6_scope_id) peer.setScopeId(QString::number(p->sin6_scope_id));
        }
    }
    bool v4 = false;
    const quint32 ipv4 = peer.toIPv4Address(&v4);
    if (v4) peer = QHostAddress(ipv4);
    const qint64 now = m_clock.elapsed();
    if (peer.isNull() || !isAllowed(peer) || m_sessions.size() >= m_maxClients
        || now + 3000 < m_nextAdmission.value(peer, 0)) {
        // Reset immediately. Do not log every rejected connection (log-flood DoS).
        const linger reset{1, 0};
        ::setsockopt(static_cast<int>(socketDescriptor), SOL_SOCKET, SO_LINGER,
                     &reset, sizeof(reset));
        ::close(static_cast<int>(socketDescriptor));
        return;
    }
    // Burst of four clients, then one admission/sec per source. No untrusted map keys.
    m_nextAdmission.insert(peer, qMax(now, m_nextAdmission.value(peer, 0)) + 1000);
    auto *socket = new QTcpSocket(this);
    socket->setReadBufferSize(1);
    if (!socket->setSocketDescriptor(socketDescriptor)) {
        qCWarning(lcServer) << "setSocketDescriptor failed:" << socket->errorString();
        delete socket;
        ::close(static_cast<int>(socketDescriptor));
        return;
    }
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
    // Connect lifetime accounting before any I/O can synchronously fail.
    QTimer::singleShot(0, session, &ClientSession::start);

    qCInfo(lcServer) << "accepted" << peerStr
                     << "(now" << m_sessions.size() << "/" << m_maxClients
                     << "clients)";
}

void KrellixdServer::onSessionDestroyed(QObject *obj)
{
    // destroyed() runs after the derived session's lifetime has ended.
    m_sessions.remove(obj);
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
