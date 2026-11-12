target_compile_definitions(krellixd PRIVATE KRELLIX_DAEMON)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_options(krellixd PRIVATE -fstack-protector-strong)
    target_link_options(krellixd PRIVATE -Wl,-z,relro,-z,now,-z,noexecstack)
    install(PROGRAMS packaging/docker-snapshot.py DESTINATION lib/krellix)
    install(FILES packaging/systemd/krellixd-docker-snapshot.service
                  packaging/systemd/krellixd-docker-snapshot.timer
            DESTINATION lib/systemd/system)
    install(FILES packaging/systemd/krellixd.service.d/zz-security.conf
            DESTINATION lib/systemd/system/krellixd.service.d)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME daemon-security COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/daemon-security.py $<TARGET_FILE:krellixd>)
    set_tests_properties(daemon-security PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
endif()
