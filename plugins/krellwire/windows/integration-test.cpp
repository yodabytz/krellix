#include "../KrellwirePlugin.h"
#include "theme/Theme.h"

#include <QApplication>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QWidget>

namespace {
class UrlSink : public QObject
{
    Q_OBJECT
public:
    QList<QUrl> opened;

public slots:
    void capture(const QUrl &url) { opened.append(url); }
};

template<class Test>
void waitFor(Test test, const char *failure)
{
    QElapsedTimer timer;
    timer.start();
    while (!test() && timer.elapsed() < 5000) {
        QCoreApplication::processEvents();
        QThread::msleep(20);
    }
    if (!test()) qFatal("%s", failure);
}

void mouse(QWidget *widget, QEvent::Type type, Qt::MouseButton button)
{
    const QPoint point = widget->rect().center();
    QMouseEvent event(type, point, widget->mapToGlobal(point), button,
                      type == QEvent::MouseButtonPress ? button : Qt::NoButton,
                      Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("wire-test"));
    app.setApplicationName(QStringLiteral("wire-test"));
    QTemporaryDir profile;
    if (!profile.isValid()) qFatal("No temporary settings directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());

    QTcpServer fixture;
    int requests = 0;
    if (!fixture.listen(QHostAddress::LocalHost)) qFatal("No fixture socket");
    QObject::connect(&fixture, &QTcpServer::newConnection, &fixture, [&] {
        while (auto *client = fixture.nextPendingConnection()) {
            QObject::connect(client, &QTcpSocket::disconnected, client, &QObject::deleteLater);
            QObject::connect(client, &QTcpSocket::readyRead, client, [client, &requests] {
                const QByteArray request = client->property("request").toByteArray() + client->readAll();
                if (!request.contains("\r\n\r\n")) {
                    client->setProperty("request", request);
                    return;
                }
                ++requests;
                const auto body = request.startsWith("GET /atom ")
                    ? QByteArray(R"(<feed xmlns="http://www.w3.org/2005/Atom"><entry>
                        <title>Atom fixture</title><link rel="alternate" href="/atom-article"/>
                        </entry></feed>)")
                    : QByteArray(R"(<rss version="2.0"><channel>
                        <item><title>A headline without a link</title></item>
                        <item><title>Blocked headline</title><link>file:///test</link></item>
                        <item><title>RSS fixture &amp; headline</title>
                        <link>https://headlines.invalid/story</link></item>
                        </channel></rss>)");
                client->write("HTTP/1.1 200 OK\r\nContent-Type: application/xml\r\nConnection: close\r\nContent-Length: "
                    + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                client->disconnectFromHost();
            });
        }
    });
    const auto root = QStringLiteral("http://127.0.0.1:%1").arg(fixture.serverPort());
    QSettings s;
    s.setValue(QStringLiteral("plugins/krellwire/feed1"), root + QStringLiteral("/rss"));
    s.setValue(QStringLiteral("plugins/krellwire/feed2"), QString());
    s.setValue(QStringLiteral("plugins/krellwire/feed3"), QString());
    s.setValue(QStringLiteral("plugins/krellwire/scroll_pps"), 160);
    s.setValue(QStringLiteral("appearance/scroll_interval_ms"), 33);
    Theme theme;
    QWidget window;
    KrellwireMonitor monitor(&theme);
    auto *panel = monitor.createWidget(&window);
    panel->resize(200, 40);
    window.resize(200, 40);
    QWidget *ticker = nullptr;
    for (auto *widget : panel->findChildren<QWidget *>()) {
        if (widget->accessibleName() == QStringLiteral("KrellWire headlines")) ticker = widget;
    }
    if (!ticker) qFatal("Ticker missing");
    window.show();

    waitFor([&] { return ticker->accessibleDescription() == QStringLiteral("RSS fixture & headline"); },
            "RSS parsing lost the valid item or accepted an unsafe link");
    UrlSink sink;
    QDesktopServices::setUrlHandler(QStringLiteral("https"), &sink, "capture");
    QElapsedTimer deadline;
    deadline.start();
    while (sink.opened.isEmpty() && deadline.elapsed() < 4500) {
        QCoreApplication::processEvents();
        mouse(ticker, QEvent::MouseButtonPress, Qt::RightButton);
        mouse(ticker, QEvent::MouseButtonRelease, Qt::RightButton);
        if (!sink.opened.isEmpty()) qFatal("Right-click opened a headline");
        mouse(ticker, QEvent::MouseButtonPress, Qt::LeftButton);
        if (!sink.opened.isEmpty()) qFatal("Press opened a headline before release");
        mouse(ticker, QEvent::MouseButtonRelease, Qt::LeftButton);
        QThread::msleep(20);
    }
    QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
    if (sink.opened != QList<QUrl>{QUrl(QStringLiteral("https://headlines.invalid/story"))})
        qFatal("Left-click did not open the expected headline");

    s.setValue(QStringLiteral("plugins/krellwire/height"), 32);
    emit theme.themeChanged();
    if (ticker->height() != 32) qFatal("Theme notification did not refresh the ticker");
    monitor.tick();
    if (requests != 1) qFatal("Live display preferences unnecessarily refetched a feed");
    s.setValue(QStringLiteral("plugins/krellwire/feed1"), root + QStringLiteral("/atom"));
    monitor.tick();
    waitFor([&] { return ticker->accessibleDescription() == QStringLiteral("Atom fixture"); },
            "A live feed change did not replace the RSS headlines with Atom");
    if (requests != 2) qFatal("Unexpected requests for the new feed");
    monitor.shutdown();
}

#include "integration-test.moc"
