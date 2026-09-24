cmake_minimum_required(VERSION 3.14)
project(wch C)

set(CMAKE_C_STANDARD 11)
if(NOT DEFINED KC_WCH_BUILD_VERSION)
    set(KC_WCH_BUILD_VERSION 0)
endif()

option(WCH_NATIVE "Enable native hardware optimizations" OFF)
option(WCH_BUILD_TESTS "Build wch tests" OFF)
set(WCH_TEST_CLI "" CACHE STRING "Path to the wch CLI used by contract tests")

set(WCH_TEST_EXTERNAL_ARTIFACTS OFF)
if(WCH_BUILD_TESTS AND WCH_TEST_SHARED_LIBRARY)
    set(WCH_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(APPLE OR EMSCRIPTEN)
        add_compile_options(-Wno-unused-function)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    if(WCH_NATIVE)
        add_compile_options(-march=native)
    endif()
    if(WIN32)
        add_definitions(-D_GNU_SOURCE -DWINVER=0x0600 -D_WIN32_WINNT=0x0600)
    elseif(APPLE)
        add_definitions(-D_DARWIN_C_SOURCE -D_GNU_SOURCE)
    else()
        add_definitions(-D_GNU_SOURCE)
    endif()
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set_source_files_properties(src/libwch.c PROPERTIES
    COMPILE_DEFINITIONS KC_WCH_BUILD_VERSION=${KC_WCH_BUILD_VERSION}
)

if(NOT WCH_TEST_EXTERNAL_ARTIFACTS)
    add_executable(wch
        src/wch.c
        src/libwch.c
    )

    target_include_directories(wch PRIVATE src)

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(wch pthread m)
    endif()

    add_library(wch_static STATIC src/libwch.c)
    set_target_properties(wch_static PROPERTIES OUTPUT_NAME "wch")
    target_include_directories(wch_static PRIVATE src)

    add_library(wch_shared SHARED src/libwch.c)
    set_target_properties(wch_shared PROPERTIES OUTPUT_NAME "wch")
    set_target_properties(wch_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_include_directories(wch_shared PRIVATE src)

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(wch_shared pthread m)
    endif()
endif()

if(WCH_BUILD_TESTS)
    enable_testing()

    if(WCH_TEST_SHARED_LIBRARY)
        add_library(wch_test_shared SHARED IMPORTED)
        set_target_properties(wch_test_shared PROPERTIES
            IMPORTED_LOCATION "${WCH_TEST_SHARED_LIBRARY}"
        )
        if(WCH_TEST_IMPORT_LIBRARY)
            set_target_properties(wch_test_shared PROPERTIES
                IMPORTED_IMPLIB "${WCH_TEST_IMPORT_LIBRARY}"
            )
        endif()
    else()
        set(WCH_TEST_SHARED_TARGET wch_shared)
    endif()

    if(TARGET wch_test_shared)
        set(WCH_TEST_SHARED_TARGET wch_test_shared)
    endif()

    add_executable(wch_contract_test src/test.c)
    target_include_directories(wch_contract_test PRIVATE src)
    target_link_libraries(wch_contract_test PRIVATE ${WCH_TEST_SHARED_TARGET})

    if(WCH_TEST_CLI)
        target_compile_definitions(
            wch_contract_test
            PRIVATE WCH_TEST_CLI="${WCH_TEST_CLI}"
        )
    elseif(TARGET wch)
        target_compile_definitions(
            wch_contract_test
            PRIVATE WCH_TEST_CLI="$<TARGET_FILE:wch>"
        )
    endif()

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(wch_contract_test PRIVATE m)
    endif()

    add_custom_command(TARGET wch_contract_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${WCH_TEST_SHARED_TARGET}>
            $<TARGET_FILE_DIR:wch_contract_test>
    )

endif()
