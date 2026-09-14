cmake_minimum_required(VERSION 3.14)
project(http C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED HTTP_BUILD_VERSION)
    set(HTTP_BUILD_VERSION 0)
endif()

option(HTTP_NATIVE "Enable native hardware optimizations" OFF)
option(HTTP_BUILD_TESTS "Build http tests" OFF)

set(HTTP_TEST_EXTERNAL_ARTIFACTS OFF)
if(HTTP_BUILD_TESTS AND HTTP_TEST_SHARED_LIBRARY)
    set(HTTP_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    if(HTTP_NATIVE)
        add_compile_options(-march=native)
    endif()
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set_source_files_properties(src/libhttp.c PROPERTIES
    COMPILE_DEFINITIONS KC_HTTP_BUILD_VERSION=${HTTP_BUILD_VERSION}
)

if(EMSCRIPTEN)
    add_executable(http_wasm src/libhttp.c)
    target_include_directories(http_wasm PRIVATE src)
    set_target_properties(http_wasm PROPERTIES
        OUTPUT_NAME "http"
        SUFFIX ".wasm"
    )
    target_link_options(http_wasm PRIVATE
        --no-entry
        -sSTANDALONE_WASM=1
        -sSTACK_SIZE=1048576
        -sEXPORTED_FUNCTIONS=_kc_http_open,_kc_http_exec,_kc_http_set_input,_kc_http_get_output,_kc_http_close,_kc_http_stop,_kc_http_set_op,_kc_http_set_all,_kc_http_set_method,_kc_http_set_target,_kc_http_set_version,_kc_http_set_status,_kc_http_set_reason,_kc_http_set_chunked,_kc_http_set_chunk_size,_kc_http_add_header,_kc_http_add_trailer,_kc_http_options_default,_kc_http_options_load_env,_kc_http_options_free,_kc_http_version
    )
endif()

if(NOT HTTP_TEST_EXTERNAL_ARTIFACTS AND NOT EMSCRIPTEN)
    add_executable(http
        src/http.c
        src/libhttp.c
    )

    target_include_directories(http PRIVATE src)

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(http m)
    endif()

    add_library(http_static STATIC src/libhttp.c)
    set_target_properties(http_static PROPERTIES OUTPUT_NAME "http")
    target_include_directories(http_static PRIVATE src)

    add_library(http_shared SHARED src/libhttp.c)
    set_target_properties(http_shared PROPERTIES OUTPUT_NAME "http")
    set_target_properties(http_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_include_directories(http_shared PRIVATE src)

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(http_shared m)
    endif()
endif()

if(HTTP_BUILD_TESTS)
    enable_testing()

    if(EMSCRIPTEN)
        add_executable(http_contract_test src/test.c src/libhttp.c)
        target_include_directories(http_contract_test PRIVATE src)
        target_link_options(http_contract_test PRIVATE -sSTACK_SIZE=1048576)
    else()
        if(HTTP_TEST_SHARED_LIBRARY)
            add_library(http_test_shared SHARED IMPORTED)
            set_target_properties(http_test_shared PROPERTIES
                IMPORTED_LOCATION "${HTTP_TEST_SHARED_LIBRARY}"
            )
            if(HTTP_TEST_IMPORT_LIBRARY)
                set_target_properties(http_test_shared PROPERTIES
                    IMPORTED_IMPLIB "${HTTP_TEST_IMPORT_LIBRARY}"
                )
            endif()
        else()
            set(HTTP_TEST_SHARED_TARGET http_shared)
        endif()

        if(TARGET http_test_shared)
            set(HTTP_TEST_SHARED_TARGET http_test_shared)
        endif()

        add_executable(http_contract_test src/test.c)
        target_include_directories(http_contract_test PRIVATE src)
        target_link_libraries(http_contract_test PRIVATE ${HTTP_TEST_SHARED_TARGET})

        if(UNIX AND NOT ANDROID AND NOT APPLE)
            target_link_libraries(http_contract_test PRIVATE m)
        endif()

        add_custom_command(TARGET http_contract_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${HTTP_TEST_SHARED_TARGET}>
                $<TARGET_FILE_DIR:http_contract_test>
        )
    endif()
endif()
