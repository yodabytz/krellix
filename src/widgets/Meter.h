#pragma once

#include <QWidget>
#include <QString>

class Theme;

// Static chart-backed meter: a themed chart background with an optional
// filled value area and centered text. Used for readings that are better
// shown as current state than as a scrolling history graph.
class Meter : public QWidget
{
    Q_OBJECT

public:
    explicit Meter(Theme *theme,
                   const QString &colorKey = QStringLiteral("krell_indicator"),
                   QWidget *parent = nullptr);
    ~Meter() override;

    void setValue(double normalized);
    void setText(const QString &text);
    void setFillVisible(bool visible);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onThemeChanged();

private:
    Theme  *m_theme;
    QString m_colorKey;
    QString m_text;
    double  m_value = 0.0;
    bool    m_fillVisible = true;

    Q_DISABLE_COPY_MOVE(Meter)
};
