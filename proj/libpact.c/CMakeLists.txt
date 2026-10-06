cmake_minimum_required(VERSION 3.14)
project(pact C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED PACT_BUILD_VERSION)
    set(PACT_BUILD_VERSION 0)
endif()

option(PACT_BUILD_TESTS "Build pact tests" OFF)

set(PACT_TEST_EXTERNAL_ARTIFACTS OFF)
if(PACT_BUILD_TESTS AND PACT_TEST_SHARED_LIBRARY)
    set(PACT_TEST_EXTERNAL_ARTIFACTS ON)
endif()

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
        PROPERTIES COMPILE_OPTIONS "-w"
    )
endif()

set(PACT_SOURCES
    src/libpact.c
    lib/monocypher/monocypher.c
)

set_source_files_properties(src/libpact.c PROPERTIES
    COMPILE_DEFINITIONS KC_PACT_BUILD_VERSION=${PACT_BUILD_VERSION}
)

set(PACT_WASM_MEMORY_FLAGS
    -sALLOW_MEMORY_GROWTH=1
    -sMAXIMUM_MEMORY=536870912
)

if(EMSCRIPTEN)
    add_executable(pact_wasm ${PACT_SOURCES})
    target_include_directories(pact_wasm PRIVATE src lib/monocypher)
    set_target_properties(pact_wasm PROPERTIES
        OUTPUT_NAME "libpact"
        SUFFIX ".js"
    )
    target_link_options(pact_wasm PRIVATE
        --no-entry
        -sEXPORTED_FUNCTIONS=_kc_pact_init,_kc_pact_invite,_kc_pact_join,_kc_pact_confirm,_kc_pact_seal,_kc_pact_unseal,_kc_pact_revoke,_kc_pact_close,_kc_pact_free,_kc_pact_version
        ${PACT_WASM_MEMORY_FLAGS}
    )
endif()

if(NOT PACT_TEST_EXTERNAL_ARTIFACTS AND NOT EMSCRIPTEN)
    add_library(pact_static STATIC ${PACT_SOURCES})
    set_target_properties(pact_static PROPERTIES OUTPUT_NAME "pact")
    target_include_directories(pact_static PRIVATE src lib/monocypher)

    add_library(pact_shared SHARED ${PACT_SOURCES})
    set_target_properties(pact_shared PROPERTIES OUTPUT_NAME "pact")
    set_target_properties(pact_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_include_directories(pact_shared PRIVATE src lib/monocypher)
    if(WIN32)
        target_link_libraries(pact_static PRIVATE bcrypt)
        target_link_libraries(pact_shared PRIVATE bcrypt)
    endif()
endif()

if(PACT_BUILD_TESTS)
    enable_testing()

    if(EMSCRIPTEN)
        add_executable(pact_contract_test src/test.c ${PACT_SOURCES})
        target_include_directories(pact_contract_test PRIVATE src lib/monocypher)
        target_link_options(pact_contract_test PRIVATE
            -sINITIAL_MEMORY=134217728
            ${PACT_WASM_MEMORY_FLAGS}
        )
    elseif(PACT_TEST_SHARED_LIBRARY)
        add_library(pact_test_shared SHARED IMPORTED)
        set_target_properties(pact_test_shared PROPERTIES
            IMPORTED_LOCATION "${PACT_TEST_SHARED_LIBRARY}"
        )
        if(PACT_TEST_IMPORT_LIBRARY)
            set_target_properties(pact_test_shared PROPERTIES
                IMPORTED_IMPLIB "${PACT_TEST_IMPORT_LIBRARY}"
            )
        endif()
    else()
        set(PACT_TEST_SHARED_TARGET pact_shared)
    endif()

    if(NOT EMSCRIPTEN)
        if(TARGET pact_test_shared)
            set(PACT_TEST_SHARED_TARGET pact_test_shared)
        endif()

        add_executable(pact_contract_test src/test.c)
        target_include_directories(pact_contract_test PRIVATE src lib/monocypher)
        target_link_libraries(pact_contract_test PRIVATE ${PACT_TEST_SHARED_TARGET})
        add_custom_command(TARGET pact_contract_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${PACT_TEST_SHARED_TARGET}>
                $<TARGET_FILE_DIR:pact_contract_test>
        )
    endif()

endif()
