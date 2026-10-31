cmake_minimum_required(VERSION 3.14)
project(leven C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED LEVEN_BUILD_VERSION)
    set(LEVEN_BUILD_VERSION 0)
endif()

option(LEVEN_NATIVE "Enable native hardware optimizations" OFF)
option(LEVEN_BUILD_TESTS "Build leven tests" OFF)

set(LEVEN_TEST_EXTERNAL_ARTIFACTS OFF)
if(LEVEN_BUILD_TESTS AND LEVEN_TEST_SHARED_LIBRARY)
    set(LEVEN_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        add_link_options(-fuse-ld=lld)
    endif()
    if(LEVEN_NATIVE)
        add_compile_options(-march=native)
    endif()
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set_source_files_properties(src/libleven.c PROPERTIES
    COMPILE_DEFINITIONS KC_LEVEN_BUILD_VERSION=${LEVEN_BUILD_VERSION}
)

set(LEVEN_CORE_SOURCES
    src/libleven.c
)

if(EMSCRIPTEN)
    add_executable(libleven_wasm ${LEVEN_CORE_SOURCES})
    target_include_directories(libleven_wasm PRIVATE src)
    set_target_properties(libleven_wasm PROPERTIES
        OUTPUT_NAME "libleven"
        SUFFIX ".wasm"
    )
    target_link_options(libleven_wasm PRIVATE
        --no-entry
        -sSTANDALONE_WASM=1
        -sEXPORTED_FUNCTIONS=_kc_leven_distance,_kc_leven_strerror,_kc_leven_version
    )

    add_library(leven_wasm_static STATIC ${LEVEN_CORE_SOURCES})
    set_target_properties(leven_wasm_static PROPERTIES
        PREFIX "lib"
        OUTPUT_NAME "leven"
        POSITION_INDEPENDENT_CODE ON
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
    )
    target_include_directories(leven_wasm_static PRIVATE src)

    add_library(leven_wasm_shared SHARED ${LEVEN_CORE_SOURCES})
    set_target_properties(leven_wasm_shared PROPERTIES
        PREFIX "lib"
        OUTPUT_NAME "leven"
        SUFFIX ".so"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
    )
    target_include_directories(leven_wasm_shared PRIVATE src)
    target_link_options(leven_wasm_shared PRIVATE -sSIDE_MODULE=1)
elseif(NOT LEVEN_TEST_EXTERNAL_ARTIFACTS)
    add_library(leven_static STATIC ${LEVEN_CORE_SOURCES})
    set_target_properties(leven_static PROPERTIES OUTPUT_NAME "leven")
    target_include_directories(leven_static PRIVATE src)

    add_library(leven_shared SHARED ${LEVEN_CORE_SOURCES})
    set_target_properties(leven_shared PROPERTIES
        OUTPUT_NAME "leven"
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
    target_include_directories(leven_shared PRIVATE src)

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(leven_shared pthread m)
    endif()
endif()

if(LEVEN_BUILD_TESTS)
    enable_testing()

    if(EMSCRIPTEN)
        add_executable(leven_contract_test
            src/test.c
            ${LEVEN_CORE_SOURCES}
        )
        target_include_directories(leven_contract_test PRIVATE src)
    else()
        if(LEVEN_TEST_SHARED_LIBRARY)
            add_library(leven_test_shared SHARED IMPORTED)
            set_target_properties(leven_test_shared PROPERTIES
                IMPORTED_LOCATION "${LEVEN_TEST_SHARED_LIBRARY}"
            )
            if(LEVEN_TEST_IMPORT_LIBRARY)
                set_target_properties(leven_test_shared PROPERTIES
                    IMPORTED_IMPLIB "${LEVEN_TEST_IMPORT_LIBRARY}"
                )
            endif()
            set(LEVEN_TEST_SHARED_TARGET leven_test_shared)
        else()
            set(LEVEN_TEST_SHARED_TARGET leven_shared)
        endif()

        add_executable(leven_contract_test src/test.c)
        target_include_directories(leven_contract_test PRIVATE src)
        target_link_libraries(leven_contract_test PRIVATE ${LEVEN_TEST_SHARED_TARGET})
        target_compile_definitions(leven_contract_test PRIVATE
            KC_CLI_PATH=\"${LEVEN_CLI_PATH}\"
        )

        if(UNIX AND NOT ANDROID AND NOT APPLE)
            target_link_libraries(leven_contract_test PRIVATE pthread m)
        endif()

        add_custom_command(TARGET leven_contract_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${LEVEN_TEST_SHARED_TARGET}>
                $<TARGET_FILE_DIR:leven_contract_test>
        )
    endif()
endif()

if(TARGET leven_shared AND NOT EMSCRIPTEN AND
    NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(leven_cli src/leven.c)
    set_target_properties(leven_cli PROPERTIES
        OUTPUT_NAME "leven"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}"
    )
    target_include_directories(leven_cli PRIVATE src)
    target_link_libraries(leven_cli PRIVATE leven_shared)
    if(APPLE)
        set_target_properties(leven_cli PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "@loader_path"
        )
    elseif(UNIX)
        set_target_properties(leven_cli PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "$ORIGIN"
        )
    endif()
endif()
