#pragma once

#include <QList>
#include <QPoint>
#include <QStringList>
#include <QWidget>

class MonitorBase;
class PluginLoader;
class Theme;
class QVBoxLayout;
class QMouseEvent;
class QContextMenuEvent;
class QCloseEvent;
class QPaintEvent;

// Top-level frameless window. Owns the Theme passed in (parented), constructs
// the requested built-in monitors, loads plugin monitors, lays them out
// vertically, and drives them with per-monitor QTimers.
//
// User interaction:
//   - left-button drag on background: move (compositor-driven via
//     QWindow::startSystemMove with a manual fallback)
//   - QSizeGrip in the bottom-right corner: resize
//   - right-click: context menu (always-on-top, About, Quit)
//
// Window size and position persist across restarts via QSettings.
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
    void closeEvent(QCloseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onThemeChanged();
    void showAbout();
    void showSettings();
    void toggleAlwaysOnTop();

private:
    struct LiveMonitor {
        MonitorBase *monitor;
        QWidget     *widget;
    };

    void addMonitor(MonitorBase *m);
    void buildBuiltins(const QStringList &enabledIds, bool clockOnly);
    void buildPanelStack(const QStringList &enabledIds);
    void clearPanelStack();
    void rebuildPanels();
    void applyFixedWidth();
    void applyFrameMargins();
    void applySettingsOverridesToTheme();
    void restorePosition();
    void persistPosition();

    QStringList   m_cliEnabledIds;             // remembered from constructor
    Theme         *m_theme;
    PluginLoader  *m_pluginLoader = nullptr;
    QVBoxLayout   *m_layout       = nullptr;
    QList<LiveMonitor> m_monitors;

    bool   m_dragging   = false;
    QPoint m_dragOffset;

    Q_DISABLE_COPY_MOVE(MainWindow)
};
