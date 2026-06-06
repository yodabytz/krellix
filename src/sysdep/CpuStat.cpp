#include "CpuStat.h"

#include <QByteArray>
#include <QByteArrayList>
#include <QFile>
#include <QLoggingCategory>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#elif defined(Q_OS_MACOS)
#include <mach/host_info.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/processor_info.h>
#include <mach/vm_map.h>
#endif

Q_LOGGING_CATEGORY(lcCpuStat, "krellix.sysdep.cpu")

namespace {
CpuStat::ReadFn g_readOverride = nullptr;
} // namespace

void CpuStat::setReadOverride(CpuStat::ReadFn fn)
{
    g_readOverride = fn;
}

namespace {

constexpr qint64 kProcStatMaxBytes = 256 * 1024;  // hard cap; /proc/stat is tiny

#if defined(Q_OS_WIN)
struct SystemProcessorPerformanceInformation {
    LARGE_INTEGER IdleTime;
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER DpcTime;
    LARGE_INTEGER InterruptTime;
    ULONG InterruptCount;
};

using NtQuerySystemInformationFn = LONG (WINAPI *)(ULONG, PVOID, ULONG, PULONG);

quint64 fileTimeToTicks(const FILETIME &ft)
{
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

QList<CpuSample> readWindowsCpuStat()
{
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto query = ntdll
        ? reinterpret_cast<NtQuerySystemInformationFn>(
              GetProcAddress(ntdll, "NtQuerySystemInformation"))
        : nullptr;

    if (query) {
        const DWORD cpuCount = qMax<DWORD>(1, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
        QList<SystemProcessorPerformanceInformation> raw;
        raw.resize(static_cast<int>(cpuCount));
        ULONG returned = 0;
        constexpr ULONG kSystemProcessorPerformanceInformation = 8;
        const LONG status = query(kSystemProcessorPerformanceInformation,
                                  raw.data(),
                                  static_cast<ULONG>(raw.size() * sizeof(raw.first())),
                                  &returned);
        if (status >= 0) {
            QList<CpuSample> samples;
            samples.reserve(raw.size() + 1);
            CpuSample aggregate;
            aggregate.name = QStringLiteral("cpu");
            aggregate.index = -1;

            for (int i = 0; i < raw.size(); ++i) {
                const auto &p = raw.at(i);
                CpuSample s;
                s.name = QStringLiteral("cpu%1").arg(i);
                s.index = i;
                s.user = static_cast<quint64>(qMax<LONGLONG>(0, p.UserTime.QuadPart));
                s.idle = static_cast<quint64>(qMax<LONGLONG>(0, p.IdleTime.QuadPart));
                const LONGLONG kernelBusy = p.KernelTime.QuadPart - p.IdleTime.QuadPart;
                s.sys = static_cast<quint64>(qMax<LONGLONG>(0, kernelBusy));
                s.irq = static_cast<quint64>(qMax<LONGLONG>(0, p.InterruptTime.QuadPart));
                s.softirq = static_cast<quint64>(qMax<LONGLONG>(0, p.DpcTime.QuadPart));

                aggregate.user += s.user;
                aggregate.sys += s.sys;
                aggregate.idle += s.idle;
                aggregate.irq += s.irq;
                aggregate.softirq += s.softirq;
                samples.append(s);
            }
            samples.prepend(aggregate);
            return samples;
        }
    }

    FILETIME idleTime{}, kernelTime{}, userTime{};
    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime))
        return {};

    CpuSample aggregate;
    aggregate.name = QStringLiteral("cpu");
    aggregate.index = -1;
    aggregate.idle = fileTimeToTicks(idleTime);
    aggregate.user = fileTimeToTicks(userTime);
    const quint64 kernel = fileTimeToTicks(kernelTime);
    aggregate.sys = kernel > aggregate.idle ? kernel - aggregate.idle : 0;
    return {aggregate};
}
#endif

#if defined(Q_OS_MACOS)
QList<CpuSample> readMacCpuStat()
{
    processor_cpu_load_info_t cpuInfo = nullptr;
    mach_msg_type_number_t cpuInfoCount = 0;
    natural_t cpuCount = 0;

    const kern_return_t kr = host_processor_info(mach_host_self(),
                                                 PROCESSOR_CPU_LOAD_INFO,
                                                 &cpuCount,
                                                 reinterpret_cast<processor_info_array_t *>(&cpuInfo),
                                                 &cpuInfoCount);
    if (kr != KERN_SUCCESS || !cpuInfo || cpuCount == 0) {
        qCWarning(lcCpuStat) << "host_processor_info failed:" << kr;
        return {};
    }

    QList<CpuSample> samples;
    samples.reserve(static_cast<int>(cpuCount) + 1);

    CpuSample aggregate;
    aggregate.name = QStringLiteral("cpu");
    aggregate.index = -1;

    for (natural_t i = 0; i < cpuCount; ++i) {
        const auto *ticks = cpuInfo[i].cpu_ticks;
        CpuSample s;
        s.name = QStringLiteral("cpu%1").arg(i);
        s.index = static_cast<int>(i);
        s.user = static_cast<quint64>(ticks[CPU_STATE_USER]);
        s.nice = static_cast<quint64>(ticks[CPU_STATE_NICE]);
        s.sys = static_cast<quint64>(ticks[CPU_STATE_SYSTEM]);
        s.idle = static_cast<quint64>(ticks[CPU_STATE_IDLE]);

        aggregate.user += s.user;
        aggregate.nice += s.nice;
        aggregate.sys += s.sys;
        aggregate.idle += s.idle;
        samples.append(s);
    }

    samples.prepend(aggregate);
    vm_deallocate(mach_task_self(),
                  reinterpret_cast<vm_address_t>(cpuInfo),
                  static_cast<vm_size_t>(cpuInfoCount * sizeof(integer_t)));
    return samples;
}
#endif

// Parse one "cpu" or "cpuN" line into a CpuSample. Returns false on malformed.
bool parseCpuLine(const QByteArray &line, CpuSample &out)
{
    const QByteArrayList parts = line.simplified().split(' ');
    if (parts.size() < 5) return false;       // need at least user nice sys idle

    const QByteArray label = parts[0];
    if (!label.startsWith("cpu")) return false;

    out.name = QString::fromLatin1(label);
    out.index = -1;
    if (label.size() > 3) {
        bool ok = false;
        const int idx = label.mid(3).toInt(&ok);
        if (!ok) return false;
        out.index = idx;
    }

    auto field = [&](int i) -> quint64 {
        if (i + 1 >= parts.size()) return 0;  // older kernels truncate fields
        bool ok = false;
        const quint64 v = parts[i + 1].toULongLong(&ok);
        return ok ? v : 0;
    };

    out.user      = field(0);
    out.nice      = field(1);
    out.sys       = field(2);
    out.idle      = field(3);
    out.iowait    = field(4);
    out.irq       = field(5);
    out.softirq   = field(6);
    out.steal     = field(7);
    // Linux 2.6.24+ adds guest after steal; 2.6.33+ adds guest_nice.
    // field() returns 0 for missing fields on older kernels, which is
    // exactly what we want — pre-virtualization counters never had any
    // guest time to subtract.
    out.guest     = field(8);
    out.guestNice = field(9);
    return true;
}

} // namespace

QList<CpuSample> CpuStat::read()
{
    if (g_readOverride) return g_readOverride();
#if defined(Q_OS_WIN)
    return readWindowsCpuStat();
#elif defined(Q_OS_MACOS)
    return readMacCpuStat();
#else
    QFile f(QStringLiteral("/proc/stat"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(lcCpuStat) << "cannot open /proc/stat:" << f.errorString();
        return {};
    }

    const QByteArray bytes = f.read(kProcStatMaxBytes);
    if (bytes.isEmpty()) return {};

    QList<CpuSample> samples;
    samples.reserve(16);

    int start = 0;
    while (start < bytes.size()) {
        const int nl = bytes.indexOf('\n', start);
        const int end = (nl < 0) ? bytes.size() : nl;
        if (end > start) {
            const QByteArray line = bytes.mid(start, end - start);
            if (line.startsWith("cpu")) {
                CpuSample s;
                if (parseCpuLine(line, s)) {
                    samples.append(s);
                } else {
                    qCDebug(lcCpuStat) << "skip malformed cpu line";
                }
            } else {
                // /proc/stat lists cpu lines first; once we see a non-cpu
                // line we know we're past the cpu block.
                if (!samples.isEmpty()) break;
            }
        }
        if (nl < 0) break;
        start = nl + 1;
    }
    return samples;
#endif
}

double CpuStat::utilization(const CpuSample &prev, const CpuSample &curr)
{
    if (prev.index != curr.index || prev.name != curr.name) return 0.0;

    const quint64 totPrev  = prev.totalAll();
    const quint64 totCurr  = curr.totalAll();
    if (totCurr <= totPrev) return 0.0;

    const quint64 idlePrev = prev.totalIdle();
    const quint64 idleCurr = curr.totalIdle();

    const double dTotal = static_cast<double>(totCurr  - totPrev);
    const double dIdle  = static_cast<double>(idleCurr > idlePrev
                                              ? idleCurr - idlePrev : 0);
    if (dTotal <= 0.0) return 0.0;

    const double util = 1.0 - (dIdle / dTotal);
    if (util < 0.0) return 0.0;
    if (util > 1.0) return 1.0;
    return util;
}
