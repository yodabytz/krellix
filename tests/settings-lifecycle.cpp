#define main krellixMain
#include "../src/krellix/main.cpp"
#undef main
#include "krellix/SettingsDialog.h"
#include <QDialogButtonBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QPointer>
#include <QTemporaryDir>
#include "sysdep/SensorDisplay.h"
#include "monitors/DiskMonitor.h"
#include "widgets/Decal.h"
#include "widgets/Panel.h"

static void mouse(QWidget *target, QEvent::Type type, const QPoint &position,
                  Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, target->mapFromGlobal(position), position,
                      button, buttons, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
}

static void verifyDragHandle(Theme *theme)
{
    MainWindow test(theme, {QStringLiteral("host"), QStringLiteral("clock")});
    test.move(100, 100);
    test.show();
    QApplication::processEvents();
    auto *host = test.findChild<Panel *>(QStringLiteral("krellixHostnamePanel"));
    if (!host || !host->isVisible()) qFatal("Missing hostname panel drag handle");
    auto *hostLabel = host->findChild<Decal *>();
    if (!hostLabel) qFatal("Missing hostname label");
    const QPoint hostPos = hostLabel->mapToGlobal(hostLabel->rect().center());
    const QPoint original = test.pos();
    const QPoint delta(20, 15);
    for (Panel *panel : test.findChildren<Panel *>(QString(), Qt::FindDirectChildrenOnly)) {
        if (panel == host || !panel->isVisible()) continue;
        const QPoint from = panel->mapToGlobal(panel->rect().center());
        mouse(panel, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        mouse(panel, QEvent::MouseMove, hostPos, Qt::NoButton, Qt::LeftButton);
        mouse(panel, QEvent::MouseButtonRelease, hostPos, Qt::LeftButton, Qt::NoButton);
        if (test.pos() != original) qFatal("A drag starting outside the hostname moved the window");
    }
    mouse(hostLabel, QEvent::MouseButtonPress, hostPos, Qt::LeftButton, Qt::LeftButton);
    mouse(hostLabel, QEvent::MouseMove, hostPos + delta, Qt::NoButton, Qt::LeftButton);
    mouse(hostLabel, QEvent::MouseButtonRelease, hostPos + delta, Qt::LeftButton, Qt::NoButton);
    if (test.pos() != original + delta) qFatal("Dragging the hostname label did not move the window");
    mouse(hostLabel, QEvent::MouseMove, hostPos + delta * 2, Qt::NoButton, Qt::LeftButton);
    if (test.pos() != original + delta) qFatal("Drag remained active after release");
    qInfo("PASS: only the hostname panel can start a window drag");
}

static int completed = 0;
static void exercise()
{
    MainWindow *window = nullptr;
    for (QWidget *widget : QApplication::topLevelWidgets())
        if (auto *candidate = qobject_cast<MainWindow *>(widget)) window = candidate;
    if (!window) qFatal("Main window missing");
#ifdef Q_OS_MACOS
    if (!isMacAccessoryApplication()) qFatal("Mac monitor unexpectedly appears in Dock");
    if (window->windowType() != Qt::Window) qFatal("Main window must remain a normal Mac window");
#endif
    static bool diskChecked = false;
    if (!diskChecked) {
        diskChecked = true;
        auto *existing = window->findChild<MonitorBase *>();
        if (!existing) qFatal("No monitor for disk test theme");
        verifyDragHandle(existing->theme());
        DiskMonitor disk(existing->theme());
        DiskStat::setReadOverride([]() { return QList<DiskSample>{}; });
        QWidget *panel = disk.createWidget(nullptr);
        DiskStat::setReadOverride([]() { DiskSample s; s.name = QStringLiteral("vda"); s.sectorsRead = 100; return QList<DiskSample>{s}; });
        disk.tick();
        bool foundRate = false;
        for (auto *label : panel->findChildren<Decal *>()) {
            if (label->text().contains(QStringLiteral("no whole disks"))) qFatal("Disk panel stayed empty after remote sample");
            if (label->text().startsWith(QStringLiteral("R "))) foundRate = true;
        }
        if (!foundRate) qFatal("Remote disk panel never appeared");
        DiskStat::setReadOverride([]() { return QList<DiskSample>{}; });
        disk.tick();
        bool foundEmpty = false;
        for (auto *label : panel->findChildren<Decal *>())
            if (label->text().contains(QStringLiteral("no whole disks"))) foundEmpty = true;
        if (!foundEmpty) qFatal("Disconnected disk left stale panel");
        delete panel;
        DiskStat::setReadOverride(nullptr);
    }
    static bool moved = false;
    if (!moved) {
        moved = true;
        window->move(42, 73);
        QTimer::singleShot(400, qApp, exercise);
        return;
    }
    if (QSettings().value(QStringLiteral("window/pos")).toPoint() != QPoint(42, 73))
        qFatal("Window position was not saved before closing");
    if (completed == 3) {
        if (!window->isVisible()) qFatal("Main window disappeared");
        window->close(); // Explicit main-window close must still exit.
        return;
    }
    QMetaObject::invokeMethod(window, "showSettings", Qt::DirectConnection);
    auto *dialog = window->findChild<SettingsDialog *>();
    if (!dialog) qFatal("Settings missing");
    if (dialog->isModal() || dialog->windowModality() != Qt::NonModal)
        qFatal("Settings must not attach as a modal sheet");
#ifdef Q_OS_WIN
    if (completed == 0) {
        auto *monitor = window->findChild<MonitorBase *>();
        if (!monitor) qFatal("No monitor for scroll test theme");
        Decal label(monitor->theme(), QStringLiteral("label"), QStringLiteral("text_primary"));
        label.resize(32, 24);
        label.show();
        auto *timer = label.findChild<QTimer *>();
        label.setText(QStringLiteral("i"));
        if (!timer || timer->isActive()) qFatal("Text that fits should not scroll");
        label.setText(QStringLiteral("A long label that cannot fit in thirty-two pixels"));
        if (!timer->isActive()) qFatal("Overflowing text did not start scrolling");
        QSettings().setValue(QStringLiteral("appearance/scroll_fps"), 20);
        QMetaObject::invokeMethod(dialog, "settingsApplied", Qt::DirectConnection);
        QMetaObject::invokeMethod(&label, "onScrollTick", Qt::DirectConnection);
        if (timer->interval() != 50) qFatal("Live scroll settings were not applied");
        label.hide();
        if (timer->isActive()) qFatal("Hidden text should not keep scrolling");
    }
#endif
    const QPoint original = window->pos();
    dialog->move(dialog->pos() + QPoint(30, 40));
    if (window->pos() != original) qFatal("Moving settings moved the monitor");
    QPointer<SettingsDialog> guard(dialog);
    QTimer::singleShot(30, dialog, [dialog]() {
        if (completed == 0) dialog->close();
        else if (completed == 1) dialog->reject(); // Escape path
        else dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Close)->click();
    });
    QTimer::singleShot(100, qApp, [guard]() {
        if (guard) qFatal("Settings dialog was not destroyed");
        ++completed;
        exercise();
    });
}
static void scheduleTest() { QTimer::singleShot(100, exercise); }
Q_COREAPP_STARTUP_FUNCTION(scheduleTest)

int main(int argc, char **argv)
{
    SensorReading a, b, disk;
    a.chip = b.chip = QStringLiteral("coretemp");
    a.label = QStringLiteral("Core 0"); a.value = 50;
    b.label = QStringLiteral("Core 1"); b.value = 80; b.critC = 100;
    disk.chip = QStringLiteral("nvme"); disk.label = QStringLiteral("Composite"); disk.value = 40;
    const QList<SensorReading> input{a, b, disk};
    const auto compact = sensorDisplayReadings(input, false);
    if (compact.size() != 2 || compact[0].value != 80 || compact[0].critC != 100
        || compact[0].label != QStringLiteral("CPU") || compact[1].value != 40)
        qFatal("Compact sensors lost maximum temperature or mixed chip readings");
    if (sensorDisplayReadings(input, true).size() != 3
        || !sensorDisplayReadings({}, false).isEmpty()) qFatal("Detailed/empty sensor regression");
    QTemporaryDir config;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.path());
    const int result = krellixMain(argc, argv);
    if (completed != 3) {
        qCritical("FAIL: application exited after closing settings (%d/3 completed)", completed);
        return 1;
    }
    QSettings base(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("isolation-test"), QStringLiteral("krellix"));
    base.setValue(QStringLiteral("theme/name"), QStringLiteral("base"));
    base.sync();
    migrateLegacySettings(QStringLiteral("isolation-test"), QStringLiteral("krellix-host-a"), QStringLiteral("krellix"));
    QSettings first(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("isolation-test"), QStringLiteral("krellix-host-a"));
    first.setValue(QStringLiteral("theme/name"), QStringLiteral("custom-a"));
    first.sync();
    migrateLegacySettings(QStringLiteral("isolation-test"), QStringLiteral("krellix-host-b"), QStringLiteral("krellix"));
    migrateLegacySettings(QStringLiteral("isolation-test"), QStringLiteral("krellix-host-a"), QStringLiteral("krellix"));
    QSettings reopened(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("isolation-test"), QStringLiteral("krellix-host-a"));
    QSettings second(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("isolation-test"), QStringLiteral("krellix-host-b"));
    if (reopened.value(QStringLiteral("theme/name")).toString() != QStringLiteral("custom-a")
        || second.value(QStringLiteral("theme/name")).toString() != QStringLiteral("base")) qFatal("Instance settings leaked or were overwritten");
    qInfo("PASS: settings lifecycle and independent persistent instance settings");
    return result;
}
