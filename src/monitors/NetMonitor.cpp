#include "NetMonitor.h"

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

constexpr double kMinAdaptiveBps = 1024.0;          // 1 KB/s floor
constexpr double kAdaptiveDecay  = 0.99;            // shrink 1% per tick when idle
constexpr double kAdaptiveGrow   = 1.10;            // 10% headroom over peak

} // namespace

NetMonitor::NetMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
}

NetMonitor::~NetMonitor() = default;

QWidget *NetMonitor::createWidget(QWidget *parent)
{
    auto *container = new QWidget(parent);
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *vbox = new QVBoxLayout(container);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);

    const QList<NetSample> samples = NetStat::read();
    if (samples.isEmpty()) {
        auto *p = new Panel(theme(), container);
        p->setTitle(QStringLiteral("Net"));
        Decal *d = p->addDecal(QStringLiteral("label"),
                               QStringLiteral("text_secondary"));
        d->setText(QStringLiteral("(no /proc/net/dev)"));
        vbox->addWidget(p);
        return container;
    }

    for (const NetSample &s : samples) {
        auto *p = new Panel(theme(), container);
        p->setTitle(s.name);

        IfaceUI ui;
        ui.textDecal = p->addDecal(QStringLiteral("label"),
                                   QStringLiteral("text_primary"));
        ui.textDecal->setText(QStringLiteral("RX 0  TX 0"));
        ui.rxKrell = p->addKrell();
        ui.txKrell = p->addKrell();
        ui.chart   = p->addChart(QStringLiteral("chart_line_net_rx"));
        if (ui.chart) ui.chart->setMaxValue(ui.maxBps);

        m_ifaces.insert(s.name, ui);
        m_prevSamples.insert(s.name, s);
        vbox->addWidget(p);
    }

    m_havePrev = true;
    m_lastReadTimer.start();
    return container;
}

void NetMonitor::tick()
{
    const QList<NetSample> samples = NetStat::read();
    if (samples.isEmpty()) return;

    const qint64 elapsedMs = m_lastReadTimer.isValid() ? m_lastReadTimer.elapsed() : 1000;
    m_lastReadTimer.restart();
    const double dt = (elapsedMs > 0) ? (elapsedMs / 1000.0) : 1.0;

    for (const NetSample &s : samples) {
        auto itUI = m_ifaces.find(s.name);
        if (itUI == m_ifaces.end()) continue;     // new interface mid-run; skip
        IfaceUI &ui = *itUI;

        double rxBps = 0.0, txBps = 0.0;
        if (m_havePrev) {
            const auto itPrev = m_prevSamples.constFind(s.name);
            if (itPrev != m_prevSamples.constEnd()) {
                const quint64 dRx = (s.rxBytes >= itPrev->rxBytes)
                    ? (s.rxBytes - itPrev->rxBytes) : 0;
                const quint64 dTx = (s.txBytes >= itPrev->txBytes)
                    ? (s.txBytes - itPrev->txBytes) : 0;
                rxBps = static_cast<double>(dRx) / dt;
                txBps = static_cast<double>(dTx) / dt;
            }
        }

        const double peakBps = qMax(rxBps, txBps);
        ui.maxBps = qMax(kMinAdaptiveBps,
                         qMax(ui.maxBps * kAdaptiveDecay, peakBps * kAdaptiveGrow));

        if (ui.rxKrell) ui.rxKrell->setValue(qBound(0.0, rxBps / ui.maxBps, 1.0));
        if (ui.txKrell) ui.txKrell->setValue(qBound(0.0, txBps / ui.maxBps, 1.0));
        if (ui.chart) {
            ui.chart->setMaxValue(ui.maxBps);
            ui.chart->appendSample(rxBps + txBps);
        }
        if (ui.textDecal) {
            ui.textDecal->setText(QStringLiteral("RX %1  TX %2")
                                  .arg(humanBps(rxBps), humanBps(txBps)));
        }

        m_prevSamples.insert(s.name, s);
    }
    m_havePrev = true;
}
