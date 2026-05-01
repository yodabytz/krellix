#pragma once

#include "MonitorBase.h"
#include "sysdep/NetStat.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QVBoxLayout>

class Chart;
class Decal;
class Krell;

// One panel per non-loopback interface: RX/TX krells and a combined
// throughput chart with the RX/TX text overlaid in the chart heading.
class NetMonitor : public MonitorBase
{
    Q_OBJECT

public:
    explicit NetMonitor(Theme *theme, QObject *parent = nullptr);
    ~NetMonitor() override;

    QString id() const override          { return QStringLiteral("net"); }
    QString displayName() const override { return QStringLiteral("Net"); }
    int     tickIntervalMs() const override { return 1000; }

    QWidget *createWidget(QWidget *parent) override;
    void     tick() override;

    struct IfaceUI {
        QPointer<Krell> rxKrell;
        QPointer<Krell> txKrell;
        QPointer<Chart> chart;
        double          maxBps = 1024.0 * 1024.0;  // adaptive scale

        // Used only by the synthetic combined docker0 panel: each
        // attached Docker bridge gets its own colored series in the
        // chart so the user can see at a glance which network is
        // doing the work. dockerBridges is the ordered list (one entry
        // per chart series), dockerPrev is the previous-tick counters
        // for each bridge so we can diff for per-series throughput.
        bool                       dockerMultiSeries = false;
        QStringList                dockerBridges;
        QHash<QString, NetSample>  dockerPrev;
    };

    // Wire docker0's chart up as a multi-series rainbow with one line
    // per Docker bridge, so the combined panel shows which network is
    // doing the work.
    void setupDockerMultiSeries(IfaceUI &ui,
                                const QList<NetSample> &samples);

private:

    QHash<QString, IfaceUI>    m_ifaces;
    QHash<QString, NetSample>  m_prevSamples;
    QPointer<QWidget>          m_container;     // for lazy-add of new ifaces
    QPointer<QVBoxLayout>      m_containerLayout;
    QPointer<QWidget>          m_placeholderPanel;  // "(waiting for data...)"
    QElapsedTimer              m_lastReadTimer;
    bool                       m_havePrev = false;

    Q_DISABLE_COPY_MOVE(NetMonitor)
};
