#include "MemStat.h"

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QLoggingCategory>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#elif defined(Q_OS_MACOS)
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <sys/sysctl.h>
#endif

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

#if defined(Q_OS_MACOS)
quint64 sysctlUint64(const char *name)
{
    quint64 value = 0;
    size_t size = sizeof(value);
    if (sysctlbyname(name, &value, &size, nullptr, 0) != 0)
        return 0;
    return value;
}

quint64 bytesToKb(quint64 bytes)
{
    return bytes / 1024ULL;
}
#endif

} // namespace

MemInfo MemStat::read()
{
    if (g_readOverride) return g_readOverride();
    MemInfo out;
#if defined(Q_OS_WIN)
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    if (!GlobalMemoryStatusEx(&mem)) {
        qCWarning(lcMemStat) << "GlobalMemoryStatusEx failed";
        return out;
    }

    out.totalKb = static_cast<quint64>(mem.ullTotalPhys / 1024ULL);
    out.availableKb = static_cast<quint64>(mem.ullAvailPhys / 1024ULL);
    out.freeKb = out.availableKb;
    out.swapTotalKb = static_cast<quint64>(mem.ullTotalPageFile / 1024ULL);
    out.swapFreeKb = static_cast<quint64>(mem.ullAvailPageFile / 1024ULL);
    return out;
#elif defined(Q_OS_MACOS)
    const quint64 totalBytes = sysctlUint64("hw.memsize");
    if (totalBytes == 0) {
        qCWarning(lcMemStat) << "cannot read hw.memsize";
        return out;
    }

    vm_size_t pageSize = 0;
    if (host_page_size(mach_host_self(), &pageSize) != KERN_SUCCESS || pageSize == 0) {
        qCWarning(lcMemStat) << "cannot read host page size";
        return out;
    }

    vm_statistics64_data_t vmStats;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    const kern_return_t kr = host_statistics64(mach_host_self(),
                                               HOST_VM_INFO64,
                                               reinterpret_cast<host_info64_t>(&vmStats),
                                               &count);
    if (kr != KERN_SUCCESS) {
        qCWarning(lcMemStat) << "host_statistics64 failed:" << kr;
        return out;
    }

    const quint64 pageKb = static_cast<quint64>(pageSize) / 1024ULL;
    const quint64 freeKb = static_cast<quint64>(vmStats.free_count) * pageKb;
    const quint64 inactiveKb = static_cast<quint64>(vmStats.inactive_count) * pageKb;
    const quint64 speculativeKb = static_cast<quint64>(vmStats.speculative_count) * pageKb;

    out.totalKb = bytesToKb(totalBytes);
    out.freeKb = freeKb;
    out.availableKb = freeKb + inactiveKb + speculativeKb;
    out.cachedKb = inactiveKb + speculativeKb;

    xsw_usage swap;
    size_t swapSize = sizeof(swap);
    if (sysctlbyname("vm.swapusage", &swap, &swapSize, nullptr, 0) == 0) {
        out.swapTotalKb = bytesToKb(static_cast<quint64>(swap.xsu_total));
        out.swapFreeKb = bytesToKb(static_cast<quint64>(swap.xsu_avail));
    }
    return out;
#else
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
#endif
}
