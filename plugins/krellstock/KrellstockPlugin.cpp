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
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>

// ── Helpers ───────────────────────────────────────────────────────────────────

namespace {

constexpr int kMaxSymbols   = 10;
constexpr int kScrollPps    = 40;      // pixels/second
constexpr int kTickMs       = 50;      // scroll timer interval
constexpr int kGapPx        = 80;      // gap between end and restart of ticker
constexpr int kDefaultIntervalMs = 300000;  // 5 minutes

QStringList configuredSymbols()
{
    QSettings s;
    QStringList out;
    for (int i = 1; i <= kMaxSymbols; ++i) {
        const QString sym = s.value(
            QStringLiteral("plugins/krellstock/symbol%1").arg(i)).toString().trimmed().toUpper();
        if (!sym.isEmpty())
            out << sym;
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

} // namespace

// ── KrellstockFetcher ─────────────────────────────────────────────────────────

KrellstockFetcher::KrellstockFetcher(QObject *parent) : QObject(parent)
{
    connect(&m_net, &QNetworkAccessManager::finished,
            this, &KrellstockFetcher::onFinished);
}

KrellstockFetcher::~KrellstockFetcher()
{
    if (m_reply) { m_reply->abort(); m_reply->deleteLater(); }
}

void KrellstockFetcher::requestFetch(const QStringList &symbols)
{
    if (m_fetching || symbols.isEmpty()) return;

    const QString url = QStringLiteral(
        "https://query1.finance.yahoo.com/v7/finance/quote"
        "?symbols=%1"
        "&fields=shortName,regularMarketPrice,regularMarketChange,"
        "regularMarketChangePercent,currency,marketState"
        "&lang=en-US&region=US&corsDomain=finance.yahoo.com"
    ).arg(symbols.join(QLatin1Char(',')));

    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("User-Agent",
        "Mozilla/5.0 (X11; Linux x86_64; rv:125.0) Gecko/20100101 Firefox/125.0");
    req.setRawHeader("Accept",          "application/json,*/*;q=0.9");
    req.setRawHeader("Accept-Language", "en-US,en;q=0.9");
    req.setRawHeader("Referer",         "https://finance.yahoo.com/");
    req.setTransferTimeout(15000);
    req.setMaximumRedirectsAllowed(3);

    m_fetching = true;
    m_reply = m_net.get(req);
}

void KrellstockFetcher::onFinished(QNetworkReply *reply)
{
    m_fetching = false;
    if (reply != m_reply.data()) { reply->deleteLater(); return; }

    const QByteArray data = reply->readAll();
    const bool ok = (reply->error() == QNetworkReply::NoError);
    reply->deleteLater();
    m_reply = nullptr;

    if (!ok || data.isEmpty()) return;

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
    if (perr.error != QJsonParseError::NoError) return;

    const QJsonArray results = doc.object()
        .value(QStringLiteral("quoteResponse")).toObject()
        .value(QStringLiteral("result")).toArray();

    QList<StockQuote> quotes;
    for (const QJsonValue &val : results) {
        const QJsonObject obj = val.toObject();
        StockQuote q;
        q.symbol     = obj.value(QStringLiteral("symbol")).toString();
        q.shortName  = obj.value(QStringLiteral("shortName")).toString().left(30);
        q.price      = obj.value(QStringLiteral("regularMarketPrice")).toDouble();
        q.change     = obj.value(QStringLiteral("regularMarketChange")).toDouble();
        q.changePct  = obj.value(QStringLiteral("regularMarketChangePercent")).toDouble();
        q.currency   = obj.value(QStringLiteral("currency")).toString();
        q.marketState= obj.value(QStringLiteral("marketState")).toString();
        q.valid      = (q.price > 0);
        if (q.valid) quotes << q;
    }

    if (!quotes.isEmpty()) {
        m_lastQuotes = quotes;
        emit quotesReady(quotes);
    }
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
    rebuildSegs();
    update();
}

void KrellstockTicker::setPlaceholder(const QString &text)
{
    m_placeholder = text;
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
    , m_placeholder(QStringLiteral("Fetching quotes…"))
{
    Q_ASSERT(m_theme);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    connect(m_theme, &Theme::themeChanged, this, &KrellstockQuoteWidget::onThemeChanged);
    onThemeChanged();
}

void KrellstockQuoteWidget::onThemeChanged()
{
    const QFont f = m_theme->font(QStringLiteral("label"));
    m_rowH = QFontMetrics(f).height() + 5;
    updateGeometry();
    update();
}

void KrellstockQuoteWidget::setQuotes(const QList<StockQuote> &quotes)
{
    m_quotes = quotes;
    updateGeometry();
    update();
}

void KrellstockQuoteWidget::setPlaceholder(const QString &text)
{
    m_placeholder = text;
    if (m_quotes.isEmpty()) update();
}

QSize KrellstockQuoteWidget::sizeHint() const
{
    const int rows = qMax(1, m_quotes.size());
    return QSize(80, rows * m_rowH + 2);
}

QSize KrellstockQuoteWidget::minimumSizeHint() const
{
    return QSize(0, m_rowH);
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

    for (int i = 0; i < m_quotes.size(); ++i) {
        const StockQuote &q = m_quotes.at(i);
        const QRect row(0, i * m_rowH, w, m_rowH);

        // Subtle alternating row tint.
        if (i % 2 == 0) p.fillRect(row, QColor(255, 255, 255, 4));

        const QColor cc    = changeColor(q.changePct, m_theme);
        const QString arrow = changeArrow(q.changePct);
        const int yBase = row.top() + (row.height() + fm.ascent() - fm.descent()) / 2;

        // ── Right: percent change  e.g. "▲+0.64%"
        const QString pctText = arrow
            + (q.changePct >= 0 ? QStringLiteral("+") : QString())
            + QString::number(q.changePct, 'f', 2)
            + QStringLiteral("%");
        const int pctW = fm.horizontalAdvance(pctText);
        p.setFont(base);
        p.setPen(cc);
        p.drawText(QPoint(w - pad - pctW, yBase), pctText);

        // ── Right of symbol: change amount  e.g. "+1.20"
        const QString chgText = formatChange(q.change, q.price);
        const int chgW = fm.horizontalAdvance(chgText);
        p.drawText(QPoint(w - pad - pctW - 6 - chgW, yBase), chgText);

        // ── Left: symbol (bold, primary)
        const QString symText = displaySym(q.symbol);
        p.setFont(bold);
        p.setPen(cPrimary);
        p.drawText(QPoint(pad, yBase), symText);
        const int symW = bfm.horizontalAdvance(symText);

        // ── Middle: price — right-aligned in the remaining gap
        const QString priceText = formatPrice(q.price, q.currency);
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
    }
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
        QStringLiteral("plugins/krellstock/quotes_enabled"), false).toBool();
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
