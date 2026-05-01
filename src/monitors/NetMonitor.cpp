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
    p->setSurfaceKey(QStringLiteral("panel_bg_net"));
    p->setTitle(alias.isEmpty() ? name : alias);
    NetMonitor::IfaceUI ui;
    ui.rxKrell = p->addKrell();
    ui.txKrell = p->addKrell();
    ui.chart   = p->addChart(QStringLiteral("chart_line_net_rx"));
    if (ui.chart) {
        ui.chart->setMaxValue(ui.maxBps);
        ui.chart->setOverlayText(QStringLiteral("RX 0  TX 0"));
    }
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
        // Default-enable real interfaces (eth*, en*, wlan*, ...). For
        // Docker bridges only docker0 is default-enabled — its counters
        // have been replaced server-side with the sum of all Docker
        // networks, so it acts as a single combined "Docker activity"
        // panel. Individual user-defined bridges (br-<hash>) are still
        // available with friendly aliases, but stay default-disabled
        // so the user gets a clean single panel by default; they can
        // toggle individuals on for a per-network breakdown.
        const bool defaultEnabled =
            (s.name == QLatin1String("docker0"))
            || NetStat::isMainInterface(s.name);
        const bool enabled = settings.value(
            QStringLiteral("monitors/net/") + s.name, defaultEnabled).toBool();
        if (!enabled) continue;
        IfaceUI ui = buildIfacePanel(theme(), container, vbox,
                                     s.name, s.alias);
        if (s.name == QLatin1String("docker0")) {
            setupDockerMultiSeries(ui, samples);
        }
        m_ifaces.insert(s.name, ui);
        m_prevSamples.insert(s.name, s);
    }

    if (m_ifaces.isEmpty()) {
        auto *p = new Panel(theme(), container);
        p->setSurfaceKey(QStringLiteral("panel_bg_net"));
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
            const bool defaultEnabled =
                (s.name == QLatin1String("docker0"))
                || NetStat::isMainInterface(s.name);
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
            IfaceUI ui = buildIfacePanel(theme(), m_container,
                                         m_containerLayout,
                                         s.name, s.alias);
            if (s.name == QLatin1String("docker0")) {
                setupDockerMultiSeries(ui, samples);
            }
            m_ifaces.insert(s.name, ui);
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

        // For docker0 (combined Docker view), feed the chart per-bridge
        // throughput in different colors so the user can see which
        // network is doing the work. Krells + decal still show the
        // combined RX/TX. For everything else, single-series chart of
        // RX+TX as before.
        double peakBps = qMax(rxBps, txBps);
        if (ui.dockerMultiSeries && ui.chart) {
            for (int i = 0; i < ui.dockerBridges.size(); ++i) {
                const QString &bn = ui.dockerBridges[i];
                const NetSample *curr = nullptr;
                for (const NetSample &b : samples)
                    if (b.name == bn) { curr = &b; break; }
                const auto prevIt = ui.dockerPrev.constFind(bn);
                if (!curr || prevIt == ui.dockerPrev.constEnd()) continue;
                const quint64 dRx = (curr->rxBytes >= prevIt->rxBytes)
                                  ? curr->rxBytes - prevIt->rxBytes : 0;
                const quint64 dTx = (curr->txBytes >= prevIt->txBytes)
                                  ? curr->txBytes - prevIt->txBytes : 0;
                const double bps = static_cast<double>(dRx + dTx) / dt;
                ui.chart->appendSampleAt(i, bps);
                if (bps > peakBps) peakBps = bps;
                ui.dockerPrev.insert(bn, *curr);
            }
        }

        ui.maxBps = qMax(kMinAdaptiveBps,
                         qMax(ui.maxBps * kAdaptiveDecay,
                              peakBps * kAdaptiveGrow));

        if (ui.rxKrell) ui.rxKrell->setValue(qBound(0.0, rxBps / ui.maxBps, 1.0));
        if (ui.txKrell) ui.txKrell->setValue(qBound(0.0, txBps / ui.maxBps, 1.0));
        if (ui.chart) {
            ui.chart->setMaxValue(ui.maxBps);
            if (!ui.dockerMultiSeries)
                ui.chart->appendSample(rxBps + txBps);
        }
        if (ui.chart) {
            ui.chart->setOverlayText(QStringLiteral("RX %1  TX %2")
                                     .arg(humanBps(rxBps), humanBps(txBps)));
        }

        m_prevSamples.insert(s.name, s);
    }
    m_havePrev = true;
}

void NetMonitor::setupDockerMultiSeries(IfaceUI &ui,
                                        const QList<NetSample> &samples)
{
    // Pull every bridge that the daemon has tagged with a friendly
    // alias (i.e. every network Docker knows about) — except docker0
    // itself, which is the synthetic combined view. Each surviving
    // bridge gets its own colored series in docker0's chart.
    for (const NetSample &b : samples) {
        if (b.alias.isEmpty()) continue;
        if (b.name == QLatin1String("docker0")) continue;
        ui.dockerBridges.append(b.name);
        ui.dockerPrev.insert(b.name, b);
    }
    if (ui.dockerBridges.isEmpty()) return;
    ui.dockerMultiSeries = true;
    if (ui.chart) ui.chart->setRainbowSeries(ui.dockerBridges.size());
}
