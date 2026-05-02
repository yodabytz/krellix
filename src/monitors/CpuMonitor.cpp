#include "CpuMonitor.h"

#include "widgets/Chart.h"
#include "widgets/Decal.h"
#include "widgets/Krell.h"
#include "widgets/Panel.h"

#include <QSettings>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace {

QList<CpuSample> sortedCoreSamples(const QList<CpuSample> &samples)
{
    QList<CpuSample> cores;
    for (const CpuSample &s : samples) {
        if (s.index >= 0)
            cores.append(s);
    }
    std::sort(cores.begin(), cores.end(), [](const CpuSample &a, const CpuSample &b) {
        return a.index < b.index;
    });
    return cores;
}

QString displayCpuName(int cpuIndex)
{
    return QStringLiteral("cpu%1").arg(cpuIndex);
}

} // namespace

CpuMonitor::CpuMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
}

CpuMonitor::~CpuMonitor() = default;

QWidget *CpuMonitor::createWidget(QWidget *parent)
{
    auto *container = new QWidget(parent);
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *vbox = new QVBoxLayout(container);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);
    m_container       = container;
    m_containerLayout = vbox;

    const QList<CpuSample> samples = CpuStat::read();

    if (samples.isEmpty()) {
        // No data yet — typical in --host mode where the first remote
        // sample arrives a tick after createWidget(). Drop a placeholder
        // and rebuild the real panels in tick() when samples land.
        auto *p = new Panel(theme(), container);
        p->setSurfaceKey(QStringLiteral("panel_bg_cpu"));
        p->setTitle(QStringLiteral("CPU"));
        Decal *d = p->addDecal(QStringLiteral("label"),
                               QStringLiteral("text_secondary"));
        d->setAlignment(Qt::AlignHCenter);
        d->setText(QStringLiteral("(waiting for data...)"));
        vbox->addWidget(p);
        m_placeholderPanel = p;
        return container;
    }

    buildPanels(samples);
    return container;
}

void CpuMonitor::buildPanels(const QList<CpuSample> &samples)
{
    if (m_panelsBuilt || samples.isEmpty()
        || !m_container || !m_containerLayout) return;

    // Discard the "(waiting for data...)" placeholder if we put one up.
    if (m_placeholderPanel) {
        m_containerLayout->removeWidget(m_placeholderPanel);
        m_placeholderPanel->deleteLater();
        m_placeholderPanel = nullptr;
    }

    QSettings s;
    const QString mode = s.value(QStringLiteral("monitors/cpu/mode"),
                                 QStringLiteral("per-core")).toString();
    if      (mode == QStringLiteral("aggregate")) m_mode = Mode::Aggregate;
    else if (mode == QStringLiteral("combined"))  m_mode = Mode::Combined;
    else                                          m_mode = Mode::PerCore;
    m_aggregateMode = (m_mode == Mode::Aggregate);

    QWidget *container = m_container;
    QVBoxLayout *vbox  = m_containerLayout;
    const QList<CpuSample> cores = sortedCoreSamples(samples);

    // Combined mode: ONE panel, ONE chart, with one line per core (each
    // in a distinct hue from the rainbow palette). Saves the most space
    // on many-core boxes while still showing every core individually.
    if (m_mode == Mode::Combined) {
        auto *p = new Panel(theme(), container);
        p->setSurfaceKey(QStringLiteral("panel_bg_cpu"));
        Krell *aggKrell = p->addKrell();
        Chart *chart = p->addChart();
        chart->setMaxValue(1.0);
        const int nCores = cores.size();
        chart->setRainbowSeries(nCores);
        chart->setOverlayText(QStringLiteral("CPU x%1  0%").arg(nCores));

        m_combinedChart = chart;
        m_aggregateUI.krell = aggKrell;
        for (const CpuSample &core : cores)
            m_combinedCoreIndices.append(core.index);
        vbox->addWidget(p);
    } else if (m_mode == Mode::Aggregate) {
        // Per-CPU panel layout is intentionally compact: just a krell
        // row and a chart, with the core name + current % drawn as an
        // overlay inside the chart (top-left). Saves a decal row.
        auto *p = new Panel(theme(), container);
        p->setSurfaceKey(QStringLiteral("panel_bg_cpu"));
        CoreUI ui;
        ui.krell = p->addKrell();
        ui.chart = p->addChart(QStringLiteral("chart_line_cpu"));
        if (ui.chart) {
            ui.chart->setMaxValue(1.0);
            ui.chart->setOverlayText(QStringLiteral("CPU 0%"));
        }
        m_aggregateUI = ui;
        vbox->addWidget(p);
    } else {
        for (int slot = 0; slot < cores.size(); ++slot) {
            const CpuSample &smp = cores[slot];
            const QString key = QStringLiteral("monitors/cpu/") +
                                QString::number(smp.index);
            const bool enabled = s.value(key, true).toBool();
            if (!enabled) continue;

            auto *p = new Panel(theme(), container);
            p->setSurfaceKey(QStringLiteral("panel_bg_cpu"));
            CoreUI ui;
            ui.krell = p->addKrell();
            ui.chart = p->addChart(QStringLiteral("chart_line_cpu"));
            if (ui.chart) {
                ui.chart->setMaxValue(1.0);
                ui.chart->setOverlayText(displayCpuName(smp.index) + QStringLiteral("  0%"));
            }
            m_cores.append(ui);
            m_visibleCoreIndices.append(smp.index);
            m_visibleCoreLabels.append(displayCpuName(smp.index));
            vbox->addWidget(p);
        }
        if (m_cores.isEmpty()) {
            auto *p = new Panel(theme(), container);
            p->setSurfaceKey(QStringLiteral("panel_bg_cpu"));
            p->setTitle(QStringLiteral("CPU"));
            Decal *d = p->addDecal(QStringLiteral("label"),
                                   QStringLiteral("text_secondary"));
            d->setAlignment(Qt::AlignHCenter);
            d->setText(QStringLiteral("(all cores disabled)"));
            vbox->addWidget(p);
        }
    }

    m_prevSamples = samples;
    m_havePrev    = true;
    m_panelsBuilt = true;
}

void CpuMonitor::tick()
{
    const QList<CpuSample> samples = CpuStat::read();
    if (samples.isEmpty()) return;

    // First sample arrived after createWidget — build the real panels
    // now (typical in --host mode where the remote sample lands a tick
    // after the monitor was constructed).
    if (!m_panelsBuilt) {
        buildPanels(samples);
        return;        // need a second sample to compute utilization
    }

    if (m_mode == Mode::Combined) {
        if (m_havePrev && m_combinedChart && !m_combinedCoreIndices.isEmpty()) {
            // Per-core series + aggregate krell at the top.
            int sumPct = 0;
            int counted = 0;
            const int n = m_combinedCoreIndices.size();
            for (int slot = 0; slot < n; ++slot) {
                const int wantIdx = m_combinedCoreIndices[slot];
                const CpuSample *prev = nullptr, *curr = nullptr;
                for (const CpuSample &c : m_prevSamples)
                    if (c.index == wantIdx) { prev = &c; break; }
                for (const CpuSample &c : samples)
                    if (c.index == wantIdx) { curr = &c; break; }
                if (!prev || !curr) continue;
                const double util = CpuStat::utilization(*prev, *curr);
                m_combinedChart->appendSampleAt(slot, util);
                sumPct += static_cast<int>(util * 100.0 + 0.5);
                ++counted;
            }
            // Aggregate krell uses the kernel's "cpu" line for accuracy.
            if (m_aggregateUI.krell && !m_prevSamples.isEmpty()) {
                const double agg = CpuStat::utilization(
                    m_prevSamples.first(), samples.first());
                m_aggregateUI.krell->setValue(agg);
            }
            if (counted > 0) {
                m_combinedChart->setOverlayText(
                    QStringLiteral("CPU x%1  avg %2%")
                        .arg(counted).arg(sumPct / counted));
            }
        }
    } else if (m_mode == Mode::Aggregate) {
        if (m_havePrev && !m_prevSamples.isEmpty()) {
            const double util =
                CpuStat::utilization(m_prevSamples.first(), samples.first());
            if (m_aggregateUI.krell) m_aggregateUI.krell->setValue(util);
            if (m_aggregateUI.chart) {
                m_aggregateUI.chart->appendSample(util);
                const int pct = static_cast<int>(util * 100.0 + 0.5);
                m_aggregateUI.chart->setOverlayText(
                    QStringLiteral("CPU %1%").arg(pct));
            }
        }
    } else if (m_havePrev) {
        // Match the visible cores to the current samples by kernel CPU
        // index — robust to hotplug or sample-count change.
        for (int slot = 0; slot < m_cores.size()
                           && slot < m_visibleCoreIndices.size(); ++slot) {
            const int wantIdx = m_visibleCoreIndices[slot];

            const CpuSample *prev = nullptr;
            for (const CpuSample &s : m_prevSamples)
                if (s.index == wantIdx) { prev = &s; break; }
            const CpuSample *curr = nullptr;
            for (const CpuSample &s : samples)
                if (s.index == wantIdx) { curr = &s; break; }
            if (!prev || !curr) continue;

            const double util = CpuStat::utilization(*prev, *curr);
            CoreUI &ui = m_cores[slot];
            if (ui.krell) {
                ui.krell->setValue(util);
                ui.krell->setAlertLevel(util >= 0.95 ? Krell::AlertLevel::Critical
                                       : util >= 0.80 ? Krell::AlertLevel::Warning
                                                      : Krell::AlertLevel::None);
            }
            if (ui.chart) {
                ui.chart->appendSample(util);
                const int pct = static_cast<int>(util * 100.0 + 0.5);
                const QString label = slot < m_visibleCoreLabels.size()
                    ? m_visibleCoreLabels[slot] : curr->name;
                ui.chart->setOverlayText(
                    QStringLiteral("%1  %2%").arg(label).arg(pct));
            }
        }
    }

    m_prevSamples = samples;
    m_havePrev    = true;
}
