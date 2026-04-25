#include "MainWindow.h"

#include "krellix/PluginLoader.h"
#include "krellix/SettingsDialog.h"
#include "monitors/ClockMonitor.h"
#include "monitors/CpuMonitor.h"
#include "monitors/DiskMonitor.h"
#include "monitors/HostMonitor.h"
#include "monitors/MemMonitor.h"
#include "monitors/MonitorBase.h"
#include "monitors/NetMonitor.h"
#include "monitors/UptimeMonitor.h"
#include "theme/Theme.h"
#include "widgets/Panel.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QLayoutItem>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#ifndef KRELLIX_VERSION
#  define KRELLIX_VERSION "0.0.0"
#endif

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

    // Initial discovery + monitor instantiation.
    (void) m_pluginLoader->discoverAndLoad(m_theme, this);
    buildPanelStack(m_cliEnabledIds);

    applyFrameMargins();
    applyFixedWidth();
    restorePosition();
    connect(m_theme, &Theme::themeChanged, this, &MainWindow::onThemeChanged);
}

MainWindow::~MainWindow() = default;

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

    m_monitors.append(LiveMonitor{m, w});
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

    if (enabled(QStringLiteral("host")))
        addMonitor(new HostMonitor(m_theme, this));
    if (enabled(QStringLiteral("cpu")))
        addMonitor(new CpuMonitor(m_theme, this));
    if (enabled(QStringLiteral("mem")))
        addMonitor(new MemMonitor(m_theme, this));
    if (enabled(QStringLiteral("uptime")))
        addMonitor(new UptimeMonitor(m_theme, this));
    if (enabled(QStringLiteral("net")))
        addMonitor(new NetMonitor(m_theme, this));
    if (enabled(QStringLiteral("disk")))
        addMonitor(new DiskMonitor(m_theme, this));
}

void MainWindow::buildPanelStack(const QStringList &enabledIds)
{
    const bool clockAtTop =
        QSettings().value(QStringLiteral("window/clock_at_top"), true).toBool();

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
        if (enabled(QStringLiteral("cpu")))
            addMonitor(new CpuMonitor(m_theme, this));
        if (enabled(QStringLiteral("mem")))
            addMonitor(new MemMonitor(m_theme, this));
        if (enabled(QStringLiteral("uptime")))
            addMonitor(new UptimeMonitor(m_theme, this));
        if (enabled(QStringLiteral("net")))
            addMonitor(new NetMonitor(m_theme, this));
        if (enabled(QStringLiteral("disk")))
            addMonitor(new DiskMonitor(m_theme, this));
    } else {
        // Clock at the bottom (legacy ordering).
        buildBuiltins(enabledIds, /*clockOnly=*/false);
    }

    // Plugin monitors: re-instantiate from cached IKrellixPlugin list.
    const QList<MonitorBase *> pluginMonitors =
        m_pluginLoader->createMonitorsForAll(m_theme, this);
    for (MonitorBase *m : pluginMonitors) addMonitor(m);

    if (!clockAtTop)
        buildBuiltins(enabledIds, /*clockOnly=*/true);

    m_layout->addStretch(1);
}

static void deleteLayoutContents(QLayout *layout)
{
    if (!layout) return;
    while (layout->count() > 0) {
        QLayoutItem *item = layout->takeAt(0);
        if (!item) break;
        if (QWidget *w = item->widget()) {
            w->setParent(nullptr);
            w->deleteLater();
        } else if (QLayout *sub = item->layout()) {
            deleteLayoutContents(sub);
            sub->deleteLater();
        }
        delete item;
    }
}

void MainWindow::clearPanelStack()
{
    // Tear down per-monitor widgets and the monitors themselves. Each
    // monitor parents its QTimer, so stopping/deleting the monitor stops
    // the tick.
    for (const LiveMonitor &lm : m_monitors) {
        if (lm.widget) {
            lm.widget->setParent(nullptr);
            lm.widget->deleteLater();
        }
        if (lm.monitor) {
            lm.monitor->deleteLater();
        }
    }
    m_monitors.clear();

    // Drop the trailing stretch too — buildPanelStack re-adds it.
    deleteLayoutContents(m_layout);
}

void MainWindow::rebuildPanels()
{
    clearPanelStack();
    buildPanelStack(m_cliEnabledIds);
    applyFrameMargins();
    applyFixedWidth();
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
    update();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    persistPosition();
    QWidget::closeEvent(event);
}

void MainWindow::paintEvent(QPaintEvent *)
{
    // Window-frame compositing: stretch frame_top/bottom horizontally and
    // frame_left/right vertically across the appropriate edge band. Their
    // alpha channel forms the visible silhouette of the window. If a theme
    // doesn't supply frames, paintEvent does nothing and the panels paint
    // themselves directly on the (translucent) window background.
    QPainter p(this);

    const QPixmap topPix    = m_theme->pixmap(QStringLiteral("frame_top"));
    const QPixmap bottomPix = m_theme->pixmap(QStringLiteral("frame_bottom"));
    const QPixmap leftPix   = m_theme->pixmap(QStringLiteral("frame_left"));
    const QPixmap rightPix  = m_theme->pixmap(QStringLiteral("frame_right"));

    const int topH    = topPix.isNull()    ? 0 : topPix.height();
    const int bottomH = bottomPix.isNull() ? 0 : bottomPix.height();
    const int leftW   = leftPix.isNull()   ? 0 : leftPix.width();
    const int rightW  = rightPix.isNull()  ? 0 : rightPix.width();

    if (!topPix.isNull())
        p.drawPixmap(QRect(0, 0, width(), topH), topPix);
    if (!bottomPix.isNull())
        p.drawPixmap(QRect(0, height() - bottomH, width(), bottomH), bottomPix);

    const int sideTop    = topH;
    const int sideBottom = height() - bottomH;
    if (sideBottom > sideTop) {
        if (!leftPix.isNull())
            p.drawPixmap(QRect(0, sideTop, leftW, sideBottom - sideTop), leftPix);
        if (!rightPix.isNull())
            p.drawPixmap(QRect(width() - rightW, sideTop, rightW, sideBottom - sideTop),
                         rightPix);
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
                    rebuildPanels();
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
                applySettingsOverridesToTheme();
                applyFixedWidth();
                rebuildPanels();
            });

    dlg->open();
}
