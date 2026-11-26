#pragma once

#include "theme/Theme.h"

#include <QPainter>
#include <array>
#include <utility>

namespace SurfaceRendering {

inline std::pair<int, int> fitEdges(int start, int end, int available)
{
    const int total = start + end;
    if (total <= available) return {start, end};
    const int left = qRound(double(start) * available / total);
    return {left, available - left};
}

inline void drawSliced(QPainter &paint, const QPixmap &image, const QRect &target,
                       const Theme::SurfaceGeometry &geometry)
{
    if (image.isNull() || target.isEmpty()) return;
    const auto [sl, sr] = fitEdges(geometry.source.left(), geometry.source.right(), image.width() - 1);
    const auto [st, sb] = fitEdges(geometry.source.top(), geometry.source.bottom(), image.height() - 1);
    int left = geometry.destination.left(), right = geometry.destination.right();
    int top = geometry.destination.top(), bottom = geometry.destination.bottom();
    if (geometry.mode == Theme::SurfaceGeometry::VerticalCaps) {
        const double factor = double(target.width()) / image.width();
        top = qRound(st * factor);
        bottom = qRound(sb * factor);
    }
    const auto [dl, dr] = fitEdges(sl ? left : 0, sr ? right : 0, target.width());
    const auto [dt, db] = fitEdges(st ? top : 0, sb ? bottom : 0, target.height());
    const std::array sx{0, sl, image.width() - sr, image.width()};
    const std::array sy{0, st, image.height() - sb, image.height()};
    const std::array dx{target.x(), target.x() + dl, target.right() + 1 - dr, target.right() + 1};
    const std::array dy{target.y(), target.y() + dt, target.bottom() + 1 - db, target.bottom() + 1};
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            const QRect source(sx[x], sy[y], sx[x + 1] - sx[x], sy[y + 1] - sy[y]);
            const QRect dest(dx[x], dy[y], dx[x + 1] - dx[x], dy[y + 1] - dy[y]);
            if (!source.isEmpty() && !dest.isEmpty())
                paint.drawPixmap(dest, image, source);
        }
    }
}

} // namespace SurfaceRendering
