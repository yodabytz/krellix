#pragma once

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
    explicit Chart(Theme *theme, QWidget *parent = nullptr);
    ~Chart() override;

    void appendSample(double value);
    void setMaxValue(double maxValue);          // full-scale; clamped >0
    void setCapacity(int samples);              // ring size; clamped [16,4096]

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onThemeChanged();

private:
    void rebuildCapacityForWidth();

    Theme *m_theme;
    std::vector<double> m_samples;     // ring buffer; size <= m_capacity
    int    m_head = 0;                 // next write index
    int    m_capacity = 128;
    double m_max = 1.0;

    Q_DISABLE_COPY_MOVE(Chart)
};
