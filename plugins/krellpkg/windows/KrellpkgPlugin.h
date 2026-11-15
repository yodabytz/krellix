#pragma once
#include "sdk/KrellixPlugin.h"
#include "monitors/MonitorBase.h"
#include "PackageData.h"
#include <QElapsedTimer>
#include <QPointer>
#include <QProcess>
#include <QTimer>
class Decal;
class WindowsPackageMonitor final : public MonitorBase {
    Q_OBJECT
public:
    WindowsPackageMonitor(Theme *theme, QObject *parent);
    ~WindowsPackageMonitor() override;
    QString id() const override { return QStringLiteral("krellpkg"); }
    QString displayName() const override { return QStringLiteral("Packages"); }
    int tickIntervalMs() const override { return 1000; }
    QWidget *createWidget(QWidget *parent) override;
    void tick() override;
    void shutdown() override;
private slots:
    void updateRemote();
private:
    void finish();
    void display(const QString &text, const QString &details = {});
    void renderLocal();
    void failed(const QString &detail);
    QPointer<Decal> m_summary;
    QProcess *m_probe = nullptr;
    QTimer m_timeout;
    QElapsedTimer m_attempt;
    QByteArray m_output;
    WindowsPackages::Snapshot m_snapshot;
    bool m_stopped = false;
};
class KrellpkgPlugin final : public QObject, public IKrellixPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID KrellixPlugin_iid)
    Q_INTERFACES(IKrellixPlugin)
public:
    QString pluginId() const override { return QStringLiteral("io.krellix.krellpkg"); }
    QString pluginName() const override { return QStringLiteral("Krellpkg"); }
    QString pluginVersion() const override { return QStringLiteral("0.1.2-windows.1"); }
    QList<MonitorBase *> createMonitors(Theme *theme, QObject *parent) override;
};
