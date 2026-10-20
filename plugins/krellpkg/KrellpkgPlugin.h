#pragma once

#include "monitors/MonitorBase.h"
#include "sdk/KrellixPlugin.h"

#include <QPointer>
#include <QStringList>

class Decal;
class QProcess;

class KrellpkgMonitor : public MonitorBase
{
    Q_OBJECT

public:
    explicit KrellpkgMonitor(Theme *theme, QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("krellpkg"); }
    QString displayName() const override { return QStringLiteral("Krellpkg"); }
    int tickIntervalMs() const override { return 300000; }
    QWidget *createWidget(QWidget *parent) override;
    void tick() override;

    struct CheckJob {
        QString id;
        QString label;
        QString program;
        QStringList args;
        int skipHeaderLines = 0;
    };

private:
    friend class PackageMonitorTest;
    void startNextCheck();
    void updateRemote();
    void finishCheck(const CheckJob &job, const QString &output);
    void updateDisplay();

    QPointer<Decal> m_summary;
    QPointer<QProcess> m_process;
    QList<CheckJob> m_pendingJobs;
    QStringList m_details;
    bool m_updating = false;
    QStringList m_errors;
    int m_checks = 0;
};

class KrellpkgPlugin : public QObject, public IKrellixPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID KrellixPlugin_iid)
    Q_INTERFACES(IKrellixPlugin)

public:
    QString pluginId() const override { return QStringLiteral("io.krellix.krellpkg"); }
    QString pluginName() const override { return QStringLiteral("Krellpkg"); }
    QString pluginVersion() const override { return QStringLiteral("0.1.1"); }
    QList<MonitorBase *> createMonitors(Theme *theme, QObject *parent) override;
};
