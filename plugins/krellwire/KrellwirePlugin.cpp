#include "KrellwirePlugin.h"

#include "theme/Theme.h"
#include "widgets/Panel.h"

#include <QDesktopServices>
#include <QMouseEvent>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QXmlStreamReader>

#include <vector>

namespace {

constexpr int kMaxFeeds = 6;
constexpr qsizetype kMaxFeedBytes = 2 * 1024 * 1024;
constexpr qsizetype kMaxTitleChars = 512;

QString defaultFeed(int index)
{
    switch (index) {
    case 1:
        return QStringLiteral("https://feeds.bbci.co.uk/news/rss.xml");
    case 2:
        return QStringLiteral("https://feeds.npr.org/1001/rss.xml");
    case 3:
        return QStringLiteral("https://rss.nytimes.com/services/xml/rss/nyt/HomePage.xml");
    default:
        return {};
    }
}

QStringList feedUrls()
{
    QSettings s;
    QStringList out;
    for (int i = 1; i <= kMaxFeeds; ++i) {
        const QString url = s.value(QStringLiteral("plugins/krellwire/feed%1").arg(i),
                                    defaultFeed(i)).toString().trimmed();
        if (!url.isEmpty())
            out << url;
    }
    out.removeDuplicates();
    return out;
}

int itemLimit()
{
    return qBound(1, QSettings().value(QStringLiteral("plugins/krellwire/items"),
                                       3).toInt(), 3);
}

int scrollPps()
{
    return qBound(10, QSettings().value(QStringLiteral("plugins/krellwire/scroll_pps"),
                                        28).toInt(), 160);
}

int fieldHeight()
{
    return qBound(14, QSettings().value(QStringLiteral("plugins/krellwire/height"),
                                        24).toInt(), 48);
}

QString normalizedText(QString text)
{
    text.replace(QChar::LineSeparator, QLatin1Char(' '));
    text.replace(QChar::ParagraphSeparator, QLatin1Char(' '));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return text.simplified();
}

bool isAllowedWebUrl(const QUrl &url)
{
    if (!url.isValid() || url.isEmpty() || url.isLocalFile())
        return false;
    const QString scheme = url.scheme().toLower();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https");
}

QUrl resolvedUrl(const QString &raw, const QUrl &base)
{
    const QUrl u(raw.trimmed());
    if (u.isRelative() && base.isValid())
        return base.resolved(u);
    return u;
}

std::vector<KrellwireFeedItem> parseFeed(const QByteArray &payload, const QUrl &baseUrl)
{
    std::vector<KrellwireFeedItem> items;
    QXmlStreamReader xml(payload);
    bool inItem = false;
    bool inEntry = false;
    bool inTitle = false;
    bool inLink = false;
    QString title;
    QString link;

    auto finish = [&]() {
        if (!title.trimmed().isEmpty()) {
            const QUrl url = resolvedUrl(link, baseUrl);
            if (isAllowedWebUrl(url))
                items.push_back(KrellwireFeedItem{
                    normalizedText(title).left(kMaxTitleChars), url});
        }
        title.clear();
        link.clear();
        inItem = false;
        inEntry = false;
        inTitle = false;
        inLink = false;
    };

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const QStringView name = xml.name();
            if (name == QLatin1String("item")) {
                inItem = true;
                title.clear();
                link.clear();
            } else if (name == QLatin1String("entry")) {
                inEntry = true;
                title.clear();
                link.clear();
            } else if ((inItem || inEntry) && name == QLatin1String("title")) {
                inTitle = true;
            } else if ((inItem || inEntry) && name == QLatin1String("link")) {
                if (inEntry) {
                    const auto attrs = xml.attributes();
                    const QString rel = attrs.value(QStringLiteral("rel")).toString();
                    const QString href = attrs.value(QStringLiteral("href")).toString();
                    if (!href.isEmpty() && (rel.isEmpty() || rel == QLatin1String("alternate")))
                        link = href;
                }
                inLink = inItem;
            }
        } else if (xml.isCharacters() || xml.isCDATA()) {
            if (inTitle)
                title += xml.text().toString();
            else if (inLink)
                link += xml.text().toString();
        } else if (xml.isEndElement()) {
            const QStringView name = xml.name();
            if (name == QLatin1String("title")) {
                inTitle = false;
            } else if (name == QLatin1String("link")) {
                inLink = false;
            } else if ((inItem && name == QLatin1String("item"))
                       || (inEntry && name == QLatin1String("entry"))) {
                finish();
                if (items.size() >= static_cast<size_t>(itemLimit()))
                    break;
            }
        }
    }

    return items;
}

class KrellwireTicker : public QWidget
{
public:
    explicit KrellwireTicker(Theme *theme, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_theme(theme)
        , m_timer(new QTimer(this))
    {
        setFixedHeight(fieldHeight());
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
        m_timer->setTimerType(Qt::CoarseTimer);
        m_timer->setInterval(33);
        connect(m_timer, &QTimer::timeout, this, [this]() {
            const qreal step = scrollPps() * (m_timer->interval() / 1000.0);
            m_offset -= step;
            const int textWidth = m_textWidth > 0 ? m_textWidth : width();
            if (m_offset < -textWidth)
                m_offset = width();
            update();
        });
        m_timer->start();
    }

    void setItems(const std::vector<KrellwireFeedItem> &items)
    {
        m_items = items;
        rebuildText();
        m_offset = width();
        update();
    }

protected:
    void resizeEvent(QResizeEvent *) override
    {
        if (m_offset == 0)
            m_offset = width();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        const int x = qRound(event->position().x() - m_offset);
        for (const Segment &segment : m_segments) {
            if (x >= segment.start && x <= segment.end && segment.url.isValid()) {
                if (isAllowedWebUrl(segment.url))
                    QDesktopServices::openUrl(segment.url);
                return;
            }
        }
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::TextAntialiasing, true);

        const QRect r = rect();
        p.fillRect(r, QColor(0, 0, 0, 20));
        p.setClipRect(r.adjusted(2, 0, -2, 0));

        QFont f = font();
        f.setPointSize(qMax(7, f.pointSize() - 1));
        f.setBold(true);
        p.setFont(f);
        const QColor color = m_theme
            ? m_theme->color(QStringLiteral("text_primary"))
            : QColor(Qt::white);
        p.setPen(QColor(0, 0, 0, 170));
        p.drawText(QPointF(m_offset + 1, baseline() + 1), m_text);
        p.setPen(color);
        p.drawText(QPointF(m_offset, baseline()), m_text);
    }

private:
    struct Segment {
        int start = 0;
        int end = 0;
        QUrl url;
    };

    int baseline() const
    {
        const QFontMetrics fm(font());
        return (height() + fm.ascent() - fm.descent()) / 2;
    }

    void rebuildText()
    {
        m_text.clear();
        m_segments.clear();
        const QFontMetrics fm(font());

        if (m_items.empty()) {
            m_text = QStringLiteral("No RSS items");
            m_textWidth = fm.horizontalAdvance(m_text);
            return;
        }

        const QString sep = QStringLiteral("   |   ");
        for (size_t i = 0; i < m_items.size(); ++i) {
            if (i > 0)
                m_text += sep;
            Segment segment;
            segment.start = fm.horizontalAdvance(m_text);
            m_text += m_items.at(i).title;
            segment.end = fm.horizontalAdvance(m_text);
            segment.url = m_items.at(i).url;
            m_segments.append(segment);
        }
        m_textWidth = fm.horizontalAdvance(m_text);
    }

    Theme *m_theme = nullptr;
    QTimer *m_timer = nullptr;
    std::vector<KrellwireFeedItem> m_items;
    QString m_text = QStringLiteral("Loading RSS...");
    QList<Segment> m_segments;
    qreal m_offset = 0;
    int m_textWidth = 0;
};

} // namespace

KrellwireMonitor::KrellwireMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
}

KrellwireMonitor::~KrellwireMonitor()
{
    shutdown();
}

QString KrellwireMonitor::id() const
{
    return QStringLiteral("krellwire");
}

QString KrellwireMonitor::displayName() const
{
    return QStringLiteral("Krellwire");
}

int KrellwireMonitor::tickIntervalMs() const
{
    return QSettings().value(QStringLiteral("plugins/krellwire/interval_ms"),
                             600000).toInt();
}

QWidget *KrellwireMonitor::createWidget(QWidget *parent)
{
    auto *panel = new Panel(theme(), parent);
    panel->setSurfaceKey(QStringLiteral("panel_bg_krellwire"));

    auto *body = new QWidget(panel);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(2, 1, 2, 1);
    layout->setSpacing(0);

    m_ticker = new KrellwireTicker(theme(), body);
    layout->addWidget(m_ticker.data());
    panel->addWidget(body);

    fetch();
    return panel;
}

void KrellwireMonitor::tick()
{
    fetch();
}

void KrellwireMonitor::shutdown()
{
    m_tearingDown = true;
    cancelReplies();
    m_ticker = nullptr;
}

void KrellwireMonitor::fetch()
{
    if (m_tearingDown || m_fetching)
        return;

    const QStringList urls = feedUrls();
    if (urls.isEmpty()) {
        if (m_ticker)
            static_cast<KrellwireTicker *>(m_ticker.data())->setItems({});
        return;
    }

    m_fetching = true;
    m_items.clear();
    for (const QString &raw : urls) {
        const QUrl url(raw);
        if (!isAllowedWebUrl(url))
            continue;
        QNetworkRequest req(url);
        req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
        req.setMaximumRedirectsAllowed(3);
        req.setTransferTimeout(15000);
        req.setRawHeader("User-Agent", "krellix-krellwire/0.1");
        QNetworkReply *reply = m_net.get(req);
        m_replies.append(reply);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            handleReply(reply);
        });
    }

    if (m_replies.isEmpty())
        m_fetching = false;
}

void KrellwireMonitor::handleReply(QNetworkReply *reply)
{
    if (m_tearingDown) {
        if (reply) reply->deleteLater();
        return;
    }

    if (!reply)
        return;

    const QUrl sourceUrl = reply->url();
    const QByteArray payload = reply->readAll();
    const bool ok = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();

    for (int i = m_replies.size() - 1; i >= 0; --i) {
        if (m_replies.at(i) == reply)
            m_replies.removeAt(i);
    }

    if (ok && payload.size() <= kMaxFeedBytes) {
        const std::vector<KrellwireFeedItem> parsed = parseFeed(payload, sourceUrl);
        for (const KrellwireFeedItem &item : parsed) {
            if (!item.title.isEmpty() && isAllowedWebUrl(item.url))
                m_items.push_back(item);
        }
    }

    if (m_replies.isEmpty()) {
        m_fetching = false;
        publishItems();
    }
}

void KrellwireMonitor::cancelReplies()
{
    m_fetching = false;
    for (const QPointer<QNetworkReply> &replyPtr : m_replies) {
        QNetworkReply *reply = replyPtr.data();
        if (!reply)
            continue;
        disconnect(reply, nullptr, this, nullptr);
        if (reply->isRunning())
            reply->abort();
        reply->deleteLater();
    }
    m_replies.clear();
}

void KrellwireMonitor::publishItems()
{
    if (!m_ticker)
        return;
    static_cast<KrellwireTicker *>(m_ticker.data())->setItems(m_items);
}

QString KrellwirePlugin::pluginId() const
{
    return QStringLiteral("krellwire");
}

QString KrellwirePlugin::pluginName() const
{
    return QStringLiteral("Krellwire");
}

QString KrellwirePlugin::pluginVersion() const
{
    return QStringLiteral("0.1.0");
}

QList<MonitorBase *> KrellwirePlugin::createMonitors(Theme *theme, QObject *parent)
{
    if (!QSettings().value(QStringLiteral("plugins/krellwire/enabled"), true).toBool())
        return {};
    return {new KrellwireMonitor(theme, parent)};
}
