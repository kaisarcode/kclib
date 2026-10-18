cmake_minimum_required(VERSION 3.14)
project(pow C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED POW_BUILD_VERSION)
    set(POW_BUILD_VERSION 0)
endif()

option(POW_BUILD_TESTS "Build pow tests" OFF)
option(POW_ENABLE_HASHWX "Build the explicit HashWX backend" ON)

set(POW_TEST_EXTERNAL_ARTIFACTS OFF)
if(POW_BUILD_TESTS AND POW_TEST_SHARED_LIBRARY)
    set(POW_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        add_link_options(-fuse-ld=lld)
    endif()
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set_source_files_properties(src/libpow.c PROPERTIES
    COMPILE_DEFINITIONS KC_POW_BUILD_VERSION=${POW_BUILD_VERSION}
)

set(HASHWX_SOURCES
    lib/hashwx/src/compiler.c
    lib/hashwx/src/context.c
    lib/hashwx/src/hashwx.c
    lib/hashwx/src/program.c
    lib/hashwx/src/program_exec.c
    lib/hashwx/src/siphash_rng.c
    lib/hashwx/src/virtual_memory.c
)

if(EMSCRIPTEN)
    list(APPEND HASHWX_SOURCES lib/hashwx/src/compiler_wasm.c)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64" OR
       CMAKE_OSX_ARCHITECTURES MATCHES "arm64" OR
       CMAKE_C_COMPILER MATCHES "aarch64|oa64")
    list(APPEND HASHWX_SOURCES lib/hashwx/src/compiler_a64.c)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i686|i386" OR
       CMAKE_OSX_ARCHITECTURES MATCHES "x86_64" OR
       CMAKE_C_COMPILER MATCHES "x86_64|i686|o64" OR
       (NOT CMAKE_CROSSCOMPILING AND
        CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i686|i386"))
    list(APPEND HASHWX_SOURCES lib/hashwx/src/compiler_x86.c)
else()
    list(APPEND HASHWX_SOURCES lib/hashwx/src/compiler_x86.c)
endif()

set(POW_CORE_SOURCES
    src/libpow.c
    src/sha256.c
    src/hashwx.c
)

if(POW_ENABLE_HASHWX)
    list(APPEND POW_CORE_SOURCES ${HASHWX_SOURCES})
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(${HASHWX_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
endif()

set(POW_INCLUDE_DIRS
    src
)

if(POW_ENABLE_HASHWX)
    list(APPEND POW_INCLUDE_DIRS
        lib/hashwx/include
        lib/hashwx/src
    )
endif()

if(EMSCRIPTEN)
    add_executable(libpow_wasm ${POW_CORE_SOURCES})
    target_include_directories(libpow_wasm PRIVATE ${POW_INCLUDE_DIRS})
    target_compile_definitions(libpow_wasm PRIVATE
        HASHWX_STATIC
        KC_POW_ENABLE_HASHWX=$<BOOL:${POW_ENABLE_HASHWX}>
    )
    set_target_properties(libpow_wasm PROPERTIES
        OUTPUT_NAME "libpow"
        SUFFIX ".wasm"
    )
    target_link_options(libpow_wasm PRIVATE
        --no-entry
        -sSTANDALONE_WASM=1
        -sEXPORTED_FUNCTIONS=_kc_pow_challenge,_kc_pow_solve,_kc_pow_verify,_kc_pow_size,_kc_pow_free,_kc_pow_version
    )
elseif(NOT POW_TEST_EXTERNAL_ARTIFACTS)
    add_library(pow_static STATIC ${POW_CORE_SOURCES})
    set_target_properties(pow_static PROPERTIES
        OUTPUT_NAME "pow"
        POSITION_INDEPENDENT_CODE ON
    )
    target_include_directories(pow_static PRIVATE ${POW_INCLUDE_DIRS})
    target_compile_definitions(pow_static PRIVATE
        HASHWX_STATIC
        KC_POW_ENABLE_HASHWX=$<BOOL:${POW_ENABLE_HASHWX}>
    )

    add_library(pow_shared SHARED ${POW_CORE_SOURCES})
    set_target_properties(pow_shared PROPERTIES
        OUTPUT_NAME "pow"
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
    target_include_directories(pow_shared PRIVATE ${POW_INCLUDE_DIRS})
    target_compile_definitions(pow_shared PRIVATE
        HASHWX_STATIC
        KC_POW_ENABLE_HASHWX=$<BOOL:${POW_ENABLE_HASHWX}>
    )

    if(WIN32)
        target_link_libraries(pow_static PRIVATE bcrypt)
        target_link_libraries(pow_shared PRIVATE bcrypt)
    endif()
endif()

if(POW_BUILD_TESTS)
    enable_testing()

    if(EMSCRIPTEN)
        add_executable(pow_contract_test
            src/test.c
            ${POW_CORE_SOURCES}
        )
        target_include_directories(pow_contract_test PRIVATE ${POW_INCLUDE_DIRS})
        target_compile_definitions(pow_contract_test PRIVATE
            KC_POW_ENABLE_HASHWX=$<BOOL:${POW_ENABLE_HASHWX}>
        )
    else()
        if(POW_TEST_SHARED_LIBRARY)
            add_library(pow_test_shared SHARED IMPORTED)
            set_target_properties(pow_test_shared PROPERTIES
                IMPORTED_LOCATION "${POW_TEST_SHARED_LIBRARY}"
            )
            if(POW_TEST_IMPORT_LIBRARY)
                set_target_properties(pow_test_shared PROPERTIES
                    IMPORTED_IMPLIB "${POW_TEST_IMPORT_LIBRARY}"
                )
            endif()
            set(POW_TEST_SHARED_TARGET pow_test_shared)
        else()
            set(POW_TEST_SHARED_TARGET pow_shared)
        endif()

        add_executable(pow_contract_test src/test.c)
        target_include_directories(pow_contract_test PRIVATE src)
        target_link_libraries(pow_contract_test PRIVATE ${POW_TEST_SHARED_TARGET})

        add_custom_command(TARGET pow_contract_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${POW_TEST_SHARED_TARGET}>
                $<TARGET_FILE_DIR:pow_contract_test>
        )
    endif()
endif()
# Build the command-line frontend against the shared library.
if(TARGET pow_shared AND NOT EMSCRIPTEN AND
    NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(pow_cli src/pow.c)
    set_target_properties(pow_cli PROPERTIES
        OUTPUT_NAME "pow"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}"
    )
    target_include_directories(pow_cli PRIVATE src)
    target_link_libraries(pow_cli PRIVATE pow_shared)
    if(APPLE)
        set_target_properties(pow_cli PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "@loader_path"
        )
    elseif(UNIX)
        set_target_properties(pow_cli PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "$ORIGIN"
        )
    endif()
endif()
