#include "KrellweatherPlugin.h"
#include "WeatherIcons.h"

#include "theme/Theme.h"
#include "widgets/Panel.h"

#ifdef Q_OS_WIN
#include "remote/RemoteSource.h"
#include "windows/Metar.h"
#include <QTimer>
#endif

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
#include <QPixmap>
#include <QSettings>
#include <QSizePolicy>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QtMath>

namespace {

constexpr const char *kMetarUrl = "https://aviationweather.gov/api/data/metar";
constexpr int kFetchTimeoutMs = 15000;
constexpr qsizetype kMaxWeatherBytes = 256 * 1024;

class WeatherIconWidget : public QWidget
{
public:
    explicit WeatherIconWidget(Theme *theme, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_theme(theme)
    {
        setFixedSize(30, 24);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        syncMode();
    }

    void setCondition(KrellWeather::Condition condition)
    {
        if (m_condition.kind == condition.kind && m_condition.night == condition.night
            && m_condition.stormPrecipitation == condition.stormPrecipitation) return;
        m_condition = condition;
        invalidate();
    }

    bool syncMode()
    {
        const QString setting = QSettings().value(QStringLiteral("plugins/krellweather/icon_mode")).toString();
        const auto mode = KrellWeather::modeFromKey(setting);
        if (m_mode == mode) return false;
        m_mode = mode;
        invalidate();
        return true;
    }

    void invalidate()
    {
        m_pixmap = {};
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const qreal dpr = devicePixelRatioF();
        if (m_pixmap.isNull() || m_cachedSize != size() || m_pixmap.devicePixelRatio() != dpr) {
            KrellWeather::Colors colors;
            if (m_theme) {
                colors.background = m_theme->color(QStringLiteral("panel_bg"), colors.background);
                colors.primary = m_theme->color(QStringLiteral("text_primary"), colors.primary);
                colors.secondary = m_theme->color(QStringLiteral("text_secondary"), colors.secondary);
                colors.accent = m_theme->color(QStringLiteral("text_accent"), colors.accent);
            }
            m_pixmap = QPixmap::fromImage(KrellWeather::renderIcon(m_condition, m_mode, colors, size(), dpr));
            m_cachedSize = size();
        }
        QPainter painter(this);
        painter.drawPixmap(0, 0, m_pixmap);
    }

private:
    Theme *m_theme;
    QPixmap m_pixmap;
    QSize m_cachedSize;
    KrellWeather::Condition m_condition;
    KrellWeather::Mode m_mode = KrellWeather::Mode::Color;
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
#ifdef Q_OS_WIN
    if (wspd < 0) return QStringLiteral("Wind n/a");
#endif
    if (wspd <= 0) return QStringLiteral("Calm");
    const int wdir = obj.value(QStringLiteral("wdir")).toInt(-1);
    if (wdir < 0) return QStringLiteral("%1 kt").arg(wspd);
    return QStringLiteral("%1 %2 kt").arg(wdir, 3, 10, QLatin1Char('0')).arg(wspd);
}


} // namespace

QString cssColor(const QColor &color)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

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
#ifdef Q_OS_WIN
    // Check for due refreshes even when the host and request timers drift.
    return 1000;
#else
    return QSettings().value(QStringLiteral("plugins/krellweather/interval_ms"),
                             600000).toInt();
#endif
}

QWidget *KrellweatherMonitor::createWidget(QWidget *parent)
{
    auto *panel = new Panel(theme(), parent);
    panel->setSurfaceKey(QStringLiteral("panel_bg_krellweather"));
#ifdef Q_OS_WIN
    panel->setAccessibleName(QStringLiteral("KrellWeather"));
#endif

    auto *body = new QWidget(panel);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_location = new QLabel(stationCode(), body);
    m_location->setAlignment(Qt::AlignCenter);

    m_primary = new QLabel(QStringLiteral("--"), body);
    m_primary->setAlignment(Qt::AlignCenter);

    m_icon = new WeatherIconWidget(theme(), body);
    auto *tempRow = new QHBoxLayout;
    tempRow->setContentsMargins(0, 0, 0, 0);
    tempRow->setSpacing(3);
    tempRow->addStretch(1);
    tempRow->addWidget(m_icon.data());
    tempRow->addWidget(m_primary);
    tempRow->addStretch(1);

    m_detail = new QLabel(QStringLiteral("waiting for weather"), body);
    m_detail->setAlignment(Qt::AlignCenter);
    m_detail->setWordWrap(false);

#ifdef Q_OS_WIN
    for (auto *label : {m_location.data(), m_primary.data(), m_detail.data()})
        label->setTextFormat(Qt::PlainText);
    m_detail->setWordWrap(true);
    updateTheme();
    // Resolve the signal through the host object's metaobject across the DLL boundary.
    connect(theme(), SIGNAL(themeChanged()), this, SLOT(updateTheme()));
#else
    applyThemeColors();
    connect(theme(), &Theme::themeChanged, this, [this]() {
        applyThemeColors();
        if (auto *icon = static_cast<WeatherIconWidget *>(m_icon.data())) icon->invalidate();
    });
#endif

    layout->addWidget(m_location);
    layout->addLayout(tempRow);
    layout->addWidget(m_detail);
    panel->addWidget(body);

    fetch();
    return panel;
}

void KrellweatherMonitor::tick()
{
    if (auto *icon = static_cast<WeatherIconWidget *>(m_icon.data())) {
        // Changing the artwork should not trigger another provider request.
        if (icon->syncMode()) return;
    }
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
#ifdef Q_OS_WIN
    if (m_requestedStation != code) {
        cancelPendingReply();
        m_observation = {};
        m_requestedAt.invalidate();
        m_requestedStation = code;
    }
    if (!KrellweatherWindows::isStationId(code)) {
        setMessage(QStringLiteral("Weather"), code.isEmpty()
            ? QStringLiteral("No station") : QStringLiteral("Enter an ICAO station"));
        return;
    }
    if (!m_observation.isEmpty() && m_lastUnitsCelsius != useCelsius())
        renderWeather(m_observation);
    const int interval = qBound(KrellweatherWindows::kMinIntervalMs,
        QSettings().value(QStringLiteral("plugins/krellweather/interval_ms"), 600000).toInt(),
        KrellweatherWindows::kMaxIntervalMs);
    if (m_requestedAt.isValid() && m_requestedAt.elapsed() < interval) return;
    if (!m_requestedAt.isValid()) setMessage(code, QStringLiteral("Fetching weather"));
#else
    if (code.isEmpty()) {
        setMessage(QStringLiteral("Weather"), QStringLiteral("No station"));
        return;
    }
#endif
    if (m_fetching) return;
    m_fetching = true;

    QUrl url(QString::fromLatin1(kMetarUrl));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("ids"), code);
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "krellix-krellweather/0.1");
    req.setTransferTimeout(kFetchTimeoutMs);
#ifdef Q_OS_WIN
    req.setRawHeader("Accept", "application/json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    m_requestedAt.start();
#endif
    QNetworkReply *reply = m_net.get(req);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleReply(reply);
    });
#ifdef Q_OS_WIN
    reply->setReadBufferSize(KrellweatherWindows::kMaxBytes + 1);
    connect(reply, &QIODevice::readyRead, this, [reply] {
        if (reply->bytesAvailable() > KrellweatherWindows::kMaxBytes) {
            reply->setProperty("responseTooLarge", true);
            reply->abort();
        }
    });
    QTimer::singleShot(kFetchTimeoutMs, reply, [reply] {
        if (reply->isRunning()) reply->abort();
    });
#endif
}

void KrellweatherMonitor::handleReply(QNetworkReply *reply)
{
#ifdef Q_OS_WIN
    if (!reply) return;
    reply->deleteLater();
    if (m_tearingDown || m_reply != reply) return;
    m_reply = nullptr;
    m_fetching = false;
    if (m_requestedStation != stationCode()) {
        fetch();
        return;
    }
    m_observation = {};
    if (reply->bytesAvailable() > KrellweatherWindows::kMaxBytes
        || reply->property("responseTooLarge").toBool()) {
        setMessage(m_requestedStation, QStringLiteral("Weather response too large"));
        return;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 204) {
        setMessage(m_requestedStation, QStringLiteral("No recent METAR"));
        return;
    }
    if (status != 200 || reply->error() != QNetworkReply::NoError) {
        setMessage(m_requestedStation, QStringLiteral("Weather unavailable"));
        return;
    }
    m_observation = KrellweatherWindows::selectObservation(reply->readAll(), m_requestedStation);
    if (m_observation.isEmpty()) {
        setMessage(m_requestedStation, QStringLiteral("No valid METAR"));
        return;
    }
    renderWeather(m_observation);
#else
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
    if (payload.size() > kMaxWeatherBytes) {
        setMessage(stationCode(), QStringLiteral("Weather response too large"));
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
#endif
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

#ifdef Q_OS_WIN
void KrellweatherMonitor::updateTheme()
{
    applyThemeColors();
    if (auto *icon = static_cast<WeatherIconWidget *>(m_icon.data())) icon->invalidate();
}
#endif

void KrellweatherMonitor::applyThemeColors()
{
    const QColor primary = theme()->textStyle(
        QStringLiteral("text_primary")).color;
    const QColor secondary = theme()->textStyle(
        QStringLiteral("text_secondary"),
        QStringLiteral("text_primary")).color;
    const QColor accent = theme()->textStyle(
        QStringLiteral("text_accent"),
        QStringLiteral("text_primary")).color;

    if (m_location) {
        m_location->setStyleSheet(QStringLiteral(
            "font-size: 9px; font-weight: 700; color: %1;")
            .arg(cssColor(accent.isValid() ? accent : primary)));
    }
    if (m_primary) {
        m_primary->setStyleSheet(QStringLiteral(
            "font-size: 16px; font-weight: 800; color: %1;")
            .arg(cssColor(primary)));
    }
    if (m_detail) {
        m_detail->setStyleSheet(QStringLiteral(
            "font-size: 9px; font-weight: 600; color: %1;")
            .arg(cssColor(secondary.isValid() ? secondary : primary)));
    }
}

void KrellweatherMonitor::renderWeather(const QJsonObject &obj)
{
#ifdef Q_OS_WIN
    m_lastUnitsCelsius = useCelsius();
#endif
    const QString code = obj.value(QStringLiteral("icaoId")).toString(stationCode());
    const double tempC = obj.value(QStringLiteral("temp")).toDouble(qQNaN());
    const QString temp = qIsNaN(tempC) ? QStringLiteral("--") : tempText(tempC);
    const QString wx = weatherText(obj);
    const QString detail = QStringLiteral("%1  %2").arg(wx, windText(obj));

    if (m_location) m_location->setText(code);
    if (auto *icon = static_cast<WeatherIconWidget *>(m_icon.data()))
        icon->setCondition(KrellWeather::classifyObservation(obj));
    if (m_primary) m_primary->setText(temp);
    if (m_detail) m_detail->setText(detail);
}

void KrellweatherMonitor::setMessage(const QString &line1, const QString &line2)
{
    if (m_location) m_location->setText(line1);
    if (auto *icon = static_cast<WeatherIconWidget *>(m_icon.data()))
        icon->setCondition({});
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
#ifdef Q_OS_WIN
    return QStringLiteral("0.1.1-windows.1");
#else
    return QStringLiteral("0.1.1");
#endif
}

QList<MonitorBase *> KrellweatherPlugin::createMonitors(Theme *theme, QObject *parent)
{
#ifdef Q_OS_WIN
    if (RemoteSource::instance()) return {};
#endif
    if (!QSettings().value(QStringLiteral("plugins/krellweather/enabled"), false).toBool())
        return {};
    return {new KrellweatherMonitor(theme, parent)};
}
