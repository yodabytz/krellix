#include "ClockMonitor.h"

#include "widgets/Decal.h"
#include "widgets/Panel.h"

#include <QDateTime>

ClockMonitor::ClockMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
}

ClockMonitor::~ClockMonitor() = default;

QWidget *ClockMonitor::createWidget(QWidget *parent)
{
    auto *p = new Panel(theme(), parent);
    m_timeDecal = p->addDecal(QStringLiteral("value"),
                              QStringLiteral("text_primary"));
    m_dateDecal = p->addDecal(QStringLiteral("label"),
                              QStringLiteral("text_secondary"));
    tick();
    return p;
}

void ClockMonitor::tick()
{
    if (!m_timeDecal || !m_dateDecal) return;
    const QDateTime now = QDateTime::currentDateTime();
    m_timeDecal->setText(now.toString(QStringLiteral("HH:mm:ss")));
    m_dateDecal->setText(now.toString(QStringLiteral("ddd MMM d yyyy")));
}
