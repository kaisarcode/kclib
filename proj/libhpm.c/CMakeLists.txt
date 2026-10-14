cmake_minimum_required(VERSION 3.14)
project(hpm C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED HPM_BUILD_VERSION)
    set(HPM_BUILD_VERSION 0)
endif()

option(HPM_BUILD_TESTS "Build hpm tests" OFF)

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

set_source_files_properties(src/libhpm.c PROPERTIES
    COMPILE_DEFINITIONS KC_HPM_BUILD_VERSION=${HPM_BUILD_VERSION}
)

set(HPM_TEST_EXTERNAL_ARTIFACTS OFF)
if(HPM_BUILD_TESTS AND HPM_TEST_SHARED_LIBRARY)
    set(HPM_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT HPM_TEST_EXTERNAL_ARTIFACTS)
    add_library(hpm_static STATIC src/libhpm.c)
    set_target_properties(hpm_static PROPERTIES OUTPUT_NAME "hpm")
    target_include_directories(hpm_static PUBLIC src)

    add_library(hpm_shared SHARED src/libhpm.c)
    set_target_properties(hpm_shared PROPERTIES OUTPUT_NAME "hpm"
        WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_include_directories(hpm_shared PUBLIC src)

    if(WIN32)
        target_link_libraries(hpm_static PUBLIC ws2_32)
        target_link_libraries(hpm_shared PUBLIC ws2_32)
    endif()
endif()

if(HPM_BUILD_TESTS)
    enable_testing()
    if(NOT WIN32)
        find_package(Threads REQUIRED)
    endif()
    if(HPM_TEST_SHARED_LIBRARY)
        add_library(hpm_test_shared SHARED IMPORTED)
        set_target_properties(hpm_test_shared PROPERTIES
            IMPORTED_LOCATION "${HPM_TEST_SHARED_LIBRARY}")
        if(HPM_TEST_IMPORT_LIBRARY)
            set_target_properties(hpm_test_shared PROPERTIES
                IMPORTED_IMPLIB "${HPM_TEST_IMPORT_LIBRARY}")
        endif()
        set(HPM_TEST_SHARED_TARGET hpm_test_shared)
    else()
        set(HPM_TEST_SHARED_TARGET hpm_shared)
    endif()
    add_executable(hpm_contract_test src/test.c)
    add_executable(hpm_over_contract_test src/test_over.c)
    target_include_directories(hpm_contract_test PRIVATE src)
    target_include_directories(hpm_over_contract_test PRIVATE src)
    target_link_libraries(hpm_contract_test PRIVATE ${HPM_TEST_SHARED_TARGET})
    target_link_libraries(hpm_over_contract_test PRIVATE ${HPM_TEST_SHARED_TARGET})
    if(WIN32)
        target_link_libraries(hpm_contract_test PRIVATE ws2_32)
        target_link_libraries(hpm_over_contract_test PRIVATE ws2_32)
    else()
        target_link_libraries(hpm_contract_test PRIVATE Threads::Threads)
        target_link_libraries(hpm_over_contract_test PRIVATE Threads::Threads)
    endif()
    add_custom_command(TARGET hpm_contract_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${HPM_TEST_SHARED_TARGET}>
            $<TARGET_FILE_DIR:hpm_contract_test>)
    add_custom_command(TARGET hpm_over_contract_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${HPM_TEST_SHARED_TARGET}>
            $<TARGET_FILE_DIR:hpm_over_contract_test>)
endif()
