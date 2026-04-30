#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

// Per-CPU snapshot read from /proc/stat. Counters are jiffies since boot;
// utilization is computed by diffing two samples.
//
// On kernels with virtualization the line carries two extra fields after
// `steal`: `guest` (time spent running a guest VM in user mode) and
// `guest_nice` (same but at niced priority). Critically, `guest` is
// already counted inside `user`, and `guest_nice` is counted inside
// `nice` — so naively summing user+nice+...+steal double-counts guest
// time and inflates "user" CPU utilization on hosts that run VMs.
// effectiveUser()/effectiveNice() back guest time out; totalAll() uses
// the effective values.
struct CpuSample {
    QString  name;          // "cpu" (aggregate) or "cpu0", "cpu1", ...
    int      index = -1;    // -1 for the aggregate; 0,1,2... for cores
    quint64  user      = 0;
    quint64  nice      = 0;
    quint64  sys       = 0;
    quint64  idle      = 0;
    quint64  iowait    = 0;
    quint64  irq       = 0;
    quint64  softirq   = 0;
    quint64  steal     = 0;
    quint64  guest     = 0;
    quint64  guestNice = 0;

    quint64 effectiveUser() const {
        return user > guest ? user - guest : 0;
    }
    quint64 effectiveNice() const {
        return nice > guestNice ? nice - guestNice : 0;
    }

    quint64 totalIdle() const { return idle + iowait; }
    quint64 totalAll()  const {
        return effectiveUser() + effectiveNice() + sys + irq + softirq
             + steal + idle + iowait;
    }
};

class CpuStat
{
public:
    // Reads /proc/stat by default; if a remote override has been installed
    // (see setReadOverride) calls that instead. First entry is the
    // aggregate ("cpu"), followed by per-core entries in CPU index order.
    // Empty list on failure.
    static QList<CpuSample> read();

    // Install a function that returns CPU samples in lieu of /proc/stat —
    // krellix uses this to route reads through RemoteSource when running
    // in --host client mode. Pass nullptr to restore local-only behavior.
    using ReadFn = QList<CpuSample> (*)();
    static void setReadOverride(ReadFn fn);

    // Compute utilization (0..1) between two samples of the same CPU.
    // Returns 0 if samples don't differ or differ in name/index.
    static double utilization(const CpuSample &prev, const CpuSample &curr);
};
