#include "KrellbluetoothPlugin.h"
#include "remote/RemoteSource.h"
#include "theme/Theme.h"
#include <QApplication>
#include <QWidget>
#include <QLayout>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QDebug>

class BluetoothMonitorTest {
public:
    static void run(Theme &theme) {
        KrellbluetoothMonitor monitor(&theme);
        QWidget *panel = monitor.createWidget(nullptr);
        panel->setFixedWidth(120);
        panel->show();
        Bluetooth::Snapshot fixture;
        fixture.devices = {{QString(200, QLatin1Char('M')), QStringLiteral("AA:BB:CC:DD:EE:FF"), 39, {}},
                           {QStringLiteral("Headphones"), QStringLiteral("00:11:22:33:44:55"), -1, {}}};
        monitor.display(fixture);
        QApplication::processEvents();
        if (panel->width() != 120 || panel->height() > 110) qFatal("Bluetooth panel is not compact");
        if (monitor.m_rows->layout()->count() != 2) qFatal("Not one row per connected device");
        auto *first = monitor.m_rows->layout()->itemAt(0)->widget();
        if (!first->toolTip().contains(fixture.devices.first().name) || !first->toolTip().contains(QStringLiteral("39%")))
            qFatal("Full device details missing");
        if (!monitor.m_rows->layout()->itemAt(1)->widget()->toolTip().contains(QStringLiteral("not reported")))
            qFatal("Unknown battery misleading");
        monitor.display({QStringLiteral("Bluetooth off"), {}});
        if (monitor.m_rows->layout()->count() != 1 || !monitor.m_rows->layout()->itemAt(0)->widget()->toolTip().contains(QStringLiteral("off")))
            qFatal("Stale devices remained after disconnect");
        delete panel;
        // Destruction while the asynchronous OS query is in flight must be safe.
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTemporaryDir config;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.path());
    QCoreApplication::setOrganizationName(QStringLiteral("krellix-test"));
    QCoreApplication::setApplicationName(QStringLiteral("bluetooth-test"));
    Theme theme;
    KrellbluetoothPlugin plugin;
    if (!plugin.createMonitors(&theme, &app).isEmpty()) qFatal("Bluetooth unexpectedly enabled by default");
    QSettings().setValue(QStringLiteral("plugins/krellbluetooth/enabled"), true);
    auto monitors = plugin.createMonitors(&theme, &app);
    if (monitors.size() != 1) qFatal("Enabled Bluetooth missing");
    qDeleteAll(monitors);
    {
        RemoteSource remote;
        if (!plugin.createMonitors(&theme, &app).isEmpty()) qFatal("Local Bluetooth leaked into remote instance");
    }
    BluetoothMonitorTest::run(theme);
    qInfo("PASS: compact Bluetooth layout, default disabled, no stale rows, remote isolation and async teardown");
    return 0;
}
