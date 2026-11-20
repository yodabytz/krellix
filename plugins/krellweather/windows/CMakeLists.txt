add_library(krellweather MODULE ../KrellweatherPlugin.cpp ../KrellweatherPlugin.h Metar.cpp Metar.h)
target_include_directories(krellweather PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(krellweather PRIVATE krellix Qt6::Widgets Qt6::Network)
target_compile_definitions(krellweather PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
    QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
set_target_properties(krellweather PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")

if(BUILD_TESTING)
    qt_add_executable(krellweather-metar-test metar-test.cpp Metar.cpp Metar.h)
    target_link_libraries(krellweather-metar-test PRIVATE Qt6::Core)
    add_test(NAME krellweather-metar-test COMMAND krellweather-metar-test)
    set_tests_properties(krellweather-metar-test PROPERTIES TIMEOUT 15)
endif()
