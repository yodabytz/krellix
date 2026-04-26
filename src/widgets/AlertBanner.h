#pragma once

#include <QWidget>

class QLabel;
class QTimer;

// A flashing red banner that appears at the top of MainWindow when a
// connection (e.g. to krellixd) has been lost for more than the debounce
// window. Hidden by default; toggled via showAlert/hideAlert from the
// owner. Internally uses a QTimer to alternate between full-bright and
// dimmer red so it pulses for attention without being seizure-inducing.
class AlertBanner : public QWidget
{
    Q_OBJECT

public:
    explicit AlertBanner(QWidget *parent = nullptr);
    ~AlertBanner() override;

    void showAlert(const QString &message);
    void hideAlert();

private slots:
    void onBlinkTick();

private:
    QLabel *m_label;
    QTimer *m_blinkTimer;
    bool    m_dim = false;

    Q_DISABLE_COPY_MOVE(AlertBanner)
};
