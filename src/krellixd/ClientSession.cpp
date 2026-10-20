#include "ClientSession.h"
#include "PackageProbe.h"

#include "sysdep/CpuStat.h"
#include "sysdep/SensorStat.h"
#include "sysdep/BatteryStat.h"
#include "sysdep/DiskStat.h"
#include "sysdep/MemStat.h"
#include "sysdep/NetStat.h"
#include "sysdep/NetPortStat.h"
#include "sysdep/ProcStat.h"
#include "sysdep/UptimeStat.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHostAddress>
#include <QHostInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QLoggingCategory>
#include <QStorageInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTcpSocket>
#include <QTimer>

#include <netdb.h>

#include <algorithm>

Q_LOGGING_CATEGORY(lcSession, "krellixd.session")

namespace {

// Even malicious clients can send us at most this much before we drop
// them. We never need to read more than a hello byte.
constexpr qint64 kMaxClientSendBytes = 4 * 1024;
constexpr qint64 kStackProbeIntervalMs = 30 * 1000;
constexpr qint64 kHealthProbeIntervalMs = 60 * 1000;

struct StackProbe {
    int containers = 0;
    int running = 0;
    int images = 0;
    int compose = 0;
    int unhealthy = 0;
    bool dockerReadable = false;
};

struct HealthFinding {
    QString severity;
    QString title;
    QString detail;
};

struct TopProcess {
    int pid = 0;
    double cpu = 0.0;
    double mem = 0.0;
    qint64 rssKiB = 0;
    QString name;
    QString command;
};

QString runProgram(const QString &program, const QStringList &args, int timeoutMs = 1500)
{
    if (QStandardPaths::findExecutable(program).isEmpty())
        return {};
    QProcess proc;
    proc.setProgram(program);
    proc.setArguments(args);
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start();
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(200);
        return {};
    }
    if (proc.exitCode() != 0)
        return {};
    return QString::fromLocal8Bit(proc.readAll()).trimmed();
}

int nonEmptyLines(const QString &text)
{
    int count = 0;
    for (const QString &line : text.split(QLatin1Char('\n')))
        if (!line.trimmed().isEmpty())
            ++count;
    return count;
}

QStringList nonEmptyLineList(const QString &text)
{
    QStringList out;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
            out << trimmed;
    }
    return out;
}

void addFinding(QList<HealthFinding> &findings,
                const QString &severity,
                const QString &title,
                const QString &detail = {})
{
    findings << HealthFinding{severity, title, detail};
}

QJsonObject healthObject()
{
    static QJsonObject cachedHealth;
    static qint64 nextProbeMs = 0;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (nowMs < nextProbeMs)
        return cachedHealth;
    nextProbeMs = nowMs + kHealthProbeIntervalMs;

    QList<HealthFinding> findings;

    for (const QStorageInfo &storage : QStorageInfo::mountedVolumes()) {
        if (!storage.isValid() || !storage.isReady() || storage.isReadOnly())
            continue;
        const QString mount = storage.rootPath();
        if (mount.startsWith(QStringLiteral("/run")) || mount.startsWith(QStringLiteral("/proc"))
            || mount.startsWith(QStringLiteral("/sys")) || mount.startsWith(QStringLiteral("/dev")))
            continue;
        const qint64 total = storage.bytesTotal();
        if (total <= 0)
            continue;
        const double usedPct = 100.0 * double(total - storage.bytesAvailable()) / double(total);
        if (usedPct >= 95.0) {
            addFinding(findings, QStringLiteral("critical"), QStringLiteral("Filesystem critical"),
                       QStringLiteral("%1 is %2% full").arg(mount).arg(usedPct, 0, 'f', 0));
        } else if (usedPct >= 85.0) {
            addFinding(findings, QStringLiteral("warning"), QStringLiteral("Filesystem high"),
                       QStringLiteral("%1 is %2% full").arg(mount).arg(usedPct, 0, 'f', 0));
        }
    }

    const QString failedUnits = runProgram(QStringLiteral("systemctl"),
        {QStringLiteral("--failed"), QStringLiteral("--no-legend"), QStringLiteral("--plain")}, 2000);
    if (!failedUnits.isEmpty()) {
        addFinding(findings, QStringLiteral("critical"), QStringLiteral("Failed systemd units"),
                   nonEmptyLineList(failedUnits).mid(0, 3).join(QStringLiteral("; ")));
    }

    const QString mdstat = runProgram(QStringLiteral("sh"),
        {QStringLiteral("-c"), QStringLiteral("cat /proc/mdstat 2>/dev/null")}, 1000);
    if (mdstat.contains(QStringLiteral("[_")) || mdstat.contains(QStringLiteral("recovery"))
        || mdstat.contains(QStringLiteral("resync"))) {
        addFinding(findings, QStringLiteral("warning"), QStringLiteral("RAID attention"),
                   nonEmptyLineList(mdstat).join(QStringLiteral(" ")).left(180));
    }

    const QString zfs = runProgram(QStringLiteral("zpool"),
        {QStringLiteral("status"), QStringLiteral("-x")}, 2500);
    if (!zfs.isEmpty() && !zfs.contains(QStringLiteral("all pools are healthy"), Qt::CaseInsensitive)) {
        addFinding(findings, QStringLiteral("critical"), QStringLiteral("ZFS pool issue"),
                   nonEmptyLineList(zfs).join(QStringLiteral("; ")).left(180));
    }

    const QString btrfs = runProgram(QStringLiteral("btrfs"),
        {QStringLiteral("device"), QStringLiteral("stats"), QStringLiteral("/")}, 2500);
    if (QRegularExpression(QStringLiteral(":\\s*[1-9][0-9]*")).match(btrfs).hasMatch()) {
        addFinding(findings, QStringLiteral("warning"), QStringLiteral("Btrfs device errors"),
                   nonEmptyLineList(btrfs).join(QStringLiteral("; ")).left(180));
    }

    if (QFileInfo::exists(QStringLiteral("/var/run/reboot-required"))
        || QFileInfo::exists(QStringLiteral("/run/reboot-required"))) {
        addFinding(findings, QStringLiteral("warning"), QStringLiteral("Reboot required"));
    }

    const QString journal = runProgram(QStringLiteral("journalctl"),
        {QStringLiteral("-p"), QStringLiteral("err"), QStringLiteral("-b"),
         QStringLiteral("--no-pager"), QStringLiteral("-n"), QStringLiteral("10")}, 2500);
    if (!journal.isEmpty()) {
        addFinding(findings, QStringLiteral("warning"), QStringLiteral("Journal errors"),
                   nonEmptyLineList(journal).mid(0, 3).join(QStringLiteral("; ")).left(180));
    }

    const QString timeSync = runProgram(QStringLiteral("timedatectl"),
        {QStringLiteral("show"), QStringLiteral("-p"), QStringLiteral("NTPSynchronized"),
         QStringLiteral("-p"), QStringLiteral("SystemClockSynchronized"), QStringLiteral("--value")}, 1500);
    if (timeSync.contains(QStringLiteral("no"), Qt::CaseInsensitive)
        || timeSync.contains(QStringLiteral("false"), Qt::CaseInsensitive)) {
        addFinding(findings, QStringLiteral("warning"), QStringLiteral("Time sync off"));
    }

    if (!QStandardPaths::findExecutable(QStringLiteral("dpkg")).isEmpty()) {
        QProcess audit;
        audit.start(QStringLiteral("dpkg"), {QStringLiteral("--audit")});
        const bool finished = audit.waitForFinished(5000);
        if (!finished) { audit.kill(); audit.waitForFinished(200); }
        const QString result = QString::fromLocal8Bit(audit.readAllStandardOutput()).trimmed();
        if (!finished || audit.exitStatus() != QProcess::NormalExit || audit.exitCode() != 0)
            addFinding(findings, QStringLiteral("warning"), QStringLiteral("Package check unavailable"),
                       QStringLiteral("dpkg --audit could not complete"));
        else if (!result.isEmpty())
            addFinding(findings, QStringLiteral("warning"), QStringLiteral("Package configuration incomplete"),
                       nonEmptyLineList(result).mid(0, 3).join(QStringLiteral("; ")).left(180));
    } else {
        const QString pkgErrors = runProgram(QStringLiteral("sh"),
            {QStringLiteral("-c"), QStringLiteral("(grep -Ei 'error|failed|conflict' /var/log/pacman.log 2>/dev/null; grep -Ei 'error|failed|conflict' /var/log/dnf.log 2>/dev/null) | tail -n 5")}, 2500);
        if (!pkgErrors.isEmpty())
            addFinding(findings, QStringLiteral("warning"), QStringLiteral("Package log warnings"),
                       QStringLiteral("Historical log entries: ") + nonEmptyLineList(pkgErrors).mid(0, 3).join(QStringLiteral("; ")).left(180));
    }

    QJsonObject out;
    QJsonArray arr;
    int critical = 0;
    int warnings = 0;
    for (const HealthFinding &finding : findings) {
        if (finding.severity == QLatin1String("critical"))
            ++critical;
        else if (finding.severity == QLatin1String("warning"))
            ++warnings;
        QJsonObject f;
        f.insert(QStringLiteral("severity"), finding.severity);
        f.insert(QStringLiteral("title"), finding.title);
        if (!finding.detail.isEmpty())
            f.insert(QStringLiteral("detail"), finding.detail);
        arr.append(f);
    }
    out.insert(QStringLiteral("critical"), critical);
    out.insert(QStringLiteral("warnings"), warnings);
    out.insert(QStringLiteral("findings"), arr);
    cachedHealth = out;
    return out;
}

int composeCount(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || trimmed == QLatin1String("[]"))
        return 0;
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8());
    if (doc.isArray())
        return doc.array().size();
    if (doc.isObject())
        return 1;
    return nonEmptyLines(trimmed);
}

StackProbe readStackProbe()
{
    const QString containers = runProgram(
        QStringLiteral("docker"),
        {QStringLiteral("ps"), QStringLiteral("-a"), QStringLiteral("--format"),
         QStringLiteral("{{.Names}}|{{.Status}}|{{.Image}}|{{.Ports}}")});
    const QString images = runProgram(
        QStringLiteral("docker"),
        {QStringLiteral("images"), QStringLiteral("--format"),
         QStringLiteral("{{.Repository}}:{{.Tag}}|{{.Size}}")});
    const QString compose = runProgram(
        QStringLiteral("docker"),
        {QStringLiteral("compose"), QStringLiteral("ls"),
         QStringLiteral("--format"), QStringLiteral("json")});

    StackProbe probe;
    probe.images = nonEmptyLines(images);
    probe.compose = composeCount(compose);
    probe.dockerReadable = !containers.isEmpty() || !images.isEmpty() || !compose.isEmpty();

    for (const QString &line : containers.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;
        ++probe.containers;
        if (trimmed.contains(QStringLiteral("|Up "), Qt::CaseInsensitive)
            || trimmed.contains(QStringLiteral("|running"), Qt::CaseInsensitive))
            ++probe.running;
        if (trimmed.contains(QStringLiteral("unhealthy"), Qt::CaseInsensitive)
            || trimmed.contains(QStringLiteral("Restarting"), Qt::CaseInsensitive)
            || trimmed.contains(QStringLiteral("Exited"), Qt::CaseInsensitive)
            || trimmed.contains(QStringLiteral("Dead"), Qt::CaseInsensitive))
            ++probe.unhealthy;
    }
    return probe;
}

QJsonObject stackObject(const QList<NetSample> &netSamples)
{
    int dockerNetworks = 0;
    for (const NetSample &sample : netSamples) {
        if (sample.name == QLatin1String("docker0")
            || sample.name.startsWith(QStringLiteral("br-"))
            || sample.name.startsWith(QStringLiteral("veth"))
            || sample.alias.contains(QStringLiteral("Docker"), Qt::CaseInsensitive)
            || sample.alias.contains(QStringLiteral("_default"), Qt::CaseInsensitive))
            ++dockerNetworks;
    }

    static StackProbe cachedProbe;
    static qint64 nextProbeMs = 0;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (nowMs >= nextProbeMs) {
        cachedProbe = readStackProbe();
        nextProbeMs = nowMs + kStackProbeIntervalMs;
    }

    QJsonObject o;
    o.insert(QStringLiteral("source"), QStringLiteral("krellixd"));
    o.insert(QStringLiteral("containers"), cachedProbe.containers);
    o.insert(QStringLiteral("running"), cachedProbe.running);
    o.insert(QStringLiteral("images"), cachedProbe.images);
    o.insert(QStringLiteral("compose"), cachedProbe.compose);
    o.insert(QStringLiteral("unhealthy"), cachedProbe.unhealthy);
    o.insert(QStringLiteral("docker_networks"), dockerNetworks);
    if (!cachedProbe.dockerReadable && dockerNetworks > 0)
        o.insert(QStringLiteral("error"), QStringLiteral("docker not readable by krellixd"));
    return o;
}

QList<TopProcess> topN(QList<TopProcess> rows, bool byMemory, int limit)
{
    std::sort(rows.begin(), rows.end(), [byMemory](const TopProcess &a, const TopProcess &b) {
        if (byMemory) {
            if (a.rssKiB != b.rssKiB)
                return a.rssKiB > b.rssKiB;
            return a.mem > b.mem;
        }
        if (qAbs(a.cpu - b.cpu) > 0.05)
            return a.cpu > b.cpu;
        return a.rssKiB > b.rssKiB;
    });
    return rows.mid(0, limit);
}

QJsonArray topProcessArray()
{
    static QJsonArray cachedTop;
    static qint64 nextProbeMs = 0;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (nowMs < nextProbeMs)
        return cachedTop;
    nextProbeMs = nowMs + 5000;

    const QString output = runProgram(
        QStringLiteral("ps"),
        {QStringLiteral("-eo"),
         QStringLiteral("pid=,pcpu=,pmem=,rss=,comm=,args=")},
        2500);
    if (output.isEmpty()) {
        cachedTop = QJsonArray{};
        return cachedTop;
    }

    QList<TopProcess> rows;
    const QRegularExpression rx(QStringLiteral("^\\s*(\\d+)\\s+([0-9.]+)\\s+([0-9.]+)\\s+(\\d+)\\s+(\\S+)\\s*(.*)$"));
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch m = rx.match(line);
        if (!m.hasMatch())
            continue;
        TopProcess row;
        row.pid = m.captured(1).toInt();
        row.cpu = m.captured(2).toDouble();
        row.mem = m.captured(3).toDouble();
        row.rssKiB = m.captured(4).toLongLong();
        row.name = m.captured(5).trimmed();
        row.command = m.captured(6).trimmed();
        if (row.pid > 0 && !row.name.isEmpty())
            rows.append(row);
    }

    QHash<int, TopProcess> selected;
    for (const TopProcess &row : topN(rows, false, 12))
        selected.insert(row.pid, row);
    for (const TopProcess &row : topN(rows, true, 12))
        selected.insert(row.pid, row);

    QJsonArray arr;
    for (const TopProcess &row : selected) {
        QJsonObject p;
        p.insert(QStringLiteral("pid"), row.pid);
        p.insert(QStringLiteral("cpu"), row.cpu);
        p.insert(QStringLiteral("mem"), row.mem);
        p.insert(QStringLiteral("rss"), qint64(row.rssKiB));
        p.insert(QStringLiteral("name"), row.name.left(64));
        p.insert(QStringLiteral("cmd"), row.command.left(240));
        arr.append(p);
    }
    cachedTop = arr;
    return cachedTop;
}

bool usableDomainName(const QString &domain)
{
    if (domain.isEmpty()
        || domain == QLatin1String("localdomain")
        || domain.contains(QLatin1Char(' '))
        || !domain.contains(QLatin1Char('.'))) {
        return false;
    }
    return QHostAddress(domain).isNull();
}

QString resolveCanonicalHostname(const QString &host)
{
    struct addrinfo hints{};
    hints.ai_flags = AI_CANONNAME;
    hints.ai_family = AF_UNSPEC;
    struct addrinfo *result = nullptr;
    QString out;
    if (getaddrinfo(host.toUtf8().constData(), nullptr, &hints, &result) == 0
        && result && result->ai_canonname) {
        out = QString::fromUtf8(result->ai_canonname);
    }
    if (result) freeaddrinfo(result);
    return out;
}

QString daemonHostname()
{
    const QString host = QSysInfo::machineHostName();
    if (host.contains(QLatin1Char('.')))
        return host;

    const QString domain = QHostInfo::localDomainName().trimmed();
    if (usableDomainName(domain)) {
        return host + QLatin1Char('.') + domain;
    }

    const QString canonical = resolveCanonicalHostname(host);
    if (canonical.contains(QLatin1Char('.')))
        return canonical;

    return host;
}

QJsonObject helloObject(int intervalMs)
{
    QJsonObject o;
    o.insert(QStringLiteral("type"),        QStringLiteral("hello"));
    o.insert(QStringLiteral("version"),
             QString::fromUtf8(KRELLIX_VERSION));
    o.insert(QStringLiteral("hostname"),    daemonHostname());
    o.insert(QStringLiteral("kernel"),
             QSysInfo::kernelType() + QStringLiteral(" ") + QSysInfo::kernelVersion());
    o.insert(QStringLiteral("interval_ms"), intervalMs);
    QJsonArray mon;
    mon << QStringLiteral("cpu") << QStringLiteral("mem")
        << QStringLiteral("proc")
        << QStringLiteral("uptime") << QStringLiteral("net")
        << QStringLiteral("net_ports")
        << QStringLiteral("disk")
        << QStringLiteral("top")
        << QStringLiteral("health") << QStringLiteral("packages");
    mon << QStringLiteral("sensors") << QStringLiteral("battery") << QStringLiteral("clock");
    o.insert(QStringLiteral("monitors"), mon);
    return o;
}

QJsonObject sampleObject()
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("sample"));
    o.insert(QStringLiteral("datetime"), QDateTime::currentDateTime().toString(Qt::ISODate));
    QJsonArray sensors;
    for (const auto &s : SensorStat::read()) {
        sensors.append(QJsonObject{{QStringLiteral("chip"), s.chip},
            {QStringLiteral("label"), s.label}, {QStringLiteral("value"), s.value},
            {QStringLiteral("type"), int(s.type)}, {QStringLiteral("max"), s.maxC},
            {QStringLiteral("crit"), s.critC}});
    }
    o.insert(QStringLiteral("sensors"), sensors);
    const auto battery = BatteryStat::read();
    o.insert(QStringLiteral("battery"), QJsonObject{
        {QStringLiteral("present"), battery.present}, {QStringLiteral("percent"), battery.percent},
        {QStringLiteral("status"), battery.status}, {QStringLiteral("ac_online"), battery.acOnline},
        {QStringLiteral("remaining"), battery.timeRemainSec}, {QStringLiteral("name"), battery.batteryName}});
    o.insert(QStringLiteral("ts"),
             QDateTime::currentSecsSinceEpoch());
    o.insert(QStringLiteral("hostname"), daemonHostname());
    o.insert(QStringLiteral("kernel"),
             QSysInfo::kernelType() + QStringLiteral(" ") + QSysInfo::kernelVersion());

    o.insert(QStringLiteral("uptime"),
             qint64(UptimeStat::secondsSinceBoot()));

    QJsonArray cpuArr;
    for (const CpuSample &s : CpuStat::read()) {
        QJsonObject c;
        c.insert(QStringLiteral("name"),  s.name);
        c.insert(QStringLiteral("i"),     s.index);
        // Send effective user / nice (raw - guest / raw - guest_nice) so
        // clients running older krellix builds — which sum user+nice+...
        // straight from the wire without subtracting guest — display
        // correct CPU utilization on virtualization-aware kernels. New
        // clients can still recover the raw figures by adding guest /
        // guest_nice back if they need them.
        c.insert(QStringLiteral("user"),       qint64(s.effectiveUser()));
        c.insert(QStringLiteral("nice"),       qint64(s.effectiveNice()));
        c.insert(QStringLiteral("sys"),        qint64(s.sys));
        c.insert(QStringLiteral("idle"),       qint64(s.idle));
        c.insert(QStringLiteral("iowait"),     qint64(s.iowait));
        c.insert(QStringLiteral("irq"),        qint64(s.irq));
        c.insert(QStringLiteral("softirq"),    qint64(s.softirq));
        c.insert(QStringLiteral("steal"),      qint64(s.steal));
        c.insert(QStringLiteral("guest"),      qint64(s.guest));
        c.insert(QStringLiteral("guest_nice"), qint64(s.guestNice));
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

    const QList<NetSample> netSamples = NetStat::read();
    QJsonArray netArr;
    for (const NetSample &s : netSamples) {
        QJsonObject n;
        n.insert(QStringLiteral("name"), s.name);
        if (!s.alias.isEmpty())
            n.insert(QStringLiteral("alias"), s.alias);
        n.insert(QStringLiteral("rx"),   qint64(s.rxBytes));
        n.insert(QStringLiteral("tx"),   qint64(s.txBytes));
        n.insert(QStringLiteral("rxp"),  qint64(s.rxPackets));
        n.insert(QStringLiteral("txp"),  qint64(s.txPackets));
        netArr.append(n);
    }
    o.insert(QStringLiteral("net"), netArr);
    o.insert(QStringLiteral("stack"), stackObject(netSamples));

    QJsonArray netPortArr;
    for (const NetPortSample &s : NetPortStat::read()) {
        if (s.protocol == QLatin1String("tcp")) {
            if (s.state != QLatin1String("01"))
                continue;
            if (s.remoteAddress.isEmpty() || s.remotePort == 0)
                continue;
        }

        QJsonObject n;
        n.insert(QStringLiteral("proto"), s.protocol);
        n.insert(QStringLiteral("local"), int(s.localPort));
        if (s.remotePort > 0)
            n.insert(QStringLiteral("remote"), int(s.remotePort));
        if (!s.remoteAddress.isEmpty())
            n.insert(QStringLiteral("remote_addr"), s.remoteAddress);
        if (!s.state.isEmpty())
            n.insert(QStringLiteral("state"), s.state);
        netPortArr.append(n);
    }
    o.insert(QStringLiteral("net_ports"), netPortArr);

    QJsonArray diskArr;
    for (const DiskSample &s : DiskStat::read()) {
        QJsonObject d;
        d.insert(QStringLiteral("name"), s.name);
        d.insert(QStringLiteral("sr"),   qint64(s.sectorsRead));
        d.insert(QStringLiteral("sw"),   qint64(s.sectorsWritten));
        diskArr.append(d);
    }
    o.insert(QStringLiteral("disk"), diskArr);

    const ProcInfo p = ProcStat::read();
    QJsonObject proc;
    proc.insert(QStringLiteral("processes"), p.processes);
    proc.insert(QStringLiteral("users"),     p.users);
    o.insert(QStringLiteral("proc"), proc);
    o.insert(QStringLiteral("packages"), packageUpdateObject());
    o.insert(QStringLiteral("health"), healthObject());
    o.insert(QStringLiteral("top"), topProcessArray());

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
    // Reset the idle clock whenever the kernel actually flushes bytes to
    // the peer. The protocol is one-way (we push samples, the client
    // doesn't reply), so without this the idle timer would always fire
    // and we'd kick perfectly healthy clients every io_timeout window.
    // Going through bytesWritten (rather than just our own send tick)
    // means a client that's TCP-alive but not draining our writes WILL
    // still be kicked: their kernel buffer fills, no bytesWritten fires,
    // idle timer expires.
    connect(m_socket, &QTcpSocket::bytesWritten, this, [this](qint64) {
        if (m_idleTimer) m_idleTimer->start();
    });

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
