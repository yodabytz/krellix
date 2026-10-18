#include "KrellstackPlugin.h"

#include "remote/RemoteSource.h"
#include "widgets/Decal.h"
#include "widgets/Panel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

namespace {

struct RuntimeStats {
    int containers = 0;
    int running = 0;
    int images = 0;
    int compose = 0;
    int podman = 0;
    int pods = 0;
    int unhealthy = 0;
    int sources = 0;
    int failures = 0;
    QString firstFailure;
};

struct CommandResult {
    QString output;
    QString error;
    int exitCode = -1;
    bool executableMissing = false;
    bool timedOut = false;
};

CommandResult runCommand(const QString &program, const QStringList &args, int timeoutMs = 3000)
{
    CommandResult result;
    if (QStandardPaths::findExecutable(program).isEmpty())
    {
        result.executableMissing = true;
        result.error = QStringLiteral("%1 not installed").arg(program);
        return result;
    }
    QProcess proc;
    proc.setProgram(program);
    proc.setArguments(args);
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start();
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(500);
        result.timedOut = true;
        result.error = QStringLiteral("%1 timed out").arg(program);
        return result;
    }
    result.exitCode = proc.exitCode();
    result.output = QString::fromLocal8Bit(proc.readAll()).trimmed();
    if (result.exitCode != 0)
        result.error = result.output.isEmpty()
            ? QStringLiteral("%1 failed").arg(program)
            : result.output.section(QLatin1Char('\n'), 0, 0).trimmed();
    return result;
}

CommandResult runShell(const QString &command, int timeoutMs = 3000)
{
    return runCommand(QStringLiteral("sh"), {QStringLiteral("-lc"), command}, timeoutMs);
}

CommandResult runSsh(const QString &host, const QString &command, int timeoutMs = 4500)
{
    return runCommand(QStringLiteral("ssh"),
                      {QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
                       QStringLiteral("-o"), QStringLiteral("ConnectTimeout=3"),
                       host, command},
                      timeoutMs);
}

int lines(const QString &text)
{
    int n = 0;
    for (const QString &line : text.split(QLatin1Char('\n')))
        if (!line.trimmed().isEmpty()) ++n;
    return n;
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

    int count = 0;
    for (const QString &line : trimmed.split(QLatin1Char('\n'))) {
        const QString item = line.trimmed();
        if (!item.isEmpty() && item != QLatin1String("[]"))
            ++count;
    }
    return count;
}

QStringList configuredHosts()
{
    const QString raw =
        QSettings().value(QStringLiteral("plugins/krellstack/hosts")).toString();
    QStringList hosts;
    for (const QString &part : raw.split(QRegularExpression(QStringLiteral("[,;\\n]")),
                                         Qt::SkipEmptyParts)) {
        const QString host = part.trimmed();
        if (!host.isEmpty() && !hosts.contains(host))
            hosts << host;
    }
    return hosts;
}

QString stackProbeCommand()
{
    return QStringLiteral(
        "printf '__KRELLSTACK_CONTAINERS__\\n'; "
        "docker ps -a --format '{{.Names}}|{{.Status}}|{{.Image}}|{{.Ports}}' 2>/dev/null || true; "
        "printf '\\n__KRELLSTACK_IMAGES__\\n'; "
        "docker images --format '{{.Repository}}:{{.Tag}}|{{.Size}}' 2>/dev/null || true; "
        "printf '\\n__KRELLSTACK_COMPOSE__\\n'; "
        "docker compose ls --format json 2>/dev/null || true");
}

QString section(const QString &text, const QString &marker, const QString &nextMarker)
{
    const int start = text.indexOf(marker);
    if (start < 0)
        return {};
    const int bodyStart = start + marker.size();
    const int end = nextMarker.isEmpty() ? -1 : text.indexOf(nextMarker, bodyStart);
    return text.mid(bodyStart, end < 0 ? -1 : end - bodyStart).trimmed();
}

void addDockerStats(RuntimeStats &stats,
                    const QString &source,
                    const QString &containers,
                    const QString &images,
                    const QString &compose)
{
    Q_UNUSED(source);
    ++stats.sources;
    stats.images += lines(images);
    stats.compose += composeCount(compose);
    for (const QString &line : containers.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        ++stats.containers;
        if (trimmed.contains(QStringLiteral("|Up "), Qt::CaseInsensitive)
            || trimmed.contains(QStringLiteral("|running"), Qt::CaseInsensitive))
            ++stats.running;
        if (trimmed.contains(QStringLiteral("unhealthy"), Qt::CaseInsensitive)
            || trimmed.contains(QStringLiteral("Restarting"), Qt::CaseInsensitive)
            || trimmed.contains(QStringLiteral("Exited"), Qt::CaseInsensitive)
            || trimmed.contains(QStringLiteral("Dead"), Qt::CaseInsensitive))
            ++stats.unhealthy;
    }
}

void addFailure(RuntimeStats &stats, const QString &source, const QString &error)
{
    ++stats.failures;
    if (stats.firstFailure.isEmpty())
        stats.firstFailure = QStringLiteral("%1: %2").arg(source, error);
}

} // namespace

KrellstackMonitor::KrellstackMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
}

QWidget *KrellstackMonitor::createWidget(QWidget *parent)
{
    auto *panel = new Panel(theme(), parent);
    panel->setSurfaceKey(QStringLiteral("panel_bg_krellstack"));
    panel->setTitle(QStringLiteral("Stack"));
    m_summary = panel->addDecal(QStringLiteral("value"), QStringLiteral("text_primary"));
    m_summary->setAlignment(Qt::AlignHCenter);
    m_detail = panel->addDecal(QStringLiteral("label"), QStringLiteral("text_secondary"));
    m_detail->setAlignment(Qt::AlignHCenter);
    tick();
    return panel;
}

void KrellstackMonitor::tick()
{
    RuntimeStats stats;
    const QStringList hosts = configuredHosts();
    if (hosts.isEmpty() || RemoteSource::instance()) {
        if (auto *remote = RemoteSource::instance(); remote) {
            const StackInfo stack = remote->stackInfo();
            const int total = stack.containers;
            if (m_summary) {
                if (!stack.error.isEmpty())
                    m_summary->setText(QStringLiteral("docker blocked"));
                else if (total == 0 && stack.images == 0 && stack.compose == 0)
                    m_summary->setText(stack.dockerNetworks > 0
                        ? QStringLiteral("%1 docker nets").arg(stack.dockerNetworks)
                        : QStringLiteral("no containers"));
                else
                    m_summary->setText(QStringLiteral("%1/%2 ctr  %3 img")
                        .arg(stack.running).arg(total).arg(stack.images));
            }
            if (m_detail) {
                if (!stack.error.isEmpty())
                    m_detail->setText(stack.error.left(44));
                else
                    m_detail->setText(QStringLiteral("compose %1  nets %2  bad %3")
                        .arg(stack.compose).arg(stack.dockerNetworks).arg(stack.unhealthy));
            }
            return;
        }
    }

    if (hosts.isEmpty() || RemoteSource::instance()) {
        const CommandResult result = runShell(stackProbeCommand());
        if (result.exitCode == 0) {
            addDockerStats(stats,
                           QStringLiteral("local"),
                           section(result.output,
                                   QStringLiteral("__KRELLSTACK_CONTAINERS__"),
                                   QStringLiteral("__KRELLSTACK_IMAGES__")),
                           section(result.output,
                                   QStringLiteral("__KRELLSTACK_IMAGES__"),
                                   QStringLiteral("__KRELLSTACK_COMPOSE__")),
                           section(result.output,
                                   QStringLiteral("__KRELLSTACK_COMPOSE__"),
                                   QString()));
        } else {
            addFailure(stats, QStringLiteral("local"), result.error);
        }
    } else {
        for (const QString &host : hosts) {
            const CommandResult result = runSsh(host, stackProbeCommand());
            if (result.exitCode == 0) {
                addDockerStats(stats,
                               host,
                               section(result.output,
                                       QStringLiteral("__KRELLSTACK_CONTAINERS__"),
                                       QStringLiteral("__KRELLSTACK_IMAGES__")),
                               section(result.output,
                                       QStringLiteral("__KRELLSTACK_IMAGES__"),
                                       QStringLiteral("__KRELLSTACK_COMPOSE__")),
                               section(result.output,
                                       QStringLiteral("__KRELLSTACK_COMPOSE__"),
                                       QString()));
            } else {
                addFailure(stats, host, result.error);
            }
        }
    }

    if (hosts.isEmpty() || RemoteSource::instance()) {
        const CommandResult podman = runCommand(QStringLiteral("podman"),
                                                {QStringLiteral("ps"), QStringLiteral("-a"), QStringLiteral("--format"),
                                                 QStringLiteral("{{.Names}}|{{.Status}}|{{.Image}}")});
        const CommandResult pods = runCommand(QStringLiteral("kubectl"),
                                              {QStringLiteral("get"), QStringLiteral("pods"),
                                               QStringLiteral("--no-headers")});
        if (!podman.executableMissing) {
            stats.podman = lines(podman.output);
            for (const QString &line : podman.output.split(QLatin1Char('\n')))
                if (line.contains(QStringLiteral("unhealthy"), Qt::CaseInsensitive)
                    || line.contains(QStringLiteral("Restarting"), Qt::CaseInsensitive))
                    ++stats.unhealthy;
        }
        if (!pods.executableMissing)
            stats.pods = lines(pods.output);
    }

    const int totalRuntimes = stats.containers + stats.podman + stats.pods;

    if (m_summary)
        m_summary->setText(totalRuntimes == 0 && stats.images == 0 && stats.compose == 0
            ? (stats.failures > 0 ? QStringLiteral("stack check failed")
                                  : QStringLiteral("no containers"))
            : QStringLiteral("%1/%2 ctr  %3 img")
                  .arg(stats.running)
                  .arg(totalRuntimes)
                  .arg(stats.images));
    if (m_detail) {
        QString detail = QStringLiteral("compose %1  bad %2").arg(stats.compose).arg(stats.unhealthy);
        if (hosts.isEmpty())
            detail += QStringLiteral("  local");
        else
            detail += QStringLiteral("  hosts %1/%2").arg(stats.sources).arg(hosts.size());
        if (stats.failures > 0)
            detail = QStringLiteral("%1 fail  %2").arg(stats.failures).arg(stats.firstFailure.left(42));
        m_detail->setText(detail);
    }
}

QList<MonitorBase *> KrellstackPlugin::createMonitors(Theme *theme, QObject *parent)
{
    if (!QSettings().value(QStringLiteral("plugins/krellstack/enabled"), false).toBool())
        return {};
    return {new KrellstackMonitor(theme, parent)};
}
