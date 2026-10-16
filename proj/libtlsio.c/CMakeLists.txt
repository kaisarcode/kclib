cmake_minimum_required(VERSION 3.14)
project(tlsio C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED TLSIO_BUILD_VERSION)
    set(TLSIO_BUILD_VERSION 0)
endif()

option(TLSIO_BUILD_TESTS "Build tlsio tests" OFF)

set(TLSIO_MBEDTLS_VERSION "3.6.7")
set(TLSIO_MBEDTLS_SHA256
    "a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6"
)
set(TLSIO_MBEDTLS_SOURCE_DIR "" CACHE PATH
    "Optional local Mbed TLS source directory"
)

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

include(FetchContent)

if(TLSIO_MBEDTLS_SOURCE_DIR)
    set(FETCHCONTENT_SOURCE_DIR_MBEDTLS
        "${TLSIO_MBEDTLS_SOURCE_DIR}"
        CACHE PATH "" FORCE
    )
endif()

FetchContent_Declare(
    mbedtls
    URL
        "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-${TLSIO_MBEDTLS_VERSION}/mbedtls-${TLSIO_MBEDTLS_VERSION}.tar.bz2"
    URL_HASH "SHA256=${TLSIO_MBEDTLS_SHA256}"
    SOURCE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/.build/deps/mbedtls-${TLSIO_MBEDTLS_VERSION}"
    BINARY_DIR
        "${CMAKE_BINARY_DIR}/mbedtls-build"
)

FetchContent_GetProperties(mbedtls)
if(NOT mbedtls_POPULATED)
    FetchContent_Populate(mbedtls)
endif()

set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
set(MBEDTLS_USER_CONFIG_FILE "tlsio-mbedtls.h" CACHE STRING "" FORCE)

add_subdirectory(
    "${mbedtls_SOURCE_DIR}"
    "${mbedtls_BINARY_DIR}"
    EXCLUDE_FROM_ALL
)

function(tlsio_collect_sources target output)
    get_target_property(target_sources ${target} SOURCES)
    get_target_property(target_source_dir ${target} SOURCE_DIR)
    set(result "")

    foreach(source IN LISTS target_sources)
        if(IS_ABSOLUTE "${source}")
            list(APPEND result "${source}")
        elseif(source MATCHES "^\\$<")
            message(FATAL_ERROR "Unsupported generated Mbed TLS source: ${source}")
        else()
            list(APPEND result "${target_source_dir}/${source}")
        endif()
    endforeach()

    set(${output} "${result}" PARENT_SCOPE)
endfunction()

tlsio_collect_sources(mbedcrypto TLSIO_MBEDCRYPTO_SOURCES)
tlsio_collect_sources(mbedx509 TLSIO_MBEDX509_SOURCES)
tlsio_collect_sources(mbedtls TLSIO_MBEDTLS_SOURCES)

set(TLSIO_BACKEND_SOURCES
    ${TLSIO_MBEDCRYPTO_SOURCES}
    ${TLSIO_MBEDX509_SOURCES}
    ${TLSIO_MBEDTLS_SOURCES}
)
list(REMOVE_DUPLICATES TLSIO_BACKEND_SOURCES)

set_source_files_properties(src/libtlsio.c PROPERTIES
    COMPILE_DEFINITIONS KC_TLSIO_BUILD_VERSION=${TLSIO_BUILD_VERSION}
)

if(MSVC)
    set_source_files_properties(
        ${TLSIO_BACKEND_SOURCES}
        PROPERTIES COMPILE_OPTIONS "/w"
    )
else()
    set_source_files_properties(
        ${TLSIO_BACKEND_SOURCES}
        PROPERTIES
            COMPILE_OPTIONS "-w;-fvisibility=hidden;-ffunction-sections;-fdata-sections"
    )
endif()

set(TLSIO_SOURCES
    src/libtlsio.c
    src/socket.c
    src/trust.c
    ${TLSIO_BACKEND_SOURCES}
)

add_library(tlsio_objects OBJECT ${TLSIO_SOURCES})
set_target_properties(tlsio_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(tlsio_objects PRIVATE
    src
    "${mbedtls_SOURCE_DIR}/include"
    "${mbedtls_SOURCE_DIR}/library"
    "${mbedtls_BINARY_DIR}/library"
)
target_compile_definitions(tlsio_objects PRIVATE
    MBEDTLS_USER_CONFIG_FILE="tlsio-mbedtls.h"
)

add_library(tlsio_static STATIC $<TARGET_OBJECTS:tlsio_objects>)
set_target_properties(tlsio_static PROPERTIES OUTPUT_NAME "tlsio")

if(WIN32)
    add_library(
        tlsio_shared SHARED
        $<TARGET_OBJECTS:tlsio_objects>
        src/tlsio.def
    )
else()
    add_library(tlsio_shared SHARED $<TARGET_OBJECTS:tlsio_objects>)
endif()
set_target_properties(tlsio_shared PROPERTIES OUTPUT_NAME "tlsio")

if(NOT MSVC)
    if(APPLE)
        target_link_options(tlsio_shared PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(tlsio_shared PRIVATE -Wl,--gc-sections)
    endif()
endif()

if(WIN32)
    target_link_libraries(tlsio_shared PRIVATE ws2_32 bcrypt crypt32)
elseif(APPLE)
    target_link_libraries(tlsio_shared PRIVATE
        "-framework Security"
        "-framework CoreFoundation"
    )
endif()

if(TLSIO_BUILD_TESTS)
    enable_testing()

    add_executable(tlsio_contract_test src/test.c)
    target_include_directories(tlsio_contract_test PRIVATE src)
    target_link_libraries(tlsio_contract_test PRIVATE tlsio_shared)

    if(WIN32)
        add_custom_command(TARGET tlsio_contract_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:tlsio_shared>
                $<TARGET_FILE_DIR:tlsio_contract_test>
        )
    endif()
endif()
