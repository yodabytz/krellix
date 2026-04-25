#pragma once

#include "widgets/Panel.h"

#include <QObject>
#include <QPointer>
#include <QString>

class Theme;
class QWidget;

// Abstract base for every krellix monitor. The owning MainWindow constructs
// the monitor, calls createPanel() to obtain a UI, and then drives tick() on
// a central QTimer. The monitor never owns its Panel — Qt parent-child does.
class MonitorBase : public QObject
{
    Q_OBJECT

public:
    explicit MonitorBase(Theme *theme, QObject *parent = nullptr);
    ~MonitorBase() override;

    virtual QString id() const = 0;             // stable, slug-style
    virtual QString displayName() const = 0;    // human-readable label

    // Default cadence is 1 Hz; monitors may request faster updates.
    virtual int tickIntervalMs() const { return 1000; }

    // Build the panel as a child of `panelParent`. Lifetime is owned by Qt
    // parent-child; the monitor only keeps a weak QPointer to it.
    virtual Panel *createPanel(QWidget *panelParent) = 0;

    virtual void tick() = 0;

    Panel *panel() const { return m_panel.data(); }
    Theme *theme() const { return m_theme; }

protected:
    void setPanel(Panel *p) { m_panel = p; }

private:
    Theme           *m_theme;
    QPointer<Panel>  m_panel;

    Q_DISABLE_COPY_MOVE(MonitorBase)
};
