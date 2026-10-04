# redp2p
# KaisarCode - CMake build configuration
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.14)
project(redp2p LANGUAGES C)
set(CMAKE_C_STANDARD 11)

if(NOT DEFINED REDP2P_BUILD_VERSION)
    set(REDP2P_BUILD_VERSION 0)
endif()

option(REDP2P_BUILD_TESTS "Build redp2p tests" OFF)

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

# Suppress warnings in vendored source files we don't control.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(
        lib/monocypher/monocypher.c
        lib/kcp/ikcp.c
        PROPERTIES COMPILE_OPTIONS "-w"
    )
endif()

# Shared library
set(REDP2P_ENGINE_SOURCES
    src/libredp2p-core.c
    src/libredp2p-peer.c
    src/libredp2p-idx.c
    src/libredp2p-pub.c
    src/libredp2p-con.c
    lib/monocypher/monocypher.c
    lib/kcp/ikcp.c
    lib/parson/parson.c
)

set(REDP2P_SOURCES
    src/libredp2p-api.c
    ${REDP2P_ENGINE_SOURCES}
)

set_source_files_properties(src/libredp2p-core.c PROPERTIES
    COMPILE_DEFINITIONS REDP2P_BUILD_VERSION=${REDP2P_BUILD_VERSION}
)

add_library(redp2p_shared SHARED ${REDP2P_SOURCES})
set_target_properties(redp2p_shared PROPERTIES
    OUTPUT_NAME "redp2p"
    C_VISIBILITY_PRESET hidden
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)
target_include_directories(redp2p_shared PRIVATE src lib/monocypher lib/kcp lib/parson)
if(NOT WIN32 AND NOT MSVC)
    set_source_files_properties(src/libredp2p-api.c PROPERTIES
        COMPILE_OPTIONS "-fvisibility=default"
    )
endif()

if(MINGW)
    set(REDP2P_PRIVATE_HEADERS src/libredp2p-core.h src/libredp2p-peer.h)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${REDP2P_PRIVATE_HEADERS})
    set(REDP2P_PRIVATE_SYMBOLS)
    foreach(header IN LISTS REDP2P_PRIVATE_HEADERS)
        file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/${header}" declarations
            REGEX "^REDP2P_INTERNAL .*[ (]redp2p_[a-z0-9_]+\\(")
        foreach(declaration IN LISTS declarations)
            string(REGEX REPLACE ".*[ *](redp2p_[a-z0-9_]+)\\(.*" "\\1"
                symbol "${declaration}")
            list(APPEND REDP2P_PRIVATE_SYMBOLS "${symbol}")
        endforeach()
    endforeach()
    list(JOIN REDP2P_PRIVATE_SYMBOLS "," REDP2P_PRIVATE_SYMBOLS)
    target_link_options(redp2p_shared PRIVATE
        "SHELL:-Xlinker --exclude-symbols -Xlinker ${REDP2P_PRIVATE_SYMBOLS}")
endif()

# Static library
add_library(redp2p_static STATIC ${REDP2P_SOURCES})
set_target_properties(redp2p_static PROPERTIES OUTPUT_NAME "redp2p")
target_include_directories(redp2p_static PRIVATE src lib/monocypher lib/kcp lib/parson)

if(UNIX AND NOT ANDROID AND NOT APPLE)
    target_link_libraries(redp2p_shared pthread m)
    target_link_libraries(redp2p_static pthread m)
elseif(MINGW)
    target_link_libraries(redp2p_shared ws2_32 bcrypt)
    target_link_libraries(redp2p_static ws2_32 bcrypt)
elseif(MSVC)
    target_link_libraries(redp2p_shared ws2_32 bcrypt)
    target_link_libraries(redp2p_static ws2_32 bcrypt)
endif()
if(REDP2P_BUILD_TESTS)
    enable_testing()

    add_executable(redp2p_contract_test
        tests/test.c
        ${REDP2P_SOURCES}
    )
    target_include_directories(redp2p_contract_test PRIVATE
        src lib/monocypher lib/kcp lib/parson
    )
    target_compile_definitions(redp2p_contract_test PRIVATE
        REDP2P_TESTING=1
    )

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(redp2p_contract_test PRIVATE pthread m)
    elseif(MINGW)
        target_link_libraries(redp2p_contract_test PRIVATE ws2_32 bcrypt)
    elseif(MSVC)
        target_link_libraries(redp2p_contract_test PRIVATE ws2_32 bcrypt)
    endif()
endif()
