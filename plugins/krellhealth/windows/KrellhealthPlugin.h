#pragma once
#include "sdk/KrellixPlugin.h"
#include "monitors/MonitorBase.h"
#include "HealthData.h"
#include <QElapsedTimer>
#include <QPointer>
#include <QProcess>
#include <QTcpSocket>
#include <QTimer>
class Panel;
class Decal;
class WindowsHealthMonitor final : public MonitorBase {
    Q_OBJECT
public:
    WindowsHealthMonitor(Theme *theme, QObject *parent);
    ~WindowsHealthMonitor() override;
    QString id() const override { return QStringLiteral("krellhealth"); }
    QString displayName() const override { return QStringLiteral("KrellHealth"); }
    int tickIntervalMs() const override { return 1000; }
    QWidget *createWidget(QWidget *parent) override;
    void tick() override;
    void shutdown() override;
private:
    void local();
    void remote(const QString &address);
    void unavailable(const QString &reason);
    void display(const HealthData &data);
    QPointer<Panel> m_panel;
    QPointer<Decal> m_summary, m_detail;
    QProcess *m_probe = nullptr;
    QTcpSocket *m_socket = nullptr;
    QTimer m_deadline;
    QElapsedTimer m_attempt;
    QByteArray m_buffer;
    bool m_stopped = false;
};
class KrellhealthPlugin final : public QObject, public IKrellixPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID KrellixPlugin_iid)
    Q_INTERFACES(IKrellixPlugin)
public:
    QString pluginId() const override { return QStringLiteral("io.krellix.krellhealth"); }
    QString pluginName() const override { return QStringLiteral("KrellHealth"); }
    QString pluginVersion() const override { return QStringLiteral("0.1.1-windows.3"); }
    QList<MonitorBase *> createMonitors(Theme *theme, QObject *parent) override;
};
