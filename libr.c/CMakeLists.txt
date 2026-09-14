cmake_minimum_required(VERSION 3.14)
project(libr C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED LIBR_BUILD_VERSION)
    set(LIBR_BUILD_VERSION 0)
endif()

option(LIBR_NATIVE "Enable native hardware optimizations" OFF)
option(LIBR_BUILD_TESTS "Build libr tests" OFF)

set(LIBR_TEST_EXTERNAL_ARTIFACTS OFF)
if(LIBR_BUILD_TESTS AND LIBR_TEST_SHARED_LIBRARY)
    set(LIBR_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        add_link_options(-fuse-ld=lld)
    endif()
    if(LIBR_NATIVE)
        add_compile_options(-march=native)
    endif()
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set_source_files_properties(src/liblibr.c PROPERTIES
    COMPILE_DEFINITIONS KC_LIBR_BUILD_VERSION=${LIBR_BUILD_VERSION}
)

# Core library sources (no kcrun, no runner)
set(LIBR_CORE_SOURCES
    src/liblibr.c
)

# CLI sources
set(LIBR_CLI_SOURCES
    src/libr.c
    ${LIBR_CORE_SOURCES}
    )

# wasm32/Emscripten library module (reusable library only, no CLI)
if(EMSCRIPTEN)
    add_executable(libr_wasm ${LIBR_CORE_SOURCES})
    target_include_directories(libr_wasm PRIVATE src)
    set_target_properties(libr_wasm PROPERTIES
        OUTPUT_NAME "libr"
        SUFFIX ".wasm"
    )
    target_link_options(libr_wasm PRIVATE
        --no-entry
        -sSTANDALONE_WASM=1
        -sEXPORTED_FUNCTIONS=_kc_libr_options_default,_kc_libr_options_set,_kc_libr_options_free,_kc_libr_version,_kc_libr_open,_kc_libr_close,_kc_libr_exec,_kc_libr_stop,_kc_libr_get_error
    )
endif()

if(NOT LIBR_TEST_EXTERNAL_ARTIFACTS AND NOT EMSCRIPTEN)
    # Main executable
    add_executable(libr
        ${LIBR_CLI_SOURCES}
    )

    target_include_directories(libr PRIVATE src)

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(libr pthread m)
    endif()

    # Static library
    add_library(libr_static STATIC ${LIBR_CORE_SOURCES})

    set_target_properties(libr_static PROPERTIES OUTPUT_NAME "libr")

    target_include_directories(libr_static PRIVATE src)

    # Shared library
    add_library(libr_shared SHARED ${LIBR_CORE_SOURCES})

    set_target_properties(libr_shared PROPERTIES OUTPUT_NAME "libr")
    set_target_properties(libr_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

    target_include_directories(libr_shared PRIVATE src)

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(libr_shared pthread m)
    endif()
endif()

if(LIBR_BUILD_TESTS)
    enable_testing()

    # kcrun for native test runner (referenced by path like llm.c does)
    set(KCRUN_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../kcrun.c")

    if(EMSCRIPTEN)
        # WebAssembly contract test: compile the public-contract tests against
        # the reusable library sources for the Emscripten toolchain.
        add_executable(libr_contract_test
            src/test.c
            ${LIBR_CORE_SOURCES}
        )
        target_include_directories(libr_contract_test PRIVATE src)
    else()
        if(LIBR_TEST_SHARED_LIBRARY)
            add_library(libr_test_shared SHARED IMPORTED)
            set_target_properties(libr_test_shared PROPERTIES
                IMPORTED_LOCATION "${LIBR_TEST_SHARED_LIBRARY}"
            )
            if(LIBR_TEST_IMPORT_LIBRARY)
                set_target_properties(libr_test_shared PROPERTIES
                    IMPORTED_IMPLIB "${LIBR_TEST_IMPORT_LIBRARY}"
                )
            endif()
        else()
            set(LIBR_TEST_SHARED_TARGET libr_shared)
        endif()

        if(TARGET libr_test_shared)
            set(LIBR_TEST_SHARED_TARGET libr_test_shared)
        endif()

        if(LIBR_TEST_CLI)
            set(LIBR_TEST_CLI_TARGET_PATH "${LIBR_TEST_CLI}")
        else()
            set(LIBR_TEST_CLI_TARGET_PATH "$<TARGET_FILE:libr>")
        endif()

        set(TEST_SOURCES
            src/test.c
        )
        if(EXISTS "${KCRUN_SOURCE_DIR}/src/libkcrun.c")
            list(APPEND TEST_SOURCES
                ${KCRUN_SOURCE_DIR}/src/libkcrun.c
                ${KCRUN_SOURCE_DIR}/lib/parson/parson.c
            )
            set_source_files_properties(${KCRUN_SOURCE_DIR}/lib/parson/parson.c PROPERTIES COMPILE_OPTIONS "-w")
        endif()

        add_executable(libr_contract_test ${TEST_SOURCES})
        target_include_directories(libr_contract_test PRIVATE src)
        if(EXISTS "${KCRUN_SOURCE_DIR}/src/libkcrun.c")
            target_include_directories(libr_contract_test PRIVATE ${KCRUN_SOURCE_DIR}/src ${KCRUN_SOURCE_DIR}/lib/parson)
        endif()
        target_link_libraries(libr_contract_test PRIVATE ${LIBR_TEST_SHARED_TARGET})

        if(UNIX AND NOT ANDROID AND NOT APPLE)
            target_link_libraries(libr_contract_test PRIVATE pthread m)
        endif()

        target_compile_definitions(libr_contract_test PRIVATE
            KC_LIBR_TEST_CLI="${LIBR_TEST_CLI_TARGET_PATH}"
        )

        add_custom_command(TARGET libr_contract_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${LIBR_TEST_SHARED_TARGET}>
                $<TARGET_FILE_DIR:libr_contract_test>
        )
    endif()
endif()