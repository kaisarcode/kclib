cmake_minimum_required(VERSION 3.14)
project(grd C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED GRD_BUILD_VERSION)
    set(GRD_BUILD_VERSION 0)
endif()

option(GRD_NATIVE "Enable native CPU optimizations" OFF)
option(GRD_BUILD_TESTS "Build grd tests" OFF)

set(GRD_TEST_EXTERNAL_ARTIFACTS OFF)
if(GRD_BUILD_TESTS AND GRD_TEST_SHARED_LIBRARY)
    set(GRD_TEST_EXTERNAL_ARTIFACTS ON)
endif()

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
set(GRD_HEADERS src/grd.h)

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
        -sEXPORTED_FUNCTIONS=_kc_grd_options_default,_kc_grd_options_load_env,_kc_grd_options_free,_kc_grd_stop,_kc_grd_version,_kc_grd_box_new,_kc_grd_box_free,_kc_grd_split_set,_kc_grd_split_add,_kc_grd_split_weight,_kc_grd_box_bounds,_kc_grd_box_layout,_kc_grd_split_gap,_kc_grd_gap_hit,_kc_grd_drag_begin,_kc_grd_drag_update,_kc_grd_drag_end,_kc_grd_box_close,_kc_grd_split_at
    )
endif()

if(NOT GRD_TEST_EXTERNAL_ARTIFACTS AND NOT EMSCRIPTEN)
    add_library(grd_shared SHARED ${GRD_SOURCES})
    set_target_properties(grd_shared PROPERTIES OUTPUT_NAME grd PREFIX "lib")
    target_include_directories(grd_shared PRIVATE src)

    add_library(grd_static STATIC ${GRD_SOURCES})
    set_target_properties(grd_static PROPERTIES OUTPUT_NAME grd PREFIX "lib")
    target_include_directories(grd_static PRIVATE src)

    add_executable(grd_exe src/grd.c ${GRD_SOURCES})
    set_target_properties(grd_exe PROPERTIES OUTPUT_NAME grd)
    target_include_directories(grd_exe PRIVATE src)

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(grd_shared m)
        target_link_libraries(grd_static m)
        target_link_libraries(grd_exe m)
    elseif(ANDROID)
        target_link_libraries(grd_shared m)
        target_link_libraries(grd_static m)
        target_link_libraries(grd_exe m)
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

        if(UNIX AND NOT ANDROID AND NOT APPLE)
            target_link_libraries(grd_contract_test PRIVATE m)
        endif()

        add_custom_command(TARGET grd_contract_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${GRD_TEST_SHARED_TARGET}>
                $<TARGET_FILE_DIR:grd_contract_test>
        )
    endif()

endif()
