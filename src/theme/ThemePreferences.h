#pragma once

class Theme;

namespace ThemePreferences {

inline constexpr int defaultChartHeight = 32;

// Profile preferences survive theme loads. A negative height clears its override.
void setPreferredHeights(Theme &theme, int chartHeight, int krellHeight);

} // namespace ThemePreferences
