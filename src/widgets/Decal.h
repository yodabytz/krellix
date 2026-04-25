#pragma once

#include <QString>
#include <QWidget>

class Theme;

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

private slots:
    void onThemeChanged();

private:
    Theme  *m_theme;
    QString m_text;
    QString m_fontKey;
    QString m_colorKey;

    Q_DISABLE_COPY_MOVE(Decal)
};
