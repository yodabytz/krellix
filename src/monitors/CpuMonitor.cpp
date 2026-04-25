#include "CpuMonitor.h"

#include "widgets/Chart.h"
#include "widgets/Decal.h"
#include "widgets/Krell.h"
#include "widgets/Panel.h"

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

    // No /proc/stat or zero samples — render a single placeholder panel.
    if (samples.size() < 2) {
        auto *p = new Panel(theme(), container);
        p->setTitle(QStringLiteral("CPU"));
        Decal *d = p->addDecal(QStringLiteral("label"),
                               QStringLiteral("text_secondary"));
        d->setText(samples.isEmpty()
                   ? QStringLiteral("(no /proc/stat)")
                   : QStringLiteral("(no per-core data)"));
        vbox->addWidget(p);
        return container;
    }

    // samples[0] is the aggregate "cpu"; skip it, render per-core panels.
    for (int i = 1; i < samples.size(); ++i) {
        auto *p = new Panel(theme(), container);
        p->setTitle(samples[i].name);

        CoreUI ui;
        ui.valueDecal = p->addDecal(QStringLiteral("value"),
                                    QStringLiteral("text_primary"));
        ui.valueDecal->setText(QStringLiteral("0%"));
        ui.krell = p->addKrell();
        ui.chart = p->addChart(QStringLiteral("chart_line_cpu"));
        if (ui.chart) ui.chart->setMaxValue(1.0);

        m_cores.append(ui);
        vbox->addWidget(p);
    }

    m_prevSamples = samples;
    m_havePrev    = true;
    return container;
}

void CpuMonitor::tick()
{
    const QList<CpuSample> samples = CpuStat::read();
    if (samples.size() < 2) return;

    // First tick after construction we may already have m_havePrev set by
    // createWidget; the first delta computation produces a real number.
    if (m_havePrev && samples.size() == m_prevSamples.size()) {
        const int n = qMin(samples.size() - 1, m_cores.size());
        for (int i = 0; i < n; ++i) {
            const CpuSample &prev = m_prevSamples[i + 1];
            const CpuSample &curr = samples[i + 1];
            if (prev.index != curr.index) continue;  // hotplug churn — skip

            const double util = CpuStat::utilization(prev, curr);
            CoreUI &ui = m_cores[i];

            if (ui.krell) ui.krell->setValue(util);
            if (ui.chart) ui.chart->appendSample(util);
            if (ui.valueDecal) {
                const int pct = static_cast<int>(util * 100.0 + 0.5);
                ui.valueDecal->setText(QString::number(pct)
                                       + QStringLiteral("%"));
            }
        }
    }

    m_prevSamples = samples;
    m_havePrev    = true;
}
