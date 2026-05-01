#pragma once

#include <QPoint>
#include <QString>
#include <QWidget>

class Theme;
class Decal;
class Krell;
class Chart;
class QVBoxLayout;
class QMouseEvent;

// Container for one monitor's UI: optional title, optional krell, optional
// chart, and any number of decals. Paints its themed border and background.
class Panel : public QWidget
{
    Q_OBJECT

public:
    explicit Panel(Theme *theme, QWidget *parent = nullptr);
    ~Panel() override;

    // Override the theme key the panel uses to look up its background
    // surface. Defaults to "panel_bg" — monitors that want their own
    // tinted bg pass e.g. "panel_bg_cpu" / "panel_bg_mem". The lookup
    // chain falls back to "panel_bg" when the specific key is absent,
    // so themes can provide one or many.
    void setSurfaceKey(const QString &key);

    void   setTitle(const QString &title);
    Decal *addDecal(const QString &fontKey   = QStringLiteral("label"),
                    const QString &colorKey  = QStringLiteral("text_primary"));
    Krell *addKrell();
    Chart *addChart(const QString &colorKey = QStringLiteral("chart_line_default"));

    Theme *theme() const { return m_theme; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
    void onThemeChanged();

private:
    Theme       *m_theme;
    QVBoxLayout *m_layout       = nullptr;
    Decal       *m_titleDecal   = nullptr;
    QString      m_surfaceKey   = QStringLiteral("panel_bg");
    bool         m_dragging     = false;
    QPoint       m_dragOffset;

    Q_DISABLE_COPY_MOVE(Panel)
};
