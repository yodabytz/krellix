add_library(krellmail MODULE ../KrellmailPlugin.cpp ../KrellmailPlugin.h)
target_include_directories(krellmail PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(krellmail PRIVATE krellix Qt6::Widgets Qt6::Network)
set_target_properties(krellmail PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")

if(BUILD_TESTING)
    qt_add_executable(krellmail-icon "${CMAKE_SOURCE_DIR}/tests/krellmail-icon.cpp"
        "${CMAKE_SOURCE_DIR}/src/monitors/MonitorBase.cpp"
        "${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Decal.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Krell.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Chart.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Meter.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/AlertBanner.cpp"
        "${CMAKE_SOURCE_DIR}/src/remote/RemoteSource.cpp")
    target_include_directories(krellmail-icon PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(krellmail-icon PRIVATE Qt6::Widgets Qt6::Network)
    target_link_options(krellmail-icon PRIVATE "-Wl,--export-all-symbols")
    set_target_properties(krellmail-icon PROPERTIES ENABLE_EXPORTS ON OUTPUT_NAME krellix
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/krellmail-icon-test-host")
    add_dependencies(krellmail-icon krellmail)
    add_test(NAME krellmail-icon COMMAND krellmail-icon "$<TARGET_FILE:krellmail>")
    set_tests_properties(krellmail-icon PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 15)

    qt_add_executable(krellmail-integration integration-test.cpp
        "${CMAKE_SOURCE_DIR}/src/monitors/MonitorBase.cpp"
        "${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Decal.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Krell.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Chart.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Meter.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/AlertBanner.cpp"
        "${CMAKE_SOURCE_DIR}/src/remote/RemoteSource.cpp")
    target_include_directories(krellmail-integration PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(krellmail-integration PRIVATE Qt6::Widgets Qt6::Network)
    target_link_options(krellmail-integration PRIVATE "-Wl,--export-all-symbols")
    set_target_properties(krellmail-integration PROPERTIES ENABLE_EXPORTS ON OUTPUT_NAME krellix
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/krellmail-test-host")
    add_dependencies(krellmail-integration krellmail)
    add_test(NAME krellmail-integration COMMAND krellmail-integration "$<TARGET_FILE:krellmail>")
    set_tests_properties(krellmail-integration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
endif()
