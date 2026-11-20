#include "KrellwirePlugin.h"

#include "theme/Theme.h"
#include "widgets/Panel.h"

#ifdef Q_OS_WIN
#include "remote/RemoteSource.h"
#include <QApplication>
#endif

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
#ifdef Q_OS_WIN
    if (url.host().isEmpty() || !url.userInfo().isEmpty() || url.toEncoded().size() > 4096) return false;
#endif
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
#ifdef Q_OS_WIN
            const QUrl url = link.trimmed().isEmpty() ? QUrl() : resolvedUrl(link, baseUrl);
#else
            const QUrl url = resolvedUrl(link, baseUrl);
#endif
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
#ifdef Q_OS_WIN
        if (xml.isDTD()) return {};
#endif
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
#ifdef Q_OS_WIN
            if (inTitle && title.size() < kMaxTitleChars)
                title += xml.text().left(kMaxTitleChars - title.size()).toString();
            else if (inLink && link.size() <= 4096)
                link += xml.text().left(4097 - link.size()).toString();
#else
            if (inTitle)
                title += xml.text().toString();
            else if (inLink)
                link += xml.text().toString();
#endif
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
        syncTheme();
#ifdef Q_OS_WIN
        updateAccessibleName();
        m_pps = scrollPps();
#else
        if (m_theme) {
            connect(m_theme, &Theme::themeChanged, this, [this]() {
                syncTheme();
                rebuildText();
                update();
            });
        }
#endif
        m_timer->setTimerType(Qt::CoarseTimer);
        m_timer->setInterval(qBound(33,
            QSettings().value(QStringLiteral("appearance/scroll_interval_ms"), 83).toInt(),
            250));
        connect(m_timer, &QTimer::timeout, this, [this]() {
#ifdef Q_OS_WIN
            if (m_pressedUrl.isValid()) return;
            const qreal step = m_pps * (m_timer->interval() / 1000.0);
#else
            const qreal step = scrollPps() * (m_timer->interval() / 1000.0);
#endif
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

#ifdef Q_OS_WIN
    void setStatus(const QString &status)
    {
        m_emptyText = status;
        setItems({});
    }

    void refreshTheme()
    {
        syncTheme();
        refreshPreferences();
        rebuildText();
        update();
    }

    void refreshPreferences()
    {
        m_pps = scrollPps();
        const int interval = qBound(33,
            QSettings().value(QStringLiteral("appearance/scroll_interval_ms"), 83).toInt(), 250);
        if (m_timer->interval() != interval) m_timer->setInterval(interval);
    }
#endif

protected:
    void resizeEvent(QResizeEvent *) override
    {
        if (m_offset == 0)
            m_offset = width();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
#ifdef Q_OS_WIN
        if (event->button() == Qt::LeftButton) {
            m_pressedUrl = headlineAt(event->position());
            if (m_pressedUrl.isValid()) {
                m_pressPosition = event->position();
                event->accept();
                return;
            }
        }
        QWidget::mousePressEvent(event);
#else
        const int x = qRound(event->position().x() - m_offset);
        for (const Segment &segment : m_segments) {
            if (x >= segment.start && x <= segment.end && segment.url.isValid()) {
                if (isAllowedWebUrl(segment.url))
                    QDesktopServices::openUrl(segment.url);
                return;
            }
        }
#endif
    }

#ifdef Q_OS_WIN
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_pressedUrl.isValid()) {
            const QUrl url = m_pressedUrl;
            m_pressedUrl = QUrl();
            if ((event->position() - m_pressPosition).manhattanLength() < QApplication::startDragDistance()
                && headlineAt(event->position()) == url) {
                QDesktopServices::openUrl(url);
            }
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::Hide)
            m_pressedUrl = QUrl();
        return QWidget::event(event);
    }
#endif

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::TextAntialiasing, true);

        const QRect r = rect();
        p.fillRect(r, QColor(0, 0, 0, 20));
        p.setClipRect(r.adjusted(2, 0, -2, 0));

        p.setFont(font());
        const Theme::TextStyle ts = m_theme
            ? m_theme->textStyle(QStringLiteral("text_primary"))
            : Theme::TextStyle{QColor(Qt::white), {}};
        if (ts.shadow.present) {
            p.setPen(ts.shadow.color);
            p.drawText(QPointF(m_offset + ts.shadow.offsetX,
                               baseline() + ts.shadow.offsetY), m_text);
        }
        p.setPen(ts.color.isValid() ? ts.color : QColor(Qt::white));
        p.drawText(QPointF(m_offset, baseline()), m_text);
    }

private:
#ifdef Q_OS_WIN
    void updateAccessibleName()
    {
        // Windows UI Automation exposes the widget name to screen readers.
        setAccessibleName(QStringLiteral("KrellWire headlines: %1").arg(m_text));
    }

    QUrl headlineAt(const QPointF &position) const
    {
        if (!QRectF(rect().adjusted(2, 0, -2, 0)).contains(position)) return {};
        const auto x = position.x() - m_offset;
        for (const auto &segment : m_segments) {
            if (x >= segment.start && x < segment.end && isAllowedWebUrl(segment.url)) return segment.url;
        }
        return {};
    }
#endif

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

    void syncTheme()
    {
        if (m_theme)
            setFont(m_theme->font(QStringLiteral("label"), QWidget::font()));
        setFixedHeight(fieldHeight());
    }

    void rebuildText()
    {
        m_text.clear();
        m_segments.clear();
        const QFontMetrics fm(font());

        if (m_items.empty()) {
#ifdef Q_OS_WIN
            m_text = m_emptyText;
            updateAccessibleName();
#else
            m_text = QStringLiteral("No RSS items");
#endif
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
#ifdef Q_OS_WIN
        updateAccessibleName();
#endif
    }

    Theme *m_theme = nullptr;
    QTimer *m_timer = nullptr;
    std::vector<KrellwireFeedItem> m_items;
    QString m_text = QStringLiteral("Loading RSS...");
    QList<Segment> m_segments;
    qreal m_offset = 0;
    int m_textWidth = 0;
#ifdef Q_OS_WIN
    QString m_emptyText = QStringLiteral("No RSS items");
    QUrl m_pressedUrl;
    QPointF m_pressPosition;
    int m_pps = 28;
#endif
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
#ifdef Q_OS_WIN
    // Check the deadline independently of the configured request interval.
    return 5000;
#else
    return QSettings().value(QStringLiteral("plugins/krellwire/interval_ms"),
                             600000).toInt();
#endif
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
#ifdef Q_OS_WIN
    // Use the host's metaobject, not the DLL's import of the signal pointer.
    connect(theme(), SIGNAL(themeChanged()), this, SLOT(updateTheme()));
#endif
    layout->addWidget(m_ticker.data());
    panel->addWidget(body);

    fetch();
    return panel;
}

void KrellwireMonitor::tick()
{
#ifdef Q_OS_WIN
    if (auto *ticker = static_cast<KrellwireTicker *>(m_ticker.data()))
        ticker->refreshPreferences();
#endif
    fetch();
}

#ifdef Q_OS_WIN
void KrellwireMonitor::updateTheme()
{
    if (auto *ticker = static_cast<KrellwireTicker *>(m_ticker.data()))
        ticker->refreshTheme();
}
#endif

void KrellwireMonitor::shutdown()
{
    m_tearingDown = true;
    cancelReplies();
    m_ticker = nullptr;
}

void KrellwireMonitor::fetch()
{
#ifdef Q_OS_WIN
    if (m_tearingDown) return;
    const QStringList urls = feedUrls();
    const int limit = itemLimit();
    auto *ticker = static_cast<KrellwireTicker *>(m_ticker.data());
    if (m_requestedUrls != urls || m_requestedLimit != limit) {
        cancelReplies();
        m_requestedAt.invalidate();
        m_requestedUrls = urls;
        m_requestedLimit = limit;
        if (ticker) ticker->setStatus(QStringLiteral("Loading RSS..."));
    }
    const int interval = qBound(60000, QSettings().value(
        QStringLiteral("plugins/krellwire/interval_ms"), 600000).toInt(), 3600000);
    if (m_fetching || (m_requestedAt.isValid() && m_requestedAt.elapsed() < interval)) return;
    m_requestedAt.start();
    m_feedSucceeded = false;
    if (urls.isEmpty()) {
        if (ticker) ticker->setStatus(QStringLiteral("No feeds configured"));
        return;
    }
#else
    if (m_tearingDown || m_fetching)
        return;

    const QStringList urls = feedUrls();
    if (urls.isEmpty()) {
        if (m_ticker)
            static_cast<KrellwireTicker *>(m_ticker.data())->setItems({});
        return;
    }
#endif

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
#ifdef Q_OS_WIN
        reply->setReadBufferSize(64 * 1024);
        QTimer::singleShot(15000, reply, [reply] {
            if (reply->isRunning()) reply->abort();
        });
#endif
        m_replies.append(reply);
        m_payloads.insert(reply, QByteArray());
        connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
            QByteArray &payload = m_payloads[reply];
#ifdef Q_OS_WIN
            if (reply->bytesAvailable() > kMaxFeedBytes - payload.size()) {
                reply->abort();
                return;
            }
#endif
            payload.append(reply->readAll());
            if (payload.size() > kMaxFeedBytes)
                reply->abort();
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            handleReply(reply);
        });
    }

    if (m_replies.isEmpty()) {
        m_fetching = false;
#ifdef Q_OS_WIN
        if (ticker) ticker->setStatus(QStringLiteral("No valid feed URL"));
#endif
    }
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
    QByteArray payload = m_payloads.take(reply);
#ifdef Q_OS_WIN
    const bool ok = reply->error() == QNetworkReply::NoError
        && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200
        && reply->bytesAvailable() <= kMaxFeedBytes - payload.size();
    if (ok) payload.append(reply->readAll());
    m_feedSucceeded |= ok;
#else
    payload.append(reply->readAll());
    const bool ok = reply->error() == QNetworkReply::NoError;
#endif
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
#ifdef Q_OS_WIN
        if (m_items.empty()) {
            if (auto *ticker = static_cast<KrellwireTicker *>(m_ticker.data()))
                ticker->setStatus(m_feedSucceeded ? QStringLiteral("No RSS items")
                                                   : QStringLiteral("Feeds unavailable"));
            return;
        }
#endif
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
    m_payloads.clear();
}

void KrellwireMonitor::publishItems()
{
    auto *ticker = static_cast<KrellwireTicker *>(m_ticker.data());
    if (!ticker)
        return;
    ticker->setItems(m_items);
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
#ifdef Q_OS_WIN
    return QStringLiteral("0.1.0-windows.1");
#else
    return QStringLiteral("0.1.0");
#endif
}

QList<MonitorBase *> KrellwirePlugin::createMonitors(Theme *theme, QObject *parent)
{
#ifdef Q_OS_WIN
    if (RemoteSource::instance()) return {};
#endif
    if (!QSettings().value(QStringLiteral("plugins/krellwire/enabled"), false).toBool())
        return {};
    return {new KrellwireMonitor(theme, parent)};
}
