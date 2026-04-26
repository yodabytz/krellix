#pragma once

#include "MonitorBase.h"
#include "sysdep/CpuStat.h"

#include "widgets/Chart.h"

#include <QList>
#include <QPointer>

class Chart;
class Decal;
class Krell;
class QVBoxLayout;
class QWidget;

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

    // Mode-specific UI: in per-core mode m_cores[i] corresponds to the
    // i-th visible core. In aggregate mode only m_aggregateUI is used.
    enum class Mode { PerCore, Aggregate, Combined };

    // Lazily build the per-mode panel layout from a non-empty samples
    // list. Called from createWidget when samples are available, and
    // from tick() when the first remote sample arrives in --host mode
    // (createWidget ran before any data had landed).
    void buildPanels(const QList<CpuSample> &samples);

    QPointer<QWidget>      m_container;
    QPointer<QVBoxLayout>  m_containerLayout;
    QPointer<QWidget>      m_placeholderPanel;     // "(waiting for data...)" panel
    bool                   m_panelsBuilt = false;

    QList<CoreUI>    m_cores;
    QList<int>       m_visibleCoreIndices;   // /proc/stat indices we actually show
    CoreUI           m_aggregateUI;
    QPointer<Chart>  m_combinedChart;
    QList<int>       m_combinedCoreIndices;
    Mode             m_mode = Mode::PerCore;
    bool             m_aggregateMode = false;  // legacy alias for backwards compat

    QList<CpuSample> m_prevSamples;
    bool             m_havePrev = false;

    Q_DISABLE_COPY_MOVE(CpuMonitor)
};
