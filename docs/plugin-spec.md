# krellix Plugin SDK

A krellix plugin is a Qt 6 shared library that exposes one or more monitors
(CPU, GPU, network counters, weather, anything) to the host application.
Plugins are discovered at startup and live for the lifetime of the process.

> Plugins run **as the user, in-process**. They can do anything krellix can.
> Only install plugins you trust.

## Search paths

krellix scans, in order (user takes precedence):

1. `~/.local/share/krellix/plugins/` (user)
2. `/usr/lib/krellix/plugins/` (system, set at compile time)
3. `./plugins/` (relative to working dir, for dev convenience)

A file is loaded only when its canonical path is *directly* inside one of
these roots. Symlinks pointing elsewhere are rejected. Files larger than
32 MiB are rejected. The first 64 plugins encountered are loaded.

## Minimum plugin

`my_plugin.h`:

```cpp
#pragma once
#include <QObject>
#include "sdk/KrellixPlugin.h"

class MyPlugin : public QObject, public IKrellixPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID KrellixPlugin_iid)
    Q_INTERFACES(IKrellixPlugin)

public:
    QString pluginId()      const override { return "io.example.my-plugin"; }
    QString pluginName()    const override { return "My Plugin"; }
    QString pluginVersion() const override { return "0.1.0"; }

    QList<MonitorBase *> createMonitors(Theme *theme,
                                        QObject *parent) override;
};
```

`my_plugin.cpp` returns one or more `MonitorBase` subclasses (subclass it
the same way the built-in monitors do — `createWidget(parent)` to build
the UI, `tick()` to refresh data on the timer).

## Building

```cmake
cmake_minimum_required(VERSION 3.21)
project(my_plugin LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_library(my_plugin SHARED my_plugin.h my_plugin.cpp)
target_link_libraries(my_plugin PRIVATE Qt6::Widgets)

# Header search paths must include krellix's src/ tree (for sdk/, monitors/,
# widgets/, theme/ headers). Either install krellix-dev headers system-wide
# or use add_subdirectory(krellix).
target_include_directories(my_plugin PRIVATE /path/to/krellix/src)
```

Drop the resulting `libmy_plugin.so` into one of the search paths above and
restart krellix.

## Lifecycle

1. krellix instantiates the plugin's `QObject` once at startup.
2. krellix calls `createMonitors(theme, parent)` once.
3. Each returned `MonitorBase`'s `createWidget(parent)` is called once;
   the widget is added to the panel stack.
4. A `QTimer` is created per monitor (parented to the monitor) and ticks at
   `tickIntervalMs()`.
5. Plugins are **never unloaded** while the application is running.

## Versioning

The plugin interface IID is `io.krellix.IKrellixPlugin/1.0`. Bumping the
minor version is a non-breaking addition; bumping the major version means
old plugins won't load until rebuilt.
