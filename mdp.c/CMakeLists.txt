cmake_minimum_required(VERSION 3.14)
project(mdp C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED MDP_BUILD_VERSION)
    set(MDP_BUILD_VERSION 0)
endif()

option(MDP_NATIVE "Enable native hardware optimizations" OFF)
option(MDP_BUILD_TESTS "Build mdp tests" OFF)

set(MDP_TEST_EXTERNAL_ARTIFACTS OFF)
if(MDP_BUILD_TESTS AND MDP_TEST_SHARED_LIBRARY)
    set(MDP_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    if(MDP_NATIVE)
        add_compile_options(-march=native)
    endif()
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set(MDP_SOURCES src/libmdp.c)

set_source_files_properties(src/libmdp.c PROPERTIES
    COMPILE_DEFINITIONS KC_MDP_BUILD_VERSION=${MDP_BUILD_VERSION}
)

if(EMSCRIPTEN)
    add_executable(mdp_wasm ${MDP_SOURCES})
    target_include_directories(mdp_wasm PRIVATE src)
    set_target_properties(mdp_wasm PROPERTIES
        OUTPUT_NAME "mdp"
        SUFFIX ".wasm"
    )
    target_link_options(mdp_wasm PRIVATE
        --no-entry
        -sSTANDALONE_WASM=1
        -sEXPORTED_FUNCTIONS=_kc_mdp_open,_kc_mdp_close,_kc_mdp_set_mode,_kc_mdp_mode,_kc_mdp_exec,_kc_mdp_free,_kc_mdp_version
    )
endif()

if(NOT MDP_TEST_EXTERNAL_ARTIFACTS AND NOT EMSCRIPTEN)
    add_executable(mdp src/mdp.c ${MDP_SOURCES})
    target_include_directories(mdp PRIVATE src)

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(mdp pthread m)
    endif()

    add_library(mdp_static STATIC ${MDP_SOURCES})
    set_target_properties(mdp_static PROPERTIES OUTPUT_NAME "mdp")
    target_include_directories(mdp_static PRIVATE src)

    add_library(mdp_shared SHARED ${MDP_SOURCES})
    set_target_properties(mdp_shared PROPERTIES OUTPUT_NAME "mdp")
    set_target_properties(mdp_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_include_directories(mdp_shared PRIVATE src)

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(mdp_shared pthread m)
    endif()
endif()

if(MDP_BUILD_TESTS)
    enable_testing()

    if(EMSCRIPTEN)
        add_executable(mdp_contract_test src/test.c ${MDP_SOURCES})
        target_include_directories(mdp_contract_test PRIVATE src)
    else()
        if(MDP_TEST_SHARED_LIBRARY)
            add_library(mdp_test_shared SHARED IMPORTED)
            set_target_properties(mdp_test_shared PROPERTIES
                IMPORTED_LOCATION "${MDP_TEST_SHARED_LIBRARY}"
            )
            if(MDP_TEST_IMPORT_LIBRARY)
                set_target_properties(mdp_test_shared PROPERTIES
                    IMPORTED_IMPLIB "${MDP_TEST_IMPORT_LIBRARY}"
                )
            endif()
            set(MDP_TEST_SHARED_TARGET mdp_test_shared)
        else()
            set(MDP_TEST_SHARED_TARGET mdp_shared)
        endif()

        add_executable(mdp_contract_test src/test.c)
        target_include_directories(mdp_contract_test PRIVATE src)
        target_link_libraries(mdp_contract_test PRIVATE ${MDP_TEST_SHARED_TARGET})

        if(UNIX AND NOT ANDROID AND NOT APPLE)
            target_link_libraries(mdp_contract_test PRIVATE m)
        endif()

        add_custom_command(TARGET mdp_contract_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${MDP_TEST_SHARED_TARGET}>
                $<TARGET_FILE_DIR:mdp_contract_test>
        )
    endif()
endif()