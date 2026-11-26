add_library(krellhello MODULE KrellHello.cpp)
target_include_directories(krellhello PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(krellhello PRIVATE Qt6::Widgets Qt6::Network)
target_compile_definitions(krellhello PRIVATE QT_NO_CAST_FROM_ASCII)
set_target_properties(krellhello PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")

if(WIN32)
    target_link_libraries(krellhello PRIVATE krellix)
    set_target_properties(krellhello PROPERTIES PREFIX "")
elseif(APPLE)
    target_link_options(krellhello PRIVATE "LINKER:-undefined,dynamic_lookup")
endif()
