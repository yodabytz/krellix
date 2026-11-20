cmake_minimum_required(VERSION 3.21)
project(KrellSpectrumRenderFixture LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Network)
get_filename_component(root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
add_executable(krellspectrum-render render-fixture.cpp
    ../KrellSpectrumPlugin.cpp ../KrellSpectrumPlugin.h
    "${root}/src/monitors/MonitorBase.cpp"
    "${root}/src/theme/Theme.cpp"
    "${root}/src/widgets/Panel.cpp"
    "${root}/src/widgets/Decal.cpp"
    "${root}/src/widgets/Krell.cpp"
    "${root}/src/widgets/Meter.cpp"
    "${root}/src/widgets/Chart.cpp")
target_include_directories(krellspectrum-render PRIVATE "${root}/src")
target_compile_definitions(krellspectrum-render PRIVATE KRELLIX_THEMES_SYSTEM_DIR="${root}/themes")
target_link_libraries(krellspectrum-render PRIVATE Qt6::Widgets Qt6::Network)
if(WIN32)
    target_sources(krellspectrum-render PRIVATE
        "${root}/src/remote/RemoteSource.cpp"
        "${root}/src/widgets/AlertBanner.cpp")
    target_compile_definitions(krellspectrum-render PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
