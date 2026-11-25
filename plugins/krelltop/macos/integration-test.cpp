#include "monitors/MonitorBase.h"
#include "remote/RemoteSource.h"
#include "sdk/KrellixPlugin.h"
#include "theme/Theme.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QPluginLoader>
#include <QProcess>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QWidget>

#include <memory>

static void check(bool condition, const char *message)
{
    if (!condition)
        qFatal("%s", message);
}

template<typename Predicate>
static void waitFor(Predicate ready, const char *message)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < 4000) {
        QApplication::processEvents();
        if (ready())
            return;
        QThread::msleep(10);
    }
    qFatal("%s", message);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    check(argc >= 2, "Plugin path required");
    QTemporaryDir config;
    check(config.isValid(), "Could not isolate test settings");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.path());
    QCoreApplication::setOrganizationName("krelltop-macos-test");
    QCoreApplication::setApplicationName("local");
    QPluginLoader loader(QString::fromLocal8Bit(argv[1]));
    auto *plugin = qobject_cast<IKrellixPlugin *>(loader.instance());
    if (!plugin)
        qFatal("Plugin load failed: %s", qPrintable(loader.errorString()));

    Theme theme;
    const QString enabled = QStringLiteral("plugins/krelltop/enabled");
    QSettings().setValue(enabled, false);
    check(plugin->createMonitors(&theme, nullptr).isEmpty(), "Disabled plugin created a monitor");
    QSettings().setValue(enabled, true);
    {
        std::unique_ptr<MonitorBase> monitor(plugin->createMonitors(&theme, nullptr).first());
        std::unique_ptr<QWidget> panel(monitor->createWidget(nullptr));
        waitFor([&] { return panel->toolTip().contains("Top memory"); }, "Local sample missing");
        check(panel->toolTip().contains("pid "), "No local process IDs");
        monitor->tick();
        monitor->shutdown();
    }

    QCoreApplication::setApplicationName("remote");
    check(plugin->createMonitors(&theme, nullptr).isEmpty(), "Local setting leaked into remote profile");
    QSettings().setValue(enabled, true);
    QTcpServer server;
    check(server.listen(QHostAddress::LocalHost), "Fixture listen failed");
    RemoteSource source;
    source.connectToHost(QStringLiteral("127.0.0.1"), server.serverPort());
    waitFor([&] { return server.hasPendingConnections(); }, "Fixture connection failed");
    auto *socket = server.nextPendingConnection();
    const QByteArray sample = R"({"type":"sample","top":[{"pid":42,"name":"/tmp/REMOTE CPU.exe","cpu":30,"mem":2,"rss":8192},{"pid":43,"name":"REMOTE RAM","cpu":3,"mem":6,"rss":65536}]})" "\n";
    std::unique_ptr<MonitorBase> monitor(plugin->createMonitors(&theme, nullptr).first());
    std::unique_ptr<QWidget> panel(monitor->createWidget(nullptr));
    auto waitForTooltip = [&](const QString &match, const char *message) {
        waitFor([&] { monitor->tick(); return panel->toolTip().contains(match); }, message);
    };
    socket->write(sample);
    waitForTooltip(QStringLiteral("REMOTE CPU"), "Remote sample missing");
    check(panel->toolTip().contains("Top CPU\n1. REMOTE CPU  pid 42"), "CPU sort or process name wrong");
    check(panel->toolTip().contains("Top memory\n1. REMOTE RAM  pid 43"), "Memory sorting wrong");
    socket->write("{\"type\":\"sample\",\"top\":[]}\n");
    waitForTooltip(QStringLiteral("No remote process data"), "No-data sample retained stale rows");
    socket->write(sample);
    waitForTooltip(QStringLiteral("REMOTE CPU"), "New remote sample was not shown");
    socket->disconnectFromHost();
    waitForTooltip(QStringLiteral("Remote disconnected"), "Disconnect left stale process rows");
    check(!monitor->findChild<QProcess *>(), "Remote mode created a local sampler");
    check(!server.hasPendingConnections(), "Plugin opened a second telemetry connection");
    monitor->shutdown();
    qInfo("PASS: Mac process sampling, per-profile enablement, CPU and memory rankings, remote-only data, disconnect and shutdown");
}
