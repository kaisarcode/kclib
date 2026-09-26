cmake_minimum_required(VERSION 3.14)
project(trust C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED TRUST_BUILD_VERSION)
    set(TRUST_BUILD_VERSION 0)
endif()

option(TRUST_BUILD_TESTS "Build trust tests" OFF)

set(TRUST_TEST_EXTERNAL_ARTIFACTS OFF)
if(TRUST_BUILD_TESTS AND TRUST_TEST_SHARED_LIBRARY)
    set(TRUST_TEST_EXTERNAL_ARTIFACTS ON)
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

set(TRUST_SOURCES
    src/libtrust.c
    lib/monocypher/monocypher.c
)

set_source_files_properties(src/libtrust.c PROPERTIES
    COMPILE_DEFINITIONS KC_TRUST_BUILD_VERSION=${TRUST_BUILD_VERSION}
)

set(TRUST_WASM_MEMORY_FLAGS
    -sALLOW_MEMORY_GROWTH=1
    -sMAXIMUM_MEMORY=536870912
)

if(EMSCRIPTEN)
    add_executable(trust_wasm ${TRUST_SOURCES})
    target_include_directories(trust_wasm PRIVATE src lib/monocypher)
    set_target_properties(trust_wasm PROPERTIES
        OUTPUT_NAME "trust"
        SUFFIX ".js"
    )
    target_link_options(trust_wasm PRIVATE
        --no-entry
        -sEXPORTED_FUNCTIONS=_kc_trust_init,_kc_trust_invite,_kc_trust_join,_kc_trust_confirm,_kc_trust_seal,_kc_trust_unseal,_kc_trust_revoke,_kc_trust_close,_kc_trust_free,_kc_trust_version
        ${TRUST_WASM_MEMORY_FLAGS}
    )
endif()

if(NOT TRUST_TEST_EXTERNAL_ARTIFACTS AND NOT EMSCRIPTEN)
    add_executable(trust
        src/trust.c
        src/libtrust.c
        lib/monocypher/monocypher.c
    )
    target_include_directories(trust PRIVATE src lib/monocypher)

    add_library(trust_static STATIC ${TRUST_SOURCES})
    set_target_properties(trust_static PROPERTIES OUTPUT_NAME "trust")
    target_include_directories(trust_static PRIVATE src lib/monocypher)

    add_library(trust_shared SHARED ${TRUST_SOURCES})
    set_target_properties(trust_shared PROPERTIES OUTPUT_NAME "trust")
    set_target_properties(trust_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_include_directories(trust_shared PRIVATE src lib/monocypher)
    if(WIN32)
        target_link_libraries(trust PRIVATE bcrypt)
        target_link_libraries(trust_static PRIVATE bcrypt)
        target_link_libraries(trust_shared PRIVATE bcrypt)
    endif()
endif()

if(TRUST_BUILD_TESTS)
    enable_testing()

    if(EMSCRIPTEN)
        add_executable(trust_contract_test src/test.c ${TRUST_SOURCES})
        target_include_directories(trust_contract_test PRIVATE src lib/monocypher)
        target_link_options(trust_contract_test PRIVATE
            -sINITIAL_MEMORY=134217728
            ${TRUST_WASM_MEMORY_FLAGS}
        )
    elseif(TRUST_TEST_SHARED_LIBRARY)
        add_library(trust_test_shared SHARED IMPORTED)
        set_target_properties(trust_test_shared PROPERTIES
            IMPORTED_LOCATION "${TRUST_TEST_SHARED_LIBRARY}"
        )
        if(TRUST_TEST_IMPORT_LIBRARY)
            set_target_properties(trust_test_shared PROPERTIES
                IMPORTED_IMPLIB "${TRUST_TEST_IMPORT_LIBRARY}"
            )
        endif()
    else()
        set(TRUST_TEST_SHARED_TARGET trust_shared)
    endif()

    if(NOT EMSCRIPTEN)
        if(TARGET trust_test_shared)
            set(TRUST_TEST_SHARED_TARGET trust_test_shared)
        endif()

        add_executable(trust_contract_test src/test.c)
        target_include_directories(trust_contract_test PRIVATE src lib/monocypher)
        target_link_libraries(trust_contract_test PRIVATE ${TRUST_TEST_SHARED_TARGET})
        if(DEFINED TRUST_TEST_CLI)
            target_compile_definitions(trust_contract_test PRIVATE
                TRUST_TEST_CLI="${TRUST_TEST_CLI}"
            )
        endif()

        add_custom_command(TARGET trust_contract_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${TRUST_TEST_SHARED_TARGET}>
                $<TARGET_FILE_DIR:trust_contract_test>
        )
    endif()

endif()
