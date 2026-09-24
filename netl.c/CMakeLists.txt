cmake_minimum_required(VERSION 3.14)
project(netl C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED NETL_BUILD_VERSION)
    set(NETL_BUILD_VERSION 0)
endif()

option(NETL_NATIVE "Enable native hardware optimizations" OFF)
option(NETL_BUILD_TESTS "Build netl tests" OFF)
set(NETL_TEST_CLI_EXECUTABLE "" CACHE FILEPATH "Prebuilt netl CLI for contract tests")

set(NETL_TEST_EXTERNAL_ARTIFACTS OFF)
if(NETL_BUILD_TESTS AND NETL_TEST_SHARED_LIBRARY)
    set(NETL_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    if(NETL_NATIVE)
        add_compile_options(-march=native)
    endif()
    if(APPLE)
        add_definitions(-D_DARWIN_C_SOURCE -D_GNU_SOURCE)
    else()
        add_definitions(-D_GNU_SOURCE)
    endif()
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set_source_files_properties(src/libnetl.c PROPERTIES
    COMPILE_DEFINITIONS KC_NETL_BUILD_VERSION=${NETL_BUILD_VERSION}
)

set(NETL_SOURCES src/libnetl.c)

if(NOT NETL_TEST_EXTERNAL_ARTIFACTS)
    add_executable(netl
        src/netl.c
        ${NETL_SOURCES}
    )

    target_include_directories(netl PRIVATE src)
    target_compile_definitions(netl PRIVATE KC_NETL_CLI=1)

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(netl pthread m)
    endif()

    if(WIN32)
        target_link_libraries(netl ws2_32)
    endif()

    add_library(netl_static STATIC ${NETL_SOURCES})
    set_target_properties(netl_static PROPERTIES OUTPUT_NAME "netl")
    target_include_directories(netl_static PRIVATE src)

    add_library(netl_shared SHARED ${NETL_SOURCES})
    set_target_properties(netl_shared PROPERTIES OUTPUT_NAME "netl")
    set_target_properties(netl_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_include_directories(netl_shared PRIVATE src)

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(netl_shared pthread m)
    endif()

    if(WIN32)
        target_link_libraries(netl_shared ws2_32)
    endif()
endif()

if(NETL_BUILD_TESTS)
    enable_testing()

    if(NETL_TEST_SHARED_LIBRARY)
        add_library(netl_test_shared SHARED IMPORTED)
        set_target_properties(netl_test_shared PROPERTIES
            IMPORTED_LOCATION "${NETL_TEST_SHARED_LIBRARY}"
        )
        if(NETL_TEST_IMPORT_LIBRARY)
            set_target_properties(netl_test_shared PROPERTIES
                IMPORTED_IMPLIB "${NETL_TEST_IMPORT_LIBRARY}"
            )
        endif()
    else()
        set(NETL_TEST_SHARED_TARGET netl_shared)
    endif()

    if(TARGET netl_test_shared)
        set(NETL_TEST_SHARED_TARGET netl_test_shared)
    endif()

    add_executable(netl_contract_test src/test.c)
    target_include_directories(netl_contract_test PRIVATE src)
    target_link_libraries(netl_contract_test PRIVATE ${NETL_TEST_SHARED_TARGET})

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(netl_contract_test PRIVATE pthread m)
    endif()

    if(WIN32)
        target_link_libraries(netl_contract_test PRIVATE ws2_32)
    endif()

    add_custom_command(TARGET netl_contract_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${NETL_TEST_SHARED_TARGET}>
            $<TARGET_FILE_DIR:netl_contract_test>
    )

    if(NETL_TEST_CLI_EXECUTABLE)
        add_custom_command(TARGET netl_contract_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${NETL_TEST_CLI_EXECUTABLE}"
                "$<TARGET_FILE_DIR:netl_contract_test>/netl${CMAKE_EXECUTABLE_SUFFIX}"
        )
    elseif(TARGET netl)
        add_custom_command(TARGET netl_contract_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:netl>
                "$<TARGET_FILE_DIR:netl_contract_test>/netl${CMAKE_EXECUTABLE_SUFFIX}"
        )
    endif()
endif()
