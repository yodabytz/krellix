#pragma once

#include <QColor>
#include <QList>
#include <QWidget>
#include <vector>

class Theme;

// Scrolling history chart. Holds a fixed-capacity ring of samples and
// repaints them as a polyline against a themed grid. New samples push the
// oldest off the left edge.
class Chart : public QWidget
{
    Q_OBJECT

public:
    // colorKey selects which theme color paints the line — e.g.
    // "chart_line_cpu", "chart_line_mem". Falls back to text_primary if
    // the key isn't defined in the theme.
    explicit Chart(Theme *theme,
                   const QString &colorKey = QStringLiteral("chart_line_default"),
                   QWidget *parent = nullptr);
    ~Chart() override;

    void appendSample(double value);             // series 0
    void appendSampleAt(int seriesIdx, double value);
    void setMaxValue(double maxValue);          // full-scale; clamped >0
    void setCapacity(int samples);              // ring size; clamped [16,4096]

    // Configure the chart for N series rendered in distinct colors. The
    // list size determines the number of series; each line shares the
    // same y-scale (m_max). Pass an empty list to revert to single-series
    // mode. Existing samples are dropped on a series-count change.
    void setSeriesColors(const QList<QColor> &colors);

    // Convenience: build N evenly-spaced HSV colors and call
    // setSeriesColors. Useful for "all CPU cores in one chart" displays.
    void setRainbowSeries(int n);

    // Optional text drawn over the chart in the top-left corner — used by
    // CpuMonitor (and any other monitor that wants to label its data
    // inside the graph instead of in a separate decal row, saving height).
    void setOverlayText(const QString &text);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onThemeChanged();

private:
    void rebuildCapacityForWidth();
    double visiblePeak() const;

    Theme  *m_theme;
    QString m_colorKey;
    QString m_overlayText;

    // Single-series ring (legacy) or multi-series (one ring per series).
    std::vector<double>              m_samples;
    std::vector<std::vector<double>> m_multiSamples;
    QList<QColor>                    m_seriesColors;

    int    m_head = 0;                 // next write index (shared)
    int    m_capacity = 128;
    double m_max = 1.0;

    Q_DISABLE_COPY_MOVE(Chart)
};
