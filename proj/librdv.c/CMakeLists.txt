cmake_minimum_required(VERSION 3.14)
project(rdv C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED RDV_BUILD_VERSION)
    set(RDV_BUILD_VERSION 0)
endif()

option(RDV_NATIVE "Enable native hardware optimizations" OFF)
option(RDV_BUILD_TESTS "Build rdv tests" OFF)

set(RDV_TEST_EXTERNAL_ARTIFACTS OFF)
if(RDV_BUILD_TESTS AND RDV_TEST_SHARED_LIBRARY)
    set(RDV_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        add_link_options(-fuse-ld=lld)
    endif()
    if(RDV_NATIVE)
        add_compile_options(-march=native)
    endif()
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set_source_files_properties(src/librdv.c PROPERTIES
    COMPILE_DEFINITIONS KC_RDV_BUILD_VERSION=${RDV_BUILD_VERSION}
)

set(RDV_CORE_SOURCES
    src/librdv.c
)

if(EMSCRIPTEN)
    add_executable(librdv_wasm ${RDV_CORE_SOURCES})
    target_include_directories(librdv_wasm PRIVATE src)
    set_target_properties(librdv_wasm PROPERTIES
        OUTPUT_NAME "librdv"
        SUFFIX ".wasm"
    )
    target_link_options(librdv_wasm PRIVATE
        --no-entry
        -sSTANDALONE_WASM=1
        -sEXPORTED_FUNCTIONS=_kc_rdv_open,_kc_rdv_get,_kc_rdv_publish,_kc_rdv_unpublish,_kc_rdv_seek,_kc_rdv_remove,_kc_rdv_prune,_kc_rdv_close,_kc_rdv_strerror,_kc_rdv_version
    )
elseif(NOT RDV_TEST_EXTERNAL_ARTIFACTS)
    add_library(rdv_static STATIC ${RDV_CORE_SOURCES})
    set_target_properties(rdv_static PROPERTIES OUTPUT_NAME "rdv")
    target_include_directories(rdv_static PRIVATE src)

    add_library(rdv_shared SHARED ${RDV_CORE_SOURCES})
    set_target_properties(rdv_shared PROPERTIES
        OUTPUT_NAME "rdv"
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
    target_include_directories(rdv_shared PRIVATE src)
endif()

if(RDV_BUILD_TESTS)
    enable_testing()

    if(EMSCRIPTEN)
        add_executable(rdv_contract_test
            src/test.c
            ${RDV_CORE_SOURCES}
        )
        target_include_directories(rdv_contract_test PRIVATE src)
    else()
        if(RDV_TEST_SHARED_LIBRARY)
            add_library(rdv_test_shared SHARED IMPORTED)
            set_target_properties(rdv_test_shared PROPERTIES
                IMPORTED_LOCATION "${RDV_TEST_SHARED_LIBRARY}"
            )
            if(RDV_TEST_IMPORT_LIBRARY)
                set_target_properties(rdv_test_shared PROPERTIES
                    IMPORTED_IMPLIB "${RDV_TEST_IMPORT_LIBRARY}"
                )
            endif()
            set(RDV_TEST_SHARED_TARGET rdv_test_shared)
        else()
            set(RDV_TEST_SHARED_TARGET rdv_shared)
        endif()

        add_executable(rdv_contract_test src/test.c)
        target_include_directories(rdv_contract_test PRIVATE src)
        target_link_libraries(rdv_contract_test PRIVATE ${RDV_TEST_SHARED_TARGET})

        add_custom_command(TARGET rdv_contract_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${RDV_TEST_SHARED_TARGET}>
                $<TARGET_FILE_DIR:rdv_contract_test>
        )
    endif()
endif()
