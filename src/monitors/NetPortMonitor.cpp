#include "NetPortMonitor.h"

#include "sysdep/NetPortStat.h"
#include "theme/Theme.h"
#include "widgets/Decal.h"
#include "widgets/Panel.h"

#include <QRegularExpression>
#include <QSettings>
#include <QVBoxLayout>

namespace {

constexpr int kMaxWatches = 8;
constexpr int kDisplayLabelChars = 5;

QList<QPair<int, int>> parsePorts(QString spec)
{
    QList<QPair<int, int>> out;
    spec = spec.trimmed().toLower();
    if (spec.startsWith(QStringLiteral("tcp ")))
        spec = spec.mid(4).trimmed();
    else if (spec.startsWith(QStringLiteral("udp ")))
        spec = spec.mid(4).trimmed();

    const QStringList parts = spec.split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                         Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QStringList range = part.split(QLatin1Char('-'), Qt::SkipEmptyParts);
        bool okA = false;
        bool okB = false;
        const int a = range.value(0).toInt(&okA);
        const int b = range.size() > 1 ? range.value(1).toInt(&okB) : a;
        if (!okA || (range.size() > 1 && !okB)) continue;
        const int lo = qBound(1, qMin(a, b), 65535);
        const int hi = qBound(1, qMax(a, b), 65535);
        out.append(qMakePair(lo, hi));
    }
    return out;
}

bool portInRanges(int port, const QList<QPair<int, int>> &ranges)
{
    for (const auto &range : ranges) {
        if (port >= range.first && port <= range.second)
            return true;
    }
    return false;
}

bool protocolMatches(const QString &watchProtocol, const QString &sampleProtocol)
{
    return watchProtocol == QLatin1String("all") || watchProtocol == sampleProtocol;
}

QString formatWatchRow(const QString &label, int count)
{
    QString shortLabel = label.trimmed().left(kDisplayLabelChars);
    shortLabel = shortLabel.leftJustified(kDisplayLabelChars, QLatin1Char(' '), true);
    return QStringLiteral("%1 %2").arg(shortLabel).arg(count);
}

} // namespace

NetPortMonitor::NetPortMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
}

NetPortMonitor::~NetPortMonitor() = default;

bool NetPortMonitor::sameWatches(const QList<Watch> &a, const QList<Watch> &b)
{
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i) {
        if (a.at(i).label != b.at(i).label
            || a.at(i).protocol != b.at(i).protocol
            || a.at(i).ports != b.at(i).ports)
            return false;
    }
    return true;
}

QWidget *NetPortMonitor::createWidget(QWidget *parent)
{
    auto *panel = new Panel(theme(), parent);
    panel->setSurfaceKey(QStringLiteral("panel_bg_netports"));
    panel->setTitle(QStringLiteral("Net Ports"));

    m_rowsWidget = new QWidget(panel);
    m_rowsLayout = new QVBoxLayout(m_rowsWidget);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(theme()->metric(QStringLiteral("panel_spacing"), 2));
    panel->addWidget(m_rowsWidget);
    m_panel = panel;

    tick();
    return panel;
}

QList<NetPortMonitor::Watch> NetPortMonitor::configuredWatches() const
{
    QSettings s;
    QList<Watch> out;
    for (int i = 1; i <= kMaxWatches; ++i) {
        const bool defEnabled = i == 1;
        if (!s.value(QStringLiteral("monitors/netports/watch%1/enabled").arg(i),
                     defEnabled).toBool())
            continue;

        const QString defaultLabel = i == 1 ? QStringLiteral("SSH") : QString();
        const QString defaultPorts = i == 1 ? QStringLiteral("22") : QString();
        const QString label = s.value(QStringLiteral("monitors/netports/watch%1/label").arg(i),
                                      defaultLabel).toString().trimmed();
        const QString ports = s.value(QStringLiteral("monitors/netports/watch%1/ports").arg(i),
                                      defaultPorts).toString().trimmed();
        QString protocol = s.value(QStringLiteral("monitors/netports/watch%1/protocol").arg(i),
                                   QStringLiteral("tcp")).toString().trimmed().toLower();
        if (protocol != QLatin1String("tcp")
            && protocol != QLatin1String("udp")
            && protocol != QLatin1String("all"))
            protocol = QStringLiteral("tcp");

        if (ports.isEmpty() || parsePorts(ports).isEmpty())
            continue;
        out.append(Watch{label.isEmpty() ? ports : label, protocol, ports});
    }
    return out;
}

void NetPortMonitor::rebuildRows(const QList<Watch> &watches)
{
    if (!m_rowsLayout) return;

    for (const QPointer<Decal> &row : m_rows) {
        if (!row) continue;
        m_rowsLayout->removeWidget(row);
        row->deleteLater();
    }
    m_rows.clear();

    if (watches.isEmpty()) {
        auto *d = new Decal(theme(), QStringLiteral("label"),
                            QStringLiteral("text_secondary"), m_rowsWidget);
        d->setAlignment(Qt::AlignHCenter);
        d->setText(QStringLiteral("add port watches in Settings"));
        m_rowsLayout->addWidget(d);
        m_rows.append(d);
        return;
    }

    for (const Watch &watch : watches) {
        auto *d = new Decal(theme(), QStringLiteral("label"),
                            QStringLiteral("text_primary"), m_rowsWidget);
        d->setAlignment(Qt::AlignLeft);
        d->setToolTip(watch.label);
        m_rowsLayout->addWidget(d);
        m_rows.append(d);
    }
}

int NetPortMonitor::countMatches(const Watch &watch,
                                 const QList<NetPortSample> &samples) const
{
    const QList<QPair<int, int>> ranges = parsePorts(watch.ports);
    if (ranges.isEmpty()) return 0;

    int count = 0;
    for (const NetPortSample &sample : samples) {
        if (!protocolMatches(watch.protocol, sample.protocol))
            continue;
        if (!portInRanges(sample.localPort, ranges))
            continue;
        if (sample.protocol == QLatin1String("tcp")
            && sample.state != QLatin1String("01"))
            continue;
        ++count;
    }
    return count;
}

void NetPortMonitor::tick()
{
    if (!m_rowsLayout) return;

    const QList<Watch> watches = configuredWatches();
    if (!sameWatches(m_watches, watches)) {
        m_watches = watches;
        rebuildRows(m_watches);
    }

    if (m_watches.isEmpty()) return;

    const QList<NetPortSample> samples = NetPortStat::read();
    for (int i = 0; i < m_watches.size() && i < m_rows.size(); ++i) {
        if (!m_rows.at(i)) continue;
        const Watch &watch = m_watches.at(i);
        const int count = countMatches(watch, samples);
        m_rows.at(i)->setToolTip(watch.label);
        m_rows.at(i)->setText(formatWatchRow(watch.label, count));
    }
}
