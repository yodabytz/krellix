#include "MainWindow.h"

#include "monitors/ClockMonitor.h"
#include "monitors/CpuMonitor.h"
#include "monitors/HostMonitor.h"
#include "monitors/MemMonitor.h"
#include "monitors/MonitorBase.h"
#include "theme/Theme.h"
#include "widgets/Panel.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
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
{
    Q_ASSERT(m_theme);

    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
    setWindowTitle(QStringLiteral("krellix"));
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    buildMonitors(enabledMonitorIds);
    m_layout->addStretch(1);

    applyMinimumWidth();
    connect(m_theme, &Theme::themeChanged, this, &MainWindow::onThemeChanged);
}

MainWindow::~MainWindow() = default;

void MainWindow::buildMonitors(const QStringList &enabledIds)
{
    auto build = [this](MonitorBase *m) {
        Q_ASSERT(m);
        m_monitors.append(m);
        QWidget *w = m->createWidget(this);
        if (w) m_layout->addWidget(w);

        auto *timer = new QTimer(m);
        timer->setTimerType(Qt::CoarseTimer);
        timer->setInterval(m->tickIntervalMs());
        connect(timer, &QTimer::timeout, m, &MonitorBase::tick);
        timer->start();
    };

    auto enabled = [&enabledIds](const QString &id) {
        return enabledIds.isEmpty() || enabledIds.contains(id);
    };

    if (enabled(QStringLiteral("host")))
        build(new HostMonitor(m_theme, this));
    if (enabled(QStringLiteral("cpu")))
        build(new CpuMonitor(m_theme, this));
    if (enabled(QStringLiteral("mem")))
        build(new MemMonitor(m_theme, this));
    if (enabled(QStringLiteral("clock")))
        build(new ClockMonitor(m_theme, this));
}

void MainWindow::applyMinimumWidth()
{
    const int w = m_theme->metric(QStringLiteral("panel_min_width"), 200);
    setMinimumWidth(w);
    setMaximumWidth(w);
}

void MainWindow::onThemeChanged()
{
    applyMinimumWidth();
    update();
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
