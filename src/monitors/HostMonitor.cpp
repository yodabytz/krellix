#include "HostMonitor.h"

#include "widgets/Decal.h"
#include "widgets/Panel.h"

#include <QSysInfo>

HostMonitor::HostMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
}

HostMonitor::~HostMonitor() = default;

QWidget *HostMonitor::createWidget(QWidget *parent)
{
    auto *p = new Panel(theme(), parent);
    p->setTitle(QStringLiteral("krellix"));
    m_hostnameDecal = p->addDecal(QStringLiteral("value"),
                                  QStringLiteral("text_primary"));
    m_sysDecal      = p->addDecal(QStringLiteral("label"),
                                  QStringLiteral("text_secondary"));
    tick();
    return p;
}

void HostMonitor::tick()
{
    if (!m_hostnameDecal || !m_sysDecal) return;
    m_hostnameDecal->setText(QSysInfo::machineHostName());
    m_sysDecal->setText(QSysInfo::kernelType()
                        + QStringLiteral(" ")
                        + QSysInfo::kernelVersion());
}
