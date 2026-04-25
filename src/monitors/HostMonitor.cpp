#include "HostMonitor.h"

#include "widgets/Decal.h"
#include "widgets/Panel.h"

#include <QHostInfo>
#include <QSettings>
#include <QSysInfo>

HostMonitor::HostMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
}

HostMonitor::~HostMonitor() = default;

QWidget *HostMonitor::createWidget(QWidget *parent)
{
    auto *p = new Panel(theme(), parent);
    // No title — the hostname IS the top label, like classic gkrellm.
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

    const bool fqdn =
        QSettings().value(QStringLiteral("host/show_fqdn"), false).toBool();

    QString name = QSysInfo::machineHostName();
    if (fqdn) {
        const QString domain = QHostInfo::localDomainName();
        if (!domain.isEmpty()
            && !name.endsWith(QLatin1Char('.') + domain)
            && name != domain) {
            name = name + QLatin1Char('.') + domain;
        }
    }
    m_hostnameDecal->setText(name);
    m_sysDecal->setText(QSysInfo::kernelType()
                        + QStringLiteral(" ")
                        + QSysInfo::kernelVersion());
}
