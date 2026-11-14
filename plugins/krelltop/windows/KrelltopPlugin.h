#pragma once

#include "monitors/MonitorBase.h"
#include "sdk/KrellixPlugin.h"

#include <QList>
#include <QPointer>
#include <QString>
#include <QElapsedTimer>
#include <memory>
#include "WindowsProcesses.h"

class Decal;
class Panel;
class QTcpSocket;
class QTimer;

class KrelltopMonitor : public MonitorBase
{
    Q_OBJECT

public:
    explicit KrelltopMonitor(Theme *theme, QObject *parent = nullptr);
    ~KrelltopMonitor() override;
    void shutdown() override;

    QString id() const override { return QStringLiteral("krelltop"); }
    QString displayName() const override { return QStringLiteral("KrellTop"); }
    int tickIntervalMs() const override { return 5000; }
    QWidget *createWidget(QWidget *parent) override;
    void tick() override;

    struct ProcessRow {
        int pid = 0;
        double cpu = 0.0;
        double mem = 0.0;
        qint64 rssKiB = 0;
        QString name;
        QString command;
    };

    static QString formatRss(qint64 rssKiB);
    static QString displayNameFor(const ProcessRow &row);

private:
    QList<ProcessRow> readProcesses(QString *error);
    std::unique_ptr<WindowsProcesses> m_local;
    QElapsedTimer m_lastAttempt;
    int m_retryMs = 5000;
    bool m_stopped = false;

    void setRows(const QList<ProcessRow> &byCpu, const QList<ProcessRow> &byMem);
    void setUnavailable(const QString &message);
    void fetchRemote(const QString &address);
    QTcpSocket *m_socket = nullptr;
    QTimer *m_timeout = nullptr;
    QByteArray m_buffer;

    QPointer<Panel> m_panel;
    QList<QPointer<Decal>> m_rows;
};

class KrelltopPlugin : public QObject, public IKrellixPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID KrellixPlugin_iid)
    Q_INTERFACES(IKrellixPlugin)

public:
    QString pluginId() const override { return QStringLiteral("io.krellix.krelltop"); }
    QString pluginName() const override { return QStringLiteral("KrellTop"); }
    QString pluginVersion() const override { return QStringLiteral("0.1.4-windows.1"); }
    QList<MonitorBase *> createMonitors(Theme *theme, QObject *parent) override;
};
