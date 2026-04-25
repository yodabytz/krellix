#pragma once

#include "MonitorBase.h"
#include "sysdep/CpuStat.h"

#include <QList>
#include <QPointer>

class Chart;
class Decal;
class Krell;

// Per-core CPU monitor: one Panel per core (cpu0, cpu1, ...) holding a
// percent decal, a krell (instant utilization), and a chart (1 Hz history).
class CpuMonitor : public MonitorBase
{
    Q_OBJECT

public:
    explicit CpuMonitor(Theme *theme, QObject *parent = nullptr);
    ~CpuMonitor() override;

    QString id() const override          { return QStringLiteral("cpu"); }
    QString displayName() const override { return QStringLiteral("CPU"); }
    int     tickIntervalMs() const override { return 1000; }

    QWidget *createWidget(QWidget *parent) override;
    void     tick() override;

private:
    struct CoreUI {
        QPointer<Decal> valueDecal;
        QPointer<Krell> krell;
        QPointer<Chart> chart;
    };

    QList<CoreUI>    m_cores;
    QList<CpuSample> m_prevSamples;
    bool             m_havePrev = false;

    Q_DISABLE_COPY_MOVE(CpuMonitor)
};
