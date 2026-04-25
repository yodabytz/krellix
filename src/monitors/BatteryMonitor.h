#pragma once

#include "MonitorBase.h"

#include <QPointer>

class Decal;
class Krell;

class BatteryMonitor : public MonitorBase
{
    Q_OBJECT

public:
    explicit BatteryMonitor(Theme *theme, QObject *parent = nullptr);
    ~BatteryMonitor() override;

    QString id() const override          { return QStringLiteral("battery"); }
    QString displayName() const override { return QStringLiteral("Battery"); }
    int     tickIntervalMs() const override { return 5000; }

    QWidget *createWidget(QWidget *parent) override;
    void     tick() override;

private:
    QPointer<Decal> m_textDecal;
    QPointer<Krell> m_krell;
    QPointer<Decal> m_etaDecal;

    Q_DISABLE_COPY_MOVE(BatteryMonitor)
};
