## CMakeLists.txt
## Summary: Build system for bm25 lexical retrieval.
##
## Author:  KaisarCode
## Website: https://kaisarcode.com
## License: https://www.gnu.org/licenses/lgpl-3.0.html

cmake_minimum_required(VERSION 3.14)
project(bm25 C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED BM25_BUILD_VERSION)
    set(BM25_BUILD_VERSION 0)
endif()

option(BM25_NATIVE "Enable native hardware optimizations" OFF)
option(BM25_BUILD_TESTS "Build bm25 tests" OFF)

set(BM25_TEST_EXTERNAL_ARTIFACTS OFF)
if(BM25_BUILD_TESTS AND BM25_TEST_SHARED_LIBRARY)
    set(BM25_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        add_link_options(-fuse-ld=lld)
    endif()
    if(BM25_NATIVE)
        add_compile_options(-march=native)
    endif()
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set_source_files_properties(src/libbm25.c PROPERTIES
    COMPILE_DEFINITIONS KC_BM25_BUILD_VERSION=${BM25_BUILD_VERSION}
)

set(BM25_CORE_SOURCES
    src/libbm25.c
)

if(EMSCRIPTEN)
    add_executable(libbm25_wasm ${BM25_CORE_SOURCES})
    target_include_directories(libbm25_wasm PRIVATE src)
    set_target_properties(libbm25_wasm PROPERTIES
        OUTPUT_NAME "libbm25"
        SUFFIX ".wasm"
    )
    target_link_options(libbm25_wasm PRIVATE
        --no-entry
        -sSTANDALONE_WASM=1
        -sEXPORTED_FUNCTIONS=_kc_bm25_open,_kc_bm25_add,_kc_bm25_build,_kc_bm25_search,_kc_bm25_save,_kc_bm25_load,_kc_bm25_count,_kc_bm25_fold_accents,_kc_bm25_free,_kc_bm25_close,_kc_bm25_strerror,_kc_bm25_version
    )

    add_library(bm25_wasm_static STATIC ${BM25_CORE_SOURCES})
    set_target_properties(bm25_wasm_static PROPERTIES
        PREFIX "lib"
        OUTPUT_NAME "bm25"
        POSITION_INDEPENDENT_CODE ON
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
    )
    target_include_directories(bm25_wasm_static PRIVATE src)

    add_library(bm25_wasm_shared SHARED ${BM25_CORE_SOURCES})
    set_target_properties(bm25_wasm_shared PROPERTIES
        PREFIX "lib"
        OUTPUT_NAME "bm25"
        SUFFIX ".so"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
    )
    target_include_directories(bm25_wasm_shared PRIVATE src)
    target_link_options(bm25_wasm_shared PRIVATE -sSIDE_MODULE=1)
elseif(NOT BM25_TEST_EXTERNAL_ARTIFACTS)
    add_library(bm25_static STATIC ${BM25_CORE_SOURCES})
    set_target_properties(bm25_static PROPERTIES OUTPUT_NAME "bm25")
    target_include_directories(bm25_static PRIVATE src)

    add_library(bm25_shared SHARED ${BM25_CORE_SOURCES})
    set_target_properties(bm25_shared PROPERTIES
        OUTPUT_NAME "bm25"
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
    target_include_directories(bm25_shared PRIVATE src)

    if(NOT MSVC)
        target_link_libraries(bm25_shared PRIVATE m)
    endif()
endif()

if(BM25_BUILD_TESTS)
    enable_testing()

    if(EMSCRIPTEN)
        add_executable(bm25_contract_test
            src/test.c
            ${BM25_CORE_SOURCES}
        )
        target_include_directories(bm25_contract_test PRIVATE src)
    else()
        if(BM25_TEST_SHARED_LIBRARY)
            add_library(bm25_test_shared SHARED IMPORTED)
            set_target_properties(bm25_test_shared PROPERTIES
                IMPORTED_LOCATION "${BM25_TEST_SHARED_LIBRARY}"
            )
            if(BM25_TEST_IMPORT_LIBRARY)
                set_target_properties(bm25_test_shared PROPERTIES
                    IMPORTED_IMPLIB "${BM25_TEST_IMPORT_LIBRARY}"
                )
            endif()
            set(BM25_TEST_SHARED_TARGET bm25_test_shared)
        else()
            set(BM25_TEST_SHARED_TARGET bm25_shared)
        endif()

        add_executable(bm25_contract_test src/test.c)
        target_include_directories(bm25_contract_test PRIVATE src)
        target_link_libraries(bm25_contract_test PRIVATE ${BM25_TEST_SHARED_TARGET})
        target_compile_definitions(bm25_contract_test PRIVATE
            KC_CLI_PATH=\"${BM25_CLI_PATH}\"
        )

        if(NOT MSVC)
            target_link_libraries(bm25_contract_test PRIVATE m)
        endif()

        add_custom_command(TARGET bm25_contract_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${BM25_TEST_SHARED_TARGET}>
                $<TARGET_FILE_DIR:bm25_contract_test>
        )
    endif()
endif()

if(TARGET bm25_shared AND NOT EMSCRIPTEN AND
    NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(bm25_cli src/bm25.c)
    set_target_properties(bm25_cli PROPERTIES
        OUTPUT_NAME "bm25"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}"
    )
    target_include_directories(bm25_cli PRIVATE src)
    target_link_libraries(bm25_cli PRIVATE bm25_shared)
    if(APPLE)
        set_target_properties(bm25_cli PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "@loader_path"
        )
    elseif(UNIX)
        set_target_properties(bm25_cli PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "$ORIGIN"
        )
    endif()
endif()
