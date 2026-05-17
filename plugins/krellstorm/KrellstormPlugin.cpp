#include "KrellstormPlugin.h"

#include "theme/Theme.h"
#include "widgets/Panel.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QProcess>
#include <QSettings>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <cmath>

namespace {

constexpr const char *kIemMapUrl = "https://mesonet.agron.iastate.edu/GIS/radmap.php";
constexpr const char *kNwsAlertsUrl = "https://api.weather.gov/alerts/active";
constexpr const char *kUserAgent = "krellix-krellstorm/0.1 (hello@cerberix.org)";
constexpr int kFetchTimeoutMs = 20000;
constexpr qsizetype kMaxMapBytes = 4 * 1024 * 1024;
constexpr qsizetype kMaxAlertBytes = 1024 * 1024;
constexpr int kDefaultMapRefreshS = 60;
constexpr int kDefaultAlertRefreshS = 120;
constexpr int kDefaultZoom = 6;
constexpr int kDefaultImageHeight = 180;
constexpr int kDefaultMinSoundGapS = 30;
constexpr int kDefaultSnoozeS = 1800;

constexpr int kSevNone     = 0;
constexpr int kSevInfo     = 1;  // advisory, statement
constexpr int kSevWatch    = 2;  // *.Watch
constexpr int kSevWarning  = 3;  // *.Warning (severe T-storm, tornado normal, flash flood, etc.)
constexpr int kSevUrgent   = 4;  // Tornado Emergency / PDS / severity=Extreme

QSettings settings()
{
    return QSettings();
}

bool boolSetting(const QString &key, bool fallback)
{
    return settings().value(QStringLiteral("plugins/krellstorm/") + key, fallback).toBool();
}

int intSetting(const QString &key, int fallback)
{
    return settings().value(QStringLiteral("plugins/krellstorm/") + key, fallback).toInt();
}

double doubleSetting(const QString &key, double fallback)
{
    return settings().value(QStringLiteral("plugins/krellstorm/") + key, fallback).toDouble();
}

QString stringSetting(const QString &key, const QString &fallback)
{
    return settings().value(QStringLiteral("plugins/krellstorm/") + key, fallback).toString();
}

QStringList layers()
{
    const QString raw = stringSetting(QStringLiteral("layers"),
        QStringLiteral("nexrad-n0q,warnings,uscounties,usstates"));
    QStringList out;
    for (const QString &part : raw.split(QLatin1Char(','), Qt::SkipEmptyParts))
        out << part.trimmed();
    if (out.isEmpty())
        out << QStringLiteral("nexrad-n0q") << QStringLiteral("warnings");
    return out;
}

struct BBox { double lonMin; double latMin; double lonMax; double latMax; };

BBox bboxFromLatLonZoom(double lat, double lon, int zoom, int widthPx, int heightPx)
{
    const int z = std::max(1, std::min(zoom, 12));
    const double lonSpan = 360.0 / std::pow(2.0, double(z));
    const int w = std::max(1, widthPx);
    const int h = std::max(1, heightPx);
    const double aspect = double(h) / double(w);
    const double latSpan = lonSpan * aspect;
    return { lon - lonSpan / 2.0, lat - latSpan / 2.0,
             lon + lonSpan / 2.0, lat + latSpan / 2.0 };
}

QString cssColor(const QColor &color)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
}

int severityRank(const QString &event, const QString &severityField,
                 const QJsonObject &parameters)
{
    const QString sev = severityField.toLower();
    const QString ev = event.toLower();

    if (sev == QLatin1String("extreme"))
        return kSevUrgent;
    if (ev.contains(QStringLiteral("tornado")) && ev.contains(QStringLiteral("warning"))) {
        const QJsonValue threat = parameters.value(QStringLiteral("tornadoDamageThreat"));
        if (threat.isArray() && !threat.toArray().isEmpty()) {
            const QString s = threat.toArray().first().toString().toUpper();
            if (s.contains(QStringLiteral("CATASTROPHIC"))
                || s.contains(QStringLiteral("CONSIDERABLE")))
                return kSevUrgent;
        }
        if (ev.contains(QStringLiteral("emergency")))
            return kSevUrgent;
        return kSevWarning;
    }
    if (ev.contains(QStringLiteral("warning")))
        return kSevWarning;
    if (ev.contains(QStringLiteral("watch")))
        return kSevWatch;
    if (sev == QLatin1String("severe"))
        return kSevWarning;
    if (sev == QLatin1String("moderate"))
        return kSevWatch;
    return kSevInfo;
}

QString severityShortLabel(int rank)
{
    switch (rank) {
    case kSevUrgent:  return QStringLiteral("TOR!");
    case kSevWarning: return QStringLiteral("WARN");
    case kSevWatch:   return QStringLiteral("WATCH");
    case kSevInfo:    return QStringLiteral("INFO");
    default:          return QStringLiteral("OK");
    }
}

QString severityColorKey(int rank)
{
    switch (rank) {
    case kSevUrgent:  return QStringLiteral("accent_critical");
    case kSevWarning: return QStringLiteral("accent_warning");
    case kSevWatch:   return QStringLiteral("accent_warning");
    case kSevInfo:    return QStringLiteral("text_accent");
    default:          return QStringLiteral("accent_ok");
    }
}

QString formatExpiry(const QString &iso)
{
    const QDateTime when = QDateTime::fromString(iso, Qt::ISODate);
    if (!when.isValid())
        return {};
    return when.toLocalTime().toString(QStringLiteral("until HH:mm"));
}

void desktopNotify(const QString &title, const QString &body, int sev)
{
    if (QStandardPaths::findExecutable(QStringLiteral("notify-send")).isEmpty())
        return;
    QString urgency = QStringLiteral("normal");
    if (sev >= kSevUrgent)
        urgency = QStringLiteral("critical");
    else if (sev <= kSevInfo)
        urgency = QStringLiteral("low");
    QProcess::startDetached(QStringLiteral("notify-send"), {
        QStringLiteral("-i"), QStringLiteral("weather-storm"),
        QStringLiteral("-u"), urgency,
        QStringLiteral("-t"), QStringLiteral("30000"),
        title, body,
    });
}

void playWav(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path))
        return;
    for (const QString &cmd : {QStringLiteral("paplay"),
                               QStringLiteral("aplay"),
                               QStringLiteral("canberra-gtk-play")}) {
        if (QStandardPaths::findExecutable(cmd).isEmpty())
            continue;
        if (cmd == QStringLiteral("canberra-gtk-play"))
            QProcess::startDetached(cmd, {QStringLiteral("-f"), path});
        else
            QProcess::startDetached(cmd, {path});
        return;
    }
}

} // namespace

// ---- StormSeverityChip ------------------------------------------------------
//
// Tiny painted pill that recolors per active alert tier. Mirrors the shape of
// krellhealth's HealthStatusBadge.

class StormSeverityChip : public QWidget
{
public:
    explicit StormSeverityChip(Theme *theme, QWidget *parent = nullptr)
        : QWidget(parent), m_theme(theme)
    {
        setFixedHeight(16);
        setMinimumWidth(48);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    void setSeverity(int rank, const QString &text)
    {
        if (m_rank == rank && m_text == text)
            return;
        m_rank = rank;
        m_text = text;
        updateGeometry();
        update();
    }

    QSize sizeHint() const override
    {
        QFontMetrics fm(font());
        return { fm.horizontalAdvance(m_text) + 16, 16 };
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        QColor bg(80, 90, 100, 200);
        QColor fg(235, 240, 245, 240);
        if (m_theme) {
            const QString key = severityColorKey(m_rank);
            const QColor themed = m_theme->color(key, bg);
            if (themed.isValid())
                bg = themed;
            const QColor text = m_theme->color(QStringLiteral("text_primary"), fg);
            if (text.isValid())
                fg = text;
        }
        bg.setAlpha(220);

        QPainterPath path;
        path.addRoundedRect(rect().adjusted(0, 0, -1, -1), 7, 7);
        p.fillPath(path, bg);

        p.setPen(fg);
        QFont f = font();
        f.setPointSize(8);
        f.setBold(true);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, m_text);
    }

private:
    Theme *m_theme = nullptr;
    int m_rank = kSevNone;
    QString m_text = severityShortLabel(kSevNone);
};

// ---- KrellstormMonitor ------------------------------------------------------

KrellstormMonitor::KrellstormMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
}

KrellstormMonitor::~KrellstormMonitor()
{
    shutdown();
}

QString KrellstormMonitor::id() const
{
    return QStringLiteral("krellstorm");
}

QString KrellstormMonitor::displayName() const
{
    return QStringLiteral("Krellstorm");
}

int KrellstormMonitor::tickIntervalMs() const
{
    const int mapS = std::max(15, intSetting(QStringLiteral("map_refresh_s"),
                                             kDefaultMapRefreshS));
    const int alertS = std::max(15, intSetting(QStringLiteral("alert_refresh_s"),
                                               kDefaultAlertRefreshS));
    return std::min(mapS, alertS) * 1000;
}

QWidget *KrellstormMonitor::createWidget(QWidget *parent)
{
    auto *panel = new Panel(theme(), parent);
    panel->setSurfaceKey(QStringLiteral("panel_bg_krellstorm"));

    auto *body = new QWidget(panel);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(6);
    m_title = new QLabel(QStringLiteral("Krellstorm"), body);
    m_title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_chip = new StormSeverityChip(theme(), body);
    m_chip->setSeverity(kSevNone, severityShortLabel(kSevNone));
    header->addWidget(m_title.data(), 1);
    header->addWidget(m_chip.data(), 0);
    layout->addLayout(header);

    m_mapLabel = new QLabel(body);
    m_mapLabel->setAlignment(Qt::AlignCenter);
    m_mapLabel->setMinimumHeight(intSetting(QStringLiteral("image_height"),
                                            kDefaultImageHeight));
    m_mapLabel->setText(QStringLiteral("loading radar..."));
    m_mapLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(m_mapLabel.data());

    m_headline = new QLabel(QStringLiteral("no active alerts"), body);
    m_headline->setAlignment(Qt::AlignCenter);
    m_headline->setWordWrap(false);
    layout->addWidget(m_headline.data());

    m_expiry = new QLabel(QString(), body);
    m_expiry->setAlignment(Qt::AlignCenter);
    m_expiry->setWordWrap(false);
    layout->addWidget(m_expiry.data());

    applyThemeColors();
    connect(theme(), &Theme::themeChanged, this, [this]() {
        applyThemeColors();
        if (m_chip) m_chip->update();
    });

    panel->addWidget(body);

    fetchMap();
    fetchAlerts();
    return panel;
}

void KrellstormMonitor::tick()
{
    if (m_tearingDown)
        return;
    const QDateTime now = QDateTime::currentDateTime();
    const int mapS = std::max(15, intSetting(QStringLiteral("map_refresh_s"),
                                             kDefaultMapRefreshS));
    const int alertS = std::max(15, intSetting(QStringLiteral("alert_refresh_s"),
                                               kDefaultAlertRefreshS));
    if (!m_lastMapAt.isValid() || m_lastMapAt.secsTo(now) >= mapS)
        fetchMap();
    if (!m_lastAlertAt.isValid() || m_lastAlertAt.secsTo(now) >= alertS)
        fetchAlerts();
}

void KrellstormMonitor::shutdown()
{
    m_tearingDown = true;
    cancelReply(m_mapReply);
    cancelReply(m_alertReply);
    m_title = nullptr;
    m_chip = nullptr;
    m_mapLabel = nullptr;
    m_headline = nullptr;
    m_expiry = nullptr;
}

void KrellstormMonitor::cancelReply(QPointer<QNetworkReply> &slot)
{
    if (!slot)
        return;
    QNetworkReply *r = slot.data();
    slot = nullptr;
    disconnect(r, nullptr, this, nullptr);
    if (r->isRunning())
        r->abort();
    r->deleteLater();
}

void KrellstormMonitor::fetchMap()
{
    if (m_tearingDown || m_mapReply)
        return;
    if (!m_mapLabel)
        return;

    const double lat = doubleSetting(QStringLiteral("lat"), 39.5);
    const double lon = doubleSetting(QStringLiteral("lon"), -98.35);
    const int zoom = intSetting(QStringLiteral("zoom"), kDefaultZoom);

    int w = m_mapLabel->width();
    if (w <= 0)
        w = 320;
    int h = intSetting(QStringLiteral("image_height"), kDefaultImageHeight);

    const BBox bb = bboxFromLatLonZoom(lat, lon, zoom, w, h);

    QUrl url(QString::fromLatin1(kIemMapUrl));
    QUrlQuery q;
    for (const QString &layer : layers())
        q.addQueryItem(QStringLiteral("layers[]"), layer);
    q.addQueryItem(QStringLiteral("bbox"), QStringLiteral("%1,%2,%3,%4")
                   .arg(bb.lonMin, 0, 'f', 4)
                   .arg(bb.latMin, 0, 'f', 4)
                   .arg(bb.lonMax, 0, 'f', 4)
                   .arg(bb.latMax, 0, 'f', 4));
    q.addQueryItem(QStringLiteral("width"), QString::number(w));
    q.addQueryItem(QStringLiteral("height"), QString::number(h));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", kUserAgent);
    req.setTransferTimeout(kFetchTimeoutMs);
    QNetworkReply *reply = m_net.get(req);
    m_mapReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleMapReply(reply);
    });
}

void KrellstormMonitor::handleMapReply(QNetworkReply *reply)
{
    if (m_tearingDown) {
        if (reply) reply->deleteLater();
        return;
    }
    if (!reply) {
        setMapStaleBadge(QStringLiteral("offline"));
        return;
    }

    const QByteArray payload = reply->readAll();
    const QNetworkReply::NetworkError err = reply->error();
    reply->deleteLater();
    if (m_mapReply == reply) m_mapReply = nullptr;

    if (err != QNetworkReply::NoError) {
        setMapStaleBadge(QStringLiteral("offline"));
        return;
    }
    if (payload.size() > kMaxMapBytes) {
        setMapStaleBadge(QStringLiteral("too large"));
        return;
    }
    renderMap(payload);
    m_lastMapAt = QDateTime::currentDateTime();
}

void KrellstormMonitor::renderMap(const QByteArray &png)
{
    if (!m_mapLabel)
        return;
    QPixmap pm;
    if (!pm.loadFromData(png)) {
        setMapStaleBadge(QStringLiteral("bad image"));
        return;
    }
    m_lastMapPixmap = pm;
    const int h = std::max(60, intSetting(QStringLiteral("image_height"),
                                          kDefaultImageHeight));
    m_mapLabel->setPixmap(pm.scaledToHeight(h, Qt::SmoothTransformation));
    m_mapLabel->setText(QString());
}

void KrellstormMonitor::setMapStaleBadge(const QString &text)
{
    if (!m_mapLabel)
        return;
    if (m_lastMapPixmap.isNull()) {
        m_mapLabel->setText(text);
        return;
    }
    QPixmap pm = m_lastMapPixmap;
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QFont f = p.font();
    f.setPointSize(9);
    f.setBold(true);
    p.setFont(f);
    QFontMetrics fm(f);
    const QString badge = QStringLiteral("STALE %1").arg(text.toUpper());
    const int pad = 6;
    const int bw = fm.horizontalAdvance(badge) + pad * 2;
    const int bh = fm.height() + 4;
    QRect r(pm.width() - bw - 6, 6, bw, bh);
    p.fillRect(r, QColor(180, 40, 40, 200));
    p.setPen(Qt::white);
    p.drawText(r, Qt::AlignCenter, badge);
    p.end();
    const int hh = std::max(60, intSetting(QStringLiteral("image_height"),
                                           kDefaultImageHeight));
    m_mapLabel->setPixmap(pm.scaledToHeight(hh, Qt::SmoothTransformation));
}

void KrellstormMonitor::fetchAlerts()
{
    if (m_tearingDown || m_alertReply)
        return;

    const double lat = doubleSetting(QStringLiteral("lat"), 39.5);
    const double lon = doubleSetting(QStringLiteral("lon"), -98.35);

    QUrl url(QString::fromLatin1(kNwsAlertsUrl));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("point"), QStringLiteral("%1,%2")
                   .arg(lat, 0, 'f', 4).arg(lon, 0, 'f', 4));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", kUserAgent);
    req.setRawHeader("Accept", "application/geo+json");
    req.setTransferTimeout(kFetchTimeoutMs);
    QNetworkReply *reply = m_net.get(req);
    m_alertReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleAlertsReply(reply);
    });
}

void KrellstormMonitor::handleAlertsReply(QNetworkReply *reply)
{
    if (m_tearingDown) {
        if (reply) reply->deleteLater();
        return;
    }
    if (!reply)
        return;

    const QByteArray payload = reply->readAll();
    const QNetworkReply::NetworkError err = reply->error();
    reply->deleteLater();
    if (m_alertReply == reply) m_alertReply = nullptr;

    if (err != QNetworkReply::NoError)
        return;
    if (payload.size() > kMaxAlertBytes)
        return;

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject())
        return;
    const QJsonArray features = doc.object().value(QStringLiteral("features")).toArray();
    renderAlerts(features);
    m_lastAlertAt = QDateTime::currentDateTime();
}

void KrellstormMonitor::renderAlerts(const QJsonArray &features)
{
    int highest = kSevNone;
    QString topHeadline;
    QString topExpiry;
    QString topEvent;
    QString topArea;
    QString topId;

    QSet<QString> activeIds;

    for (const QJsonValue &v : features) {
        const QJsonObject f = v.toObject();
        const QJsonObject props = f.value(QStringLiteral("properties")).toObject();
        const QString event = props.value(QStringLiteral("event")).toString();
        const QString severityField = props.value(QStringLiteral("severity")).toString();
        const QString headline = props.value(QStringLiteral("headline")).toString();
        const QString area = props.value(QStringLiteral("areaDesc")).toString();
        const QString expires = props.value(QStringLiteral("expires")).toString();
        const QString id = props.value(QStringLiteral("id")).toString();
        const QJsonObject params = props.value(QStringLiteral("parameters")).toObject();

        const int rank = severityRank(event, severityField, params);
        activeIds.insert(id);

        const int prev = m_seenAlertSeverity.value(id, kSevNone);
        if (rank > prev) {
            maybeNotify(id, rank, event, headline.isEmpty() ? event : headline,
                        area, expires);
            m_seenAlertSeverity.insert(id, rank);
        }

        if (rank > highest) {
            highest = rank;
            topHeadline = headline.isEmpty() ? event : headline;
            topExpiry = expires;
            topEvent = event;
            topArea = area;
            topId = id;
        }
    }

    // Drop seen entries that are no longer active so memory doesn't grow.
    for (auto it = m_seenAlertSeverity.begin(); it != m_seenAlertSeverity.end(); ) {
        if (!activeIds.contains(it.key()))
            it = m_seenAlertSeverity.erase(it);
        else
            ++it;
    }

    m_currentSeverity = highest;
    if (m_chip)
        m_chip->setSeverity(highest, severityShortLabel(highest));

    if (m_headline) {
        if (highest == kSevNone) {
            m_headline->setText(QStringLiteral("no active alerts"));
        } else {
            QString line = topHeadline;
            if (!topArea.isEmpty())
                line = QStringLiteral("%1 (%2)").arg(topEvent, topArea.section(QLatin1Char(';'), 0, 0).trimmed());
            m_headline->setText(line);
        }
    }
    if (m_expiry) {
        if (highest == kSevNone)
            m_expiry->setText(QString());
        else
            m_expiry->setText(formatExpiry(topExpiry));
    }

    applyThemeColors();
}

void KrellstormMonitor::maybeNotify(const QString &alertId,
                                    int severityRank,
                                    const QString &event,
                                    const QString &headline,
                                    const QString &area,
                                    const QString &expiresIso)
{
    Q_UNUSED(alertId);
    if (severityRank < kSevWatch)
        return;

    const QDateTime now = QDateTime::currentDateTime();
    if (m_snoozeUntil.isValid() && now < m_snoozeUntil)
        return;

    if (boolSetting(QStringLiteral("notify_desktop"), true)) {
        const QString title = QStringLiteral("%1 — %2").arg(severityShortLabel(severityRank), event);
        QString body = headline;
        const QString exp = formatExpiry(expiresIso);
        if (!area.isEmpty())
            body = QStringLiteral("%1\n%2").arg(area.section(QLatin1Char(';'), 0, 0).trimmed(), body);
        if (!exp.isEmpty())
            body = QStringLiteral("%1\n%2").arg(body, exp);
        desktopNotify(title, body, severityRank);
    }
    if (boolSetting(QStringLiteral("notify_sound"), false))
        playSoundForSeverity(severityRank);
}

void KrellstormMonitor::playSoundForSeverity(int severityRank)
{
    const QDateTime now = QDateTime::currentDateTime();
    const int gap = std::max(0, intSetting(QStringLiteral("min_sound_gap_s"),
                                           kDefaultMinSoundGapS));
    if (m_lastSoundAt.isValid() && m_lastSoundAt.secsTo(now) < gap)
        return;

    QString path;
    if (severityRank >= kSevUrgent)
        path = stringSetting(QStringLiteral("sound_urgent"), QString());
    else if (severityRank >= kSevWarning)
        path = stringSetting(QStringLiteral("sound_warning"), QString());
    else
        path = stringSetting(QStringLiteral("sound_watch"), QString());

    if (path.isEmpty())
        return;
    playWav(path);
    m_lastSoundAt = now;
}

void KrellstormMonitor::applyThemeColors()
{
    const QColor primary = theme()->textStyle(
        QStringLiteral("text_primary")).color;
    const QColor secondary = theme()->textStyle(
        QStringLiteral("text_secondary"),
        QStringLiteral("text_primary")).color;
    const QColor accent = theme()->textStyle(
        QStringLiteral("text_accent"),
        QStringLiteral("text_primary")).color;

    if (m_title) {
        m_title->setStyleSheet(QStringLiteral(
            "font-size: 10px; font-weight: 700; color: %1;")
            .arg(cssColor(accent.isValid() ? accent : primary)));
    }
    if (m_headline) {
        QColor c = primary;
        if (m_currentSeverity >= kSevWarning) {
            const QColor warn = theme()->color(
                QStringLiteral("accent_warning"), c);
            if (warn.isValid()) c = warn;
        }
        m_headline->setStyleSheet(QStringLiteral(
            "font-size: 10px; font-weight: 700; color: %1;")
            .arg(cssColor(c)));
    }
    if (m_expiry) {
        m_expiry->setStyleSheet(QStringLiteral(
            "font-size: 9px; font-weight: 500; color: %1;")
            .arg(cssColor(secondary.isValid() ? secondary : primary)));
    }
}

// ---- KrellstormPlugin -------------------------------------------------------

QString KrellstormPlugin::pluginId() const
{
    return QStringLiteral("io.krellix.krellstorm");
}

QString KrellstormPlugin::pluginName() const
{
    return QStringLiteral("Krellstorm");
}

QString KrellstormPlugin::pluginVersion() const
{
    return QStringLiteral("0.1.0");
}

QList<MonitorBase *> KrellstormPlugin::createMonitors(Theme *theme, QObject *parent)
{
    if (!QSettings().value(QStringLiteral("plugins/krellstorm/enabled"), false).toBool())
        return {};
    return {new KrellstormMonitor(theme, parent)};
}
