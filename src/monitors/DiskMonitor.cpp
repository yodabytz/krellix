#include "DiskMonitor.h"

#include "widgets/Chart.h"
#include "widgets/Decal.h"
#include "widgets/Krell.h"
#include "widgets/Panel.h"

#include <QSettings>
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

bool diskEnabled(const QSettings &settings, const QString &name)
{
    return settings.value(QStringLiteral("monitors/disk/devices/") + name,
                          !name.startsWith(QStringLiteral("loop"))).toBool();
}

double sectorDelta(quint64 previous, quint64 current)
{
    return current >= previous ? static_cast<double>(current - previous) : 0.0;
}

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

    m_container = container;
    m_lastReadTimer.invalidate();
    tick();
    return container;
}

void DiskMonitor::rebuildDisks(const QList<DiskSample> &enabled)
{
    auto *container = m_container.data();
    if (!container) return;
    auto *vbox = static_cast<QVBoxLayout *>(container->layout());
    while (auto *item = vbox->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_disks.clear();
    m_compositeUI = {};
    m_prevSamples.clear();

    const auto addDisk = [&](const QString &title) {
        auto *p = new Panel(theme(), container);
        p->setSurfaceKey(QStringLiteral("panel_bg_disk"));
        p->setTitle(title);

        DiskUI ui;
        ui.chart      = p->addChart(QStringLiteral("chart_line_disk"));
        ui.chart->setMaxValue(ui.maxBps);
        ui.chart->setOverlayText(QStringLiteral("R 0  W 0"));
        ui.readKrell  = p->addKrell();
        ui.writeKrell = p->addKrell();

        vbox->addWidget(p);
        return ui;
    };

    if (enabled.isEmpty()) {
        auto *p = new Panel(theme(), container);
        p->setSurfaceKey(QStringLiteral("panel_bg_disk"));
        p->setTitle(QStringLiteral("Disk"));
        Decal *d = p->addDecal(QStringLiteral("label"),
                               QStringLiteral("text_secondary"));
        d->setText(m_hasDisks ? QStringLiteral("(no disks enabled)")
                             : QStringLiteral("(no whole disks found)"));
        vbox->addWidget(p);
    } else if (m_composite) {
        m_compositeUI = addDisk(QStringLiteral("All disks"));
    } else {
        for (const DiskSample &s : enabled)
            m_disks.insert(s.name, addDisk(s.name));
    }

    for (const DiskSample &s : enabled)
        m_prevSamples.insert(s.name, s);
    m_lastReadTimer.start();
    container->updateGeometry();
}

void DiskMonitor::tick()
{
    if (!m_container) return;
    const QList<DiskSample> samples = DiskStat::read();
    const QSettings settings;
    const bool composite = settings.value(QStringLiteral("monitors/disk/mode")).toString()
        == QStringLiteral("composite");
    const bool hasDisks = !samples.isEmpty();
    QList<DiskSample> enabled;
    QStringList visible;
    for (const auto &sample : samples) {
        if (!diskEnabled(settings, sample.name)) continue;
        enabled.append(sample);
        visible.append(sample.name);
    }
    if (!m_lastReadTimer.isValid() || composite != m_composite
        || hasDisks != m_hasDisks || visible != m_visibleDisks) {
        m_composite = composite;
        m_hasDisks = hasDisks;
        m_visibleDisks = visible;
        rebuildDisks(enabled);
        return;
    }
    if (enabled.isEmpty()) return;

    const qint64 elapsedMs = m_lastReadTimer.isValid() ? m_lastReadTimer.elapsed() : 1000;
    m_lastReadTimer.restart();
    const double dt = (elapsedMs > 0) ? (elapsedMs / 1000.0) : 1.0;

    const auto updatePanel = [composite](DiskUI &ui, double readBps, double writeBps) {
        const double peakBps = composite ? readBps + writeBps : qMax(readBps, writeBps);
        ui.maxBps = qMax(kMinAdaptiveBps,
                         qMax(ui.maxBps * kAdaptiveDecay, peakBps * kAdaptiveGrow));

        if (ui.readKrell)  ui.readKrell->setValue(qBound(0.0, readBps  / ui.maxBps, 1.0));
        if (ui.writeKrell) ui.writeKrell->setValue(qBound(0.0, writeBps / ui.maxBps, 1.0));
        if (ui.chart) {
            ui.chart->setMaxValue(ui.maxBps);
            ui.chart->appendSample(readBps + writeBps);
            ui.chart->setOverlayText(QStringLiteral("R %1  W %2")
                                  .arg(humanBps(readBps), humanBps(writeBps)));
        }

    };

    double totalRead = 0.0, totalWrite = 0.0;
    for (const DiskSample &s : enabled) {
        const auto previous = m_prevSamples.value(s.name, s);
        const double readBps = sectorDelta(previous.sectorsRead, s.sectorsRead) * DiskStat::kSectorSize / dt;
        const double writeBps = sectorDelta(previous.sectorsWritten, s.sectorsWritten) * DiskStat::kSectorSize / dt;
        if (composite) {
            // Delta each drive before adding; a reset on one drive must not wipe out the others.
            totalRead += readBps;
            totalWrite += writeBps;
        } else {
            auto panel = m_disks.find(s.name);
            if (panel != m_disks.end()) updatePanel(*panel, readBps, writeBps);
        }
        m_prevSamples.insert(s.name, s);
    }
    if (composite) updatePanel(m_compositeUI, totalRead, totalWrite);
}
