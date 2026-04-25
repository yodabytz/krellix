#include "ClockMonitor.h"

#include "widgets/Decal.h"
#include "widgets/Panel.h"

#include <QDateTime>
#include <QSettings>

ClockMonitor::ClockMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
}

ClockMonitor::~ClockMonitor() = default;

QWidget *ClockMonitor::createWidget(QWidget *parent)
{
    auto *p = new Panel(theme(), parent);
    // Larger but not bold — the "time" font key is tuned for the clock
    // display (defaults to Monospace 12 / not bold; themes may override).
    m_timeDecal = p->addDecal(QStringLiteral("time"),
                              QStringLiteral("text_primary"));
    m_dateDecal = p->addDecal(QStringLiteral("label"),
                              QStringLiteral("text_secondary"));
    tick();
    return p;
}

void ClockMonitor::tick()
{
    if (!m_timeDecal || !m_dateDecal) return;

    const bool military =
        QSettings().value(QStringLiteral("clock/military"), true).toBool();

    const QDateTime now = QDateTime::currentDateTime();
    const QString timeFmt = military
        ? QStringLiteral("HH:mm:ss")
        : QStringLiteral("h:mm:ss AP");
    m_timeDecal->setText(now.toString(timeFmt));
    m_dateDecal->setText(now.toString(QStringLiteral("ddd MMM d yyyy")));
}
