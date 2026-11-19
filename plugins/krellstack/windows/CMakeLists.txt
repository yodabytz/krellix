if(NOT WIN32)
    message(FATAL_ERROR "Windows-only Stack implementation")
endif()
add_library(krellstack MODULE KrellstackPlugin.cpp KrellstackPlugin.h StackData.h)
target_include_directories(krellstack PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(krellstack PRIVATE krellix Qt6::Widgets Qt6::Network)
set_target_properties(krellstack PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")
qt_add_executable(krellstack-probe Probe.cpp)
target_link_libraries(krellstack-probe PRIVATE Qt6::Core)
target_compile_definitions(krellstack-probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
add_dependencies(krellstack krellstack-probe)
if(BUILD_TESTING)
    qt_add_executable(krellstack-data data-test.cpp)
    target_link_libraries(krellstack-data PRIVATE Qt6::Core)
    add_test(NAME krellstack-data COMMAND krellstack-data)
    qt_add_executable(krellstack-fixture fixture-probe.cpp)
    target_link_libraries(krellstack-fixture PRIVATE Qt6::Core)
    qt_add_executable(krellstack-native native-test.cpp)
    target_link_libraries(krellstack-native PRIVATE Qt6::Core)
    target_compile_definitions(krellstack-native PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_dependencies(krellstack-native krellstack-probe krellstack-fixture)
    add_test(NAME krellstack-native COMMAND krellstack-native "$<TARGET_FILE:krellstack-probe>" "$<TARGET_FILE:krellstack-fixture>")
    set_tests_properties(krellstack-native PROPERTIES TIMEOUT 60)
    qt_add_executable(krellstack-integration integration-test.cpp
        "${CMAKE_SOURCE_DIR}/src/monitors/MonitorBase.cpp"
        "${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Decal.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Krell.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Chart.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Meter.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/AlertBanner.cpp"
        "${CMAKE_SOURCE_DIR}/src/remote/RemoteSource.cpp")
    target_include_directories(krellstack-integration PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(krellstack-integration PRIVATE Qt6::Widgets Qt6::Network)
    target_link_options(krellstack-integration PRIVATE "-Wl,--export-all-symbols")
    set_target_properties(krellstack-integration PROPERTIES ENABLE_EXPORTS ON OUTPUT_NAME krellix
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/krellstack-test-host")
    add_dependencies(krellstack-integration krellstack krellstack-fixture krellstack-native)
    add_test(NAME krellstack-integration COMMAND krellstack-integration "$<TARGET_FILE:krellstack>" "$<TARGET_FILE:krellstack-fixture>")
    set_tests_properties(krellstack-integration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
endif()
