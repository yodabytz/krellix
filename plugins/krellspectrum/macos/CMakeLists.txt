add_executable(krellspectrum-coreaudio CoreAudioPlayback.cpp)
find_library(SPECTRUM_AUDIO_TOOLBOX AudioToolbox REQUIRED)
find_library(SPECTRUM_CORE_AUDIO CoreAudio REQUIRED)
find_library(SPECTRUM_CORE_FOUNDATION CoreFoundation REQUIRED)
target_link_libraries(krellspectrum-coreaudio PRIVATE
    "${SPECTRUM_AUDIO_TOOLBOX}" "${SPECTRUM_CORE_AUDIO}" "${SPECTRUM_CORE_FOUNDATION}")
target_link_options(krellspectrum-coreaudio PRIVATE
    "LINKER:-sectcreate,__TEXT,__info_plist,${CMAKE_CURRENT_SOURCE_DIR}/Info.plist")
set_property(TARGET krellspectrum-coreaudio APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist")
install(TARGETS krellspectrum-coreaudio RUNTIME DESTINATION lib/krellix/plugins)
