cmake_minimum_required(VERSION 3.21)
project(krellix
    VERSION 0.1.0
    DESCRIPTION "Themeable Qt 6 system monitor in the spirit of GKrellM"
    LANGUAGES CXX)

# SemVer pre-release label. Full string ("0.1.0-beta") is what the binary
# reports via --version, the About dialog, and packaging artifacts. Bump
# this (and PROJECT_VERSION) when cutting a new release.
set(KRELLIX_PRERELEASE "beta")
if(KRELLIX_PRERELEASE STREQUAL "")
    set(KRELLIX_VERSION_FULL "${PROJECT_VERSION}")
else()
    set(KRELLIX_VERSION_FULL "${PROJECT_VERSION}-${KRELLIX_PRERELEASE}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wshadow -Wnon-virtual-dtor -Wold-style-cast
        -Woverloaded-virtual -Wcast-align -Wunused
        -Wmisleading-indentation -Wnull-dereference
        -Wdouble-promotion -Wformat=2
    )
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Network DBus)

qt_standard_project_setup()

set(KRELLIX_SOURCES
    src/krellix/main.cpp
    src/krellix/MainWindow.cpp
    src/krellix/MainWindow.h
    src/krellix/PluginLoader.cpp
    src/krellix/PluginLoader.h
    src/krellix/SettingsDialog.cpp
    src/krellix/SettingsDialog.h
    src/remote/RemoteSource.cpp
    src/remote/RemoteSource.h
    src/sdk/KrellixPlugin.h
    src/theme/Theme.cpp
    src/theme/Theme.h
    src/widgets/Panel.cpp
    src/widgets/Panel.h
    src/widgets/Decal.cpp
    src/widgets/Decal.h
    src/widgets/Krell.cpp
    src/widgets/Krell.h
    src/widgets/Meter.cpp
    src/widgets/Meter.h
    src/widgets/Chart.cpp
    src/widgets/Chart.h
    src/widgets/AlertBanner.cpp
    src/widgets/AlertBanner.h
    src/sysdep/CpuStat.cpp
    src/sysdep/CpuStat.h
    src/sysdep/MemStat.cpp
    src/sysdep/MemStat.h
    src/sysdep/ProcStat.cpp
    src/sysdep/ProcStat.h
    src/sysdep/UptimeStat.cpp
    src/sysdep/UptimeStat.h
    src/sysdep/NetStat.cpp
    src/sysdep/NetStat.h
    src/sysdep/DockerNet.cpp
    src/sysdep/DockerNet.h
    src/sysdep/DiskStat.cpp
    src/sysdep/DiskStat.h
    src/sysdep/SensorStat.cpp
    src/sysdep/SensorStat.h
    src/sysdep/BatteryStat.cpp
    src/sysdep/BatteryStat.h
    src/monitors/MonitorBase.cpp
    src/monitors/MonitorBase.h
    src/monitors/HostMonitor.cpp
    src/monitors/HostMonitor.h
    src/monitors/ClockMonitor.cpp
    src/monitors/ClockMonitor.h
    src/monitors/CpuMonitor.cpp
    src/monitors/CpuMonitor.h
    src/monitors/MemMonitor.cpp
    src/monitors/MemMonitor.h
    src/monitors/ProcMonitor.cpp
    src/monitors/ProcMonitor.h
    src/monitors/UptimeMonitor.cpp
    src/monitors/UptimeMonitor.h
    src/monitors/NetMonitor.cpp
    src/monitors/NetMonitor.h
    src/monitors/DiskMonitor.cpp
    src/monitors/DiskMonitor.h
    src/monitors/SensorsMonitor.cpp
    src/monitors/SensorsMonitor.h
    src/monitors/BatteryMonitor.cpp
    src/monitors/BatteryMonitor.h
)

qt_add_executable(krellix ${KRELLIX_SOURCES})

target_include_directories(krellix PRIVATE src)

target_link_libraries(krellix PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

target_compile_definitions(krellix PRIVATE
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    QT_NO_NARROWING_CONVERSIONS_IN_CONNECT
    QT_DEPRECATED_WARNINGS
    KRELLIX_VERSION="${KRELLIX_VERSION_FULL}"
    KRELLIX_THEMES_SYSTEM_DIR="${CMAKE_INSTALL_PREFIX}/share/krellix/themes"
    KRELLIX_PLUGIN_SYSTEM_DIR="${CMAKE_INSTALL_PREFIX}/lib/krellix/plugins"
)

set_target_properties(krellix PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    ENABLE_EXPORTS ON
)

install(TARGETS krellix RUNTIME DESTINATION bin)
install(DIRECTORY themes/ DESTINATION share/krellix/themes)

# ---- krellixd: remote monitoring daemon ---------------------------------
# QtCore + QtNetwork only (no Widgets, no display). Designed to run as a
# headless systemd service that exposes /proc readings over TCP. Reuses
# the same sysdep readers as the GUI client.
qt_add_executable(krellixd
    src/krellixd/main.cpp
    src/krellixd/KrellixdServer.cpp
    src/krellixd/KrellixdServer.h
    src/krellixd/ClientSession.cpp
    src/krellixd/ClientSession.h
    src/sysdep/CpuStat.cpp
    src/sysdep/CpuStat.h
    src/sysdep/MemStat.cpp
    src/sysdep/MemStat.h
    src/sysdep/ProcStat.cpp
    src/sysdep/ProcStat.h
    src/sysdep/NetStat.cpp
    src/sysdep/NetStat.h
    src/sysdep/DockerNet.cpp
    src/sysdep/DockerNet.h
    src/sysdep/DiskStat.cpp
    src/sysdep/DiskStat.h
    src/sysdep/UptimeStat.cpp
    src/sysdep/UptimeStat.h
    src/sysdep/SensorStat.cpp
    src/sysdep/SensorStat.h
    src/sysdep/BatteryStat.cpp
    src/sysdep/BatteryStat.h
)
target_include_directories(krellixd PRIVATE src)
target_link_libraries(krellixd PRIVATE Qt6::Core Qt6::Network)
target_compile_definitions(krellixd PRIVATE
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    QT_NO_NARROWING_CONVERSIONS_IN_CONNECT
    KRELLIX_VERSION="${KRELLIX_VERSION_FULL}"
)
set_target_properties(krellixd PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)
# Install the daemon under bin/, not sbin/. On modern Arch (and most
# usrmerged distros) /usr/sbin is a symlink to /usr/bin owned by the
# `filesystem` package — installing into sbin/ created a directory
# inside the package that conflicted with that symlink, so `pacman -S
# krellix` aborted with "/usr/sbin exists in filesystem". Living in
# /usr/bin avoids the conflict and is consistent with current FHS.
install(TARGETS krellixd RUNTIME DESTINATION bin)

# ---- Optional installable plugins ---------------------------------------
add_library(krellkam MODULE
    plugins/krellkam/KrellkamPlugin.cpp
    plugins/krellkam/KrellkamPlugin.h
)
target_include_directories(krellkam PRIVATE src)
target_link_libraries(krellkam PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)
target_compile_definitions(krellkam PRIVATE
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    QT_NO_NARROWING_CONVERSIONS_IN_CONNECT
)
set_target_properties(krellkam PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)
install(TARGETS krellkam LIBRARY DESTINATION lib/krellix/plugins)

add_library(krelldacious MODULE
    plugins/krelldacious/KrelldaciousPlugin.cpp
    plugins/krelldacious/KrelldaciousPlugin.h
)
target_include_directories(krelldacious PRIVATE src)
target_link_libraries(krelldacious PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::DBus
)
target_compile_definitions(krelldacious PRIVATE
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    QT_NO_NARROWING_CONVERSIONS_IN_CONNECT
)
set_target_properties(krelldacious PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)
install(TARGETS krelldacious LIBRARY DESTINATION lib/krellix/plugins)

add_library(krellweather MODULE
    plugins/krellweather/KrellweatherPlugin.cpp
    plugins/krellweather/KrellweatherPlugin.h
)
target_include_directories(krellweather PRIVATE src)
target_link_libraries(krellweather PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)
target_compile_definitions(krellweather PRIVATE
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    QT_NO_NARROWING_CONVERSIONS_IN_CONNECT
)
set_target_properties(krellweather PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)
install(TARGETS krellweather LIBRARY DESTINATION lib/krellix/plugins)

add_library(krellwire MODULE
    plugins/krellwire/KrellwirePlugin.cpp
    plugins/krellwire/KrellwirePlugin.h
)
target_include_directories(krellwire PRIVATE src)
target_link_libraries(krellwire PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)
target_compile_definitions(krellwire PRIVATE
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    QT_NO_NARROWING_CONVERSIONS_IN_CONNECT
)
set_target_properties(krellwire PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)
install(TARGETS krellwire LIBRARY DESTINATION lib/krellix/plugins)
