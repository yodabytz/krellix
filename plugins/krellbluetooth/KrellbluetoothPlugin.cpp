#include "KrellbluetoothPlugin.h"
#include "remote/RemoteSource.h"
#include "theme/Theme.h"
#include "widgets/Panel.h"
#include <QLabel>
#include <QPainter>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#ifdef Q_OS_LINUX
#include <QtDBus>
using BluezInterfaces = QMap<QString, QVariantMap>;
using BluezObjects = QMap<QDBusObjectPath, BluezInterfaces>;
Q_DECLARE_METATYPE(BluezInterfaces)
Q_DECLARE_METATYPE(BluezObjects)
#endif

namespace {
class DeviceRow final : public QWidget {
public:
    DeviceRow(Theme *theme, const QString &name, const QString &value, QWidget *parent)
        : QWidget(parent), m_theme(theme), m_name(name), m_value(value) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        connect(theme, &Theme::themeChanged, this, [this] { updateGeometry(); update(); });
    }
    QSize sizeHint() const override { return {100, QFontMetrics(m_theme->font(QStringLiteral("label"))).height() + 4}; }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setFont(m_theme->font(QStringLiteral("label")));
        painter.setPen(m_theme->color(QStringLiteral("text_primary"), Qt::white));
        const QFontMetrics fm(painter.font());
        const int valueWidth = m_value.isEmpty() ? 0 : fm.horizontalAdvance(m_value) + 5;
        painter.drawText(rect().adjusted(0, 0, -valueWidth, 0), Qt::AlignVCenter | Qt::AlignLeft,
                         fm.elidedText(m_name, Qt::ElideRight, qMax(0, width() - valueWidth)));
        painter.drawText(rect(), Qt::AlignVCenter | Qt::AlignRight, m_value);
    }
private:
    Theme *m_theme;
    QString m_name, m_value;
};
}

KrellbluetoothMonitor::KrellbluetoothMonitor(Theme *theme, QObject *parent) : MonitorBase(theme, parent) {}
KrellbluetoothMonitor::~KrellbluetoothMonitor()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}
QWidget *KrellbluetoothMonitor::createWidget(QWidget *parent)
{
    auto *panel = new Panel(theme(), parent);
    panel->setTitle(QStringLiteral("Bluetooth"));
    panel->setSurfaceKey(QStringLiteral("panel_bg_krellbluetooth"));
    m_rows = new QWidget(panel);
    auto *layout = new QVBoxLayout(m_rows);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(1);
    panel->addWidget(m_rows);
    display({QStringLiteral("Checking…"), {}});
    QTimer::singleShot(0, this, &KrellbluetoothMonitor::tick);
    return panel;
}
void KrellbluetoothMonitor::display(const Bluetooth::Snapshot &snapshot)
{
    if (!m_rows) return;
    auto *layout = m_rows->layout();
    while (auto *item = layout->takeAt(0)) { delete item->widget(); delete item; }
    if (snapshot.devices.isEmpty()) {
        auto *row = new DeviceRow(theme(), snapshot.status, {}, m_rows);
        row->setToolTip(snapshot.status);
        layout->addWidget(row);
    }
    for (const auto &device : snapshot.devices) {
        const QString value = device.battery >= 0 ? QStringLiteral("%1%").arg(device.battery)
            : device.batteryDetail.isEmpty() ? QStringLiteral("—") : QStringLiteral("ⓘ");
        auto *row = new DeviceRow(theme(), device.name, value, m_rows);
        row->setToolTip(device.name + QLatin1Char('\n') + device.address + QStringLiteral("\nConnected\n")
            + (device.batteryDetail.isEmpty() ? device.battery >= 0 ? QStringLiteral("Battery: %1%").arg(device.battery)
                   : QStringLiteral("Battery level not reported by this device/OS") : device.batteryDetail));
        layout->addWidget(row);
    }
}
void KrellbluetoothMonitor::tick()
{
    if (RemoteSource::instance()) { display({QStringLiteral("Local devices only"), {}}); return; }
    if (m_busy) return;
#ifdef Q_OS_LINUX
    qDBusRegisterMetaType<BluezInterfaces>();
    qDBusRegisterMetaType<BluezObjects>();
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), QStringLiteral("/"),
        QStringLiteral("org.freedesktop.DBus.ObjectManager"), QStringLiteral("GetManagedObjects"));
    m_busy = true;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message, 5000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        QDBusPendingReply<BluezObjects> reply = *watcher;
        m_busy = false;
        if (reply.isError()) {
            display({QStringLiteral("Bluetooth unavailable"), {}});
            if (m_rows) m_rows->setToolTip(QStringLiteral("BlueZ: ") + reply.error().message());
        } else {
            QJsonObject objects;
            const auto values = reply.value();
            for (auto obj = values.begin(); obj != values.end(); ++obj) {
                QJsonObject interfaces;
                for (auto iface = obj.value().begin(); iface != obj.value().end(); ++iface)
                    interfaces.insert(iface.key(), QJsonObject::fromVariantMap(iface.value()));
                objects.insert(obj.key().path(), interfaces);
            }
            if (m_rows) m_rows->setToolTip(QString());
            display(Bluetooth::parseBluez(objects));
        }
        watcher->deleteLater();
    });
#elif defined(Q_OS_MACOS) || defined(Q_OS_FREEBSD)
#ifdef Q_OS_MACOS
    const QString program = QStringLiteral("/usr/sbin/system_profiler");
    const QStringList args{QStringLiteral("SPBluetoothDataType"), QStringLiteral("-json"), QStringLiteral("-timeout"), QStringLiteral("8")};
#else
    const QString program = QStandardPaths::findExecutable(QStringLiteral("hccontrol"), {QStringLiteral("/usr/sbin"), QStringLiteral("/usr/local/sbin")});
    const QStringList args{QStringLiteral("-N"), QStringLiteral("read_connection_list")};
#endif
    if (program.isEmpty()) { display({QStringLiteral("Bluetooth tool missing"), {}}); return; }
    m_busy = true;
    auto *process = new QProcess(this);
    m_process = process;
    auto *timeout = new QTimer(process);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, process, [process] { process->kill(); });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        m_busy = false;
        display({QStringLiteral("Bluetooth unavailable"), {}});
        process->deleteLater();
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this, process, timeout](int code, QProcess::ExitStatus status) {
            timeout->stop();
            m_busy = false;
            if (code != 0 || status != QProcess::NormalExit) display({QStringLiteral("Bluetooth unavailable"), {}});
            else {
#ifdef Q_OS_MACOS
                auto snapshot = Bluetooth::parseMac(process->readAllStandardOutput());
                Bluetooth::supplementMacBatteries(snapshot);
                display(snapshot);
#else
                display(Bluetooth::parseFreeBsd(process->readAllStandardOutput()));
#endif
            }
            process->deleteLater();
        });
    process->start(program, args);
    timeout->start(10000);
#else
    display({QStringLiteral("OS not supported"), {}});
#endif
}
QList<MonitorBase *> KrellbluetoothPlugin::createMonitors(Theme *theme, QObject *parent)
{
    if (RemoteSource::instance() || !QSettings().value(QStringLiteral("plugins/krellbluetooth/enabled"), false).toBool()) return {};
    return {new KrellbluetoothMonitor(theme, parent)};
}
