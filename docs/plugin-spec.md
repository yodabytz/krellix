# Krellix Plugin SDK

A plugin is a native Qt 6 module that provides monitors to Krellix.
Plugins execute in the host process with the user's permissions. They
are not sandboxed. Install binaries only from sources you trust.

Start with [KrellHello](../examples/krellhello) and the
[plugin author's guide](https://cerberix.org/extras/krellix/plugins/#build-a-plugin).

## Discovery

The loader scans the user Qt data directory first:

| Platform | Usual user folder |
| --- | --- |
| Linux | `~/.local/share/krellix/plugins/`, or `$XDG_DATA_HOME/krellix/plugins/` |
| macOS | `~/Library/Application Support/krellix/plugins/` |
| Windows | `%LOCALAPPDATA%\krellix\plugins\` |

It also searches the compiled-in system path (usually
`/usr/lib/krellix/plugins/` on Linux), macOS configuration folders and the
Mac bundle's plugin locations. `./plugins/` is searched **only** when
`KRELLIX_DEV_PLUGINS=1` is set for that process. It is relative to the
working directory, not necessarily the executable.

Place the module itself directly in the chosen directory. The loader
recognizes `.so`, `.dylib`, and `.dll`. It skips empty files and modules
larger than 32 MiB, rejects paths resolving outside the root, and caps
the number of loaded factories at 64. Do not leave duplicate copies
in multiple roots. Restart Krellix after installing or replacing a binary.

## Factory and monitor contracts

The factory implements [IKrellixPlugin](../src/sdk/KrellixPlugin.h) and
declares `Q_OBJECT`, `Q_PLUGIN_METADATA(IID KrellixPlugin_iid)`, and
`Q_INTERFACES(IKrellixPlugin)`. Give it a unique stable reverse-domain ID
and a semantic version. Give each monitor its own stable slug ID.

- `createMonitors(theme, parent)` returns QObject-parented monitor instances.
  The host can call it again during a settings or panel rebuild. Keep it cheap
  and return an empty list when the plugin cannot operate on this host.
- `createWidget(parent)` returns a QWidget owned by Qt. Use `QPointer` for
  widgets you reference later. `tick()` updates the UI on the GUI thread;
  the user update interval can slow it below the requested cadence.
- `shutdown()` must stop timers and cancel outstanding replies, processes,
  or workers. Callbacks should have a QObject context, and late results
  must be safe to discard. A monitor can be discarded before its UI is built.
- A plugin module is not hot-reloaded. Its monitor instances can still be
  removed, hidden, or rebuilt while the application is running.

For remote windows, use telemetry from the selected server only. A
local-only plugin should return no monitors when `RemoteSource::instance()`
is set, even if that remote connection is currently down.

## Building and compatibility

Use the matching Krellix source tree and build setup. Linux modules resolve
host SDK symbols at load time. On macOS, use dynamic lookup for host symbols;
on Windows, link against the exported `krellix` CMake target so the module
uses the matching import library. See the [example CMake file](../examples/krellhello/CMakeLists.txt).

The interface IID is `io.krellix.IKrellixPlugin/1.0`. Matching the IID does
not establish C++ or Qt binary compatibility. Match the host version, Qt
runtime, compiler/ABI, OS, and CPU architecture, and test the packaged
combination. Do not ship one native library as a universal plugin or
redistribute mismatched Qt DLLs with it.
