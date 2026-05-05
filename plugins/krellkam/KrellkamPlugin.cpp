#include "KrellkamPlugin.h"

#include "theme/Theme.h"
#include "widgets/Panel.h"

#include <QDateTime>
#include <QFile>
#include <QDialog>
#include <QLabel>
#include <QMouseEvent>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace {

constexpr int kMaxSources = 5;

QList<KrellkamSource> configuredSources()
{
    QSettings s;
    QList<KrellkamSource> out;

    const QStringList packed = s.value(QStringLiteral("plugins/krellkam/sources"))
                                   .toString()
                                   .split(QRegularExpression(QStringLiteral("[\\n;,]")),
                                          Qt::SkipEmptyParts);
    for (const QString &entry : packed) {
        const QString source = entry.trimmed();
        if (!source.isEmpty() && out.size() < kMaxSources)
            out.append(KrellkamSource{QString(), QStringLiteral("auto"), source});
    }

    for (int i = 1; i <= kMaxSources && out.size() < kMaxSources; ++i) {
        const QString source =
            s.value(QStringLiteral("plugins/krellkam/source%1").arg(i))
                .toString()
                .trimmed();
        if (source.isEmpty()) continue;
        bool duplicate = false;
        for (const KrellkamSource &existing : out) {
            if (existing.source == source) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        const QString type =
            s.value(QStringLiteral("plugins/krellkam/type%1").arg(i),
                    QStringLiteral("auto")).toString().trimmed().toLower();
        const QString title =
            s.value(QStringLiteral("plugins/krellkam/title%1").arg(i))
                .toString()
                .trimmed();
        out.append(KrellkamSource{title,
                                  type.isEmpty() ? QStringLiteral("auto") : type,
                                  source});
    }

    return out;
}

bool isRemote(const QString &source)
{
    const QUrl url(source);
    return url.isValid()
        && (url.scheme() == QStringLiteral("http")
            || url.scheme() == QStringLiteral("https"));
}

bool isYoutubeUrl(const QString &source)
{
    const QUrl url(source);
    const QString host = url.host().toLower();
    return host.contains(QStringLiteral("youtube.com"))
        || host.contains(QStringLiteral("youtu.be"));
}

bool executableExists(const QString &program)
{
    return !QStandardPaths::findExecutable(program).isEmpty();
}

int indexOfBytes(const QByteArray &bytes, const QByteArray &needle, int from = 0)
{
    return bytes.indexOf(needle, from);
}

} // namespace

KrellkamField::KrellkamField(Theme *theme, QWidget *parent)
    : QWidget(parent)
    , m_theme(theme)
{
    Q_ASSERT(m_theme);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setCursor(Qt::PointingHandCursor);
    connect(m_theme, &Theme::themeChanged, this, &KrellkamField::onThemeChanged);
}

KrellkamField::~KrellkamField() = default;

void KrellkamField::setSources(const QList<KrellkamSource> &sources)
{
    if (sources == m_sources && !m_slots.isEmpty()) return;
    m_sources = sources;
    m_slots.clear();
    for (const KrellkamSource &source : sources.mid(0, kMaxSources)) {
        Slot slot;
        slot.title = source.title;
        slot.type = source.type;
        slot.source = source.source;
        slot.status = QStringLiteral("loading");
        m_slots.append(slot);
    }
    if (m_slots.isEmpty()) {
        Slot slot;
        slot.status = QStringLiteral("add a camera in Settings > Plugins");
        m_slots.append(slot);
    }
    updateGeometry();
    update();
}

void KrellkamField::refresh()
{
    for (int i = 0; i < m_slots.size(); ++i)
        requestSource(i);
}

QSize KrellkamField::sizeHint() const
{
    const int per = qBound(24,
        QSettings().value(QStringLiteral("plugins/krellkam/field_height"),
                          m_theme->metric(QStringLiteral("chart_height"), 48)).toInt(),
        240);
    const int spacing = m_theme->metric(QStringLiteral("panel_spacing"), 2);
    return QSize(0, m_slots.size() * per + qMax(0, m_slots.size() - 1) * spacing);
}

QSize KrellkamField::minimumSizeHint() const
{
    return QSize(0, qMax(20, m_slots.size() * 20));
}

void KrellkamField::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const int count = qMax(1, m_slots.size());
    const int spacing = m_theme->metric(QStringLiteral("panel_spacing"), 2);
    const int h = (height() - spacing * (count - 1)) / count;
    int y = 0;

    for (int i = 0; i < count; ++i) {
        const QRect r(0, y, width(), qMax(1, i == count - 1 ? height() - y : h));
        paintChartBackground(p, r);

        const Slot &slot = m_slots.at(i);
        if (!slot.image.isNull()) {
            p.drawImage(r, slot.image);
        } else {
            const Theme::TextStyle ts = m_theme->textStyle(
                QStringLiteral("chart_overlay"),
                QStringLiteral("text_secondary"));
            const QRect textRect = r.adjusted(4, 1, -4, -1);
            if (ts.shadow.present) {
                p.setPen(ts.shadow.color);
                p.drawText(textRect.translated(ts.shadow.offsetX, ts.shadow.offsetY),
                           Qt::AlignCenter | Qt::TextWordWrap, slot.status);
            }
            p.setPen(ts.color.isValid() ? ts.color
                                        : m_theme->color(QStringLiteral("text_secondary")));
            p.drawText(textRect, Qt::AlignCenter | Qt::TextWordWrap, slot.status);
        }
        if (!slot.title.isEmpty()) {
            QFont f = m_theme->font(QStringLiteral("label"));
            p.setFont(f);
            const QFontMetrics fm(f);
            const int titleH = qBound(10, fm.height() + 2, qMax(10, r.height() / 3));
            const QRect titleRect = QRect(r.left(), r.top(), r.width(), titleH);
            QColor bg = m_theme->color(QStringLiteral("chart_bg"), QColor(0, 0, 0));
            bg.setAlpha(185);
            p.fillRect(titleRect, bg);
            const Theme::TextStyle ts = m_theme->textStyle(
                QStringLiteral("chart_overlay"),
                QStringLiteral("text_primary"));
            const QRect textRect = titleRect.adjusted(3, 0, -3, 0);
            if (ts.shadow.present) {
                p.setPen(ts.shadow.color);
                p.drawText(textRect.translated(ts.shadow.offsetX, ts.shadow.offsetY),
                           Qt::AlignVCenter | Qt::AlignLeft,
                           fm.elidedText(slot.title, Qt::ElideRight, textRect.width()));
            }
            p.setPen(ts.color.isValid() ? ts.color
                                        : m_theme->color(QStringLiteral("text_primary")));
            p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                       fm.elidedText(slot.title, Qt::ElideRight, textRect.width()));
        }

        y += r.height() + spacing;
    }
}

void KrellkamField::onThemeChanged()
{
    updateGeometry();
    update();
}

void KrellkamField::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const int index = slotAt(event->pos());
        if (index >= 0 && index < m_slots.size() && !m_slots.at(index).image.isNull()) {
            openViewer(index);
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void KrellkamField::requestSource(int index)
{
    if (index < 0 || index >= m_slots.size()) return;
    if (m_slots.at(index).fetching) return;
    QString type = m_slots.at(index).type.trimmed().toLower();
    const QString source = m_slots.at(index).source;
    if (source.isEmpty()) return;

    if (type.isEmpty()) type = QStringLiteral("auto");
    if (type == QStringLiteral("command")) {
        requestCommandSource(index, source);
        return;
    }
    if (type == QStringLiteral("mjpeg")) {
        requestMjpegSource(index, source);
        return;
    }
    if (type == QStringLiteral("youtube") || (type == QStringLiteral("auto") && isYoutubeUrl(source))) {
        requestYoutubeSource(index, source);
        return;
    }
    requestImageSource(index, source);
}

void KrellkamField::requestImageSource(int index, const QString &source)
{
    if (!isRemote(source)) {
        QFile f(source);
        if (!f.open(QIODevice::ReadOnly)) {
            setSlotError(index, QStringLiteral("cannot read image"));
            return;
        }
        setSlotImage(index, f.readAll());
        return;
    }

    QUrl url(source);
    QUrlQuery query(url);
    query.removeQueryItem(QStringLiteral("_krellkam"));
    query.addQueryItem(QStringLiteral("_krellkam"),
                       QString::number(QDateTime::currentMSecsSinceEpoch()));
    url.setQuery(query);

    QNetworkRequest req{url};
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::AlwaysNetwork);
    req.setRawHeader("Cache-Control", "no-cache, no-store, max-age=0");
    req.setRawHeader("Pragma", "no-cache");
    req.setRawHeader("User-Agent", "krellix-krellkam/0.1");
    QNetworkReply *reply = m_net.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, index]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setSlotError(index, reply->errorString());
            return;
        }
        setSlotImage(index, reply->readAll());
    });
}

void KrellkamField::requestMjpegSource(int index, const QString &source)
{
    QNetworkRequest req{QUrl(source)};
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::AlwaysNetwork);
    req.setRawHeader("User-Agent", "krellix-krellkam/0.1");

    QNetworkReply *reply = m_net.get(req);
    auto *buffer = new QByteArray;
    connect(reply, &QObject::destroyed, this, [buffer]() { delete buffer; });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, buffer, index]() {
        buffer->append(reply->readAll());
        if (buffer->size() > 4 * 1024 * 1024)
            buffer->remove(0, buffer->size() - 1024 * 1024);
        if (tryExtractMjpegFrame(index, *buffer))
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, buffer, index]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::OperationCanceledError)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            setSlotError(index, reply->errorString());
            return;
        }
        (void) tryExtractMjpegFrame(index, *buffer);
    });
}

void KrellkamField::requestYoutubeSource(int index, const QString &source)
{
    if (index < 0 || index >= m_slots.size()) return;
    m_slots[index].fetching = true;
    requestYoutubeFrame(source, [this, index](const QByteArray &image, const QString &error) {
        if (index >= 0 && index < m_slots.size())
            m_slots[index].fetching = false;
        if (!error.isEmpty()) {
            setSlotError(index, error);
            return;
        }
        setSlotImage(index, image);
    });
}

void KrellkamField::requestYoutubeFrame(const QString &source,
                                        std::function<void(const QByteArray &, const QString &)> callback)
{
    if (!executableExists(QStringLiteral("yt-dlp"))) {
        callback({}, QStringLiteral("yt-dlp not installed"));
        return;
    }
    if (!executableExists(QStringLiteral("ffmpeg"))) {
        callback({}, QStringLiteral("ffmpeg not installed"));
        return;
    }

    const YoutubeStream cached = m_youtubeStreams.value(source);
    if (!cached.url.isEmpty()
        && cached.resolvedAt.secsTo(QDateTime::currentDateTimeUtc()) < 600) {
        requestYoutubeFrameFromStream(cached.url, callback);
        return;
    }

    auto *resolver = new QProcess(this);
    resolver->setProgram(QStringLiteral("yt-dlp"));
    resolver->setArguments({QStringLiteral("-f"),
                            QStringLiteral("best[height<=480]/best[height<=360]/worst"),
                            QStringLiteral("-g"),
                            source});
    resolver->setProcessChannelMode(QProcess::SeparateChannels);
    connect(resolver,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, resolver, source, callback](int exitCode, QProcess::ExitStatus status) {
                const QString streamUrl = QString::fromLocal8Bit(resolver->readAllStandardOutput())
                                              .split(QLatin1Char('\n'), Qt::SkipEmptyParts)
                                              .value(0)
                                              .trimmed();
                const QString err = QString::fromLocal8Bit(resolver->readAllStandardError()).trimmed();
                resolver->deleteLater();
                if (status != QProcess::NormalExit || exitCode != 0 || streamUrl.isEmpty()) {
                    callback({}, err.isEmpty() ? QStringLiteral("yt-dlp failed") : err.left(160));
                    return;
                }

                m_youtubeStreams.insert(source, YoutubeStream{streamUrl, QDateTime::currentDateTimeUtc()});
                requestYoutubeFrameFromStream(streamUrl, callback);
            });
    QTimer::singleShot(12000, resolver, [resolver]() {
        if (resolver->state() != QProcess::NotRunning)
            resolver->kill();
    });
    resolver->start();
}

void KrellkamField::requestYoutubeFrameFromStream(
    const QString &streamUrl,
    std::function<void(const QByteArray &, const QString &)> callback)
{
    auto *ffmpeg = new QProcess(this);
    ffmpeg->setProgram(QStringLiteral("ffmpeg"));
    ffmpeg->setArguments({QStringLiteral("-hide_banner"),
                          QStringLiteral("-nostdin"),
                          QStringLiteral("-loglevel"), QStringLiteral("error"),
                          QStringLiteral("-threads"), QStringLiteral("1"),
                          QStringLiteral("-i"), streamUrl,
                          QStringLiteral("-frames:v"), QStringLiteral("1"),
                          QStringLiteral("-vf"), QStringLiteral("scale='min(480,iw)':-2"),
                          QStringLiteral("-f"), QStringLiteral("image2pipe"),
                          QStringLiteral("-vcodec"), QStringLiteral("mjpeg"),
                          QStringLiteral("-")});
    ffmpeg->setProcessChannelMode(QProcess::SeparateChannels);
    connect(ffmpeg,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [ffmpeg, callback](int code, QProcess::ExitStatus ffStatus) {
                const QByteArray image = ffmpeg->readAllStandardOutput();
                const QString errText = QString::fromLocal8Bit(ffmpeg->readAllStandardError()).trimmed();
                ffmpeg->deleteLater();
                if (ffStatus != QProcess::NormalExit || code != 0 || image.isEmpty()) {
                    callback({}, errText.isEmpty()
                        ? QStringLiteral("ffmpeg failed")
                        : errText.left(160));
                    return;
                }
                callback(image, QString());
            });
    QTimer::singleShot(12000, ffmpeg, [ffmpeg]() {
        if (ffmpeg->state() != QProcess::NotRunning)
            ffmpeg->kill();
    });
    ffmpeg->start();
}

void KrellkamField::requestCommandSource(int index, const QString &source)
{
    auto *proc = new QProcess(this);
    proc->setProgram(QStringLiteral("/bin/sh"));
    proc->setArguments({QStringLiteral("-c"), source});
    connect(proc, &QProcess::finished, this,
            [this, proc, index](int exitCode, QProcess::ExitStatus status) {
                proc->deleteLater();
                if (status != QProcess::NormalExit || exitCode != 0) {
                    const QString err = QString::fromLocal8Bit(proc->readAllStandardError())
                                            .trimmed();
                    setSlotError(index, err.isEmpty()
                        ? QStringLiteral("command failed")
                        : err.left(160));
                    return;
                }
                setSlotImage(index, proc->readAllStandardOutput());
            });
    QTimer::singleShot(10000, proc, [proc]() {
        if (proc->state() != QProcess::NotRunning)
            proc->kill();
    });
    proc->start();
}

void KrellkamField::setSlotImage(int index, const QByteArray &bytes)
{
    if (index < 0 || index >= m_slots.size()) return;
    QImage image;
    if (!image.loadFromData(bytes)) {
        setSlotError(index, QStringLiteral("bad image"));
        return;
    }
    m_slots[index].image = image;
    m_slots[index].status.clear();
    update();
}

bool KrellkamField::tryExtractMjpegFrame(int index, QByteArray &buffer)
{
    int start = indexOfBytes(buffer, QByteArray::fromHex("ffd8"));
    if (start < 0) return false;

    int end = indexOfBytes(buffer, QByteArray::fromHex("ffd9"), start + 2);
    if (end < 0) {
        if (start > 0) buffer.remove(0, start);
        return false;
    }

    end += 2;
    const QByteArray frame = buffer.mid(start, end - start);
    buffer.remove(0, end);
    setSlotImage(index, frame);
    return true;
}

void KrellkamField::setSlotError(int index, const QString &status)
{
    if (index < 0 || index >= m_slots.size()) return;
    m_slots[index].status = status;
    update();
}

void KrellkamField::paintChartBackground(QPainter &p, const QRect &rect) const
{
    const QPixmap bgPix = m_theme->pixmap(QStringLiteral("chart_bg"));
    if (!bgPix.isNull() && rect.height() > 0) {
        const QString mode = m_theme->imageMode(QStringLiteral("chart_bg"),
                                                QStringLiteral("tile"));
        if (mode == QStringLiteral("stretch")) {
            p.drawPixmap(rect, bgPix);
        } else {
            const QPixmap scaled = (bgPix.height() == rect.height())
                ? bgPix
                : bgPix.scaledToHeight(rect.height(), Qt::SmoothTransformation);
            p.drawTiledPixmap(rect, scaled);
        }
    } else {
        p.fillRect(rect, m_theme->brush(QStringLiteral("chart_bg"), rect,
                                        m_theme->color(QStringLiteral("chart_bg"))));
    }

    p.setPen(m_theme->color(QStringLiteral("chart_grid")));
    p.drawRect(rect.adjusted(0, 0, -1, -1));
}

int KrellkamField::slotAt(const QPoint &pos) const
{
    const int count = qMax(1, m_slots.size());
    const int spacing = m_theme->metric(QStringLiteral("panel_spacing"), 2);
    const int h = (height() - spacing * (count - 1)) / count;
    int y = 0;
    for (int i = 0; i < count; ++i) {
        const QRect r(0, y, width(), qMax(1, i == count - 1 ? height() - y : h));
        if (r.contains(pos)) return i;
        y += r.height() + spacing;
    }
    return -1;
}

void KrellkamField::openViewer(int index)
{
    if (index < 0 || index >= m_slots.size()) return;
    const Slot &slot = m_slots.at(index);
    if (slot.image.isNull()) return;

    auto *dialog = new QDialog(window());
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setWindowTitle(slot.source.isEmpty()
        ? QStringLiteral("Krellkam")
        : QStringLiteral("Krellkam - %1").arg(slot.source));
    dialog->setWindowFlag(Qt::Tool, true);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto *image = new QLabel(dialog);
    image->setAlignment(Qt::AlignCenter);
    image->setMinimumSize(320, 180);
    image->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    image->setStyleSheet(QStringLiteral("QLabel { background: #050607; }"));

    const QPixmap pix = QPixmap::fromImage(slot.image);
    const QSize target = slot.image.size().scaled(QSize(760, 520), Qt::KeepAspectRatio);
    image->setPixmap(pix.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    layout->addWidget(image, 1);

    QTimer *liveTimer = nullptr;
    if (slot.type == QStringLiteral("youtube") || isYoutubeUrl(slot.source)) {
        liveTimer = new QTimer(dialog);
        liveTimer->setInterval(qBound(3000,
            QSettings().value(QStringLiteral("plugins/krellkam/youtube_viewer_ms"), 5000).toInt(),
            15000));
        QPointer<QLabel> viewer(image);
        QPointer<QDialog> liveDialog(dialog);
        auto inFlight = std::make_shared<bool>(false);
        auto refreshLive = [this, source = slot.source, viewer, liveDialog, inFlight]() {
            if (!viewer || !liveDialog || *inFlight) return;
            *inFlight = true;
            requestYoutubeFrame(source, [viewer, inFlight](const QByteArray &bytes, const QString &error) {
                *inFlight = false;
                if (!viewer || !error.isEmpty()) return;
                QImage frame;
                if (!frame.loadFromData(bytes)) return;
                const QPixmap framePix = QPixmap::fromImage(frame);
                viewer->setPixmap(framePix.scaled(viewer->size(), Qt::KeepAspectRatio,
                                                  Qt::SmoothTransformation));
            });
        };
        connect(liveTimer, &QTimer::timeout, dialog, refreshLive);
        liveTimer->start();
    }

    auto *close = new QPushButton(QStringLiteral("Close"), dialog);
    connect(close, &QPushButton::clicked, dialog, &QDialog::close);
    layout->addWidget(close, 0, Qt::AlignRight);

    dialog->resize(target.width() + 16, target.height() + close->sizeHint().height() + 30);
    dialog->show();
}

KrellkamMonitor::KrellkamMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
}

KrellkamMonitor::~KrellkamMonitor() = default;

QString KrellkamMonitor::id() const
{
    return QStringLiteral("krellkam");
}

QString KrellkamMonitor::displayName() const
{
    return QStringLiteral("Krellkam");
}

int KrellkamMonitor::tickIntervalMs() const
{
    return qBound(1000,
        QSettings().value(QStringLiteral("plugins/krellkam/interval_ms"), 5000).toInt(),
        300000);
}

QWidget *KrellkamMonitor::createWidget(QWidget *parent)
{
    auto *panel = new Panel(theme(), parent);
    panel->setSurfaceKey(QStringLiteral("panel_bg_krellkam"));

    m_field = new KrellkamField(theme(), panel);
    m_field->setSources(sources());
    panel->addWidget(m_field);
    tick();
    return panel;
}

void KrellkamMonitor::tick()
{
    if (!m_field) return;
    const QList<KrellkamSource> current = sources();
    m_field->setSources(current);
    m_field->refresh();
}

QList<KrellkamSource> KrellkamMonitor::sources() const
{
    return configuredSources();
}

QString KrellkamPlugin::pluginId() const
{
    return QStringLiteral("io.krellix.krellkam");
}

QString KrellkamPlugin::pluginName() const
{
    return QStringLiteral("Krellkam");
}

QString KrellkamPlugin::pluginVersion() const
{
    return QStringLiteral("0.1.0");
}

QList<MonitorBase *> KrellkamPlugin::createMonitors(Theme *theme, QObject *parent)
{
    if (!QSettings().value(QStringLiteral("plugins/krellkam/enabled"), true).toBool())
        return {};
    return {new KrellkamMonitor(theme, parent)};
}
