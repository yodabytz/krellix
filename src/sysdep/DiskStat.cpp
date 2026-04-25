#include "DiskStat.h"

#include <QByteArray>
#include <QByteArrayList>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcDiskStat, "krellix.sysdep.disk")

namespace {
constexpr qint64 kProcDiskstatsMaxBytes = 1024 * 1024;
DiskStat::ReadFn g_readOverride = nullptr;
}

void DiskStat::setReadOverride(DiskStat::ReadFn fn) { g_readOverride = fn; }

QList<DiskSample> DiskStat::read()
{
    if (g_readOverride) return g_readOverride();
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
}
