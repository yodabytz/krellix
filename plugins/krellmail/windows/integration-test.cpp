#include "monitors/MonitorBase.h"
#include "remote/RemoteSource.h"
#include "sdk/KrellixPlugin.h"
#include "theme/Theme.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QPluginLoader>
#include <QSettings>
#include <QSslSocket>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QThread>

namespace {
void check(bool result, const char *message)
{
    if (!result) qFatal("%s", message);
}

bool waitForText(QWidget *widget, const QString &text)
{
    QElapsedTimer timeout;
    timeout.start();
    do {
        QApplication::processEvents();
        for (const auto *label : widget->findChildren<QLabel *>()) {
            if (label->text().contains(text) || label->toolTip().contains(text)) return true;
        }
        QThread::msleep(5);
    } while (timeout.elapsed() < 6000);
    return false;
}

enum class Response { Imap, Pop, Rejected, Oversized };
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir profile;
    check(profile.isValid(), "Cannot create isolated settings directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QCoreApplication::setOrganizationName(QStringLiteral("krellmail-test"));
    QCoreApplication::setApplicationName(QStringLiteral("local"));
    check(QSslSocket::supportsSsl(), "TLS backend unavailable");
    check(argc == 2, "Missing KrellMail DLL path");

    QPluginLoader loader(QString::fromLocal8Bit(argv[1]));
    auto *plugin = qobject_cast<IKrellixPlugin *>(loader.instance());
    if (!plugin) qFatal("Cannot load KrellMail: %s", qPrintable(loader.errorString()));
    Theme theme;
    QObject owner;
    check(plugin->createMonitors(&theme, &owner).isEmpty(), "Disabled plugin created a monitor");

    QSettings settings;
    settings.setValue(QStringLiteral("plugins/krellmail/enabled"), true);
    settings.setValue(QStringLiteral("plugins/krellmail/account_count"), 0);
    const auto testMonitor = [&](const QString &expected) {
        const auto monitors = plugin->createMonitors(&theme, &owner);
        check(monitors.size() == 1, "Expected one local monitor");
        auto *monitor = monitors.front();
        auto *widget = monitor->createWidget(nullptr);
        check(waitForText(widget, expected), "Unexpected mail status");
        monitor->tick();
        monitor->shutdown();
        delete widget;
        delete monitor;
        QApplication::processEvents();
    };
    testMonitor(QStringLiteral("configure account"));

    QTcpServer server;
    check(server.listen(QHostAddress::LocalHost, 0), "Cannot listen for fixture mail requests");
    Response response = Response::Imap;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (server.hasPendingConnections()) {
            auto *socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            if (response == Response::Rejected) {
                socket->write("* BAD fixture rejected\r\n");
                continue;
            }
            if (response == Response::Oversized) {
                socket->write(QByteArray(300000, 'x'));
                continue;
            }
            const bool pop = response == Response::Pop;
            socket->write(pop ? "+OK fixture\r\n" : "* OK fixture\r\n");
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, pop] {
                while (socket->canReadLine()) {
                    const QByteArray line = socket->readLine();
                    if (pop) {
                        socket->write(line.startsWith("STAT") ? "+OK 3 100\r\n" : "+OK\r\n");
                    } else {
                        if (line.contains("SEARCH")) socket->write("* SEARCH 1 2 3\r\n");
                        socket->write(line.left(line.indexOf(' ')) + " OK done\r\n");
                    }
                }
            });
        }
    });
    settings.setValue(QStringLiteral("plugins/krellmail/account_count"), 1);
    settings.beginGroup(QStringLiteral("plugins/krellmail/account1"));
    settings.setValue(QStringLiteral("host"), QStringLiteral("127.0.0.1"));
    settings.setValue(QStringLiteral("port"), server.serverPort());
    settings.setValue(QStringLiteral("ssl"), false);
    settings.setValue(QStringLiteral("username"), QStringLiteral("fixture"));
    settings.setValue(QStringLiteral("password"), QStringLiteral("fixture"));
    settings.endGroup();
    for (const auto mode : {Response::Pop, Response::Imap, Response::Rejected, Response::Oversized}) {
        response = mode;
        settings.setValue(QStringLiteral("plugins/krellmail/account1/protocol"),
                          mode == Response::Pop ? QStringLiteral("pop3") : QStringLiteral("imap"));
        testMonitor(mode == Response::Rejected ? QStringLiteral("fixture rejected")
                    : mode == Response::Oversized ? QStringLiteral("too large")
                    : QStringLiteral("3 messages"));
    }

    RemoteSource remote;
    check(plugin->createMonitors(&theme, &owner).isEmpty(), "Remote instance created a local mail monitor");
    qInfo("PASS: KrellMail DLL, local POP/IMAP, errors, active shutdown and remote isolation");
}
