#include "sdk/KrellixPlugin.h"
#include "monitors/MonitorBase.h"
#include "remote/RemoteSource.h"
#include "theme/Theme.h"
#include "widgets/Decal.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QPluginLoader>
#include <QProcess>
#include <QPixmap>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <functional>
static bool badSignal = false;
static void messages(QtMsgType, const QMessageLogContext &, const QString &message) {
    if (message.contains("signal not found") || message.contains("No such slot")) badSignal = true;
    fprintf(stderr, "%s\n", qPrintable(message));
}
static void check(bool value, const char *message) { if (!value) qFatal("%s", message); }
static bool until(const std::function<bool()> &predicate, int deadline = 10000) {
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < deadline) { QApplication::processEvents(); if (predicate()) return true; QThread::msleep(10); }
    return false;
}
int main(int argc, char **argv) {
    QApplication app(argc, argv); check(argc == 3, "Missing paths"); qInstallMessageHandler(messages);
    const auto root = app.applicationDirPath(); check(root.endsWith("krellpkg-test-host"), "Wrong test directory");
    QTemporaryDir temporary; QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    app.setOrganizationName("pkg-test"); app.setApplicationName("isolated");
    QPluginLoader loader(QString::fromLocal8Bit(argv[1]));
    auto *plugin = qobject_cast<IKrellixPlugin *>(loader.instance());
    if (!plugin) qFatal("DLL: %s", qPrintable(loader.errorString()));
    Theme theme; QObject parent;
    check(plugin->createMonitors(&theme, &parent).isEmpty(), "Enabled by default");
    QSettings().setValue("plugins/krellpkg/enabled", true);
    QDir().mkpath(root + "/packages"); const auto helper = root + "/packages/krellpkg-probe.exe";
    QFile::remove(helper); check(QFile::copy(QString::fromLocal8Bit(argv[2]), helper), "Fixture copy failed");
    for (const auto *mode : {"ok", "empty", "error", "malformed", "oversize", "timeout"}) {
        qputenv("KRELLPKG_TEST_CASE", mode);
        auto *monitor = plugin->createMonitors(&theme, &parent).first();
        auto *widget = monitor->createWidget(nullptr); widget->setFixedWidth(120); widget->show();
        auto *label = widget->findChild<Decal *>("packageSummary"); check(label, "No package label");
        check(until([&] { return !label->text().startsWith("Checking"); }, 70000), "Query stuck");
        if (QByteArray(mode) == "ok") {
            check(label->text() == "WinGet 2", "Wrong local count");
            check(label->toolTip().contains("&lt;b&gt;"), "Tooltip injection");
            QSettings().setValue("plugins/krellpkg/exclude_patterns", "*.Two"); monitor->tick();
            check(label->text() == "WinGet 1", "Live filtering failed");
            QSettings().remove("plugins/krellpkg/exclude_patterns"); monitor->tick();
            check(label->text() == "WinGet 2", "Filter reset failed"); widget->grab();
        } else if (QByteArray(mode) == "empty") check(label->text() == "WinGet 0", "Zero wrong");
        else check(label->text() == "WinGet unavailable", "Error shown as zero");
        monitor->shutdown(); delete widget; delete monitor;
    }
    // A remote monitor must never launch the local helper, even when disconnected.
    qputenv("KRELLPKG_TEST_CASE", "ok");
    {
        RemoteSource remote; QTcpServer server; check(server.listen(QHostAddress::LocalHost), "Listen failed");
        auto *monitor = plugin->createMonitors(&theme, &parent).first();
        auto *widget = monitor->createWidget(nullptr); auto *label = widget->findChild<Decal *>("packageSummary");
        monitor->tick(); check(label->text() == "Server disconnected", "Local fallback on disconnect");
        remote.connectToHost("127.0.0.1", server.serverPort());
        check(until([&] { return server.hasPendingConnections() && remote.isConnected(); }), "Remote connect failed");
        auto *peer = server.nextPendingConnection();
        peer->write("{\"type\":\"sample\",\"packages\":{\"status\":\"ok\",\"system\":\"apt\",\"count\":7,\"packages\":[\"<b>test</b>\"]}}\n"); peer->flush();
        check(until([&] { return label->text() == "apt 7"; }), "Remote sample failed");
        check(label->toolTip().contains("&lt;b&gt;"), "Remote tooltip injection");
        check(monitor->findChildren<QProcess *>().isEmpty(), "Remote launched local helper");
        peer->abort(); check(until([&] { return label->text() == "Server disconnected"; }), "Stale remote count");
        monitor->shutdown(); delete widget; delete monitor;
    }
    qputenv("KRELLPKG_TEST_CASE", "timeout");
    auto *monitor = plugin->createMonitors(&theme, &parent).first(); auto *widget = monitor->createWidget(nullptr); monitor->tick();
    QElapsedTimer timer; timer.start(); monitor->shutdown(); delete widget; delete monitor;
    check(timer.elapsed() < 2500, "Shutdown stalled");
    check(!badSignal, "DLL signal mismatch"); QFile::remove(helper);
    qInfo("PASS actual DLL, live filters, error/empty/timeout/size handling, remote isolation, disconnect and shutdown");
}
