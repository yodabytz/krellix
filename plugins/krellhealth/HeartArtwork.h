#pragma once

#include "theme/Theme.h"

#include <QPainter>

namespace KrellHealthArtwork {

inline QPixmap heart(const Theme &theme)
{
    return theme.pixmap(QStringLiteral("panel_icon_krellhealth_heart"));
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
