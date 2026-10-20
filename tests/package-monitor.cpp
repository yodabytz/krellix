#include "KrellpkgPlugin.h"
#include "theme/Theme.h"
#include "widgets/Decal.h"
#include "remote/RemoteSource.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#include <QDebug>
class PackageMonitorTest {
public:
 static void run(Theme &theme, const QString &bin) {
    KrellpkgMonitor monitor(&theme);
    QWidget *panel = monitor.createWidget(nullptr);
    QApplication::processEvents();
    if (monitor.m_summary->text() != QStringLiteral("Select package systems")) qFatal("Empty selection reported up-to-date");
    QSettings().setValue(QStringLiteral("plugins/krellpkg/systems/brew"), true);
    auto check = [&](const QByteArray &script, const QString &expected) {
        QFile brew(bin + QStringLiteral("/brew"));
        if (!brew.open(QIODevice::WriteOnly | QIODevice::Truncate)) qFatal("fixture write failed");
        brew.write("#!/bin/sh\n" + script); brew.close();
        brew.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        monitor.tick();
        QElapsedTimer wait; wait.start();
        while (monitor.m_updating && wait.elapsed() < 7000) {
            QApplication::processEvents(); QThread::msleep(10);
        }
        if (monitor.m_updating || monitor.m_summary->text() != expected)
            qFatal("Unexpected package state: %s", qPrintable(monitor.m_summary->text()));
    };
    check("sleep 3\nprintf 'openssl\\ncmake\\n'\nprintf 'warning from stderr\\n' >&2\n", QStringLiteral("Homebrew 2"));
    check("exit 1\n", QStringLiteral("Check failed"));
    check("exit 0\n", QStringLiteral("Homebrew 0"));
    QFile::remove(bin + QStringLiteral("/brew"));
#ifndef Q_OS_MACOS
    monitor.tick();
    QElapsedTimer wait; wait.start();
    while (monitor.m_updating && wait.elapsed() < 2000) { QApplication::processEvents(); QThread::msleep(10); }
    if (monitor.m_summary->text() != QStringLiteral("Check failed")) qFatal("Missing command reported success");
#endif
    delete panel;
 }
};
int main(int argc, char **argv) {
 QApplication app(argc, argv);
 QTemporaryDir tmp;
 QSettings::setDefaultFormat(QSettings::IniFormat);
 QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp.path());
 QCoreApplication::setOrganizationName(QStringLiteral("krellix-test"));
 QCoreApplication::setApplicationName(QStringLiteral("package-test"));
 qputenv("PATH", tmp.path().toUtf8() + ":/usr/bin:/bin");
 QSettings().setValue(QStringLiteral("plugins/krellpkg/systems/brew"), false);
 Theme theme;
 KrellpkgPlugin plugin;
 if (!plugin.createMonitors(&theme, &app).isEmpty()) qFatal("Enabled by default");
 QSettings().setValue(QStringLiteral("plugins/krellpkg/enabled"), true);
 { RemoteSource remote; if (!plugin.createMonitors(&theme, &app).isEmpty()) qFatal("Local packages leaked into remote"); }
 PackageMonitorTest::run(theme, tmp.path());
 qInfo("PASS: slow package check, stderr exclusion, failed/empty/missing checks, no selection and remote isolation");
}
