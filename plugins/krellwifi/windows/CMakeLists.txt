if(NOT WIN32)
    message(FATAL_ERROR "This implementation is Windows-only")
endif()
add_library(krellwifi MODULE KrellwifiPlugin.cpp KrellwifiPlugin.h WifiData.h)
target_include_directories(krellwifi PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(krellwifi PRIVATE krellix Qt6::Widgets Qt6::Network)
set_target_properties(krellwifi PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")
qt_add_executable(krellwifi-probe probe.cpp NativeWifi.cpp NativeWifi.h WifiData.h)
target_link_libraries(krellwifi-probe PRIVATE Qt6::Core wlanapi iphlpapi ws2_32)
target_compile_definitions(krellwifi-probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties(krellwifi-probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/wifi")
if(BUILD_TESTING)
    qt_add_executable(krellwifi-data data-test.cpp NativeWifi.cpp)
    target_link_libraries(krellwifi-data PRIVATE Qt6::Core wlanapi iphlpapi ws2_32)
    target_compile_definitions(krellwifi-data PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME krellwifi-data COMMAND krellwifi-data)
    qt_add_executable(krellwifi-fixture fixture-probe.cpp)
    target_link_libraries(krellwifi-fixture PRIVATE Qt6::Core)
    qt_add_executable(krellwifi-integration integration-test.cpp
        "${CMAKE_SOURCE_DIR}/src/monitors/MonitorBase.cpp"
        "${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Decal.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Krell.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Chart.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Meter.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/AlertBanner.cpp"
        "${CMAKE_SOURCE_DIR}/src/remote/RemoteSource.cpp")
    target_include_directories(krellwifi-integration PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(krellwifi-integration PRIVATE Qt6::Widgets Qt6::Network)
    target_link_options(krellwifi-integration PRIVATE "-Wl,--export-all-symbols")
    set_target_properties(krellwifi-integration PROPERTIES ENABLE_EXPORTS ON OUTPUT_NAME krellix RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/krellwifi-test-host")
    add_dependencies(krellwifi-integration krellwifi krellwifi-probe krellwifi-fixture)
    add_test(NAME krellwifi-integration COMMAND krellwifi-integration "$<TARGET_FILE:krellwifi>" "$<TARGET_FILE:krellwifi-probe>" "$<TARGET_FILE:krellwifi-fixture>")
    set_tests_properties(krellwifi-integration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 100)
endif()
