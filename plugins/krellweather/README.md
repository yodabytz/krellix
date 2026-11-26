# KrellWeather icons

The monitor draws the icon into its original 30 x 24 logical-pixel slot.
Settings > Plugins > Krellweather > Weather icons selects Color (default),
Theme, or Monochrome. The choice is saved per local profile. It does not
start another weather request.

The renderer uses Qt paths and transparent, device-pixel-aware cached images.
The classifier uses the Aviation Weather Center's
[METAR weather groups](https://aviationweather.gov/viewer/help/#weather-symbols)
and documented sky cover. METAR does not classify tropical cyclones or certify
a severe thunderstorm; the hurricane, tropical storm, and severe thunderstorm
artwork is preview-only. Generic windy and cloudy icons are also included in the
preview; METAR's squall code and explicit cloud cover groups take precedence.
Day and night use the station's longitude and observation time as an approximate
local hour, not a sunrise/sunset calculation.

## Developer preview

Build with `BUILD_TESTING=ON`, then run:

```sh
QT_QPA_PLATFORM=offscreen build/bin/krellweather-icons --preview weather-preview
```

This generates nine PNG contact sheets from the same vector code used by the
monitor: all icon variants at 16, 24, 32, 48, and 64 pixels in three modes and
on three backgrounds. The PNGs are not runtime assets.

`krellweather-layout` accepts optional `KRELLIX_WEATHER_SNAPSHOTS` and
`KRELLIX_WEATHER_BASELINE` directories. It uses fixed observations and
compares the full panel pixel-for-pixel outside the icon rectangle.
