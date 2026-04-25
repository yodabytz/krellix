#pragma once

#include <QString>
#include <QWidget>

class Theme;
class Decal;
class Krell;
class Chart;
class QVBoxLayout;

// Container for one monitor's UI: optional title, optional krell, optional
// chart, and any number of decals. Paints its themed border and background.
class Panel : public QWidget
{
    Q_OBJECT

public:
    explicit Panel(Theme *theme, QWidget *parent = nullptr);
    ~Panel() override;

    void   setTitle(const QString &title);
    Decal *addDecal(const QString &fontKey   = QStringLiteral("label"),
                    const QString &colorKey  = QStringLiteral("text_primary"));
    Krell *addKrell();
    Chart *addChart();

    Theme *theme() const { return m_theme; }

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onThemeChanged();

private:
    Theme       *m_theme;
    QVBoxLayout *m_layout       = nullptr;
    Decal       *m_titleDecal   = nullptr;

    Q_DISABLE_COPY_MOVE(Panel)
};
