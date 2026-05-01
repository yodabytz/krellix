#include "UptimeMonitor.h"

#include "sysdep/UptimeStat.h"
#include "widgets/Decal.h"
#include "widgets/Panel.h"

UptimeMonitor::UptimeMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
}

UptimeMonitor::~UptimeMonitor() = default;

QWidget *UptimeMonitor::createWidget(QWidget *parent)
{
    auto *p = new Panel(theme(), parent);
    p->setSurfaceKey(QStringLiteral("panel_bg_uptime"));
    p->setTitle(QStringLiteral("Uptime"));
    m_decal = p->addDecal(QStringLiteral("label"),
                          QStringLiteral("text_primary"));
    if (m_decal) m_decal->setAlignment(Qt::AlignHCenter);
    tick();
    return p;
}

void UptimeMonitor::tick()
{
    if (!m_decal) return;
    const qint64 secs = UptimeStat::secondsSinceBoot();
    if (secs < 0) {
        m_decal->setText(QStringLiteral("?"));
        return;
    }
    const qint64 days   = secs / 86400;
    const qint64 hours  = (secs % 86400) / 3600;
    const qint64 mins   = (secs % 3600)  / 60;

    QString out;
    if (days > 0) {
        out = QStringLiteral("%1d %2:%3")
                  .arg(days)
                  .arg(hours, 2, 10, QLatin1Char('0'))
                  .arg(mins,  2, 10, QLatin1Char('0'));
    } else {
        out = QStringLiteral("%1:%2")
                  .arg(hours, 2, 10, QLatin1Char('0'))
                  .arg(mins,  2, 10, QLatin1Char('0'));
    }
    m_decal->setText(out);
}
