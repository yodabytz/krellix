#pragma once
#include "sdk/KrellixPlugin.h"
#include "monitors/MonitorBase.h"
#include "BluetoothData.h"
#include <QElapsedTimer>
#include <QPointer>
#include <QProcess>
#include <QTimer>

class WindowsBluetoothMonitor final : public MonitorBase {
    Q_OBJECT
public:
    WindowsBluetoothMonitor(Theme *theme, QObject *parent);
    ~WindowsBluetoothMonitor() override;
    QString id() const override { return QStringLiteral("krellbluetooth"); }
    QString displayName() const override { return QStringLiteral("Bluetooth"); }
    int tickIntervalMs() const override { return 1000; }
    QWidget *createWidget(QWidget *parent) override;
    void tick() override;
    void shutdown() override;
private:
    void display(const WindowsBluetooth::Snapshot &snapshot);
    void unavailable(const QString &message);
    void finish();
    QPointer<QWidget> m_rows;
    QProcess *m_probe = nullptr;
    QTimer m_timeout;
    QElapsedTimer m_attempt;
    QByteArray m_output;
    bool m_stopped = false;
};
class KrellbluetoothPlugin final : public QObject, public IKrellixPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID KrellixPlugin_iid)
    Q_INTERFACES(IKrellixPlugin)
public:
    QString pluginId() const override { return QStringLiteral("io.krellix.krellbluetooth"); }
    QString pluginName() const override { return QStringLiteral("Bluetooth"); }
    QString pluginVersion() const override { return QStringLiteral("0.1.2-windows.1"); }
    QList<MonitorBase *> createMonitors(Theme *theme, QObject *parent) override;
};
