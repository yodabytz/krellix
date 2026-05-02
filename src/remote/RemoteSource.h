#pragma once

#include "sysdep/CpuStat.h"
#include "sysdep/DiskStat.h"
#include "sysdep/MemStat.h"
#include "sysdep/NetStat.h"
#include "sysdep/ProcStat.h"

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>

class QTcpSocket;
class QTimer;

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
    QList<DiskSample> diskSamples() const { return m_disk; }
    ProcInfo          procInfo()    const { return m_proc; }

signals:
    void connectionStateChanged(bool connected);
    void sampleReceived();

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
    QList<DiskSample> m_disk;
    ProcInfo          m_proc;

    int               m_consecutiveParseErrors = 0;
    int               m_reconnectDelayMs       = 1000;

    Q_DISABLE_COPY_MOVE(RemoteSource)
};
