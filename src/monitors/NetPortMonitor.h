#pragma once

#include "MonitorBase.h"

#include <QList>
#include <QPointer>
#include <QString>

class Decal;
class Panel;
class QVBoxLayout;
struct NetPortSample;

class NetPortMonitor : public MonitorBase
{
    Q_OBJECT

public:
    explicit NetPortMonitor(Theme *theme, QObject *parent = nullptr);
    ~NetPortMonitor() override;

    QString id() const override          { return QStringLiteral("netports"); }
    QString displayName() const override { return QStringLiteral("Net Ports"); }
    int     tickIntervalMs() const override { return 3000; }

    QWidget *createWidget(QWidget *parent) override;
    void     tick() override;

private:
    struct Watch {
        QString label;
        QString protocol;
        QString ports;
    };
    static bool sameWatches(const QList<Watch> &a, const QList<Watch> &b);

    QList<Watch> configuredWatches() const;
    void rebuildRows(const QList<Watch> &watches);
    int countMatches(const Watch &watch, const QList<NetPortSample> &samples) const;

    QPointer<Panel> m_panel;
    QPointer<QWidget> m_rowsWidget;
    QVBoxLayout *m_rowsLayout = nullptr;
    QList<QPointer<Decal>> m_rows;
    QList<Watch> m_watches;

    Q_DISABLE_COPY_MOVE(NetPortMonitor)
};
