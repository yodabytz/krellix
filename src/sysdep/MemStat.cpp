#include "MemStat.h"

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcMemStat, "krellix.sysdep.mem")

namespace { MemStat::ReadFn g_readOverride = nullptr; }

void MemStat::setReadOverride(MemStat::ReadFn fn) { g_readOverride = fn; }

namespace {

constexpr qint64 kProcMeminfoMaxBytes = 64 * 1024;  // /proc/meminfo is tiny

// Each line is "Key:     12345 kB" (or no unit for some fields).
// We only care about the integer; unit is always kB on Linux for the keys
// we read. Returns 0 on malformed lines.
quint64 parseValueKb(const QByteArray &line)
{
    const int colon = line.indexOf(':');
    if (colon < 0) return 0;

    QByteArray rhs = line.mid(colon + 1).trimmed();
    if (rhs.isEmpty()) return 0;

    // Strip trailing " kB" if present.
    if (rhs.endsWith(" kB")) rhs.chop(3);
    rhs = rhs.trimmed();

    bool ok = false;
    const quint64 v = rhs.toULongLong(&ok);
    return ok ? v : 0;
}

} // namespace

MemInfo MemStat::read()
{
    if (g_readOverride) return g_readOverride();
    MemInfo out;
    QFile f(QStringLiteral("/proc/meminfo"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(lcMemStat) << "cannot open /proc/meminfo:" << f.errorString();
        return out;
    }

    const QByteArray bytes = f.read(kProcMeminfoMaxBytes);
    if (bytes.isEmpty()) return out;

    int start = 0;
    while (start < bytes.size()) {
        const int nl = bytes.indexOf('\n', start);
        const int end = (nl < 0) ? bytes.size() : nl;
        if (end > start) {
            const QByteArray line = bytes.mid(start, end - start);
            if      (line.startsWith("MemTotal:"))     out.totalKb     = parseValueKb(line);
            else if (line.startsWith("MemFree:"))      out.freeKb      = parseValueKb(line);
            else if (line.startsWith("MemAvailable:")) out.availableKb = parseValueKb(line);
            else if (line.startsWith("Buffers:"))      out.buffersKb   = parseValueKb(line);
            else if (line.startsWith("Cached:"))       out.cachedKb    = parseValueKb(line);
            else if (line.startsWith("SwapTotal:"))    out.swapTotalKb = parseValueKb(line);
            else if (line.startsWith("SwapFree:"))     out.swapFreeKb  = parseValueKb(line);
        }
        if (nl < 0) break;
        start = nl + 1;
    }
    return out;
}
