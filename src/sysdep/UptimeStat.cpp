#include "UptimeStat.h"

#include <QByteArray>
#include <QFile>

namespace { UptimeStat::ReadFn g_readOverride = nullptr; }

void UptimeStat::setReadOverride(UptimeStat::ReadFn fn) { g_readOverride = fn; }

qint64 UptimeStat::secondsSinceBoot()
{
    if (g_readOverride) return g_readOverride();
    QFile f(QStringLiteral("/proc/uptime"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return -1;
    const QByteArray bytes = f.read(64);
    const int sp = bytes.indexOf(' ');
    const QByteArray first = (sp > 0) ? bytes.left(sp) : bytes.simplified();
    bool ok = false;
    const double secs = first.toDouble(&ok);
    if (!ok || secs < 0.0) return -1;
    return static_cast<qint64>(secs);
}
