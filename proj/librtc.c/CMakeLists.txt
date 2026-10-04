cmake_minimum_required(VERSION 3.14)
project(rtc C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if(NOT DEFINED RTC_BUILD_VERSION)
    set(RTC_BUILD_VERSION 0)
endif()

option(RTC_BUILD_TESTS "Build rtc tests" OFF)

if(NOT LIBDATACHANNEL_DIR)
    message(FATAL_ERROR
        "LIBDATACHANNEL_DIR is required and must point to libdatachannel sources")
endif()

if(NOT EXISTS "${LIBDATACHANNEL_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "Invalid LIBDATACHANNEL_DIR: ${LIBDATACHANNEL_DIR}")
endif()

foreach(RTC_LIBDATACHANNEL_DEP plog usrsctp libjuice)
    if(NOT EXISTS
        "${LIBDATACHANNEL_DIR}/deps/${RTC_LIBDATACHANNEL_DEP}/CMakeLists.txt")
        message(FATAL_ERROR
            "libdatachannel submodules are not initialized. Run: "
            "cd ${LIBDATACHANNEL_DIR} && git submodule update --init --recursive")
    endif()
endforeach()

if(NOT MBEDTLS_DIR)
    message(FATAL_ERROR
        "MBEDTLS_DIR is required and must point to Mbed TLS sources")
endif()

if(NOT EXISTS "${MBEDTLS_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "Invalid MBEDTLS_DIR: ${MBEDTLS_DIR}")
endif()

if(EXISTS "${MBEDTLS_DIR}/tf-psa-crypto/CMakeLists.txt" AND
    NOT EXISTS "${MBEDTLS_DIR}/tf-psa-crypto/framework/CMakeLists.txt")
    message(FATAL_ERROR
        "Mbed TLS nested submodules are not initialized. Run: "
        "cd ${MBEDTLS_DIR} && git submodule update --init --recursive")
endif()

set(RTC_TEST_EXTERNAL_ARTIFACTS OFF)
if(RTC_BUILD_TESTS AND RTC_TEST_SHARED_LIBRARY)
    set(RTC_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set_source_files_properties(src/librtc.c PROPERTIES
    COMPILE_DEFINITIONS KC_RTC_BUILD_VERSION=${RTC_BUILD_VERSION}
)

set(RTC_CORE_SOURCES
    src/librtc.c
)

if(NOT RTC_TEST_EXTERNAL_ARTIFACTS)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" -c "import jsonschema"
        RESULT_VARIABLE RTC_MBEDTLS_PYTHON_RESULT
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(NOT RTC_MBEDTLS_PYTHON_RESULT EQUAL 0)
        message(FATAL_ERROR
            "Mbed TLS Python build requirements are missing. Run: "
            "${Python3_EXECUTABLE} -m pip install --user -r "
            "${MBEDTLS_DIR}/scripts/basic.requirements.txt")
    endif()

    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
    set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)

    add_subdirectory(
        "${MBEDTLS_DIR}"
        "${CMAKE_BINARY_DIR}/mbedtls"
        EXCLUDE_FROM_ALL
    )

    if(NOT TARGET MbedTLS::MbedTLS)
        if(TARGET MbedTLS::mbedtls)
            set(RTC_MBEDTLS_TLS_TARGET MbedTLS::mbedtls)
        elseif(TARGET mbedtls)
            set(RTC_MBEDTLS_TLS_TARGET mbedtls)
        else()
            message(FATAL_ERROR
                "Mbed TLS source tree does not expose a TLS target")
        endif()

        if(TARGET MbedTLS::mbedx509)
            set(RTC_MBEDTLS_X509_TARGET MbedTLS::mbedx509)
        elseif(TARGET mbedx509)
            set(RTC_MBEDTLS_X509_TARGET mbedx509)
        else()
            message(FATAL_ERROR
                "Mbed TLS source tree does not expose an X.509 target")
        endif()

        if(TARGET MbedTLS::tfpsacrypto)
            set(RTC_MBEDTLS_CRYPTO_TARGET MbedTLS::tfpsacrypto)
        elseif(TARGET tfpsacrypto)
            set(RTC_MBEDTLS_CRYPTO_TARGET tfpsacrypto)
        elseif(TARGET MbedTLS::mbedcrypto)
            set(RTC_MBEDTLS_CRYPTO_TARGET MbedTLS::mbedcrypto)
        elseif(TARGET mbedcrypto)
            set(RTC_MBEDTLS_CRYPTO_TARGET mbedcrypto)
        else()
            message(FATAL_ERROR
                "Mbed TLS source tree does not expose a crypto target")
        endif()

        add_library(MbedTLS::MbedTLS INTERFACE IMPORTED GLOBAL)
        set_property(TARGET MbedTLS::MbedTLS PROPERTY
            INTERFACE_LINK_LIBRARIES
            "${RTC_MBEDTLS_TLS_TARGET};${RTC_MBEDTLS_X509_TARGET};${RTC_MBEDTLS_CRYPTO_TARGET}"
        )
    endif()

    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_DEPS_LIBS OFF CACHE BOOL "" FORCE)
    set(NO_MEDIA ON CACHE BOOL "" FORCE)
    set(NO_WEBSOCKET ON CACHE BOOL "" FORCE)
    set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
    set(NO_TESTS ON CACHE BOOL "" FORCE)
    set(PREFER_SYSTEM_LIB OFF CACHE BOOL "" FORCE)
    set(USE_GNUTLS OFF CACHE BOOL "" FORCE)
    set(USE_MBEDTLS ON CACHE BOOL "" FORCE)
    set(USE_NICE OFF CACHE BOOL "" FORCE)
    set(WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
    set(RTC_UPDATE_VERSION_HEADER OFF CACHE BOOL "" FORCE)

    add_subdirectory(
        "${LIBDATACHANNEL_DIR}"
        "${CMAKE_BINARY_DIR}/libdatachannel"
        EXCLUDE_FROM_ALL
    )

    add_library(rtc_static STATIC ${RTC_CORE_SOURCES})
    set_target_properties(rtc_static PROPERTIES OUTPUT_NAME "rtc")
    target_include_directories(rtc_static PRIVATE src)
    target_link_libraries(rtc_static PRIVATE
        LibDataChannel::LibDataChannelStatic)

    add_library(rtc_shared SHARED ${RTC_CORE_SOURCES})
    set_target_properties(rtc_shared PROPERTIES
        OUTPUT_NAME "rtc"
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
    target_include_directories(rtc_shared PRIVATE src)
    target_link_libraries(rtc_shared PRIVATE
        LibDataChannel::LibDataChannelStatic)
endif()

if(RTC_BUILD_TESTS)
    enable_testing()

    if(RTC_TEST_SHARED_LIBRARY)
        add_library(rtc_test_shared SHARED IMPORTED)
        set_target_properties(rtc_test_shared PROPERTIES
            IMPORTED_LOCATION "${RTC_TEST_SHARED_LIBRARY}"
        )
        if(RTC_TEST_IMPORT_LIBRARY)
            set_target_properties(rtc_test_shared PROPERTIES
                IMPORTED_IMPLIB "${RTC_TEST_IMPORT_LIBRARY}"
            )
        endif()
        set(RTC_TEST_SHARED_TARGET rtc_test_shared)
    else()
        set(RTC_TEST_SHARED_TARGET rtc_shared)
    endif()

    add_executable(rtc_contract_test src/test.c)
    target_include_directories(rtc_contract_test PRIVATE src)
    target_link_libraries(rtc_contract_test PRIVATE ${RTC_TEST_SHARED_TARGET})

    add_custom_command(TARGET rtc_contract_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${RTC_TEST_SHARED_TARGET}>
            $<TARGET_FILE_DIR:rtc_contract_test>
    )
endif()
