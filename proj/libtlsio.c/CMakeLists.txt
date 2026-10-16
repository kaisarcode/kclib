cmake_minimum_required(VERSION 3.14)
project(tlsio C)

set(CMAKE_C_STANDARD 11)

if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc")
endif()

if(NOT DEFINED TLSIO_BUILD_VERSION)
    set(TLSIO_BUILD_VERSION 0)
endif()

option(TLSIO_BUILD_TESTS "Build tlsio tests" OFF)

set(TLSIO_TEST_EXTERNAL_ARTIFACTS OFF)
if(TLSIO_BUILD_TESTS AND TLSIO_TEST_SHARED_LIBRARY)
    set(TLSIO_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

file(GLOB MBEDTLS_SOURCES "lib/mbedtls/library/*.c")
if(NOT MBEDTLS_SOURCES)
    message(FATAL_ERROR "Missing vendored Mbed TLS sources in lib/mbedtls")
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(
        ${MBEDTLS_SOURCES}
        PROPERTIES COMPILE_OPTIONS "-w"
    )
endif()

set(TLSIO_SOURCES
    src/libtlsio.c
    ${MBEDTLS_SOURCES}
)

set_source_files_properties(src/libtlsio.c PROPERTIES
    COMPILE_DEFINITIONS KC_TLSIO_BUILD_VERSION=${TLSIO_BUILD_VERSION}
)

if(NOT TLSIO_TEST_EXTERNAL_ARTIFACTS)
    add_library(tlsio_static STATIC ${TLSIO_SOURCES})
    set_target_properties(tlsio_static PROPERTIES OUTPUT_NAME "tlsio")
    target_include_directories(tlsio_static PRIVATE
        src
        lib/mbedtls
        lib/mbedtls/include
        lib/mbedtls/library
    )
    target_compile_definitions(tlsio_static PRIVATE
        MBEDTLS_USER_CONFIG_FILE="config.h"
    )

    add_library(tlsio_shared SHARED ${TLSIO_SOURCES})
    set_target_properties(tlsio_shared PROPERTIES OUTPUT_NAME "tlsio")
    set_target_properties(tlsio_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_include_directories(tlsio_shared PRIVATE
        src
        lib/mbedtls
        lib/mbedtls/include
        lib/mbedtls/library
    )
    target_compile_definitions(tlsio_shared PRIVATE
        MBEDTLS_USER_CONFIG_FILE="config.h"
    )

    if(WIN32)
        target_link_libraries(tlsio_static PRIVATE ws2_32 bcrypt crypt32)
        target_link_libraries(tlsio_shared PRIVATE ws2_32 bcrypt crypt32)
    elseif(APPLE)
        target_link_libraries(tlsio_static PRIVATE
            "-framework Security"
            "-framework CoreFoundation"
        )
        target_link_libraries(tlsio_shared PRIVATE
            "-framework Security"
            "-framework CoreFoundation"
        )
    endif()
endif()

if(TLSIO_BUILD_TESTS)
    enable_testing()

    if(TLSIO_TEST_SHARED_LIBRARY)
        add_library(tlsio_test_shared SHARED IMPORTED)
        set_target_properties(tlsio_test_shared PROPERTIES
            IMPORTED_LOCATION "${TLSIO_TEST_SHARED_LIBRARY}"
        )
        if(TLSIO_TEST_IMPORT_LIBRARY)
            set_target_properties(tlsio_test_shared PROPERTIES
                IMPORTED_IMPLIB "${TLSIO_TEST_IMPORT_LIBRARY}"
            )
        endif()
        set(TLSIO_TEST_SHARED_TARGET tlsio_test_shared)
    else()
        set(TLSIO_TEST_SHARED_TARGET tlsio_shared)
    endif()

    add_executable(tlsio_contract_test src/test.c)
    target_include_directories(tlsio_contract_test PRIVATE src)
    target_link_libraries(tlsio_contract_test PRIVATE ${TLSIO_TEST_SHARED_TARGET})
    add_custom_command(TARGET tlsio_contract_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${TLSIO_TEST_SHARED_TARGET}>
            $<TARGET_FILE_DIR:tlsio_contract_test>
    )
endif()
