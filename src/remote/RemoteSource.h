#pragma once

#include "sysdep/CpuStat.h"
#include "sysdep/SensorStat.h"
#include "sysdep/BatteryStat.h"
#include <QDateTime>
#include <QElapsedTimer>
#include "sysdep/DiskStat.h"
#include "sysdep/MemStat.h"
#include "sysdep/NetStat.h"
#include "sysdep/NetPortStat.h"
#include "sysdep/ProcStat.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

class QTcpSocket;
class QTimer;

struct StackInfo {
    int containers = 0;
    int running = 0;
    int images = 0;
    int compose = 0;
    int unhealthy = 0;
    int dockerNetworks = 0;
    QString source;
    QString error;
};

struct HealthFindingInfo {
    QString severity;
    QString title;
    QString detail;
};

struct HealthInfo {
    int critical = 0;
    int warnings = 0;
    QList<HealthFindingInfo> findings;
};

struct TopProcessInfo {
    int pid = 0;
    double cpu = 0.0;
    double mem = 0.0;
    qint64 rssKiB = 0;
    QString name;
    QString command;
};

// Client-side counterpart of krellixd. Maintains a TCP connection,
// parses JSON-lines messages, and caches the latest sample so that the
// per-monitor sysdep::read() overrides can return remote data
// synchronously. Reconnects automatically with backoff on disconnect.
//
// Hardened to match the server's posture: bounded line buffer, hard cap
// on buffered bytes, JSON parse failures don't kill the connection but
// do increment a counter; persistently-broken streams get reset.
class RemoteSource : public QObject
{
    Q_OBJECT

public:
    explicit RemoteSource(QObject *parent = nullptr);
    ~RemoteSource() override;

    // Singleton accessor — sysdep override callbacks find the active
    // source through here.
    static RemoteSource *instance();

    void connectToHost(const QString &host, quint16 port);
    bool isConnected() const;

    QString  remoteHostname() const { return m_hostname; }
    QString  remoteKernel()   const { return m_kernel; }
    QString  remoteAddress()  const;
    qint64   uptimeSeconds()  const { return m_uptime; }
    QList<CpuSample>  cpuSamples()  const { return m_cpu; }
    MemInfo           memInfo()     const { return m_mem; }
    QList<NetSample>  netSamples()  const { return m_net; }
    QList<NetPortSample> netPortSamples() const { return m_netPorts; }
    QList<DiskSample> diskSamples() const { return m_disk; }
    ProcInfo          procInfo()    const { return m_proc; }
    StackInfo         stackInfo()   const { return m_stack; }
    HealthInfo        healthInfo()  const { return m_health; }
    QJsonObject       packageInfo() const { return m_packages; }
    QList<TopProcessInfo> topProcesses() const { return m_top; }

    QList<SensorReading> sensorReadings() const { return m_sensors; }
    BatteryInfo batteryInfo() const { return m_battery; }
    QDateTime remoteTime() const { return m_time.isValid() ? m_time.addMSecs(m_timeAge.elapsed()) : QDateTime{}; }

signals:
    void connectionStateChanged(bool connected);
    void sampleReceived();
    void retriesExhausted();

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onReadyRead();
    void onSocketError();
    void scheduleReconnect();

private:
    void parseLine(const QByteArray &line);
    void resetCachedState();

    QTcpSocket *m_socket;
    QTimer     *m_reconnectTimer;

    QString    m_host;
    quint16    m_port = 19150;

    QByteArray m_buffer;     // line-accumulator; flushed on each '\n'

    // Most-recent cached sample.
    QString           m_hostname;
    QString           m_kernel;
    qint64            m_uptime = 0;
    QList<CpuSample>  m_cpu;
    MemInfo           m_mem;
    QList<NetSample>  m_net;
    QList<NetPortSample> m_netPorts;
    QList<DiskSample> m_disk;
    ProcInfo          m_proc;
    StackInfo         m_stack;
    HealthInfo        m_health;
    QList<TopProcessInfo> m_top;

    QList<SensorReading> m_sensors;
    BatteryInfo m_battery;
    QDateTime m_time;
    QElapsedTimer m_timeAge;
    int               m_consecutiveParseErrors = 0;
    int               m_reconnectAttempt       = 0;

    // Append telemetry fields to preserve offsets used by existing plugins.
    QJsonObject       m_packages;

    Q_DISABLE_COPY_MOVE(RemoteSource)
};
