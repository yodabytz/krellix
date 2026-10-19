Krellix for macOS

Drag Krellix.app and Krellix All.app into Applications.
Monitor windows remain visible without a Dock icon or Command-Tab entry.
Right-click a monitor for Settings or Quit. Closing Settings keeps it running.

For a named remote monitor:
/Applications/Krellix.app/Contents/MacOS/krellix --host server.example --instance server1
Each instance remembers its own settings, theme and window position.
Servers must run krellixd and allow the client's current home/VPN IP.
No SSH tunnel is required. Unavailable server sensors are not fabricated.

To configure background launching, run these commands in Terminal once:
/Applications/Krellix.app/Contents/Resources/cli/krellix-all add local
/Applications/Krellix.app/Contents/Resources/cli/krellix-all add server1 server.example
/Applications/Krellix.app/Contents/Resources/cli/krellix-all add server2 another.example
Then double-click Krellix All.app. Repeated starts do not duplicate its jobs.
The same helper accepts stop and status. Launching is on demand, not at login.
No personal hosts, credentials, mail accounts, or VPN configurations ship.

For shorter commands, symlink the executable and helper into a directory
on your PATH. If /usr/local/bin is writable and these names do not exist:
ln -s /Applications/Krellix.app/Contents/MacOS/krellix /usr/local/bin/krellix
ln -s /Applications/Krellix.app/Contents/Resources/cli/krellix-all /usr/local/bin/krellix-all

This package contains Intel x86_64 binaries. Apple Silicon requires Rosetta
and has not been tested. Qt/runtime libraries are bundled; no Homebrew needed.
Optional external tools/services used by plugins remain separately configured.

Bluetooth is included but disabled by default. Enable it in Settings > Plugins.
It shows compact, theme-colored device names and available battery percentages.
Allow Bluetooth access if macOS asks, so it can read connected BLE accessories.
An em dash means the device/OS did not provide a battery level. Classic headset
batteries are not available on every Mac/device combination.

The build is ad-hoc signed, not Apple Developer ID signed or notarized.
macOS may require approving it in System Settings > Privacy & Security after
you try opening it. Do not disable Gatekeeper globally.
