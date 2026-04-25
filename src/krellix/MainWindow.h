#pragma once

#include <QList>
#include <QPoint>
#include <QStringList>
#include <QWidget>

class MonitorBase;
class Theme;
class QVBoxLayout;
class QMouseEvent;
class QContextMenuEvent;

// Top-level frameless window. Owns the Theme passed in (parented), constructs
// the requested monitors, lays them out vertically, and drives them with
// per-monitor QTimers. Provides drag-to-move and a right-click context menu.
class MainWindow : public QWidget
{
    Q_OBJECT

public:
    MainWindow(Theme *theme,
               const QStringList &enabledMonitorIds,
               QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private slots:
    void onThemeChanged();
    void showAbout();
    void toggleAlwaysOnTop();

private:
    void buildMonitors(const QStringList &enabledIds);
    void applyMinimumWidth();

    Theme       *m_theme;
    QVBoxLayout *m_layout    = nullptr;
    QList<MonitorBase *> m_monitors;

    bool   m_dragging   = false;
    QPoint m_dragOffset;

    Q_DISABLE_COPY_MOVE(MainWindow)
};
