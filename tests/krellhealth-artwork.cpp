#include "krellhealth-artwork-fixture.h"
#include "monitors/MonitorBase.h"
#include "remote/RemoteSource.h"
#include "sdk/KrellixPlugin.h"
#include "theme/Theme.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QPluginLoader>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <memory>

namespace {
void require(bool ok, const char *message) { if (!ok) qFatal("%s", message); }

template<class Predicate>
bool until(Predicate predicate)
{
    QElapsedTimer time;
    time.start();
    do {
        QApplication::processEvents();
        if (predicate()) return true;
        QThread::msleep(5);
    } while (time.elapsed() < 3000);
    return false;
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir scratch;
    require(argc == 2 && scratch.isValid() && QDir::setCurrent(scratch.path()), "Bad test setup");
    QCoreApplication::setOrganizationName(QStringLiteral("krellhealth-artwork-test"));
    QCoreApplication::setApplicationName(QStringLiteral("isolated"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, scratch.path());
    QSettings().setValue(QStringLiteral("plugins/krellhealth/enabled"), true);
    makeHealthThemes();

    QPluginLoader loader(QString::fromLocal8Bit(argv[1]));
    auto *plugin = qobject_cast<IKrellixPlugin *>(loader.instance());
    if (!plugin) qFatal("%s", qPrintable(loader.errorString()));

    RemoteSource source;
    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost), "Could not open health fixture");
    int samples = 0;
    QObject::connect(&source, &RemoteSource::sampleReceived, &app, [&] { ++samples; });
    source.connectToHost(QStringLiteral("127.0.0.1"), server.serverPort());
    require(until([&] { return source.isConnected() && server.hasPendingConnections(); }), "No fixture connection");
    auto *peer = server.nextPendingConnection();
    auto send = [&](const QByteArray &observation) {
        const int previous = samples;
        peer->write(observation + '\n');
        require(until([&] { return samples > previous; }), "No telemetry sample");
    };
    send(R"({"type":"sample","health":{"critical":0,"warnings":0,"findings":[]}})");

    Theme theme;
    require(theme.load(QStringLiteral("health-test-plain")), "Theme not found");
    QObject root;
    const auto monitors = plugin->createMonitors(&theme, &root);
    require(monitors.size() == 1, "KrellHealth is not enabled");
    auto *monitor = monitors.first();
    std::unique_ptr<QWidget> widget(monitor->createWidget(nullptr));
    monitor->tick();
    widget->setFixedWidth(120);
    widget->show();
    QApplication::processEvents();
    require(widget->toolTip().contains(QStringLiteral("No active warnings")), "Fixture not healthy");
    const auto fallback = widget->grab().toImage();
    const auto hint = widget->sizeHint();
    require(theme.load(QStringLiteral("health-test-custom")), "Could not switch to custom theme");
    const auto art = healthArtworkBounds(widget.get());
    require(!art.isEmpty() && qAbs(art.width() - art.height() * 2) <= 2, "Themed heart was absent or distorted");
    require(widget->sizeHint() == hint, "The themed heart resized the panel");
    require(theme.load(QStringLiteral("health-test-missing")), "Could not switch to missing-art test");
    require(widget->grab().toImage() == fallback, "Missing artwork changed the default rendering");
    require(theme.load(QStringLiteral("health-test-custom")), "Could not restore the custom theme");
    send(R"({"type":"sample","health":{"warnings":1,"findings":[{"severity":"warning","title":"Fixture warning","detail":"Full detail kept"}]}})");
    monitor->tick();
    require(healthArtworkBounds(widget.get()).isEmpty(), "Warning showed the healthy artwork");
    require(widget->toolTip().contains(QStringLiteral("Full detail kept")), "Warning lost detail");
    qInfo("KrellHealth: themed and fallback heart, live switching, layout and warning checks passed");
}
