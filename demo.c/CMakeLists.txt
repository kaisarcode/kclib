cmake_minimum_required(VERSION 3.14)
project(demo C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED DEMO_BUILD_VERSION)
    set(DEMO_BUILD_VERSION 0)
endif()

option(DEMO_NATIVE "Enable native hardware optimizations" OFF)
option(DEMO_BUILD_TESTS "Build demo tests" OFF)

set(DEMO_TEST_EXTERNAL_ARTIFACTS OFF)
if(DEMO_BUILD_TESTS AND DEMO_TEST_SHARED_DEMOARY)
    set(DEMO_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        add_link_options(-fuse-ld=lld)
    endif()
    if(DEMO_NATIVE)
        add_compile_options(-march=native)
    endif()
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set_source_files_properties(src/libdemo.c PROPERTIES
    COMPILE_DEFINITIONS KC_DEMO_BUILD_VERSION=${DEMO_BUILD_VERSION}
)

# Core library sources
set(DEMO_CORE_SOURCES
    src/libdemo.c
)

# CLI sources
set(DEMO_CLI_SOURCES
    src/demo.c
    ${DEMO_CORE_SOURCES}
    )

# wasm32/Emscripten library module (reusable library only, no CLI)
if(EMSCRIPTEN)
    add_executable(demo_wasm ${DEMO_CORE_SOURCES})
    target_include_directories(demo_wasm PRIVATE src)
    set_target_properties(demo_wasm PROPERTIES
        OUTPUT_NAME "demo"
        SUFFIX ".wasm"
    )
    target_link_options(demo_wasm PRIVATE
        --no-entry
        -sSTANDALONE_WASM=1
        -sEXPORTED_FUNCTIONS=_kc_demo_greet,_kc_demo_free,_kc_demo_version
    )
endif()

if(NOT DEMO_TEST_EXTERNAL_ARTIFACTS AND NOT EMSCRIPTEN)
    # Main executable
    add_executable(demo
        ${DEMO_CLI_SOURCES}
    )

    target_include_directories(demo PRIVATE src)

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(demo pthread m)
    endif()

    # Static library
    add_library(demo_static STATIC ${DEMO_CORE_SOURCES})

    set_target_properties(demo_static PROPERTIES OUTPUT_NAME "demo")

    target_include_directories(demo_static PRIVATE src)

    # Shared library
    add_library(demo_shared SHARED ${DEMO_CORE_SOURCES})

    set_target_properties(demo_shared PROPERTIES OUTPUT_NAME "demo")
    set_target_properties(demo_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

    target_include_directories(demo_shared PRIVATE src)

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(demo_shared pthread m)
    endif()
endif()

if(DEMO_BUILD_TESTS)
    enable_testing()

    if(EMSCRIPTEN)
        add_executable(demo_contract_test
            src/test.c
            ${DEMO_CORE_SOURCES}
        )
        target_include_directories(demo_contract_test PRIVATE src)
    else()
        if(DEMO_TEST_SHARED_DEMOARY)
            add_library(demo_test_shared SHARED IMPORTED)
            set_target_properties(demo_test_shared PROPERTIES
                IMPORTED_LOCATION "${DEMO_TEST_SHARED_DEMOARY}"
            )
            if(DEMO_TEST_IMPORT_DEMOARY)
                set_target_properties(demo_test_shared PROPERTIES
                    IMPORTED_IMPLIB "${DEMO_TEST_IMPORT_DEMOARY}"
                )
            endif()
        else()
            set(DEMO_TEST_SHARED_TARGET demo_shared)
        endif()

        if(TARGET demo_test_shared)
            set(DEMO_TEST_SHARED_TARGET demo_test_shared)
        endif()

        if(DEMO_TEST_CLI)
            set(DEMO_TEST_CLI_TARGET_PATH "${DEMO_TEST_CLI}")
        else()
            set(DEMO_TEST_CLI_TARGET_PATH "$<TARGET_FILE:demo>")
        endif()

        add_executable(demo_contract_test src/test.c)
        target_include_directories(demo_contract_test PRIVATE src)
        target_link_libraries(demo_contract_test PRIVATE ${DEMO_TEST_SHARED_TARGET})

        if(UNIX AND NOT ANDROID AND NOT APPLE)
            target_link_libraries(demo_contract_test PRIVATE pthread m)
        endif()

        target_compile_definitions(demo_contract_test PRIVATE
            KC_DEMO_TEST_CLI="${DEMO_TEST_CLI_TARGET_PATH}"
        )

        add_custom_command(TARGET demo_contract_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${DEMO_TEST_SHARED_TARGET}>
                $<TARGET_FILE_DIR:demo_contract_test>
        )
    endif()
endif()