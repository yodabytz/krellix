#pragma once

#include "MonitorBase.h"
#include "sysdep/DiskStat.h"

#include <QElapsedTimer>
#include <QHash>
#include <QPointer>

class Chart;
class Decal;
class Krell;

// One panel per whole disk: text decal "R 4.5M  W 0", read krell, write
// krell, combined-throughput chart. Adaptive scale per disk.
class DiskMonitor : public MonitorBase
{
    Q_OBJECT

public:
    explicit DiskMonitor(Theme *theme, QObject *parent = nullptr);
    ~DiskMonitor() override;

    QString id() const override          { return QStringLiteral("disk"); }
    QString displayName() const override { return QStringLiteral("Disk"); }
    int     tickIntervalMs() const override { return 1000; }

    QWidget *createWidget(QWidget *parent) override;
    void     tick() override;

private:
    struct DiskUI {
        QPointer<Decal> textDecal;
        QPointer<Krell> readKrell;
        QPointer<Krell> writeKrell;
        QPointer<Chart> chart;
        double          maxBps = 50.0 * 1024.0 * 1024.0;
    };

    QHash<QString, DiskUI>      m_disks;
    QHash<QString, DiskSample>  m_prevSamples;
    QElapsedTimer               m_lastReadTimer;
    bool                        m_havePrev = false;

    Q_DISABLE_COPY_MOVE(DiskMonitor)
};
