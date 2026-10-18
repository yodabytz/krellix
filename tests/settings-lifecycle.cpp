#define main krellixMain
#include "../src/krellix/main.cpp"
#undef main
#include "krellix/SettingsDialog.h"
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTimer>
#include <QPointer>
#include <QTemporaryDir>

static int completed = 0;
static void exercise()
{
    MainWindow *window = nullptr;
    for (QWidget *widget : QApplication::topLevelWidgets())
        if (auto *candidate = qobject_cast<MainWindow *>(widget)) window = candidate;
    if (!window) qFatal("Main window missing");
    if (completed == 3) {
        if (!window->isVisible()) qFatal("Main window disappeared");
        window->close(); // Explicit main-window close must still exit.
        return;
    }
    QMetaObject::invokeMethod(window, "showSettings", Qt::DirectConnection);
    auto *dialog = window->findChild<SettingsDialog *>();
    if (!dialog) qFatal("Settings missing");
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
    QTemporaryDir config;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    const int result = krellixMain(argc, argv);
    if (completed != 3) {
        qCritical("FAIL: application exited after closing settings (%d/3 completed)", completed);
        return 1;
    }
    qInfo("PASS: window close, Escape, Close button, and explicit app close");
    return result;
}
