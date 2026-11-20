#include "WindowsStockFetcher.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimeZone>
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

QString yahooSymbol(QString symbol)
{
    if (symbol.startsWith(QLatin1Char('$')))
        symbol.remove(0, 1);
    static const QRegularExpression pair(QStringLiteral("^([A-Z0-9]{1,8})/([A-Z]{3})$"));
    const auto match = pair.match(symbol);
    if (match.hasMatch())
        symbol = match.captured(1) + QLatin1Char('-') + match.captured(2);
    return symbol;
}

std::optional<double> number(const QJsonValue &v)
{
    bool ok = v.isDouble();
    const double result = ok ? v.toDouble() : v.toString().toDouble(&ok);
    if (!ok || !std::isfinite(result)) return std::nullopt;
    return result;
}

std::optional<double> previousClose(const QJsonObject &chart, const QJsonObject &meta)
{
    if (auto prior = number(meta.value(QStringLiteral("previousClose"))); prior && *prior > 0)
        return prior;
    const auto timestamps = chart.value(QStringLiteral("timestamp")).toArray();
    const auto groups = chart.value(QStringLiteral("indicators")).toObject()
        .value(QStringLiteral("quote")).toArray();
    if (groups.isEmpty()) return std::nullopt;
    const auto closes = groups.first().toObject().value(QStringLiteral("close")).toArray();
    const auto quoteTime = meta.value(QStringLiteral("regularMarketTime")).toInteger();
    const QTimeZone zone(meta.value(QStringLiteral("exchangeTimezoneName")).toString().toUtf8());
    if (quoteTime > 0 && zone.isValid()) {
        const auto quoteDate = QDateTime::fromSecsSinceEpoch(quoteTime, zone).date();
        for (qsizetype i = qMin(closes.size(), timestamps.size()); i > 0; --i) {
            const auto t = timestamps.at(i - 1).toInteger();
            const auto close = number(closes.at(i - 1));
            if (t > 0 && close && *close > 0 && QDateTime::fromSecsSinceEpoch(t, zone).date() < quoteDate)
                return close;
        }
    } else {
        for (qsizetype i = closes.size() - 2; i >= 0; --i)
            if (auto close = number(closes.at(i)); close && *close > 0) return close;
    }
    return std::nullopt;
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

QString WindowsStockFetcher::cleanSymbol(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral("^\\$?[A-Z0-9^][A-Z0-9./=-]{0,18}$"));
    const auto symbol = text.trimmed().toUpper();
    return pattern.match(symbol).hasMatch() && !yahooSymbol(symbol).contains(QLatin1Char('/'))
        ? symbol : QString();
}

void WindowsStockFetcher::configure(const QStringList &symbols, int intervalMs)
{
    QStringList wanted;
    for (const auto &raw : symbols) {
        const auto symbol = cleanSymbol(raw);
        if (!symbol.isEmpty() && !wanted.contains(symbol)) wanted.append(symbol);
        if (wanted.size() == 10) break;
    }
    m_intervalMs = qBound(kRefreshMs, intervalMs, kClosedMs);
    if (m_symbols == wanted) { poll(); return; }
    if (!wanted.contains(m_active)) stopRequest();
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
    QUrl url = m_endpoint;
    url.setPath(url.path() + QLatin1Char('/') + yahooSymbol(symbol));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("range"), QStringLiteral("5d"));
    query.addQueryItem(QStringLiteral("interval"), QStringLiteral("1d"));
    url.setQuery(query);
    return url;
}

void WindowsStockFetcher::poll()
{
    const qint64 now = m_clock.elapsed();
    if (m_reply || now < m_backoffUntil) return;
    while (!m_recent.isEmpty() && now - m_recent.first() >= 60000) m_recent.removeFirst();
    // Limit bursts even when the user enables both panels at once.
    if (m_recent.size() >= 8) return;
    for (const auto &symbol : m_symbols) {
        if (m_entries.value(symbol).next > now) continue;
        QNetworkRequest request(requestUrl(symbol));
        request.setRawHeader("Accept", "application/json");
        request.setRawHeader("User-Agent", "Krellix/0.1.2 (KrellStock)");
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
    const auto results = obj.value(QStringLiteral("chart")).toObject().value(QStringLiteral("result")).toArray();
    const auto chart = results.isEmpty() ? QJsonObject() : results.first().toObject();
    const auto meta = chart.value(QStringLiteral("meta")).toObject();
    const auto price = number(meta.value(QStringLiteral("regularMarketPrice")));
    const qint64 now = m_clock.elapsed();
    auto &entry = m_entries[symbol];
    entry.next = now + m_intervalMs;
    if (http == 429) {
        m_backoffMs = m_backoffMs == 0 ? kRefreshMs : qMin(2 * m_backoffMs, kClosedMs);
        const int headerMs = retryAfterOk ? qBound(0, retryAfter, 86400) * 1000 : 0;
        m_backoffUntil = now + qMax(m_backoffMs, headerMs);
        entry.status = m_globalStatus = QStringLiteral("Rate limited");
    } else if (transportOk && price && *price > 0
               && meta.value(QStringLiteral("symbol")).toString().compare(yahooSymbol(symbol), Qt::CaseInsensitive) == 0) {
        StockQuote quote;
        quote.symbol = symbol;
        quote.shortName = meta.value(QStringLiteral("shortName")).toString().simplified().left(64);
        quote.price = *price;
        const double previous = previousClose(chart, meta).value_or(*price);
        quote.change = *price - previous;
        quote.changePct = quote.change / previous * 100;
        if (!std::isfinite(quote.changePct)) quote.changePct = 0;
        quote.currency = meta.value(QStringLiteral("currency")).toString().left(6);
        const auto type = meta.value(QStringLiteral("instrumentType")).toString();
        const auto session = meta.value(QStringLiteral("currentTradingPeriod")).toObject()
            .value(QStringLiteral("regular")).toObject();
        const auto open = session.value(QStringLiteral("start")).toInteger();
        const auto close = session.value(QStringLiteral("end")).toInteger();
        const auto epoch = QDateTime::currentSecsSinceEpoch();
        const bool equity = type == QLatin1String("EQUITY") || type == QLatin1String("ETF");
        const bool closed = equity && open > 0 && close > open && (epoch < open || epoch >= close);
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
    // Do not log response bodies or headers.
    if (entry.status.isEmpty())
        qCDebug(lcStock).noquote() << "Quote accepted:" << symbol;
    else
        qCDebug(lcStock).noquote() << "Quote status:" << symbol << entry.status << http << int(networkError);
    emit changed();
    // Continue only after Qt has returned from the reply's finished handler.
    QTimer::singleShot(0, this, &WindowsStockFetcher::poll);
}
