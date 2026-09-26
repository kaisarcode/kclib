cmake_minimum_required(VERSION 3.14)
project(ngram C)

set(CMAKE_C_STANDARD 11)
if(NOT DEFINED KC_NGRAM_BUILD_VERSION)
    set(KC_NGRAM_BUILD_VERSION 0)
endif()

option(NGRAM_NATIVE "Enable native CPU optimizations" OFF)
option(NGRAM_BUILD_TESTS "Build ngram tests" OFF)

set(NGRAM_TEST_EXTERNAL_ARTIFACTS OFF)
if(NGRAM_BUILD_TESTS AND NGRAM_TEST_SHARED_LIBRARY)
    set(NGRAM_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror -fno-finite-math-only)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    add_definitions(-D_GNU_SOURCE)
    if(NGRAM_NATIVE)
        add_compile_options(-march=native)
    endif()
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set(NGRAM_SOURCES src/libngram.c)

set_source_files_properties(src/libngram.c PROPERTIES
    COMPILE_DEFINITIONS KC_NGRAM_BUILD_VERSION=${KC_NGRAM_BUILD_VERSION}
)

if(EMSCRIPTEN)
    add_executable(ngram_wasm ${NGRAM_SOURCES})
    target_include_directories(ngram_wasm PRIVATE src)
    set_target_properties(ngram_wasm PROPERTIES
        OUTPUT_NAME "ngram"
        SUFFIX ".wasm"
    )
    target_link_options(ngram_wasm PRIVATE
        --no-entry
        -sSTANDALONE_WASM=1
        -sEXPORTED_FUNCTIONS=_kc_ngram_traverse,_kc_ngram_version
    )
endif()

if(NOT NGRAM_TEST_EXTERNAL_ARTIFACTS AND NOT EMSCRIPTEN)
    add_library(ngram_shared SHARED ${NGRAM_SOURCES})
    set_target_properties(ngram_shared PROPERTIES OUTPUT_NAME ngram PREFIX "lib")
    target_include_directories(ngram_shared PRIVATE src)

    add_library(ngram_static STATIC ${NGRAM_SOURCES})
    set_target_properties(ngram_static PROPERTIES OUTPUT_NAME ngram PREFIX "lib" POSITION_INDEPENDENT_CODE ON)
    target_include_directories(ngram_static PRIVATE src)

    add_executable(ngram_exe src/ngram.c)
    set_target_properties(ngram_exe PROPERTIES OUTPUT_NAME ngram)
    target_include_directories(ngram_exe PRIVATE src)
    target_link_libraries(ngram_exe PRIVATE ngram_static)
endif()

if(NGRAM_BUILD_TESTS)
    enable_testing()

    if(EMSCRIPTEN)
        add_executable(ngram_contract_test src/test.c ${NGRAM_SOURCES})
        target_include_directories(ngram_contract_test PRIVATE src)
    else()
        if(NGRAM_TEST_SHARED_LIBRARY)
            add_library(ngram_test_shared SHARED IMPORTED)
            set_target_properties(ngram_test_shared PROPERTIES
                IMPORTED_LOCATION "${NGRAM_TEST_SHARED_LIBRARY}"
            )
            if(NGRAM_TEST_IMPORT_LIBRARY)
                set_target_properties(ngram_test_shared PROPERTIES
                    IMPORTED_IMPLIB "${NGRAM_TEST_IMPORT_LIBRARY}"
                )
            endif()
        else()
            set(NGRAM_TEST_SHARED_TARGET ngram_shared)
        endif()

        if(TARGET ngram_test_shared)
            set(NGRAM_TEST_SHARED_TARGET ngram_test_shared)
        endif()

        add_executable(ngram_contract_test src/test.c)
        target_include_directories(ngram_contract_test PRIVATE src)
        target_link_libraries(ngram_contract_test PRIVATE ${NGRAM_TEST_SHARED_TARGET})

        add_custom_command(TARGET ngram_contract_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${NGRAM_TEST_SHARED_TARGET}>
                $<TARGET_FILE_DIR:ngram_contract_test>
        )
    endif()

    if(NGRAM_TEST_CLI)
        set(NGRAM_TEST_CLI_TARGET_PATH "${NGRAM_TEST_CLI}")
    else()
        set(NGRAM_TEST_CLI_TARGET_PATH "")
    endif()
    target_compile_definitions(ngram_contract_test PRIVATE
        KC_NGRAM_TEST_CLI="${NGRAM_TEST_CLI_TARGET_PATH}"
    )
endif()
