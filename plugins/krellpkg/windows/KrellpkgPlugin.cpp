#include "KrellpkgPlugin.h"
#include "remote/RemoteSource.h"
#include "widgets/Decal.h"
#include "widgets/Panel.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSettings>
#include <cmath>

WindowsPackageMonitor::WindowsPackageMonitor(Theme *theme, QObject *parent) : MonitorBase(theme, parent) {
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] { finish(); failed("WinGet check timed out; retrying in five minutes."); });
    if (auto *remote = RemoteSource::instance()) {
        // Imported host signal addresses differ across the Windows DLL boundary.
        connect(remote, SIGNAL(sampleReceived()), this, SLOT(updateRemote()));
        connect(remote, SIGNAL(connectionStateChanged(bool)), this, SLOT(updateRemote()));
    }
}
WindowsPackageMonitor::~WindowsPackageMonitor() { shutdown(); }
void WindowsPackageMonitor::finish() {
    m_timeout.stop();
    if (!m_probe) return;
    auto *process = m_probe; m_probe = nullptr;
    disconnect(process, nullptr, this, nullptr);
    if (process->state() == QProcess::NotRunning) process->deleteLater();
    else { connect(process, &QProcess::finished, process, &QObject::deleteLater); process->kill(); }
}
void WindowsPackageMonitor::shutdown() {
    m_stopped = true; m_timeout.stop();
    if (m_probe) {
        disconnect(m_probe, nullptr, this, nullptr);
        m_probe->kill(); m_probe->waitForFinished(1000); delete m_probe; m_probe = nullptr;
    }
}
QWidget *WindowsPackageMonitor::createWidget(QWidget *parent) {
    auto *panel = new Panel(theme(), parent);
    panel->setTitle("Packages"); panel->setSurfaceKey("panel_bg_krellpkg");
    m_summary = panel->addDecal("label", "text_primary");
    m_summary->setObjectName("packageSummary");
    m_summary->setAlignment(Qt::AlignHCenter);
    display("Checking packages...");
    QTimer::singleShot(0, this, &WindowsPackageMonitor::tick);
    return panel;
}
void WindowsPackageMonitor::display(const QString &text, const QString &details) {
    auto *summary = m_summary.data();
    if (!summary) return;
    if (summary->text() != text) {
        summary->setText(text);
        qInfo().noquote() << "krellpkg:" << text;
    }
    summary->setToolTip("<qt>" + details.toHtmlEscaped().replace('\n', "<br>") + "</qt>");
    summary->setAccessibleDescription(details);
}
void WindowsPackageMonitor::failed(const QString &detail) {
    m_snapshot = {}; m_snapshot.valid = true; m_snapshot.status = "error"; m_snapshot.detail = detail;
    renderLocal();
}
void WindowsPackageMonitor::renderLocal() {
    if (!m_snapshot.valid) return;
    if (m_snapshot.status != "ok") { display("WinGet unavailable", m_snapshot.detail); return; }
    QSettings settings;
    const auto packages = WindowsPackages::filtered(m_snapshot,
        settings.value("plugins/krellpkg/include_patterns").toString(),
        settings.value("plugins/krellpkg/exclude_patterns").toString());
    QStringList details{"WinGet community-source updates (read-only).", "Checked: " + m_snapshot.checked,
        "Windows Update and Microsoft Store updates are not included.",
        "Refreshes every five minutes. Name/ID filters apply immediately.",
        "Windows uses WinGet automatically; Unix backend selections do not apply."};
    for (const auto &package : packages.mid(0, 100))
        details << package.id + " (" + package.name + "): " + package.installed + " -> " + package.available;
    if (packages.size() > 100) details << "Only the first 100 updates are listed here.";
    display(QStringLiteral("WinGet %1").arg(packages.size()), details.join('\n'));
}
void WindowsPackageMonitor::updateRemote() {
    auto *remote = RemoteSource::instance();
    if (!remote || m_stopped) return;
    finish(); m_snapshot = {};
    if (!remote->isConnected()) { display("Server disconnected"); return; }
    const auto info = remote->packageInfo();
    const auto status = info.value("status").toString();
    if (status == "checking") { display("Checking packages..."); return; }
    if (status == "error") { display("Server check failed"); return; }
    const double count = info.value("count").toDouble(-1);
    const auto system = info.value("system").toString();
    if (status != "ok" || !std::isfinite(count) || count < 0 || count > 1000000 || std::floor(count) != count
        || system.isEmpty() || system.size() > 64 || !info.value("packages").isArray()) {
        display("Server data unavailable"); return;
    }
    QStringList details{"Server: " + remote->remoteHostname().left(256),
        "Checked: " + info.value("checked_at").toString().left(128),
        "Uses only the server's cached package lists; no upgrades are installed.",
        "Package lists updated: " + info.value("index_updated_at").toString().left(128)};
    const auto packages = info.value("packages").toArray();
    for (int i = 0; i < qMin(100, int(packages.size())); ++i) details << packages[i].toString().left(2048);
    if (count > packages.size()) details << "The server provides a partial name list; the count includes all updates.";
    display(QStringLiteral("%1 %2").arg(system).arg(int(count)), details.join('\n'));
}
void WindowsPackageMonitor::tick() {
    if (m_stopped) return;
    if (RemoteSource::instance()) { updateRemote(); return; }
    if (m_probe) return;
    if (m_attempt.isValid() && m_attempt.elapsed() < 300000) { renderLocal(); return; }
    m_attempt.start(); m_output.clear(); m_snapshot = {};
    const auto helper = QCoreApplication::applicationDirPath() + "/packages/krellpkg-probe.exe";
    if (!QFileInfo::exists(helper)) { failed("Package helper missing. Reinstall the matching KrellPkg package."); return; }
    display("Checking packages...");
    auto *process = new QProcess(this); m_probe = process;
    process->setProgram(helper);
    auto drain = [this, process](bool error) {
        process->setReadChannel(error ? QProcess::StandardError : QProcess::StandardOutput);
        const auto bytes = process->read(qMax<qint64>(1, 1024 * 1024 - m_output.size() + 1));
        if (error) { if (!bytes.isEmpty()) { finish(); failed("Unexpected helper error output"); } return; }
        m_output += bytes;
        if (m_output.size() > 1024 * 1024) { finish(); m_output.clear(); failed("Package response too large"); }
    };
    connect(process, &QProcess::readyReadStandardOutput, this, [drain] { drain(false); });
    connect(process, &QProcess::readyReadStandardError, this, [drain] { drain(true); });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { finish(); failed("Package helper failed to start"); }
    });
    connect(process, &QProcess::finished, this, [this, drain](int code, QProcess::ExitStatus status) {
        drain(false); if (!m_probe) return;
        finish();
        const auto document = QJsonDocument::fromJson(m_output); m_output.clear();
        if (code || status != QProcess::NormalExit || !document.isObject()) { failed("WinGet query failed"); return; }
        m_snapshot = WindowsPackages::parse(document.object());
        if (!m_snapshot.valid) { failed("Invalid WinGet package data"); return; }
        renderLocal();
    });
    m_timeout.start(60000); process->start();
}
QList<MonitorBase *> KrellpkgPlugin::createMonitors(Theme *theme, QObject *parent) {
    if (!QSettings().value("plugins/krellpkg/enabled", false).toBool()) return {};
    return {new WindowsPackageMonitor(theme, parent)};
}
