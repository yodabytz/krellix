add_library(krellwire MODULE ../KrellwirePlugin.cpp ../KrellwirePlugin.h)
target_include_directories(krellwire PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(krellwire PRIVATE krellix Qt6::Widgets Qt6::Network)
target_compile_definitions(krellwire PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
    QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
set_target_properties(krellwire PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")

if(BUILD_TESTING)
    qt_add_executable(krellwire-integration integration-test.cpp
        ../KrellwirePlugin.cpp ../KrellwirePlugin.h
        "${CMAKE_SOURCE_DIR}/src/monitors/MonitorBase.cpp"
        "${CMAKE_SOURCE_DIR}/src/theme/Theme.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Decal.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Krell.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Meter.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/Chart.cpp"
        "${CMAKE_SOURCE_DIR}/src/widgets/AlertBanner.cpp"
        "${CMAKE_SOURCE_DIR}/src/remote/RemoteSource.cpp")
    target_include_directories(krellwire-integration PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(krellwire-integration PRIVATE Qt6::Widgets Qt6::Network)
    add_test(NAME krellwire-integration COMMAND krellwire-integration)
    set_tests_properties(krellwire-integration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
endif()
