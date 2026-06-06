#include "DiskStat.h"

#include <QByteArray>
#include <QByteArrayList>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#include <winioctl.h>

#include <cwchar>
#endif

Q_LOGGING_CATEGORY(lcDiskStat, "krellix.sysdep.disk")

namespace {
constexpr qint64 kProcDiskstatsMaxBytes = 1024 * 1024;
DiskStat::ReadFn g_readOverride = nullptr;
}

void DiskStat::setReadOverride(DiskStat::ReadFn fn) { g_readOverride = fn; }

QList<DiskSample> DiskStat::read()
{
    if (g_readOverride) return g_readOverride();
#if defined(Q_OS_WIN)
    QList<DiskSample> samples;

    constexpr DWORD kDriveBufferChars = 512;
    wchar_t drives[kDriveBufferChars] = {};
    const DWORD len = GetLogicalDriveStringsW(kDriveBufferChars - 1, drives);
    if (len == 0 || len >= kDriveBufferChars) {
        qCWarning(lcDiskStat) << "GetLogicalDriveStrings failed";
        return samples;
    }

    for (const wchar_t *drive = drives; *drive; drive += wcslen(drive) + 1) {
        if (GetDriveTypeW(drive) == DRIVE_NO_ROOT_DIR)
            continue;

        QString name = QString::fromWCharArray(drive);
        if (name.endsWith(QLatin1Char('\\')))
            name.chop(1);

        QString device = QStringLiteral("\\\\.\\") + name;
        HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(device.utf16()),
                               0,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr,
                               OPEN_EXISTING,
                               0,
                               nullptr);
        if (h == INVALID_HANDLE_VALUE)
            continue;

        DISK_PERFORMANCE perf{};
        DWORD returned = 0;
        const BOOL ok = DeviceIoControl(h,
                                        IOCTL_DISK_PERFORMANCE,
                                        nullptr,
                                        0,
                                        &perf,
                                        sizeof(perf),
                                        &returned,
                                        nullptr);
        CloseHandle(h);
        if (!ok)
            continue;

        DiskSample s;
        s.name = name;
        s.sectorsRead = static_cast<quint64>(qMax<LONGLONG>(0, perf.BytesRead.QuadPart))
                        / DiskStat::kSectorSize;
        s.sectorsWritten = static_cast<quint64>(qMax<LONGLONG>(0, perf.BytesWritten.QuadPart))
                           / DiskStat::kSectorSize;
        samples.append(s);
    }

    return samples;
#else
    QFile f(QStringLiteral("/proc/diskstats"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(lcDiskStat) << "cannot open /proc/diskstats:" << f.errorString();
        return {};
    }
    const QByteArray bytes = f.read(kProcDiskstatsMaxBytes);
    if (bytes.isEmpty()) return {};

    QList<DiskSample> samples;

    int start = 0;
    while (start < bytes.size()) {
        const int nl  = bytes.indexOf('\n', start);
        const int end = (nl < 0) ? bytes.size() : nl;
        if (end > start) {
            const QByteArray line = bytes.mid(start, end - start);
            const QByteArrayList parts = line.simplified().split(' ');
            if (parts.size() >= 14) {
                const QString name = QString::fromLatin1(parts[2]);
                // Whole-disk filter: /sys/block/<name>/ exists for whole
                // disks but not for partitions, loop devices, or dm-*.
                if (QFileInfo::exists(QStringLiteral("/sys/block/") + name)) {
                    DiskSample s;
                    s.name           = name;
                    s.sectorsRead    = parts[5].toULongLong();
                    s.sectorsWritten = parts[9].toULongLong();
                    samples.append(s);
                }
            }
        }
        if (nl < 0) break;
        start = nl + 1;
    }
    return samples;
#endif
}
