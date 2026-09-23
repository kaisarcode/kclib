cmake_minimum_required(VERSION 3.14)
project(init C)

set(CMAKE_C_STANDARD 11)

if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "init supports Linux and Windows only")
endif()

if(NOT DEFINED INIT_BUILD_VERSION)
    set(INIT_BUILD_VERSION 0)
endif()

option(INIT_NATIVE "Enable native hardware optimizations" OFF)
option(INIT_BUILD_TESTS "Build init tests" OFF)
set(INIT_TEST_CLI "" CACHE STRING "Path to the init CLI used by contract tests")

set(INIT_TEST_EXTERNAL_ARTIFACTS OFF)
if(INIT_BUILD_TESTS AND INIT_TEST_SHARED_LIBRARY)
    set(INIT_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    if(INIT_NATIVE)
        add_compile_options(-march=native)
    endif()
    if(NOT WIN32)
        add_definitions(-D_GNU_SOURCE)
    endif()
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set_source_files_properties(src/libinit.c PROPERTIES
    COMPILE_DEFINITIONS KC_INIT_BUILD_VERSION=${INIT_BUILD_VERSION}
)

if(NOT INIT_TEST_EXTERNAL_ARTIFACTS)
    add_executable(init
        src/init.c
        src/libinit.c
    )

    target_include_directories(init PRIVATE src)

    add_library(init_static STATIC src/libinit.c)
    set_target_properties(init_static PROPERTIES OUTPUT_NAME "init")
    target_include_directories(init_static PRIVATE src)

    add_library(init_shared SHARED src/libinit.c)
    set_target_properties(init_shared PROPERTIES OUTPUT_NAME "init")
    set_target_properties(init_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_include_directories(init_shared PRIVATE src)

    if(WIN32)
        target_link_libraries(init PRIVATE advapi32)
        target_link_libraries(init_static PRIVATE advapi32)
        target_link_libraries(init_shared PRIVATE advapi32)
    endif()
endif()

if(INIT_BUILD_TESTS)
    enable_testing()

    if(INIT_TEST_SHARED_LIBRARY)
        add_library(init_test_shared SHARED IMPORTED)
        set_target_properties(init_test_shared PROPERTIES
            IMPORTED_LOCATION "${INIT_TEST_SHARED_LIBRARY}"
        )
        if(INIT_TEST_IMPORT_LIBRARY)
            set_target_properties(init_test_shared PROPERTIES
                IMPORTED_IMPLIB "${INIT_TEST_IMPORT_LIBRARY}"
            )
        endif()
    else()
        set(INIT_TEST_SHARED_TARGET init_shared)
    endif()

    if(TARGET init_test_shared)
        set(INIT_TEST_SHARED_TARGET init_test_shared)
    endif()

    add_executable(init_contract_test src/test.c)
    target_include_directories(init_contract_test PRIVATE src)
    target_link_libraries(init_contract_test PRIVATE ${INIT_TEST_SHARED_TARGET})

    if(INIT_TEST_CLI)
        target_compile_definitions(init_contract_test PRIVATE INIT_TEST_CLI="${INIT_TEST_CLI}")
    elseif(TARGET init)
        target_compile_definitions(init_contract_test PRIVATE INIT_TEST_CLI="$<TARGET_FILE:init>")
    endif()

    if(WIN32)
        target_link_libraries(init_contract_test PRIVATE advapi32)
    endif()

    add_custom_command(TARGET init_contract_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${INIT_TEST_SHARED_TARGET}>
            $<TARGET_FILE_DIR:init_contract_test>
    )
endif()
