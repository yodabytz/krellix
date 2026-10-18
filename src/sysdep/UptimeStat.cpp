#include "UptimeStat.h"

#include <QByteArray>
#include <QDateTime>
#include <QFile>

#if defined(Q_OS_MACOS)
#include <sys/sysctl.h>
#endif

namespace { UptimeStat::ReadFn g_readOverride = nullptr; }

void UptimeStat::setReadOverride(UptimeStat::ReadFn fn) { g_readOverride = fn; }

qint64 UptimeStat::secondsSinceBoot()
{
    if (g_readOverride) return g_readOverride();
#if defined(Q_OS_MACOS)
    timeval boottime{};
    size_t size = sizeof(boottime);
    const int mib[2] = {CTL_KERN, KERN_BOOTTIME};
    if (sysctl(const_cast<int *>(mib), 2, &boottime, &size, nullptr, 0) != 0
        || boottime.tv_sec <= 0) {
        return -1;
    }
    return QDateTime::currentSecsSinceEpoch() - static_cast<qint64>(boottime.tv_sec);
#else
    QFile f(QStringLiteral("/proc/uptime"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return -1;
    const QByteArray bytes = f.read(64);
    const int sp = bytes.indexOf(' ');
    const QByteArray first = (sp > 0) ? bytes.left(sp) : bytes.simplified();
    bool ok = false;
    const double secs = first.toDouble(&ok);
    if (!ok || secs < 0.0) return -1;
    return static_cast<qint64>(secs);
#endif
}
