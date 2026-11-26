#include "WeatherIcons.h"

#include <QPainter>
#include <QPainterPath>
#include <QTransform>
#include <QtMath>

#include <cmath>

namespace KrellWeather {
namespace {

QColor rgb(unsigned int value)
{
    return QColor::fromRgb(value);
}

qreal luminance(const QColor &color)
{
    const auto linear = [](qreal value) {
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
}

qreal contrast(const QColor &first, const QColor &second)
{
    const qreal a = luminance(first) + 0.05;
    const qreal b = luminance(second) + 0.05;
    return qMax(a, b) / qMin(a, b);
}

QColor readable(const QColor &foreground, const QColor &background)
{
    if (contrast(foreground, background) >= 3) return foreground;
    const auto light = rgb(0xf0f4f8);
    const auto dark = rgb(0x263238);
    return contrast(light, background) > contrast(dark, background) ? light : dark;
}

QColor blend(const QColor &first, const QColor &second, qreal amount)
{
    return QColor::fromRgbF(first.redF() + (second.redF() - first.redF()) * amount,
                           first.greenF() + (second.greenF() - first.greenF()) * amount,
                           first.blueF() + (second.blueF() - first.blueF()) * amount);
}

struct Ink {
    QColor edge, sun, moon, lightCloud, backCloud, overcast, storm, rain, rainEdge;
    QColor snow, snowEdge, lightning, wind, fog, ice, severe;
    bool mono;
};

Ink inks(Mode mode, const Colors &colors)
{
    const auto edge = readable(rgb(0x263238), colors.background);
    if (mode == Mode::Color)
        return {edge, rgb(0xffd02a), rgb(0xffd75a), rgb(0xf0f4f8), rgb(0xd5dde6),
                rgb(0xa4afbd), rgb(0x788594), rgb(0x39bdf2), readable(rgb(0x087dae), colors.background),
                rgb(0xf5faff), readable(rgb(0x397da6), colors.background), rgb(0xffd000),
                rgb(0x62b9e8), rgb(0xaab7c4), rgb(0x9dddf5), rgb(0xe53935), false};
    const auto primary = readable(colors.primary, colors.background);
    const auto secondary = readable(colors.secondary, colors.background);
    const auto accent = readable(colors.accent, colors.background);
    if (mode == Mode::Monochrome)
        return {primary, primary, primary, primary, primary, primary, primary, primary, primary,
                primary, primary, primary, primary, primary, primary, primary, true};
    return {edge, accent, blend(accent, primary, .3), primary, blend(primary, secondary, .45),
            secondary, blend(secondary, colors.background, .32), accent, edge, primary, edge,
            accent, accent, secondary, blend(primary, accent, .45), accent, false};
}

QPainterPath cloudPath()
{
    QPainterPath path;
    path.moveTo(15, 47);
    path.cubicTo(7, 47, 4, 41, 5, 35);
    path.cubicTo(6, 29, 11, 25, 18, 26);
    path.cubicTo(20, 17, 27, 12, 35, 13);
    path.cubicTo(44, 13, 50, 20, 51, 27);
    path.cubicTo(59, 27, 62, 32, 61, 38);
    path.cubicTo(61, 44, 56, 47, 50, 47);
    path.closeSubpath();
    return path;
}

QPainterPath crescentPath()
{
    QPainterPath outer, cutout;
    outer.addEllipse(QPointF(32, 32), 22, 22);
    cutout.addEllipse(QPointF(20, 22), 19, 19);
    return outer.subtracted(cutout);
}

QPainterPath at(const QPainterPath &path, QPointF center, qreal scale, QPointF anchor = QPointF(32, 32))
{
    QTransform transform;
    transform.translate(center.x(), center.y());
    transform.scale(scale, scale);
    transform.translate(-anchor.x(), -anchor.y());
    return transform.map(path);
}

class Drawing
{
public:
    Drawing(QPainter &painter, Ink palette, bool compact) : p(painter), c(palette), small(compact) {}

    void draw(Condition condition)
    {
        const auto kind = condition.kind;
        const bool night = condition.night;
        switch (kind) {
        case Kind::Clear:
            luminary(night, {32, 32}, 1);
            break;
        case Kind::MostlyClear:
            luminary(night, {26, 27}, .77);
            cloud({42, 51}, .60, c.lightCloud);
            break;
        case Kind::PartlyCloudy:
            luminary(night, {23, 23}, .68);
            cloud({37, 53}, .84, c.lightCloud);
            break;
        case Kind::MostlyCloudy:
            luminary(night, {45, 18}, .51);
            cloud({23, 39}, .66, c.backCloud);
            cloud({33, 53}, .94, c.lightCloud);
            break;
        case Kind::Cloudy:
            if (night) luminary(true, {43, 17}, .56);
            cloud({24, 39}, .66, c.backCloud);
            cloud({34, 51}, .92, c.lightCloud);
            break;
        case Kind::Overcast:
            cloud({24, 39}, .66, c.storm);
            cloud({34, 51}, .91, c.overcast);
            break;
        case Kind::Drizzle: case Kind::LightRain: case Kind::Rain: case Kind::HeavyRain: case Kind::RainShowers:
            weatherCloud(night, kind == Kind::HeavyRain ? c.overcast : c.lightCloud);
            rain(kind);
            break;
        case Kind::LightSnow: case Kind::Snow: case Kind::HeavySnow: case Kind::SnowShowers:
            weatherCloud(night, c.lightCloud);
            snow(kind);
            break;
        case Kind::Sleet: case Kind::Mixed: case Kind::FreezingRain:
            weatherCloud(night, c.overcast);
            drop({21, 49}, 1.03);
            if (kind == Kind::FreezingRain) {
                icicle({37, 43}, 1);
                icicle({49, 46}, .8);
            } else {
                flake({45, 51}, kind == Kind::Sleet ? 6 : 9);
                if (kind == Kind::Sleet && !small) pellet({32, 55}, 2.5);
            }
            break;
        case Kind::Hail:
            weatherCloud(night, c.storm);
            for (int i = 0; i < (small ? 3 : 4); ++i)
                pellet({14.0 + i * (small ? 17 : 12), i % 2 ? 54.0 : 47.0}, small ? 3.9 : 3.4);
            break;
        case Kind::BlowingSnow:
            weatherCloud(night, c.overcast);
            flake({19, 50}, 7);
            air(33, 51, .49);
            break;
        case Kind::Fog: case Kind::Mist:
            weatherCloud(night, c.backCloud);
            for (int i = 0; i < (kind == Kind::Mist ? 2 : 3); ++i)
                line({8.0 + 4 * i, 44.0 + 7 * i}, {56.0 - 4 * i, 44.0 + 7 * i}, c.fog, 3);
            break;
        case Kind::Haze:
            luminary(night, {32, 26}, .73);
            for (int i = 0; i < 2; ++i)
                line({8.0 + 4 * i, 48.0 + 8 * i}, {56.0 - 4 * i, 48.0 + 8 * i}, c.fog, 3);
            break;
        case Kind::Smoke:
            smoke();
            break;
        case Kind::Dust:
            air(8, 29, .86);
            pellet({16, 45}, 2.8);
            pellet({29, 50}, 2.5);
            if (!small) pellet({48, 43}, 2.2);
            break;
        case Kind::Wind: case Kind::StrongWind:
            air(7, 31, 1, kind == Kind::StrongWind);
            break;
        case Kind::Thunderstorm: case Kind::SevereThunderstorm:
            storm(night, condition.stormPrecipitation, kind == Kind::SevereThunderstorm);
            break;
        case Kind::Tornado:
            tornado();
            break;
        case Kind::Hurricane: case Kind::TropicalStorm:
            cyclone(kind == Kind::Hurricane);
            break;
        case Kind::Unknown:
            unknown();
            break;
        }
    }

private:
    void shape(const QPainterPath &path, const QColor &fill, const QColor &edge)
    {
        if (c.mono) {
            p.save();
            p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
            p.fillPath(path, Qt::black);
            p.restore();
        }
        p.setBrush(c.mono ? QBrush(Qt::NoBrush) : QBrush(fill));
        p.setPen(QPen(edge, 2.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
    }

    void stroke(const QPainterPath &path, const QColor &color, qreal width, const QColor &edge = {})
    {
        p.setBrush(Qt::NoBrush);
        if (!c.mono && edge.isValid()) {
            p.setPen(QPen(edge, width + 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(path);
        }
        p.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
    }

    void line(QPointF first, QPointF second, const QColor &color, qreal width = 2.8)
    {
        QPainterPath path(first);
        path.lineTo(second);
        stroke(path, color, width, c.edge);
    }

    void luminary(bool moon, QPointF center, qreal scale)
    {
        if (moon) {
            static const auto crescent = crescentPath();
            shape(at(crescent, center, scale), c.moon, c.edge);
            return;
        }
        QPainterPath rays;
        for (int i = 0; i < 8; ++i) {
            constexpr qreal eighthTurn = 0.7853981633974483;
            const QPointF direction(qCos(i * eighthTurn), qSin(i * eighthTurn));
            rays.moveTo(center + direction * (21 * scale));
            rays.lineTo(center + direction * (27 * scale));
        }
        stroke(rays, c.sun, scale < .6 ? 2.4 : 3.2, c.edge);
        QPainterPath solarDisk;
        solarDisk.addEllipse(center, 15 * scale, 15 * scale);
        shape(solarDisk, c.sun, c.edge);
    }

    void cloud(QPointF center, qreal scale, const QColor &color)
    {
        static const auto silhouette = cloudPath();
        shape(at(silhouette, center, scale, {32, 47}), color, c.edge);
    }

    void weatherCloud(bool night, const QColor &color)
    {
        if (night) luminary(true, {45, 16}, .51);
        cloud({32, 35}, .85, color);
    }

    void drop(QPointF center, qreal scale)
    {
        QPainterPath path;
        path.moveTo(2, -7);
        path.cubicTo(3, -2, 6, 1, 4, 4);
        path.cubicTo(2, 8, -5, 7, -5, 2);
        path.cubicTo(-5, -1, 0, -4, 2, -7);
        path.closeSubpath();
        shape(at(path, center, scale, {}), c.rain, c.rainEdge);
    }

    void rain(Kind kind)
    {
        if (kind == Kind::HeavyRain) {
            const int count = small ? 3 : 5;
            for (int i = 0; i < count; ++i) {
                const qreal x = 12 + i * (count == 3 ? 18 : 10);
                line({x + 4, 42}, {x - 1, 57}, c.rain, 3.2);
            }
        } else if (kind == Kind::Drizzle) {
            for (int i = 0; i < 3; ++i) drop({17.0 + i * 15, 46.0 + (i % 2) * 6}, .52);
        } else if (kind == Kind::LightRain) {
            drop({23, 46}, .92);
            drop({42, 54}, .92);
        } else {
            drop({17, 48}, 1);
            drop({33, kind == Kind::RainShowers ? 55.0 : 50.0}, 1);
            drop({49, 48}, 1);
        }
    }

    void flake(QPointF center, qreal radius)
    {
        QPainterPath arms;
        for (int i = 0; i < 6; ++i) {
            constexpr qreal sixthTurn = 1.0471975511965976;
            const QPointF d(qCos(i * sixthTurn), qSin(i * sixthTurn));
            const QPointF normal(-d.y(), d.x());
            const QPointF end = center + d * radius;
            arms.moveTo(center);
            arms.lineTo(end);
            if (!small && radius >= 7) {
                arms.moveTo(end - d * 4 + normal * 2.7);
                arms.lineTo(end - d * 2);
                arms.lineTo(end - d * 4 - normal * 2.7);
            }
        }
        stroke(arms, c.snow, 2.65, c.snowEdge);
    }

    void snow(Kind kind)
    {
        if (kind == Kind::LightSnow) {
            flake({32, 49}, 10);
        } else if (kind == Kind::HeavySnow && !small) {
            flake({15, 46}, 7.4);
            flake({32, 53}, 7.4);
            flake({49, 46}, 7.4);
        } else {
            flake({21, 49}, 8);
            flake({44, kind == Kind::SnowShowers ? 53.0 : 49.0}, 8);
        }
    }

    void pellet(QPointF center, qreal radius)
    {
        QPainterPath circle;
        circle.addEllipse(center, radius, radius);
        shape(circle, c.ice, c.snowEdge);
    }

    void icicle(QPointF position, qreal scale)
    {
        QPainterPath path;
        path.moveTo(-4, 0);
        path.lineTo(4, 0);
        path.lineTo(0, 15);
        path.closeSubpath();
        shape(at(path, position, scale, {}), c.ice, c.snowEdge);
    }

    void air(qreal x, qreal y, qreal scale, bool strong = false)
    {
        QPainterPath path;
        path.moveTo(0, -8);
        path.lineTo(33, -8);
        path.cubicTo(44, -8, 43, -22, 34, -20);
        path.moveTo(3, 3);
        path.lineTo(46, 3);
        path.cubicTo(56, 3, 57, 17, 47, 17);
        if (strong) {
            path.moveTo(4, 14);
            path.lineTo(30, 14);
            path.cubicTo(38, 14, 39, 26, 31, 26);
        }
        stroke(at(path, {x, y}, scale, {}), c.wind, 3.8, c.edge);
    }

    void smoke()
    {
        QPainterPath path;
        for (const qreal x : {17.0, 33.0, 49.0}) {
            path.moveTo(x - 3, 57);
            path.cubicTo(x + 13, 44, x - 11, 33, x, 24);
            path.cubicTo(x + 8, 17, x + 5, 11, x + 1, 7);
        }
        stroke(path, c.fog, 3.5, c.edge);
    }

    void storm(bool night, bool precipitation, bool severe)
    {
        weatherCloud(night, c.storm);
        if (precipitation) {
            drop({13, 50}, .74);
            drop({52, 50}, .74);
        }
        QPainterPath bolt;
        bolt.moveTo(30, 28);
        bolt.lineTo(43, 28);
        bolt.lineTo(34, 40);
        bolt.lineTo(44, 40);
        bolt.lineTo(23, 61);
        bolt.lineTo(29, 46);
        bolt.lineTo(20, 46);
        bolt.closeSubpath();
        shape(bolt, c.lightning, severe ? c.severe : c.edge);
    }

    void tornado()
    {
        QPainterPath path;
        path.moveTo(6, 14);
        path.cubicTo(18, 7, 46, 7, 58, 14);
        path.cubicTo(64, 20, 16, 28, 13, 31);
        path.cubicTo(11, 35, 46, 35, 45, 40);
        path.cubicTo(44, 44, 19, 45, 21, 49);
        path.cubicTo(23, 52, 31, 51, 29, 57);
        stroke(path, c.overcast, 4.6, c.edge);
    }

    void cyclone(bool hurricane)
    {
        QPainterPath arm;
        arm.moveTo(56, 9);
        arm.cubicTo(57, 24, 48, 44, 31, 43);
        arm.cubicTo(14, 42, 16, 19, 31, 18);
        arm.cubicTo(38, 17, 43, 21, 42, 28);
        arm.cubicTo(37, 18, 24, 26, 28, 32);
        arm.cubicTo(35, 42, 49, 27, 56, 9);
        arm.closeSubpath();
        QTransform rotate;
        rotate.translate(32, 32);
        rotate.rotate(180);
        rotate.translate(-32, -32);
        const auto symbol = arm.united(rotate.map(arm));
        QPainterPath eye;
        eye.addEllipse(QPointF(32, 32), 5.5, 5.5);
        const auto cyclone = symbol.subtracted(eye);
        if (hurricane) shape(cyclone, c.severe, c.edge);
        else stroke(cyclone, c.severe, 3.0, c.edge);
    }

    void unknown()
    {
        QPainterPath ring;
        ring.addEllipse(QPointF(32, 32), 20, 20);
        stroke(ring, c.fog, 3.6, c.edge);
        QPainterPath mark;
        mark.moveTo(25, 25);
        mark.cubicTo(26, 19, 39, 19, 39, 27);
        mark.cubicTo(39, 32, 32, 32, 32, 38);
        stroke(mark, c.fog, 3.6, c.edge);
        pellet({32, 45}, 1.8);
    }

    QPainter &p;
    Ink c;
    bool small;
};

} // namespace

QImage renderIcon(Condition condition, Mode mode, const Colors &colors, QSize logicalSize, qreal dpr)
{
    if (logicalSize.isEmpty() || !qIsFinite(dpr) || dpr <= 0) return {};
    QImage image(QSize(qCeil(logicalSize.width() * dpr), qCeil(logicalSize.height() * dpr)),
                 QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return image;
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const int side = qMin(logicalSize.width(), logicalSize.height());
    painter.translate((logicalSize.width() - side) / 2.0, (logicalSize.height() - side) / 2.0);
    painter.scale(side / 64.0, side / 64.0);
    // Leave room for rounded caps and antialiasing at 16 px.
    painter.translate(2.5, 2.5);
    painter.scale(59.0 / 64.0, 59.0 / 64.0);
    Drawing drawing(painter, inks(mode, colors), side < 24);
    drawing.draw(condition);
    painter.end();
    return image;
}

} // namespace KrellWeather
