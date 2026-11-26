#pragma once

class Theme;

namespace ThemePreferences {

// Profile preferences survive theme loads. A negative height clears its override.
void setPreferredHeights(Theme &theme, int chartHeight, int krellHeight);

} // namespace ThemePreferences
