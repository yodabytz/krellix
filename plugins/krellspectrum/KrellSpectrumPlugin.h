#pragma once

#include "monitors/MonitorBase.h"
#include "sdk/KrellixPlugin.h"

#include <QColor>
#include <QElapsedTimer>
#include <QPointer>
#include <QProcess>
#include <QThread>
#include <QVector>
#include <QWidget>

class QLabel;

struct KrellSpectrumConfig {
    QString visualMode;
    QString colorMode;
    QString backend;
    QString device;
    QColor staticColor;
    int bands = 32;
    int fps = 30;
    double sensitivity = 1.35;
    double smoothing = 0.72;
    bool peakHold = true;
    bool stereoSplit = false;
};

class KrellSpectrumProcessor
{
public:
    void configure(int bands, double smoothing, double sensitivity);
    void process(const QVector<float> &samples);

    const QVector<float> &bands() const { return m_bands; }
    const QVector<float> &waveform() const { return m_waveform; }
    const QVector<float> &peaks() const { return m_peaks; }
    double level() const { return m_level; }
    bool beat() const { return m_beat; }

private:
    void ensureFftSize(int sampleCount);
    void fft(QVector<double> &real, QVector<double> &imag) const;

    int m_bandCount = 32;
    int m_fftSize = 1024;
    double m_smoothing = 0.72;
    double m_sensitivity = 1.35;
    double m_noiseFloor = 0.0005;
    double m_level = 0.0;
    double m_levelHistory = 0.05;
    bool m_beat = false;
    QVector<float> m_bands;
    QVector<float> m_waveform;
    QVector<float> m_peaks;
};

class KrellSpectrumAudioCapture : public QObject
{
    Q_OBJECT

public:
    explicit KrellSpectrumAudioCapture(QObject *parent = nullptr);
    ~KrellSpectrumAudioCapture() override;

public slots:
    void start(KrellSpectrumConfig config);
    void stop();

signals:
    void samplesReady(const QVector<float> &samples);
    void statusChanged(const QString &status);

private slots:
    void readAudio();
    void processFinished(int exitCode, QProcess::ExitStatus status);
    void processError(QProcess::ProcessError error);

private:
    QString programForBackend(const QString &backend) const;
    QStringList argsForBackend(const QString &program,
                               const KrellSpectrumConfig &config) const;
    QString resolvedCaptureDevice(const QString &program,
                                  const KrellSpectrumConfig &config) const;
    void startNextBackend();
    void consumeBytes(const QByteArray &bytes);

    QProcess *m_process = nullptr;
    KrellSpectrumConfig m_config;
    QStringList m_backends;
    int m_backendIndex = 0;
    QByteArray m_pendingBytes;
    QVector<float> m_frame;
    bool m_stopping = false;
};

class KrellSpectrumWidget : public QWidget
{
    Q_OBJECT

public:
    explicit KrellSpectrumWidget(Theme *theme, QWidget *parent = nullptr);
    ~KrellSpectrumWidget() override;

    void setConfig(const KrellSpectrumConfig &config);
    void setStatus(const QString &status);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

public slots:
    void setSamples(const QVector<float> &samples);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onThemeChanged();

private:
    QColor baseColor() const;
    QColor bandColor(int index, int count, float value) const;
    void paintBackground(QPainter &p, const QRect &rect) const;
    void paintStatus(QPainter &p, const QRect &rect) const;
    void paintBars(QPainter &p, const QRect &rect, bool smooth);
    void paintWaveform(QPainter &p, const QRect &rect, bool filled);
    void paintRadial(QPainter &p, const QRect &rect);
    void paintDots(QPainter &p, const QRect &rect);

    Theme *m_theme = nullptr;
    KrellSpectrumConfig m_config;
    KrellSpectrumProcessor m_processor;
    QString m_status;
    QElapsedTimer m_lastPaint;
    bool m_haveData = false;
};

class KrellSpectrumMonitor : public MonitorBase
{
    Q_OBJECT

public:
    explicit KrellSpectrumMonitor(Theme *theme, QObject *parent = nullptr);
    ~KrellSpectrumMonitor() override;

    QString id() const override;
    QString displayName() const override;
    int tickIntervalMs() const override;
    QWidget *createWidget(QWidget *parent) override;
    void tick() override;
    void shutdown() override;

private:
    KrellSpectrumConfig readConfig() const;
    void startCapture(const KrellSpectrumConfig &config);
    void stopCapture();

    QPointer<KrellSpectrumWidget> m_view;
    QPointer<QLabel> m_label;
    QThread *m_thread = nullptr;
    KrellSpectrumAudioCapture *m_capture = nullptr;
    KrellSpectrumConfig m_config;
};

class KrellSpectrumPlugin : public QObject, public IKrellixPlugin
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

Q_DECLARE_METATYPE(KrellSpectrumConfig)
