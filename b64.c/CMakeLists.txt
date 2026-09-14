cmake_minimum_required(VERSION 3.14)
project(b64 C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED B64_BUILD_VERSION)
    set(B64_BUILD_VERSION 0)
endif()

option(B64_NATIVE "Enable native hardware optimizations" OFF)
option(B64_BUILD_TESTS "Build b64 tests" OFF)

set(B64_TEST_EXTERNAL_ARTIFACTS OFF)
if(B64_BUILD_TESTS AND B64_TEST_SHARED_LIBRARY)
    set(B64_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    if(B64_NATIVE)
        add_compile_options(-march=native)
    endif()
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set_source_files_properties(src/libb64.c PROPERTIES
    COMPILE_DEFINITIONS KC_B64_BUILD_VERSION=${B64_BUILD_VERSION}
)

if(NOT B64_TEST_EXTERNAL_ARTIFACTS)
    add_library(b64_static STATIC src/libb64.c)
    set_target_properties(b64_static PROPERTIES OUTPUT_NAME "b64")
    target_include_directories(b64_static PUBLIC src)

    add_library(b64_shared SHARED src/libb64.c)
    set_target_properties(b64_shared PROPERTIES OUTPUT_NAME "b64")
    set_target_properties(b64_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_include_directories(b64_shared PUBLIC src)

    add_executable(b64 src/b64.c)
    target_link_libraries(b64 b64_static)
endif()

if(B64_BUILD_TESTS)
    if(B64_TEST_SHARED_LIBRARY)
        add_library(b64_test_shared SHARED IMPORTED)
        set_target_properties(b64_test_shared PROPERTIES
            IMPORTED_LOCATION "${B64_TEST_SHARED_LIBRARY}"
        )
        if(B64_TEST_IMPORT_LIBRARY)
            set_target_properties(b64_test_shared PROPERTIES
                IMPORTED_IMPLIB "${B64_TEST_IMPORT_LIBRARY}"
            )
        endif()
    else()
        set(B64_TEST_SHARED_TARGET b64_shared)
    endif()

    if(TARGET b64_test_shared)
        set(B64_TEST_SHARED_TARGET b64_test_shared)
    endif()

    add_executable(b64_contract_test src/test.c)
    target_include_directories(b64_contract_test PRIVATE src)
    target_link_libraries(b64_contract_test PRIVATE ${B64_TEST_SHARED_TARGET})

    add_custom_command(TARGET b64_contract_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${B64_TEST_SHARED_TARGET}>
            $<TARGET_FILE_DIR:b64_contract_test>
    )
endif()
