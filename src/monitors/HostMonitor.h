#pragma once

#include "MonitorBase.h"

#include <QPointer>

class Decal;

class HostMonitor : public MonitorBase
{
    Q_OBJECT

public:
    explicit HostMonitor(Theme *theme, QObject *parent = nullptr);
    ~HostMonitor() override;

    QString id() const override          { return QStringLiteral("host"); }
    QString displayName() const override { return QStringLiteral("Host"); }
    int     tickIntervalMs() const override { return 5000; }

    QWidget *createWidget(QWidget *parent) override;
    void     tick() override;

private:
    QPointer<Decal> m_hostnameDecal;
    QPointer<Decal> m_sysDecal;

    Q_DISABLE_COPY_MOVE(HostMonitor)
};
