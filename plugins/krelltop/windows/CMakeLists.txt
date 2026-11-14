if(NOT WIN32)
    message(FATAL_ERROR "This implementation is Windows-only; use the existing Unix plugin on Linux/macOS")
endif()
add_library(krelltop MODULE KrelltopPlugin.cpp KrelltopPlugin.h WindowsProcesses.cpp WindowsProcesses.h)
target_include_directories(krelltop PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(krelltop PRIVATE krellix Qt6::Widgets Qt6::Network psapi)
target_compile_definitions(krelltop PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
set_target_properties(krelltop PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")
if(BUILD_TESTING)
    qt_add_executable(krelltop-native native-test.cpp WindowsProcesses.cpp)
    target_link_libraries(krelltop-native PRIVATE Qt6::Core psapi)
    target_compile_definitions(krelltop-native PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME krelltop-native COMMAND krelltop-native)
    set_tests_properties(krelltop-native PROPERTIES TIMEOUT 20)

    qt_add_executable(krelltop-integration integration-test.cpp
        "${CMAKE_SOURCE_DIR}/src/monitors/MonitorBase.cpp"
        "${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Decal.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Krell.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Chart.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Meter.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/AlertBanner.cpp"
        "${CMAKE_SOURCE_DIR}/src/remote/RemoteSource.cpp")
    target_include_directories(krelltop-integration PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(krelltop-integration PRIVATE Qt6::Widgets Qt6::Network)
    target_link_options(krelltop-integration PRIVATE "-Wl,--export-all-symbols")
    set_target_properties(krelltop-integration PROPERTIES ENABLE_EXPORTS ON OUTPUT_NAME krellix
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/krelltop-test-host")
    add_dependencies(krelltop-integration krelltop)
    add_test(NAME krelltop-integration COMMAND krelltop-integration "$<TARGET_FILE:krelltop>")
    set_tests_properties(krelltop-integration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45)
endif()
