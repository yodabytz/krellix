#pragma once

#include "theme/Theme.h"

#include <QPainter>

namespace KrellHealthArtwork {

inline QPixmap heart(const Theme &theme)
{
    return theme.pixmap(QStringLiteral("panel_icon_krellhealth_heart"));
}

inline QPixmap status(const Theme &theme, const QString &label)
{
    QString key;
    if (label == QStringLiteral("OK")) {
        const auto art = theme.pixmap(QStringLiteral("panel_icon_krellhealth_status_healthy"));
        return art.isNull() ? heart(theme) : art;
    }
    if (label.endsWith(QLatin1String(" INFO"))) key = QStringLiteral("info");
    else if (label.endsWith(QLatin1String(" WARN"))) key = QStringLiteral("warning");
    else if (label.endsWith(QLatin1String(" CRIT"))) key = QStringLiteral("critical");
    else if (label == QStringLiteral("CHECK") || label == QStringLiteral("? CHECK")) key = QStringLiteral("check");
    return key.isEmpty() ? QPixmap() : theme.pixmap(QStringLiteral("panel_icon_krellhealth_status_") + key);
}

inline bool hasExplicitStatusSize(const Theme &theme)
{
    return theme.metric(QStringLiteral("panel_icon_krellhealth_status_size"), 0) > 0;
}

inline QSize requestedSize(const Theme &theme, const QPixmap &icon, bool healthy = true,
                           int availableWidth = 4096)
{
    const int statusSize = theme.metric(QStringLiteral("panel_icon_krellhealth_status_size"), 0);
    const int size = statusSize > 0 ? statusSize
        : healthy ? theme.metric(QStringLiteral("panel_icon_krellhealth_heart_size"), 0) : 0;
    if (icon.isNull() || size <= 0) return {};
    const int edge = qBound(8, size, statusSize > 0 ? 80 : 32);
    return icon.size().scaled(qMin(edge, qMax(0, availableWidth)), edge, Qt::KeepAspectRatio);
}

inline void paint(QPainter &painter, const QPixmap &icon, const QRect &slot)
{
    if (slot.isEmpty()) return;
    const auto size = icon.size().scaled(slot.size(), Qt::KeepAspectRatio);
    QRect target(QPoint(), size);
    target.moveCenter(slot.center());
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawPixmap(target, icon);
    painter.restore();
}

} // namespace KrellHealthArtwork
