#include "NetMonitor.h"

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

constexpr double kMinAdaptiveBps = 1024.0;          // 1 KB/s floor
constexpr double kAdaptiveDecay  = 0.99;            // shrink 1% per tick when idle
constexpr double kAdaptiveGrow   = 1.10;            // 10% headroom over peak

} // namespace

NetMonitor::NetMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
}

NetMonitor::~NetMonitor() = default;

// Build a panel for one interface and append it to the container's vbox.
// Pulled out so tick() can lazy-add interfaces that show up after
// createWidget — important in --host mode where the first remote sample
// may arrive after the monitor has already been constructed.
//
// `alias` is the human-friendly label (e.g. Docker network name) when
// known; the panel title prefers it over the raw interface name so a
// user looking at "internal" doesn't have to remember that means
// br-f6739fab5f74.
static NetMonitor::IfaceUI buildIfacePanel(Theme *theme, QWidget *parent,
                                           QVBoxLayout *into,
                                           const QString &name,
                                           const QString &alias)
{
    auto *p = new Panel(theme, parent);
    p->setTitle(alias.isEmpty() ? name : alias);
    NetMonitor::IfaceUI ui;
    ui.textDecal = p->addDecal(QStringLiteral("label"),
                               QStringLiteral("text_primary"));
    ui.textDecal->setAlignment(Qt::AlignHCenter);
    ui.textDecal->setText(QStringLiteral("RX 0  TX 0"));
    ui.rxKrell = p->addKrell();
    ui.txKrell = p->addKrell();
    ui.chart   = p->addChart(QStringLiteral("chart_line_net_rx"));
    if (ui.chart) ui.chart->setMaxValue(ui.maxBps);
    into->addWidget(p);
    return ui;
}

QWidget *NetMonitor::createWidget(QWidget *parent)
{
    auto *container = new QWidget(parent);
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *vbox = new QVBoxLayout(container);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);
    m_container = container;
    m_containerLayout = vbox;

    QSettings settings;
    const QList<NetSample> samples = NetStat::read();

    for (const NetSample &s : samples) {
        const bool defaultEnabled = NetStat::isMainInterface(s.name);
        const bool enabled = settings.value(
            QStringLiteral("monitors/net/") + s.name, defaultEnabled).toBool();
        if (!enabled) continue;
        m_ifaces.insert(s.name,
                        buildIfacePanel(theme(), container, vbox,
                                        s.name, s.alias));
        m_prevSamples.insert(s.name, s);
    }

    if (m_ifaces.isEmpty()) {
        auto *p = new Panel(theme(), container);
        p->setTitle(QStringLiteral("Net"));
        Decal *d = p->addDecal(QStringLiteral("label"),
                               QStringLiteral("text_secondary"));
        d->setAlignment(Qt::AlignHCenter);
        d->setText(samples.isEmpty()
                   ? QStringLiteral("(waiting for data...)")
                   : QStringLiteral("(no interfaces selected)"));
        vbox->addWidget(p);
        // Track only the "waiting" placeholder — that one is supposed to
        // disappear when real interfaces appear (typical in --host mode).
        // The "(no interfaces selected)" placeholder is a user state and
        // should stay until they enable an interface.
        if (samples.isEmpty()) m_placeholderPanel = p;
    }

    m_havePrev = !samples.isEmpty();
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

    QSettings settings;
    for (const NetSample &s : samples) {
        auto itUI = m_ifaces.find(s.name);
        if (itUI == m_ifaces.end()) {
            // Interface arrived after createWidget — typical when in
            // --host mode and the first remote sample lands a tick after
            // the monitor was constructed, OR when a fresh setting just
            // enabled this iface. Build its panel now so the user sees
            // their toggle take effect without another rebuild.
            const bool defaultEnabled = NetStat::isMainInterface(s.name);
            const bool enabled = settings.value(
                QStringLiteral("monitors/net/") + s.name, defaultEnabled).toBool();
            if (!enabled || !m_container || !m_containerLayout) continue;
            // Drop the "(waiting for data...)" placeholder the first time
            // we add a real interface panel — otherwise it just sits
            // above the live data forever.
            if (m_placeholderPanel) {
                m_containerLayout->removeWidget(m_placeholderPanel);
                m_placeholderPanel->deleteLater();
                m_placeholderPanel = nullptr;
            }
            m_ifaces.insert(s.name,
                            buildIfacePanel(theme(), m_container,
                                            m_containerLayout,
                                            s.name, s.alias));
            m_prevSamples.insert(s.name, s);
            continue;   // first appearance: defer rate computation to next tick
        }
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
