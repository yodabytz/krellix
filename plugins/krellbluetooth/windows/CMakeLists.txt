if(NOT WIN32)
    message(FATAL_ERROR "Windows-only Bluetooth implementation")
endif()
add_library(krellbluetooth MODULE KrellbluetoothPlugin.cpp KrellbluetoothPlugin.h BluetoothData.h)
target_include_directories(krellbluetooth PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(krellbluetooth PRIVATE krellix Qt6::Widgets Qt6::Network)
set_target_properties(krellbluetooth PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")
set(BT_PROBE "${CMAKE_BINARY_DIR}/bluetooth/krellbt-probe.exe")
add_custom_command(OUTPUT "${BT_PROBE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bluetooth"
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/build-probe.ps1" -Output "${BT_PROBE}"
    DEPENDS Probe.cs build-probe.ps1 VERBATIM)
add_custom_target(krellbt-probe DEPENDS "${BT_PROBE}")
add_dependencies(krellbluetooth krellbt-probe)
if(BUILD_TESTING)
    set(BT_PROBE_TEST "${CMAKE_BINARY_DIR}/bluetooth/krellbt-probe-test.exe")
    add_custom_command(OUTPUT "${BT_PROBE_TEST}"
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/build-probe.ps1" -Output "${BT_PROBE_TEST}" -Test
        DEPENDS Probe.cs ProbeTest.cs build-probe.ps1 VERBATIM)
    add_custom_target(krellbt-native-data DEPENDS "${BT_PROBE_TEST}")
    add_dependencies(krellbt-native-data krellbt-probe)
    add_test(NAME krellbt-native-data COMMAND "${BT_PROBE_TEST}")
    qt_add_executable(krellbt-data data-test.cpp)
    target_link_libraries(krellbt-data PRIVATE Qt6::Core)
    add_test(NAME krellbt-data COMMAND krellbt-data)
    qt_add_executable(krellbt-fixture fixture-probe.cpp)
    target_link_libraries(krellbt-fixture PRIVATE Qt6::Core)
    qt_add_executable(krellbt-integration integration-test.cpp
        "${CMAKE_SOURCE_DIR}/src/monitors/MonitorBase.cpp"
        "${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Decal.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Krell.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Chart.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Meter.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/AlertBanner.cpp"
        "${CMAKE_SOURCE_DIR}/src/remote/RemoteSource.cpp")
    target_include_directories(krellbt-integration PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(krellbt-integration PRIVATE Qt6::Widgets Qt6::Network)
    target_link_options(krellbt-integration PRIVATE "-Wl,--export-all-symbols")
    set_target_properties(krellbt-integration PROPERTIES ENABLE_EXPORTS ON OUTPUT_NAME krellix
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/krellbt-test-host")
    add_dependencies(krellbt-integration krellbluetooth krellbt-fixture krellbt-native-data)
    add_test(NAME krellbt-integration COMMAND krellbt-integration "$<TARGET_FILE:krellbluetooth>" "${BT_PROBE}" "$<TARGET_FILE:krellbt-fixture>")
    set_tests_properties(krellbt-integration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
endif()
