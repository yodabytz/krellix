# Krelldacious on Windows

Install Audacious with its bundled `audtool.exe`. Extract the signed ZIP
and run `./install-krelldacious.ps1 -KrellixDirectory C:/path/to/Krellix`.
Reopen the local Krellix window and enable Krelldacious in Settings / Plugins.

The panel uses the existing themed controls: Open Audacious, play/pause,
previous/next track, the track title, and Audacious volume. The volume slider
sends commands to Audacious, and the plugin does not accept remote playback commands. Other platforms keep their
existing backends. Audacious is not bundled in the plugin.

Windows discovers the installed player in standard Audacious directories,
or finds a portable Audacious player already running in the desktop session.
The helper runs asynchronously, without console popups, with bounded output
and a deadline. If Audacious is not installed or cannot answer its own
control commands, the panel distinguishes those states.

By default the panel controls the first Audacious instance. If you launch
a numbered instance, advanced users may set
`plugins/krelldacious/windows_instance` to 1-9 in the local Krellix profile.
The native integration fixture uses instance 9 with disposable tracks.
