#include "CpuStat.h"

#include <QByteArray>
#include <QByteArrayList>
#include <QFile>
#include <QLoggingCategory>

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
