#pragma once

#include "MonitorBase.h"

#include <QPointer>

class Decal;

class ClockMonitor : public MonitorBase
{
    Q_OBJECT

public:
    explicit ClockMonitor(Theme *theme, QObject *parent = nullptr);
    ~ClockMonitor() override;

    QString id() const override          { return QStringLiteral("clock"); }
    QString displayName() const override { return QStringLiteral("Clock"); }
    int     tickIntervalMs() const override { return 1000; }

    QWidget *createWidget(QWidget *parent) override;
    void     tick() override;

private:
    QPointer<Decal> m_timeDecal;
    QPointer<Decal> m_dateDecal;

    Q_DISABLE_COPY_MOVE(ClockMonitor)
};
