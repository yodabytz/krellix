#include "KrellstockPlugin.h"

#include "theme/Theme.h"
#include "widgets/Decal.h"
#include "widgets/Panel.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <cmath>

// ── Helpers ───────────────────────────────────────────────────────────────────

namespace {

constexpr int kMaxSymbols   = 10;
constexpr int kScrollPps    = 40;      // pixels/second
constexpr int kTickMs       = 50;      // scroll timer interval
constexpr int kGapPx        = 80;      // gap between end and restart of ticker
constexpr int kQuoteAdvanceMs = 4000;
constexpr int kQuoteSlideMs   = 360;
constexpr int kDefaultIntervalMs = 300000;  // 5 minutes
constexpr int kFetchTimeoutMs = 3500;

QStringList configuredSymbols()
{
    QSettings s;
    QStringList out;
    for (int i = 1; i <= kMaxSymbols; ++i) {
        const QString raw = s.value(
            QStringLiteral("plugins/krellstock/symbol%1").arg(i)).toString().trimmed().toUpper();
        const QStringList parts = raw.split(
            QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            if (!out.contains(part))
                out << part;
        }
    }
    if (out.isEmpty()) {
        out << QStringLiteral("AAPL")
            << QStringLiteral("MSFT")
            << QStringLiteral("BTC-USD");
    }
    return out;
}

// Display-ready symbol: strip "-USD"/"-EUR" suffix from crypto, cap length.
QString displaySym(const QString &raw)
{
    QString s = raw;
    const int dash = s.indexOf(QLatin1Char('-'));
    if (dash > 0 && dash <= 5)
        s = s.left(dash);
    return s.left(7);
}

// Currency-prefixed, locale-formatted price.
QString formatPrice(double price, const QString &currency)
{
    static const QHash<QString, QString> kPrefix = {
        {QStringLiteral("USD"), QStringLiteral("$")},
        {QStringLiteral("EUR"), QStringLiteral("€")},
        {QStringLiteral("GBP"), QStringLiteral("£")},
        {QStringLiteral("GBp"), QStringLiteral("p")},
        {QStringLiteral("JPY"), QStringLiteral("¥")},
        {QStringLiteral("CNY"), QStringLiteral("¥")},
        {QStringLiteral("HKD"), QStringLiteral("HK$")},
        {QStringLiteral("CAD"), QStringLiteral("CA$")},
        {QStringLiteral("AUD"), QStringLiteral("A$")},
        {QStringLiteral("CHF"), QStringLiteral("Fr")},
        {QStringLiteral("KRW"), QStringLiteral("₩")},
        {QStringLiteral("INR"), QStringLiteral("₹")},
        {QStringLiteral("SEK"), QStringLiteral("kr")},
        {QStringLiteral("NOK"), QStringLiteral("kr")},
        {QStringLiteral("DKK"), QStringLiteral("kr")},
    };
    const QString prefix = kPrefix.value(currency, currency + QStringLiteral(" "));
    int decimals = 2;
    if (price >= 10000)     decimals = 0;
    else if (price >= 1000) decimals = 0;
    else if (price < 0.01)  decimals = 6;
    else if (price < 1)     decimals = 4;
    return prefix + QLocale().toString(price, 'f', decimals);
}

// Signed change string with appropriate decimal places.
QString formatChange(double change, double refPrice)
{
    int dec = 2;
    if (refPrice >= 1000)   dec = 0;
    else if (refPrice < 1)  dec = 4;
    const QString sign = change >= 0 ? QStringLiteral("+") : QString();
    return sign + QLocale().toString(change, 'f', dec);
}

// Colour for a change value using theme keys.
QColor changeColor(double pct, Theme *theme)
{
    if (pct > 0.005)
        return theme->color(QStringLiteral("accent_ok"),       QColor(0x44cc44));
    if (pct < -0.005)
        return theme->color(QStringLiteral("accent_critical"), QColor(0xdd4444));
    return theme->color(QStringLiteral("text_secondary"), QColor(0x888888));
}

QString changeArrow(double pct)
{
    if (pct > 0.005) return QStringLiteral("▲");
    if (pct < -0.005) return QStringLiteral("▼");
    return QStringLiteral("◆");
}

QString quoteTooltipText(const QList<StockQuote> &quotes, const QString &placeholder)
{
    if (quotes.isEmpty())
        return placeholder;

    QStringList lines;
    lines.reserve(quotes.size());
    for (const StockQuote &q : quotes) {
        const QString pct = QStringLiteral("%1%2%")
            .arg(q.changePct >= 0 ? QStringLiteral("+") : QString())
            .arg(q.changePct, 0, 'f', 2);
        QString line = QStringLiteral("%1  %2  %3 %4")
            .arg(displaySym(q.symbol),
                 formatPrice(q.price, q.currency),
                 changeArrow(q.changePct),
                 pct);
        if (!q.shortName.isEmpty())
            line += QStringLiteral("  - ") + q.shortName;
        lines << line;
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace

// ── KrellstockFetcher ─────────────────────────────────────────────────────────

KrellstockFetcher::KrellstockFetcher(QObject *parent) : QObject(parent)
{
    connect(&m_net, &QNetworkAccessManager::finished,
            this, &KrellstockFetcher::onReplyFinished);
}

KrellstockFetcher::~KrellstockFetcher()
{
    for (auto it = m_inFlight.keyBegin(); it != m_inFlight.keyEnd(); ++it) {
        QNetworkReply *r = *it;
        if (r) { r->abort(); r->deleteLater(); }
    }
}

void KrellstockFetcher::requestFetch(const QStringList &symbols)
{
    if (m_fetching || symbols.isEmpty()) return;
    m_fetching = true;
    m_building.clear();
    m_requestedOrder.clear();
    for (const QString &rawSymbol : symbols) {
        const QString symbol = rawSymbol.trimmed().toUpper();
        if (!symbol.isEmpty() && !m_requestedOrder.contains(symbol))
            m_requestedOrder << symbol;
    }
    m_pending = m_requestedOrder;
    m_inFlight.clear();

    // Yahoo's batch quote endpoint currently rejects anonymous clients from
    // some networks. Use chart metadata directly, but publish each response as
    // it lands so one slow symbol cannot make the whole panel look stuck.
    startNext();
}

void KrellstockFetcher::startNext()
{
    if (m_pending.isEmpty()) return;
    const QStringList symbols = m_pending;
    m_pending.clear();
    startChartFallback(symbols);
}

void KrellstockFetcher::startChartFallback(const QStringList &symbols)
{
    for (const QString &rawSymbol : symbols) {
        const QString symbol = rawSymbol.trimmed().toUpper();
        if (symbol.isEmpty())
            continue;

        const QString escaped =
            QString::fromLatin1(QUrl::toPercentEncoding(symbol));
        QUrl url(QStringLiteral("https://query1.finance.yahoo.com/v8/finance/chart/")
                 + escaped);
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("interval"), QStringLiteral("1d"));
        query.addQueryItem(QStringLiteral("range"), QStringLiteral("5d"));
        url.setQuery(query);

        QNetworkRequest req{url};
        req.setRawHeader("User-Agent",
            "Mozilla/5.0 (X11; Linux x86_64; rv:125.0) Gecko/20100101 Firefox/125.0");
        req.setRawHeader("Accept",          "application/json,*/*;q=0.9");
        req.setRawHeader("Accept-Language", "en-US,en;q=0.9");
        req.setRawHeader("Referer",         "https://finance.yahoo.com/quote/"
            + symbol.toUtf8());
        req.setTransferTimeout(kFetchTimeoutMs);
        req.setMaximumRedirectsAllowed(3);

        QNetworkReply *reply = m_net.get(req);
        m_inFlight.insert(reply, QStringLiteral("chart:") + symbol);
    }
}

void KrellstockFetcher::onReplyFinished(QNetworkReply *reply)
{
    const QString requestId = m_inFlight.take(reply);
    const bool isChart = requestId.startsWith(QStringLiteral("chart:"));
    const QString requested = isChart
        ? requestId.mid(6)
        : requestId.mid(requestId.startsWith(QStringLiteral("quote:")) ? 6 : 0);
    const QStringList requestedOrder = requested.split(QLatin1Char(','), Qt::SkipEmptyParts);
    const QByteArray data = reply->readAll();
    const bool ok = (reply->error() == QNetworkReply::NoError);
    reply->deleteLater();

    bool gotQuote = false;
    if (ok && !data.isEmpty()) {
        QJsonParseError perr;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
        if (perr.error == QJsonParseError::NoError && !isChart) {
            const QJsonArray quotes = doc.object()
                .value(QStringLiteral("quoteResponse")).toObject()
                .value(QStringLiteral("result")).toArray();
            QHash<QString, StockQuote> bySymbol;
            for (const QJsonValue &value : quotes) {
                const QJsonObject obj = value.toObject();
                const QString symbol = obj.value(QStringLiteral("symbol")).toString().toUpper();
                const double price = obj.value(QStringLiteral("regularMarketPrice")).toDouble();
                if (symbol.isEmpty() || price <= 0)
                    continue;

                const double prevClose =
                    obj.value(QStringLiteral("regularMarketPreviousClose")).toDouble();
                double change =
                    obj.value(QStringLiteral("regularMarketChange")).toDouble();
                double changePct =
                    obj.value(QStringLiteral("regularMarketChangePercent")).toDouble();
                if (change == 0 && prevClose > 0)
                    change = price - prevClose;
                if (changePct == 0 && prevClose > 0)
                    changePct = (price - prevClose) / prevClose * 100.0;

                StockQuote q;
                q.symbol    = symbol;
                q.shortName = obj.value(QStringLiteral("shortName")).toString().left(30);
                if (q.shortName.isEmpty())
                    q.shortName = obj.value(QStringLiteral("longName")).toString().left(30);
                q.price     = price;
                q.change    = change;
                q.changePct = changePct;
                q.currency  = obj.value(QStringLiteral("currency")).toString();
                q.marketState = obj.value(QStringLiteral("marketState")).toString();
                q.valid     = true;
                bySymbol.insert(symbol, q);
                gotQuote = true;
            }

            for (const QString &symbol : requestedOrder) {
                const QString key = symbol.trimmed().toUpper();
                if (bySymbol.contains(key))
                    m_building << bySymbol.value(key);
            }
        } else if (perr.error == QJsonParseError::NoError && isChart) {
            const QJsonArray results = doc.object()
                .value(QStringLiteral("chart")).toObject()
                .value(QStringLiteral("result")).toArray();
            if (!results.isEmpty()) {
                const QJsonObject result = results.first().toObject();
                const QJsonObject meta =
                    result.value(QStringLiteral("meta")).toObject();
                const double price =
                    meta.value(QStringLiteral("regularMarketPrice")).toDouble();
                if (price > 0) {
                    double prevClose =
                        meta.value(QStringLiteral("chartPreviousClose")).toDouble();
                    if (prevClose <= 0) {
                        const QJsonArray quoteBlocks = result
                            .value(QStringLiteral("indicators")).toObject()
                            .value(QStringLiteral("quote")).toArray();
                        const QJsonArray closes = quoteBlocks.isEmpty()
                            ? QJsonArray()
                            : quoteBlocks.at(0).toObject()
                            .value(QStringLiteral("close")).toArray();
                        for (int i = closes.size() - 2; i >= 0; --i) {
                            const double close = closes.at(i).toDouble();
                            if (close > 0) {
                                prevClose = close;
                                break;
                            }
                        }
                    }

                    StockQuote q;
                    q.symbol = meta.value(QStringLiteral("symbol")).toString()
                        .toUpper();
                    if (q.symbol.isEmpty())
                        q.symbol = requested.trimmed().toUpper();
                    q.shortName =
                        meta.value(QStringLiteral("shortName")).toString().left(30);
                    if (q.shortName.isEmpty())
                        q.shortName =
                            meta.value(QStringLiteral("longName")).toString().left(30);
                    q.price = price;
                    q.change = prevClose > 0 ? price - prevClose : 0;
                    q.changePct = prevClose > 0
                        ? (price - prevClose) / prevClose * 100.0
                        : 0;
                    q.currency =
                        meta.value(QStringLiteral("currency")).toString();
                    q.marketState =
                        meta.value(QStringLiteral("marketState")).toString();
                    q.valid = true;
                    m_building << q;
                    gotQuote = true;
                }
            }
        }
    }

    if (gotQuote)
        publishBuilding();

    if (!isChart && !gotQuote) {
        startChartFallback(requestedOrder);
        if (!m_inFlight.isEmpty())
            return;
    }

    // All in-flight done: publish results.
    if (m_inFlight.isEmpty() && m_pending.isEmpty()) {
        m_fetching = false;
        publishBuilding();
    }
}

void KrellstockFetcher::publishBuilding()
{
    if (m_building.isEmpty())
        return;

    QHash<QString, StockQuote> bySymbol;
    for (const StockQuote &q : m_building) {
        if (!q.symbol.isEmpty())
            bySymbol.insert(q.symbol.toUpper(), q);
    }

    QList<StockQuote> ordered;
    ordered.reserve(m_building.size());
    for (const QString &symbol : m_requestedOrder) {
        const QString key = symbol.trimmed().toUpper();
        if (bySymbol.contains(key))
            ordered << bySymbol.value(key);
    }
    for (const StockQuote &q : m_building) {
        bool alreadyAdded = false;
        for (const StockQuote &existing : ordered) {
            if (existing.symbol.compare(q.symbol, Qt::CaseInsensitive) == 0) {
                alreadyAdded = true;
                break;
            }
        }
        if (!alreadyAdded)
            ordered << q;
    }

    m_lastQuotes = ordered;
    emit quotesReady(m_lastQuotes);
}

// ── KrellstockTicker ──────────────────────────────────────────────────────────

KrellstockTicker::KrellstockTicker(Theme *theme, QWidget *parent)
    : QWidget(parent)
    , m_theme(theme)
    , m_timer(new QTimer(this))
    , m_placeholder(QStringLiteral("Fetching quotes…"))
{
    Q_ASSERT(m_theme);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    onThemeChanged();

    m_timer->setInterval(kTickMs);
    m_timer->setTimerType(Qt::CoarseTimer);
    connect(m_timer, &QTimer::timeout, this, &KrellstockTicker::onTick);
    connect(m_theme, &Theme::themeChanged, this, &KrellstockTicker::onThemeChanged);
}

void KrellstockTicker::setQuotes(const QList<StockQuote> &quotes)
{
    m_quotes = quotes;
    setToolTip(quoteTooltipText(m_quotes, m_placeholder));
    rebuildSegs();
    update();
}

void KrellstockTicker::setPlaceholder(const QString &text)
{
    m_placeholder = text;
    setToolTip(quoteTooltipText(m_quotes, m_placeholder));
    if (m_quotes.isEmpty()) { rebuildSegs(); update(); }
}

void KrellstockTicker::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    m_clock.start();
    m_timer->start();
}

void KrellstockTicker::hideEvent(QHideEvent *e)
{
    m_timer->stop();
    QWidget::hideEvent(e);
}

void KrellstockTicker::onThemeChanged()
{
    const QFont f = m_theme->font(QStringLiteral("label"));
    setFixedHeight(qBound(16, QFontMetrics(f).height() + 6, 30));
    rebuildSegs();
    update();
}

void KrellstockTicker::onTick()
{
    if (m_totalW <= 0) return;
    const qint64 elapsed = m_clock.isValid() ? m_clock.restart() : kTickMs;
    if (!m_clock.isValid()) m_clock.start();
    const double step = kScrollPps * elapsed / 1000.0;
    m_offset -= step;
    if (m_offset < -m_totalW)
        m_offset = static_cast<double>(width());
    update();
}

void KrellstockTicker::resizeEvent(QResizeEvent *)
{
    if (m_offset == 0)
        m_offset = static_cast<double>(width());
}

void KrellstockTicker::rebuildSegs()
{
    m_segs.clear();

    const QFont base = m_theme->font(QStringLiteral("label"));
    QFont bold = base; bold.setBold(true);
    const QFontMetrics bfm(bold), fm(base);

    const QColor cPrimary = m_theme->color(QStringLiteral("text_primary"),   QColor(Qt::white));
    const QColor cDim     = m_theme->color(QStringLiteral("text_secondary"),  QColor(0x888888));
    const QColor cGreen   = m_theme->color(QStringLiteral("accent_ok"),       QColor(0x44cc44));
    const QColor cRed     = m_theme->color(QStringLiteral("accent_critical"), QColor(0xdd4444));

    auto addSeg = [&](const QString &text, const QColor &color, bool isBold) {
        if (text.isEmpty()) return;
        Seg s;
        s.text = text;
        s.color = color;
        s.bold = isBold;
        s.pixelWidth = isBold ? bfm.horizontalAdvance(text) : fm.horizontalAdvance(text);
        m_segs << s;
    };

    if (m_quotes.isEmpty()) {
        addSeg(m_placeholder, cDim, false);
        m_totalW = m_segs.isEmpty() ? 0 : m_segs.first().pixelWidth;
        return;
    }

    const QString sep = QStringLiteral("    ·    ");

    for (int i = 0; i < m_quotes.size(); ++i) {
        const StockQuote &q = m_quotes.at(i);
        if (i > 0) addSeg(sep, cDim, false);

        const QColor cc = (q.changePct > 0.005) ? cGreen : (q.changePct < -0.005 ? cRed : cDim);
        const QString arrow = changeArrow(q.changePct);
        const QString pct   = QStringLiteral("%1%2%")
            .arg(q.changePct >= 0 ? QStringLiteral("+") : QString())
            .arg(q.changePct, 0, 'f', 2);
        const QString chg   = formatChange(q.change, q.price);

        // [SYM]  [PRICE]  [▲ +1.20  +0.64%]
        addSeg(displaySym(q.symbol),     cPrimary, true);
        addSeg(QStringLiteral("  "),     cDim,     false);
        addSeg(formatPrice(q.price, q.currency), cPrimary, false);
        addSeg(QStringLiteral("  "),     cDim,     false);
        addSeg(arrow + QStringLiteral(" ") + chg + QStringLiteral("  ") + pct, cc, false);
    }

    m_totalW = 0;
    for (const Seg &s : m_segs) m_totalW += s.pixelWidth;
}

void KrellstockTicker::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QRect r = rect();
    p.fillRect(r, QColor(0, 0, 0, 18));
    p.setClipRect(r.adjusted(2, 0, -2, 0));

    if (m_segs.isEmpty()) return;

    const QFont base = m_theme->font(QStringLiteral("label"));
    QFont bold = base; bold.setBold(true);
    const QFontMetrics fm(base);
    const int yBase = (r.height() + fm.ascent() - fm.descent()) / 2;

    const int loop = m_totalW + kGapPx;

    for (int pass = 0; pass < 2; ++pass) {
        double x = m_offset + pass * loop;
        if (x > r.width()) break;
        for (const Seg &seg : m_segs) {
            if (x + seg.pixelWidth < 0) { x += seg.pixelWidth; continue; }
            if (x > r.width()) break;
            p.setFont(seg.bold ? bold : base);
            p.setPen(seg.color);
            p.drawText(QPointF(x, yBase), seg.text);
            x += seg.pixelWidth;
        }
    }
}

// ── KrellstockQuoteWidget ─────────────────────────────────────────────────────

KrellstockQuoteWidget::KrellstockQuoteWidget(Theme *theme, QWidget *parent)
    : QWidget(parent)
    , m_theme(theme)
    , m_advanceTimer(new QTimer(this))
    , m_slideTimer(new QTimer(this))
    , m_placeholder(QStringLiteral("Fetching quotes…"))
{
    Q_ASSERT(m_theme);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    m_advanceTimer->setInterval(kQuoteAdvanceMs);
    m_advanceTimer->setSingleShot(true);
    m_advanceTimer->setTimerType(Qt::CoarseTimer);
    m_slideTimer->setInterval(kTickMs);
    m_slideTimer->setTimerType(Qt::CoarseTimer);
    connect(m_advanceTimer, &QTimer::timeout, this, &KrellstockQuoteWidget::onAdvance);
    connect(m_slideTimer, &QTimer::timeout, this, &KrellstockQuoteWidget::onSlideTick);
    connect(m_theme, &Theme::themeChanged, this, &KrellstockQuoteWidget::onThemeChanged);
    onThemeChanged();
    updateQuoteTooltip();
}

void KrellstockQuoteWidget::onThemeChanged()
{
    const QFont f = m_theme->font(QStringLiteral("label"));
    const int fontH = QFontMetrics(f).height();
    m_rowH = qBound(22, fontH * 2 + 3, 42);
    updateGeometry();
    update();
}

void KrellstockQuoteWidget::setQuotes(const QList<StockQuote> &quotes)
{
    const QString currentSymbol = (m_currentQuote >= 0 && m_currentQuote < m_quotes.size())
        ? m_quotes.at(m_currentQuote).symbol
        : QString();
    m_quotes = quotes;
    if (!currentSymbol.isEmpty()) {
        for (int i = 0; i < m_quotes.size(); ++i) {
            if (m_quotes.at(i).symbol.compare(currentSymbol, Qt::CaseInsensitive) == 0) {
                m_currentQuote = i;
                break;
            }
        }
    }
    if (m_currentQuote < 0 || m_currentQuote >= m_quotes.size())
        m_currentQuote = 0;
    if (m_quotes.size() <= 1) {
        m_nextQuote = -1;
        m_slideOffset = 0;
        m_advanceTimer->stop();
        m_slideTimer->stop();
    }
    updateQuoteTooltip();
    updateTimerState();
    updateGeometry();
    update();
}

void KrellstockQuoteWidget::setPlaceholder(const QString &text)
{
    m_placeholder = text;
    updateQuoteTooltip();
    if (m_quotes.isEmpty()) update();
}

QSize KrellstockQuoteWidget::sizeHint() const
{
    return QSize(80, m_rowH + 2);
}

QSize KrellstockQuoteWidget::minimumSizeHint() const
{
    return QSize(0, m_rowH);
}

void KrellstockQuoteWidget::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    updateTimerState();
}

void KrellstockQuoteWidget::hideEvent(QHideEvent *e)
{
    m_advanceTimer->stop();
    m_slideTimer->stop();
    QWidget::hideEvent(e);
}

void KrellstockQuoteWidget::onAdvance()
{
    if (m_quotes.size() <= 1 || m_slideTimer->isActive())
        return;

    m_nextQuote = (m_currentQuote + 1) % m_quotes.size();
    m_slideOffset = 0;
    m_slideClock.restart();
    m_slideTimer->start();
}

void KrellstockQuoteWidget::onSlideTick()
{
    if (m_nextQuote < 0 || m_quotes.size() <= 1) {
        m_slideTimer->stop();
        m_slideOffset = 0;
        update();
        return;
    }

    const int elapsed = static_cast<int>(m_slideClock.elapsed());
    if (elapsed >= kQuoteSlideMs) {
        m_currentQuote = m_nextQuote;
        m_nextQuote = -1;
        m_slideOffset = 0;
        m_slideTimer->stop();
        update();
        scheduleNextQuote();
        return;
    }

    m_slideOffset = qBound(0, elapsed * m_rowH / kQuoteSlideMs, m_rowH);
    update();
}

void KrellstockQuoteWidget::scheduleNextQuote()
{
    if (!isVisible() || m_quotes.size() <= 1 || m_slideTimer->isActive()) {
        m_advanceTimer->stop();
        return;
    }
    if (!m_advanceTimer->isActive())
        m_advanceTimer->start(kQuoteAdvanceMs);
}

void KrellstockQuoteWidget::updateTimerState()
{
    scheduleNextQuote();
}

void KrellstockQuoteWidget::updateQuoteTooltip()
{
    setToolTip(quoteTooltipText(m_quotes, m_placeholder));
}

void KrellstockQuoteWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QFont base = m_theme->font(QStringLiteral("label"));
    QFont bold = base; bold.setBold(true);
    const QFontMetrics bfm(bold);
    const QFontMetrics fm(base);

    const QColor cPrimary = m_theme->color(QStringLiteral("text_primary"),   QColor(Qt::white));
    const QColor cDim     = m_theme->color(QStringLiteral("text_secondary"),  QColor(0x888888));

    const int w   = width();
    const int pad = 4;

    if (m_quotes.isEmpty()) {
        p.setFont(base);
        p.setPen(cDim);
        p.drawText(rect(), Qt::AlignCenter, m_placeholder);
        return;
    }

    auto drawQuote = [&](const StockQuote &q, const QRect &row, bool tinted) {
        p.save();
        p.setClipRect(row.adjusted(1, 0, -1, 0));

        // Subtle alternating row tint.
        if (tinted) p.fillRect(row, QColor(255, 255, 255, 4));

        const QColor cc    = changeColor(q.changePct, m_theme);
        const QString arrow = changeArrow(q.changePct);
        const QString symText = displaySym(q.symbol);
        const QString priceText = formatPrice(q.price, q.currency);
        const QString pctText = arrow
            + (q.changePct >= 0 ? QStringLiteral("+") : QString())
            + QString::number(q.changePct, 'f', 2)
            + QStringLiteral("%");
        const QString chgText = formatChange(q.change, q.price);

        if (w < 150) {
            const int firstBase = row.top() + fm.ascent() + 1;
            const int secondBase = row.bottom() - fm.descent() - 1;
            p.setFont(bold);
            p.setPen(cPrimary);
            p.drawText(QPoint(pad, firstBase), fm.elidedText(symText, Qt::ElideRight, w / 2));

            p.setFont(base);
            p.setPen(cc);
            const int pctW = fm.horizontalAdvance(pctText);
            p.drawText(QPoint(qMax(pad, w - pad - pctW), firstBase), pctText);

            p.setPen(cPrimary);
            p.drawText(QPoint(pad, secondBase),
                       fm.elidedText(priceText, Qt::ElideRight, qMax(20, w - pad * 2)));
            p.restore();
            return;
        }

        const int yBase = row.top() + (row.height() + fm.ascent() - fm.descent()) / 2;

        // ── Right: percent change  e.g. "▲+0.64%"
        const int pctW = fm.horizontalAdvance(pctText);
        p.setFont(base);
        p.setPen(cc);
        p.drawText(QPoint(w - pad - pctW, yBase), pctText);

        // ── Right of symbol: change amount  e.g. "+1.20"
        const int chgW = fm.horizontalAdvance(chgText);
        p.drawText(QPoint(w - pad - pctW - 6 - chgW, yBase), chgText);

        // ── Left: symbol (bold, primary)
        p.setFont(bold);
        p.setPen(cPrimary);
        p.drawText(QPoint(pad, yBase), symText);
        const int symW = bfm.horizontalAdvance(symText);

        // ── Middle: price — right-aligned in the remaining gap
        const int priceW   = fm.horizontalAdvance(priceText);
        const int gapLeft  = pad + symW + 6;
        const int gapRight = w - pad - pctW - 6 - chgW - 6;
        const int priceX   = gapRight - priceW;
        if (priceX >= gapLeft) {
            p.setFont(base);
            p.setPen(cPrimary);
            p.drawText(QPoint(priceX, yBase), priceText);
        }

        // ── After-hours badge  (PRE / POST) in dim colour, above price gap
        if (q.marketState != QLatin1String("REGULAR") && !q.marketState.isEmpty()) {
            const QString badge = q.marketState.left(4);
            p.setFont(base);
            p.setPen(cDim);
            p.drawText(QPoint(gapLeft, yBase), badge);
        }
        p.restore();
    };

    const int current = qBound(0, m_currentQuote, m_quotes.size() - 1);
    drawQuote(m_quotes.at(current), QRect(0, -m_slideOffset, w, m_rowH), true);
    if (m_nextQuote >= 0 && m_nextQuote < m_quotes.size())
        drawQuote(m_quotes.at(m_nextQuote), QRect(0, m_rowH - m_slideOffset, w, m_rowH), false);
}

// ── KrellstockTickerMonitor ───────────────────────────────────────────────────

KrellstockTickerMonitor::KrellstockTickerMonitor(Theme *theme,
                                                  QSharedPointer<KrellstockFetcher> fetcher,
                                                  QObject *parent)
    : MonitorBase(theme, parent)
    , m_fetcher(std::move(fetcher))
{
    Q_ASSERT(m_fetcher);
    connect(m_fetcher.get(), &KrellstockFetcher::quotesReady,
            this, &KrellstockTickerMonitor::onQuotesReady);
}

int KrellstockTickerMonitor::tickIntervalMs() const
{
    return QSettings().value(QStringLiteral("plugins/krellstock/interval_ms"),
                             kDefaultIntervalMs).toInt();
}

QWidget *KrellstockTickerMonitor::createWidget(QWidget *parent)
{
    auto *panel = new Panel(theme(), parent);
    panel->setSurfaceKey(QStringLiteral("panel_bg_krellstock_ticker"));
    panel->setTitle(QStringLiteral("Stocks"));

    m_ticker = new KrellstockTicker(theme(), panel);
    panel->addWidget(m_ticker);

    // Show cached data immediately if available.
    const auto cached = m_fetcher->lastQuotes();
    if (!cached.isEmpty())
        m_ticker->setQuotes(cached);

    QTimer::singleShot(0, this, &KrellstockTickerMonitor::tick);
    return panel;
}

void KrellstockTickerMonitor::tick()
{
    m_fetcher->requestFetch(configuredSymbols());
}

void KrellstockTickerMonitor::onQuotesReady(const QList<StockQuote> &quotes)
{
    if (m_ticker) m_ticker->setQuotes(quotes);
}

// ── KrellstockQuoteMonitor ────────────────────────────────────────────────────

KrellstockQuoteMonitor::KrellstockQuoteMonitor(Theme *theme,
                                                QSharedPointer<KrellstockFetcher> fetcher,
                                                QObject *parent)
    : MonitorBase(theme, parent)
    , m_fetcher(std::move(fetcher))
{
    Q_ASSERT(m_fetcher);
    connect(m_fetcher.get(), &KrellstockFetcher::quotesReady,
            this, &KrellstockQuoteMonitor::onQuotesReady);
}

int KrellstockQuoteMonitor::tickIntervalMs() const
{
    return QSettings().value(QStringLiteral("plugins/krellstock/interval_ms"),
                             kDefaultIntervalMs).toInt();
}

QWidget *KrellstockQuoteMonitor::createWidget(QWidget *parent)
{
    auto *panel = new Panel(theme(), parent);
    panel->setSurfaceKey(QStringLiteral("panel_bg_krellstock"));
    panel->setTitle(QStringLiteral("Stocks"));

    m_quoteWidget = new KrellstockQuoteWidget(theme(), panel);
    panel->addWidget(m_quoteWidget);

    const auto cached = m_fetcher->lastQuotes();
    if (!cached.isEmpty())
        m_quoteWidget->setQuotes(cached);

    QTimer::singleShot(0, this, &KrellstockQuoteMonitor::tick);
    return panel;
}

void KrellstockQuoteMonitor::tick()
{
    m_fetcher->requestFetch(configuredSymbols());
}

void KrellstockQuoteMonitor::onQuotesReady(const QList<StockQuote> &quotes)
{
    if (m_quoteWidget) m_quoteWidget->setQuotes(quotes);
}

// ── KrellstockPlugin ──────────────────────────────────────────────────────────

QList<MonitorBase *> KrellstockPlugin::createMonitors(Theme *theme, QObject *parent)
{
    QSettings s;
    const bool tickerOn = s.value(
        QStringLiteral("plugins/krellstock/ticker_enabled"), false).toBool();
    const bool quotesOn = s.value(
        QStringLiteral("plugins/krellstock/quotes_enabled"), true).toBool();
    if (!tickerOn && !quotesOn) return {};

    // One fetcher shared between both monitors so one network round-trip feeds both.
    auto fetcher = QSharedPointer<KrellstockFetcher>::create();

    QList<MonitorBase *> out;
    if (tickerOn)
        out << new KrellstockTickerMonitor(theme, fetcher, parent);
    if (quotesOn)
        out << new KrellstockQuoteMonitor(theme, fetcher, parent);
    return out;
}

#include "KrellstockPlugin.moc"
