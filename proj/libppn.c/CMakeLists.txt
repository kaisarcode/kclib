# CMakeLists.txt
# Summary: Cross-compilation builder for ppn.
#
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: https://www.gnu.org/licenses/gpl-3.0.html

cmake_minimum_required(VERSION 3.14)
project(ppn C)

set(CMAKE_C_STANDARD 11)

if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    message(FATAL_ERROR "ppn supports Linux, Windows, and macOS only")
endif()

if(NOT DEFINED PPN_BUILD_VERSION)
    set(PPN_BUILD_VERSION 0)
endif()

option(PPN_NATIVE "Enable native hardware optimizations" OFF)
option(PPN_BUILD_TESTS "Build ppn tests" OFF)

set(PPN_TEST_EXTERNAL_ARTIFACTS OFF)
if(PPN_BUILD_TESTS AND PPN_TEST_SHARED_LIBRARY)
    set(PPN_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(PPN_NATIVE)
        add_compile_options(-march=native)
    endif()
    if(NOT WIN32)
        add_definitions(-D_GNU_SOURCE)
    endif()
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set(PPN_SOURCES src/libppn.c)
set_source_files_properties(src/libppn.c PROPERTIES
    COMPILE_DEFINITIONS KC_PPN_BUILD_VERSION=${PPN_BUILD_VERSION}
)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    if(NOT CMAKE_OBJC_COMPILER AND CMAKE_C_COMPILER)
        set(CMAKE_OBJC_COMPILER "${CMAKE_C_COMPILER}" CACHE FILEPATH
            "Objective-C compiler")
    endif()
    enable_language(OBJC)
    set_source_files_properties(src/libppn.c PROPERTIES LANGUAGE OBJC)
endif()

if(NOT PPN_TEST_EXTERNAL_ARTIFACTS)
    add_library(ppn_static STATIC ${PPN_SOURCES})
    set_target_properties(ppn_static PROPERTIES OUTPUT_NAME "ppn")
    target_include_directories(ppn_static PRIVATE src)

    add_library(ppn_shared SHARED ${PPN_SOURCES})
    set_target_properties(ppn_shared PROPERTIES OUTPUT_NAME "ppn")
    set_target_properties(ppn_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_include_directories(ppn_shared PRIVATE src)

    if(WIN32)
        target_link_libraries(ppn_static PRIVATE user32 shell32)
        target_link_libraries(ppn_shared PRIVATE user32 shell32)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        target_link_libraries(ppn_static PRIVATE "-framework Foundation")
        target_link_libraries(ppn_shared PRIVATE "-framework Foundation")
    endif()
endif()

if(PPN_BUILD_TESTS)
    enable_testing()

    if(PPN_TEST_SHARED_LIBRARY)
        add_library(ppn_test_shared SHARED IMPORTED)
        set_target_properties(ppn_test_shared PROPERTIES
            IMPORTED_LOCATION "${PPN_TEST_SHARED_LIBRARY}"
        )
        if(PPN_TEST_IMPORT_LIBRARY)
            set_target_properties(ppn_test_shared PROPERTIES
                IMPORTED_IMPLIB "${PPN_TEST_IMPORT_LIBRARY}"
            )
        endif()
        set(PPN_TEST_SHARED_TARGET ppn_test_shared)
    else()
        set(PPN_TEST_SHARED_TARGET ppn_shared)
    endif()

    add_executable(ppn_contract_test src/test.c)
    target_include_directories(ppn_contract_test PRIVATE src)
    target_link_libraries(ppn_contract_test PRIVATE ${PPN_TEST_SHARED_TARGET})

    if(WIN32)
        target_link_libraries(ppn_contract_test PRIVATE user32 shell32)
    endif()

    add_custom_command(TARGET ppn_contract_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${PPN_TEST_SHARED_TARGET}>
            $<TARGET_FILE_DIR:ppn_contract_test>
    )
endif()
