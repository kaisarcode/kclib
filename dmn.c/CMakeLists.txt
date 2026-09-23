cmake_minimum_required(VERSION 3.14)
project(dmn C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED DMN_BUILD_VERSION)
    set(DMN_BUILD_VERSION 0)
endif()

option(DMN_NATIVE "Enable native hardware optimizations" OFF)
option(DMN_BUILD_TESTS "Build dmn tests" OFF)
set(DMN_TEST_CLI "" CACHE STRING "Path to the dmn CLI used by contract tests")

set(DMN_TEST_EXTERNAL_ARTIFACTS OFF)
if(DMN_BUILD_TESTS AND DMN_TEST_SHARED_LIBRARY)
    set(DMN_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    if(DMN_NATIVE)
        add_compile_options(-march=native)
    endif()
    if(NOT WIN32)
        add_definitions(-D_GNU_SOURCE)
    endif()
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set_source_files_properties(src/libdmn.c PROPERTIES
    COMPILE_DEFINITIONS KC_DMN_BUILD_VERSION=${DMN_BUILD_VERSION}
)

set(DMN_SOURCES src/libdmn.c)

if(NOT DMN_TEST_EXTERNAL_ARTIFACTS)
    add_executable(dmn
        src/dmn.c
        ${DMN_SOURCES}
    )

    target_include_directories(dmn PRIVATE src)

    add_library(dmn_static STATIC ${DMN_SOURCES})
    set_target_properties(dmn_static PROPERTIES OUTPUT_NAME "dmn")
    target_include_directories(dmn_static PRIVATE src)

    add_library(dmn_shared SHARED ${DMN_SOURCES})
    set_target_properties(dmn_shared PROPERTIES OUTPUT_NAME "dmn")
    set_target_properties(dmn_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_include_directories(dmn_shared PRIVATE src)
endif()

if(DMN_BUILD_TESTS)
    enable_testing()

    if(DMN_TEST_SHARED_LIBRARY)
        add_library(dmn_test_shared SHARED IMPORTED)
        set_target_properties(dmn_test_shared PROPERTIES
            IMPORTED_LOCATION "${DMN_TEST_SHARED_LIBRARY}"
        )
        if(DMN_TEST_IMPORT_LIBRARY)
            set_target_properties(dmn_test_shared PROPERTIES
                IMPORTED_IMPLIB "${DMN_TEST_IMPORT_LIBRARY}"
            )
        endif()
    else()
        set(DMN_TEST_SHARED_TARGET dmn_shared)
    endif()

    if(TARGET dmn_test_shared)
        set(DMN_TEST_SHARED_TARGET dmn_test_shared)
    endif()

    add_executable(dmn_contract_test src/test.c)
    target_include_directories(dmn_contract_test PRIVATE src)
    target_link_libraries(dmn_contract_test PRIVATE ${DMN_TEST_SHARED_TARGET})

    if(DMN_TEST_CLI)
        target_compile_definitions(dmn_contract_test PRIVATE DMN_TEST_CLI="${DMN_TEST_CLI}")
    elseif(TARGET dmn)
        target_compile_definitions(dmn_contract_test PRIVATE DMN_TEST_CLI="$<TARGET_FILE:dmn>")
    endif()

    add_custom_command(TARGET dmn_contract_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${DMN_TEST_SHARED_TARGET}>
            $<TARGET_FILE_DIR:dmn_contract_test>
    )

endif()
