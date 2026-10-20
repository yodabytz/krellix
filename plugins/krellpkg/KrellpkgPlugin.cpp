#include "KrellpkgPlugin.h"

#include "widgets/Decal.h"
#include "widgets/Panel.h"
#include "remote/RemoteSource.h"
#include <QProcessEnvironment>
#include <QDebug>
#include <memory>
#include <QJsonArray>

#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>

namespace {

constexpr int kBackendTimeoutMs = 120000;

QString executable(const QString &name)
{
    QString found = QStandardPaths::findExecutable(name);
#ifdef Q_OS_MACOS
    if (found.isEmpty())
        found = QStandardPaths::findExecutable(name, {QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin")});
#endif
    return found;
}

struct PackageBackend {
    QString id;
    QString label;
    QString program;
    QStringList args;
    int skipHeaderLines = 0;
};

QStringList nonEmptyLines(const QString &text, int skipHeaderLines = 0)
{
    QStringList out;
    int seen = 0;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        if (seen++ < skipHeaderLines) continue;
        out << trimmed;
    }
    return out;
}

QString packageNameFromLine(const QString &line)
{
    QString name = line.section(QRegularExpression(QStringLiteral("\\s+")), 0, 0).trimmed();
    const int slash = name.indexOf(QLatin1Char('/'));
    if (slash > 0)
        name = name.left(slash);
    const int bracket = name.indexOf(QLatin1Char('['));
    if (bracket > 0)
        name = name.left(bracket);
    return name.trimmed();
}

QStringList splitPatterns(const QString &value)
{
    return value.split(QRegularExpression(QStringLiteral("[,;\\n]")), Qt::SkipEmptyParts);
}

bool matchesAnyPattern(const QString &packageName, const QStringList &patterns)
{
    for (QString pattern : patterns) {
        pattern = pattern.trimmed();
        if (pattern.isEmpty()) continue;
        QRegularExpression rx(QRegularExpression::wildcardToRegularExpression(pattern),
                              QRegularExpression::CaseInsensitiveOption);
        if (rx.match(packageName).hasMatch())
            return true;
    }
    return false;
}

QStringList filteredPackageLines(const QString &text, int skipHeaderLines)
{
    QSettings s;
    const QStringList includes =
        splitPatterns(s.value(QStringLiteral("plugins/krellpkg/include_patterns")).toString());
    const QStringList excludes =
        splitPatterns(s.value(QStringLiteral("plugins/krellpkg/exclude_patterns")).toString());
    QStringList out;
    for (const QString &line : nonEmptyLines(text, skipHeaderLines)) {
        const QString packageName = packageNameFromLine(line);
        if (!includes.isEmpty() && !matchesAnyPattern(packageName, includes))
            continue;
        if (!excludes.isEmpty() && matchesAnyPattern(packageName, excludes))
            continue;
        out << line;
    }
    return out;
}

bool backendEnabled(const QString &id)
{
#ifdef Q_OS_MACOS
    const bool defaultEnabled = id == QStringLiteral("brew");
#else
    const bool defaultEnabled = false;
#endif
    return QSettings().value(QStringLiteral("plugins/krellpkg/systems/") + id, defaultEnabled).toBool();
}

QList<PackageBackend> packageBackends()
{
    return {
        {QStringLiteral("pacman"), QStringLiteral("pacman"), QStringLiteral("checkupdates"), {}},
        {QStringLiteral("apt"), QStringLiteral("apt"), QStringLiteral("apt"),
         {QStringLiteral("list"), QStringLiteral("--upgradable")}, 1},
        {QStringLiteral("dnf"), QStringLiteral("dnf"), QStringLiteral("dnf"),
         {QStringLiteral("check-update"), QStringLiteral("--quiet")}},
        {QStringLiteral("zypper"), QStringLiteral("zypper"), QStringLiteral("zypper"),
         {QStringLiteral("--non-interactive"), QStringLiteral("list-updates")}, 4},
        {QStringLiteral("apk"), QStringLiteral("apk"), QStringLiteral("apk"),
         {QStringLiteral("version"), QStringLiteral("-l"), QStringLiteral("<")}},
        {QStringLiteral("xbps"), QStringLiteral("xbps"), QStringLiteral("xbps-install"),
         {QStringLiteral("-Mun")}},
        {QStringLiteral("eopkg"), QStringLiteral("eopkg"), QStringLiteral("eopkg"),
         {QStringLiteral("list-upgrades")}},
        {QStringLiteral("emerge"), QStringLiteral("emerge"), QStringLiteral("emerge"),
         {QStringLiteral("-uDNp"), QStringLiteral("@world")}},
        {QStringLiteral("nix"), QStringLiteral("nix"), QStringLiteral("nix"),
         {QStringLiteral("profile"), QStringLiteral("diff-closures"), QStringLiteral("--profile"),
          QStringLiteral("/nix/var/nix/profiles/system")}},
        {QStringLiteral("guix"), QStringLiteral("guix"), QStringLiteral("guix"),
         {QStringLiteral("package"), QStringLiteral("--list-upgradable")}},
        {QStringLiteral("freebsd_pkg"), QStringLiteral("FreeBSD pkg"), QStringLiteral("pkg"),
         {QStringLiteral("version"), QStringLiteral("-l"), QStringLiteral("<")}},
        {QStringLiteral("openbsd_pkg_add"), QStringLiteral("OpenBSD pkg_add"), QStringLiteral("pkg_add"),
         {QStringLiteral("-un")}},
        {QStringLiteral("netbsd_pkgin"), QStringLiteral("NetBSD pkgin"), QStringLiteral("pkgin"),
         {QStringLiteral("-n"), QStringLiteral("upgrade")}},
        {QStringLiteral("flatpak"), QStringLiteral("Flatpak"), QStringLiteral("flatpak"),
         {QStringLiteral("remote-ls"), QStringLiteral("--updates")}},
        {QStringLiteral("snap"), QStringLiteral("Snap"), QStringLiteral("snap"),
         {QStringLiteral("refresh"), QStringLiteral("--list")}, 1},
        {QStringLiteral("brew"), QStringLiteral("Homebrew"), QStringLiteral("brew"),
         {QStringLiteral("outdated"), QStringLiteral("--quiet")}},
        {QStringLiteral("pipx"), QStringLiteral("pipx"), QStringLiteral("pipx"),
         {QStringLiteral("upgrade-all"), QStringLiteral("--dry-run")}},
        {QStringLiteral("npm"), QStringLiteral("npm global"), QStringLiteral("npm"),
         {QStringLiteral("outdated"), QStringLiteral("-g"), QStringLiteral("--parseable")}},
        {QStringLiteral("gem"), QStringLiteral("RubyGems"), QStringLiteral("gem"),
         {QStringLiteral("outdated")}},
        {QStringLiteral("cargo"), QStringLiteral("cargo-install"), QStringLiteral("cargo"),
         {QStringLiteral("install-update"), QStringLiteral("--list")}},
        {QStringLiteral("composer"), QStringLiteral("Composer global"), QStringLiteral("composer"),
         {QStringLiteral("global"), QStringLiteral("outdated"), QStringLiteral("--direct")}},
    };
}

KrellpkgMonitor::CheckJob toJob(const PackageBackend &backend)
{
    return {backend.id, backend.label, backend.program, backend.args, backend.skipHeaderLines};
}

QList<KrellpkgMonitor::CheckJob> enabledJobs()
{
    QList<KrellpkgMonitor::CheckJob> jobs;
    for (const PackageBackend &backend : packageBackends()) {
        if (!backendEnabled(backend.id))
            continue;
        auto job = toJob(backend);
        const QString path = executable(backend.program);
        if (!path.isEmpty()) job.program = path;
        jobs << job;
    }

    if (backendEnabled(QStringLiteral("aur"))) {
        if (!QStandardPaths::findExecutable(QStringLiteral("paru")).isEmpty()) {
            jobs << KrellpkgMonitor::CheckJob{QStringLiteral("aur"), QStringLiteral("AUR"),
                                              QStringLiteral("paru"), {QStringLiteral("-Qua")}, 0};
        } else if (!QStandardPaths::findExecutable(QStringLiteral("yay")).isEmpty()) {
            jobs << KrellpkgMonitor::CheckJob{QStringLiteral("aur"), QStringLiteral("AUR"),
                                              QStringLiteral("yay"), {QStringLiteral("-Qua")}, 0};
        }
    }
    return jobs;
}

} // namespace

KrellpkgMonitor::KrellpkgMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
    if (auto *remote = RemoteSource::instance()) {
        connect(remote, &RemoteSource::sampleReceived, this, &KrellpkgMonitor::updateRemote);
        connect(remote, &RemoteSource::connectionStateChanged, this, &KrellpkgMonitor::updateRemote);
    }
}

QWidget *KrellpkgMonitor::createWidget(QWidget *parent)
{
    auto *panel = new Panel(theme(), parent);
    panel->setSurfaceKey(QStringLiteral("panel_bg_krellpkg"));
    panel->setTitle(QStringLiteral("Packages"));
    m_summary = panel->addDecal(QStringLiteral("label"), QStringLiteral("text_primary"));
    m_summary->setAlignment(Qt::AlignHCenter);
    m_detail = panel->addDecal(QStringLiteral("label"), QStringLiteral("text_secondary"));
    m_detail->setAlignment(Qt::AlignHCenter);
    if (m_summary)
        m_summary->setText(QStringLiteral("Checking packages..."));
    QTimer::singleShot(0, this, &KrellpkgMonitor::tick);
    return panel;
}

void KrellpkgMonitor::tick()
{
    if (RemoteSource::instance()) { updateRemote(); return; }
    if (m_updating)
        return;

    m_pendingJobs = enabledJobs();
    m_details.clear();
    m_errors.clear();
    m_checks = m_pendingJobs.size();

    if (m_pendingJobs.isEmpty()) {
        updateDisplay();
        return;
    }

    m_updating = true;
    if (m_summary)
        m_summary->setText(QStringLiteral("Checking packages..."));
    startNextCheck();
}

void KrellpkgMonitor::updateRemote()
{
    auto *remote = RemoteSource::instance();
    if (!remote || !m_summary) return;
    const auto info = remote->packageInfo();
    const QString status = info.value(QStringLiteral("status")).toString();
    QString text;
    if (!remote->isConnected()) text = QStringLiteral("Server disconnected");
    else if (status == QStringLiteral("checking")) text = QStringLiteral("Checking packages...");
    else if (status == QStringLiteral("ok") && info.value(QStringLiteral("count")).isDouble())
        text = QStringLiteral("%1 %2").arg(info.value(QStringLiteral("system")).toString()).arg(info.value(QStringLiteral("count")).toInt());
    else if (status == QStringLiteral("error")) text = QStringLiteral("Server check failed");
    else text = QStringLiteral("Server data unavailable");
    if (m_summary->text() != text) {
        m_summary->setText(text);
        qInfo().noquote() << "krellpkg remote:" << remote->remoteAddress() << text;
    }
    QStringList details{QStringLiteral("Server: ") + remote->remoteHostname(),
        QStringLiteral("Checked: ") + info.value(QStringLiteral("checked_at")).toString(),
        QStringLiteral("Uses the server's cached package lists; no upgrades are installed."),
        QStringLiteral("Package lists updated: ") + info.value(QStringLiteral("index_updated_at")).toString()};
    for (const auto &name : info.value(QStringLiteral("packages")).toArray()) details << name.toString();
    m_summary->setToolTip(details.join(QLatin1Char('\n')));
    if (m_detail) m_detail->setText(QString());
}

void KrellpkgMonitor::startNextCheck()
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
    auto environment = QProcessEnvironment::systemEnvironment();
#ifdef Q_OS_MACOS
    environment.insert(QStringLiteral("PATH"), environment.value(QStringLiteral("PATH"))
        + QStringLiteral(":/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin"));
#endif
    environment.insert(QStringLiteral("HOMEBREW_NO_AUTO_UPDATE"), QStringLiteral("1"));
    environment.insert(QStringLiteral("HOMEBREW_NO_ANALYTICS"), QStringLiteral("1"));
    proc->setProcessEnvironment(environment);
    proc->setProcessChannelMode(QProcess::SeparateChannels);
    auto completed = std::make_shared<bool>(false);
    auto finish = [this, proc, job, completed](int code, QProcess::ExitStatus status) {
        if (*completed) return;
        *completed = true;
        const QString output = QString::fromLocal8Bit(proc->readAllStandardOutput()).trimmed();
        const bool success = status == QProcess::NormalExit && !proc->property("timedOut").toBool()
            && (code == 0 || (job.id == QStringLiteral("dnf") && code == 100)
                || (job.id == QStringLiteral("pacman") && code == 2)
                || (job.id == QStringLiteral("npm") && code == 1));
        if (success) finishCheck(job, output);
        else m_errors << job.label + (proc->property("timedOut").toBool()
            ? QStringLiteral(" timed out") : QStringLiteral(" check failed"));
        if (m_process == proc) m_process = nullptr;
        proc->deleteLater();
        startNextCheck();
    };
    connect(proc, &QProcess::finished, this, finish);
    connect(proc, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
        // Crashes also emit finished; failed starts do not.
        if (error == QProcess::FailedToStart) finish(-1, QProcess::CrashExit);
    });
    QTimer::singleShot(kBackendTimeoutMs, proc, [proc]() {
        if (proc->state() != QProcess::NotRunning) {
            proc->setProperty("timedOut", true);
            proc->kill();
        }
    });

    proc->start();
}

void KrellpkgMonitor::finishCheck(const CheckJob &job, const QString &output)
{
    const int count = filteredPackageLines(output, job.skipHeaderLines).size();
    m_details << QStringLiteral("%1 %2").arg(job.label).arg(count);
}

void KrellpkgMonitor::updateDisplay()
{
    QStringList compact = m_details.mid(0, 4);
    if (m_details.size() > compact.size())
        compact << QStringLiteral("+%1 more").arg(m_details.size() - compact.size());

    const QString summary = !compact.isEmpty() ? compact.join(QStringLiteral(" | "))
        : m_checks == 0 ? QStringLiteral("Select package systems") : QStringLiteral("Check failed");
    if (m_summary) {
        m_summary->setText(summary);
        m_summary->setToolTip((m_details + m_errors).join(QLatin1Char('\n')));
    }
    qInfo().noquote() << "krellpkg:" << summary << m_errors.join(QStringLiteral("; "));
    if (m_detail)
        m_detail->setText(m_errors.isEmpty() ? QString() : m_errors.first());
}

QList<MonitorBase *> KrellpkgPlugin::createMonitors(Theme *theme, QObject *parent)
{
    if (!QSettings().value(QStringLiteral("plugins/krellpkg/enabled"), false).toBool())
        return {};
    return {new KrellpkgMonitor(theme, parent)};
}
