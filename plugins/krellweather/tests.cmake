set(weather_plugin "${CMAKE_CURRENT_LIST_DIR}")
set(weather_studio "${CMAKE_SOURCE_DIR}")
set(weather_drawing "${weather_plugin}/WeatherConditions.cpp" "${weather_plugin}/WeatherIcons.cpp")
add_executable(krellweather-icons "${weather_studio}/tests/krellweather-icons.cpp" ${weather_drawing})
target_include_directories(krellweather-icons PRIVATE "${weather_plugin}")
target_link_libraries(krellweather-icons PRIVATE Qt6::Gui)
add_test(NAME krellweather-icons COMMAND krellweather-icons)

add_executable(krellweather-layout "${weather_studio}/tests/krellweather-layout.cpp"
    "${weather_plugin}/KrellweatherPlugin.cpp" "${weather_plugin}/KrellweatherPlugin.h" ${weather_drawing}
    "${weather_studio}/src/monitors/MonitorBase.cpp" "${weather_studio}/src/theme/Theme.cpp"
    "${weather_studio}/src/widgets/Panel.cpp" "${weather_studio}/src/widgets/Decal.cpp"
    "${weather_studio}/src/widgets/Krell.cpp" "${weather_studio}/src/widgets/Chart.cpp"
    "${weather_studio}/src/widgets/Meter.cpp")
if(WIN32)
    target_sources(krellweather-layout PRIVATE "${weather_plugin}/windows/Metar.cpp"
        "${weather_studio}/src/remote/RemoteSource.cpp" "${weather_studio}/src/widgets/AlertBanner.cpp")
endif()
target_include_directories(krellweather-layout PRIVATE "${weather_studio}" "${weather_studio}/src")
target_link_libraries(krellweather-layout PRIVATE Qt6::Widgets Qt6::Network)
add_test(NAME krellweather-layout COMMAND krellweather-layout "${weather_studio}")
set_tests_properties(krellweather-icons krellweather-layout
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=1" TIMEOUT 25)
