#include "KrellweatherPlugin.h"

#include "theme/Theme.h"
#include "widgets/Panel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QSizePolicy>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QtMath>

namespace {

constexpr const char *kMetarUrl = "https://aviationweather.gov/api/data/metar";

enum class WeatherIconKind {
    Unknown,
    Sun,
    Moon,
    PartlySun,
    PartlyMoon,
    Cloud
};

class WeatherIconWidget : public QWidget
{
public:
    explicit WeatherIconWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(30, 24);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    void setKind(WeatherIconKind kind)
    {
        if (m_kind == kind) return;
        m_kind = kind;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        if (m_kind == WeatherIconKind::Unknown) {
            p.setPen(QPen(QColor(170, 210, 225, 180), 1.4));
            p.drawEllipse(QRectF(8, 5, 14, 14));
            p.drawLine(QPointF(15, 11), QPointF(15, 15));
            p.drawPoint(QPointF(15, 18));
            return;
        }

        const bool cloud =
            m_kind == WeatherIconKind::Cloud
            || m_kind == WeatherIconKind::PartlySun
            || m_kind == WeatherIconKind::PartlyMoon;
        const bool moon =
            m_kind == WeatherIconKind::Moon
            || m_kind == WeatherIconKind::PartlyMoon;
        const bool sun =
            m_kind == WeatherIconKind::Sun
            || m_kind == WeatherIconKind::PartlySun;

        if (sun) {
            p.setPen(QPen(QColor(255, 220, 88, 220), 1.3));
            for (int i = 0; i < 8; ++i) {
                const double a = i * M_PI / 4.0;
                const QPointF c(11, 10);
                p.drawLine(c + QPointF(qCos(a) * 7.0, qSin(a) * 7.0),
                           c + QPointF(qCos(a) * 10.0, qSin(a) * 10.0));
            }
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 202, 55, 235));
            p.drawEllipse(QRectF(5, 4, 12, 12));
        }

        if (moon) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(218, 232, 246, 230));
            p.drawEllipse(QRectF(5, 3, 14, 14));
            p.setBrush(QColor(24, 42, 55, 240));
            p.drawEllipse(QRectF(10, 1, 13, 14));
        }

        if (cloud) {
            const int y = m_kind == WeatherIconKind::Cloud ? 7 : 9;
            p.setPen(QPen(QColor(42, 64, 74, 210), 1.0));
            p.setBrush(QColor(190, 218, 226, 238));
            QPainterPath path;
            path.addEllipse(QRectF(6, y + 3, 9, 8));
            path.addEllipse(QRectF(12, y, 10, 11));
            path.addEllipse(QRectF(18, y + 4, 8, 7));
            path.addRoundedRect(QRectF(7, y + 7, 19, 7), 3, 3);
            p.drawPath(path.simplified());
        }
    }

private:
    WeatherIconKind m_kind = WeatherIconKind::Unknown;
};

QString stationCode()
{
    return QSettings().value(QStringLiteral("plugins/krellweather/station"),
                             QStringLiteral("KMIA")).toString().trimmed().toUpper();
}

bool useCelsius()
{
    return QSettings().value(QStringLiteral("plugins/krellweather/units"),
                             QStringLiteral("F")).toString().toUpper()
        == QLatin1String("C");
}

QString tempText(double celsius)
{
    if (useCelsius())
        return QStringLiteral("%1C").arg(qRound(celsius));
    return QStringLiteral("%1F").arg(qRound(celsius * 9.0 / 5.0 + 32.0));
}

QString weatherText(const QJsonObject &obj)
{
    const QString wx = obj.value(QStringLiteral("wxString")).toString().trimmed();
    if (!wx.isEmpty()) return wx;

    const QString cover = obj.value(QStringLiteral("cover")).toString().trimmed();
    if (!cover.isEmpty()) {
        if (cover == QLatin1String("CLR") || cover == QLatin1String("SKC"))
            return QStringLiteral("Clear");
        if (cover == QLatin1String("FEW")) return QStringLiteral("Few clouds");
        if (cover == QLatin1String("SCT")) return QStringLiteral("Scattered");
        if (cover == QLatin1String("BKN")) return QStringLiteral("Broken");
        if (cover == QLatin1String("OVC")) return QStringLiteral("Overcast");
        return cover;
    }

    const QString cat = obj.value(QStringLiteral("fltCat")).toString().trimmed();
    return cat.isEmpty() ? QStringLiteral("Observed") : cat;
}

QString windText(const QJsonObject &obj)
{
    const int wspd = obj.value(QStringLiteral("wspd")).toInt(-1);
    if (wspd <= 0) return QStringLiteral("Calm");
    const int wdir = obj.value(QStringLiteral("wdir")).toInt(-1);
    if (wdir < 0) return QStringLiteral("%1 kt").arg(wspd);
    return QStringLiteral("%1 %2 kt").arg(wdir, 3, 10, QLatin1Char('0')).arg(wspd);
}

bool isStationDark(const QJsonObject &obj)
{
    QDateTime utc = QDateTime::fromString(
        obj.value(QStringLiteral("obsTime")).toString(), Qt::ISODate);
    if (!utc.isValid())
        utc = QDateTime::currentDateTimeUtc();
    utc = utc.toUTC();

    const double lon = obj.value(QStringLiteral("lon")).toDouble(qQNaN());
    const int offsetSecs = qIsNaN(lon) ? 0 : qRound(lon / 15.0 * 3600.0);
    const QTime local = utc.addSecs(offsetSecs).time();
    return local.hour() < 6 || local.hour() >= 19;
}

WeatherIconKind iconKindForWeather(const QJsonObject &obj)
{
    const bool dark = isStationDark(obj);
    const QString wx = obj.value(QStringLiteral("wxString")).toString().toUpper();
    const QString cover = obj.value(QStringLiteral("cover")).toString().toUpper();
    const QString combined = wx + QLatin1Char(' ') + cover;

    if (combined.contains(QStringLiteral("OVC"))
        || combined.contains(QStringLiteral("BKN"))
        || combined.contains(QStringLiteral("CLOUD"))
        || combined.contains(QStringLiteral("RA"))
        || combined.contains(QStringLiteral("SN"))
        || combined.contains(QStringLiteral("TS"))
        || combined.contains(QStringLiteral("DZ"))) {
        return WeatherIconKind::Cloud;
    }

    if (combined.contains(QStringLiteral("FEW"))
        || combined.contains(QStringLiteral("SCT"))
        || combined.contains(QStringLiteral("PART"))) {
        return dark ? WeatherIconKind::PartlyMoon : WeatherIconKind::PartlySun;
    }

    if (combined.contains(QStringLiteral("CLR"))
        || combined.contains(QStringLiteral("SKC"))
        || combined.contains(QStringLiteral("CLEAR"))
        || combined.trimmed().isEmpty()) {
        return dark ? WeatherIconKind::Moon : WeatherIconKind::Sun;
    }

    return dark ? WeatherIconKind::Moon : WeatherIconKind::Sun;
}

} // namespace

KrellweatherMonitor::KrellweatherMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
}

KrellweatherMonitor::~KrellweatherMonitor()
{
    shutdown();
}

QString KrellweatherMonitor::id() const
{
    return QStringLiteral("krellweather");
}

QString KrellweatherMonitor::displayName() const
{
    return QStringLiteral("Krellweather");
}

int KrellweatherMonitor::tickIntervalMs() const
{
    return QSettings().value(QStringLiteral("plugins/krellweather/interval_ms"),
                             600000).toInt();
}

QWidget *KrellweatherMonitor::createWidget(QWidget *parent)
{
    auto *panel = new Panel(theme(), parent);
    panel->setSurfaceKey(QStringLiteral("panel_bg_krellweather"));

    auto *body = new QWidget(panel);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(1);

    m_location = new QLabel(stationCode(), body);
    m_location->setAlignment(Qt::AlignCenter);
    m_location->setStyleSheet(QStringLiteral(
        "font-size: 9px; font-weight: 700;"
        "color: rgba(205, 230, 242, 235);"));

    m_primary = new QLabel(QStringLiteral("--"), body);
    m_primary->setAlignment(Qt::AlignCenter);
    m_primary->setStyleSheet(QStringLiteral(
        "font-size: 16px; font-weight: 800;"
        "color: rgba(245, 252, 255, 245);"));

    m_icon = new WeatherIconWidget(body);
    auto *tempRow = new QHBoxLayout;
    tempRow->setContentsMargins(0, 0, 0, 0);
    tempRow->setSpacing(3);
    tempRow->addStretch(1);
    tempRow->addWidget(m_icon.data());
    tempRow->addWidget(m_primary);
    tempRow->addStretch(1);

    m_detail = new QLabel(QStringLiteral("waiting for weather"), body);
    m_detail->setAlignment(Qt::AlignCenter);
    m_detail->setWordWrap(true);
    m_detail->setStyleSheet(QStringLiteral(
        "font-size: 9px; font-weight: 600;"
        "color: rgba(170, 210, 225, 220);"));

    layout->addWidget(m_location);
    layout->addLayout(tempRow);
    layout->addWidget(m_detail);
    panel->addWidget(body);

    fetch();
    return panel;
}

void KrellweatherMonitor::tick()
{
    fetch();
}

void KrellweatherMonitor::shutdown()
{
    m_tearingDown = true;
    cancelPendingReply();
    m_location = nullptr;
    m_icon = nullptr;
    m_primary = nullptr;
    m_detail = nullptr;
}

void KrellweatherMonitor::fetch()
{
    if (m_tearingDown) return;

    const QString code = stationCode();
    if (code.isEmpty()) {
        setMessage(QStringLiteral("Weather"), QStringLiteral("No station"));
        return;
    }
    if (m_fetching) return;
    m_fetching = true;

    QUrl url(QString::fromLatin1(kMetarUrl));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("ids"), code);
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "krellix-krellweather/0.1");
    QNetworkReply *reply = m_net.get(req);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleReply(reply);
    });
}

void KrellweatherMonitor::handleReply(QNetworkReply *reply)
{
    if (m_tearingDown) {
        if (reply) reply->deleteLater();
        return;
    }
    if (!reply) {
        m_fetching = false;
        setMessage(stationCode(), QStringLiteral("Weather unavailable"));
        return;
    }

    const QByteArray payload = reply->readAll();
    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError error = reply->error();
    const QString errorText = reply->errorString();
    reply->deleteLater();
    if (m_reply == reply) m_reply = nullptr;
    m_fetching = false;

    if (error != QNetworkReply::NoError || httpStatus == 204) {
        setMessage(stationCode(), QStringLiteral("Weather unavailable"));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        setMessage(stationCode(), errorText.isEmpty()
            ? QStringLiteral("Bad weather data")
            : QStringLiteral("Weather unavailable"));
        return;
    }
    if (!doc.isArray() || doc.array().isEmpty()) {
        setMessage(stationCode(), QStringLiteral("No METAR data"));
        return;
    }

    const QJsonObject obj = doc.array().first().toObject();
    if (obj.isEmpty()) {
        setMessage(stationCode(), QStringLiteral("No METAR data"));
        return;
    }
    renderWeather(obj);
}

void KrellweatherMonitor::cancelPendingReply()
{
    m_fetching = false;
    if (!m_reply) return;

    QNetworkReply *reply = m_reply.data();
    m_reply = nullptr;
    disconnect(reply, nullptr, this, nullptr);
    if (reply->isRunning())
        reply->abort();
    reply->deleteLater();
}

void KrellweatherMonitor::renderWeather(const QJsonObject &obj)
{
    const QString code = obj.value(QStringLiteral("icaoId")).toString(stationCode());
    const double tempC = obj.value(QStringLiteral("temp")).toDouble(qQNaN());
    const QString temp = qIsNaN(tempC) ? QStringLiteral("--") : tempText(tempC);
    const QString wx = weatherText(obj);
    const QString detail = QStringLiteral("%1  %2").arg(wx, windText(obj));

    if (m_location) m_location->setText(code);
    if (m_icon)
        static_cast<WeatherIconWidget *>(m_icon.data())->setKind(iconKindForWeather(obj));
    if (m_primary) m_primary->setText(temp);
    if (m_detail) m_detail->setText(detail);
}

void KrellweatherMonitor::setMessage(const QString &line1, const QString &line2)
{
    if (m_location) m_location->setText(line1);
    if (m_icon)
        static_cast<WeatherIconWidget *>(m_icon.data())->setKind(WeatherIconKind::Unknown);
    if (m_primary) m_primary->setText(QStringLiteral("--"));
    if (m_detail) m_detail->setText(line2);
}

QString KrellweatherPlugin::pluginId() const
{
    return QStringLiteral("krellweather");
}

QString KrellweatherPlugin::pluginName() const
{
    return QStringLiteral("Krellweather");
}

QString KrellweatherPlugin::pluginVersion() const
{
    return QStringLiteral("0.1.0");
}

QList<MonitorBase *> KrellweatherPlugin::createMonitors(Theme *theme, QObject *parent)
{
    if (!QSettings().value(QStringLiteral("plugins/krellweather/enabled"), true).toBool())
        return {};
    return {new KrellweatherMonitor(theme, parent)};
}
