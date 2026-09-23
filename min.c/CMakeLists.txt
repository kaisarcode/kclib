cmake_minimum_required(VERSION 3.14)
project(min C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED MIN_BUILD_VERSION)
    set(MIN_BUILD_VERSION 0)
endif()

option(MIN_NATIVE "Enable native hardware optimizations" OFF)
option(MIN_BUILD_TESTS "Build min tests" OFF)

set(MIN_TEST_EXTERNAL_ARTIFACTS OFF)
if(MIN_BUILD_TESTS AND MIN_TEST_SHARED_LIBRARY)
    set(MIN_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    if(MIN_NATIVE)
        add_compile_options(-march=native)
    endif()
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set(MIN_SOURCES src/libmin.c)

set_source_files_properties(src/libmin.c PROPERTIES
    COMPILE_DEFINITIONS KC_MIN_BUILD_VERSION=${MIN_BUILD_VERSION}
)

if(EMSCRIPTEN)
    add_executable(min_wasm ${MIN_SOURCES})
    target_include_directories(min_wasm PRIVATE src)
    set_target_properties(min_wasm PROPERTIES
        OUTPUT_NAME "min"
        SUFFIX ".wasm"
    )
    target_link_options(min_wasm PRIVATE
        --no-entry
        -sSTANDALONE_WASM=1
        -sEXPORTED_FUNCTIONS=_kc_min_minify,_kc_min_free,_kc_min_version
    )
endif()

if(NOT MIN_TEST_EXTERNAL_ARTIFACTS AND NOT EMSCRIPTEN)
    add_executable(min
        src/min.c
        ${MIN_SOURCES}
    )

    target_include_directories(min PRIVATE src)

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(min pthread m)
    endif()

    add_library(min_static STATIC ${MIN_SOURCES})
    set_target_properties(min_static PROPERTIES OUTPUT_NAME "min")
    target_include_directories(min_static PRIVATE src)

    add_library(min_shared SHARED ${MIN_SOURCES})
    set_target_properties(min_shared PROPERTIES OUTPUT_NAME "min")
    set_target_properties(min_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_include_directories(min_shared PRIVATE src)

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(min_shared pthread m)
    endif()
endif()

if(MIN_BUILD_TESTS)
    enable_testing()

    if(EMSCRIPTEN)
        add_executable(min_contract_test src/test.c ${MIN_SOURCES})
        target_include_directories(min_contract_test PRIVATE src)
    elseif(MIN_TEST_SHARED_LIBRARY)
        add_library(min_test_shared SHARED IMPORTED)
        set_target_properties(min_test_shared PROPERTIES
            IMPORTED_LOCATION "${MIN_TEST_SHARED_LIBRARY}"
        )
        if(MIN_TEST_IMPORT_LIBRARY)
            set_target_properties(min_test_shared PROPERTIES
                IMPORTED_IMPLIB "${MIN_TEST_IMPORT_LIBRARY}"
            )
        endif()
    else()
        set(MIN_TEST_SHARED_TARGET min_shared)
    endif()

    if(NOT EMSCRIPTEN)
        if(TARGET min_test_shared)
            set(MIN_TEST_SHARED_TARGET min_test_shared)
        endif()

        add_executable(min_contract_test src/test.c)
        target_include_directories(min_contract_test PRIVATE src)
        target_link_libraries(min_contract_test PRIVATE ${MIN_TEST_SHARED_TARGET})

        if(UNIX AND NOT ANDROID AND NOT APPLE)
            target_link_libraries(min_contract_test PRIVATE m)
        endif()

        add_custom_command(TARGET min_contract_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${MIN_TEST_SHARED_TARGET}>
                $<TARGET_FILE_DIR:min_contract_test>
        )
    endif()
endif()

if(MIN_BUILD_TESTS)
    if(MIN_TEST_CLI)
        set(MIN_TEST_CLI_TARGET_PATH "${MIN_TEST_CLI}")
    else()
        set(MIN_TEST_CLI_TARGET_PATH "")
    endif()
    target_compile_definitions(min_contract_test PRIVATE
        MIN_TEST_CLI="${MIN_TEST_CLI_TARGET_PATH}"
    )
endif()
