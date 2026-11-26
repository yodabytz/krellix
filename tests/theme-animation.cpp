#include "theme/ThemeAnimation.h"
#include "widgets/Krell.h"

#include <QApplication>
#include <QDir>
#include <QPainter>
#include <QTemporaryDir>
#include <QThread>

namespace {
void check(bool yes, const char *message)
{
    if (!yes) qFatal("%s", message);
}

template<typename Ready>
bool until(Ready ready, int ms = 1000)
{
    QElapsedTimer clock;
    clock.start();
    do {
        QApplication::processEvents();
        if (ready()) return true;
        QThread::msleep(5);
    } while (clock.elapsed() < ms);
    return false;
}

void write(const QString &path, const QByteArray &content)
{
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(content) == content.size(), "Cannot write fixture");
}

QColor center(const QPixmap &pixmap)
{
    const auto image = pixmap.toImage();
    return image.isNull() ? QColor() : image.pixelColor(image.rect().center());
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir scratch;
    const auto oldPath = QDir::currentPath();
    check(scratch.isValid() && QDir::setCurrent(scratch.path()), "Cannot isolate fixture");
    const QString dir = QStringLiteral("themes/animation-fixture/");
    check(QDir().mkpath(dir), "Cannot create fixture theme");
    write(dir + QStringLiteral("theme.json"),
          R"({"images":{"frame_left":"static.png"},"surfaces":{"panel_bg":{"image":"static.png","slice":2}}})");
    QImage fallback(12, 10, QImage::Format_ARGB32);
    fallback.fill(Qt::yellow);
    check(fallback.save(dir + QStringLiteral("static.png")), "Cannot save static art");
    QImage sheet(36, 10, QImage::Format_ARGB32);
    sheet.fill(Qt::transparent);
    {
        QPainter painter(&sheet);
        painter.fillRect(0, 0, 12, 10, Qt::red);
        painter.fillRect(12, 0, 12, 10, Qt::green);
        painter.fillRect(24, 0, 12, 10, Qt::blue);
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        painter.fillRect(0, 0, 1, 1, Qt::black);
    }
    check(sheet.save(dir + QStringLiteral("sheet.png")), "Cannot save animation art");
    const auto valid = QByteArrayLiteral(R"({"sheet":"sheet.png","fallback":"static.png","frame_width":12,
        "frame_height":10,"frames":3,"fps":20,"layout":"horizontal","loop":true,"trigger":"continuous","target":"%1"})");
    const auto frameLeft = QStringLiteral("images.frame_left");
    const auto body = QStringLiteral("surfaces.panel_bg");
    auto json = [](QByteArray entry, const QByteArray &target) { return entry.replace("%1", target); };
    QByteArray broken = json(valid, "images.frame_right");
    broken.replace("\"sheet.png\"", "\"../animation-fixture/sheet.png\"");
    QByteArray wrongSize = json(valid, "images.frame_bottom");
    wrongSize.replace("\"frames\":3", "\"frames\":4");
    QByteArray badRate = json(valid, "images.krell");
    badRate.replace("\"fps\":20", "\"fps\":0");
    write(dir + QStringLiteral("animation.json"), "{\"version\":1,\"animations\":{\"left\":"
          + json(valid, "images.frame_left") + ",\"body\":" + json(valid, "surfaces.panel_bg")
          + ",\"unsafe\":" + broken + ",\"size\":" + wrongSize + ",\"fps\":" + badRate + "}}");

    QWidget window;
    window.resize(30, 30);
    window.show();
    Theme theme;
    const auto name = QStringLiteral("animation-fixture");
    check(theme.load(name), "Cannot load fixture");
    check(theme.hasAnimation(frameLeft) && theme.hasAnimation(body), "Valid effects were ignored");
    check(!theme.hasAnimation(QStringLiteral("images.frame_right"))
          && !theme.hasAnimation(QStringLiteral("images.frame_bottom"))
          && !theme.hasAnimation(QStringLiteral("images.krell")), "Invalid manifest entries were accepted");
    const auto clock = theme.findChild<QTimer *>(QStringLiteral("theme-frame-clock"));
    check(clock && !clock->isActive(), "Unpainted theme started a timer");
    auto frame = [&] { return theme.animationFrame(frameLeft, &window, QRect(0, 0, 12, 10), QSize(12, 10)); };
    auto first = frame();
    check(first.size() == QSize(12, 10) && center(first) == Qt::red
          && first.toImage().pixelColor(0, 0).alpha() == 0, "Wrong frame boundaries or alpha");
    check(until([&] { return center(frame()) == Qt::green; }), "Frame never advanced");
    check(until([&] { return center(frame()) == Qt::red; }), "Loop did not wrap");
    check(theme.surface(QStringLiteral("panel_bg")).slice == 2
          && center(theme.surface(QStringLiteral("panel_bg")).image) == Qt::yellow,
          "Static surface geometry uses animated assets");
    check(center(theme.paintedSurface(QStringLiteral("panel_bg"), {}, &window).image) == Qt::red,
          "Painting did not extract a frame before slicing");
    write(dir + QStringLiteral("theme.json"),
          R"({"images":{"frame_left":"static.png"},"surfaces":{"panel_bg":{"image":"static.png",
                "mode":"vertical-caps","cap_top":3,"cap_bottom":2}}})");
    check(theme.load(name), "Cannot load capped animation fixture");
    Theme::SurfaceGeometry caps;
    const auto animated = theme.paintedSurface(QStringLiteral("panel_bg"), {}, &window, caps, QRect(5, 6, 16, 12));
    check(caps.mode == Theme::SurfaceGeometry::VerticalCaps && caps.source == QMargins(0, 3, 0, 2)
          && center(animated.image) == Qt::red, "Cap geometry did not survive frame selection");
    check(QFile::remove(dir + QStringLiteral("sheet.png")), "Cannot remove original fixture sheet");
    check(until([&] { return center(frame()) == Qt::blue; }), "Painting reread the atlas");
    window.hide();
    check(until([&] { return !clock->isActive(); }), "Hidden window did not pause");
    window.show();
    frame();
    window.setWindowState(Qt::WindowMinimized);
    check(until([&] { return !clock->isActive(); }), "Minimized window did not pause");
    window.setWindowState(Qt::WindowNoState);
    window.show();
    theme.setMotion(Theme::Motion::Reduced);
    frame();
    check(clock->isActive() && clock->interval() == 125, "Reduced motion did not slow the clock");
    theme.setMotion(Theme::Motion::Off);
    check(!clock->isActive() && frame().isNull()
          && center(theme.paintedSurface(QStringLiteral("panel_bg"), {}, &window).image) == Qt::yellow,
          "Off did not restore static artwork");
    check(theme.load(name) && !theme.hasAnimation(frameLeft)
          && !theme.findChild<QTimer *>(QStringLiteral("theme-frame-clock")), "Theme reload retained old frame caches");

    theme.setMetric(QStringLiteral("krell_height"), 12);
    Krell krell(&theme);
    check(krell.sizeHint().height() == 8, "Plain Krell is not one third shorter");
    const auto krellDir = QStringLiteral("themes/slim-krell-fixture/");
    check(QDir().mkpath(krellDir), "Cannot create themed Krell fixture");
    check(fallback.save(krellDir + QStringLiteral("static.png"))
        && sheet.save(krellDir + QStringLiteral("sheet.png")), "Cannot save themed Krell art");
    QImage track(2, 10, QImage::Format_RGB32);
    track.fill(Qt::black);
    {
        QPainter painter(&track);
        painter.fillRect(0, 7, 2, 3, Qt::blue);
    }
    check(track.save(krellDir + QStringLiteral("track.png")), "Cannot save Krell track");
    write(krellDir + QStringLiteral("theme.json"),
          R"({"images":{"krell":"static.png","krell_track":"track.png"}})");
    write(krellDir + QStringLiteral("animation.json"),
          "{\"version\":1,\"animations\":{\"indicator\":" + json(valid, "images.krell") + "}}");
    check(theme.load(QStringLiteral("slim-krell-fixture")), "Cannot load slim Krell fixture");
    check(krell.sizeHint().height() == 7, "Themed Krell is not one third shorter");
    krell.setFixedSize(60, krell.sizeHint().height());
    krell.show();
    krell.setValue(1);
    const auto image = krell.grab().toImage();
    check(image.pixelColor(54, 3) == Qt::yellow && image.pixelColor(6, 3) != Qt::yellow,
          "The new height changed the indicator's horizontal position");
    check(image.pixelColor(24, 6) == Qt::blue, "The shortened track cropped its bottom");
    theme.setMotion(Theme::Motion::Full);
    check(until([&] { return krell.grab().toImage().pixelColor(54, 3) == Qt::green; }),
          "The shorter indicator lost themed animation or shifted position");

    ThemePhase a, b;
    a.advance(2, 16, 48, 1.8, 2);
    b.advance(1, 16, 48, 1.8, 2);
    b.advance(1, 16, 48, 1.8, 2);
    check(qAbs(a.position - b.position) < 0.00001 && a.speed > 1 && a.speed < 1.8,
          "CPU smoothing is not elapsed-time based");
    check(a.advance(600, 16, 48, 1., 0) < 48, "Long elapsed time overflowed loop");
    check(QDir::setCurrent(oldPath), "Cannot restore working directory");
    qInfo("PASS: theme manifest bounds, shared clock, alpha, pause, fallback and CPU phase");
}
