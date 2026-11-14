cmake_minimum_required(VERSION 3.21)
if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()
if(NOT DEFINED BUNDLE OR NOT DEFINED QT_BIN)
    message(FATAL_ERROR "Set BUNDLE and QT_BIN")
endif()
file(GLOB_RECURSE plugin_dlls "${BUNDLE}/*.dll")
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${BUNDLE}/krellix.exe"
    LIBRARIES ${plugin_dlls}
    DIRECTORIES "${QT_BIN}"
    RESOLVED_DEPENDENCIES_VAR resolved
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    CONFLICTING_DEPENDENCIES_PREFIX conflicts
    PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
    POST_EXCLUDE_REGEXES ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss]/[Ss][Yy][Ss][Tt][Ee][Mm]32/.*")
if(unresolved)
    message(FATAL_ERROR "Unresolved runtime dependencies: ${unresolved}")
endif()
foreach(name IN LISTS conflicts_FILENAMES)
    # windeployqt patches QtCore's installation prefix in the bundled copy.
    if(NOT EXISTS "${BUNDLE}/${name}")
        message(FATAL_ERROR "Conflicting dependency not already bundled: ${name}")
    endif()
endforeach()
foreach(dll IN LISTS resolved)
    file(TO_CMAKE_PATH "${dll}" normalized)
    string(TOLOWER "${normalized}" normalized)
    if(normalized MATCHES "/windows/system32/")
        continue()
    endif()
    get_filename_component(name "${dll}" NAME)
    if(NOT EXISTS "${BUNDLE}/${name}")
        file(COPY "${dll}" DESTINATION "${BUNDLE}")
    endif()
endforeach()
