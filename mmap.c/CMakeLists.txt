cmake_minimum_required(VERSION 3.14)
project(mmap C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED MMAP_BUILD_VERSION)
    set(MMAP_BUILD_VERSION 0)
endif()

option(MMAP_NATIVE "Enable native hardware optimizations" OFF)
option(MMAP_BUILD_TESTS "Build mmap tests" OFF)

set(MMAP_TEST_EXTERNAL_ARTIFACTS OFF)
if(MMAP_BUILD_TESTS AND MMAP_TEST_SHARED_LIBRARY)
    set(MMAP_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    if(MMAP_NATIVE)
        add_compile_options(-march=native)
    endif()
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set(MMAP_SOURCES src/libmmap.c)

set_source_files_properties(src/libmmap.c PROPERTIES
    COMPILE_DEFINITIONS KC_MMAP_BUILD_VERSION=${MMAP_BUILD_VERSION}
)

if(NOT MMAP_TEST_EXTERNAL_ARTIFACTS)
    add_executable(mmap
        src/mmap.c
        ${MMAP_SOURCES}
    )

    target_include_directories(mmap PRIVATE src)

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(mmap pthread m)
    endif()

    add_library(mmap_static STATIC ${MMAP_SOURCES})
    set_target_properties(mmap_static PROPERTIES OUTPUT_NAME "mmap")
    target_include_directories(mmap_static PRIVATE src)

    add_library(mmap_shared SHARED ${MMAP_SOURCES})
    set_target_properties(mmap_shared PROPERTIES OUTPUT_NAME "mmap")
    set_target_properties(mmap_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_include_directories(mmap_shared PRIVATE src)

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(mmap_shared pthread m)
    endif()
endif()

if(MMAP_BUILD_TESTS)
    enable_testing()

    if(MMAP_TEST_SHARED_LIBRARY)
        add_library(mmap_test_shared SHARED IMPORTED)
        set_target_properties(mmap_test_shared PROPERTIES
            IMPORTED_LOCATION "${MMAP_TEST_SHARED_LIBRARY}"
        )
        if(MMAP_TEST_IMPORT_LIBRARY)
            set_target_properties(mmap_test_shared PROPERTIES
                IMPORTED_IMPLIB "${MMAP_TEST_IMPORT_LIBRARY}"
            )
        endif()
    else()
        set(MMAP_TEST_SHARED_TARGET mmap_shared)
    endif()

    if(TARGET mmap_test_shared)
        set(MMAP_TEST_SHARED_TARGET mmap_test_shared)
    endif()

    add_executable(mmap_contract_test src/test.c)
    target_include_directories(mmap_contract_test PRIVATE src)
    target_link_libraries(mmap_contract_test PRIVATE ${MMAP_TEST_SHARED_TARGET})

    add_custom_command(TARGET mmap_contract_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${MMAP_TEST_SHARED_TARGET}>
            $<TARGET_FILE_DIR:mmap_contract_test>
    )
endif()
