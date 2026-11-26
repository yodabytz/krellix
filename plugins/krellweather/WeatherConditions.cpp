#include "WeatherIcons.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QtMath>

namespace KrellWeather {
namespace {

bool stationIsDark(const QJsonObject &observation)
{
    const auto value = observation.value(QStringLiteral("obsTime"));
    QDateTime utc;
    if (value.isDouble()) {
        const double time = value.toDouble();
        if (time > 0 && time < 253402300800000.0) {
            const auto stamp = static_cast<qint64>(time);
            utc = QDateTime::fromMSecsSinceEpoch(stamp > 253402300799LL ? stamp : 1000 * stamp);
        }
    } else {
        utc = QDateTime::fromString(value.toString(), Qt::ISODate);
    }
    if (!utc.isValid())
        utc = QDateTime::fromString(observation.value(QStringLiteral("reportTime")).toString(), Qt::ISODate);
    if (!utc.isValid()) utc = QDateTime::currentDateTimeUtc();
    const double longitude = observation.value(QStringLiteral("lon")).toDouble();
    const int offset = qIsFinite(longitude) && qAbs(longitude) <= 180 ? qRound(longitude * 240) : 0;
    const int hour = utc.toUTC().addSecs(offset).time().hour();
    // Keep the existing local solar-time approximation; METAR has no day/night flag.
    return hour < 6 || hour >= 19;
}

struct Group {
    Kind kind = Kind::Unknown;
    int priority = 0;
    bool stormPrecipitation = false;
};

Group presentWeather(const QString &token)
{
    // AWC METAR present-weather groups: an optional qualifier, a descriptor,
    // and whole two-letter phenomenon codes. Never match substrings of free text.
    static const QRegularExpression pattern(QStringLiteral(
        R"(^(VC|[+-])?(MI|PR|BC|DR|BL|SH|TS|FZ)?((?:DZ|RA|SN|SG|IC|PL|PE|GR|GS|UP|BR|FG|FU|VA|DU|SA|HZ|PY|PO|SQ|FC|SS|DS){0,3})$)"));
    const auto match = pattern.match(token);
    if (!match.hasMatch()) return {};
    const auto qualifier = match.capturedView(1);
    const auto descriptor = match.capturedView(2);
    const auto phenomena = match.capturedView(3);
    const auto has = [phenomena](QLatin1String code) {
        for (qsizetype i = 0; i < phenomena.size(); i += 2)
            if (phenomena.mid(i, 2) == code) return true;
        return false;
    };
    const bool rain = has(QLatin1String("RA")) || has(QLatin1String("DZ"));
    const bool snow = has(QLatin1String("SN")) || has(QLatin1String("SG"));
    const bool hail = has(QLatin1String("GR")) || has(QLatin1String("GS"));
    const bool ice = has(QLatin1String("PL")) || has(QLatin1String("PE")) || has(QLatin1String("IC"));
    const bool light = qualifier == QLatin1String("-");
    const bool heavy = qualifier == QLatin1String("+");

    if (has(QLatin1String("FC"))) return {Kind::Tornado, 100};
    // +TSRA is heavy precipitation, not a severe-thunderstorm designation.
    if (descriptor == QLatin1String("TS")) return {Kind::Thunderstorm, 90, rain || snow || hail || ice};
    if (descriptor == QLatin1String("FZ") && rain) return {Kind::FreezingRain, 84};
    if (hail) return {Kind::Hail, 82};
    if (ice) return {Kind::Sleet, 81};
    if (rain && snow) return {Kind::Mixed, 80};
    if (snow && (descriptor == QLatin1String("BL") || descriptor == QLatin1String("DR")))
        return {Kind::BlowingSnow, 78};
    if (descriptor == QLatin1String("SH")) {
        if (snow) return {Kind::SnowShowers, 74};
        if (rain || (qualifier == QLatin1String("VC") && phenomena.isEmpty()))
            return {Kind::RainShowers, 74};
    }
    if (snow) return {light ? Kind::LightSnow : heavy ? Kind::HeavySnow : Kind::Snow, heavy ? 73 : light ? 65 : 70};
    if (has(QLatin1String("RA"))) return {light ? Kind::LightRain : heavy ? Kind::HeavyRain : Kind::Rain,
                                        heavy ? 73 : light ? 65 : 70};
    if (rain) return {Kind::Drizzle, heavy ? 69 : 64};
    if (has(QLatin1String("SQ"))) return {Kind::StrongWind, 60};
    if (has(QLatin1String("FG"))) return {Kind::Fog, 55};
    if (has(QLatin1String("BR"))) return {Kind::Mist, 54};
    if (has(QLatin1String("FU")) || has(QLatin1String("VA"))) return {Kind::Smoke, 53};
    if (has(QLatin1String("DU")) || has(QLatin1String("SA")) || has(QLatin1String("SS"))
        || has(QLatin1String("DS")) || has(QLatin1String("PO"))) return {Kind::Dust, 52};
    if (has(QLatin1String("HZ"))) return {Kind::Haze, 51};
    if (has(QLatin1String("UP"))) return {Kind::Unknown, 50};
    return {};
}

int coverRank(QStringView cover)
{
    if (cover == QLatin1String("CLR") || cover == QLatin1String("SKC")) return 0;
    if (cover == QLatin1String("FEW") || cover == QLatin1String("CAVOK")) return 1;
    if (cover == QLatin1String("SCT")) return 2;
    if (cover == QLatin1String("BKN")) return 3;
    if (cover == QLatin1String("OVC") || cover == QLatin1String("VV")) return 4;
    return -1;
}

} // namespace

Mode modeFromKey(QStringView key)
{
    if (key == QLatin1String("theme")) return Mode::Theme;
    if (key == QLatin1String("mono")) return Mode::Monochrome;
    return Mode::Color;
}

Condition classifyObservation(const QJsonObject &observation)
{
    const bool night = stationIsDark(observation);
    const QString wx = observation.value(QStringLiteral("wxString")).toString().trimmed().toUpper();
    static const QRegularExpression whitespace(QStringLiteral(R"(\s+)"));
    Group strongest;
    for (const auto &token : wx.split(whitespace, Qt::SkipEmptyParts)) {
        const Group group = presentWeather(token);
        if (group.priority > strongest.priority) strongest = group;
    }
    if (strongest.priority > 0) return {strongest.kind, night, strongest.stormPrecipitation};
    if (!wx.isEmpty()) return {Kind::Unknown, night};

    int rank = coverRank(observation.value(QStringLiteral("cover")).toString().trimmed().toUpper());
    for (const auto &layer : observation.value(QStringLiteral("clouds")).toArray())
        rank = qMax(rank, coverRank(layer.toObject().value(QStringLiteral("cover")).toString().trimmed().toUpper()));
    constexpr Kind sky[] = {Kind::Clear, Kind::MostlyClear, Kind::PartlyCloudy, Kind::MostlyCloudy, Kind::Overcast};
    return {rank < 0 ? Kind::Unknown : sky[rank], night};
}

} // namespace KrellWeather
