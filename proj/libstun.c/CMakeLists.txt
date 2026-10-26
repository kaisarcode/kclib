cmake_minimum_required(VERSION 3.14)
project(stun C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED STUN_BUILD_VERSION)
    set(STUN_BUILD_VERSION 0)
endif()

option(STUN_BUILD_TESTS "Build stun tests" OFF)

if(NOT WIN32)
    find_package(Threads REQUIRED)
endif()

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

set_source_files_properties(src/libstun.c PROPERTIES
    COMPILE_DEFINITIONS KC_STUN_BUILD_VERSION=${STUN_BUILD_VERSION}
)

set(STUN_TEST_EXTERNAL_ARTIFACTS OFF)
if(STUN_BUILD_TESTS AND STUN_TEST_SHARED_LIBRARY)
    set(STUN_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT STUN_TEST_EXTERNAL_ARTIFACTS)
    add_library(stun_static STATIC src/libstun.c src/stun-crypto.c)
    set_target_properties(stun_static PROPERTIES OUTPUT_NAME "stun")
    target_include_directories(stun_static PUBLIC src)

    add_library(stun_shared SHARED src/libstun.c src/stun-crypto.c)
    set_target_properties(stun_shared PROPERTIES OUTPUT_NAME "stun"
        WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_include_directories(stun_shared PUBLIC src)

    if(WIN32)
        target_link_libraries(stun_static PUBLIC ws2_32 bcrypt)
        target_link_libraries(stun_shared PUBLIC ws2_32 bcrypt)
    else()
        target_link_libraries(stun_static PUBLIC Threads::Threads)
        target_link_libraries(stun_shared PUBLIC Threads::Threads)
    endif()
endif()

if(STUN_BUILD_TESTS)
    enable_testing()
    if(STUN_TEST_SHARED_LIBRARY)
        add_library(stun_test_shared SHARED IMPORTED)
        set_target_properties(stun_test_shared PROPERTIES
            IMPORTED_LOCATION "${STUN_TEST_SHARED_LIBRARY}")
        if(STUN_TEST_IMPORT_LIBRARY)
            set_target_properties(stun_test_shared PROPERTIES
                IMPORTED_IMPLIB "${STUN_TEST_IMPORT_LIBRARY}")
        endif()
        set(STUN_TEST_SHARED_TARGET stun_test_shared)
    else()
        set(STUN_TEST_SHARED_TARGET stun_shared)
    endif()
    add_executable(stun_contract_test src/test.c)
    target_include_directories(stun_contract_test PRIVATE src)
    target_link_libraries(stun_contract_test PRIVATE ${STUN_TEST_SHARED_TARGET})
    if(STUN_CLI_PATH)
        target_compile_definitions(stun_contract_test PRIVATE
            KC_CLI_PATH="${STUN_CLI_PATH}"
        )
    endif()
    if(WIN32)
        target_link_libraries(stun_contract_test PRIVATE ws2_32)
    else()
        target_link_libraries(stun_contract_test PRIVATE Threads::Threads)
    endif()
    add_custom_command(TARGET stun_contract_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${STUN_TEST_SHARED_TARGET}>
            $<TARGET_FILE_DIR:stun_contract_test>)
endif()

if(TARGET stun_shared AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(stun_cli src/stun.c)
    set_target_properties(stun_cli PROPERTIES
        OUTPUT_NAME "stun"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}"
    )
    target_include_directories(stun_cli PRIVATE src)
    target_link_libraries(stun_cli PRIVATE stun_shared)
    if(APPLE)
        set_target_properties(stun_cli PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "@loader_path"
        )
    elseif(UNIX)
        set_target_properties(stun_cli PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "$ORIGIN"
        )
    endif()
endif()
