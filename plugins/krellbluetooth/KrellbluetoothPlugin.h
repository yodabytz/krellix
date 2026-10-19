#pragma once
#include "monitors/MonitorBase.h"
#include "sdk/KrellixPlugin.h"
#include "BluetoothData.h"
#include <QPointer>

class QProcess;
class QVBoxLayout;
class KrellbluetoothMonitor : public MonitorBase {
    Q_OBJECT
public:
    explicit KrellbluetoothMonitor(Theme *theme, QObject *parent = nullptr);
    ~KrellbluetoothMonitor() override;
    QString id() const override { return QStringLiteral("krellbluetooth"); }
    QString displayName() const override { return QStringLiteral("KrellBluetooth"); }
    int tickIntervalMs() const override { return 15000; }
    QWidget *createWidget(QWidget *parent) override;
    void tick() override;
private:
    friend class BluetoothMonitorTest;
    void display(const Bluetooth::Snapshot &snapshot);
    QPointer<QWidget> m_rows;
    QPointer<QProcess> m_process;
    bool m_busy = false;
};

class KrellbluetoothPlugin : public QObject, public IKrellixPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID KrellixPlugin_iid)
    Q_INTERFACES(IKrellixPlugin)
public:
    QString pluginId() const override { return QStringLiteral("io.krellix.krellbluetooth"); }
    QString pluginName() const override { return QStringLiteral("KrellBluetooth"); }
    QString pluginVersion() const override { return QStringLiteral("0.1.0"); }
    QList<MonitorBase *> createMonitors(Theme *theme, QObject *parent) override;
};
