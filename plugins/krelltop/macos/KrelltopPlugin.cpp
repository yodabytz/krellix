#include "monitors/MonitorBase.h"
#include "remote/RemoteSource.h"
#include "sdk/KrellixPlugin.h"
#include "widgets/Decal.h"
#include "widgets/Panel.h"

#include <QFileInfo>
#include <QLoggingCategory>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {

Q_LOGGING_CATEGORY(lcTop, "krellix.krelltop", QtInfoMsg)

constexpr int visibleRows = 3;
constexpr int tooltipRows = 6;

QString percent(double value)
{
    return QString::number(value, 'f', value >= 10.0 ? 0 : 1) + QLatin1Char('%');
}

QString processName(const QString &name)
{
    const QString base = QFileInfo(name).fileName();
    if (base.startsWith(QLatin1Char('[')) && base.endsWith(QLatin1Char(']')))
        return base;
    const auto dot = base.indexOf(QLatin1Char('.'), base.startsWith(QLatin1Char('.')) ? 1 : 0);
    return dot > 0 ? base.left(dot) : base;
}

QString rss(qint64 kib)
{
    if (kib >= 1024 * 1024)
        return QStringLiteral("%1 GiB").arg(kib / 1048576.0, 0, 'f', 1);
    if (kib >= 1024)
        return QStringLiteral("%1 MiB").arg(kib / 1024.0, 0, 'f', 0);
    return QStringLiteral("%1 KiB").arg(kib);
}

void appendRanking(QStringList &lines, const QString &title, const QList<TopProcessInfo> &rows)
{
    lines << title;
    for (qsizetype i = 0; i < qMin(rows.size(), qsizetype(tooltipRows)); ++i) {
        const auto &row = rows.at(i);
        lines << QStringLiteral("%1. %2  pid %3  cpu %4  mem %5 (%6)")
                     .arg(i + 1).arg(processName(row.name).left(48)).arg(row.pid)
                     .arg(percent(row.cpu), percent(row.mem), rss(row.rssKiB));
    }
}

QList<TopProcessInfo> parseProcesses(const QByteArray &output)
{
    static const QRegularExpression rowPattern(
        QStringLiteral(R"(^\s*(\d+)\s+([0-9.]+)\s+([0-9.]+)\s+(\d+)\s+(\S.*)$)"));
    QList<TopProcessInfo> rows;
    for (const auto &line : QString::fromLocal8Bit(output).split(QLatin1Char('\n'))) {
        const auto match = rowPattern.match(line);
        if (!match.hasMatch())
            continue;
        bool cpuOk = false;
        bool memOk = false;
        const double cpu = match.captured(2).toDouble(&cpuOk);
        const double mem = match.captured(3).toDouble(&memOk);
        const int pid = match.captured(1).toInt();
        if (pid > 0 && cpuOk && memOk && std::isfinite(cpu) && std::isfinite(mem))
            rows.append({pid, cpu, mem, match.captured(4).toLongLong(), match.captured(5).trimmed(), {}});
    }
    return rows;
}

class KrelltopMonitor final : public MonitorBase
{
public:
    using MonitorBase::MonitorBase;

    QString id() const override { return QStringLiteral("krelltop"); }
    QString displayName() const override { return QStringLiteral("KrellTop"); }
    int tickIntervalMs() const override { return 5000; }

    QWidget *createWidget(QWidget *parent) override
    {
        auto *panel = new Panel(theme(), parent);
        panel->setSurfaceKey(QStringLiteral("panel_bg_krelltop"));
        panel->setTitle(QStringLiteral("Top"));
        m_panel = panel;
        m_decals.clear();
        for (int i = 0; i < visibleRows; ++i) {
            auto *row = panel->addDecal(i == 0 ? QStringLiteral("value") : QStringLiteral("label"),
                                       i == 0 ? QStringLiteral("text_primary") : QStringLiteral("text_secondary"));
            row->setAlignment(Qt::AlignHCenter);
            m_decals.append(row);
        }
        showStatus(QStringLiteral("Waiting for process data"));
        tick();
        return panel;
    }

    void tick() override
    {
        if (m_stopped || !m_panel)
            return;
        if (auto *remote = RemoteSource::instance()) {
            if (m_remote != remote) {
                m_remote = remote;
                connect(remote, &RemoteSource::connectionStateChanged, this, [this] { tick(); });
            }
            if (!remote->isConnected()) {
                showStatus(QStringLiteral("Remote disconnected"));
                return;
            }
            showRows(remote->topProcesses(), true);
            return;
        }
        startSample();
    }

    void shutdown() override
    {
        m_stopped = true;
        if (m_deadline)
            m_deadline->stop();
        if (m_process && m_process->state() != QProcess::NotRunning)
            m_process->kill();
    }

private:
    void startSample()
    {
        if (!m_process) {
            m_process = new QProcess(this);
            m_process->setProgram(QStringLiteral("/bin/ps"));
            m_process->setArguments({QStringLiteral("-Aww"), QStringLiteral("-o"), QStringLiteral("pid="),
                                     QStringLiteral("-o"), QStringLiteral("%cpu="), QStringLiteral("-o"),
                                     QStringLiteral("%mem="), QStringLiteral("-o"), QStringLiteral("rss="),
                                     QStringLiteral("-o"), QStringLiteral("comm=")});
            auto env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
            m_process->setProcessEnvironment(env);
            m_process->setStandardErrorFile(QProcess::nullDevice());
            m_deadline = new QTimer(this);
            m_deadline->setSingleShot(true);
            connect(m_deadline, &QTimer::timeout, this, [this] { m_process->kill(); });
            connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    m_deadline->stop();
                    if (!m_stopped && !RemoteSource::instance())
                        showStatus(QStringLiteral("Could not start process sampler"));
                }
            });
            connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
                m_deadline->stop();
                const auto output = m_process->readAllStandardOutput();
                if (m_stopped || RemoteSource::instance())
                    return;
                if (code != 0 || status != QProcess::NormalExit) {
                    showStatus(QStringLiteral("Process sample failed"));
                    return;
                }
                showRows(parseProcesses(output), false);
            });
        }
        if (m_process->state() == QProcess::NotRunning) {
            m_deadline->start(2000);
            m_process->start();
        }
    }

    void showStatus(const QString &message)
    {
        for (qsizetype i = 0; i < m_decals.size(); ++i) {
            if (m_decals.at(i))
                m_decals.at(i)->setText(i == 0 ? message : QString());
        }
        if (m_panel)
            m_panel->setToolTip(message);
    }

    void showRows(QList<TopProcessInfo> rows, bool remote)
    {
        rows.removeIf([](const auto &row) {
            return row.pid <= 0 || processName(row.name).isEmpty() || row.rssKiB < 0
                || !std::isfinite(row.cpu) || row.cpu < 0 || !std::isfinite(row.mem) || row.mem < 0;
        });
        if (rows.isEmpty()) {
            showStatus(remote ? QStringLiteral("No remote process data") : QStringLiteral("No local process data"));
            return;
        }
        std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
            if (a.cpu != b.cpu)
                return a.cpu > b.cpu;
            return a.rssKiB != b.rssKiB ? a.rssKiB > b.rssKiB : a.pid < b.pid;
        });

        for (qsizetype i = 0; i < m_decals.size(); ++i) {
            if (!m_decals.at(i))
                continue;
            m_decals.at(i)->setText(i >= rows.size() ? QString() : QStringLiteral("%1 %2 %3")
                .arg(i + 1).arg(processName(rows.at(i).name).left(13), percent(rows.at(i).cpu)));
        }

        QStringList lines;
        appendRanking(lines, QStringLiteral("Top CPU"), rows);
        std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
            return a.rssKiB != b.rssKiB ? a.rssKiB > b.rssKiB : a.pid < b.pid;
        });
        lines << QString();
        appendRanking(lines, QStringLiteral("Top memory"), rows);
        if (m_panel) {
            m_panel->setToolTip(QStringLiteral("<pre>%1</pre>").arg(lines.join(QLatin1Char('\n')).toHtmlEscaped()));
            qCDebug(lcTop, "%s process rows displayed: %lld", remote ? "Remote" : "Local",
                    static_cast<long long>(qMin(rows.size(), qsizetype(visibleRows))));
        }
    }

    QPointer<Panel> m_panel;
    QList<QPointer<Decal>> m_decals;
    QPointer<RemoteSource> m_remote;
    QProcess *m_process = nullptr;
    QTimer *m_deadline = nullptr;
    bool m_stopped = false;
};

} // namespace

class KrelltopPlugin final : public QObject, public IKrellixPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID KrellixPlugin_iid)
    Q_INTERFACES(IKrellixPlugin)

public:
    QString pluginId() const override { return QStringLiteral("io.krellix.krelltop"); }
    QString pluginName() const override { return QStringLiteral("KrellTop"); }
    QString pluginVersion() const override { return QStringLiteral("0.1.2"); }

    QList<MonitorBase *> createMonitors(Theme *theme, QObject *parent) override
    {
        if (!QSettings().value(QStringLiteral("plugins/krelltop/enabled"), false).toBool())
            return {};
        return {new KrelltopMonitor(theme, parent)};
    }
};

#include "KrelltopPlugin.moc"
