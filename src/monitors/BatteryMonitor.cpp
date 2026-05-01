#include "BatteryMonitor.h"

#include "sysdep/BatteryStat.h"
#include "widgets/Decal.h"
#include "widgets/Krell.h"
#include "widgets/Panel.h"

#include <QString>

BatteryMonitor::BatteryMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
}

BatteryMonitor::~BatteryMonitor() = default;

QWidget *BatteryMonitor::createWidget(QWidget *parent)
{
    auto *p = new Panel(theme(), parent);
    p->setSurfaceKey(QStringLiteral("panel_bg_battery"));
    p->setTitle(QStringLiteral("Battery"));

    m_textDecal = p->addDecal(QStringLiteral("label"),
                              QStringLiteral("text_primary"));
    m_textDecal->setAlignment(Qt::AlignHCenter);
    m_krell = p->addKrell();
    m_etaDecal = p->addDecal(QStringLiteral("label"),
                             QStringLiteral("text_secondary"));
    m_etaDecal->setAlignment(Qt::AlignHCenter);

    tick();
    return p;
}

void BatteryMonitor::tick()
{
    const BatteryInfo b = BatteryStat::read();
    if (!m_textDecal || !m_krell) return;

    if (!b.present) {
        m_textDecal->setText(QStringLiteral("(no battery)"));
        if (m_etaDecal) m_etaDecal->setText(QString());
        m_krell->setValue(0.0);
        return;
    }

    // Status flag: discharging vs charging vs full, plus AC state.
    QString sym;
    if (b.status == QStringLiteral("Charging"))         sym = QStringLiteral("[CHG]");
    else if (b.status == QStringLiteral("Discharging")) sym = QStringLiteral("[BAT]");
    else if (b.status == QStringLiteral("Full"))        sym = QStringLiteral("[FULL]");
    else if (b.acOnline)                                sym = QStringLiteral("[AC]");
    else                                                sym = QString();

    m_textDecal->setText(QStringLiteral("%1 %2%%").arg(sym).arg(b.percent));
    m_krell->setValue(qBound(0.0, b.percent / 100.0, 1.0));
    // Low-battery alert only when actually discharging — full but unplugged
    // doesn't need a red flash.
    if (b.status == QStringLiteral("Discharging")) {
        m_krell->setAlertLevel(b.percent <= 10 ? Krell::AlertLevel::Critical
                               : b.percent <= 20 ? Krell::AlertLevel::Warning
                                                 : Krell::AlertLevel::None);
    } else {
        m_krell->setAlertLevel(Krell::AlertLevel::None);
    }

    if (m_etaDecal) {
        if (b.timeRemainSec > 0) {
            const qint64 hours = b.timeRemainSec / 3600;
            const qint64 mins  = (b.timeRemainSec % 3600) / 60;
            m_etaDecal->setText(QStringLiteral("%1h %2m left")
                                .arg(hours)
                                .arg(mins, 2, 10, QLatin1Char('0')));
        } else {
            m_etaDecal->setText(QString());
        }
    }
}
