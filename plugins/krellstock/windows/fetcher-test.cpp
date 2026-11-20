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
    const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1/quote").arg(server.serverPort()));
    QStringList received;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        auto *client = server.nextPendingConnection();
        auto buffer = std::make_shared<QByteArray>();
        QObject::connect(client, &QTcpSocket::readyRead, client, [&, client, buffer] {
            *buffer += client->readAll();
            if (!buffer->contains("\r\n\r\n")) return;
            const auto request = buffer->split('\n').first().split(' ');
            require(request.size() >= 2, "Malformed fixture request");
            const QUrlQuery query(QUrl::fromEncoded("http://localhost" + request.at(1)));
            const auto symbol = query.queryItemValue(QStringLiteral("symbol"));
            require(!query.hasQueryItem(QStringLiteral("apikey")), "Key appeared in the URL");
            require(buffer->toLower().contains("\r\nauthorization: apikey fixture"), "No key in the authorization header");
            received << symbol;
            if (symbol == QStringLiteral("PENDING")) return;
            QByteArray response;
            QByteArray status = "200 OK";
            if (symbol == QStringLiteral("AAPL")) {
                response = R"({"symbol":"AAPL","close":"250.25","previous_close":"249.00","name":"Apple","currency":"USD","is_market_open":false})";
            } else if (symbol == QStringLiteral("BTC/USD")) {
                response = R"({"symbol":"BTC/USD","close":"12345.67","name":"Bitcoin"})";
            } else if (symbol == QStringLiteral("LIMIT")) {
                status = "429 Too Many Requests";
                response = R"({"code":429,"status":"error"})";
            } else if (symbol == QStringLiteral("BP")) {
                require(query.queryItemValue(QStringLiteral("mic_code")) == QStringLiteral("XLON"), "Exchange suffix resolved incorrectly");
                response = R"({"symbol":"BP","mic_code":"XNYS","close":"1.23"})";
            } else {
                qFatal("Unexpected fixture symbol");
            }
            client->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nContent-Length: "
                + QByteArray::number(response.size()) + "\r\nConnection: close\r\n\r\n" + response);
            client->disconnectFromHost();
        });
        QObject::connect(client, &QTcpSocket::disconnected, client, &QObject::deleteLater);
    });
    require(!WindowsStockFetcher::validKey("key\r\n"), "Accepted a key with header delimiters");
    {
        WindowsStockFetcher fetcher(endpoint);
        const QStringList choice{QStringLiteral("aapl"), QStringLiteral("BTC-USD"),
            QStringLiteral("AAPL"), QStringLiteral("LIMIT"), QStringLiteral("MSFT")};
        fetcher.configure(choice, {});
        require(fetcher.quotes().size() == 4
            && fetcher.quotes().first().shortName == QStringLiteral("Add API key") && received.isEmpty(),
            "Missing-key display or symbol deduplication failed");
        fetcher.configure(choice, "fixture");
        waitFor([&] { return fetcher.quotes().last().shortName == QStringLiteral("Rate limited"); });
        const auto quotes = fetcher.quotes();
        require(quotes.at(0).price == 250.25 && quotes.at(0).marketState == QStringLiteral("CLOSED")
            && quotes.at(1).valid && quotes.at(1).symbol == QStringLiteral("BTC-USD")
            && quotes.at(1).currency == QStringLiteral("USD"),
            "Valid stock or crypto quote not displayed in configured order");
        require(received == QStringList{QStringLiteral("AAPL"), QStringLiteral("BTC/USD"), QStringLiteral("LIMIT")},
            "Fetcher did not stop the queue on rate limit");
        fetcher.poll();
        require(received.size() == 3, "Backoff or cache was ignored");
        fetcher.configure({QStringLiteral("BTC-USD")}, "fixture");
        require(fetcher.quotes().size() == 1 && fetcher.quotes().first().valid, "Removed symbol remained visible or cache was lost");
        fetcher.configure({QStringLiteral("BP.L")}, "fixture2");
        waitFor([&] { return fetcher.quotes().first().shortName == QStringLiteral("Unavailable"); });
        require(!fetcher.quotes().first().valid, "A quote from the wrong exchange was accepted");
        fetcher.configure({QStringLiteral("PENDING")}, "fixture2");
        waitFor([&] { return received.contains(QStringLiteral("PENDING")); });
    }
    QCoreApplication::processEvents();
    qInfo("PASS: no-key setup, sparse configuration, equities, crypto, rate limits, cache, exchange checks and pending-reply destruction");
    return 0;
}
