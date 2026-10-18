#pragma once

#include "monitors/MonitorBase.h"
#include "sdk/KrellixPlugin.h"

#include <QDateTime>
#include <QPointer>
#include <QStringList>

class Decal;
class HealthStatusBadge;
class QProcess;

class KrellhealthMonitor : public MonitorBase
{
    Q_OBJECT

public:
    explicit KrellhealthMonitor(Theme *theme, QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("krellhealth"); }
    QString displayName() const override { return QStringLiteral("KrellHealth"); }
    int tickIntervalMs() const override { return 60000; }
    QWidget *createWidget(QWidget *parent) override;
    void tick() override;

private:
    enum class Severity {
        Info,
        Warn,
        Critical,
    };

    struct Finding {
        Severity severity = Severity::Info;
        QString title;
        QString detail;
    };

    struct CheckJob {
        QString id;
        QString label;
        QString program;
        QStringList args;
        int timeoutMs = 2500;
    };

    void addLocalChecks();
    void startNextJob();
    void finishJob(const CheckJob &job, const QString &output, int exitCode);
    void addFinding(Severity severity, const QString &title, const QString &detail = {});
    void updateDisplay();

    QPointer<HealthStatusBadge> m_summary;
    QPointer<Decal> m_detail;
    QPointer<QWidget> m_panel;
    QPointer<QProcess> m_process;
    QList<CheckJob> m_pendingJobs;
    QList<Finding> m_findings;
    bool m_updating = false;
};

class KrellhealthPlugin : public QObject, public IKrellixPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID KrellixPlugin_iid)
    Q_INTERFACES(IKrellixPlugin)

public:
    QString pluginId() const override { return QStringLiteral("io.krellix.krellhealth"); }
    QString pluginName() const override { return QStringLiteral("KrellHealth"); }
    QString pluginVersion() const override { return QStringLiteral("0.1.0"); }
    QList<MonitorBase *> createMonitors(Theme *theme, QObject *parent) override;
};
