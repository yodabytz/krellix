#include "RemoteSource.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QTcpSocket>
#include <QTimer>

Q_LOGGING_CATEGORY(lcRemote, "krellix.remote")

namespace {

constexpr qint64 kMaxLineBytes        = 256 * 1024;
constexpr qint64 kMaxBufferBytes      = 1024 * 1024;   // hard cap before drop
constexpr int    kMaxParseErrorsBeforeReset = 5;
constexpr int    kBaseReconnectMs     = 1000;
constexpr int    kMaxReconnectMs      = 30 * 1000;

RemoteSource *g_instance = nullptr;

} // namespace

RemoteSource *RemoteSource::instance() { return g_instance; }

RemoteSource::RemoteSource(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_reconnectTimer(new QTimer(this))
{
    g_instance = this;

    m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    connect(m_socket, &QTcpSocket::connected,
            this, &RemoteSource::onSocketConnected);
    connect(m_socket, &QTcpSocket::disconnected,
            this, &RemoteSource::onSocketDisconnected);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &RemoteSource::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred,
            this, &RemoteSource::onSocketError);

    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (m_socket->state() == QAbstractSocket::UnconnectedState
            && !m_host.isEmpty()) {
            qCInfo(lcRemote) << "reconnecting to" << m_host << m_port;
            m_socket->connectToHost(m_host, m_port);
        }
    });
}

RemoteSource::~RemoteSource()
{
    if (g_instance == this) g_instance = nullptr;
}

void RemoteSource::connectToHost(const QString &host, quint16 port)
{
    m_host = host;
    m_port = port ? port : 19150;
    m_reconnectDelayMs = kBaseReconnectMs;
    qCInfo(lcRemote) << "connecting to" << m_host << m_port;
    m_socket->connectToHost(m_host, m_port);
}

bool RemoteSource::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

QString RemoteSource::remoteAddress() const
{
    return m_host + QStringLiteral(":") + QString::number(m_port);
}

void RemoteSource::onSocketConnected()
{
    qCInfo(lcRemote) << "connected to" << remoteAddress();
    m_buffer.clear();
    m_consecutiveParseErrors = 0;
    m_reconnectDelayMs = kBaseReconnectMs;
    emit connectionStateChanged(true);
}

void RemoteSource::onSocketDisconnected()
{
    qCInfo(lcRemote) << "disconnected from" << remoteAddress();
    // Drop cached samples so a subsequent reconnect doesn't blip the
    // monitors with rates computed against pre-disconnect counters
    // (which can be many minutes stale). Better to show "0" for one
    // tick than to fabricate a giant spike.
    resetCachedState();
    m_buffer.clear();
    emit connectionStateChanged(false);
    scheduleReconnect();
}

void RemoteSource::onSocketError()
{
    qCWarning(lcRemote) << "socket error from" << remoteAddress() << ":"
                        << m_socket->errorString();
    // For a connection that *was* established, `disconnected` fires next
    // and reconnect is scheduled there. For a failed initial connect
    // (host unreachable, refused, etc.) we never reach ConnectedState, so
    // `disconnected` never fires — schedule the reconnect ourselves so
    // we don't get wedged after a one-time outage at startup.
    if (m_socket
        && m_socket->state() == QAbstractSocket::UnconnectedState) {
        scheduleReconnect();
    }
}

void RemoteSource::scheduleReconnect()
{
    if (m_host.isEmpty()) return;
    m_reconnectTimer->start(m_reconnectDelayMs);
    m_reconnectDelayMs = qMin(m_reconnectDelayMs * 2, kMaxReconnectMs);
}

void RemoteSource::onReadyRead()
{
    if (!m_socket) return;

    const QByteArray chunk = m_socket->readAll();
    if (chunk.isEmpty()) return;

    if (m_buffer.size() + chunk.size() > kMaxBufferBytes) {
        qCWarning(lcRemote) << "buffer overflow — dropping connection";
        m_socket->abort();
        return;
    }
    m_buffer.append(chunk);

    while (true) {
        const int nl = m_buffer.indexOf('\n');
        if (nl < 0) {
            if (m_buffer.size() > kMaxLineBytes) {
                qCWarning(lcRemote) << "line too long — dropping connection";
                m_socket->abort();
                return;
            }
            break;
        }
        const QByteArray line = m_buffer.left(nl);
        m_buffer.remove(0, nl + 1);
        if (!line.trimmed().isEmpty())
            parseLine(line);
        // parseLine() may have called m_socket->abort() on too-many parse
        // errors; that fires `disconnected` synchronously and resets
        // m_buffer. Stop the loop to avoid touching post-abort state.
        if (!m_socket
            || m_socket->state() != QAbstractSocket::ConnectedState)
            return;
    }
}

void RemoteSource::parseLine(const QByteArray &line)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcRemote) << "JSON parse error:" << err.errorString();
        if (++m_consecutiveParseErrors > kMaxParseErrorsBeforeReset) {
            qCWarning(lcRemote) << "too many parse errors — dropping connection";
            m_socket->abort();
        }
        return;
    }
    m_consecutiveParseErrors = 0;

    const QJsonObject obj = doc.object();
    const QString type = obj.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("hello")) {
        m_hostname = obj.value(QStringLiteral("hostname")).toString();
        m_kernel   = obj.value(QStringLiteral("kernel")).toString();
        qCInfo(lcRemote) << "remote hello: hostname=" << m_hostname
                         << "kernel=" << m_kernel;
        return;
    }
    if (type != QStringLiteral("sample")) return;

    if (obj.contains(QStringLiteral("hostname")))
        m_hostname = obj.value(QStringLiteral("hostname")).toString();
    if (obj.contains(QStringLiteral("kernel")))
        m_kernel   = obj.value(QStringLiteral("kernel")).toString();
    m_uptime = static_cast<qint64>(obj.value(QStringLiteral("uptime")).toDouble());

    QList<CpuSample> cpu;
    for (const QJsonValue &v : obj.value(QStringLiteral("cpu")).toArray()) {
        const QJsonObject c = v.toObject();
        CpuSample s;
        s.name      = c.value(QStringLiteral("name")).toString();
        s.index     = c.value(QStringLiteral("i")).toInt(-1);
        // Daemon already sends effective user/nice (guest backed out)
        // for old-client compatibility. Leaving guest/guestNice at 0 in
        // the parsed sample keeps effectiveUser()/effectiveNice() a
        // no-op so we don't double-subtract on the client side.
        s.user      = quint64(c.value(QStringLiteral("user")).toDouble());
        s.nice      = quint64(c.value(QStringLiteral("nice")).toDouble());
        s.sys       = quint64(c.value(QStringLiteral("sys")).toDouble());
        s.idle      = quint64(c.value(QStringLiteral("idle")).toDouble());
        s.iowait    = quint64(c.value(QStringLiteral("iowait")).toDouble());
        s.irq       = quint64(c.value(QStringLiteral("irq")).toDouble());
        s.softirq   = quint64(c.value(QStringLiteral("softirq")).toDouble());
        s.steal     = quint64(c.value(QStringLiteral("steal")).toDouble());
        cpu.append(s);
    }
    m_cpu = cpu;

    const QJsonObject m = obj.value(QStringLiteral("mem")).toObject();
    MemInfo mi;
    mi.totalKb     = quint64(m.value(QStringLiteral("total")).toDouble());
    mi.freeKb      = quint64(m.value(QStringLiteral("free")).toDouble());
    mi.availableKb = quint64(m.value(QStringLiteral("avail")).toDouble());
    mi.buffersKb   = quint64(m.value(QStringLiteral("buf")).toDouble());
    mi.cachedKb    = quint64(m.value(QStringLiteral("cached")).toDouble());
    mi.swapTotalKb = quint64(m.value(QStringLiteral("swap_total")).toDouble());
    mi.swapFreeKb  = quint64(m.value(QStringLiteral("swap_free")).toDouble());
    m_mem = mi;

    QList<NetSample> net;
    for (const QJsonValue &v : obj.value(QStringLiteral("net")).toArray()) {
        const QJsonObject n = v.toObject();
        NetSample s;
        s.name      = n.value(QStringLiteral("name")).toString();
        s.alias     = n.value(QStringLiteral("alias")).toString();
        s.rxBytes   = quint64(n.value(QStringLiteral("rx")).toDouble());
        s.txBytes   = quint64(n.value(QStringLiteral("tx")).toDouble());
        s.rxPackets = quint64(n.value(QStringLiteral("rxp")).toDouble());
        s.txPackets = quint64(n.value(QStringLiteral("txp")).toDouble());
        net.append(s);
    }
    m_net = net;

    QList<DiskSample> disk;
    for (const QJsonValue &v : obj.value(QStringLiteral("disk")).toArray()) {
        const QJsonObject d = v.toObject();
        DiskSample s;
        s.name           = d.value(QStringLiteral("name")).toString();
        s.sectorsRead    = quint64(d.value(QStringLiteral("sr")).toDouble());
        s.sectorsWritten = quint64(d.value(QStringLiteral("sw")).toDouble());
        disk.append(s);
    }
    m_disk = disk;

    const QJsonObject p = obj.value(QStringLiteral("proc")).toObject();
    ProcInfo pi;
    pi.processes = p.value(QStringLiteral("processes")).toInt();
    pi.users     = p.value(QStringLiteral("users")).toInt();
    m_proc = pi;

    emit sampleReceived();
}

void RemoteSource::resetCachedState()
{
    m_cpu.clear();
    m_mem  = MemInfo{};
    m_net.clear();
    m_disk.clear();
    m_proc = ProcInfo{};
    // -1 is the "unknown" sentinel UptimeMonitor uses to render "?".
    // Zero would render as "00:00" — actively misleading during a
    // disconnect window.
    m_uptime = -1;
}
