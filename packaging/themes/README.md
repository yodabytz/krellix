# Download-only Krellix themes

Fenrir, Leviathan, Anubis, Nucleo, Kronix, Obsidian, Arctic, Spectrum,
BlueHeart, and Bluemask are [separate downloads](https://cerberix.org/extras/krellix/themes/).
The default theme set is in `../../themes/` and includes Cheese. Do not copy
the directories here into an application bundle or a system package.

Unpack the archive into your user theme directory, for example
`~/.config/krellix/themes/` on macOS and Linux, then choose the theme under
Settings > Appearance. Each theme directory contains `theme.json` and any
images it needs. The textured themes render best with the current Krellix
version and a panel width around 240.

KrellMail uses `images.panel_icon_krellmail` for the idle icon. An optional
`images.panel_icon_krellmail_unread` object with `image` and `frames`
(2 to 128 equally wide frames in one horizontal row) supplies the unread
animation at 50 ms per frame. Bluemask provides both. Without usable theme
artwork KrellMail keeps its built-in icon.
