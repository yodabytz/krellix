#pragma once

#include <QObject>
#include <QString>

class Theme;
class QWidget;

// Abstract base for every krellix monitor. The owning MainWindow constructs
// the monitor, calls createWidget() to obtain a UI (which may be a single
// Panel or a container of multiple Panels — e.g. one per CPU core), and then
// drives tick() on a per-monitor QTimer.
//
// Lifetime: the returned QWidget is parented under `parent`; Qt owns it.
// The monitor itself is parented under MainWindow. Subclasses keep
// QPointer-tracked references to whatever sub-widgets they need to update.
class MonitorBase : public QObject
{
    Q_OBJECT

public:
    explicit MonitorBase(Theme *theme, QObject *parent = nullptr);
    ~MonitorBase() override;

    virtual QString id() const = 0;             // stable, slug-style
    virtual QString displayName() const = 0;    // human-readable label

    // Default cadence is 1 Hz; monitors may request faster/slower.
    virtual int tickIntervalMs() const { return 1000; }

    // Build the widget(s) as a child of `parent`. Returns the top-level
    // widget (Panel or container) to add to MainWindow's vbox.
    virtual QWidget *createWidget(QWidget *parent) = 0;

    virtual void tick() = 0;

    // Called before a live monitor is removed from the panel stack.
    // Subclasses with async work can cancel it here; QObject deletion may be
    // deferred with deleteLater().
    virtual void shutdown() {}

    Theme *theme() const { return m_theme; }

private:
    Theme *m_theme;

    Q_DISABLE_COPY_MOVE(MonitorBase)
};
