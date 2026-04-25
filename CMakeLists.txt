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

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets)

qt_standard_project_setup()

set(KRELLIX_SOURCES
    src/krellix/main.cpp
    src/krellix/MainWindow.cpp
    src/krellix/MainWindow.h
    src/theme/Theme.cpp
    src/theme/Theme.h
    src/widgets/Panel.cpp
    src/widgets/Panel.h
    src/widgets/Decal.cpp
    src/widgets/Decal.h
    src/widgets/Krell.cpp
    src/widgets/Krell.h
    src/widgets/Chart.cpp
    src/widgets/Chart.h
    src/sysdep/CpuStat.cpp
    src/sysdep/CpuStat.h
    src/sysdep/MemStat.cpp
    src/sysdep/MemStat.h
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
)

qt_add_executable(krellix ${KRELLIX_SOURCES})

target_include_directories(krellix PRIVATE src)

target_link_libraries(krellix PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

target_compile_definitions(krellix PRIVATE
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    QT_NO_NARROWING_CONVERSIONS_IN_CONNECT
    QT_DEPRECATED_WARNINGS
    KRELLIX_VERSION="${KRELLIX_VERSION_FULL}"
    KRELLIX_THEMES_SYSTEM_DIR="${CMAKE_INSTALL_PREFIX}/share/krellix/themes"
)

set_target_properties(krellix PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

install(TARGETS krellix RUNTIME DESTINATION bin)
install(DIRECTORY themes/ DESTINATION share/krellix/themes)
