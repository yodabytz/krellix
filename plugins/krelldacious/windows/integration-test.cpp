#include "monitors/MonitorBase.h"
#include "remote/RemoteSource.h"
#include "sdk/KrellixPlugin.h"
#include "theme/Theme.h"
#include "widgets/Decal.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QPluginLoader>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

namespace {

void check(bool condition, const char *message)
{
    if (!condition) qFatal("%s", message);
}

template<typename Condition>
void waitFor(MonitorBase *monitor, Condition condition, const char *message)
{
    QElapsedTimer elapsed;
    elapsed.start();
    do {
        monitor->tick();
        for (int i = 0; i < 16; ++i) {
            QApplication::processEvents();
            if (condition()) return;
            QThread::msleep(15);
        }
    } while (elapsed.elapsed() < 18000);
    qFatal("%s", message);
}

QByteArray audtool(const QString &bin, const QStringList &args)
{
    QProcess command;
    command.setWorkingDirectory(bin);
    command.start(bin + QStringLiteral("/audtool.exe"), QStringList{QStringLiteral("-9")} + args);
    check(command.waitForFinished(5000) && command.exitCode() == 0, "Native fixture audtool command failed");
    return command.readAllStandardOutput().trimmed();
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    check(argc == 5, "Pass DLL, Audacious bin directory and two test WAV files");
    QTemporaryDir profile;
    check(profile.isValid(), "Cannot isolate test settings");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    app.setOrganizationName(QStringLiteral("krelldacious-test"));
    app.setApplicationName(QStringLiteral("windows"));
    QSettings().setValue(QStringLiteral("plugins/krelldacious/windows_instance"), 9);

    QPluginLoader loader(QString::fromLocal8Bit(argv[1]));
    auto *plugin = qobject_cast<IKrellixPlugin *>(loader.instance());
    if (!plugin) qFatal("Cannot load Windows plugin: %s", qPrintable(loader.errorString()));
    Theme theme;
    QObject owner;
    check(plugin->createMonitors(&theme, &owner).isEmpty(), "Disabled plugin created a monitor");
    QSettings().setValue(QStringLiteral("plugins/krelldacious/enabled"), true);
    const auto monitors = plugin->createMonitors(&theme, &owner);
    check(monitors.size() == 1, "Local Windows monitor missing");
    auto *monitor = monitors.front();
    QWidget *panel = monitor->createWidget(nullptr);
    panel->resize(130, 110);
    panel->show();

    QPushButton *play = nullptr;
    QPushButton *previous = nullptr;
    QPushButton *next = nullptr;
    for (auto *button : panel->findChildren<QPushButton *>()) {
        if (button->accessibleName() == QStringLiteral("Play / pause")) play = button;
        if (button->accessibleName() == QStringLiteral("Previous")) previous = button;
        if (button->accessibleName() == QStringLiteral("Next")) next = button;
    }
    auto *volume = panel->findChild<QSlider *>();
    QLabel *open = nullptr;
    for (auto *label : panel->findChildren<QLabel *>())
        if (label->text().contains(QStringLiteral("Open Audacious"))) open = label;
    check(play && previous && next && volume && open, "Published panel controls missing");
    waitFor(monitor, [&] { return !open->isHidden(); }, "Open Audacious link not offered");
    QMetaObject::invokeMethod(open, "linkActivated", Q_ARG(QString, QStringLiteral("open")));
    waitFor(monitor, [&] { return next->isEnabled(); }, "Open Audacious did not launch the installed player");

    const QString bin = QString::fromLocal8Bit(argv[2]);
    const auto fileUrl = [](const char *path) { return QUrl::fromLocalFile(QString::fromLocal8Bit(path)).toString(); };
    audtool(bin, {QStringLiteral("playlist-addurl"), fileUrl(argv[3]),
                  QStringLiteral("playlist-addurl"), fileUrl(argv[4])});
    const auto titleHas = [&](const QString &name) {
        for (auto *decal : panel->findChildren<Decal *>())
            if (decal->text().contains(name)) return true;
        return false;
    };
    const QString alpha = QStringLiteral("QA Alpha");
    const QString beta = QStringLiteral("QA Beta");
    play->click();
    waitFor(monitor, [&] { return play->property("playing").toBool() && titleHas(alpha); },
            "Play did not start or display the live Audacious track");
    play->click();
    waitFor(monitor, [&] { return !play->property("playing").toBool() && titleHas(alpha); }, "Pause failed");
    check(audtool(bin, {QStringLiteral("playback-status")}) == "paused", "Player did not actually pause");
    play->click();
    waitFor(monitor, [&] { return play->property("playing").toBool(); }, "Resume failed");
    next->click();
    waitFor(monitor, [&] { return titleHas(beta); }, "Next failed");
    previous->click();
    waitFor(monitor, [&] { return titleHas(alpha); }, "Previous failed");
    const int originalVolume = audtool(bin, {QStringLiteral("get-volume")}).toInt();
    const int testVolume = originalVolume == 37 ? 56 : 37;
    volume->setValue(testVolume);
    for (int i = 0; i < 70; ++i) {
        QApplication::processEvents();
        QThread::msleep(20);
    }
    const bool volumeChanged = audtool(bin, {QStringLiteral("get-volume")}).toInt() == testVolume;
    audtool(bin, {QStringLiteral("set-volume"), QString::number(originalVolume)});
    check(volumeChanged, "Slider did not update native Audacious volume");
    audtool(bin, {QStringLiteral("shutdown")});
    waitFor(monitor, [&] { return !next->isEnabled() && !open->isHidden(); }, "Player shutdown left stale controls");
    monitor->shutdown();
    delete panel;
    delete monitor;
    RemoteSource remote;
    check(plugin->createMonitors(&theme, &owner).isEmpty(), "Remote window exposed local player controls");
    qInfo("PASS: installed Audacious, plugin Open, play, pause, resume, next, previous, title, volume, shutdown and remote isolation");
}
