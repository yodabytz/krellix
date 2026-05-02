#pragma once

#include "MonitorBase.h"

#include <QPointer>

class Decal;

class ProcMonitor : public MonitorBase
{
    Q_OBJECT

public:
    explicit ProcMonitor(Theme *theme, QObject *parent = nullptr);
    ~ProcMonitor() override;

    QString id() const override          { return QStringLiteral("proc"); }
    QString displayName() const override { return QStringLiteral("Proc"); }
    int     tickIntervalMs() const override { return 5000; }

    QWidget *createWidget(QWidget *parent) override;
    void     tick() override;

private:
    QPointer<Decal> m_text;

    Q_DISABLE_COPY_MOVE(ProcMonitor)
};
