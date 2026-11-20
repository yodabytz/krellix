#pragma once

#include "../KrellstockPlugin.h"
#include <QUrl>

class WindowsStockFetcher final : public QObject
{
    Q_OBJECT
public:
    explicit WindowsStockFetcher(QUrl endpoint = QUrl(QStringLiteral("https://api.twelvedata.com/quote")));
    ~WindowsStockFetcher() override;

    void configure(const QStringList &symbols, const QByteArray &key, int intervalMs = 900000);
    void poll();
    QList<StockQuote> quotes() const;
    static bool validKey(const QByteArray &key);
    static QString cleanSymbol(const QString &text);

signals:
    void changed();

private:
    struct Entry {
        StockQuote quote;
        QString status;
        qint64 next = 0;
    };

    void stopRequest();
    void readReply(QNetworkReply *reply);
    void finishReply(QNetworkReply *reply);
    QUrl requestUrl(const QString &symbol) const;

    QUrl m_endpoint;
    QNetworkAccessManager m_network;
    QTimer m_deadline;
    QElapsedTimer m_clock;
    QNetworkReply *m_reply = nullptr;
    QByteArray m_buffer;
    QByteArray m_key;
    QStringList m_symbols;
    QString m_active;
    QString m_globalStatus;
    QHash<QString, Entry> m_entries;
    QList<qint64> m_recent;
    qint64 m_backoffUntil = 0;
    int m_backoffMs = 0;
    int m_intervalMs = 900000;
    bool m_oversized = false;
};
