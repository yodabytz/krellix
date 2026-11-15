#include "KrellbluetoothPlugin.h"
#include "remote/RemoteSource.h"
#include "theme/Theme.h"
#include "widgets/Panel.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPainter>
#include <QSettings>
#include <QVBoxLayout>

namespace {
class DeviceRow final : public QWidget {
    Q_OBJECT
public:
    DeviceRow(Theme *theme, QWidget *parent) : QWidget(parent), m_theme(theme) {
        setObjectName(QStringLiteral("bluetoothDeviceRow"));
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        // String-based lookup avoids imported signal-function thunk identity
        // differences across the Windows host/plugin DLL boundary.
        connect(theme, SIGNAL(themeChanged()), this, SLOT(refreshTheme()));
    }
    QSize sizeHint() const override { return {100, QFontMetrics(m_theme->font("label")).height() + 4}; }
    void setContent(const QString &name, const QString &value) {
        m_name = name; m_value = value;
        setAccessibleName(name); setAccessibleDescription(value); update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setFont(m_theme->font("label"));
        painter.setPen(m_theme->color("text_accent", m_theme->color("text_primary", Qt::white)));
        const QFontMetrics metrics(painter.font());
        const int reserved = m_value.isEmpty() ? 0 : metrics.horizontalAdvance(m_value) + 5;
        painter.drawText(rect().adjusted(0, 0, -reserved, 0), Qt::AlignVCenter | Qt::AlignLeft,
            metrics.elidedText(m_name, Qt::ElideRight, qMax(0, width() - reserved)));
        painter.setPen(m_theme->color("text_primary", Qt::white));
        painter.drawText(rect(), Qt::AlignVCenter | Qt::AlignRight, m_value);
    }
private slots:
    void refreshTheme() {
        updateGeometry(); update();
    }
private:
    Theme *m_theme;
    QString m_name, m_value;
};
}
WindowsBluetoothMonitor::WindowsBluetoothMonitor(Theme *theme, QObject *parent) : MonitorBase(theme, parent)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] { finish(); unavailable("Bluetooth helper timed out"); });
}
WindowsBluetoothMonitor::~WindowsBluetoothMonitor() { shutdown(); }
void WindowsBluetoothMonitor::finish()
{
    m_timeout.stop();
    if (!m_probe) return;
    auto *process = m_probe; m_probe = nullptr;
    disconnect(process, nullptr, this, nullptr);
    if (process->state() == QProcess::NotRunning) process->deleteLater();
    else { connect(process, &QProcess::finished, process, &QObject::deleteLater); process->kill(); }
}
void WindowsBluetoothMonitor::shutdown()
{
    m_stopped = true; m_timeout.stop();
    if (m_probe) {
        disconnect(m_probe, nullptr, this, nullptr);
        m_probe->kill(); m_probe->waitForFinished(1000); delete m_probe; m_probe = nullptr;
    }
}
QWidget *WindowsBluetoothMonitor::createWidget(QWidget *parent)
{
    auto *panel = new Panel(theme(), parent);
    panel->setTitle("Bluetooth"); panel->setSurfaceKey("panel_bg_krellbluetooth");
    m_rows = new QWidget(panel);
    auto *layout = new QVBoxLayout(m_rows);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(1);
    panel->addWidget(m_rows);
    unavailable("Checking Bluetooth...");
    QTimer::singleShot(0, this, &WindowsBluetoothMonitor::tick);
    return panel;
}
void WindowsBluetoothMonitor::display(const WindowsBluetooth::Snapshot &snapshot)
{
    if (!m_rows) return;
    auto *layout = m_rows->layout();
    const int count = qMax(1, int(snapshot.devices.size()));
    while (layout->count() > count) { auto *item = layout->takeAt(layout->count() - 1); delete item->widget(); delete item; }
    while (layout->count() < count) layout->addWidget(new DeviceRow(theme(), m_rows));
    auto tooltip = [](QWidget *row, const QString &text) {
        row->setToolTip("<qt>" + text.toHtmlEscaped().replace('\n', "<br>") + "</qt>");
    };
    if (snapshot.devices.isEmpty()) {
        auto *row = static_cast<DeviceRow *>(layout->itemAt(0)->widget());
        row->setContent(snapshot.status, {}); tooltip(row, snapshot.status + '\n' + snapshot.detail);
    }
    for (int i = 0; i < snapshot.devices.size(); ++i) {
        const auto &device = snapshot.devices[i];
        auto *row = static_cast<DeviceRow *>(layout->itemAt(i)->widget());
        row->setContent(device.name, device.battery < 0 ? QString(QChar(0x2014)) : QString::number(device.battery) + '%');
        tooltip(row, device.name + '\n' + device.address + "\nConnected\n" +
            (device.battery < 0 ? QStringLiteral("Battery unknown") : QStringLiteral("Battery: %1%").arg(device.battery))
            + '\n' + device.detail);
    }
}
void WindowsBluetoothMonitor::unavailable(const QString &message) { display({message, {}, {}, true}); }
void WindowsBluetoothMonitor::tick()
{
    if (m_stopped) return;
    if (RemoteSource::instance()) { finish(); unavailable("Local Bluetooth only"); return; }
    if (m_probe || (m_attempt.isValid() && m_attempt.elapsed() < 15000)) return;
    m_attempt.start(); m_output.clear();
    const QString helper = QCoreApplication::applicationDirPath() + "/bluetooth/krellbt-probe.exe";
    if (!QFileInfo::exists(helper)) { unavailable("Bluetooth helper missing"); return; }
    auto *process = new QProcess(this); m_probe = process;
    process->setProgram(helper);
    connect(process, &QProcess::readyReadStandardError, this, [process] { process->readAllStandardError(); });
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        m_output += process->readAllStandardOutput();
        if (m_output.size() > 256 * 1024) { finish(); m_output.clear(); unavailable("Bluetooth response too large"); }
    });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { finish(); unavailable("Bluetooth helper failed to start"); }
    });
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        m_output += process->readAllStandardOutput(); finish();
        const auto document = QJsonDocument::fromJson(m_output);
        if (code || status != QProcess::NormalExit || m_output.size() > 256 * 1024 || !document.isObject()) {
            unavailable("Bluetooth query failed"); return;
        }
        const auto snapshot = WindowsBluetooth::parse(document.object());
        if (!snapshot.valid) { unavailable("Invalid Bluetooth data"); return; }
        display(snapshot); m_output.clear();
    });
    m_timeout.start(12000); process->start();
}
QList<MonitorBase *> KrellbluetoothPlugin::createMonitors(Theme *theme, QObject *parent)
{
    if (RemoteSource::instance() || !QSettings().value("plugins/krellbluetooth/enabled", false).toBool()) return {};
    return {new WindowsBluetoothMonitor(theme, parent)};
}

#include "KrellbluetoothPlugin.moc"
