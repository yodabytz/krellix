#include "HostMonitor.h"

#include "remote/RemoteSource.h"
#include "widgets/Decal.h"
#include "widgets/Panel.h"

#include <QSettings>
#include <QSysInfo>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

// Resolve the canonical/fully-qualified hostname fresh each call. We
// previously cached this for the process lifetime, but that meant any
// runtime hostname change (or /etc/hosts edit) wasn't visible until the
// app was restarted — and the user explicitly hit that gotcha. The call
// resolves locally (gethostname + /etc/hosts) so the cost is negligible
// at the host monitor's 5-second tick rate.
QString resolveFqdn()
{
    char buf[256];
    if (gethostname(buf, sizeof(buf)) != 0) return QString();
    buf[sizeof(buf) - 1] = '\0';

    struct addrinfo hints{};
    hints.ai_flags  = AI_CANONNAME;
    hints.ai_family = AF_UNSPEC;
    struct addrinfo *result = nullptr;
    QString out;
    if (getaddrinfo(buf, nullptr, &hints, &result) == 0
        && result && result->ai_canonname) {
        out = QString::fromUtf8(result->ai_canonname);
    } else {
        out = QString::fromLatin1(buf);
    }
    if (result) freeaddrinfo(result);
    return out;
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
    p->setSurfaceKey(QStringLiteral("panel_bg_host"));
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

    // Helper: drop everything from the first dot onward when the user has
    // FQDN turned off. So "mail.quantumbytz.com" -> "mail" with the
    // checkbox unchecked, but the full name returns when checked.
    auto applyFqdnSetting = [fqdn](QString s) {
        if (fqdn) return s;
        const int dot = s.indexOf(QLatin1Char('.'));
        return (dot > 0) ? s.left(dot) : s;
    };

    // In remote-host mode (krellix --host ...), prefer the hostname &
    // kernel reported by the daemon. Still respect the FQDN setting —
    // strip the domain when the user has it unchecked, even though the
    // wire format always carries the full name.
    if (auto *r = RemoteSource::instance(); r && r->isConnected()) {
        const QString rh = r->remoteHostname();
        const QString rk = r->remoteKernel();
        if (!rh.isEmpty()) m_hostnameDecal->setText(applyFqdnSetting(rh));
        if (!rk.isEmpty()) m_sysDecal->setText(rk);
        if (!rh.isEmpty() || !rk.isEmpty()) return;
        // fall through to local readings until first remote sample arrives
    }

    const QString name = fqdn ? resolveFqdn() : QSysInfo::machineHostName();
    m_hostnameDecal->setText(name);
    m_sysDecal->setText(QSysInfo::kernelType()
                        + QStringLiteral(" ")
                        + QSysInfo::kernelVersion());
}
