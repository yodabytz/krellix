add_library(krelltop MODULE KrelltopPlugin.cpp)
target_include_directories(krelltop PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(krelltop PRIVATE Qt6::Widgets)
target_link_options(krelltop PRIVATE -Wl,-undefined,dynamic_lookup)
set_target_properties(krelltop PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")
install(TARGETS krelltop LIBRARY DESTINATION lib/krellix/plugins)

if(BUILD_TESTING)
    set(src "${CMAKE_SOURCE_DIR}/src")
    qt_add_executable(krelltop-integration integration-test.cpp
        "${src}/monitors/MonitorBase.cpp" "${src}/remote/RemoteSource.cpp"
        "${src}/theme/Theme.cpp" "${src}/widgets/Panel.cpp" "${src}/widgets/Decal.cpp"
        "${src}/widgets/Krell.cpp" "${src}/widgets/Chart.cpp" "${src}/widgets/Meter.cpp"
        "${src}/widgets/AlertBanner.cpp")
    target_include_directories(krelltop-integration PRIVATE "${src}")
    target_link_libraries(krelltop-integration PRIVATE Qt6::Widgets Qt6::Network)
    set_target_properties(krelltop-integration PROPERTIES ENABLE_EXPORTS ON)
    add_dependencies(krelltop-integration krelltop)
    add_test(NAME krelltop-integration COMMAND krelltop-integration "$<TARGET_FILE:krelltop>")
    set_tests_properties(krelltop-integration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)
endif()
