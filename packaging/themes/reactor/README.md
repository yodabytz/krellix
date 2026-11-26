# Reactor

Reactor is an optional Krellix theme, not part of the bundled theme set.
Install the `reactor` directory beside your other user themes and select it
in Settings. Settings > Appearance > Theme animation offers Full, Reduced,
and Off. Older versions that do not recognize `animation.json` draw
the regular static artwork from `theme.json`.

For the themed mail alert, select Follow Theme in KrellMail settings. For
the supplied weather art, select Theme in KrellWeather settings. The
weather icons are static. Condition and day/night slots without artwork
use the standard vector fallback.

The header runs at its nominal speed until CPU samples are available. It
uses the local or remote CPU already shown by the instance; it does not
start a separate poll. Reduced motion limits frame rates to four frames
per second and disables CPU-dependent speed.
