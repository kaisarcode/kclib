# CMakeLists.txt
# Summary: Cross-compilation builder for tray.
#
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: https://www.gnu.org/licenses/gpl-3.0.html

cmake_minimum_required(VERSION 3.14)
project(tray C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED TRAY_BUILD_VERSION)
    set(TRAY_BUILD_VERSION 0)
endif()

option(LIBTRAY_NATIVE "Enable native hardware optimizations" OFF)
option(TRAY_BUILD_TESTS "Build tray tests" OFF)

set(TRAY_TEST_EXTERNAL_ARTIFACTS OFF)
if(TRAY_BUILD_TESTS AND TRAY_TEST_SHARED_LIBRARY)
    set(TRAY_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    if(LIBTRAY_NATIVE)
        add_compile_options(-march=native)
    endif()
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

if(NOT TRAY_TEST_EXTERNAL_ARTIFACTS)
    if(WIN32)
        set(TRAY_SOURCES src/libtray.c)
        set_source_files_properties(src/libtray.c PROPERTIES
            COMPILE_DEFINITIONS KC_TRAY_BUILD_VERSION=${TRAY_BUILD_VERSION}
        )

        add_executable(tray src/tray.c ${TRAY_SOURCES})
        target_include_directories(tray PRIVATE src)
        target_link_libraries(tray user32 shell32)

        add_library(tray_static STATIC ${TRAY_SOURCES})
        set_target_properties(tray_static PROPERTIES OUTPUT_NAME "tray")
        target_include_directories(tray_static PRIVATE src)
        target_link_libraries(tray_static user32 shell32)

        add_library(tray_shared SHARED ${TRAY_SOURCES})
        set_target_properties(tray_shared PROPERTIES OUTPUT_NAME "tray")
        set_target_properties(tray_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
        target_include_directories(tray_shared PRIVATE src)
        target_link_libraries(tray_shared user32 shell32)
    else()
        set(TRAY_SOURCES src/libtray.c)
        set_source_files_properties(src/libtray.c PROPERTIES
            COMPILE_DEFINITIONS KC_TRAY_BUILD_VERSION=${TRAY_BUILD_VERSION}
        )

        if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            if(NOT CMAKE_OBJC_COMPILER AND CMAKE_C_COMPILER)
                set(CMAKE_OBJC_COMPILER "${CMAKE_C_COMPILER}" CACHE FILEPATH
                    "Objective-C compiler")
            endif()
            enable_language(OBJC)
            set_source_files_properties(src/libtray.c PROPERTIES
                LANGUAGE OBJC
            )
            add_executable(tray src/tray.c ${TRAY_SOURCES})
            target_include_directories(tray PRIVATE src)
            target_link_libraries(tray "-framework Cocoa" "-framework Foundation" pthread m)

            add_library(tray_static STATIC ${TRAY_SOURCES})
            set_target_properties(tray_static PROPERTIES OUTPUT_NAME "tray")
            target_include_directories(tray_static PRIVATE src)
            target_link_libraries(tray_static pthread m)

            add_library(tray_shared SHARED ${TRAY_SOURCES})
            set_target_properties(tray_shared PROPERTIES OUTPUT_NAME "tray")
            set_target_properties(tray_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
            target_include_directories(tray_shared PRIVATE src)
            target_link_libraries(tray_shared "-framework Cocoa" "-framework Foundation" pthread m)
        else()
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

            add_executable(tray src/tray.c ${TRAY_SOURCES})
            target_include_directories(tray PRIVATE src ${GTK3_INCLUDE_DIRS})
            target_compile_options(tray PRIVATE ${GTK3_CFLAGS_OTHER})
            target_link_directories(tray PRIVATE ${GTK3_LIBRARY_DIRS})
            target_link_libraries(tray ${GTK3_LIBRARIES} pthread m dl)

            add_library(tray_static STATIC ${TRAY_SOURCES})
            set_target_properties(tray_static PROPERTIES OUTPUT_NAME "tray")
            target_include_directories(tray_static PRIVATE src ${GTK3_INCLUDE_DIRS})
            target_compile_options(tray_static PRIVATE ${GTK3_CFLAGS_OTHER})
            target_link_directories(tray_static PRIVATE ${GTK3_LIBRARY_DIRS})
            target_link_libraries(tray_static ${GTK3_LIBRARIES} pthread m dl)

            add_library(tray_shared SHARED ${TRAY_SOURCES})
            set_target_properties(tray_shared PROPERTIES OUTPUT_NAME "tray")
            target_include_directories(tray_shared PRIVATE src ${GTK3_INCLUDE_DIRS})
            target_compile_options(tray_shared PRIVATE ${GTK3_CFLAGS_OTHER})
            target_link_directories(tray_shared PRIVATE ${GTK3_LIBRARY_DIRS})
            target_link_libraries(tray_shared ${GTK3_LIBRARIES} pthread m dl)
        endif()
    endif()
endif()

if(TRAY_BUILD_TESTS)
    if(TRAY_TEST_SHARED_LIBRARY)
        add_library(tray_test_shared SHARED IMPORTED)
        set_target_properties(tray_test_shared PROPERTIES
            IMPORTED_LOCATION "${TRAY_TEST_SHARED_LIBRARY}"
        )
        if(TRAY_TEST_IMPORT_LIBRARY)
            set_target_properties(tray_test_shared PROPERTIES
                IMPORTED_IMPLIB "${TRAY_TEST_IMPORT_LIBRARY}"
            )
        endif()
        set(TRAY_TEST_SHARED_TARGET tray_test_shared)
    else()
        set(TRAY_TEST_SHARED_TARGET tray_shared)
    endif()

    if(TRAY_TEST_CLI)
        set(TRAY_TEST_CLI_TARGET_PATH "${TRAY_TEST_CLI}")
    elseif(TARGET tray)
        set(TRAY_TEST_CLI_TARGET_PATH "$<TARGET_FILE:tray>")
    else()
        set(TRAY_TEST_CLI_TARGET_PATH "")
    endif()

    add_executable(tray_contract_test src/test.c)
    target_include_directories(tray_contract_test PRIVATE src)
    target_link_libraries(tray_contract_test PRIVATE ${TRAY_TEST_SHARED_TARGET})
    target_compile_definitions(tray_contract_test PRIVATE
        KC_TRAY_TEST_CLI="${TRAY_TEST_CLI_TARGET_PATH}"
    )

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GTK3_TEST gtk+-3.0)
        target_include_directories(tray_contract_test PRIVATE ${GTK3_TEST_INCLUDE_DIRS})
        target_compile_options(tray_contract_test PRIVATE ${GTK3_TEST_CFLAGS_OTHER})
        target_link_directories(tray_contract_test PRIVATE ${GTK3_TEST_LIBRARY_DIRS})
        target_link_libraries(tray_contract_test PRIVATE ${GTK3_TEST_LIBRARIES} pthread m dl)
    endif()

    if(WIN32)
        target_link_libraries(tray_contract_test PRIVATE user32 shell32)
    endif()

    add_custom_command(TARGET tray_contract_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${TRAY_TEST_SHARED_TARGET}>
            $<TARGET_FILE_DIR:tray_contract_test>
    )

    if(WIN32 AND TRAY_TEST_CLI)
        add_custom_command(TARGET tray_contract_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${TRAY_TEST_CLI}"
                $<TARGET_FILE_DIR:tray_contract_test>/tray.exe
        )
    endif()
endif()
