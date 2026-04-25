#include "ClientSession.h"

#include "sysdep/CpuStat.h"
#include "sysdep/DiskStat.h"
#include "sysdep/MemStat.h"
#include "sysdep/NetStat.h"
#include "sysdep/UptimeStat.h"

#include <QDateTime>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSysInfo>
#include <QTcpSocket>
#include <QTimer>

Q_LOGGING_CATEGORY(lcSession, "krellixd.session")

namespace {

// Even malicious clients can send us at most this much before we drop
// them. We never need to read more than a hello byte.
constexpr qint64 kMaxClientSendBytes = 4 * 1024;

QJsonObject helloObject(int intervalMs)
{
    QJsonObject o;
    o.insert(QStringLiteral("type"),        QStringLiteral("hello"));
    o.insert(QStringLiteral("version"),
             QString::fromUtf8(KRELLIX_VERSION));
    o.insert(QStringLiteral("hostname"),    QSysInfo::machineHostName());
    o.insert(QStringLiteral("kernel"),
             QSysInfo::kernelType() + QStringLiteral(" ") + QSysInfo::kernelVersion());
    o.insert(QStringLiteral("interval_ms"), intervalMs);
    QJsonArray mon;
    mon << QStringLiteral("cpu") << QStringLiteral("mem")
        << QStringLiteral("uptime") << QStringLiteral("net")
        << QStringLiteral("disk");
    o.insert(QStringLiteral("monitors"), mon);
    return o;
}

QJsonObject sampleObject()
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("sample"));
    o.insert(QStringLiteral("ts"),
             QDateTime::currentSecsSinceEpoch());
    o.insert(QStringLiteral("hostname"), QSysInfo::machineHostName());
    o.insert(QStringLiteral("kernel"),
             QSysInfo::kernelType() + QStringLiteral(" ") + QSysInfo::kernelVersion());

    o.insert(QStringLiteral("uptime"),
             qint64(UptimeStat::secondsSinceBoot()));

    QJsonArray cpuArr;
    for (const CpuSample &s : CpuStat::read()) {
        QJsonObject c;
        c.insert(QStringLiteral("name"),  s.name);
        c.insert(QStringLiteral("i"),     s.index);
        c.insert(QStringLiteral("user"),    qint64(s.user));
        c.insert(QStringLiteral("nice"),    qint64(s.nice));
        c.insert(QStringLiteral("sys"),     qint64(s.sys));
        c.insert(QStringLiteral("idle"),    qint64(s.idle));
        c.insert(QStringLiteral("iowait"),  qint64(s.iowait));
        c.insert(QStringLiteral("irq"),     qint64(s.irq));
        c.insert(QStringLiteral("softirq"), qint64(s.softirq));
        c.insert(QStringLiteral("steal"),   qint64(s.steal));
        cpuArr.append(c);
    }
    o.insert(QStringLiteral("cpu"), cpuArr);

    const MemInfo m = MemStat::read();
    QJsonObject mem;
    mem.insert(QStringLiteral("total"),      qint64(m.totalKb));
    mem.insert(QStringLiteral("free"),       qint64(m.freeKb));
    mem.insert(QStringLiteral("avail"),      qint64(m.availableKb));
    mem.insert(QStringLiteral("buf"),        qint64(m.buffersKb));
    mem.insert(QStringLiteral("cached"),     qint64(m.cachedKb));
    mem.insert(QStringLiteral("swap_total"), qint64(m.swapTotalKb));
    mem.insert(QStringLiteral("swap_free"),  qint64(m.swapFreeKb));
    o.insert(QStringLiteral("mem"), mem);

    QJsonArray netArr;
    for (const NetSample &s : NetStat::read()) {
        QJsonObject n;
        n.insert(QStringLiteral("name"), s.name);
        n.insert(QStringLiteral("rx"),   qint64(s.rxBytes));
        n.insert(QStringLiteral("tx"),   qint64(s.txBytes));
        n.insert(QStringLiteral("rxp"),  qint64(s.rxPackets));
        n.insert(QStringLiteral("txp"),  qint64(s.txPackets));
        netArr.append(n);
    }
    o.insert(QStringLiteral("net"), netArr);

    QJsonArray diskArr;
    for (const DiskSample &s : DiskStat::read()) {
        QJsonObject d;
        d.insert(QStringLiteral("name"), s.name);
        d.insert(QStringLiteral("sr"),   qint64(s.sectorsRead));
        d.insert(QStringLiteral("sw"),   qint64(s.sectorsWritten));
        diskArr.append(d);
    }
    o.insert(QStringLiteral("disk"), diskArr);

    return o;
}

void writeJsonLine(QTcpSocket *s, const QJsonObject &obj)
{
    if (!s) return;
    const QByteArray bytes =
        QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
    s->write(bytes);
}

} // namespace

ClientSession::ClientSession(QTcpSocket *socket,
                             int intervalMs,
                             int idleTimeoutMs,
                             QObject *parent)
    : QObject(parent)
    , m_socket(socket)
    , m_sendTimer(new QTimer(this))
    , m_idleTimer(new QTimer(this))
    , m_intervalMs(intervalMs)
    , m_idleTimeoutMs(idleTimeoutMs)
{
    Q_ASSERT(m_socket);
    m_socket->setParent(this);
    m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);

    connect(m_socket, &QTcpSocket::readyRead,
            this, &ClientSession::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected,
            this, &ClientSession::onDisconnected);

    m_sendTimer->setTimerType(Qt::CoarseTimer);
    m_sendTimer->setInterval(m_intervalMs);
    connect(m_sendTimer, &QTimer::timeout, this, &ClientSession::onTick);

    m_idleTimer->setSingleShot(true);
    m_idleTimer->setInterval(m_idleTimeoutMs);
    connect(m_idleTimer, &QTimer::timeout,
            this, &ClientSession::onIdleTimeout);
    m_idleTimer->start();

    qCInfo(lcSession) << "client connected from" << peerAddressString();
    sendHello();
    sendSample();      // immediate first sample so client doesn't wait a tick
    m_sendTimer->start();
}

ClientSession::~ClientSession() = default;

QString ClientSession::peerAddressString() const
{
    return m_socket
        ? (m_socket->peerAddress().toString() + QStringLiteral(":")
           + QString::number(m_socket->peerPort()))
        : QStringLiteral("(disconnected)");
}

void ClientSession::onTick()
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) return;
    sendSample();
}

void ClientSession::onReadyRead()
{
    if (!m_socket) return;
    // We don't expect the client to send anything substantive; silently
    // discard but enforce a hard cap so a malicious peer can't pile bytes
    // into our buffers.
    if (m_socket->bytesAvailable() > kMaxClientSendBytes) {
        closeWithError("client exceeded receive cap");
        return;
    }
    (void) m_socket->readAll();
    m_idleTimer->start();   // any I/O resets the idle clock
}

void ClientSession::onDisconnected()
{
    qCInfo(lcSession) << "client disconnected" << peerAddressString();
    deleteLater();
}

void ClientSession::onIdleTimeout()
{
    closeWithError("idle timeout");
}

void ClientSession::sendHello()
{
    writeJsonLine(m_socket, helloObject(m_intervalMs));
}

void ClientSession::sendSample()
{
    writeJsonLine(m_socket, sampleObject());
}

void ClientSession::closeWithError(const char *reason)
{
    qCWarning(lcSession) << "closing" << peerAddressString() << ":" << reason;
    if (m_socket) {
        m_socket->disconnectFromHost();
    }
}
