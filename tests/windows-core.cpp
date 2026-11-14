#define main krellixMain
#include "../src/krellix/main.cpp"
#undef main
#include "krellix/SettingsDialog.h"
#include "monitors/CpuMonitor.h"
#include "monitors/NetMonitor.h"
#include "monitors/DiskMonitor.h"
#include "widgets/Panel.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QScrollArea>
#include <QStackedWidget>
#include <QSpinBox>
#include <QPushButton>
#include <QLineEdit>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QThread>

static int pagesTested = 0;
static bool remoteTest = false;
static int remoteSamples = 0;
static void verifyEmptyRemote()
{
    if (!CpuStat::read().isEmpty() || MemStat::read().totalKb != 0
        || !DiskStat::read().isEmpty() || !NetStat::read().isEmpty()
        || !NetPortStat::read().isEmpty() || ProcStat::read().processes != 0
        || BatteryStat::read().present || !SensorStat::read().isEmpty()
        || RemoteSource::instance()->remoteTime().isValid()) qFatal("Local fallback or stale data in remote mode");
    if (qApp->findChild<QObject *>(QStringLiteral("windows-sensor-reader"))) qFatal("Remote mode started local sensor helper");
}
static void startRemoteTest()
{
    auto *server = new QTcpServer(qApp);
    if (!server->listen(QHostAddress::LocalHost, 19997)) qFatal("Isolation test listener unavailable");
    QObject::connect(server, &QTcpServer::newConnection, server, [server] {
        auto *socket = server->nextPendingConnection();
        auto *remote = RemoteSource::instance();
        verifyEmptyRemote();
        if (UptimeStat::secondsSinceBoot() != -1) qFatal("Remote initial uptime is local");
        QObject::connect(remote, &RemoteSource::sampleReceived, socket, [socket, server] {
            ++remoteSamples;
            if (remoteSamples == 1) {
                if (CpuStat::read().size() != 1 || CpuStat::read()[0].name != "REMOTE_CPU"
                    || MemStat::read().totalKb != 123456 || DiskStat::read().size() != 1 || DiskStat::read()[0].name != "REMOTE_DISK"
                    || NetStat::read().size() != 1 || NetStat::read()[0].name != "REMOTE_NET"
                    || NetPortStat::read().size() != 1 || NetPortStat::read()[0].remoteAddress != "192.0.2.55"
                    || ProcStat::read().processes != 314 || UptimeStat::secondsSinceBoot() != 12345
                    || BatteryStat::read().percent != 77 || SensorStat::read().size() != 1 || SensorStat::read()[0].value != 61
                    || RemoteSource::instance()->remoteHostname() != "REMOTE_HOST"
                    || RemoteSource::instance()->remoteKernel() != "REMOTE_KERNEL"
                    || RemoteSource::instance()->remoteTime().date().year() != 2001) qFatal("Remote telemetry replaced with local data");
                socket->write("{\"type\":\"sample\",\"uptime\":-1}\n");
            } else {
                verifyEmptyRemote();
                server->close();
                socket->disconnectFromHost();
                QTimer::singleShot(500, [] {
                    verifyEmptyRemote();
                    if (UptimeStat::secondsSinceBoot() != -1) qFatal("Disconnect lost remote unknown uptime");
                    qInfo("PASS: every remote core source, absent fields, disconnect and no local sensor helper");
                    qApp->quit();
                });
            }
        });
        socket->write(R"({"type":"sample","hostname":"REMOTE_HOST","kernel":"REMOTE_KERNEL","datetime":"2001-01-02T03:04:05+00:00","uptime":12345,"cpu":[{"name":"REMOTE_CPU","i":-1}],"mem":{"total":123456},"disk":[{"name":"REMOTE_DISK"}],"net":[{"name":"REMOTE_NET"}],"net_ports":[{"proto":"tcp","local":443,"remote_addr":"192.0.2.55","state":"01"}],"proc":{"processes":314},"battery":{"present":true,"percent":77},"sensors":[{"chip":"REMOTE","label":"CPU","value":61,"type":0}]})" "\n");
    });
    QTimer::singleShot(10000, [] { qFatal("Remote isolation test timed out"); });
}
static void exercise()
{
    MainWindow *window = nullptr;
    for (QWidget *widget : QApplication::topLevelWidgets())
        if (auto *candidate = qobject_cast<MainWindow *>(widget)) window = candidate;
    if (!window) qFatal("Main window missing");
    {
        Theme theme;
        CpuMonitor cpu(&theme);
        QWidget *widget = cpu.createWidget(nullptr);
        for (const auto &mode : {QStringLiteral("aggregate"), QStringLiteral("combined"), QStringLiteral("per-core")}) {
            QSettings().setValue(QStringLiteral("monitors/cpu/mode"), mode);
            cpu.tick();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            const int wanted = mode == QStringLiteral("per-core") ? CpuStat::read().size() - 1 : 1;
            if (widget->findChildren<Chart *>().size() != wanted) qFatal("CPU mode did not change actual chart count");
        }
        delete widget;
    }
    {
        Theme theme;
        DiskStat::setReadOverride([]() {
            DiskSample a, b; a.name = QStringLiteral("test-a"); b.name = QStringLiteral("test-b");
            a.sectorsRead = 100; b.sectorsRead = 200;
            return QList<DiskSample>{a,b};
        });
        DiskMonitor disk(&theme);
        QWidget *widget = disk.createWidget(nullptr);
        QSettings().setValue(QStringLiteral("monitors/disk/mode"), QStringLiteral("composite"));
        disk.tick();
        if (widget->findChildren<Chart *>().size() != 1) qFatal("Composite disk mode not applied");
        QSettings().setValue(QStringLiteral("monitors/disk/mode"), QStringLiteral("per-disk"));
        disk.tick();
        if (widget->findChildren<Chart *>().size() != 2) qFatal("Per-drive disk mode not applied");
        delete widget;
        DiskStat::setReadOverride(nullptr);

        NetStat::setReadOverride([]() { NetSample s; s.name = QStringLiteral("test-net"); return QList<NetSample>{s}; });
        NetMonitor net(&theme);
        widget = net.createWidget(nullptr);
        QSettings().setValue(QStringLiteral("monitors/net/test-net"), false);
        net.tick();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (!widget->findChildren<Chart *>().isEmpty()) qFatal("Disabled interface stayed visible");
        QSettings().setValue(QStringLiteral("monitors/net/test-net"), true);
        net.tick();
        if (widget->findChildren<Chart *>().size() != 1) qFatal("Enabled interface stayed hidden");
        delete widget;
        NetStat::setReadOverride(nullptr);
    }
    QTcpServer listener;
    if (!listener.listen(QHostAddress::LocalHost, 0)) qFatal("TCP test listener failed");
    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, listener.serverPort());
    if (!client.waitForConnected(3000) || !listener.waitForNewConnection(3000)) qFatal("TCP test connect failed");
    QUdpSocket udp;
    if (!udp.bind(QHostAddress::LocalHost, 0)) qFatal("UDP test bind failed");
    bool tcpFound = false, udpFound = false;
    for (const auto &sample : NetPortStat::read()) {
        if (sample.protocol == QStringLiteral("tcp") && sample.localPort == listener.serverPort()
            && sample.remoteAddress == QStringLiteral("127.0.0.1") && sample.state == QStringLiteral("01")) tcpFound = true;
        if (sample.protocol == QStringLiteral("udp") && sample.localPort == udp.localPort()) udpFound = true;
    }
    if (!tcpFound || !udpFound) qFatal("Native TCP/UDP port monitoring failed");
    if (DiskStat::read().isEmpty()) qFatal("Native disk I/O missing");
    QMetaObject::invokeMethod(window, "showSettings", Qt::DirectConnection);
    auto *dialog = window->findChild<SettingsDialog *>();
    if (!dialog || dialog->isModal()) qFatal("Independent settings missing");
    auto *stack = dialog->findChild<QStackedWidget *>();
    QListWidget *sidebar = nullptr;
    for (auto *list : dialog->findChildren<QListWidget *>())
        if (list->count() && list->item(0)->text() == QStringLiteral("General")) sidebar = list;
    if (!sidebar || !stack) qFatal("Settings navigation missing");
    const QStringList required{QStringLiteral("General"), QStringLiteral("Appearance"),
        QStringLiteral("Monitors"), QStringLiteral("Order"), QStringLiteral("CPU"), QStringLiteral("Network")};
    for (const auto &name : required) {
        const auto matches = sidebar->findItems(name, Qt::MatchExactly);
        if (matches.size() != 1) qFatal("Required settings page missing");
        sidebar->setCurrentItem(matches.first());
        QApplication::processEvents();
        QWidget *page = stack->currentWidget();
        for (auto *box : page->findChildren<QCheckBox *>()) {
            if (!box->isEnabled()) continue;
            const bool original = box->isChecked();
            box->setChecked(!original);
            QApplication::processEvents();
            box->setChecked(original);
            QApplication::processEvents();
        }
        for (auto *spin : page->findChildren<QSpinBox *>()) {
            const int original = spin->value();
            spin->setValue(qMin(spin->maximum(), original + spin->singleStep()));
            QApplication::processEvents();
            spin->setValue(original);
        }
        for (auto *combo : page->findChildren<QComboBox *>()) {
            const int original = combo->currentIndex();
            for (int i = 0; i < combo->count(); ++i) {
                combo->setCurrentIndex(i);
                QApplication::processEvents();
                if (name == QStringLiteral("CPU") && QSettings().value(QStringLiteral("monitors/cpu/mode")).toString() != combo->currentData().toString()
                    && i != original) qFatal("CPU mode not saved live");
            }
            combo->setCurrentIndex(original);
        }
        if (name == QStringLiteral("Order")) {
            auto *list = page->findChild<QListWidget *>();
            list->setCurrentRow(1);
            for (auto *button : page->findChildren<QPushButton *>())
                if (button->text() == QStringLiteral("Up")) button->click();
            QApplication::processEvents();
            if (QSettings().value(QStringLiteral("monitors/order")).toString().isEmpty()) qFatal("Order not saved");
        }
        if (name == QStringLiteral("CPU")) {
            for (auto *button : page->findChildren<QPushButton *>()) {
                if (button->text() != QStringLiteral("CPU Settings...")) continue;
                QTimer::singleShot(100, []() {
                    auto *advanced = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                    if (!advanced) qFatal("CPU advanced settings missing");
                    advanced->findChild<QLineEdit *>()->setText(QStringLiteral("{name}: {index}"));
                    for (auto *spin : advanced->findChildren<QSpinBox *>()) spin->setValue(spin->value() - 1);
                    advanced->accept();
                });
                button->click();
                QApplication::processEvents();
                if (QSettings().value(QStringLiteral("monitors/cpu/label_format")).toString() != QStringLiteral("{name}: {index}"))
                    qFatal("CPU advanced settings not saved");
            }
        }
        if (name == QStringLiteral("Network")) {
            for (auto *edit : page->findChildren<QLineEdit *>()) {
                const bool ports = edit->placeholderText().contains(QStringLiteral("8000"));
                edit->setText(ports ? QStringLiteral("22,443,60000-61000") : QStringLiteral("Test watch"));
                QMetaObject::invokeMethod(edit, "editingFinished", Qt::DirectConnection);
            }
            QApplication::processEvents();
            for (int i = 1; i <= 8; ++i) {
                const QString key = QStringLiteral("monitors/netports/watch%1/").arg(i);
                if (QSettings().value(key + QStringLiteral("ports")).toString() != QStringLiteral("22,443,60000-61000")
                    || QSettings().value(key + QStringLiteral("label")).toString() != QStringLiteral("Test watch"))
                    qFatal("Network watch settings not saved");
            }
        }
        if (!dialog->grab().save(name + QStringLiteral(".png"))) qFatal("Screenshot failed");
        ++pagesTested;
    }
    if (QSettings().status() != QSettings::NoError) qFatal("Settings not writable");
    window->grab().save(QStringLiteral("Monitor.png"));
    dialog->close();
    QTimer::singleShot(100, window, [window]() {
        if (!window->isVisible()) qFatal("Monitor disappeared after settings");
        window->close();
    });
}
static void scheduleTest() { if (remoteTest) startRemoteTest(); else QTimer::singleShot(500, exercise); }
Q_COREAPP_STARTUP_FUNCTION(scheduleTest)

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
        if (QByteArray(argv[i]) == "remote-isolation") remoteTest = true;
    if (remoteTest) {
        QTemporaryDir config;
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.path());
        const int result = krellixMain(argc, argv);
        if (remoteSamples != 2) qFatal("Missing remote isolation samples");
        return result;
    }
    if (CpuStat::read().size() < 2) qFatal("Per-core CPU samples missing");
    const auto memory = MemStat::read();
    if (!memory.totalKb || memory.availableKb > memory.totalKb) qFatal("Invalid memory");
    if (UptimeStat::secondsSinceBoot() <= 0) qFatal("Missing uptime");
    if (ProcStat::read().processes < 1) qFatal("Missing processes");
    if (NetStat::read().isEmpty()) qFatal("Missing interfaces");
    QTemporaryDir config;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.path());
    const int result = krellixMain(argc, argv);
    if (pagesTested != 6) qFatal("Not all settings pages tested");
    qInfo("PASS: Windows native monitors and six core settings pages");
    return result;
}
