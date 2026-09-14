## CMakeLists.txt
## Summary: Build system for hnsw — HNSW vector search library.
##
## Author:  KaisarCode
## Website: https://kaisarcode.com
## License: https://www.gnu.org/licenses/gpl-3.0.html

cmake_minimum_required(VERSION 3.14)
project(hnsw.c C)

set(CMAKE_C_STANDARD 11)

option(HNSW_BUILD_TESTS "Build hnsw tests" OFF)

if(NOT DEFINED HNSW_BUILD_VERSION)
    set(HNSW_BUILD_VERSION 0)
endif()

## Compiler flags

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror -fno-finite-math-only)
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

## Include paths

include_directories(src)

## Library targets

set(LIB_SOURCES src/libhnsw.c)

set_source_files_properties(src/libhnsw.c PROPERTIES
    COMPILE_DEFINITIONS KC_HNSW_BUILD_VERSION=${HNSW_BUILD_VERSION}
)

if(EMSCRIPTEN)
    add_executable(hnsw_wasm ${LIB_SOURCES})
    target_include_directories(hnsw_wasm PRIVATE src)
    set_target_properties(hnsw_wasm PROPERTIES
        OUTPUT_NAME "hnsw"
        SUFFIX ".wasm"
    )
    target_link_options(hnsw_wasm PRIVATE
        --no-entry
        -sSTANDALONE_WASM=1
        -sEXPORTED_FUNCTIONS=_kc_hnsw_options_default,_kc_hnsw_options_load_env,_kc_hnsw_options_free,_kc_hnsw_open,_kc_hnsw_stop,_kc_hnsw_stop_requested,_kc_hnsw_close,_kc_hnsw_reserve,_kc_hnsw_add,_kc_hnsw_search,_kc_hnsw_build,_kc_hnsw_dimension,_kc_hnsw_metric,_kc_hnsw_count,_kc_hnsw_metric_from_string,_kc_hnsw_metric_to_string,_kc_hnsw_strerror,_kc_hnsw_version
    )
endif()

if(NOT EMSCRIPTEN)
add_library(hnsw_static STATIC ${LIB_SOURCES})
set_target_properties(hnsw_static PROPERTIES OUTPUT_NAME "hnsw" POSITION_INDEPENDENT_CODE ON)
target_include_directories(hnsw_static PRIVATE src)

if(UNIX AND NOT ANDROID)
    target_link_libraries(hnsw_static pthread m)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "armv7|arm|mips")
        target_link_libraries(hnsw_static atomic)
    endif()
elseif(ANDROID)
    target_link_libraries(hnsw_static m)
endif()

add_library(hnsw_shared SHARED ${LIB_SOURCES})
set_target_properties(hnsw_shared PROPERTIES OUTPUT_NAME "hnsw")
target_include_directories(hnsw_shared PRIVATE src)

if(UNIX AND NOT ANDROID)
    target_link_libraries(hnsw_shared pthread m)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "armv7|arm|mips")
        target_link_libraries(hnsw_shared atomic)
    endif()
elseif(ANDROID)
    target_link_libraries(hnsw_shared m)
endif()

## Executable

add_executable(hnsw src/hnsw.c)
target_include_directories(hnsw PRIVATE src)
target_link_libraries(hnsw hnsw_static)

if(UNIX AND NOT ANDROID)
    target_link_libraries(hnsw pthread m)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "armv7|arm|mips")
        target_link_libraries(hnsw atomic)
    endif()
elseif(ANDROID)
    target_link_libraries(hnsw m)
endif()

endif()

if(HNSW_BUILD_TESTS)
    enable_testing()

    if(EMSCRIPTEN)
        add_executable(hnsw_contract_test src/test.c ${LIB_SOURCES})
        target_include_directories(hnsw_contract_test PRIVATE src)
    else()
        if(HNSW_TEST_SHARED_LIBRARY)
            add_library(hnsw_test_shared SHARED IMPORTED)
            set_target_properties(hnsw_test_shared PROPERTIES
                IMPORTED_LOCATION "${HNSW_TEST_SHARED_LIBRARY}"
            )
            if(HNSW_TEST_IMPORT_LIBRARY)
                set_target_properties(hnsw_test_shared PROPERTIES
                    IMPORTED_IMPLIB "${HNSW_TEST_IMPORT_LIBRARY}"
                )
            endif()
            set(HNSW_TEST_SHARED_TARGET hnsw_test_shared)
        else()
            set(HNSW_TEST_SHARED_TARGET hnsw_shared)
        endif()

        if(HNSW_TEST_CLI)
            set(HNSW_TEST_CLI_TARGET_PATH "${HNSW_TEST_CLI}")
        else()
            set(HNSW_TEST_CLI_TARGET_PATH "$<TARGET_FILE:hnsw>")
        endif()

        add_executable(hnsw_contract_test src/test.c)
        target_include_directories(hnsw_contract_test PRIVATE src)
        target_link_libraries(hnsw_contract_test ${HNSW_TEST_SHARED_TARGET})
        target_compile_definitions(hnsw_contract_test PRIVATE
            KC_HNSW_TEST_CLI="${HNSW_TEST_CLI_TARGET_PATH}"
        )
        if(UNIX AND NOT ANDROID AND NOT APPLE)
            target_link_libraries(hnsw_contract_test pthread m)
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "armv7|arm|mips")
                target_link_libraries(hnsw_contract_test atomic)
            endif()
        endif()

        add_custom_command(TARGET hnsw_contract_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${HNSW_TEST_SHARED_TARGET}>
                $<TARGET_FILE_DIR:hnsw_contract_test>
        )
    endif()

endif()
