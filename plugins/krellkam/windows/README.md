# KrellKam for Windows

With Krellix closed, verify the signed ZIP, extract it, then run
`./install-kam-moon.ps1 -KrellixDirectory C:/path/to/preview.6/Krellix`.
Open Settings > Plugins > Krellkam, enable the panel, add a source, and
click Apply camera sources. Click an image to open its larger viewer.

- **Auto/Image:** enter a Windows file path, a `file:///C:/...` URL, or an
  HTTP(S) snapshot URL from a camera.
- **MJPEG:** enter the camera's HTTP(S) MJPEG URL.
- **YouTube live:** requires `yt-dlp.exe` and `ffmpeg.exe` on PATH and a
  stream the user can access. The plugin does not include those tools.
- **Command:** off by default. Enable only for a trusted `.exe` that writes
  an image to stdout. Quote paths containing spaces. Arguments are passed
  directly, not through a shell. A camera utility such as FFmpeg can
  provide a frame from a Windows camera using its DirectShow input.

KrellKam does not open a camera without a configured source. Remote
Krellix windows do not read local camera sources or run camera commands.
