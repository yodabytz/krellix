# KrellSpectrum for Windows

This optional Windows x86-64 plugin uses the published Krellix preview.6
host for installer verification. It does not replace Krellix, its runtime,
your profile, or the Linux/macOS plugins.

Close local Krellix, extract the ZIP, and run this in PowerShell:

    ./install-krellspectrum.ps1 -KrellixDirectory C:/path/to/preview.6/Krellix

Reopen local Krellix and enable the visualizer under **Settings > Plugins >
KrellSpectrum**. The visual modes, colors, bands, smoothing, and dimensions
use the same renderer and settings as Linux. Match the theme, panel width,
height, and display scaling for the same appearance.

Windows uses native WASAPI loopback on the default playback device. Use
**Auto** in the existing backend selector; PipeWire and PulseAudio choices
are not used on Windows. Leave **Device** blank to follow the default, or
enter an exact Windows playback-device name or endpoint ID. This captures
what Windows is playing, not the microphone. The plugin does not save or
send audio. Remote Krellix windows do not capture local playback.
