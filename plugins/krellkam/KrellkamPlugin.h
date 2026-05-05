#pragma once

#include "monitors/MonitorBase.h"
#include "sdk/KrellixPlugin.h"

#include <QHash>
#include <QImage>
#include <QList>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QUrl>
#include <QWidget>

#include <functional>

class QLabel;

struct KrellkamSource {
    QString title;
    QString type;
    QString source;
};

inline bool operator==(const KrellkamSource &a, const KrellkamSource &b)
{
    return a.title == b.title && a.type == b.type && a.source == b.source;
}

class KrellkamField : public QWidget
{
    Q_OBJECT

public:
    explicit KrellkamField(Theme *theme, QWidget *parent = nullptr);
    ~KrellkamField() override;

    void setSources(const QList<KrellkamSource> &sources);
    void refresh();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private slots:
    void onThemeChanged();

private:
    struct Slot {
        QString title;
        QString type;
        QString source;
        QImage image;
        QString status;
    };

    void requestSource(int index);
    void requestImageSource(int index, const QString &source);
    void requestMjpegSource(int index, const QString &source);
    void requestYoutubeSource(int index, const QString &source);
    void requestYoutubeFrame(const QString &source,
                             std::function<void(const QByteArray &, const QString &)> callback);
    void requestCommandSource(int index, const QString &source);
    void setSlotImage(int index, const QByteArray &bytes);
    void setSlotError(int index, const QString &status);
    bool tryExtractMjpegFrame(int index, QByteArray &buffer);
    void paintChartBackground(QPainter &p, const QRect &rect) const;
    int slotAt(const QPoint &pos) const;
    void openViewer(int index);

    Theme *m_theme = nullptr;
    QList<KrellkamSource> m_sources;
    QList<Slot> m_slots;
    QNetworkAccessManager m_net;
};

class KrellkamMonitor : public MonitorBase
{
    Q_OBJECT

public:
    explicit KrellkamMonitor(Theme *theme, QObject *parent = nullptr);
    ~KrellkamMonitor() override;

    QString id() const override;
    QString displayName() const override;
    int tickIntervalMs() const override;
    QWidget *createWidget(QWidget *parent) override;
    void tick() override;

private:
    QList<KrellkamSource> sources() const;

    QPointer<KrellkamField> m_field;
};

class KrellkamPlugin : public QObject, public IKrellixPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID KrellixPlugin_iid)
    Q_INTERFACES(IKrellixPlugin)

public:
    QString pluginId() const override;
    QString pluginName() const override;
    QString pluginVersion() const override;
    QList<MonitorBase *> createMonitors(Theme *theme, QObject *parent) override;
};
