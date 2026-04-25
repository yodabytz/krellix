#include "CpuMonitor.h"

#include "widgets/Chart.h"
#include "widgets/Decal.h"
#include "widgets/Krell.h"
#include "widgets/Panel.h"

#include <QSettings>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

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

    const QList<CpuSample> samples = CpuStat::read();

    if (samples.isEmpty()) {
        auto *p = new Panel(theme(), container);
        p->setTitle(QStringLiteral("CPU"));
        Decal *d = p->addDecal(QStringLiteral("label"),
                               QStringLiteral("text_secondary"));
        d->setAlignment(Qt::AlignHCenter);
        d->setText(QStringLiteral("(no /proc/stat)"));
        vbox->addWidget(p);
        return container;
    }

    QSettings s;
    const QString mode = s.value(QStringLiteral("monitors/cpu/mode"),
                                 QStringLiteral("per-core")).toString();
    m_aggregateMode = (mode == QStringLiteral("aggregate"));

    // Per-CPU panel layout is intentionally compact: just a krell row and
    // a chart, with the core name + current % drawn as an overlay inside
    // the chart (top-left). Saves a whole decal row per core, so a many-
    // core machine fits in a much shorter window.
    if (m_aggregateMode) {
        auto *p = new Panel(theme(), container);
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
        for (int i = 1; i < samples.size(); ++i) {
            const CpuSample &smp = samples[i];
            const QString key = QStringLiteral("monitors/cpu/") +
                                QString::number(smp.index);
            const bool enabled = s.value(key, true).toBool();
            if (!enabled) continue;

            auto *p = new Panel(theme(), container);
            CoreUI ui;
            ui.krell = p->addKrell();
            ui.chart = p->addChart(QStringLiteral("chart_line_cpu"));
            if (ui.chart) {
                ui.chart->setMaxValue(1.0);
                ui.chart->setOverlayText(smp.name + QStringLiteral("  0%"));
            }
            m_cores.append(ui);
            m_visibleCoreIndices.append(smp.index);
            vbox->addWidget(p);
        }
        if (m_cores.isEmpty()) {
            auto *p = new Panel(theme(), container);
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
    return container;
}

void CpuMonitor::tick()
{
    const QList<CpuSample> samples = CpuStat::read();
    if (samples.isEmpty()) return;

    if (m_aggregateMode) {
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
            if (ui.krell) ui.krell->setValue(util);
            if (ui.chart) {
                ui.chart->appendSample(util);
                const int pct = static_cast<int>(util * 100.0 + 0.5);
                ui.chart->setOverlayText(
                    QStringLiteral("%1  %2%").arg(curr->name).arg(pct));
            }
        }
    }

    m_prevSamples = samples;
    m_havePrev    = true;
}
