cmake_minimum_required(VERSION 3.14)
project(menu C)

set(CMAKE_C_STANDARD 11)

if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "menu supports Linux and Windows only")
endif()

if(NOT DEFINED MENU_BUILD_VERSION)
    set(MENU_BUILD_VERSION 0)
endif()

option(MENU_NATIVE "Enable native hardware optimizations" OFF)
option(MENU_BUILD_TESTS "Build menu tests" OFF)

set(MENU_TEST_EXTERNAL_ARTIFACTS OFF)
if(MENU_BUILD_TESTS AND MENU_TEST_SHARED_LIBRARY)
    set(MENU_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(MENU_NATIVE)
        add_compile_options(-march=native)
    endif()
    if(NOT WIN32)
        add_definitions(-D_GNU_SOURCE)
    endif()
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set_source_files_properties(src/libmenu.c PROPERTIES
    COMPILE_DEFINITIONS KC_MENU_BUILD_VERSION=${MENU_BUILD_VERSION}
)

if(NOT MENU_TEST_EXTERNAL_ARTIFACTS)
    add_library(menu_static STATIC src/libmenu.c)
    set_target_properties(menu_static PROPERTIES OUTPUT_NAME "menu")
    target_include_directories(menu_static PRIVATE src)

    add_library(menu_shared SHARED src/libmenu.c)
    set_target_properties(menu_shared PROPERTIES OUTPUT_NAME "menu")
    set_target_properties(menu_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_include_directories(menu_shared PRIVATE src)

    if(WIN32)
        target_link_libraries(menu_static PRIVATE ole32 shell32 uuid)
        target_link_libraries(menu_shared PRIVATE ole32 shell32 uuid)
    endif()
endif()

if(MENU_BUILD_TESTS)
    enable_testing()

    if(MENU_TEST_SHARED_LIBRARY)
        add_library(menu_test_shared SHARED IMPORTED)
        set_target_properties(menu_test_shared PROPERTIES
            IMPORTED_LOCATION "${MENU_TEST_SHARED_LIBRARY}"
        )
        if(MENU_TEST_IMPORT_LIBRARY)
            set_target_properties(menu_test_shared PROPERTIES
                IMPORTED_IMPLIB "${MENU_TEST_IMPORT_LIBRARY}"
            )
        endif()
    else()
        set(MENU_TEST_SHARED_TARGET menu_shared)
    endif()

    if(TARGET menu_test_shared)
        set(MENU_TEST_SHARED_TARGET menu_test_shared)
    endif()

    add_executable(menu_contract_test src/test.c)
    target_include_directories(menu_contract_test PRIVATE src)
    target_link_libraries(menu_contract_test PRIVATE ${MENU_TEST_SHARED_TARGET})

    if(WIN32)
        target_link_libraries(menu_contract_test PRIVATE ole32 shell32 uuid)
    endif()

    add_custom_command(TARGET menu_contract_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${MENU_TEST_SHARED_TARGET}>
            $<TARGET_FILE_DIR:menu_contract_test>
    )
endif()
