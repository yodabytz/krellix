#include "MainWindow.h"

#include "krellix/PluginLoader.h"
#include "monitors/ClockMonitor.h"
#include "monitors/CpuMonitor.h"
#include "monitors/HostMonitor.h"
#include "monitors/MemMonitor.h"
#include "monitors/MonitorBase.h"
#include "theme/Theme.h"
#include "widgets/Panel.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QSettings>
#include <QSizeGrip>
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
    , m_theme(theme)
    , m_pluginLoader(new PluginLoader(this))
{
    Q_ASSERT(m_theme);

    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
    setWindowTitle(QStringLiteral("krellix"));
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    // Built-in monitors except Clock first.
    buildBuiltins(enabledMonitorIds, /*clockOnly=*/false);

    // Plugin monitors next.
    const QList<MonitorBase *> pluginMonitors =
        m_pluginLoader->discoverAndLoad(m_theme, this);
    for (MonitorBase *m : pluginMonitors) addMonitor(m);

    // Clock pinned to the bottom of the monitor stack.
    buildBuiltins(enabledMonitorIds, /*clockOnly=*/true);

    m_layout->addStretch(1);

    // Resize grip in the bottom-right corner.
    auto *gripRow = new QHBoxLayout;
    gripRow->setContentsMargins(0, 0, 0, 0);
    gripRow->addStretch(1);
    gripRow->addWidget(new QSizeGrip(this), 0, Qt::AlignRight | Qt::AlignBottom);
    m_layout->addLayout(gripRow);

    applyMinimumWidth();
    restoreGeometry();
    connect(m_theme, &Theme::themeChanged, this, &MainWindow::onThemeChanged);
}

MainWindow::~MainWindow() = default;

void MainWindow::addMonitor(MonitorBase *m)
{
    Q_ASSERT(m);
    if (!m->parent()) m->setParent(this);
    m_monitors.append(m);

    QWidget *w = m->createWidget(this);
    if (w) m_layout->addWidget(w);

    auto *timer = new QTimer(m);
    timer->setTimerType(Qt::CoarseTimer);
    timer->setInterval(m->tickIntervalMs());
    connect(timer, &QTimer::timeout, m, &MonitorBase::tick);
    timer->start();
}

void MainWindow::buildBuiltins(const QStringList &enabledIds, bool clockOnly)
{
    auto enabled = [&enabledIds](const QString &id) {
        return enabledIds.isEmpty() || enabledIds.contains(id);
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
}

void MainWindow::applyMinimumWidth()
{
    const int w = m_theme->metric(QStringLiteral("panel_min_width"), 200);
    setMinimumWidth(w);
    // No setMaximumWidth — user can resize via the QSizeGrip.
}

void MainWindow::restoreGeometry()
{
    QSettings s;
    s.beginGroup(QStringLiteral("window"));
    const QSize savedSize = s.value(QStringLiteral("size")).toSize();
    const QPoint savedPos = s.value(QStringLiteral("pos")).toPoint();
    s.endGroup();

    if (savedSize.isValid() && savedSize.width() >= minimumWidth()
        && savedSize.height() > 0) {
        resize(savedSize);
    }
    if (!savedPos.isNull()) {
        move(savedPos);
    }
}

void MainWindow::persistGeometry()
{
    QSettings s;
    s.beginGroup(QStringLiteral("window"));
    s.setValue(QStringLiteral("size"), size());
    s.setValue(QStringLiteral("pos"),  pos());
    s.endGroup();
}

void MainWindow::onThemeChanged()
{
    applyMinimumWidth();
    update();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    persistGeometry();
    QWidget::closeEvent(event);
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

    QAction *aotA = menu.addAction(QStringLiteral("Always on top"));
    aotA->setCheckable(true);
    aotA->setChecked(windowFlags().testFlag(Qt::WindowStaysOnTopHint));
    connect(aotA, &QAction::triggered, this, &MainWindow::toggleAlwaysOnTop);

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
    Qt::WindowFlags f = windowFlags();
    f.setFlag(Qt::WindowStaysOnTopHint, !f.testFlag(Qt::WindowStaysOnTopHint));
    setWindowFlags(f);
    show();
}

void MainWindow::showAbout()
{
    QMessageBox::about(this,
        QStringLiteral("About krellix"),
        QStringLiteral("krellix %1\n\nA themeable Qt 6 system monitor in the spirit of GKrellM.")
        .arg(QString::fromUtf8(KRELLIX_VERSION)));
}
