#pragma once

#include "theme/Theme.h"

#include <QPainter>

namespace KrellHealthArtwork {

inline QPixmap heart(const Theme &theme)
{
    return theme.pixmap(QStringLiteral("panel_icon_krellhealth_heart"));
}

inline QSize requestedSize(const Theme &theme, const QPixmap &icon)
{
    const int size = theme.metric(QStringLiteral("panel_icon_krellhealth_heart_size"), 0);
    if (icon.isNull() || size <= 0) return {};
    const int edge = qBound(8, size, 32);
    return icon.size().scaled(edge, edge, Qt::KeepAspectRatio);
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
