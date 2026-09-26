## CMakeLists.txt
## Summary: Build system for emb — embedding library with embedded GGUF model.
##
## Author:  KaisarCode
## Website: https://kaisarcode.com
## License: https://www.gnu.org/licenses/gpl-3.0.html

cmake_minimum_required(VERSION 3.14)
project(emb.c C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if(NOT DEFINED EMB_BUILD_VERSION)
    set(EMB_BUILD_VERSION 0)
endif()

option(EMB_BUILD_TESTS "Build emb tests" OFF)

set(EMB_TEST_EXTERNAL_ARTIFACTS OFF)
if(EMB_BUILD_TESTS AND EMB_TEST_SHARED_LIBRARY)
    set(EMB_TEST_EXTERNAL_ARTIFACTS ON)
endif()

## Compiler flags

if(NOT MSVC)
    add_compile_options(-O3 -fno-finite-math-only -Wall -Wextra -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    add_definitions(-D_GNU_SOURCE -DGGML_USE_CPU -DGGML_VERSION="1" -DGGML_COMMIT="custom")
else()
    add_compile_options(/O2 /arch:AVX2 /W4 /WX)
    add_definitions(-DGGML_USE_CPU -DGGML_VERSION="1" -DGGML_COMMIT="custom")
endif()

set_source_files_properties(src/libemb.c PROPERTIES
    COMPILE_DEFINITIONS KC_EMB_BUILD_VERSION=${EMB_BUILD_VERSION}
)

## GGML sources

include_directories(
    src
    lib/ggml/include
    lib/ggml/src
    lib/ggml/src/ggml-cpu
)

file(GLOB GGML_BASE_SOURCES
    "lib/ggml/src/*.c"
    "lib/ggml/src/*.cpp"
    "lib/ggml/src/ggml-cpu/*.c"
    "lib/ggml/src/ggml-cpu/*.cpp"
)

set(GGML_ARCH_SOURCES "")
if(EMSCRIPTEN)
    file(GLOB GGML_ARCH_SOURCES
        "lib/ggml/src/ggml-cpu/arch/wasm/*.c"
    )
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    file(GLOB GGML_ARCH_SOURCES
        "lib/ggml/src/ggml-cpu/arch/x86/*.c"
        "lib/ggml/src/ggml-cpu/arch/x86/*.cpp"
    )
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i686|i386")
    file(GLOB GGML_ARCH_SOURCES
        "lib/ggml/src/ggml-cpu/arch/x86/*.c"
        "lib/ggml/src/ggml-cpu/arch/x86/*.cpp"
    )
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|armv7|arm")
    file(GLOB GGML_ARCH_SOURCES
        "lib/ggml/src/ggml-cpu/arch/arm/*.c"
        "lib/ggml/src/ggml-cpu/arch/arm/*.cpp"
    )
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
    file(GLOB GGML_ARCH_SOURCES
        "lib/ggml/src/ggml-cpu/arch/riscv/*.c"
        "lib/ggml/src/ggml-cpu/arch/riscv/*.cpp"
    )
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "powerpc64le|ppc64le")
    file(GLOB GGML_ARCH_SOURCES
        "lib/ggml/src/ggml-cpu/arch/powerpc/*.c"
    )
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "mips")
    set(GGML_ARCH_SOURCES "")
    add_definitions(-DGGML_CPU_GENERIC)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "s390x")
    file(GLOB GGML_ARCH_SOURCES
        "lib/ggml/src/ggml-cpu/arch/s390/*.c"
        "lib/ggml/src/ggml-cpu/arch/s390/*.cpp"
    )
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "loongarch64")
    file(GLOB GGML_ARCH_SOURCES
        "lib/ggml/src/ggml-cpu/arch/loongarch/*.c"
    )
endif()

set(GGML_SOURCES ${GGML_BASE_SOURCES} ${GGML_ARCH_SOURCES})
set(GGML_C_SOURCES "")
set(GGML_CXX_SOURCES "")

foreach(GGML_SOURCE ${GGML_SOURCES})
    get_filename_component(GGML_SOURCE_EXT "${GGML_SOURCE}" EXT)
    if(GGML_SOURCE_EXT STREQUAL ".c")
        list(APPEND GGML_C_SOURCES "${GGML_SOURCE}")
    elseif(GGML_SOURCE_EXT STREQUAL ".cpp")
        list(APPEND GGML_CXX_SOURCES "${GGML_SOURCE}")
    endif()
endforeach()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(
        ${GGML_C_SOURCES}
        PROPERTIES COMPILE_OPTIONS "-w;-Wno-psabi"
    )
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(
        ${GGML_CXX_SOURCES}
        PROPERTIES COMPILE_OPTIONS "-w;-Wno-psabi"
    )
endif()

## Embed model

set(MODEL_GGUF "${CMAKE_SOURCE_DIR}/lib/model.gguf")
set(MODEL_S    "${CMAKE_BINARY_DIR}/model.S")

file(SIZE "${MODEL_GGUF}" MODEL_GGUF_SIZE)

configure_file(
    "${CMAKE_SOURCE_DIR}/src/model.S.in"
    "${MODEL_S}"
    @ONLY
)

set_source_files_properties("${MODEL_S}" PROPERTIES
    OBJECT_DEPENDS "${MODEL_GGUF}"
)

set(MODEL_EMBED_SOURCES "${MODEL_S}")

set(LIB_SOURCES src/libemb.c ${GGML_SOURCES})

if(EMSCRIPTEN)
    add_executable(emb_wasm ${LIB_SOURCES} ${MODEL_EMBED_SOURCES})
    target_include_directories(emb_wasm PRIVATE src lib/ggml/include lib/ggml/src lib/ggml/src/ggml-cpu)
    set_target_properties(emb_wasm PROPERTIES
        OUTPUT_NAME "emb"
        SUFFIX ".wasm"
    )
    target_link_options(emb_wasm PRIVATE
        --no-entry
        -sSTANDALONE_WASM=1
        -sINITIAL_MEMORY=268435456
        -sSTACK_SIZE=1048576
        -sEXPORTED_FUNCTIONS=_kc_emb_version,_kc_emb_dimension,_kc_emb_embed,_kc_emb_free
    )
endif()

## Library targets

if(NOT EMB_TEST_EXTERNAL_ARTIFACTS AND NOT EMSCRIPTEN)
    add_library(emb_static STATIC ${LIB_SOURCES} ${MODEL_EMBED_SOURCES})
    set_target_properties(emb_static PROPERTIES OUTPUT_NAME "emb" POSITION_INDEPENDENT_CODE ON)
    target_include_directories(emb_static PRIVATE src lib/ggml/include lib/ggml/src lib/ggml/src/ggml-cpu)

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(emb_static pthread dl m)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "armv7|arm|mips")
            target_link_libraries(emb_static atomic)
        endif()
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "mips")
            target_link_options(emb_static PRIVATE -Wl,--no-warn-mismatch)
        endif()
    elseif(ANDROID)
        target_link_libraries(emb_static dl m)
    endif()

    add_library(emb_shared SHARED ${LIB_SOURCES} ${MODEL_EMBED_SOURCES})
    set_target_properties(emb_shared PROPERTIES OUTPUT_NAME "emb")
    set_target_properties(emb_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_include_directories(emb_shared PRIVATE src lib/ggml/include lib/ggml/src lib/ggml/src/ggml-cpu)

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(emb_shared pthread dl m)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "armv7|arm|mips")
            target_link_libraries(emb_shared atomic)
        endif()
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "mips")
            target_link_options(emb_shared PRIVATE -Wl,--no-warn-mismatch)
        endif()
    elseif(ANDROID)
        target_link_libraries(emb_shared dl m)
    endif()

    add_executable(emb src/emb.c)
    target_include_directories(emb PRIVATE src)
    target_link_libraries(emb emb_static)

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        target_link_libraries(emb pthread dl m)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "armv7|arm|mips")
            target_link_libraries(emb atomic)
        endif()
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "mips")
            target_link_options(emb PRIVATE -Wl,--no-warn-mismatch)
        endif()
    elseif(ANDROID)
        target_link_libraries(emb dl m)
    endif()
endif()

if(EMB_BUILD_TESTS)
    enable_testing()

    if(EMSCRIPTEN)
        add_executable(emb_contract_test src/test.c ${LIB_SOURCES} ${MODEL_EMBED_SOURCES})
        target_include_directories(emb_contract_test PRIVATE src lib/ggml/include lib/ggml/src lib/ggml/src/ggml-cpu)
        target_link_options(emb_contract_test PRIVATE
            -sINITIAL_MEMORY=268435456
            -sSTACK_SIZE=1048576
        )
    else()
        if(EMB_TEST_SHARED_LIBRARY)
            add_library(emb_test_shared SHARED IMPORTED)
            set_target_properties(emb_test_shared PROPERTIES
                IMPORTED_LOCATION "${EMB_TEST_SHARED_LIBRARY}"
            )
            if(EMB_TEST_IMPORT_LIBRARY)
                set_target_properties(emb_test_shared PROPERTIES
                    IMPORTED_IMPLIB "${EMB_TEST_IMPORT_LIBRARY}"
                )
            endif()
        else()
            set(EMB_TEST_SHARED_TARGET emb_shared)
        endif()

        if(TARGET emb_test_shared)
            set(EMB_TEST_SHARED_TARGET emb_test_shared)
        endif()

        add_executable(emb_contract_test src/test.c)
        target_include_directories(emb_contract_test PRIVATE src)
        target_link_libraries(emb_contract_test PRIVATE ${EMB_TEST_SHARED_TARGET})

        if(UNIX AND NOT ANDROID)
            target_link_libraries(emb_contract_test PRIVATE pthread dl m)
        elseif(ANDROID)
            target_link_libraries(emb_contract_test PRIVATE dl m)
        endif()

        add_custom_command(TARGET emb_contract_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${EMB_TEST_SHARED_TARGET}>
                $<TARGET_FILE_DIR:emb_contract_test>
        )
    endif()
endif()

if(EMB_BUILD_TESTS)
    if(EMB_TEST_CLI)
        set(EMB_TEST_CLI_TARGET_PATH "${EMB_TEST_CLI}")
    else()
        set(EMB_TEST_CLI_TARGET_PATH "")
    endif()
    target_compile_definitions(emb_contract_test PRIVATE
        EMB_TEST_CLI="${EMB_TEST_CLI_TARGET_PATH}"
    )
endif()
