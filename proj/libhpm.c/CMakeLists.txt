cmake_minimum_required(VERSION 3.14)
project(hpm C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED HPM_BUILD_VERSION)
    set(HPM_BUILD_VERSION 0)
endif()

option(HPM_BUILD_TESTS "Build hpm tests" OFF)

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(APPLE)
        add_definitions(-D_DARWIN_C_SOURCE -D_GNU_SOURCE)
    else()
        add_definitions(-D_GNU_SOURCE)
    endif()
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set_source_files_properties(src/libhpm.c PROPERTIES
    COMPILE_DEFINITIONS KC_HPM_BUILD_VERSION=${HPM_BUILD_VERSION}
)

add_library(hpm_static STATIC src/libhpm.c)
set_target_properties(hpm_static PROPERTIES OUTPUT_NAME "hpm")
target_include_directories(hpm_static PUBLIC src)

add_library(hpm_shared SHARED src/libhpm.c)
set_target_properties(hpm_shared PROPERTIES OUTPUT_NAME "hpm")
set_target_properties(hpm_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_include_directories(hpm_shared PUBLIC src)

if(WIN32)
    target_link_libraries(hpm_static PUBLIC ws2_32)
    target_link_libraries(hpm_shared PUBLIC ws2_32)
endif()

if(HPM_BUILD_TESTS)
    if(NOT WIN32)
        find_package(Threads REQUIRED)
    endif()

    add_executable(hpm_contract_test src/test.c)
    target_include_directories(hpm_contract_test PRIVATE src)
    target_link_libraries(hpm_contract_test PRIVATE hpm_shared)
    if(WIN32)
        target_link_libraries(hpm_contract_test PRIVATE ws2_32)
    else()
        target_link_libraries(hpm_contract_test PRIVATE Threads::Threads)
    endif()
endif()
