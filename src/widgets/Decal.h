#pragma once

#include <QString>
#include <QWidget>

class Theme;
class QTimer;

// A text strip painted with theme font/color. Used for labels, values, and
// the panel title row. Repaints automatically when the theme reloads.
class Decal : public QWidget
{
    Q_OBJECT

public:
    Decal(Theme *theme,
          const QString &fontKey,
          const QString &colorKey,
          QWidget *parent = nullptr);
    ~Decal() override;

    void setText(const QString &text);
    QString text() const { return m_text; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private slots:
    void onThemeChanged();
    void onScrollTick();

private:
    int  textPixelWidth() const;
    void updateScrollState();

    Theme  *m_theme;
    QString m_text;
    QString m_fontKey;
    QString m_colorKey;

    QTimer *m_scrollTimer = nullptr;   // child of this widget
    int     m_scrollOffset = 0;
    bool    m_scrolling    = false;

    Q_DISABLE_COPY_MOVE(Decal)
};
