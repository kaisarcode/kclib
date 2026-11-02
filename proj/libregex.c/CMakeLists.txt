cmake_minimum_required(VERSION 3.14)
project(regex C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED REGEX_BUILD_VERSION)
    set(REGEX_BUILD_VERSION 0)
endif()

option(REGEX_NATIVE "Enable native hardware optimizations" OFF)
option(REGEX_BUILD_TESTS "Build regex tests" OFF)

set(REGEX_TEST_EXTERNAL_ARTIFACTS OFF)
if(REGEX_BUILD_TESTS AND REGEX_TEST_SHARED_LIBRARY)
    set(REGEX_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        add_link_options(-fuse-ld=lld)
    endif()
    if(REGEX_NATIVE)
        add_compile_options(-march=native)
    endif()
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set_source_files_properties(src/libregex.c PROPERTIES
    COMPILE_DEFINITIONS KC_REGEX_BUILD_VERSION=${REGEX_BUILD_VERSION}
)

set(REGEX_CORE_SOURCES
    src/libregex.c
)

if(EMSCRIPTEN)
    add_executable(libregex_wasm ${REGEX_CORE_SOURCES})
    target_include_directories(libregex_wasm PRIVATE src)
    set_target_properties(libregex_wasm PROPERTIES
        OUTPUT_NAME "libregex"
        SUFFIX ".wasm"
    )
    target_link_options(libregex_wasm PRIVATE
        --no-entry
        -sSTANDALONE_WASM=1
        -sEXPORTED_FUNCTIONS=_kc_regex_open,_kc_regex_match,_kc_regex_search,_kc_regex_close,_kc_regex_strerror,_kc_regex_version
    )

    add_library(regex_wasm_static STATIC ${REGEX_CORE_SOURCES})
    set_target_properties(regex_wasm_static PROPERTIES
        PREFIX "lib"
        OUTPUT_NAME "regex"
        POSITION_INDEPENDENT_CODE ON
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
    )
    target_include_directories(regex_wasm_static PRIVATE src)

    add_library(regex_wasm_shared SHARED ${REGEX_CORE_SOURCES})
    set_target_properties(regex_wasm_shared PROPERTIES
        PREFIX "lib"
        OUTPUT_NAME "regex"
        SUFFIX ".so"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
    )
    target_include_directories(regex_wasm_shared PRIVATE src)
    target_link_options(regex_wasm_shared PRIVATE -sSIDE_MODULE=1)
elseif(NOT REGEX_TEST_EXTERNAL_ARTIFACTS)
    add_library(regex_static STATIC ${REGEX_CORE_SOURCES})
    set_target_properties(regex_static PROPERTIES OUTPUT_NAME "regex" POSITION_INDEPENDENT_CODE ON)
    target_include_directories(regex_static PRIVATE src)

    add_library(regex_shared SHARED ${REGEX_CORE_SOURCES})
    set_target_properties(regex_shared PROPERTIES
        OUTPUT_NAME "regex"
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
    target_include_directories(regex_shared PRIVATE src)

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(regex_shared pthread m)
    endif()
endif()

if(REGEX_BUILD_TESTS)
    enable_testing()

    if(EMSCRIPTEN)
        add_executable(regex_contract_test
            src/test.c
            ${REGEX_CORE_SOURCES}
        )
        target_include_directories(regex_contract_test PRIVATE src)
    else()
        if(REGEX_TEST_SHARED_LIBRARY)
            add_library(regex_test_shared SHARED IMPORTED)
            set_target_properties(regex_test_shared PROPERTIES
                IMPORTED_LOCATION "${REGEX_TEST_SHARED_LIBRARY}"
            )
            if(REGEX_TEST_IMPORT_LIBRARY)
                set_target_properties(regex_test_shared PROPERTIES
                    IMPORTED_IMPLIB "${REGEX_TEST_IMPORT_LIBRARY}"
                )
            endif()
            set(REGEX_TEST_SHARED_TARGET regex_test_shared)
        else()
            set(REGEX_TEST_SHARED_TARGET regex_shared)
        endif()

        add_executable(regex_contract_test src/test.c)
        target_include_directories(regex_contract_test PRIVATE src)
        target_link_libraries(regex_contract_test PRIVATE ${REGEX_TEST_SHARED_TARGET})
        target_compile_definitions(regex_contract_test PRIVATE
            KC_CLI_PATH=\"${REGEX_CLI_PATH}\"
        )

        if(UNIX AND NOT ANDROID AND NOT APPLE)
            target_link_libraries(regex_contract_test PRIVATE pthread m)
        endif()

        add_custom_command(TARGET regex_contract_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${REGEX_TEST_SHARED_TARGET}>
                $<TARGET_FILE_DIR:regex_contract_test>
        )
    endif()
endif()

if(TARGET regex_shared AND NOT EMSCRIPTEN AND
    NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(regex_cli src/regex.c)
    set_target_properties(regex_cli PROPERTIES
        OUTPUT_NAME "regex"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}"
    )
    target_include_directories(regex_cli PRIVATE src)
    target_link_libraries(regex_cli PRIVATE regex_shared)
    if(APPLE)
        set_target_properties(regex_cli PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "@loader_path"
        )
    elseif(UNIX)
        set_target_properties(regex_cli PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "$ORIGIN"
        )
    endif()
endif()
