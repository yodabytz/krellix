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

    if (m_aggregateMode) {
        // Single panel showing the kernel's "cpu" aggregate (samples[0]).
        // Useful when the machine has many cores and per-core panels would
        // make the window taller than the screen.
        auto *p = new Panel(theme(), container);
        p->setTitle(QStringLiteral("CPU (all cores)"));

        CoreUI ui;
        ui.valueDecal = p->addDecal(QStringLiteral("label"),
                                    QStringLiteral("text_primary"));
        ui.valueDecal->setAlignment(Qt::AlignHCenter);
        ui.valueDecal->setText(QStringLiteral("0%"));
        ui.krell = p->addKrell();
        ui.chart = p->addChart(QStringLiteral("chart_line_cpu"));
        if (ui.chart) ui.chart->setMaxValue(1.0);

        m_aggregateUI = ui;
        vbox->addWidget(p);
    } else {
        // Per-core mode: one panel per kernel CPU index, but each one is
        // gated by monitors/cpu/<index> (default true) so the user can
        // disable individual cores on a many-CPU machine via settings.
        for (int i = 1; i < samples.size(); ++i) {
            const CpuSample &smp = samples[i];
            const QString key = QStringLiteral("monitors/cpu/") +
                                QString::number(smp.index);
            const bool enabled = s.value(key, true).toBool();
            if (!enabled) continue;

            auto *p = new Panel(theme(), container);
            p->setTitle(smp.name);

            CoreUI ui;
            ui.valueDecal = p->addDecal(QStringLiteral("label"),
                                        QStringLiteral("text_primary"));
            ui.valueDecal->setAlignment(Qt::AlignHCenter);
            ui.valueDecal->setText(QStringLiteral("0%"));
            ui.krell = p->addKrell();
            ui.chart = p->addChart(QStringLiteral("chart_line_cpu"));
            if (ui.chart) ui.chart->setMaxValue(1.0);

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
            if (m_aggregateUI.chart) m_aggregateUI.chart->appendSample(util);
            if (m_aggregateUI.valueDecal) {
                const int pct = static_cast<int>(util * 100.0 + 0.5);
                m_aggregateUI.valueDecal->setText(QString::number(pct)
                                                  + QStringLiteral("%"));
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
