#include "monitors/MonitorBase.h"
#include "sdk/KrellixPlugin.h"
#include "theme/Theme.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPluginLoader>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>
#include <QWidget>
#include <cstdio>
#include <memory>

namespace {
void require(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        qFatal("%s", message);
    }
}

template<typename Predicate>
bool until(Predicate done, int timeoutMs = 2500)
{
    QElapsedTimer elapsed;
    elapsed.start();
    do {
        QApplication::processEvents();
        if (done()) return true;
        QThread::msleep(5);
    } while (elapsed.elapsed() < timeoutMs);
    return false;
}

void writeTheme(const QString &root, const QByteArray &manifest)
{
    require(QDir().mkpath(root), "Cannot create test theme");
    QFile file(root + QStringLiteral("/theme.json"));
    require(file.open(QIODevice::WriteOnly) && file.write(manifest) == manifest.size(), "Cannot write test theme");
}

void makeTheme(const QString &name, Qt::GlobalColor color, int frames, int iconSize = 20)
{
    const QString root = QStringLiteral("themes/") + name;
    writeTheme(root, QByteArrayLiteral(R"({"metrics":{"panel_icon_krellmail_size":)") + QByteArray::number(iconSize)
                   + QByteArrayLiteral(R"(},"images":{"panel_icon_krellmail":"idle.png","panel_icon_krellmail_unread":{"image":"unread.png","frames":)")
                   + QByteArray::number(frames) + QByteArrayLiteral("}}}"));
    QImage icon(8, 16, QImage::Format_ARGB32);
    icon.fill(color);
    require(icon.save(root + QStringLiteral("/idle.png")), "Cannot write idle icon");
    QImage sprite(48, 16, QImage::Format_ARGB32);
    {
        QPainter painter(&sprite);
        painter.fillRect(0, 0, 16, 16, Qt::red);
        painter.fillRect(16, 0, 16, 16, Qt::yellow);
        painter.fillRect(32, 0, 16, 16, Qt::cyan);
    }
    require(sprite.save(root + QStringLiteral("/unread.png")), "Cannot write sprite");
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    require(argc == 2, "Plugin path required");
    QTemporaryDir scratch;
    require(scratch.isValid() && QDir::setCurrent(scratch.path()), "Cannot isolate test");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, scratch.path());
    QCoreApplication::setOrganizationName(QStringLiteral("krellmail-icon-test"));
    QCoreApplication::setApplicationName(QStringLiteral("isolated"));
    QSettings settings;
    settings.setValue(QStringLiteral("plugins/krellmail/enabled"), true);
    settings.setValue(QStringLiteral("plugins/krellmail/account_count"), 0);
    makeTheme(QStringLiteral("gallery-mail-themed"), Qt::green, 3, 28);
    makeTheme(QStringLiteral("gallery-mail-invalid"), Qt::blue, 5);
    writeTheme(QStringLiteral("themes/gallery-mail-plain"),
               QByteArrayLiteral(R"({"metrics":{"panel_icon_krellmail_size":40}})"));

    QPluginLoader loader(QString::fromLocal8Bit(argv[1]));
    auto *plugin = qobject_cast<IKrellixPlugin *>(loader.instance());
    if (!plugin) {
        const QByteArray error = loader.errorString().toLocal8Bit();
        require(false, error.constData());
    }
    Theme theme;
    require(theme.load(QStringLiteral("gallery-mail-plain")), "Cannot load plain theme");
    const auto monitors = plugin->createMonitors(&theme, nullptr);
    require(monitors.size() == 1, "KrellMail monitor missing");
    std::unique_ptr<MonitorBase> monitor(monitors.first());
    std::unique_ptr<QWidget> panel(monitor->createWidget(nullptr));
    panel->setFixedWidth(120);
    panel->show();
    auto *icon = panel->findChild<QWidget *>(QStringLiteral("krellmail-icon"));
    require(icon, "KrellMail icon widget missing");
    require(icon->size() == QSize(28, 20), "Default envelope unexpectedly resized");
    const auto pixels = [icon] { return icon->grab().toImage(); };
    const auto centerColor = [&] {
        const QImage image = pixels();
        return image.pixelColor(image.rect().center());
    };

    const QImage defaultIdle = pixels();
    require(theme.load(QStringLiteral("gallery-mail-themed")), "Cannot load themed icon");
    require(icon->size() == QSize(28, 28), "Themed icon did not grow on reload");
    require(centerColor() == Qt::green, "Idle icon did not override the default");
    require(pixels().pixelColor(2, pixels().height() / 2) != Qt::green, "Icon aspect ratio was stretched");

    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost), "Cannot start local POP3 fixture");
    int mailCount = 2;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (server.hasPendingConnections()) {
            auto *socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            socket->write("+OK test\r\n");
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                while (socket->canReadLine()) {
                    const QByteArray line = socket->readLine();
                    socket->write(line.startsWith("STAT") ? "+OK " + QByteArray::number(mailCount) + " 96\r\n" : "+OK\r\n");
                }
            });
        }
    });
    settings.setValue(QStringLiteral("plugins/krellmail/account_count"), 1);
    settings.beginGroup(QStringLiteral("plugins/krellmail/account1"));
    settings.setValue(QStringLiteral("host"), QStringLiteral("127.0.0.1"));
    settings.setValue(QStringLiteral("port"), server.serverPort());
    settings.setValue(QStringLiteral("protocol"), QStringLiteral("pop3"));
    settings.setValue(QStringLiteral("ssl"), false);
    settings.setValue(QStringLiteral("username"), QStringLiteral("fixture"));
    settings.setValue(QStringLiteral("password"), QStringLiteral("fixture"));
    settings.endGroup();
    monitor->tick();
    require(until([&] { return centerColor() == Qt::red; }), "Unread sprite never appeared");
    require(until([&] { return centerColor() == Qt::cyan; }), "Unread sprite did not animate");

    auto *status = panel->findChild<QLabel *>(QStringLiteral("krellmail-status"));
    auto *count = panel->findChild<QLabel *>(QStringLiteral("krellmail-count"));
    require(status && count, "Status or count is missing");
    require(!panel->findChild<QToolButton *>(), "Settings must not be in the monitor panel");
    require(status->text() == QLatin1String("You Have Mail") && count->text() == QLatin1String("2 messages"),
            "Unread wording or count is wrong");
    for (int width : {120, 240}) {
        panel->setFixedWidth(width);
        panel->adjustSize();
        QApplication::processEvents();
        require(status->width() >= status->fontMetrics().horizontalAdvance(status->text()),
                "Unread status clips at a supported panel width");
        require(status->mapTo(panel.get(), {}).x() > icon->mapTo(panel.get(), {}).x() + icon->width()
                && count->y() > status->geometry().bottom(), "Original mail layout changed");
    }
    const auto configureStyle = [&](const QString &key) {
        settings.setValue(QStringLiteral("plugins/krellmail/icon_style"), key);
        monitor->tick();
    };
    configureStyle(QStringLiteral("envelope"));
    const auto closed = pixels();
    require(until([&] { return pixels() != closed; }), "Bundled Envelope did not animate");
    configureStyle(QStringLiteral("tux"));
    const auto tuxVisible = [&] {
        const auto img = pixels();
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                const QColor c = img.pixelColor(x, y);
                if (c.red() > c.blue() + 65 && c.green() > c.blue() + 40)
                    return true;
            }
        }
        return false;
    };
    require(until(tuxVisible), "Bundled Tux artwork did not appear");
    configureStyle(QStringLiteral("theme"));
    require(until([&] { return centerColor() == Qt::cyan; }), "Follow Theme did not restore the theme sprite");

    require(theme.load(QStringLiteral("gallery-mail-invalid")), "Cannot load invalid-sprite fixture");
    require(icon->size() == QSize(28, 20), "Theme without a size override did not restore the default size");
    require(centerColor() == Qt::blue, "Invalid sprite did not fall back to the themed static icon");
    require(theme.load(QStringLiteral("gallery-mail-themed")), "Cannot restore themed icon");
    require(until([&] { return centerColor() == Qt::yellow; }), "Live theme switch did not restore animation");
    mailCount = 0;
    monitor->tick();
    require(until([&] {
        QApplication::processEvents();
        return centerColor() == Qt::green;
    }), "Zero unread did not bring back the static icon");
    require(!until([&] { return centerColor() != Qt::green; }, 200), "Animation kept running after zero unread");
    require(theme.load(QStringLiteral("gallery-mail-plain")), "Cannot switch back to the plain theme");
    require(pixels() == defaultIdle, "Plain theme did not restore the existing default icon");
    monitor->shutdown();
    qInfo("PASS: narrow layout, You Have Mail, saved Envelope/Tux/Theme choices, themed size, animation and fallback");
}
