cmake_minimum_required(VERSION 3.14)
project(tlsio C)

set(CMAKE_C_STANDARD 11)

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
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

find_package(OpenSSL 1.1.1 REQUIRED COMPONENTS SSL Crypto)

set_source_files_properties(src/libtlsio.c PROPERTIES
    COMPILE_DEFINITIONS KC_TLSIO_BUILD_VERSION=${TLSIO_BUILD_VERSION}
)

set(TLSIO_SOURCES src/libtlsio.c)

if(NOT TLSIO_TEST_EXTERNAL_ARTIFACTS)
    add_library(tlsio_static STATIC ${TLSIO_SOURCES})
    set_target_properties(tlsio_static PROPERTIES OUTPUT_NAME "tlsio")
    target_include_directories(tlsio_static PRIVATE src)
    target_link_libraries(tlsio_static PRIVATE OpenSSL::SSL OpenSSL::Crypto)

    add_library(tlsio_shared SHARED ${TLSIO_SOURCES})
    set_target_properties(tlsio_shared PROPERTIES
        OUTPUT_NAME "tlsio"
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
    target_include_directories(tlsio_shared PRIVATE src)
    target_link_libraries(tlsio_shared PRIVATE OpenSSL::SSL OpenSSL::Crypto)

    if(WIN32)
        target_link_libraries(tlsio_static PRIVATE ws2_32)
        target_link_libraries(tlsio_shared PRIVATE ws2_32)
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
