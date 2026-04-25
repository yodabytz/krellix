#include "DiskMonitor.h"

#include "widgets/Chart.h"
#include "widgets/Decal.h"
#include "widgets/Krell.h"
#include "widgets/Panel.h"

#include <QString>
#include <QVBoxLayout>
#include <QWidget>

namespace {

QString humanBps(double bps)
{
    if (bps >= 1024.0 * 1024.0)
        return QString::number(bps / (1024.0 * 1024.0), 'f', 1) + QStringLiteral("M");
    if (bps >= 1024.0)
        return QString::number(bps / 1024.0, 'f', 1) + QStringLiteral("K");
    return QString::number(static_cast<int>(bps)) + QStringLiteral("B");
}

constexpr double kMinAdaptiveBps = 1024.0;
constexpr double kAdaptiveDecay  = 0.99;
constexpr double kAdaptiveGrow   = 1.10;

} // namespace

DiskMonitor::DiskMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
}

DiskMonitor::~DiskMonitor() = default;

QWidget *DiskMonitor::createWidget(QWidget *parent)
{
    auto *container = new QWidget(parent);
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *vbox = new QVBoxLayout(container);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);

    const QList<DiskSample> samples = DiskStat::read();
    if (samples.isEmpty()) {
        auto *p = new Panel(theme(), container);
        p->setTitle(QStringLiteral("Disk"));
        Decal *d = p->addDecal(QStringLiteral("label"),
                               QStringLiteral("text_secondary"));
        d->setText(QStringLiteral("(no whole disks found)"));
        vbox->addWidget(p);
        return container;
    }

    for (const DiskSample &s : samples) {
        auto *p = new Panel(theme(), container);
        p->setTitle(s.name);

        DiskUI ui;
        ui.textDecal = p->addDecal(QStringLiteral("label"),
                                   QStringLiteral("text_primary"));
        ui.textDecal->setText(QStringLiteral("R 0  W 0"));
        ui.readKrell  = p->addKrell();
        ui.writeKrell = p->addKrell();
        ui.chart      = p->addChart(QStringLiteral("chart_line_disk"));
        if (ui.chart) ui.chart->setMaxValue(ui.maxBps);

        m_disks.insert(s.name, ui);
        m_prevSamples.insert(s.name, s);
        vbox->addWidget(p);
    }

    m_havePrev = true;
    m_lastReadTimer.start();
    return container;
}

void DiskMonitor::tick()
{
    const QList<DiskSample> samples = DiskStat::read();
    if (samples.isEmpty()) return;

    const qint64 elapsedMs = m_lastReadTimer.isValid() ? m_lastReadTimer.elapsed() : 1000;
    m_lastReadTimer.restart();
    const double dt = (elapsedMs > 0) ? (elapsedMs / 1000.0) : 1.0;

    for (const DiskSample &s : samples) {
        auto itUI = m_disks.find(s.name);
        if (itUI == m_disks.end()) continue;
        DiskUI &ui = *itUI;

        double readBps = 0.0, writeBps = 0.0;
        if (m_havePrev) {
            const auto itPrev = m_prevSamples.constFind(s.name);
            if (itPrev != m_prevSamples.constEnd()) {
                const quint64 dR = (s.sectorsRead >= itPrev->sectorsRead)
                    ? (s.sectorsRead - itPrev->sectorsRead) : 0;
                const quint64 dW = (s.sectorsWritten >= itPrev->sectorsWritten)
                    ? (s.sectorsWritten - itPrev->sectorsWritten) : 0;
                readBps  = static_cast<double>(dR * DiskStat::kSectorSize) / dt;
                writeBps = static_cast<double>(dW * DiskStat::kSectorSize) / dt;
            }
        }

        const double peakBps = qMax(readBps, writeBps);
        ui.maxBps = qMax(kMinAdaptiveBps,
                         qMax(ui.maxBps * kAdaptiveDecay, peakBps * kAdaptiveGrow));

        if (ui.readKrell)  ui.readKrell->setValue(qBound(0.0, readBps  / ui.maxBps, 1.0));
        if (ui.writeKrell) ui.writeKrell->setValue(qBound(0.0, writeBps / ui.maxBps, 1.0));
        if (ui.chart) {
            ui.chart->setMaxValue(ui.maxBps);
            ui.chart->appendSample(readBps + writeBps);
        }
        if (ui.textDecal) {
            ui.textDecal->setText(QStringLiteral("R %1  W %2")
                                  .arg(humanBps(readBps), humanBps(writeBps)));
        }

        m_prevSamples.insert(s.name, s);
    }
    m_havePrev = true;
}
