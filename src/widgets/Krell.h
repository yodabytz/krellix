#pragma once

#include <QWidget>

class Theme;

// A single horizontal indicator that animates between 0..1 of full scale.
// Phase 0 paints a flat track with a moving notch; later phases will support
// sprite-frame krells driven by the theme.
class Krell : public QWidget
{
    Q_OBJECT

public:
    explicit Krell(Theme *theme, QWidget *parent = nullptr);
    ~Krell() override;

    // Clamps to [0, 1].
    void setValue(double normalized);
    double value() const { return m_value; }

    enum class AlertLevel { None, Warning, Critical };
    void setAlertLevel(AlertLevel level);
    AlertLevel alertLevel() const { return m_alertLevel; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onThemeChanged();

private:
    Theme     *m_theme;
    double     m_value = 0.0;
    AlertLevel m_alertLevel = AlertLevel::None;

    Q_DISABLE_COPY_MOVE(Krell)
};
