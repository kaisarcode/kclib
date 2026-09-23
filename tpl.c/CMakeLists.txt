cmake_minimum_required(VERSION 3.14)
project(tpl C)

set(CMAKE_C_STANDARD 11)

option(TPL_NATIVE "Enable native hardware optimizations" OFF)
option(TPL_BUILD_TESTS "Build tpl tests" OFF)

if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc")
endif()

if(NOT DEFINED TPL_BUILD_VERSION)
    set(TPL_BUILD_VERSION 0)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    add_definitions(-D_GNU_SOURCE)
    if(TPL_NATIVE)
        add_compile_options(-march=native)
    endif()
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set(TPL_SOURCES src/libtpl.c)
set(TPL_HEADERS src/libtpl.h)

set_source_files_properties(src/libtpl.c PROPERTIES
    COMPILE_DEFINITIONS KC_TPL_BUILD_VERSION=${TPL_BUILD_VERSION}
)

if(EMSCRIPTEN)
    add_executable(tpl_wasm ${TPL_SOURCES})
    target_include_directories(tpl_wasm PRIVATE src)
    set_target_properties(tpl_wasm PROPERTIES
        OUTPUT_NAME "tpl"
        SUFFIX ".wasm"
    )
    target_link_options(tpl_wasm PRIVATE
        --no-entry
        -sSTANDALONE_WASM=1
        -sSTACK_SIZE=4194304
        -sEXPORTED_FUNCTIONS=_kc_tpl_open,_kc_tpl_render,_kc_tpl_error,_kc_tpl_free,_kc_tpl_close,_kc_tpl_version
    )
endif()

if(NOT EMSCRIPTEN)
add_library(tpl_shared SHARED ${TPL_SOURCES})
set_target_properties(tpl_shared PROPERTIES OUTPUT_NAME tpl PREFIX "lib")
target_include_directories(tpl_shared PRIVATE src)

add_library(tpl_static STATIC ${TPL_SOURCES})
set_target_properties(tpl_static PROPERTIES OUTPUT_NAME tpl PREFIX "lib" POSITION_INDEPENDENT_CODE ON)
target_include_directories(tpl_static PRIVATE src)

add_executable(tpl_exe src/tpl.c)
set_target_properties(tpl_exe PROPERTIES OUTPUT_NAME tpl)
target_include_directories(tpl_exe PRIVATE src)
target_link_libraries(tpl_exe tpl_static)

if(UNIX AND NOT ANDROID AND NOT APPLE)
    target_link_libraries(tpl_exe pthread m)
endif()
endif()

if(TPL_BUILD_TESTS)
    enable_testing()

    if(EMSCRIPTEN)
        add_executable(tpl_contract_test src/test.c ${TPL_SOURCES})
        target_include_directories(tpl_contract_test PRIVATE src)
        target_link_options(tpl_contract_test PRIVATE -sSTACK_SIZE=4194304)
    else()
        if(TPL_TEST_SHARED_LIBRARY)
            add_library(tpl_test_shared SHARED IMPORTED)
            set_target_properties(tpl_test_shared PROPERTIES
                IMPORTED_LOCATION "${TPL_TEST_SHARED_LIBRARY}"
            )
            if(TPL_TEST_IMPORT_LIBRARY)
                set_target_properties(tpl_test_shared PROPERTIES
                    IMPORTED_IMPLIB "${TPL_TEST_IMPORT_LIBRARY}"
                )
            endif()
            set(TPL_TEST_SHARED_TARGET tpl_test_shared)
        else()
            set(TPL_TEST_SHARED_TARGET tpl_shared)
        endif()

        if(TPL_TEST_CLI)
            set(TPL_TEST_CLI_TARGET_PATH "${TPL_TEST_CLI}")
        else()
            set(TPL_TEST_CLI_TARGET_PATH "$<TARGET_FILE:tpl_exe>")
        endif()

        add_executable(tpl_contract_test src/test.c)
        target_include_directories(tpl_contract_test PRIVATE src)
        target_link_libraries(tpl_contract_test ${TPL_TEST_SHARED_TARGET})
        target_compile_definitions(tpl_contract_test PRIVATE
            KC_TPL_TEST_CLI="${TPL_TEST_CLI_TARGET_PATH}"
        )
        if(UNIX AND NOT ANDROID AND NOT APPLE)
            target_link_libraries(tpl_contract_test pthread m)
        endif()

        add_custom_command(TARGET tpl_contract_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${TPL_TEST_SHARED_TARGET}>
                $<TARGET_FILE_DIR:tpl_contract_test>
        )
    endif()
endif()
