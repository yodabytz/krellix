#include "sdk/KrellixPlugin.h"
#include "monitors/MonitorBase.h"
#include "remote/RemoteSource.h"
#include "theme/Theme.h"
#include <QApplication>
#include <QWidget>
#include <QPixmap>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QPluginLoader>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <functional>
#include <cstdio>
static bool signalWarning = false;
static void messages(QtMsgType, const QMessageLogContext &, const QString &message) {
    if (message.contains("signal not found") || message.contains("No such slot")) signalWarning = true;
    std::fprintf(stderr, "%s\n", qPrintable(message));
}
static void check(bool value, const char *message) { if (!value) qFatal("%s", message); }
static bool until(const std::function<bool()> &predicate, int deadline = 16000) {
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < deadline) { QApplication::processEvents(); if (predicate()) return true; QThread::msleep(10); }
    return false;
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv); check(argc == 4, "Missing test paths");
    qInstallMessageHandler(messages);
    const auto root = app.applicationDirPath(); check(root.endsWith("krellbt-test-host"), "Wrong isolated host directory");
    QTemporaryDir temporary; QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    app.setOrganizationName("bt-test"); app.setApplicationName("isolated");
    QPluginLoader loader(QString::fromLocal8Bit(argv[1]));
    auto *plugin = qobject_cast<IKrellixPlugin *>(loader.instance());
    if (!plugin) qFatal("DLL load failed: %s", qPrintable(loader.errorString()));
    Theme theme; QObject parent;
    check(plugin->createMonitors(&theme, &parent).isEmpty(), "Default-disabled violated");
    QSettings().setValue("plugins/krellbluetooth/enabled", true);
    QDir().mkpath(root + "/bluetooth"); const QString helper = root + "/bluetooth/krellbt-probe.exe";
    QFile::remove(helper); check(QFile::copy(QString::fromLocal8Bit(argv[2]), helper), "Native helper copy failed");
    auto *monitor = plugin->createMonitors(&theme, &parent).first();
    auto *widget = monitor->createWidget(nullptr); widget->setFixedWidth(120); widget->show();
    auto settled = [&] {
        const auto rows = widget->findChildren<QWidget *>("bluetoothDeviceRow");
        return !rows.isEmpty() && !rows[0]->accessibleName().startsWith("Checking");
    };
    check(until(settled), "Native query did not settle");
    qInfo("Native state: %s", qPrintable(widget->findChildren<QWidget *>("bluetoothDeviceRow")[0]->accessibleName()));
    monitor->shutdown(); delete widget; delete monitor;
    QFile::remove(helper); check(QFile::copy(QString::fromLocal8Bit(argv[3]), helper), "Fixture copy failed");
    for (const auto *mode : {"ok", "empty", "no_adapter", "radio_off", "permission", "error", "malformed", "oversize", "timeout"}) {
        qputenv("KRELLBT_TEST_CASE", mode);
        monitor = plugin->createMonitors(&theme, &parent).first(); widget = monitor->createWidget(nullptr);
        widget->setFixedWidth(120); widget->show(); check(until(settled), "Fixture did not settle");
        const auto rows = widget->findChildren<QWidget *>("bluetoothDeviceRow");
        if (QByteArray(mode) == "ok") {
            check(rows.size() == 3 && rows[0]->accessibleDescription() == "0%" && rows[2]->accessibleDescription() == "100%", "Battery layout wrong");
            check(rows[1]->toolTip().contains("Battery unknown"), "Unknown shown as zero");
            check(rows[0]->toolTip().contains("&lt;b&gt;"), "Device name injected HTML");
            check(rows[0]->width() <= 120 && widget->width() == 120, "Narrow layout expanded");
            widget->grab();
            theme.setMetric("test_bluetooth_refresh", 1);
            QApplication::processEvents(); widget->grab();
            check(!signalWarning, "Theme signal connection failed across DLL boundary");
            qputenv("KRELLBT_TEST_CASE", "malformed");
            check(until([&] {
                monitor->tick();
                const auto current = widget->findChildren<QWidget *>("bluetoothDeviceRow");
                return current.size() == 1 && current[0]->accessibleName() == "Bluetooth query failed";
            }, 30000), "Failed refresh did not clear previously displayed devices");
        } else check(rows.size() == 1 && rows[0]->accessibleDescription().isEmpty(), "Error retained device rows");
        monitor->shutdown(); delete widget; delete monitor;
    }
    qputenv("KRELLBT_TEST_CASE", "timeout");
    monitor = plugin->createMonitors(&theme, &parent).first(); widget = monitor->createWidget(nullptr); monitor->tick();
    QElapsedTimer timer; timer.start(); monitor->shutdown(); delete widget; delete monitor;
    check(timer.elapsed() < 2500, "Shutdown stalled");
    { RemoteSource remote; check(plugin->createMonitors(&theme, &parent).isEmpty(), "Local Bluetooth leaked into remote instance"); }
    QSettings().setValue("plugins/krellbluetooth/enabled", false);
    check(plugin->createMonitors(&theme, &parent).isEmpty(), "Disable failed");
    qunsetenv("KRELLBT_TEST_CASE"); QFile::remove(helper);
    check(!signalWarning, "Invalid signal/slot connection");
    qInfo("PASS actual DLL, native helper, 120px layout, zero/unknown/full batteries, escaped names, errors, timeout, shutdown and remote isolation");
}
