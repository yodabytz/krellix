#include "WindowsStockFetcher.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrlQuery>
#include <cmath>
#include <optional>
#include <utility>

Q_LOGGING_CATEGORY(lcStock, "krellix.stock", QtWarningMsg)

namespace {
constexpr int kRefreshMs = 15 * 60 * 1000;
constexpr int kClosedMs = 60 * 60 * 1000;
constexpr int kResponseBytes = 64 * 1024;
constexpr int kTimeoutMs = 10000;

struct Instrument {
    QString symbol;
    QString mic;
    bool continuous = false;
};

Instrument instrument(QString symbol)
{
    if (symbol.startsWith(QLatin1Char('$')))
        symbol.remove(0, 1);
    static const QRegularExpression pair(QStringLiteral("^([A-Z0-9]{1,6})-([A-Z]{3})$"));
    const auto match = pair.match(symbol);
    if (match.hasMatch())
        symbol = match.captured(1) + QLatin1Char('/') + match.captured(2);
    if (symbol.contains(QLatin1Char('/')))
        return {symbol, {}, true};
    static const QHash<QString, QString> markets{
        {QStringLiteral(".L"), QStringLiteral("XLON")},
        {QStringLiteral(".DE"), QStringLiteral("XETR")},
        {QStringLiteral(".T"), QStringLiteral("XTKS")},
        {QStringLiteral(".HK"), QStringLiteral("XHKG")}
    };
    const auto suffix = markets.constFind(symbol.mid(symbol.lastIndexOf(QLatin1Char('.'))));
    if (suffix != markets.constEnd())
        return {symbol.left(symbol.size() - suffix.key().size()), suffix.value(), false};
    return {symbol, {}, false};
}

std::optional<double> number(const QJsonValue &v)
{
    bool ok = v.isDouble();
    const double result = ok ? v.toDouble() : v.toString().toDouble(&ok);
    if (!ok || !std::isfinite(result)) return std::nullopt;
    return result;
}
}

WindowsStockFetcher::WindowsStockFetcher(QUrl endpoint) : m_endpoint(std::move(endpoint))
{
    m_clock.start();
    m_deadline.setSingleShot(true);
    m_deadline.setInterval(kTimeoutMs);
    connect(&m_deadline, &QTimer::timeout, this, [this] { if (m_reply) m_reply->abort(); });
}

WindowsStockFetcher::~WindowsStockFetcher() { stopRequest(); }

bool WindowsStockFetcher::validKey(const QByteArray &key)
{
    if (key.isEmpty() || key.size() > 128) return false;
    for (char c : key)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
              || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    return true;
}

QString WindowsStockFetcher::cleanSymbol(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral("^\\$?[A-Z0-9^][A-Z0-9./=-]{0,18}$"));
    const auto symbol = text.trimmed().toUpper();
    return pattern.match(symbol).hasMatch() ? symbol : QString();
}

void WindowsStockFetcher::configure(const QStringList &symbols, const QByteArray &key, int intervalMs)
{
    QStringList wanted;
    for (const auto &raw : symbols) {
        const auto symbol = cleanSymbol(raw);
        if (!symbol.isEmpty() && !wanted.contains(symbol)) wanted.append(symbol);
        if (wanted.size() == 10) break;
    }
    const auto normalizedKey = validKey(key) ? key : QByteArray();
    m_intervalMs = qBound(kRefreshMs, intervalMs, kClosedMs);
    if (m_symbols == wanted && m_key == normalizedKey) { poll(); return; }
    if (m_key != normalizedKey) {
        stopRequest();
        m_key = normalizedKey;
        m_entries.clear();
        m_recent.clear();
        m_backoffMs = 0;
        m_backoffUntil = 0;
        m_globalStatus.clear();
    } else if (!wanted.contains(m_active)) {
        stopRequest();
    }
    for (auto it = m_entries.begin(); it != m_entries.end(); ) {
        if (!wanted.contains(it.key())) it = m_entries.erase(it);
        else ++it;
    }
    m_symbols = wanted;
    emit changed();
    poll();
}

QList<StockQuote> WindowsStockFetcher::quotes() const
{
    QList<StockQuote> result;
    for (const auto &symbol : m_symbols) {
        const auto entry = m_entries.value(symbol);
        StockQuote quote = entry.quote;
        quote.symbol = symbol;
        if (quote.valid) {
            if (!entry.status.isEmpty())
                quote.shortName += QStringLiteral(" (cached)");
        } else if (m_key.isEmpty()) {
            quote.shortName = QStringLiteral("Add API key");
        } else if (!entry.status.isEmpty()) {
            quote.shortName = entry.status;
        } else if (m_clock.elapsed() < m_backoffUntil) {
            quote.shortName = m_globalStatus;
        } else {
            quote.shortName = m_active == symbol ? QStringLiteral("Fetching...") : QStringLiteral("Queued");
        }
        result.append(quote);
    }
    return result;
}

QUrl WindowsStockFetcher::requestUrl(const QString &symbol) const
{
    const auto item = instrument(symbol);
    QUrl url = m_endpoint;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("symbol"), item.symbol);
    if (!item.mic.isEmpty()) query.addQueryItem(QStringLiteral("mic_code"), item.mic);
    url.setQuery(query);
    return url;
}

void WindowsStockFetcher::poll()
{
    const qint64 now = m_clock.elapsed();
    if (m_reply || m_key.isEmpty() || now < m_backoffUntil) return;
    while (!m_recent.isEmpty() && now - m_recent.first() >= 60000) m_recent.removeFirst();
    // Leave extra symbols queued for the next credit window.
    if (m_recent.size() >= 8) return;
    for (const auto &symbol : m_symbols) {
        if (m_entries.value(symbol).next > now) continue;
        QNetworkRequest request(requestUrl(symbol));
        request.setRawHeader("Authorization", "apikey " + m_key);
        request.setRawHeader("Accept", "application/json");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        m_buffer.clear();
        m_oversized = false;
        m_active = symbol;
        m_recent.append(now);
        auto *reply = m_network.get(request);
        m_reply = reply;
        reply->setReadBufferSize(kResponseBytes + 1);
        connect(reply, &QNetworkReply::readyRead, this, [this, reply] { readReply(reply); });
        connect(reply, &QNetworkReply::finished, this, [this, reply] { finishReply(reply); });
        m_deadline.start();
        emit changed();
        return;
    }
}

void WindowsStockFetcher::stopRequest()
{
    m_deadline.stop();
    m_active.clear();
    if (auto *reply = std::exchange(m_reply, nullptr)) {
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
}

void WindowsStockFetcher::readReply(QNetworkReply *reply)
{
    if (reply != m_reply || m_oversized) return;
    m_buffer += reply->read(kResponseBytes + 1 - m_buffer.size());
    if (m_buffer.size() > kResponseBytes) {
        m_oversized = true;
        reply->abort();
    }
}

void WindowsStockFetcher::finishReply(QNetworkReply *reply)
{
    if (reply != m_reply) return;
    m_reply = nullptr;
    m_deadline.stop();
    const QString symbol = std::exchange(m_active, QString());
    if (!m_oversized)
        m_buffer += reply->read(kResponseBytes + 1 - m_buffer.size());
    const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto networkError = reply->error();
    const bool transportOk = networkError == QNetworkReply::NoError && http == 200;
    bool retryAfterOk = false;
    const int retryAfter = reply->rawHeader("Retry-After").toInt(&retryAfterOk);
    reply->deleteLater();
    const auto obj = m_buffer.size() <= kResponseBytes && !m_oversized
        ? QJsonDocument::fromJson(m_buffer).object() : QJsonObject();
    const int code = obj.value(QStringLiteral("code")).toVariant().toInt();
    const auto item = instrument(symbol);
    const auto price = number(obj.value(QStringLiteral("close")));
    const qint64 now = m_clock.elapsed();
    auto &entry = m_entries[symbol];
    entry.next = now + m_intervalMs;
    if (http == 429 || code == 429) {
        m_backoffMs = m_backoffMs == 0 ? kRefreshMs : qMin(2 * m_backoffMs, kClosedMs);
        const int headerMs = retryAfterOk ? qBound(0, retryAfter, 86400) * 1000 : 0;
        m_backoffUntil = now + qMax(m_backoffMs, headerMs);
        entry.status = m_globalStatus = QStringLiteral("Rate limited");
    } else if (http == 401 || code == 401) {
        m_backoffUntil = now + kRefreshMs;
        entry.status = m_globalStatus = QStringLiteral("Key rejected");
    } else if (transportOk && price && *price > 0
               && obj.value(QStringLiteral("symbol")).toString().compare(item.symbol, Qt::CaseInsensitive) == 0
               && (item.mic.isEmpty() || obj.value(QStringLiteral("mic_code")).toString() == item.mic)) {
        StockQuote quote;
        quote.symbol = symbol;
        quote.shortName = obj.value(QStringLiteral("name")).toString().simplified().left(64);
        quote.price = *price;
        const double previous = number(obj.value(QStringLiteral("previous_close"))).value_or(*price);
        quote.change = number(obj.value(QStringLiteral("change"))).value_or(*price - previous);
        quote.changePct = number(obj.value(QStringLiteral("percent_change")))
            .value_or(previous > 0 ? quote.change / previous * 100 : 0);
        if (!std::isfinite(quote.changePct)) quote.changePct = 0;
        quote.currency = obj.value(QStringLiteral("currency")).toString().left(6);
        if (quote.currency.isEmpty() && item.continuous)
            quote.currency = item.symbol.section(QLatin1Char('/'), -1).left(6);
        const QJsonValue open = obj.value(QStringLiteral("is_market_open"));
        const bool closed = !item.continuous && open.isBool() && !open.toBool();
        quote.marketState = closed ? QStringLiteral("CLOSED") : QStringLiteral("REGULAR");
        quote.valid = true;
        entry.quote = quote;
        entry.status.clear();
        if (closed) entry.next = now + kClosedMs;
        m_backoffMs = 0;
        m_globalStatus.clear();
    } else {
        if (m_oversized) entry.status = QStringLiteral("Response too large");
        else if (networkError == QNetworkReply::SslHandshakeFailedError) entry.status = QStringLiteral("TLS failed");
        else if (networkError == QNetworkReply::OperationCanceledError) entry.status = QStringLiteral("Timed out");
        else if (!http) entry.status = QStringLiteral("Network unavailable");
        else entry.status = QStringLiteral("Unavailable");
    }
    // Never log response bodies, headers, or the key.
    if (entry.status.isEmpty())
        qCDebug(lcStock).noquote() << "Quote accepted:" << symbol;
    else
        qCDebug(lcStock).noquote() << "Quote status:" << symbol << entry.status << http << int(networkError);
    emit changed();
    // Continue only after Qt has returned from the reply's finished handler.
    QTimer::singleShot(0, this, &WindowsStockFetcher::poll);
}
