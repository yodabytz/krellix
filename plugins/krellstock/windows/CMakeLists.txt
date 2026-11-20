add_library(krellstock MODULE
    ../KrellstockPlugin.cpp ../KrellstockPlugin.h
    KrellstockWindows.cpp WindowsStockFetcher.cpp WindowsStockFetcher.h)
target_include_directories(krellstock PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(krellstock PRIVATE krellix Qt6::Widgets Qt6::Network)
target_compile_definitions(krellstock PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
    QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
set_target_properties(krellstock PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")

if(BUILD_TESTING)
    qt_add_executable(krellstock-fetcher-test fetcher-test.cpp WindowsStockFetcher.cpp WindowsStockFetcher.h)
    target_include_directories(krellstock-fetcher-test PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(krellstock-fetcher-test PRIVATE Qt6::Widgets Qt6::Network)
    add_test(NAME krellstock-fetcher-test COMMAND krellstock-fetcher-test)
    set_tests_properties(krellstock-fetcher-test PROPERTIES TIMEOUT 20)
endif()
