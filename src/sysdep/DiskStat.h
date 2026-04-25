#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

struct DiskSample {
    QString name;            // sda, nvme0n1, mmcblk0
    quint64 sectorsRead    = 0;
    quint64 sectorsWritten = 0;
};

class DiskStat
{
public:
    static constexpr qint64 kSectorSize = 512;

    // Reads /proc/diskstats. Returns whole disks only — partitions and
    // virtual devices (loop, ram, dm-) are filtered out by checking that
    // /sys/block/<name>/ exists. Honors a remote override when set.
    static QList<DiskSample> read();

    using ReadFn = QList<DiskSample> (*)();
    static void setReadOverride(ReadFn fn);
};
