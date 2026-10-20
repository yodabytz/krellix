#include "KrellhealthPlugin.h"

#include "remote/RemoteSource.h"
#include "theme/Theme.h"
#include "widgets/Decal.h"
#include "widgets/Panel.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <QWidget>

namespace {

bool haveCommand(const QString &program)
{
    return !QStandardPaths::findExecutable(program).isEmpty();
}

QStringList lines(const QString &text)
{
    QStringList out;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
            out << trimmed;
    }
    return out;
}

bool fileExists(const QString &path)
{
    return QFileInfo::exists(path);
}

QString newestKernelModuleDir()
{
    QDir modules(QStringLiteral("/usr/lib/modules"));
    QString newest;
    QDateTime newestTime;
    for (const QFileInfo &entry : modules.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!newestTime.isValid() || entry.lastModified() > newestTime) {
            newestTime = entry.lastModified();
            newest = entry.fileName();
        }
    }
    return newest;
}

QStringList certificateFiles()
{
    QStringList paths;
    const QStringList roots = {
        QStringLiteral("/etc/letsencrypt/live"),
        QStringLiteral("/etc/ssl"),
        QStringLiteral("/usr/local/etc/letsencrypt/live"),
    };
    for (const QString &root : roots) {
        if (!QFileInfo::exists(root))
            continue;
        QDirIterator it(root, {QStringLiteral("fullchain.pem"), QStringLiteral("cert.pem")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            paths << it.next();
    }
    paths.removeDuplicates();
    return paths.mid(0, 8);
}

QStringList blockDevicesForSmart()
{
    QStringList devices;
    const QDir dev(QStringLiteral("/dev"));
    for (const QFileInfo &entry : dev.entryInfoList(QDir::System | QDir::Files)) {
        const QString name = entry.fileName();
        if (QRegularExpression(QStringLiteral("^(sd[a-z]|nvme[0-9]+n[0-9]+|vd[a-z])$")).match(name).hasMatch())
            devices << entry.absoluteFilePath();
    }
    return devices.mid(0, 8);
}

} // namespace

class HealthStatusBadge final : public QWidget
{
    Q_OBJECT

public:
    explicit HealthStatusBadge(Theme *theme, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_theme(theme)
    {
        Q_ASSERT(m_theme);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(m_theme, &Theme::themeChanged, this, [this]() {
            updateGeometry();
            update();
        });
    }

    void setStatus(const QString &icon, const QString &text, const QString &colorKey)
    {
        if (m_icon == icon && m_text == text && m_colorKey == colorKey)
            return;
        m_icon = icon;
        m_text = text;
        m_colorKey = colorKey;
        updateGeometry();
        update();
    }

    QSize sizeHint() const override
    {
        const QFont textFont = m_theme->font(QStringLiteral("label"));
        QFont iconFont = textFont;
        iconFont.setPointSize(qMax(textFont.pointSize() + 6, 15));
        iconFont.setBold(true);

        const QFontMetrics ifm(iconFont);
        const QFontMetrics tfm(textFont);
        return QSize(ifm.horizontalAdvance(m_icon) + tfm.horizontalAdvance(m_text) + 12,
                     qMax(ifm.height(), tfm.height()) + 2);
    }

    QSize minimumSizeHint() const override
    {
        return QSize(0, sizeHint().height());
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::TextAntialiasing, true);

        const Theme::TextStyle ts = m_theme->textStyle(m_colorKey);
        const QColor fg = ts.color.isValid() ? ts.color : QColor(Qt::white);
        const QColor text = m_theme->color(QStringLiteral("text_primary"), fg);

        QFont textFont = m_theme->font(QStringLiteral("label"));
        QFont iconFont = textFont;
        iconFont.setPointSize(qMax(textFont.pointSize() + 6, 15));
        iconFont.setBold(true);

        const QRect r = rect();
        const QFontMetrics ifm(iconFont);
        const QFontMetrics tfm(textFont);
        const int iconW = ifm.horizontalAdvance(m_icon);
        const int textW = tfm.horizontalAdvance(m_text);
        const int totalW = iconW + 5 + textW;
        const int x = qMax(0, (r.width() - totalW) / 2);

        p.setFont(iconFont);
        p.setPen(fg);
        const int iconY = (r.height() + ifm.ascent() - ifm.descent()) / 2;
        p.drawText(x, iconY, m_icon);

        p.setFont(textFont);
        p.setPen(text);
        const int textY = (r.height() + tfm.ascent() - tfm.descent()) / 2;
        p.drawText(x + iconW + 5, textY, m_text);
    }

private:
    Theme *m_theme = nullptr;
    QString m_icon = QStringLiteral("♥");
    QString m_text = QStringLiteral("OK");
    QString m_colorKey = QStringLiteral("accent_critical");
};

KrellhealthMonitor::KrellhealthMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
}

QWidget *KrellhealthMonitor::createWidget(QWidget *parent)
{
    auto *panel = new Panel(theme(), parent);
    panel->setSurfaceKey(QStringLiteral("panel_bg_krellhealth"));
    panel->setTitle(QStringLiteral("Health"));
    m_panel = panel;
    m_summary = new HealthStatusBadge(theme(), panel);
    panel->addWidget(m_summary);
    m_detail = panel->addDecal(QStringLiteral("label"), QStringLiteral("text_secondary"));
    m_detail->setAlignment(Qt::AlignHCenter);
    if (m_summary)
        m_summary->setStatus(QStringLiteral("?"), QStringLiteral("CHECK"), QStringLiteral("text_secondary"));
    QTimer::singleShot(0, this, &KrellhealthMonitor::tick);
    return panel;
}

void KrellhealthMonitor::tick()
{
    if (m_updating)
        return;

    if (RemoteSource *remote = RemoteSource::instance(); remote) {
        m_findings.clear();
        const HealthInfo health = remote->healthInfo();
        for (const HealthFindingInfo &finding : health.findings) {
            Severity severity = Severity::Info;
            if (finding.severity == QLatin1String("critical"))
                severity = Severity::Critical;
            else if (finding.severity == QLatin1String("warning"))
                severity = Severity::Warn;
            addFinding(severity, finding.title, finding.detail);
        }
        updateDisplay();
        return;
    }

    m_updating = true;
    m_findings.clear();
    m_pendingJobs.clear();
    addLocalChecks();

    if (haveCommand(QStringLiteral("systemctl")))
        m_pendingJobs << CheckJob{QStringLiteral("systemd"), QStringLiteral("systemd"),
                                  QStringLiteral("systemctl"),
                                  {QStringLiteral("--failed"), QStringLiteral("--no-legend"),
                                   QStringLiteral("--plain")}, 2500};
    if (haveCommand(QStringLiteral("zpool")))
        m_pendingJobs << CheckJob{QStringLiteral("zfs"), QStringLiteral("ZFS"),
                                  QStringLiteral("zpool"), {QStringLiteral("status"), QStringLiteral("-x")}, 3000};
    if (haveCommand(QStringLiteral("btrfs")))
        m_pendingJobs << CheckJob{QStringLiteral("btrfs"), QStringLiteral("Btrfs"),
                                  QStringLiteral("btrfs"), {QStringLiteral("device"), QStringLiteral("stats"), QStringLiteral("/")}, 3000};
    if (haveCommand(QStringLiteral("journalctl")))
        m_pendingJobs << CheckJob{QStringLiteral("journal"), QStringLiteral("journal"),
                                  QStringLiteral("journalctl"), {QStringLiteral("-p"), QStringLiteral("err"),
                                  QStringLiteral("-b"), QStringLiteral("--no-pager"), QStringLiteral("-n"), QStringLiteral("10")}, 3000};
    if (haveCommand(QStringLiteral("timedatectl")))
        m_pendingJobs << CheckJob{QStringLiteral("timesync"), QStringLiteral("time sync"),
                                  QStringLiteral("timedatectl"), {QStringLiteral("show"),
                                  QStringLiteral("-p"), QStringLiteral("NTPSynchronized"),
                                  QStringLiteral("-p"), QStringLiteral("SystemClockSynchronized"),
                                  QStringLiteral("--value")}, 2000};
    if (haveCommand(QStringLiteral("pacman")))
        m_pendingJobs << CheckJob{QStringLiteral("pacmanlog"), QStringLiteral("pacman"),
                                  QStringLiteral("sh"), {QStringLiteral("-c"),
                                  QStringLiteral("grep -Ei 'error|failed|conflict' /var/log/pacman.log 2>/dev/null | tail -n 5")}, 2500};
    if (haveCommand(QStringLiteral("dpkg")))
        m_pendingJobs << CheckJob{QStringLiteral("dpkg-audit"), QStringLiteral("dpkg"),
                                  QStringLiteral("dpkg"), {QStringLiteral("--audit")}, 5000};
    if (haveCommand(QStringLiteral("dnf")))
        m_pendingJobs << CheckJob{QStringLiteral("dnflog"), QStringLiteral("dnf"),
                                  QStringLiteral("sh"), {QStringLiteral("-c"),
                                  QStringLiteral("grep -Ei 'error|failed|conflict' /var/log/dnf.log 2>/dev/null | tail -n 5")}, 2500};

    if (haveCommand(QStringLiteral("smartctl"))) {
        for (const QString &device : blockDevicesForSmart()) {
            m_pendingJobs << CheckJob{QStringLiteral("smart"), QStringLiteral("SMART"),
                                      QStringLiteral("smartctl"), {QStringLiteral("-H"), device}, 3500};
        }
    }
    if (haveCommand(QStringLiteral("openssl"))) {
        for (const QString &cert : certificateFiles()) {
            m_pendingJobs << CheckJob{QStringLiteral("cert"), QStringLiteral("cert"),
                                      QStringLiteral("openssl"), {QStringLiteral("x509"), QStringLiteral("-checkend"),
                                      QStringLiteral("1209600"), QStringLiteral("-noout"), QStringLiteral("-in"), cert}, 2500};
        }
    }

    if (m_pendingJobs.isEmpty()) {
        m_updating = false;
        updateDisplay();
        return;
    }

    startNextJob();
}

void KrellhealthMonitor::addLocalChecks()
{
    for (const QStorageInfo &storage : QStorageInfo::mountedVolumes()) {
        if (!storage.isValid() || !storage.isReady() || storage.isReadOnly())
            continue;
        const QByteArray mount = storage.rootPath().toLocal8Bit();
        if (!mount.startsWith('/') || mount.startsWith("/run") || mount.startsWith("/proc")
            || mount.startsWith("/sys") || mount.startsWith("/dev"))
            continue;
        const qint64 bytesTotal = storage.bytesTotal();
        const qint64 bytesFree = storage.bytesAvailable();
        if (bytesTotal <= 0)
            continue;
        const double usedPct = 100.0 * double(bytesTotal - bytesFree) / double(bytesTotal);
        if (usedPct >= 95.0) {
            addFinding(Severity::Critical, QStringLiteral("Filesystem critical"),
                       QStringLiteral("%1 is %2% full").arg(storage.rootPath()).arg(usedPct, 0, 'f', 0));
        } else if (usedPct >= 85.0) {
            addFinding(Severity::Warn, QStringLiteral("Filesystem high"),
                       QStringLiteral("%1 is %2% full").arg(storage.rootPath()).arg(usedPct, 0, 'f', 0));
        }
    }

    QFile mdstat(QStringLiteral("/proc/mdstat"));
    if (mdstat.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString text = QString::fromLocal8Bit(mdstat.readAll());
        if (text.contains(QStringLiteral("[_")) || text.contains(QStringLiteral("recovery"))
            || text.contains(QStringLiteral("resync"))) {
            addFinding(Severity::Warn, QStringLiteral("RAID attention"),
                       lines(text).join(QStringLiteral(" ")).left(180));
        }
    }

    if (fileExists(QStringLiteral("/var/run/reboot-required"))
        || fileExists(QStringLiteral("/run/reboot-required"))
        || fileExists(QStringLiteral("/usr/lib/update-notifier/reboot-required"))) {
        addFinding(Severity::Warn, QStringLiteral("Reboot required"));
    }

    if (fileExists(QStringLiteral("/proc/sys/kernel/osrelease"))) {
        QFile f(QStringLiteral("/proc/sys/kernel/osrelease"));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString running = QString::fromLocal8Bit(f.readAll()).trimmed();
            const QString newest = newestKernelModuleDir();
            if (!newest.isEmpty() && !running.startsWith(newest)) {
                addFinding(Severity::Info, QStringLiteral("Kernel/package mismatch"),
                           QStringLiteral("running %1, newest modules %2").arg(running, newest));
            }
        }
    }

    const QStringList backupRoots = {
        QStringLiteral("/backup"), QStringLiteral("/backups"), QStringLiteral("/var/backups"),
        QStringLiteral("/var/lib/borg"), QStringLiteral("/var/lib/restic"),
        QStringLiteral("/.snapshots"),
    };
    QDateTime newestBackup;
    QString newestPath;
    for (const QString &root : backupRoots) {
        if (!QFileInfo::exists(root))
            continue;
        QDirIterator it(root, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
        int seen = 0;
        while (it.hasNext() && seen++ < 500) {
            const QFileInfo info(it.next());
            if (!newestBackup.isValid() || info.lastModified() > newestBackup) {
                newestBackup = info.lastModified();
                newestPath = root;
            }
        }
    }
    if (newestBackup.isValid()) {
        const qint64 ageHours = newestBackup.secsTo(QDateTime::currentDateTime()) / 3600;
        if (ageHours >= 72)
            addFinding(Severity::Warn, QStringLiteral("Backup stale"),
                       QStringLiteral("%1 last changed %2h ago").arg(newestPath).arg(ageHours));
    }
}

void KrellhealthMonitor::startNextJob()
{
    if (!m_updating)
        return;
    if (m_pendingJobs.isEmpty()) {
        m_updating = false;
        updateDisplay();
        return;
    }

    const CheckJob job = m_pendingJobs.takeFirst();
    auto *proc = new QProcess(this);
    m_process = proc;
    proc->setProgram(job.program);
    proc->setArguments(job.args);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc, &QProcess::finished, this, [this, proc, job](int exitCode, QProcess::ExitStatus) {
        const QString output = QString::fromLocal8Bit(proc->readAll()).trimmed();
        if (m_process == proc)
            m_process = nullptr;
        proc->deleteLater();
        finishJob(job, output, exitCode);
        startNextJob();
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc, job](QProcess::ProcessError) {
        if (m_process == proc)
            m_process = nullptr;
        proc->deleteLater();
        finishJob(job, {}, -1);
        startNextJob();
    });
    QTimer::singleShot(job.timeoutMs, proc, [proc]() {
        if (proc->state() != QProcess::NotRunning)
            proc->kill();
    });
    proc->start();
}

void KrellhealthMonitor::finishJob(const CheckJob &job, const QString &output, int exitCode)
{
    const QStringList outLines = lines(output);
    if (job.id == QStringLiteral("systemd")) {
        if (!outLines.isEmpty())
            addFinding(Severity::Critical, QStringLiteral("Failed systemd units"),
                       outLines.mid(0, 3).join(QStringLiteral("; ")));
    } else if (job.id == QStringLiteral("zfs")) {
        if (!output.contains(QStringLiteral("all pools are healthy"), Qt::CaseInsensitive))
            addFinding(Severity::Critical, QStringLiteral("ZFS pool issue"), outLines.join(QStringLiteral("; ")).left(180));
    } else if (job.id == QStringLiteral("btrfs")) {
        if (QRegularExpression(QStringLiteral(":\\s*[1-9][0-9]*")).match(output).hasMatch())
            addFinding(Severity::Warn, QStringLiteral("Btrfs device errors"), outLines.join(QStringLiteral("; ")).left(180));
    } else if (job.id == QStringLiteral("journal")) {
        if (!outLines.isEmpty())
            addFinding(Severity::Warn, QStringLiteral("Journal errors"), outLines.mid(0, 3).join(QStringLiteral("; ")).left(180));
    } else if (job.id == QStringLiteral("timesync")) {
        if (output.contains(QStringLiteral("no"), Qt::CaseInsensitive)
            || output.contains(QStringLiteral("false"), Qt::CaseInsensitive))
            addFinding(Severity::Warn, QStringLiteral("Time sync off"));
    } else if (job.id == QStringLiteral("dpkg-audit")) {
        if (exitCode != 0)
            addFinding(Severity::Warn, QStringLiteral("Package check unavailable"), QStringLiteral("dpkg --audit could not complete"));
        else if (!outLines.isEmpty())
            addFinding(Severity::Warn, QStringLiteral("Package configuration incomplete"), outLines.mid(0, 3).join(QStringLiteral("; ")).left(180));
    } else if (job.id.endsWith(QStringLiteral("log"))) {
        if (!outLines.isEmpty())
            addFinding(Severity::Warn, QStringLiteral("%1 transaction errors").arg(job.label),
                       outLines.mid(0, 3).join(QStringLiteral("; ")).left(180));
    } else if (job.id == QStringLiteral("smart")) {
        if (exitCode != 0 || output.contains(QStringLiteral("FAILED"), Qt::CaseInsensitive)
            || output.contains(QStringLiteral("pre-fail"), Qt::CaseInsensitive))
            addFinding(Severity::Critical, QStringLiteral("SMART warning"), outLines.join(QStringLiteral("; ")).left(180));
    } else if (job.id == QStringLiteral("cert")) {
        if (exitCode != 0)
            addFinding(Severity::Warn, QStringLiteral("Certificate expiring"),
                       job.args.value(job.args.indexOf(QStringLiteral("-in")) + 1));
    }
}

void KrellhealthMonitor::addFinding(Severity severity, const QString &title, const QString &detail)
{
    m_findings << Finding{severity, title, detail};
}

void KrellhealthMonitor::updateDisplay()
{
    int critical = 0;
    int warn = 0;
    int info = 0;
    for (const Finding &finding : std::as_const(m_findings)) {
        if (finding.severity == Severity::Critical)
            ++critical;
        else if (finding.severity == Severity::Warn)
            ++warn;
        else
            ++info;
    }

    if (m_summary) {
        if (critical > 0) {
            m_summary->setStatus(QStringLiteral("✖"), QStringLiteral("%1 CRIT").arg(critical),
                                 QStringLiteral("accent_critical"));
        } else if (warn > 0) {
            m_summary->setStatus(QStringLiteral("⚠"), QStringLiteral("%1 WARN").arg(warn),
                                 QStringLiteral("accent_warning"));
        } else if (info > 0) {
            m_summary->setStatus(QStringLiteral("ⓘ"), QStringLiteral("%1 INFO").arg(info),
                                 QStringLiteral("text_secondary"));
        } else {
            m_summary->setStatus(QStringLiteral("♥"), QStringLiteral("OK"),
                                 QStringLiteral("accent_critical"));
        }
    }
    if (m_detail) {
        if (!m_findings.isEmpty())
            m_detail->setText(m_findings.first().title.left(22));
        else
            m_detail->setText(QStringLiteral("all tracked checks clear"));
    }

    QStringList tooltip;
    tooltip << QStringLiteral("KrellHealth checks:");
    if (m_findings.isEmpty()) {
        tooltip << QStringLiteral("No active warnings from enabled probes.");
    } else {
        for (const Finding &finding : std::as_const(m_findings)) {
            const QString prefix = finding.severity == Severity::Critical ? QStringLiteral("CRIT")
                                 : finding.severity == Severity::Warn ? QStringLiteral("WARN")
                                                                       : QStringLiteral("INFO");
            tooltip << QStringLiteral("%1: %2%3")
                           .arg(prefix, finding.title,
                                finding.detail.isEmpty() ? QString() : QStringLiteral(" - ") + finding.detail);
        }
    }
    if (m_panel)
        m_panel->setToolTip(tooltip.join(QLatin1Char('\n')));
}

QList<MonitorBase *> KrellhealthPlugin::createMonitors(Theme *theme, QObject *parent)
{
    if (!QSettings().value(QStringLiteral("plugins/krellhealth/enabled"), false).toBool())
        return {};
    return {new KrellhealthMonitor(theme, parent)};
}

#include "KrellhealthPlugin.moc"
