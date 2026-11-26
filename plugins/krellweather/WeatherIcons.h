#pragma once

#include <QColor>
#include <QImage>
#include <QSize>
#include <QStringView>

class QJsonObject;

namespace KrellWeather {

enum class Kind {
    Unknown, Clear, MostlyClear, PartlyCloudy, MostlyCloudy, Cloudy, Overcast,
    Drizzle, LightRain, Rain, HeavyRain, RainShowers, FreezingRain,
    LightSnow, Snow, HeavySnow, SnowShowers, Sleet, Mixed, Hail, BlowingSnow,
    Fog, Mist, Haze, Smoke, Dust, Wind, StrongWind,
    Thunderstorm, SevereThunderstorm, Tornado, Hurricane, TropicalStorm
};

enum class Mode { Color, Theme, Monochrome };

struct Condition {
    Kind kind = Kind::Unknown;
    bool night = false;
    bool stormPrecipitation = false;
};

struct Colors {
    QColor background = QColor(24, 42, 55);
    QColor primary = QColor(240, 244, 248);
    QColor secondary = QColor(170, 183, 196);
    QColor accent = QColor(98, 185, 232);
};

Mode modeFromKey(QStringView key);
Condition classifyObservation(const QJsonObject &observation);
QImage renderIcon(Condition condition, Mode mode, const Colors &colors, QSize logicalSize, qreal dpr = 1);

} // namespace KrellWeather
