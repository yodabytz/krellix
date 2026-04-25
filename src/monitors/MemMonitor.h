#pragma once

#include "MonitorBase.h"

#include <QPointer>

class Chart;
class Decal;
class Krell;

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
    QPointer<Krell> m_memKrell;
    QPointer<Chart> m_memChart;
    QPointer<Decal> m_swapText;
    QPointer<Krell> m_swapKrell;

    Q_DISABLE_COPY_MOVE(MemMonitor)
};
