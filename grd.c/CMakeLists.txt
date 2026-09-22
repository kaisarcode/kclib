cmake_minimum_required(VERSION 3.14)
project(grd C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED GRD_BUILD_VERSION)
    set(GRD_BUILD_VERSION 0)
endif()

option(GRD_NATIVE "Enable native CPU optimizations" OFF)
option(GRD_BUILD_TESTS "Build grd tests" OFF)

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror -fno-finite-math-only)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    add_definitions(-D_GNU_SOURCE)
    if(GRD_NATIVE)
        add_compile_options(-march=native)
    endif()
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set(GRD_SOURCES src/libgrd.c)
set(GRD_HEADERS src/libgrd.h)

set_source_files_properties(src/libgrd.c PROPERTIES
    COMPILE_DEFINITIONS KC_GRD_BUILD_VERSION=${GRD_BUILD_VERSION}
)

if(EMSCRIPTEN)
    add_executable(grd_wasm ${GRD_SOURCES})
    target_include_directories(grd_wasm PRIVATE src)
    set_target_properties(grd_wasm PROPERTIES
        OUTPUT_NAME "grd"
        SUFFIX ".wasm"
    )
    target_link_options(grd_wasm PRIVATE
        --no-entry
        -sSTANDALONE_WASM=1
        -sEXPORTED_FUNCTIONS=_kc_grd_version,_kc_grd_grid_open,_kc_grd_grid_close,_kc_grd_grid_get_region,_kc_grd_region_get_direction,_kc_grd_region_set_direction,_kc_grd_region_get_box_count,_kc_grd_region_get_box,_kc_grd_region_get_separator_count,_kc_grd_region_get_separator,_kc_grd_region_insert_box,_kc_grd_box_get_weight,_kc_grd_box_set_weight,_kc_grd_box_get_region,_kc_grd_box_get_content,_kc_grd_box_subdivide,_kc_grd_box_remove,_kc_grd_separator_get_size,_kc_grd_separator_set_size,_kc_grd_separator_get_before,_kc_grd_separator_get_after,_kc_grd_content_get_box
    )
endif()

if(NOT EMSCRIPTEN)
    add_library(grd_shared SHARED ${GRD_SOURCES})
    set_target_properties(grd_shared PROPERTIES OUTPUT_NAME grd PREFIX "lib")
    target_include_directories(grd_shared PRIVATE src)

    add_library(grd_static STATIC ${GRD_SOURCES})
    set_target_properties(grd_static PROPERTIES OUTPUT_NAME grd PREFIX "lib")
    target_include_directories(grd_static PRIVATE src)

    if(UNIX)
        target_link_libraries(grd_shared m)
        target_link_libraries(grd_static m)
    endif()
endif()

if(GRD_BUILD_TESTS)
    enable_testing()

    if(EMSCRIPTEN)
        add_executable(grd_contract_test src/test.c ${GRD_SOURCES})
        target_include_directories(grd_contract_test PRIVATE src)
    elseif(GRD_TEST_SHARED_LIBRARY)
        add_library(grd_test_shared SHARED IMPORTED)
        set_target_properties(grd_test_shared PROPERTIES
            IMPORTED_LOCATION "${GRD_TEST_SHARED_LIBRARY}"
        )
        if(GRD_TEST_IMPORT_LIBRARY)
            set_target_properties(grd_test_shared PROPERTIES
                IMPORTED_IMPLIB "${GRD_TEST_IMPORT_LIBRARY}"
            )
        endif()
    else()
        set(GRD_TEST_SHARED_TARGET grd_shared)
    endif()

    if(NOT EMSCRIPTEN)
        if(TARGET grd_test_shared)
            set(GRD_TEST_SHARED_TARGET grd_test_shared)
        endif()

        add_executable(grd_contract_test src/test.c)
        target_include_directories(grd_contract_test PRIVATE src)
        target_link_libraries(grd_contract_test PRIVATE ${GRD_TEST_SHARED_TARGET})

        if(UNIX)
            target_link_libraries(grd_contract_test PRIVATE m)
        endif()

        add_custom_command(TARGET grd_contract_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${GRD_TEST_SHARED_TARGET}>
                $<TARGET_FILE_DIR:grd_contract_test>
        )
    endif()

endif()
