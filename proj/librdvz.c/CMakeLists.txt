cmake_minimum_required(VERSION 3.14)
project(rdvz C)

set(CMAKE_C_STANDARD 11)

# Apple linkers require Darwin archives; host-default GNU archives are not
# accepted even when their members are valid Mach-O objects.
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    find_program(RDVZ_DARWIN_AR NAMES llvm-ar)
    if(NOT RDVZ_DARWIN_AR)
        message(FATAL_ERROR "llvm-ar is required for Darwin static archives")
    endif()
    set(CMAKE_AR "${RDVZ_DARWIN_AR}")
    set(CMAKE_C_ARCHIVE_CREATE
        "<CMAKE_AR> --format=darwin qc <TARGET> <LINK_FLAGS> <OBJECTS>"
    )
    set(CMAKE_C_ARCHIVE_FINISH
        "<CMAKE_AR> --format=darwin s <TARGET>"
    )
endif()

if(NOT DEFINED RDVZ_BUILD_VERSION)
    set(RDVZ_BUILD_VERSION 0)
endif()

option(RDVZ_NATIVE "Enable native hardware optimizations" OFF)
option(RDVZ_BUILD_TESTS "Build rdvz tests" OFF)

set(RDVZ_TEST_EXTERNAL_ARTIFACTS OFF)
if(RDVZ_BUILD_TESTS AND RDVZ_TEST_SHARED_LIBRARY)
    set(RDVZ_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        add_link_options(-fuse-ld=lld)
    endif()
    if(RDVZ_NATIVE)
        add_compile_options(-march=native)
    endif()
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set_source_files_properties(src/librdvz.c PROPERTIES
    COMPILE_DEFINITIONS KC_RDVZ_BUILD_VERSION=${RDVZ_BUILD_VERSION}
)

set(RDVZ_CORE_SOURCES
    src/librdvz.c
)

if(EMSCRIPTEN)
    add_executable(librdvz_wasm ${RDVZ_CORE_SOURCES})
    target_include_directories(librdvz_wasm PRIVATE src)
    set_target_properties(librdvz_wasm PROPERTIES
        OUTPUT_NAME "librdvz"
        SUFFIX ".wasm"
    )
    target_link_options(librdvz_wasm PRIVATE
        --no-entry
        -sSTANDALONE_WASM=1
        -sEXPORTED_FUNCTIONS=_kc_rdvz_open,_kc_rdvz_get,_kc_rdvz_publish,_kc_rdvz_unpublish,_kc_rdvz_seek,_kc_rdvz_remove,_kc_rdvz_prune,_kc_rdvz_close,_kc_rdvz_strerror,_kc_rdvz_version
    )
elseif(NOT RDVZ_TEST_EXTERNAL_ARTIFACTS)
    add_library(rdvz_static STATIC ${RDVZ_CORE_SOURCES})
    set_target_properties(rdvz_static PROPERTIES OUTPUT_NAME "rdvz")
    target_include_directories(rdvz_static PRIVATE src)

    add_library(rdvz_shared SHARED ${RDVZ_CORE_SOURCES})
    set_target_properties(rdvz_shared PROPERTIES
        OUTPUT_NAME "rdvz"
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
    target_include_directories(rdvz_shared PRIVATE src)
endif()

if(RDVZ_BUILD_TESTS)
    enable_testing()

    if(EMSCRIPTEN)
        add_executable(rdvz_contract_test
            src/test.c
            ${RDVZ_CORE_SOURCES}
        )
        target_include_directories(rdvz_contract_test PRIVATE src)
    else()
        if(RDVZ_TEST_SHARED_LIBRARY)
            add_library(rdvz_test_shared SHARED IMPORTED)
            set_target_properties(rdvz_test_shared PROPERTIES
                IMPORTED_LOCATION "${RDVZ_TEST_SHARED_LIBRARY}"
            )
            if(RDVZ_TEST_IMPORT_LIBRARY)
                set_target_properties(rdvz_test_shared PROPERTIES
                    IMPORTED_IMPLIB "${RDVZ_TEST_IMPORT_LIBRARY}"
                )
            endif()
            set(RDVZ_TEST_SHARED_TARGET rdvz_test_shared)
        else()
            set(RDVZ_TEST_SHARED_TARGET rdvz_shared)
        endif()

        add_executable(rdvz_contract_test src/test.c)
        target_include_directories(rdvz_contract_test PRIVATE src)
        target_link_libraries(rdvz_contract_test PRIVATE ${RDVZ_TEST_SHARED_TARGET})

        add_custom_command(TARGET rdvz_contract_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${RDVZ_TEST_SHARED_TARGET}>
                $<TARGET_FILE_DIR:rdvz_contract_test>
        )
    endif()
endif()
