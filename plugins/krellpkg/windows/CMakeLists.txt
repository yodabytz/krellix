if(NOT WIN32)
    message(FATAL_ERROR "Windows-only package implementation")
endif()
add_library(krellpkg MODULE KrellpkgPlugin.cpp KrellpkgPlugin.h PackageData.h)
target_include_directories(krellpkg PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(krellpkg PRIVATE krellix Qt6::Widgets Qt6::Network)
set_target_properties(krellpkg PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")
set(PKG_PROBE "${CMAKE_BINARY_DIR}/packages/krellpkg-probe.exe")
add_custom_command(OUTPUT "${PKG_PROBE}" "${CMAKE_BINARY_DIR}/packages/Microsoft.Management.Deployment.winmd"
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/build-probe.ps1" -Output "${PKG_PROBE}"
    DEPENDS Probe.cs build-probe.ps1 VERBATIM)
add_custom_target(krellpkg-probe DEPENDS "${PKG_PROBE}")
add_dependencies(krellpkg krellpkg-probe)
if(BUILD_TESTING)
    qt_add_executable(krellpkg-data data-test.cpp)
    target_link_libraries(krellpkg-data PRIVATE Qt6::Core)
    add_test(NAME krellpkg-data COMMAND krellpkg-data)
    qt_add_executable(krellpkg-fixture fixture-probe.cpp)
    target_link_libraries(krellpkg-fixture PRIVATE Qt6::Core)
    qt_add_executable(krellpkg-integration integration-test.cpp
        "${CMAKE_SOURCE_DIR}/src/monitors/MonitorBase.cpp"
        "${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Decal.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Krell.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Chart.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Meter.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/AlertBanner.cpp"
        "${CMAKE_SOURCE_DIR}/src/remote/RemoteSource.cpp")
    target_include_directories(krellpkg-integration PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(krellpkg-integration PRIVATE Qt6::Widgets Qt6::Network)
    target_link_options(krellpkg-integration PRIVATE "-Wl,--export-all-symbols")
    set_target_properties(krellpkg-integration PROPERTIES ENABLE_EXPORTS ON OUTPUT_NAME krellix
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/krellpkg-test-host")
    add_dependencies(krellpkg-integration krellpkg krellpkg-fixture)
    add_test(NAME krellpkg-integration COMMAND krellpkg-integration "$<TARGET_FILE:krellpkg>" "$<TARGET_FILE:krellpkg-fixture>")
    set_tests_properties(krellpkg-integration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
endif()
