#include "HostMonitor.h"

#include "widgets/Decal.h"
#include "widgets/Panel.h"

#include <QSettings>
#include <QSysInfo>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

// Cached FQDN — getaddrinfo() can block on DNS, so we resolve once per
// process and reuse. Empty until first computed; falls back to the short
// hostname if resolution fails.
QString cachedFqdn()
{
    static QString cache;
    static bool    computed = false;
    if (computed) return cache;
    computed = true;

    char buf[256];
    if (gethostname(buf, sizeof(buf)) != 0) return cache;
    buf[sizeof(buf) - 1] = '\0';

    struct addrinfo hints{};
    hints.ai_flags  = AI_CANONNAME;
    hints.ai_family = AF_UNSPEC;
    struct addrinfo *result = nullptr;
    if (getaddrinfo(buf, nullptr, &hints, &result) == 0
        && result && result->ai_canonname) {
        cache = QString::fromUtf8(result->ai_canonname);
    } else {
        cache = QString::fromLatin1(buf);
    }
    if (result) freeaddrinfo(result);
    return cache;
}

} // namespace

HostMonitor::HostMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
}

HostMonitor::~HostMonitor() = default;

QWidget *HostMonitor::createWidget(QWidget *parent)
{
    auto *p = new Panel(theme(), parent);
    // Hostname uses the lighter "label" font (was overpowering as "value")
    // and centers under the panel — matches the clock+date below it.
    m_hostnameDecal = p->addDecal(QStringLiteral("label"),
                                  QStringLiteral("text_primary"));
    m_sysDecal      = p->addDecal(QStringLiteral("label"),
                                  QStringLiteral("text_secondary"));
    if (m_hostnameDecal) m_hostnameDecal->setAlignment(Qt::AlignHCenter);
    if (m_sysDecal)      m_sysDecal->setAlignment(Qt::AlignHCenter);
    tick();
    return p;
}

void HostMonitor::tick()
{
    if (!m_hostnameDecal || !m_sysDecal) return;

    const bool fqdn =
        QSettings().value(QStringLiteral("host/show_fqdn"), false).toBool();

    const QString name = fqdn ? cachedFqdn() : QSysInfo::machineHostName();
    m_hostnameDecal->setText(name);
    m_sysDecal->setText(QSysInfo::kernelType()
                        + QStringLiteral(" ")
                        + QSysInfo::kernelVersion());
}
