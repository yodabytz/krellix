if(NOT WIN32)
    message(FATAL_ERROR "This implementation is Windows-only")
endif()
add_library(krellhealth MODULE KrellhealthPlugin.cpp KrellhealthPlugin.h HealthData.h)
target_include_directories(krellhealth PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(krellhealth PRIVATE krellix Qt6::Widgets Qt6::Network)
set_target_properties(krellhealth PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")
qt_add_executable(krellhealth-probe probe.cpp HealthData.h)
target_link_libraries(krellhealth-probe PRIVATE Qt6::Core advapi32 ole32 oleaut32 uuid)
target_compile_definitions(krellhealth-probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties(krellhealth-probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/health")
if(BUILD_TESTING)
    qt_add_executable(krellhealth-data data-test.cpp)
    target_link_libraries(krellhealth-data PRIVATE Qt6::Core)
    add_test(NAME krellhealth-data COMMAND krellhealth-data)
    qt_add_executable(krellhealth-integration integration-test.cpp
        "${CMAKE_SOURCE_DIR}/src/monitors/MonitorBase.cpp"
        "${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Decal.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Krell.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Chart.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Meter.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/AlertBanner.cpp"
        "${CMAKE_SOURCE_DIR}/src/remote/RemoteSource.cpp")
    target_include_directories(krellhealth-integration PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(krellhealth-integration PRIVATE Qt6::Widgets Qt6::Network)
    target_link_options(krellhealth-integration PRIVATE "-Wl,--export-all-symbols")
    set_target_properties(krellhealth-integration PROPERTIES ENABLE_EXPORTS ON OUTPUT_NAME krellix
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/krellhealth-test-host")
    add_dependencies(krellhealth-integration krellhealth krellhealth-probe)
    add_custom_command(TARGET krellhealth-integration POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:krellhealth-integration>/health"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:krellhealth-probe>" "$<TARGET_FILE_DIR:krellhealth-integration>/health/")
    add_test(NAME krellhealth-integration COMMAND krellhealth-integration "$<TARGET_FILE:krellhealth>")
    set_tests_properties(krellhealth-integration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 75)
endif()
