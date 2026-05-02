#include "MainWindow.h"

#include "krellix/PluginLoader.h"
#include "krellix/SettingsDialog.h"
#include "remote/RemoteSource.h"
#include "monitors/BatteryMonitor.h"
#include "monitors/ClockMonitor.h"
#include "monitors/CpuMonitor.h"
#include "monitors/DiskMonitor.h"
#include "monitors/HostMonitor.h"
#include "monitors/MemMonitor.h"
#include "monitors/MonitorBase.h"
#include "monitors/NetMonitor.h"
#include "monitors/ProcMonitor.h"
#include "monitors/SensorsMonitor.h"
#include "monitors/UptimeMonitor.h"
#include "theme/Theme.h"
#include "widgets/AlertBanner.h"
#include "widgets/Panel.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QKeySequence>
#include <QLayoutItem>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QSettings>
#include <QSet>
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#ifndef KRELLIX_VERSION
#  define KRELLIX_VERSION "0.0.0"
#endif

namespace {

QStringList defaultMonitorOrder()
{
    return {
        QStringLiteral("host"),
        QStringLiteral("cpu"),
        QStringLiteral("proc"),
        QStringLiteral("mem"),
        QStringLiteral("uptime"),
        QStringLiteral("net"),
        QStringLiteral("krellkam"),
        QStringLiteral("krelldacious"),
        QStringLiteral("krellweather"),
        QStringLiteral("krellwire"),
        QStringLiteral("disk"),
        QStringLiteral("sensors"),
        QStringLiteral("battery"),
    };
}

QStringList configuredMonitorOrder()
{
    const QStringList defaults = defaultMonitorOrder();
    const QString raw = QSettings().value(QStringLiteral("monitors/order")).toString();
    QStringList out;
    for (const QString &id : raw.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString trimmed = id.trimmed();
        if ((defaults.contains(trimmed) || trimmed.startsWith(QStringLiteral("plugin:")))
            && !out.contains(trimmed))
            out.append(trimmed);
    }
    for (const QString &id : defaults) {
        if (!out.contains(id))
            out.append(id);
    }
    return out;
}

} // namespace

MainWindow::MainWindow(Theme *theme,
                       const QStringList &enabledMonitorIds,
                       QWidget *parent)
    : QWidget(parent)
    , m_cliEnabledIds(enabledMonitorIds)
    , m_theme(theme)
    , m_pluginLoader(new PluginLoader(this))
{
    Q_ASSERT(m_theme);

    Qt::WindowFlags flags = Qt::FramelessWindowHint | Qt::Tool;
    {
        QSettings s;
        if (s.value(QStringLiteral("window/always_on_top"), false).toBool())
            flags |= Qt::WindowStaysOnTopHint;
    }
    setWindowFlags(flags);
    setWindowTitle(QStringLiteral("krellix"));

    // Translucent main window: panels paint their own backgrounds, gaps are
    // see-through, alpha-channeled frame_* pixmaps shape the silhouette.
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground,    true);

    connect(qApp, &QCoreApplication::aboutToQuit,
            this, &MainWindow::persistPosition);

    applySettingsOverridesToTheme();

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    // Initial discovery loads plugin factories; the actual live monitor
    // instances are created by buildPanelStack() below so ordering and
    // enable/disable state are handled in one place.
    (void) m_pluginLoader->discoverAndLoad(m_theme, this);
    buildPanelStack(m_cliEnabledIds);

    applyFrameMargins();
    applyFixedWidth();
    fitToPanelStack();
    restorePosition();
    connect(m_theme, &Theme::themeChanged, this, &MainWindow::onThemeChanged);

    // Keybindings: T / Shift+T and Up / Down (with optional Shift) cycle
    // through installed themes (forward / backward). No rebuild on theme
    // switch, so chart history survives.
    const QKeySequence forwardKeys[] = {
        QKeySequence(QStringLiteral("T")),
        QKeySequence(Qt::Key_Up),
        QKeySequence(Qt::SHIFT | Qt::Key_Up),
    };
    const QKeySequence backwardKeys[] = {
        QKeySequence(QStringLiteral("Shift+T")),
        QKeySequence(Qt::Key_Down),
        QKeySequence(Qt::SHIFT | Qt::Key_Down),
    };
    for (const QKeySequence &k : forwardKeys) {
        auto *sc = new QShortcut(k, this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated,
                this, &MainWindow::cycleThemeForward);
    }
    for (const QKeySequence &k : backwardKeys) {
        auto *sc = new QShortcut(k, this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated,
                this, &MainWindow::cycleThemeBackward);
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::attachRemoteSource(RemoteSource *remote)
{
    if (!remote || m_remote == remote) return;
    m_remote = remote;

    if (!m_alertBanner) {
        m_alertBanner = new AlertBanner(this);
        // Insert at the very top of the panel stack — above hostname.
        m_layout->insertWidget(0, m_alertBanner);
    }
    if (!m_alertDebounce) {
        m_alertDebounce = new QTimer(this);
        m_alertDebounce->setSingleShot(true);
        m_alertDebounce->setInterval(5000);     // 5s grace before alarming
        connect(m_alertDebounce, &QTimer::timeout, this, [this]() {
            if (m_remote && !m_remote->isConnected() && m_alertBanner) {
                m_alertBanner->showAlert(
                    QStringLiteral("Connection lost: %1")
                        .arg(m_remote->remoteAddress()));
            }
        });
    }

    connect(m_remote, &RemoteSource::connectionStateChanged, this,
            [this](bool connected) {
                if (!m_alertBanner) return;
                if (connected) {
                    m_alertDebounce->stop();
                    m_alertBanner->hideAlert();
                } else {
                    // Debounce: a quick reconnect (< 5s) won't show the
                    // banner. Only sustained loss does.
                    m_alertDebounce->start();
                }
            });
}

void MainWindow::addMonitor(MonitorBase *m)
{
    Q_ASSERT(m);
    if (!m->parent()) m->setParent(this);

    QWidget *w = m->createWidget(this);
    if (w) m_layout->addWidget(w);

    auto *timer = new QTimer(m);
    timer->setTimerType(Qt::CoarseTimer);
    // Settings 'update/interval_ms' acts as a global lower bound (slowest
    // tick floor). Monitors that prefer slower (e.g. host wants 5000ms)
    // keep their preference; faster preferences are clamped to the floor.
    const int settingsMs = QSettings().value(
        QStringLiteral("update/interval_ms"), 1000).toInt();
    timer->setInterval(qMax(m->tickIntervalMs(), qMax(100, settingsMs)));
    connect(timer, &QTimer::timeout, m, &MonitorBase::tick);
    timer->start();

    m_monitors.append(LiveMonitor{m, w, timer});
}

void MainWindow::buildBuiltins(const QStringList &enabledIds, bool clockOnly)
{
    auto enabled = [&enabledIds](const QString &id) {
        if (!enabledIds.isEmpty()) return enabledIds.contains(id);
        return QSettings().value(QStringLiteral("monitors/") + id, true).toBool();
    };

    if (clockOnly) {
        if (enabled(QStringLiteral("clock")))
            addMonitor(new ClockMonitor(m_theme, this));
        return;
    }

    auto addById = [this](const QString &id) {
        if (id == QStringLiteral("host")) addMonitor(new HostMonitor(m_theme, this));
        else if (id == QStringLiteral("cpu")) addMonitor(new CpuMonitor(m_theme, this));
        else if (id == QStringLiteral("proc")) addMonitor(new ProcMonitor(m_theme, this));
        else if (id == QStringLiteral("mem")) addMonitor(new MemMonitor(m_theme, this));
        else if (id == QStringLiteral("uptime")) addMonitor(new UptimeMonitor(m_theme, this));
        else if (id == QStringLiteral("net")) addMonitor(new NetMonitor(m_theme, this));
        else if (id == QStringLiteral("disk")) addMonitor(new DiskMonitor(m_theme, this));
        else if (id == QStringLiteral("sensors")) addMonitor(new SensorsMonitor(m_theme, this));
        else if (id == QStringLiteral("battery")) addMonitor(new BatteryMonitor(m_theme, this));
    };

    for (const QString &id : configuredMonitorOrder()) {
        if (enabled(id)) addById(id);
    }
}

void MainWindow::buildPanelStack(const QStringList &enabledIds)
{
    // Optional decorative top strip — drawn ABOVE the first real
    // monitor when the active theme requests it. Themes set the
    // metric "top_strip_height" to a positive pixel value and define
    // a "panel_bg_top" surface; everything else uses the surface
    // image at that fixed height. Themes that don't set the metric
    // get no strip — this is opt-in per theme, not a global feature.
    //
    // The widget is always created so theme cycles (T / Up arrow) can
    // toggle visibility without rebuilding the panel stack. Initial
    // visibility / height is set from the active theme's metric here;
    // onThemeChanged() re-applies on every subsequent theme switch.
    if (!m_topStrip) {
        m_topStrip = new Panel(m_theme, this);
        m_topStrip->setSurfaceKey(QStringLiteral("panel_bg_top"));
        m_layout->addWidget(m_topStrip);
    }
    applyTopStripFromTheme();

    const bool clockAtTop =
        QSettings().value(QStringLiteral("window/clock_at_top"), true).toBool();
    const QList<MonitorBase *> pluginMonitors =
        m_pluginLoader->createMonitorsForAll(m_theme, this);
    QSet<MonitorBase *> addedPlugins;
    auto addPluginById = [this, &pluginMonitors, &addedPlugins](const QString &id) {
        for (MonitorBase *m : pluginMonitors) {
            if (m && m->id() == id && !addedPlugins.contains(m)) {
                addMonitor(m);
                addedPlugins.insert(m);
                return true;
            }
        }
        return false;
    };

    if (clockAtTop) {
        // GKrellM-style: clock right under host. host first, clock second,
        // then the rest. We achieve this by adding host alone, then clock,
        // then the remaining built-ins.
        const auto enabled = [&enabledIds](const QString &id) {
            if (!enabledIds.isEmpty()) return enabledIds.contains(id);
            return QSettings().value(QStringLiteral("monitors/") + id, true).toBool();
        };
        if (enabled(QStringLiteral("host")))
            addMonitor(new HostMonitor(m_theme, this));
        if (enabled(QStringLiteral("clock")))
            addMonitor(new ClockMonitor(m_theme, this));

        auto addById = [this](const QString &id) {
            if (id == QStringLiteral("cpu")) addMonitor(new CpuMonitor(m_theme, this));
            else if (id == QStringLiteral("proc")) addMonitor(new ProcMonitor(m_theme, this));
            else if (id == QStringLiteral("mem")) addMonitor(new MemMonitor(m_theme, this));
            else if (id == QStringLiteral("uptime")) addMonitor(new UptimeMonitor(m_theme, this));
            else if (id == QStringLiteral("net")) addMonitor(new NetMonitor(m_theme, this));
            else if (id == QStringLiteral("disk")) addMonitor(new DiskMonitor(m_theme, this));
            else if (id == QStringLiteral("sensors")) addMonitor(new SensorsMonitor(m_theme, this));
            else if (id == QStringLiteral("battery")) addMonitor(new BatteryMonitor(m_theme, this));
        };
        for (const QString &id : configuredMonitorOrder()) {
            if (addPluginById(id)) {
                continue;
            } else if (id.startsWith(QStringLiteral("plugin:"))
                       && addPluginById(id.mid(7))) {
                continue;
            } else if (id == QStringLiteral("krellkam")) {
                (void) addPluginById(id);
            } else if (id != QStringLiteral("host") && enabled(id)) {
                addById(id);
            }
        }
    } else {
        const auto enabled = [&enabledIds](const QString &id) {
            if (!enabledIds.isEmpty()) return enabledIds.contains(id);
            return QSettings().value(QStringLiteral("monitors/") + id, true).toBool();
        };
        auto addById = [this](const QString &id) {
            if (id == QStringLiteral("host")) addMonitor(new HostMonitor(m_theme, this));
            else if (id == QStringLiteral("cpu")) addMonitor(new CpuMonitor(m_theme, this));
            else if (id == QStringLiteral("proc")) addMonitor(new ProcMonitor(m_theme, this));
            else if (id == QStringLiteral("mem")) addMonitor(new MemMonitor(m_theme, this));
            else if (id == QStringLiteral("uptime")) addMonitor(new UptimeMonitor(m_theme, this));
            else if (id == QStringLiteral("net")) addMonitor(new NetMonitor(m_theme, this));
            else if (id == QStringLiteral("disk")) addMonitor(new DiskMonitor(m_theme, this));
            else if (id == QStringLiteral("sensors")) addMonitor(new SensorsMonitor(m_theme, this));
            else if (id == QStringLiteral("battery")) addMonitor(new BatteryMonitor(m_theme, this));
        };
        for (const QString &id : configuredMonitorOrder()) {
            if (addPluginById(id)) {
                continue;
            } else if (id.startsWith(QStringLiteral("plugin:"))
                       && addPluginById(id.mid(7))) {
                continue;
            } else if (id == QStringLiteral("krellkam")) {
                (void) addPluginById(id);
            } else if (enabled(id)) {
                addById(id);
            }
        }
    }

    if (!clockAtTop)
        buildBuiltins(enabledIds, /*clockOnly=*/true);

    for (MonitorBase *m : pluginMonitors) {
        if (m && !addedPlugins.contains(m)) {
            addMonitor(m);
            addedPlugins.insert(m);
        }
    }

    fitToPanelStack();
}

void MainWindow::clearPanelStack()
{
    // Tear down per-monitor widgets and the monitors themselves. Each
    // monitor parents its QTimer, so stopping/deleting the monitor stops
    // the tick. Leave non-monitor layout widgets such as the top strip and
    // connection alert in place; deleting them here leaves stale raw pointers
    // that can be touched by later theme or remote-source callbacks.
    for (const LiveMonitor &lm : m_monitors) {
        if (lm.timer) {
            lm.timer->stop();
            lm.timer->disconnect(this);
            lm.timer->disconnect(lm.monitor);
        }
        if (lm.monitor)
            lm.monitor->shutdown();
        if (lm.widget) {
            m_layout->removeWidget(lm.widget);
            lm.widget->setParent(nullptr);
            lm.widget->deleteLater();
        }
        if (lm.monitor) {
            lm.monitor->disconnect(this);
            lm.monitor->deleteLater();
        }
    }
    m_monitors.clear();
}

void MainWindow::rebuildPanels()
{
    if (m_rebuildingPanels)
        return;
    m_rebuildingPanels = true;
    clearPanelStack();
    buildPanelStack(m_cliEnabledIds);
    applyFrameMargins();
    applyFixedWidth();
    fitToPanelStack();
    update();
    m_rebuildingPanels = false;
}

void MainWindow::refreshMonitorTimers()
{
    const int settingsMs = QSettings().value(
        QStringLiteral("update/interval_ms"), 1000).toInt();
    for (const LiveMonitor &lm : m_monitors) {
        if (!lm.monitor || !lm.timer) continue;
        lm.timer->setInterval(qMax(lm.monitor->tickIntervalMs(),
                                   qMax(100, settingsMs)));
    }
}

void MainWindow::refreshLiveSettings()
{
    applySettingsOverridesToTheme();
    applyFixedWidth();
    refreshMonitorTimers();
    if (m_layout) {
        for (const LiveMonitor &lm : m_monitors) {
            if (lm.widget) {
                lm.widget->updateGeometry();
                lm.widget->update();
            }
            if (lm.monitor
                && (lm.monitor->id() == QStringLiteral("krellweather")
                    || lm.monitor->id() == QStringLiteral("krellwire")))
                lm.monitor->tick();
        }
        m_layout->invalidate();
    }
    fitToPanelStack();
    update();
}

void MainWindow::applyFixedWidth()
{
    // Window width is locked to the user's appearance/panel_width setting
    // (defaults to 100). Themes never override this — switching themes
    // keeps your chosen width. Resize handle is not provided; change the
    // value via Settings.
    const int w = qBound(80,
        QSettings().value(QStringLiteral("appearance/panel_width"), 100).toInt(),
        600);
    setFixedWidth(w);
}

void MainWindow::applyFrameMargins()
{
    if (!m_layout) return;
    const int top    = m_theme->pixmap(QStringLiteral("frame_top")).height();
    const int bottom = m_theme->pixmap(QStringLiteral("frame_bottom")).height();
    const int left   = m_theme->pixmap(QStringLiteral("frame_left")).width();
    const int right  = m_theme->pixmap(QStringLiteral("frame_right")).width();
    m_layout->setContentsMargins(left, top, right, bottom);
}

void MainWindow::fitToPanelStack()
{
    if (!m_layout) return;
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    m_layout->activate();
    updateGeometry();
    const int h = m_layout->sizeHint().height();
    if (h > 0) resize(width(), h);
}

void MainWindow::applySettingsOverridesToTheme()
{
    QSettings s;
    // Note: panel_width is NOT pushed into the theme — window width is
    // controlled by applyFixedWidth() reading the setting directly.
    // Krell/chart height overrides do go into the theme so widgets can
    // pick them up via the standard themeChanged signal path.
    if (s.contains(QStringLiteral("appearance/krell_height")))
        m_theme->setMetric(QStringLiteral("krell_height"),
                           s.value(QStringLiteral("appearance/krell_height")).toInt());
    if (s.contains(QStringLiteral("appearance/chart_height")))
        m_theme->setMetric(QStringLiteral("chart_height"),
                           s.value(QStringLiteral("appearance/chart_height")).toInt());
}

void MainWindow::restorePosition()
{
    // Width is fixed by setting; only the on-screen position is restored.
    QSettings s;
    const QPoint savedPos = s.value(QStringLiteral("window/pos")).toPoint();
    if (!savedPos.isNull()) move(savedPos);
}

void MainWindow::persistPosition()
{
    QSettings s;
    s.setValue(QStringLiteral("window/pos"), pos());
}

void MainWindow::onThemeChanged()
{
    applyFrameMargins();
    applyFixedWidth();
    applyTopStripFromTheme();
    fitToPanelStack();
    update();
}

void MainWindow::applyTopStripFromTheme()
{
    if (!m_topStrip) return;
    const int h = m_theme->metric(QStringLiteral("top_strip_height"), 0);
    if (h > 0) {
        m_topStrip->setFixedHeight(h);
        m_topStrip->setVisible(true);
    } else {
        // Theme doesn't want a top strip — collapse the widget so it
        // takes zero pixels. setVisible(false) is enough; the layout
        // skips hidden children.
        m_topStrip->setVisible(false);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    persistPosition();
    QWidget::closeEvent(event);
}

void MainWindow::paintEvent(QPaintEvent *)
{
    // Window-frame compositing: top + bottom frames stretch horizontally
    // (smoothly) to the window width; left + right frames TILE vertically
    // down the side rather than stretching, so a tall window doesn't blow
    // up the artwork into pixelated mush. Alpha channel of each pixmap
    // forms the visible silhouette of the window.
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QPixmap topPix    = m_theme->pixmap(QStringLiteral("frame_top"));
    const QPixmap bottomPix = m_theme->pixmap(QStringLiteral("frame_bottom"));
    const QPixmap leftPix   = m_theme->pixmap(QStringLiteral("frame_left"));
    const QPixmap rightPix  = m_theme->pixmap(QStringLiteral("frame_right"));

    const int topH    = topPix.isNull()    ? 0 : topPix.height();
    const int bottomH = bottomPix.isNull() ? 0 : bottomPix.height();
    const int leftW   = leftPix.isNull()   ? 0 : leftPix.width();
    const int rightW  = rightPix.isNull()  ? 0 : rightPix.width();

    p.fillRect(rect(), m_theme->color(QStringLiteral("panel_bg"),
                                      QColor(0, 0, 0)));

    if (!topPix.isNull())
        p.drawTiledPixmap(QRect(0, 0, width(), topH), topPix);
    if (!bottomPix.isNull())
        p.drawTiledPixmap(QRect(0, height() - bottomH, width(), bottomH), bottomPix);

    const int sideTop    = topH;
    const int sideBottom = height() - bottomH;
    if (sideBottom > sideTop) {
        if (!leftPix.isNull()) {
            p.drawTiledPixmap(QRect(0, sideTop, leftW, sideBottom - sideTop),
                              leftPix);
        }
        if (!rightPix.isNull()) {
            p.drawTiledPixmap(QRect(width() - rightW, sideTop, rightW,
                                    sideBottom - sideTop),
                              rightPix);
        }
    }
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (QWindow *wh = windowHandle()) {
            if (wh->startSystemMove()) {
                event->accept();
                return;
            }
        }
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void MainWindow::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);

    QAction *settingsA = menu.addAction(QStringLiteral("Settings..."));
    connect(settingsA, &QAction::triggered, this, &MainWindow::showSettings);

    QAction *aotA = menu.addAction(QStringLiteral("Always on top"));
    aotA->setCheckable(true);
    aotA->setChecked(windowFlags().testFlag(Qt::WindowStaysOnTopHint));
    connect(aotA, &QAction::triggered, this, &MainWindow::toggleAlwaysOnTop);

    QMenu *themeMenu = menu.addMenu(QStringLiteral("Theme"));
    const QStringList themes = Theme::availableThemes();
    const QString currentName = m_theme->name();
    if (themes.isEmpty()) {
        QAction *none = themeMenu->addAction(QStringLiteral("(no themes found)"));
        none->setEnabled(false);
    } else {
        for (const QString &name : themes) {
            QAction *a = themeMenu->addAction(name);
            a->setCheckable(true);
            a->setChecked(name == currentName);
            connect(a, &QAction::triggered, this, [this, name]() {
                if (m_theme->load(name)) {
                    QSettings s;
                    s.setValue(QStringLiteral("theme/name"), name);
                    applySettingsOverridesToTheme();
                    // No rebuildPanels() here — Theme::themeChanged fires
                    // and every Panel/Decal/Krell/Chart already repaints
                    // itself, so chart history and per-monitor state are
                    // preserved across theme switches.
                }
            });
        }
    }

    menu.addSeparator();

    QAction *aboutA = menu.addAction(QStringLiteral("About krellix"));
    connect(aboutA, &QAction::triggered, this, &MainWindow::showAbout);

    QAction *quitA = menu.addAction(QStringLiteral("Quit"));
    connect(quitA, &QAction::triggered, qApp, &QApplication::quit);

    menu.popup(event->globalPos());
    QEventLoop loop;
    QObject::connect(&menu, &QMenu::aboutToHide, &loop, &QEventLoop::quit);
    loop.exec();
}

void MainWindow::toggleAlwaysOnTop()
{
    const bool now = !windowFlags().testFlag(Qt::WindowStaysOnTopHint);
    Qt::WindowFlags f = windowFlags();
    f.setFlag(Qt::WindowStaysOnTopHint, now);
    setWindowFlags(f);
    show();

    QSettings s;
    s.setValue(QStringLiteral("window/always_on_top"), now);
}

void MainWindow::cycleThemeForward()  { cycleTheme(+1); }
void MainWindow::cycleThemeBackward() { cycleTheme(-1); }

void MainWindow::cycleTheme(int direction)
{
    const QStringList themes = Theme::availableThemes();
    if (themes.isEmpty()) return;

    const QString current = m_theme->name();
    int idx = themes.indexOf(current);
    if (idx < 0) idx = 0;
    const int n = themes.size();
    idx = ((idx + direction) % n + n) % n;
    const QString next = themes[idx];
    if (m_theme->load(next)) {
        QSettings().setValue(QStringLiteral("theme/name"), next);
        applySettingsOverridesToTheme();
    }
}

void MainWindow::showAbout()
{
    QMessageBox::about(this,
        QStringLiteral("About krellix"),
        QStringLiteral("krellix %1\n\nA themeable Qt 6 system monitor in the spirit of GKrellM.")
        .arg(QString::fromUtf8(KRELLIX_VERSION)));
}

void MainWindow::showSettings()
{
    auto *dlg = new SettingsDialog(m_theme, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    connect(dlg, &SettingsDialog::themeNameChanged, this,
            [this](const QString &name) {
                if (m_theme->load(name)) applySettingsOverridesToTheme();
            });
    connect(dlg, &SettingsDialog::alwaysOnTopChanged, this,
            [this](bool on) {
                Qt::WindowFlags f = windowFlags();
                f.setFlag(Qt::WindowStaysOnTopHint, on);
                setWindowFlags(f);
                show();
            });
    connect(dlg, &SettingsDialog::settingsApplied, this,
            [this]() {
                refreshLiveSettings();
            });
    connect(dlg, &SettingsDialog::panelStackChanged, this,
            [this]() {
                applySettingsOverridesToTheme();
                rebuildPanels();
            });

    dlg->open();
}
