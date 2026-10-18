#define main krellixMain
#include "../src/krellix/main.cpp"
#undef main
#include "krellix/SettingsDialog.h"
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTimer>
#include <QPointer>
#include <QTemporaryDir>
#include "sysdep/SensorDisplay.h"

static int completed = 0;
static void exercise()
{
    MainWindow *window = nullptr;
    for (QWidget *widget : QApplication::topLevelWidgets())
        if (auto *candidate = qobject_cast<MainWindow *>(widget)) window = candidate;
    if (!window) qFatal("Main window missing");
#ifdef Q_OS_MACOS
    if (window->windowType() != Qt::Window) qFatal("Main window must remain a normal Mac window");
#endif
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
        || compact[0].label != QStringLiteral("coretemp max") || compact[1].value != 40)
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
