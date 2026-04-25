#pragma once

#include <QtGlobal>

// Snapshot of /proc/meminfo. All sizes are in KiB as the kernel reports them.
// Use the helper getters for ratios in the [0, 1] range expected by Krell.
struct MemInfo {
    quint64 totalKb     = 0;
    quint64 freeKb      = 0;
    quint64 availableKb = 0;   // MemAvailable; kernel-computed "really free"
    quint64 buffersKb   = 0;
    quint64 cachedKb    = 0;
    quint64 swapTotalKb = 0;
    quint64 swapFreeKb  = 0;

    bool valid() const { return totalKb > 0; }

    quint64 memUsedKb() const {
        // Prefer kernel-reported MemAvailable when present; falls back to
        // free + buffers + cached for older kernels that don't expose it.
        const quint64 freeish = (availableKb > 0)
            ? availableKb
            : (freeKb + buffersKb + cachedKb);
        return (freeish > totalKb) ? 0 : (totalKb - freeish);
    }
    quint64 swapUsedKb() const {
        return (swapFreeKb > swapTotalKb) ? 0 : (swapTotalKb - swapFreeKb);
    }
    double memUsedRatio() const {
        return totalKb     ? static_cast<double>(memUsedKb())  / static_cast<double>(totalKb)     : 0.0;
    }
    double swapUsedRatio() const {
        return swapTotalKb ? static_cast<double>(swapUsedKb()) / static_cast<double>(swapTotalKb) : 0.0;
    }
};

class MemStat
{
public:
    // Reads /proc/meminfo. Returns a MemInfo with valid()==false on failure.
    static MemInfo read();
};
