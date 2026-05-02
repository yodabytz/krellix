#include "KrellSpectrumPlugin.h"

#include "theme/Theme.h"
#include "widgets/Panel.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QSettings>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <complex>

namespace {

constexpr int kSampleRate = 48000;
constexpr int kFrameSamples = 256;
constexpr double kPi = 3.14159265358979323846;

KrellSpectrumConfig configuredSpectrum()
{
    QSettings s;
    KrellSpectrumConfig c;
    c.visualMode = s.value(QStringLiteral("plugins/krellspectrum/visual_mode"),
                           QStringLiteral("bars")).toString();
    c.colorMode = s.value(QStringLiteral("plugins/krellspectrum/color_mode"),
                          QStringLiteral("gradient")).toString();
    c.backend = s.value(QStringLiteral("plugins/krellspectrum/backend"),
                        QStringLiteral("auto")).toString();
    c.device = s.value(QStringLiteral("plugins/krellspectrum/device")).toString().trimmed();
    c.staticColor = QColor(s.value(QStringLiteral("plugins/krellspectrum/static_color"),
                                   QStringLiteral("#56c7ff")).toString());
    if (!c.staticColor.isValid()) c.staticColor = QColor(86, 199, 255);
    c.bands = qBound(16, s.value(QStringLiteral("plugins/krellspectrum/bands"), 32).toInt(), 128);
    c.fps = qBound(8, s.value(QStringLiteral("plugins/krellspectrum/fps"), 45).toInt(), 60);
    c.sensitivity = qBound(0.2,
                           s.value(QStringLiteral("plugins/krellspectrum/sensitivity"), 1.35).toDouble(),
                           8.0);
    c.smoothing = qBound(0.0,
                         s.value(QStringLiteral("plugins/krellspectrum/smoothing"), 0.38).toDouble(),
                         0.95);
    c.peakHold = s.value(QStringLiteral("plugins/krellspectrum/peak_hold"), true).toBool();
    c.stereoSplit = s.value(QStringLiteral("plugins/krellspectrum/stereo_split"), false).toBool();
    return c;
}

bool configNeedsRestart(const KrellSpectrumConfig &a, const KrellSpectrumConfig &b)
{
    return a.backend != b.backend || a.device != b.device;
}

bool executableExists(const QString &program)
{
    return !QStandardPaths::findExecutable(program).isEmpty();
}

QString commandOutput(const QString &program, const QStringList &args, int timeoutMs = 300)
{
    if (!executableExists(program)) return QString();
    QProcess proc;
    proc.setProgram(program);
    proc.setArguments(args);
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start();
    if (!proc.waitForStarted(timeoutMs)) return QString();
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(50);
        return QString();
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
        return QString();
    return QString::fromLocal8Bit(proc.readAllStandardOutput()).trimmed();
}

QString defaultPulseMonitorSource()
{
    QString sink = commandOutput(QStringLiteral("pactl"),
                                 {QStringLiteral("get-default-sink")});
    if (!sink.isEmpty()) {
        const QString monitor = sink + QStringLiteral(".monitor");
        const QString sources = commandOutput(QStringLiteral("pactl"),
                                             {QStringLiteral("list"),
                                              QStringLiteral("short"),
                                              QStringLiteral("sources")},
                                             500);
        if (sources.contains(monitor))
            return monitor;
    }

    const QString sources = commandOutput(QStringLiteral("pactl"),
                                         {QStringLiteral("list"),
                                          QStringLiteral("short"),
                                          QStringLiteral("sources")},
                                         500);
    const QStringList lines = sources.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList cols = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                            Qt::SkipEmptyParts);
        if (cols.size() >= 2 && cols.at(1).endsWith(QStringLiteral(".monitor")))
            return cols.at(1);
    }
    return QString();
}

QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

double clamp01(double v)
{
    return std::clamp(v, 0.0, 1.0);
}

QString normalizedMode(QString mode)
{
    mode = mode.trimmed().toLower();
    if (mode == QStringLiteral("smooth")) return QStringLiteral("smooth_bars");
    if (mode == QStringLiteral("wave")) return QStringLiteral("waveform");
    if (mode == QStringLiteral("filled")) return QStringLiteral("filled_waveform");
    if (mode == QStringLiteral("radial")) return QStringLiteral("circular");
    if (mode == QStringLiteral("dots")) return QStringLiteral("particles");
    return mode.isEmpty() ? QStringLiteral("bars") : mode;
}

} // namespace

void KrellSpectrumProcessor::configure(int bands, double smoothing, double sensitivity)
{
    m_bandCount = qBound(16, bands, 128);
    m_smoothing = qBound(0.0, smoothing, 0.95);
    m_sensitivity = qBound(0.2, sensitivity, 8.0);
    if (m_bands.size() != m_bandCount) {
        m_bands = QVector<float>(m_bandCount, 0.0f);
        m_peaks = QVector<float>(m_bandCount, 0.0f);
    }
}

void KrellSpectrumProcessor::ensureFftSize(int sampleCount)
{
    int size = 1;
    while (size < sampleCount && size < 4096) size <<= 1;
    size = qBound(512, size, 4096);
    if (m_fftSize != size) m_fftSize = size;
}

void KrellSpectrumProcessor::process(const QVector<float> &samples)
{
    if (samples.isEmpty()) return;

    ensureFftSize(samples.size());
    configure(m_bandCount, m_smoothing, m_sensitivity);

    m_waveform.resize(qMin(samples.size(), 512));
    const int stride = qMax(1, samples.size() / qMax(1, m_waveform.size()));
    double sum = 0.0;
    for (int i = 0; i < m_waveform.size(); ++i) {
        const float v = samples.at(qMin(samples.size() - 1, i * stride));
        m_waveform[i] = v;
        sum += std::abs(static_cast<double>(v));
    }
    m_level = clamp01((sum / qMax(1, m_waveform.size())) * m_sensitivity * 3.0);
    m_beat = m_level > qMax(0.18, m_levelHistory * 1.75);
    m_levelHistory = m_levelHistory * 0.94 + m_level * 0.06;

    QVector<double> real(m_fftSize, 0.0);
    QVector<double> imag(m_fftSize, 0.0);
    const int copyCount = qMin(samples.size(), m_fftSize);
    for (int i = 0; i < copyCount; ++i) {
        const double window = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (m_fftSize - 1));
        real[i] = static_cast<double>(samples.at(i)) * window;
    }
    fft(real, imag);

    const int bins = m_fftSize / 2;
    QVector<double> mags(bins, 0.0);
    double maxMag = m_noiseFloor;
    for (int i = 1; i < bins; ++i) {
        const double mag = std::sqrt(real.at(i) * real.at(i) + imag.at(i) * imag.at(i));
        mags[i] = mag;
        maxMag = qMax(maxMag, mag);
    }
    m_noiseFloor = m_noiseFloor * 0.985 + maxMag * 0.015;
    const double norm = qMax(0.0001, qMax(maxMag * 0.72, m_noiseFloor * 1.5));

    QVector<float> next(m_bandCount, 0.0f);
    const double minFreq = 35.0;
    const double maxFreq = 11025.0;
    const double logMin = std::log(minFreq);
    const double logMax = std::log(maxFreq);
    for (int b = 0; b < m_bandCount; ++b) {
        const double f0 = std::exp(logMin + (logMax - logMin) * b / m_bandCount);
        const double f1 = std::exp(logMin + (logMax - logMin) * (b + 1) / m_bandCount);
        int bin0 = qBound(1, static_cast<int>(std::floor(f0 * m_fftSize / kSampleRate)), bins - 1);
        int bin1 = qBound(bin0 + 1, static_cast<int>(std::ceil(f1 * m_fftSize / kSampleRate)), bins);
        double acc = 0.0;
        for (int i = bin0; i < bin1; ++i) acc += mags.at(i);
        const double avg = acc / qMax(1, bin1 - bin0);
        next[b] = static_cast<float>(clamp01(std::log1p(avg / norm * 4.0 * m_sensitivity)
                                             / std::log(5.0)));
    }

    for (int i = 0; i < m_bandCount; ++i) {
        const double current = m_bands.at(i);
        const double candidate = next.at(i);
        const double smoothing = candidate > current ? qMin(m_smoothing, 0.18) : m_smoothing;
        const float smoothed = static_cast<float>(current * smoothing
            + candidate * (1.0 - smoothing));
        m_bands[i] = smoothed;
        m_peaks[i] = qMax(smoothed, m_peaks.at(i) * 0.965f - 0.002f);
    }
}

void KrellSpectrumProcessor::fft(QVector<double> &real, QVector<double> &imag) const
{
    const int n = real.size();
    int j = 0;
    for (int i = 1; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
    }

    for (int len = 2; len <= n; len <<= 1) {
        const double angle = -2.0 * kPi / len;
        const double wlenR = std::cos(angle);
        const double wlenI = std::sin(angle);
        for (int i = 0; i < n; i += len) {
            double wr = 1.0;
            double wi = 0.0;
            for (int k = 0; k < len / 2; ++k) {
                const int u = i + k;
                const int v = i + k + len / 2;
                const double vr = real[v] * wr - imag[v] * wi;
                const double vi = real[v] * wi + imag[v] * wr;
                real[v] = real[u] - vr;
                imag[v] = imag[u] - vi;
                real[u] += vr;
                imag[u] += vi;
                const double nextWr = wr * wlenR - wi * wlenI;
                wi = wr * wlenI + wi * wlenR;
                wr = nextWr;
            }
        }
    }
}

KrellSpectrumAudioCapture::KrellSpectrumAudioCapture(QObject *parent)
    : QObject(parent)
{
    m_frame.reserve(kFrameSamples);
}

KrellSpectrumAudioCapture::~KrellSpectrumAudioCapture()
{
    stop();
}

void KrellSpectrumAudioCapture::start(KrellSpectrumConfig config)
{
    stop();
    m_config = config;
    m_minEmitMs = qMax(1, 1000 / qBound(8, m_config.fps, 60));
    m_emitTimer.restart();
    m_stopping = false;
    m_backendIndex = 0;
    m_backends.clear();
    const QString backend = config.backend.trimmed().toLower();
    if (backend == QStringLiteral("pipewire")) {
        m_backends << QStringLiteral("pw-record");
    } else if (backend == QStringLiteral("pulse")) {
        m_backends << QStringLiteral("parec");
    } else {
        if (!defaultPulseMonitorSource().isEmpty())
            m_backends << QStringLiteral("parec") << QStringLiteral("pw-record");
        else
            m_backends << QStringLiteral("pw-record") << QStringLiteral("parec");
    }
    startNextBackend();
}

void KrellSpectrumAudioCapture::stop()
{
    m_stopping = true;
    if (!m_process) return;
    QProcess *p = m_process;
    m_process = nullptr;
    p->disconnect(this);
    p->terminate();
    if (p->state() != QProcess::NotRunning && !p->waitForFinished(300))
        p->kill();
    p->deleteLater();
    m_pendingBytes.clear();
    m_frame.clear();
}

QString KrellSpectrumAudioCapture::programForBackend(const QString &backend) const
{
    return backend;
}

QStringList KrellSpectrumAudioCapture::argsForBackend(const QString &program,
                                                      const KrellSpectrumConfig &config) const
{
    QStringList args;
    const QString device = resolvedCaptureDevice(program, config);
    if (program == QStringLiteral("pw-record")) {
        args << QStringLiteral("--raw")
             << QStringLiteral("--format=s16")
             << QStringLiteral("--rate=%1").arg(kSampleRate)
             << QStringLiteral("--channels=1")
             << QStringLiteral("--latency=10ms");
        if (!device.isEmpty())
            args << QStringLiteral("--target=%1").arg(device);
        args << QStringLiteral("-");
    } else {
        args << QStringLiteral("--raw")
             << QStringLiteral("--format=s16le")
             << QStringLiteral("--rate=%1").arg(kSampleRate)
             << QStringLiteral("--channels=1")
             << QStringLiteral("--latency-msec=10")
             << QStringLiteral("--process-time-msec=5");
        if (!device.isEmpty())
            args << QStringLiteral("--device=%1").arg(device);
    }
    return args;
}

QString KrellSpectrumAudioCapture::resolvedCaptureDevice(const QString &program,
                                                         const KrellSpectrumConfig &config) const
{
    const QString configured = config.device.trimmed();
    if (!configured.isEmpty()) return configured;

    const QString monitor = defaultPulseMonitorSource();
    if (!monitor.isEmpty())
        return monitor;

    if (program == QStringLiteral("parec"))
        return QStringLiteral("@DEFAULT_MONITOR@");
    return QString();
}

void KrellSpectrumAudioCapture::startNextBackend()
{
    while (m_backendIndex < m_backends.size()) {
        const QString program = programForBackend(m_backends.at(m_backendIndex++));
        if (!executableExists(program))
            continue;

        m_process = new QProcess(this);
        m_process->setProgram(program);
        m_process->setArguments(argsForBackend(program, m_config));
        m_process->setProcessChannelMode(QProcess::SeparateChannels);
        connect(m_process, &QProcess::readyReadStandardOutput,
                this, &KrellSpectrumAudioCapture::readAudio);
        connect(m_process, &QProcess::errorOccurred,
                this, &KrellSpectrumAudioCapture::processError);
        connect(m_process,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &KrellSpectrumAudioCapture::processFinished);
        const QString device = resolvedCaptureDevice(program, m_config);
        emit statusChanged((program == QStringLiteral("pw-record")
            ? QStringLiteral("PipeWire")
            : QStringLiteral("PulseAudio"))
            + (device.isEmpty() ? QStringLiteral(" capture")
                                : QStringLiteral(" monitor: ") + device));
        m_process->start();
        return;
    }

    emit statusChanged(QStringLiteral("no PipeWire/PulseAudio capture tool available"));
}

void KrellSpectrumAudioCapture::readAudio()
{
    if (!m_process) return;
    consumeBytes(m_process->readAllStandardOutput());
}

void KrellSpectrumAudioCapture::processFinished(int, QProcess::ExitStatus)
{
    if (m_stopping) return;
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    startNextBackend();
}

void KrellSpectrumAudioCapture::processError(QProcess::ProcessError)
{
    if (m_stopping) return;
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    startNextBackend();
}

void KrellSpectrumAudioCapture::consumeBytes(const QByteArray &bytes)
{
    m_pendingBytes.append(bytes);
    const int usable = (m_pendingBytes.size() / 2) * 2;
    const char *data = m_pendingBytes.constData();
    for (int i = 0; i < usable; i += 2) {
        const auto lo = static_cast<unsigned char>(data[i]);
        const auto hi = static_cast<signed char>(data[i + 1]);
        const qint16 sample = static_cast<qint16>((static_cast<qint16>(hi) << 8) | lo);
        m_frame.append(static_cast<float>(sample) / 32768.0f);
        if (m_frame.size() >= kFrameSamples) {
            if (!m_emitTimer.isValid() || m_emitTimer.elapsed() >= m_minEmitMs) {
                emit samplesReady(m_frame);
                m_emitTimer.restart();
            }
            m_frame.clear();
        }
    }
    m_pendingBytes.remove(0, usable);
}

KrellSpectrumWidget::KrellSpectrumWidget(Theme *theme, QWidget *parent)
    : QWidget(parent)
    , m_theme(theme)
{
    Q_ASSERT(m_theme);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_theme, &Theme::themeChanged, this, &KrellSpectrumWidget::onThemeChanged);
    m_lastPaint.start();
    setConfig(configuredSpectrum());
}

KrellSpectrumWidget::~KrellSpectrumWidget() = default;

void KrellSpectrumWidget::setConfig(const KrellSpectrumConfig &config)
{
    m_config = config;
    m_config.visualMode = normalizedMode(m_config.visualMode);
    m_processor.configure(m_config.bands, m_config.smoothing, m_config.sensitivity);
    updateGeometry();
    update();
}

void KrellSpectrumWidget::setStatus(const QString &status)
{
    m_status = status;
    if (!m_haveData)
        update();
}

QSize KrellSpectrumWidget::sizeHint() const
{
    const int h = qBound(24,
        QSettings().value(QStringLiteral("plugins/krellspectrum/height"),
                          m_theme->metric(QStringLiteral("chart_height"), 44)).toInt(),
        220);
    return QSize(0, h);
}

QSize KrellSpectrumWidget::minimumSizeHint() const
{
    return QSize(0, 22);
}

void KrellSpectrumWidget::setSamples(const QVector<float> &samples)
{
    m_processor.configure(m_config.bands, m_config.smoothing, m_config.sensitivity);
    m_processor.process(samples);
    m_haveData = true;
    const qint64 minFrameMs = 1000 / qMax(1, m_config.fps);
    if (m_lastPaint.elapsed() >= minFrameMs) {
        m_lastPaint.restart();
        update();
    }
}

void KrellSpectrumWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    const QRect r = rect();
    paintBackground(p, r);

    if (!m_haveData) {
        paintStatus(p, r);
        return;
    }

    const QString mode = normalizedMode(m_config.visualMode);
    if (mode == QStringLiteral("smooth_bars")) paintBars(p, r, true);
    else if (mode == QStringLiteral("waveform")) paintWaveform(p, r, false);
    else if (mode == QStringLiteral("filled_waveform")) paintWaveform(p, r, true);
    else if (mode == QStringLiteral("circular")) paintRadial(p, r);
    else if (mode == QStringLiteral("particles")) paintDots(p, r);
    else paintBars(p, r, false);
}

void KrellSpectrumWidget::onThemeChanged()
{
    updateGeometry();
    update();
}

QColor KrellSpectrumWidget::baseColor() const
{
    if (m_config.colorMode == QStringLiteral("theme"))
        return m_theme->textStyle(QStringLiteral("text_accent"),
                                  QStringLiteral("chart_line_default")).color;
    if (m_config.staticColor.isValid())
        return m_config.staticColor;
    return m_theme->color(QStringLiteral("chart_line_default"), QColor(86, 199, 255));
}

QColor KrellSpectrumWidget::bandColor(int index, int count, float value) const
{
    const QString mode = m_config.colorMode.trimmed().toLower();
    QColor base = baseColor();
    if (mode == QStringLiteral("per_band")) {
        const int hue = (index * 300 / qMax(1, count) + 175) % 360;
        return QColor::fromHsv(hue, 190, 235, 230);
    }
    if (mode == QStringLiteral("reactive")) {
        const int hue = qBound(130, 200 - static_cast<int>(value * 150), 210);
        return QColor::fromHsv(hue, 210, qBound(120, 150 + static_cast<int>(value * 105), 255), 235);
    }
    base.setAlpha(230);
    return base;
}

void KrellSpectrumWidget::paintBackground(QPainter &p, const QRect &rect) const
{
    const QColor fallback = m_theme->color(QStringLiteral("chart_bg"), QColor(4, 8, 11));
    p.fillRect(rect, m_theme->brush(QStringLiteral("chart_bg"), rect, fallback));
    QColor grid = m_theme->color(QStringLiteral("chart_grid"), QColor(80, 95, 105));
    grid.setAlpha(70);
    p.setPen(QPen(grid, 1));
    const int lines = qBound(2, 2 + static_cast<int>(m_processor.level() * 5.0), 7);
    for (int i = 1; i < lines; ++i) {
        const int y = rect.top() + rect.height() * i / lines;
        p.drawLine(rect.left(), y, rect.right(), y);
    }
}

void KrellSpectrumWidget::paintStatus(QPainter &p, const QRect &rect) const
{
    const Theme::TextStyle ts = m_theme->textStyle(QStringLiteral("chart_overlay"),
                                                   QStringLiteral("text_secondary"));
    const QString text = m_status.isEmpty() ? QStringLiteral("waiting for audio") : m_status;
    const QRect tr = rect.adjusted(4, 1, -4, -1);
    if (ts.shadow.present) {
        p.setPen(ts.shadow.color);
        p.drawText(tr.translated(ts.shadow.offsetX, ts.shadow.offsetY),
                   Qt::AlignCenter | Qt::TextWordWrap, text);
    }
    p.setPen(ts.color.isValid() ? ts.color
                                : m_theme->color(QStringLiteral("text_secondary"), Qt::gray));
    p.drawText(tr, Qt::AlignCenter | Qt::TextWordWrap, text);
}

void KrellSpectrumWidget::paintBars(QPainter &p, const QRect &rect, bool smooth)
{
    const QVector<float> &values = m_processor.bands();
    if (values.isEmpty()) return;
    const int count = values.size();
    const qreal gap = smooth ? 1.0 : qMax(1.0, rect.width() / 120.0);
    const qreal w = qMax(1.0, (rect.width() - gap * (count - 1)) / count);
    QPainterPath smoothPath;
    QPainterPath fillPath;

    if (smooth) {
        fillPath.moveTo(rect.left(), rect.bottom());
        for (int i = 0; i < count; ++i) {
            const qreal x = rect.left() + (i + 0.5) * rect.width() / count;
            const qreal y = rect.bottom() - values.at(i) * (rect.height() - 2);
            if (i == 0) smoothPath.moveTo(x, y);
            else smoothPath.lineTo(x, y);
            fillPath.lineTo(x, y);
        }
        fillPath.lineTo(rect.right(), rect.bottom());
        fillPath.closeSubpath();
        QColor fill = baseColor();
        fill.setAlpha(85);
        p.fillPath(fillPath, fill);
        p.setPen(QPen(baseColor().lighter(m_processor.beat() ? 145 : 115), 1.6));
        p.drawPath(smoothPath);
        return;
    }

    for (int i = 0; i < count; ++i) {
        const float v = values.at(i);
        const qreal h = qMax(1.0, v * (rect.height() - 2));
        const QRectF br(rect.left() + i * (w + gap), rect.bottom() - h, w, h);
        QColor color = bandColor(i, count, v);
        if (m_config.colorMode == QStringLiteral("gradient")) {
            QLinearGradient g(br.topLeft(), br.bottomLeft());
            g.setColorAt(0.0, color.lighter(145));
            g.setColorAt(1.0, QColor(90, 230, 145, color.alpha()));
            p.fillRect(br, g);
        } else {
            p.fillRect(br, color);
        }
        if (m_config.peakHold && i < m_processor.peaks().size()) {
            p.setPen(QPen(withAlpha(color.lighter(160), 180), 1));
            const qreal py = rect.bottom() - m_processor.peaks().at(i) * (rect.height() - 2);
            p.drawLine(QPointF(br.left(), py), QPointF(br.right(), py));
        }
    }
}

void KrellSpectrumWidget::paintWaveform(QPainter &p, const QRect &rect, bool filled)
{
    const QVector<float> &wave = m_processor.waveform();
    if (wave.size() < 2) return;
    const qreal mid = rect.center().y();
    const qreal amp = rect.height() * 0.45;
    QPainterPath path;
    QPainterPath fill;
    fill.moveTo(rect.left(), mid);
    for (int i = 0; i < wave.size(); ++i) {
        const qreal x = rect.left() + i * rect.width() / qMax(1, wave.size() - 1);
        const qreal sample = qBound(-1.0,
                                    static_cast<double>(wave.at(i)) * m_config.sensitivity,
                                    1.0);
        const qreal y = mid - sample * amp;
        if (i == 0) path.moveTo(x, y);
        else path.lineTo(x, y);
        fill.lineTo(x, y);
    }
    fill.lineTo(rect.right(), mid);
    fill.closeSubpath();
    QColor color = baseColor();
    if (filled) {
        color.setAlpha(95);
        p.fillPath(fill, color);
    }
    color.setAlpha(235);
    p.setPen(QPen(color.lighter(m_processor.beat() ? 150 : 115), 1.4));
    p.drawPath(path);
}

void KrellSpectrumWidget::paintRadial(QPainter &p, const QRect &rect)
{
    const QVector<float> &values = m_processor.bands();
    if (values.isEmpty()) return;
    const QPointF c = rect.center();
    const qreal radius = qMin(rect.width(), rect.height()) * 0.25;
    const qreal maxLen = qMin(rect.width(), rect.height()) * 0.23;
    for (int i = 0; i < values.size(); ++i) {
        const double a = -kPi / 2.0 + 2.0 * kPi * i / values.size();
        const qreal inner = radius;
        const qreal outer = radius + static_cast<qreal>(values.at(i)) * maxLen;
        const QPointF p0(c.x() + std::cos(a) * inner, c.y() + std::sin(a) * inner);
        const QPointF p1(c.x() + std::cos(a) * outer, c.y() + std::sin(a) * outer);
        p.setPen(QPen(bandColor(i, values.size(), values.at(i)), 1.5));
        p.drawLine(p0, p1);
    }
    QColor core = baseColor();
    core.setAlpha(m_processor.beat() ? 120 : 70);
    p.setBrush(core);
    p.setPen(Qt::NoPen);
    p.drawEllipse(c, radius * 0.45, radius * 0.45);
}

void KrellSpectrumWidget::paintDots(QPainter &p, const QRect &rect)
{
    const QVector<float> &values = m_processor.bands();
    if (values.isEmpty()) return;
    p.setPen(Qt::NoPen);
    for (int i = 0; i < values.size(); ++i) {
        const float v = values.at(i);
        const qreal x = rect.left() + (i + 0.5) * rect.width() / values.size();
        const qreal y = rect.bottom() - v * (rect.height() - 4) - 2;
        const qreal size = 1.5 + static_cast<qreal>(v) * 4.0;
        QColor color = bandColor(i, values.size(), v);
        color.setAlpha(qBound(80, 120 + static_cast<int>(v * 130), 245));
        p.setBrush(color);
        p.drawEllipse(QPointF(x, y), size, size);
    }
}

KrellSpectrumMonitor::KrellSpectrumMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
    qRegisterMetaType<QVector<float>>("QVector<float>");
    qRegisterMetaType<KrellSpectrumConfig>("KrellSpectrumConfig");
}

KrellSpectrumMonitor::~KrellSpectrumMonitor()
{
    shutdown();
}

QString KrellSpectrumMonitor::id() const
{
    return QStringLiteral("krellspectrum");
}

QString KrellSpectrumMonitor::displayName() const
{
    return QStringLiteral("KrellSpectrum");
}

int KrellSpectrumMonitor::tickIntervalMs() const
{
    return 1000;
}

QWidget *KrellSpectrumMonitor::createWidget(QWidget *parent)
{
    m_config = readConfig();
    auto *panel = new Panel(theme(), parent);
    panel->setSurfaceKey(QStringLiteral("panel_bg_krellspectrum"));

    m_view = new KrellSpectrumWidget(theme(), panel);
    m_view->setConfig(m_config);
    panel->addWidget(m_view);

    startCapture(m_config);
    return panel;
}

void KrellSpectrumMonitor::tick()
{
    const KrellSpectrumConfig next = readConfig();
    if (m_view) m_view->setConfig(next);
    if (configNeedsRestart(m_config, next)) {
        stopCapture();
        startCapture(next);
    }
    m_config = next;
}

void KrellSpectrumMonitor::shutdown()
{
    stopCapture();
}

KrellSpectrumConfig KrellSpectrumMonitor::readConfig() const
{
    return configuredSpectrum();
}

void KrellSpectrumMonitor::startCapture(const KrellSpectrumConfig &config)
{
    if (m_thread || m_capture) return;
    m_thread = new QThread(this);
    m_capture = new KrellSpectrumAudioCapture;
    m_capture->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_capture, &QObject::deleteLater);
    connect(m_thread, &QThread::started, m_capture, [this, config]() {
        if (m_capture) m_capture->start(config);
    });
    connect(m_capture, &KrellSpectrumAudioCapture::samplesReady,
            m_view, &KrellSpectrumWidget::setSamples, Qt::QueuedConnection);
    connect(m_capture, &KrellSpectrumAudioCapture::statusChanged,
            m_view, &KrellSpectrumWidget::setStatus, Qt::QueuedConnection);
    m_thread->start();
}

void KrellSpectrumMonitor::stopCapture()
{
    if (!m_thread) return;
    if (m_capture) {
        QMetaObject::invokeMethod(m_capture, "stop", Qt::BlockingQueuedConnection);
        m_capture = nullptr;
    }
    QThread *thread = m_thread;
    m_thread = nullptr;
    thread->quit();
    if (!thread->wait(1000))
        thread->terminate();
    thread->deleteLater();
}

QString KrellSpectrumPlugin::pluginId() const
{
    return QStringLiteral("io.krellix.krellspectrum");
}

QString KrellSpectrumPlugin::pluginName() const
{
    return QStringLiteral("KrellSpectrum");
}

QString KrellSpectrumPlugin::pluginVersion() const
{
    return QStringLiteral("0.1.0");
}

QList<MonitorBase *> KrellSpectrumPlugin::createMonitors(Theme *theme, QObject *parent)
{
    if (!QSettings().value(QStringLiteral("plugins/krellspectrum/enabled"), true).toBool())
        return {};
    return {new KrellSpectrumMonitor(theme, parent)};
}
