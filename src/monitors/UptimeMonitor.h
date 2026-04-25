#pragma once

#include "MonitorBase.h"

#include <QPointer>

class Decal;

class UptimeMonitor : public MonitorBase
{
    Q_OBJECT

public:
    explicit UptimeMonitor(Theme *theme, QObject *parent = nullptr);
    ~UptimeMonitor() override;

    QString id() const override          { return QStringLiteral("uptime"); }
    QString displayName() const override { return QStringLiteral("Uptime"); }
    int     tickIntervalMs() const override { return 30000; }  // 30s is fine for whole-minute display

    QWidget *createWidget(QWidget *parent) override;
    void     tick() override;

private:
    QPointer<Decal> m_decal;

    Q_DISABLE_COPY_MOVE(UptimeMonitor)
};
