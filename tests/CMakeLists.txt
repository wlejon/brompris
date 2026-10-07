function(brompris_test name)
    cmake_parse_arguments(T "" "TIMEOUT" "LIBS" ${ARGN})
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE brompris::brompris ${T_LIBS})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
    brompris_warnings(${name})
    if(NOT T_TIMEOUT)
        set(T_TIMEOUT 60)
    endif()
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT ${T_TIMEOUT} SKIP_RETURN_CODE 77)
endfunction()

# Unit tests
brompris_test(test_metadata)
brompris_test(test_player_model)

# D-Bus MPRIS2 integration tests (Linux)
if(BROMPRIS_LINUX)
    brompris_test(test_mpris_client LIBS brodbus::brodbus)
    brompris_test(test_media_manager LIBS brodbus::brodbus)
endif()

if(BROMPRIS_ENABLE_API)
    add_executable(test_api
        test_api.cpp
    )
    target_include_directories(test_api PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/api
        ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(test_api PRIVATE
        brompris_api
        brompris
        bronze_runtime_shared
    )
    if(BROMPRIS_LINUX)
        target_link_libraries(test_api PRIVATE brodbus::brodbus)
    endif()
    brompris_warnings(test_api)
    if(WIN32)
        target_compile_definitions(test_api PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00)
        add_custom_command(TARGET test_api POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:bronze_runtime_shared>
                $<TARGET_FILE_DIR:test_api>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_LINKER_FILE:bronze_runtime_shared>
                $<TARGET_FILE_DIR:test_api>
        )
    endif()

    if(NOT BRASS_HOST_BACKEND)
        message(STATUS "brompris: test_api not registered (brass has no code generator for this target)")
    else()
        add_test(NAME test_api COMMAND test_api)
        set_tests_properties(test_api PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

        add_test(NAME test_api_gc_stress COMMAND test_api)
        set_tests_properties(test_api_gc_stress PROPERTIES
            TIMEOUT 600
            SKIP_RETURN_CODE 77
            ENVIRONMENT "BRONZE_GC_STRESS=1;BRONZE_GC_POISON=1"
        )
        if(WIN32)
            set_tests_properties(test_api PROPERTIES
                ENVIRONMENT "PATH=$<TARGET_FILE_DIR:bronze_runtime_shared>;$<TARGET_FILE_DIR:test_api>;$ENV{PATH}")
            set_property(TEST test_api_gc_stress APPEND PROPERTY
                ENVIRONMENT "PATH=$<TARGET_FILE_DIR:bronze_runtime_shared>;$<TARGET_FILE_DIR:test_api>;$ENV{PATH}")
        endif()
    endif()
endif()
