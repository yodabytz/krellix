#pragma once

#include <QList>
#include <QPoint>
#include <QStringList>
#include <QWidget>

class AlertBanner;
class MonitorBase;
class PluginLoader;
class RemoteSource;
class Theme;
class QVBoxLayout;
class QMouseEvent;
class QContextMenuEvent;
class QCloseEvent;
class QPaintEvent;
class QTimer;

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

    // Hook the connection-lost banner up to a RemoteSource. The banner
    // is added to the top of the layout the first time this is called.
    // Safe to skip when running in local mode (no banner ever shown).
    void attachRemoteSource(RemoteSource *remote);

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
    void cycleThemeForward();
    void cycleThemeBackward();

private:
    struct LiveMonitor {
        MonitorBase *monitor;
        QWidget     *widget;
        QTimer      *timer;
    };

    void addMonitor(MonitorBase *m);
    void cycleTheme(int direction);
    void buildBuiltins(const QStringList &enabledIds, bool clockOnly);
    void buildPanelStack(const QStringList &enabledIds);
    void clearPanelStack();
    void rebuildPanels();
    void refreshLiveSettings();
    void refreshMonitorTimers();
    void applyFixedWidth();
    void applyFrameMargins();
    void fitToPanelStack();
    void applySettingsOverridesToTheme();
    void restorePosition();
    void persistPosition();
    void applyTopStripFromTheme();

    QStringList   m_cliEnabledIds;             // remembered from constructor
    Theme         *m_theme;
    PluginLoader  *m_pluginLoader = nullptr;
    QVBoxLayout   *m_layout       = nullptr;
    AlertBanner   *m_alertBanner  = nullptr;
    QTimer        *m_alertDebounce = nullptr;
    RemoteSource  *m_remote       = nullptr;
    class Panel   *m_topStrip     = nullptr;   // optional decorative header
    QList<LiveMonitor> m_monitors;
    bool m_rebuildingPanels = false;

    bool   m_dragging   = false;
    QPoint m_dragOffset;

    Q_DISABLE_COPY_MOVE(MainWindow)
};
