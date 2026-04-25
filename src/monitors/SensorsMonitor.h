#pragma once

#include "MonitorBase.h"

#include <QHash>
#include <QPointer>

class Decal;

// One panel listing each detected temperature sensor as a row of text.
// First cut: text-only ("CPU 47°C"); fan/voltage and per-sensor krells
// can come later.
class SensorsMonitor : public MonitorBase
{
    Q_OBJECT

public:
    explicit SensorsMonitor(Theme *theme, QObject *parent = nullptr);
    ~SensorsMonitor() override;

    QString id() const override          { return QStringLiteral("sensors"); }
    QString displayName() const override { return QStringLiteral("Sensors"); }
    int     tickIntervalMs() const override { return 2000; }

    QWidget *createWidget(QWidget *parent) override;
    void     tick() override;

private:
    QHash<QString, QPointer<Decal>> m_rows;   // key = "chip:label"

    Q_DISABLE_COPY_MOVE(SensorsMonitor)
};
