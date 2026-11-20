#include "WindowsStockFetcher.h"

#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QUrlQuery>
#include <functional>
#include <memory>

static void require(bool result, const char *reason)
{
    if (!result) qFatal("%s", reason);
}

static void waitFor(const std::function<bool()> &done)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 5000) {
        QCoreApplication::processEvents();
        QThread::msleep(5);
    }
    require(done(), "The fixture did not finish");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost), "Could not start loopback quote fixture");
    const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1/chart").arg(server.serverPort()));
    QStringList received;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        auto *client = server.nextPendingConnection();
        auto buffer = std::make_shared<QByteArray>();
        QObject::connect(client, &QTcpSocket::readyRead, client, [&, client, buffer] {
            *buffer += client->readAll();
            if (!buffer->contains("\r\n\r\n")) return;
            const auto request = buffer->split('\n').first().split(' ');
            require(request.size() >= 2, "Malformed fixture request");
            const QUrl url = QUrl::fromEncoded("http://localhost" + request.at(1));
            const QUrlQuery query(url);
            const auto symbol = url.path().section(QLatin1Char('/'), -1);
            require(!query.hasQueryItem(QStringLiteral("apikey")) &&
                    !buffer->toLower().contains("\r\nauthorization:"), "Requested a quote with credentials");
            require(query.queryItemValue(QStringLiteral("interval")) == QStringLiteral("1d"), "Wrong Yahoo interval");
            received << symbol;
            if (symbol == QStringLiteral("PENDING")) return;
            QByteArray response;
            QByteArray status = "200 OK";
            if (symbol == QStringLiteral("AAPL")) {
                response = R"({"chart":{"result":[{"meta":{"symbol":"AAPL","regularMarketPrice":250.25,"shortName":"Apple","currency":"USD","instrumentType":"EQUITY","currentTradingPeriod":{"regular":{"start":1,"end":2}}},"indicators":{"quote":[{"close":[248.5,249,250.25]}]}}]}})";
            } else if (symbol == QStringLiteral("BTC-USD")) {
                response = R"({"chart":{"result":[{"meta":{"symbol":"BTC-USD","regularMarketPrice":12345.67,"currency":"USD","instrumentType":"CRYPTOCURRENCY"}}]}})";
            } else if (symbol == QStringLiteral("LIMIT")) {
                status = "429 Too Many Requests";
                response = R"({"code":429,"status":"error"})";
            } else if (symbol == QStringLiteral("BP.L")) {
                response = R"({"chart":{"result":[{"meta":{"symbol":"BP","regularMarketPrice":1.23}}]}})";
            } else {
                qFatal("Unexpected fixture symbol");
            }
            client->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nContent-Length: "
                + QByteArray::number(response.size()) + "\r\nConnection: close\r\n\r\n" + response);
            client->disconnectFromHost();
        });
        QObject::connect(client, &QTcpSocket::disconnected, client, &QObject::deleteLater);
    });
    {
        WindowsStockFetcher fetcher(endpoint);
        const QStringList choice{QStringLiteral("aapl"), QStringLiteral("BTC/USD"),
            QStringLiteral("AAPL"), QStringLiteral("LIMIT"), QStringLiteral("MSFT")};
        fetcher.configure({});
        require(fetcher.quotes().isEmpty() && received.isEmpty(), "Requested unconfigured symbols");
        fetcher.configure(choice);
        waitFor([&] { return fetcher.quotes().last().shortName == QStringLiteral("Rate limited"); });
        const auto quotes = fetcher.quotes();
        require(quotes.at(0).price == 250.25 && quotes.at(0).marketState == QStringLiteral("CLOSED")
            && quotes.at(0).change == 1.25
            && quotes.at(1).valid && quotes.at(1).symbol == QStringLiteral("BTC/USD")
            && quotes.at(1).currency == QStringLiteral("USD"),
            "Valid stock or crypto quote not displayed in configured order");
        require(received == QStringList{QStringLiteral("AAPL"), QStringLiteral("BTC-USD"), QStringLiteral("LIMIT")},
            "Fetcher did not stop the queue on rate limit");
        fetcher.poll();
        require(received.size() == 3, "Backoff or cache was ignored");
        fetcher.configure({QStringLiteral("BTC/USD")});
        require(fetcher.quotes().size() == 1 && fetcher.quotes().first().valid, "Removed symbol remained visible or cache was lost");
    }
    {
        WindowsStockFetcher fetcher(endpoint);
        fetcher.configure({QStringLiteral("BP.L")});
        waitFor([&] { return fetcher.quotes().first().shortName == QStringLiteral("Unavailable"); });
        require(!fetcher.quotes().first().valid, "A quote from the wrong exchange was accepted");
        fetcher.configure({QStringLiteral("PENDING")});
        waitFor([&] { return received.contains(QStringLiteral("PENDING")); });
    }
    QCoreApplication::processEvents();
    qInfo("PASS: Yahoo quotes without credentials, sparse configuration, equities, crypto, rate limits, cache, symbol checks and pending-reply destruction");
    return 0;
}
