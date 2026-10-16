cmake_minimum_required(VERSION 3.14)
project(punch C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED PUNCH_BUILD_VERSION)
    set(PUNCH_BUILD_VERSION 0)
endif()

option(PUNCH_BUILD_TESTS "Build punch tests" OFF)

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    if(APPLE)
        add_definitions(-D_DARWIN_C_SOURCE -D_GNU_SOURCE)
    else()
        add_definitions(-D_GNU_SOURCE)
    endif()
else()
    add_compile_options(/O2 /W3 /WX)
endif()

add_compile_definitions(KC_PUNCH_BUILD_VERSION=${PUNCH_BUILD_VERSION})

set(PUNCH_SOURCES src/libpunch.c)
set(PUNCH_TEST_EXTERNAL_ARTIFACTS OFF)
if(PUNCH_BUILD_TESTS AND PUNCH_TEST_SHARED_LIBRARY)
    set(PUNCH_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT WIN32)
    find_package(Threads REQUIRED)
endif()

if(NOT PUNCH_TEST_EXTERNAL_ARTIFACTS)
    add_library(punch_static STATIC ${PUNCH_SOURCES})
    set_target_properties(punch_static PROPERTIES OUTPUT_NAME "punch")
    target_include_directories(punch_static PUBLIC src)

    add_library(punch_shared SHARED ${PUNCH_SOURCES})
    set_target_properties(punch_shared PROPERTIES OUTPUT_NAME "punch"
        WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_include_directories(punch_shared PUBLIC src)

    if(WIN32)
        target_link_libraries(punch_static PUBLIC ws2_32)
        target_link_libraries(punch_shared PUBLIC ws2_32)
    else()
        target_link_libraries(punch_static PUBLIC Threads::Threads)
        target_link_libraries(punch_shared PUBLIC Threads::Threads)
    endif()
endif()

if(PUNCH_BUILD_TESTS)
    enable_testing()
    if(PUNCH_TEST_SHARED_LIBRARY)
        add_library(punch_test_shared SHARED IMPORTED)
        set_target_properties(punch_test_shared PROPERTIES
            IMPORTED_LOCATION "${PUNCH_TEST_SHARED_LIBRARY}")
        if(PUNCH_TEST_IMPORT_LIBRARY)
            set_target_properties(punch_test_shared PROPERTIES
                IMPORTED_IMPLIB "${PUNCH_TEST_IMPORT_LIBRARY}")
        endif()
        set(PUNCH_TEST_SHARED_TARGET punch_test_shared)
    else()
        set(PUNCH_TEST_SHARED_TARGET punch_shared)
    endif()
    add_executable(punch_contract_test src/test.c)
    target_include_directories(punch_contract_test PRIVATE src)
    target_link_libraries(punch_contract_test PRIVATE ${PUNCH_TEST_SHARED_TARGET})
    if(WIN32)
        target_link_libraries(punch_contract_test PRIVATE ws2_32)
    else()
        target_link_libraries(punch_contract_test PRIVATE Threads::Threads)
    endif()
    add_custom_command(TARGET punch_contract_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${PUNCH_TEST_SHARED_TARGET}>
            $<TARGET_FILE_DIR:punch_contract_test>)
endif()
