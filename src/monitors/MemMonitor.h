#pragma once

#include "MonitorBase.h"

#include <QPointer>

class Decal;
class Krell;
class Meter;

// Memory + swap in one panel: text "used / total" plus a krell each.
class MemMonitor : public MonitorBase
{
    Q_OBJECT

public:
    explicit MemMonitor(Theme *theme, QObject *parent = nullptr);
    ~MemMonitor() override;

    QString id() const override          { return QStringLiteral("mem"); }
    QString displayName() const override { return QStringLiteral("Memory"); }
    int     tickIntervalMs() const override { return 1000; }

    QWidget *createWidget(QWidget *parent) override;
    void     tick() override;

private:
    QPointer<Decal> m_memText;
    QPointer<Meter> m_memMeter;
    QPointer<Decal> m_swapText;
    QPointer<Meter> m_swapMeter;

    Q_DISABLE_COPY_MOVE(MemMonitor)
};
