add_library(krelldacious MODULE ../KrelldaciousPlugin.cpp ../KrelldaciousPlugin.h
    AudaciousWindows.cpp AudaciousWindows.h)
target_include_directories(krelldacious PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(krelldacious PRIVATE krellix Qt6::Widgets)
target_compile_definitions(krelldacious PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties(krelldacious PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")

if(BUILD_TESTING)
    qt_add_executable(krelldacious-integration integration-test.cpp
        "${CMAKE_SOURCE_DIR}/src/monitors/MonitorBase.cpp"
        "${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Decal.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Krell.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Chart.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Meter.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/AlertBanner.cpp"
        "${CMAKE_SOURCE_DIR}/src/remote/RemoteSource.cpp")
    target_include_directories(krelldacious-integration PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(krelldacious-integration PRIVATE Qt6::Widgets Qt6::Network)
    target_link_options(krelldacious-integration PRIVATE "-Wl,--export-all-symbols")
    set_target_properties(krelldacious-integration PROPERTIES ENABLE_EXPORTS ON OUTPUT_NAME krellix
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/krelldacious-test-host")
    add_dependencies(krelldacious-integration krelldacious)
endif()
