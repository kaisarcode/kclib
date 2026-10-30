cmake_minimum_required(VERSION 3.14)
project(tls C)

set(CMAKE_C_STANDARD 11)

if(APPLE)
    get_property(TLS_AR CACHE CMAKE_AR PROPERTY VALUE)
    get_property(TLS_RANLIB CACHE CMAKE_RANLIB PROPERTY VALUE)
    set(CMAKE_AR "${TLS_AR}")
    set(CMAKE_RANLIB "${TLS_RANLIB}")
    set(CMAKE_C_ARCHIVE_CREATE
        "<CMAKE_AR> qc <TARGET> <LINK_FLAGS> <OBJECTS>"
    )
    set(CMAKE_C_ARCHIVE_FINISH "<CMAKE_RANLIB> <TARGET>")
endif()

if(NOT WIN32)
    find_package(Threads REQUIRED)
endif()

if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc")
endif()

if(NOT DEFINED TLS_BUILD_VERSION)
    set(TLS_BUILD_VERSION 0)
endif()

option(TLS_BUILD_TESTS "Build tls tests" OFF)

set(TLS_TEST_EXTERNAL_ARTIFACTS OFF)
if(TLS_BUILD_TESTS AND TLS_TEST_SHARED_LIBRARY)
    set(TLS_TEST_EXTERNAL_ARTIFACTS ON)
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

set(TLS_DISABLED_MBEDTLS_SOURCES
    aria.c
    bignum_mod.c
    bignum_mod_raw.c
    block_cipher.c
    camellia.c
    ccm.c
    cmac.c
    des.c
    dhm.c
    ecjpake.c
    ecp_curves_new.c
    lmots.c
    lms.c
    memory_buffer_alloc.c
    mps_trace.c
    net_sockets.c
    nist_kw.c
    pkcs12.c
    pkcs5.c
    pkcs7.c
    psa_crypto_ffdh.c
    psa_crypto_pake.c
    psa_crypto_se.c
    ripemd160.c
    ssl_cache.c
    ssl_cookie.c
    ssl_ticket.c
    threading.c
    x509_create.c
    x509_crl.c
    x509_csr.c
    x509write.c
    x509write_crt.c
    x509write_csr.c
)

if(APPLE AND CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    list(APPEND TLS_DISABLED_MBEDTLS_SOURCES aesce.c)
    list(APPEND TLS_DISABLED_MBEDTLS_SOURCES padlock.c)
endif()

foreach(source IN LISTS TLS_DISABLED_MBEDTLS_SOURCES)
    list(REMOVE_ITEM MBEDTLS_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/mbedtls/library/${source}"
    )
endforeach()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(
        ${MBEDTLS_SOURCES}
        PROPERTIES COMPILE_OPTIONS "-w"
    )
endif()

if(ANDROID)
    set_source_files_properties(
        lib/mbedtls/library/ctr_drbg.c
        PROPERTIES COMPILE_OPTIONS "-w;-O2"
    )
endif()

set(TLS_SOURCES
    src/libtls.c
    ${MBEDTLS_SOURCES}
)

set_source_files_properties(src/libtls.c PROPERTIES
    COMPILE_DEFINITIONS KC_TLS_BUILD_VERSION=${TLS_BUILD_VERSION}
)

if(NOT TLS_TEST_EXTERNAL_ARTIFACTS)
    add_library(tls_static STATIC ${TLS_SOURCES})
    set_target_properties(tls_static PROPERTIES OUTPUT_NAME "tls")
    target_include_directories(tls_static PRIVATE
        src
        lib/mbedtls
        lib/mbedtls/include
        lib/mbedtls/library
    )
    target_compile_definitions(tls_static PRIVATE
        MBEDTLS_USER_CONFIG_FILE="config.h"
    )

    add_library(tls_shared SHARED ${TLS_SOURCES})
    set_target_properties(tls_shared PROPERTIES OUTPUT_NAME "tls")
    set_target_properties(tls_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_include_directories(tls_shared PRIVATE
        src
        lib/mbedtls
        lib/mbedtls/include
        lib/mbedtls/library
    )
    target_compile_definitions(tls_shared PRIVATE
        MBEDTLS_USER_CONFIG_FILE="config.h"
    )

    if(WIN32)
        target_link_libraries(tls_static ws2_32 bcrypt crypt32)
        target_link_libraries(tls_shared ws2_32 bcrypt crypt32)
    else()
        target_link_libraries(tls_static Threads::Threads)
        target_link_libraries(tls_shared Threads::Threads)
        if(APPLE)
            target_link_libraries(tls_static
                "-framework Security"
                "-framework CoreFoundation"
            )
            target_link_libraries(tls_shared
                "-framework Security"
                "-framework CoreFoundation"
            )
        endif()
    endif()
endif()

if(TLS_BUILD_TESTS)
    enable_testing()

    if(TLS_TEST_SHARED_LIBRARY)
        add_library(tls_test_shared SHARED IMPORTED)
        set_target_properties(tls_test_shared PROPERTIES
            IMPORTED_LOCATION "${TLS_TEST_SHARED_LIBRARY}"
        )
        if(TLS_TEST_IMPORT_LIBRARY)
            set_target_properties(tls_test_shared PROPERTIES
                IMPORTED_IMPLIB "${TLS_TEST_IMPORT_LIBRARY}"
            )
        endif()
        set(TLS_TEST_SHARED_TARGET tls_test_shared)
    else()
        set(TLS_TEST_SHARED_TARGET tls_shared)
    endif()

    add_executable(tls_contract_test src/test.c)
    target_include_directories(tls_contract_test PRIVATE src)
    target_link_libraries(tls_contract_test PRIVATE
        ${TLS_TEST_SHARED_TARGET}
    )
    if(TLS_CLI_PATH)
        target_compile_definitions(tls_contract_test PRIVATE
            KC_CLI_PATH="${TLS_CLI_PATH}"
        )
    endif()
    add_custom_command(TARGET tls_contract_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${TLS_TEST_SHARED_TARGET}>
            $<TARGET_FILE_DIR:tls_contract_test>
    )
endif()
# Build the command-line frontend against the shared library.
if(TARGET tls_static AND NOT EMSCRIPTEN AND
    NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(tls_cli src/tls.c)
    set_target_properties(tls_cli PROPERTIES
        OUTPUT_NAME "tls"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}"
    )
    target_include_directories(tls_cli PRIVATE src)
    target_link_libraries(tls_cli PRIVATE tls_static
    if(APPLE)
        set_target_properties(tls_cli PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "@loader_path"
        )
    elseif(UNIX)
        set_target_properties(tls_cli PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "$ORIGIN"
        )
    endif()
endif()
