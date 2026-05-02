#include "ProcMonitor.h"

#include "sysdep/ProcStat.h"
#include "widgets/Decal.h"
#include "widgets/Panel.h"

ProcMonitor::ProcMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
}

ProcMonitor::~ProcMonitor() = default;

QWidget *ProcMonitor::createWidget(QWidget *parent)
{
    auto *p = new Panel(theme(), parent);
    p->setSurfaceKey(QStringLiteral("panel_bg_proc"));
    p->setTitle(QStringLiteral("Proc"));
    m_text = p->addDecal(QStringLiteral("label"),
                         QStringLiteral("text_primary"));
    if (m_text) m_text->setAlignment(Qt::AlignHCenter);
    tick();
    return p;
}

void ProcMonitor::tick()
{
    if (!m_text) return;
    const ProcInfo info = ProcStat::read();
    m_text->setText(QStringLiteral("P %1  U %2")
                    .arg(info.processes)
                    .arg(info.users));
}
