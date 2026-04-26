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

// One panel per non-loopback interface: text decal "RX 1.2K  TX 0.4K",
// RX krell, TX krell, and a combined-throughput chart.
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

private:
public:
    struct IfaceUI {
        QPointer<Decal> textDecal;
        QPointer<Krell> rxKrell;
        QPointer<Krell> txKrell;
        QPointer<Chart> chart;
        double          maxBps = 1024.0 * 1024.0;  // adaptive scale
    };
private:

    QHash<QString, IfaceUI>    m_ifaces;
    QHash<QString, NetSample>  m_prevSamples;
    QPointer<QWidget>          m_container;     // for lazy-add of new ifaces
    QPointer<QVBoxLayout>      m_containerLayout;
    QElapsedTimer              m_lastReadTimer;
    bool                       m_havePrev = false;

    Q_DISABLE_COPY_MOVE(NetMonitor)
};
