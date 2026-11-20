# KrellWire for Windows

This optional Windows x86-64 plugin uses the published Krellix preview.6
host for installer verification. It does not replace the host or profiles.

Close local Krellix, extract the ZIP, and run this in PowerShell:

    ./install-krellwire.ps1 -KrellixDirectory C:/path/to/preview.6/Krellix

Open local Krellix. Right-click and choose **Settings > Plugins > Krellwire**
to enable the ticker, configure RSS or Atom feeds, and adjust refresh,
headlines per feed, and scrolling speed. Left-click a headline to open it in
the default browser. Right-click opens the normal Krellix menu.

The Windows ticker is local-only. Linux and macOS keep their existing
behavior.
