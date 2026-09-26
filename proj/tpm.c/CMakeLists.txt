cmake_minimum_required(VERSION 3.14)
project(tpm C)

set(CMAKE_C_STANDARD 11)
if(NOT DEFINED KC_TPM_BUILD_VERSION)
    set(KC_TPM_BUILD_VERSION 0)
endif()

option(TPM_NATIVE "Enable native CPU optimizations" OFF)
option(TPM_BUILD_TESTS "Build tpm tests" OFF)

add_definitions(-DKC_TPM_BUILD_VERSION=${KC_TPM_BUILD_VERSION})

set(TPM_TEST_EXTERNAL_ARTIFACTS OFF)
if(TPM_BUILD_TESTS AND TPM_TEST_SHARED_LIBRARY)
    set(TPM_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    if(TPM_NATIVE)
        add_compile_options(-march=native)
    endif()
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set(TPM_SOURCES src/libtpm.c)
set(TPM_HEADERS src/libtpm.h)

set_source_files_properties(src/libtpm.c PROPERTIES
    COMPILE_DEFINITIONS KC_TPM_BUILD_VERSION=${KC_TPM_BUILD_VERSION}
)

if(EMSCRIPTEN)
    add_executable(tpm_wasm ${TPM_SOURCES})
    target_include_directories(tpm_wasm PRIVATE src)
    set_target_properties(tpm_wasm PROPERTIES
        OUTPUT_NAME "tpm"
        SUFFIX ".wasm"
    )
    target_link_options(tpm_wasm PRIVATE
        --no-entry
        -sSTANDALONE_WASM=1
        -sSTACK_SIZE=1048576
        -sEXPORTED_FUNCTIONS=_kc_tpm_open,_kc_tpm_score,_kc_tpm_close,_kc_tpm_version
    )
endif()

if(NOT TPM_TEST_EXTERNAL_ARTIFACTS AND NOT EMSCRIPTEN)
    add_library(tpm_shared SHARED ${TPM_SOURCES})
    set_target_properties(tpm_shared PROPERTIES OUTPUT_NAME tpm PREFIX "lib")
    set_target_properties(tpm_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_include_directories(tpm_shared PRIVATE src)

    add_library(tpm_static STATIC ${TPM_SOURCES})
    set_target_properties(tpm_static PROPERTIES
        OUTPUT_NAME tpm
        PREFIX "lib"
        POSITION_INDEPENDENT_CODE ON
    )
    target_include_directories(tpm_static PRIVATE src)

    add_executable(tpm_exe src/tpm.c ${TPM_SOURCES})
    set_target_properties(tpm_exe PROPERTIES OUTPUT_NAME tpm)
    target_include_directories(tpm_exe PRIVATE src)

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(tpm_shared m)
        target_link_libraries(tpm_static m)
        target_link_libraries(tpm_exe m)
    elseif(ANDROID)
        target_link_libraries(tpm_shared m)
        target_link_libraries(tpm_static m)
        target_link_libraries(tpm_exe m)
    endif()
endif()

if(TPM_BUILD_TESTS)
    enable_testing()

    if(EMSCRIPTEN)
        add_executable(tpm_contract_test src/test.c ${TPM_SOURCES})
        target_include_directories(tpm_contract_test PRIVATE src)
        target_link_options(tpm_contract_test PRIVATE -sSTACK_SIZE=1048576)
    else()
        if(TPM_TEST_SHARED_LIBRARY)
            add_library(tpm_test_shared SHARED IMPORTED)
            set_target_properties(tpm_test_shared PROPERTIES
                IMPORTED_LOCATION "${TPM_TEST_SHARED_LIBRARY}"
            )
            if(TPM_TEST_IMPORT_LIBRARY)
                set_target_properties(tpm_test_shared PROPERTIES
                    IMPORTED_IMPLIB "${TPM_TEST_IMPORT_LIBRARY}"
                )
            endif()
        else()
            set(TPM_TEST_SHARED_TARGET tpm_shared)
        endif()

        if(TARGET tpm_test_shared)
            set(TPM_TEST_SHARED_TARGET tpm_test_shared)
        endif()

        add_executable(tpm_contract_test src/test.c)
        target_include_directories(tpm_contract_test PRIVATE src)
        target_link_libraries(tpm_contract_test PRIVATE ${TPM_TEST_SHARED_TARGET})
        if(TPM_TEST_CLI)
            target_compile_definitions(tpm_contract_test PRIVATE
                KC_TPM_TEST_CLI="${TPM_TEST_CLI}"
            )
        endif()

        if(UNIX AND NOT ANDROID AND NOT APPLE)
            target_link_libraries(tpm_contract_test PRIVATE m)
        elseif(ANDROID)
            target_link_libraries(tpm_contract_test PRIVATE m)
        endif()

        add_custom_command(TARGET tpm_contract_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${TPM_TEST_SHARED_TARGET}>
                $<TARGET_FILE_DIR:tpm_contract_test>
        )
    endif()

endif()
