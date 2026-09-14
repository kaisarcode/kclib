# llm.c - KaisarCode local generative inference library and CLI
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.14)
project(llm C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MINGW)
    add_link_options(-static-libgcc -static-libstdc++)
endif()

if(NOT DEFINED KC_LLM_BUILD_VERSION)
    set(KC_LLM_BUILD_VERSION 0)
endif()

if(NOT DEFINED KC_LLAMACPP_COMMIT)
    set(KC_LLAMACPP_COMMIT "unknown")
endif()

# Always prefer the environment variable so it takes effect
# even when CMake re-runs from a stale cache.
if(DEFINED ENV{KC_LLAMACPP_DIR} AND NOT "$ENV{KC_LLAMACPP_DIR}" STREQUAL "")
    set(KC_LLAMACPP_DIR "$ENV{KC_LLAMACPP_DIR}" CACHE PATH "Path to llama.cpp source tree" FORCE)
elseif(NOT KC_LLAMACPP_DIR)
    message(FATAL_ERROR "KC_LLAMACPP_DIR cache variable or environment variable is required")
endif()
option(LLM_CUDA_ENABLED "Build with CUDA backend support" OFF)
option(LLM_BUILD_TESTS "Build libllm API tests" OFF)
set(LLM_TEST_MODEL "" CACHE STRING "Optional local test model path")
set(LLM_TEST_LORA "" CACHE STRING "Optional local test LoRA path")

if(NOT EXISTS "${KC_LLAMACPP_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "llama.cpp not found at ${KC_LLAMACPP_DIR}; check the KC_LLAMACPP_DIR environment variable")
endif()
if(NOT EXISTS "${KC_LLAMACPP_DIR}/include/llama.h")
    message(FATAL_ERROR "llama.cpp not found at ${KC_LLAMACPP_DIR}; check the KC_LLAMACPP_DIR environment variable")
endif()

if(NOT CMAKE_SYSTEM_PROCESSOR OR CMAKE_SYSTEM_PROCESSOR STREQUAL "")
    set(CMAKE_SYSTEM_PROCESSOR "${CMAKE_HOST_SYSTEM_PROCESSOR}" CACHE STRING "Target processor" FORCE)
endif()
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "amd64")
    set(CMAKE_SYSTEM_PROCESSOR "x86_64" CACHE STRING "Target processor" FORCE)
endif()
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    set(CMAKE_SYSTEM_PROCESSOR "aarch64" CACHE STRING "Target processor" FORCE)
endif()
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "armv7l")
    set(CMAKE_SYSTEM_PROCESSOR "armv7" CACHE STRING "Target processor" FORCE)
endif()
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "i386")
    set(CMAKE_SYSTEM_PROCESSOR "i686" CACHE STRING "Target processor" FORCE)
endif()

if(LLM_CUDA_ENABLED)
    if(NOT CMAKE_CUDA_ARCHITECTURES)
        if(CMAKE_CROSSCOMPILING)
            set(CMAKE_CUDA_ARCHITECTURES "86" CACHE STRING "CUDA architectures" FORCE)
        else()
            set(CMAKE_CUDA_ARCHITECTURES "native" CACHE STRING "CUDA architectures" FORCE)
        endif()
    endif()
    enable_language(CUDA)
endif()

if(NOT MSVC)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
endif()

# Configure llama.cpp as a read-only source dependency.
set(LLAMA_OPENSSL OFF CACHE BOOL "llama: use openssl to support HTTPS" FORCE)
set(LLAMA_STANDALONE OFF)
set(BUILD_SHARED_LIBS OFF)
set(LLAMA_BUILD_COMMON ON)
set(LLAMA_BUILD_TESTS OFF)
set(LLAMA_BUILD_EXAMPLES OFF)
set(LLAMA_BUILD_TOOLS OFF)
set(LLAMA_BUILD_SERVER OFF)
set(LLAMA_BUILD_APP OFF)
set(LLAMA_BUILD_UI OFF)
set(LLAMA_USE_PREBUILT_UI OFF)
set(GGML_CCACHE OFF)
set(GGML_ACCELERATE OFF)
set(GGML_BLAS OFF)
set(GGML_LLAMAFILE OFF)
set(GGML_METAL OFF)
set(GGML_BACKEND_DL OFF)
set(GGML_OPENMP OFF)
set(GGML_NATIVE OFF)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
    set(GGML_AVX2 ON CACHE BOOL "ggml: enable AVX2" FORCE)
    set(GGML_FMA ON CACHE BOOL "ggml: enable FMA" FORCE)
    set(GGML_F16C ON CACHE BOOL "ggml: enable F16C" FORCE)
endif()
set(GGML_CUDA ${LLM_CUDA_ENABLED})
set(GGML_CUDA_NCCL OFF CACHE BOOL "ggml: use NVIDIA Collective Comm. Library" FORCE)
set(LLAMA_BUILD_MTMD ON)

if(LLM_CUDA_ENABLED AND CMAKE_CROSSCOMPILING AND CUDAToolkit_ROOT)
    set(CMAKE_CUDA_COMPILER_TOOLKIT_ROOT "${CUDAToolkit_ROOT}" CACHE PATH "CUDA toolkit installation root" FORCE)
    set(CUDAToolkit_INCLUDE_DIR "${CUDAToolkit_ROOT}/include" CACHE PATH "" FORCE)
    set(CUDA_cuda_driver_LIBRARY "${CUDAToolkit_ROOT}/targets/sbsa-linux/lib/stubs/libcuda.so" CACHE FILEPATH "" FORCE)
    set(CUDA_cublas_LIBRARY "${CUDAToolkit_ROOT}/targets/sbsa-linux/lib/libcublas_static.a" CACHE FILEPATH "" FORCE)
    set(CUDA_cublasLt_LIBRARY "${CUDAToolkit_ROOT}/targets/sbsa-linux/lib/libcublasLt_static.a" CACHE FILEPATH "" FORCE)
endif()
if(ANDROID)
    set(MTMD_VIDEO OFF)
endif()

add_subdirectory("${KC_LLAMACPP_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/llamacpp" EXCLUDE_FROM_ALL)

if(LLM_CUDA_ENABLED AND CMAKE_CROSSCOMPILING AND CUDAToolkit_ROOT)
    if(TARGET CUDA::cuda_driver)
        set_target_properties(CUDA::cuda_driver PROPERTIES
            IMPORTED_LOCATION "${CUDAToolkit_ROOT}/targets/sbsa-linux/lib/stubs/libcuda.so"
            IMPORTED_IMPLIB "${CUDAToolkit_ROOT}/targets/sbsa-linux/lib/stubs/libcuda.so")
    endif()
    if(TARGET CUDA::cublas)
        set_target_properties(CUDA::cublas PROPERTIES
            IMPORTED_LOCATION "${CUDAToolkit_ROOT}/targets/sbsa-linux/lib/libcublas_static.a")
    endif()
    if(TARGET CUDA::cublasLt)
        set_target_properties(CUDA::cublasLt PROPERTIES
            IMPORTED_LOCATION "${CUDAToolkit_ROOT}/targets/sbsa-linux/lib/libcublasLt_static.a")
    endif()
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    file(GLOB KC_LLAMACPP_MODEL_SOURCES "${KC_LLAMACPP_DIR}/src/models/*.cpp")
    set_source_files_properties(
        ${KC_LLAMACPP_MODEL_SOURCES}
        PROPERTIES COMPILE_OPTIONS "-O0"
    )
endif()

if(ANDROID AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    file(GLOB KC_LLAMACPP_MODEL_SOURCES_ANDROID "${KC_LLAMACPP_DIR}/src/models/*.cpp")
    set_source_files_properties(
        ${KC_LLAMACPP_MODEL_SOURCES_ANDROID}
        "${KC_LLAMACPP_DIR}/src/llama-model.cpp"
        PROPERTIES COMPILE_OPTIONS "-O0"
    )
    if(TARGET ggml-base)
        target_compile_options(ggml-base PRIVATE -O0 -g0)
    endif()
    if(TARGET ggml-cpu)
        target_compile_options(ggml-cpu PRIVATE -O0 -g0)
    endif()
    if(TARGET ggml)
        target_compile_options(ggml PRIVATE -O0 -g0)
    endif()
    if(TARGET llama)
        target_compile_options(llama PRIVATE -O0 -g0)
    endif()
    if(TARGET mtmd)
        target_compile_options(mtmd PRIVATE -O0 -g0)
        set_source_files_properties(
            "${KC_LLAMACPP_DIR}/tools/mtmd/models/gemma4uv.cpp"
            PROPERTIES HEADER_FILE_ONLY ON
        )
    endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang"
   AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64"
   AND (CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "iOS"))
    if(TARGET ggml-base)
        target_compile_options(ggml-base PRIVATE -O0 -g0)
    endif()
    if(TARGET ggml-cpu)
        target_compile_options(ggml-cpu PRIVATE -O0 -g0)
    endif()
    if(TARGET ggml)
        target_compile_options(ggml PRIVATE -O0 -g0)
    endif()
    if(TARGET llama)
        target_compile_options(llama PRIVATE -O0 -g0)
    endif()
    if(TARGET mtmd)
        target_compile_options(mtmd PRIVATE -O0 -g0)
    endif()
endif()

# Suppress all vendor warnings — we don't control this code.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    foreach(_tgt ggml-base ggml-cpu ggml llama llama-common llama-common-base mtmd cpp-httplib)
        if(TARGET ${_tgt})
            target_compile_options(${_tgt} PRIVATE -w -Wno-psabi)
        endif()
    endforeach()
endif()

if(ANDROID AND TARGET llama)
    target_compile_definitions(llama PRIVATE
        posix_madvise=madvise
        POSIX_MADV_WILLNEED=MADV_WILLNEED
        POSIX_MADV_RANDOM=MADV_RANDOM
    )
endif()

set(KC_LLM_COMMON_DEFS "")

set(KC_LLM_COMMON_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${KC_LLAMACPP_DIR}/include"
    "${KC_LLAMACPP_DIR}/ggml/include"
    "${KC_LLAMACPP_DIR}/common"
    "${KC_LLAMACPP_DIR}/vendor"
    "${KC_LLAMACPP_DIR}/tools/mtmd"
)

set(KC_LLM_COMMON_LINKS llama ggml ggml-base mtmd)

set_source_files_properties(src/libllm.c PROPERTIES
    LANGUAGE CXX
    COMPILE_OPTIONS "-Wno-unused-function;-Wno-deprecated;-Wno-constant-conversion"
)

set_source_files_properties(src/llm.c PROPERTIES
    LANGUAGE CXX
    COMPILE_OPTIONS "-Wno-deprecated"
)

if(EMSCRIPTEN)
    if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/kc_llm_version.h")
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/kc_llm_version.h"
            "#define KC_LLM_BUILD_VERSION ${KC_LLM_BUILD_VERSION}\n"
            "#define KC_LLAMACPP_COMMIT \"${KC_LLAMACPP_COMMIT}\"\n")
    endif()

    add_executable(llm_wasm src/libllm.c)
    set_target_properties(llm_wasm PROPERTIES OUTPUT_NAME "llm" SUFFIX ".wasm")
    target_include_directories(llm_wasm PRIVATE ${KC_LLM_COMMON_INCLUDES})
    target_compile_definitions(llm_wasm PRIVATE ${KC_LLM_COMMON_DEFS})
    target_compile_options(llm_wasm PRIVATE -include kc_llm_version.h -sMEMORY64=1)
    target_link_libraries(llm_wasm PRIVATE ${KC_LLM_COMMON_LINKS} llama-common)
    target_link_options(llm_wasm PRIVATE
        --no-entry
        -sSTANDALONE_WASM=1
        -sMEMORY64=1
        -sALLOW_MEMORY_GROWTH=1
        -Wno-deprecated
        -sINITIAL_MEMORY=268435456
        -sSTACK_SIZE=1048576
        -sEXPORTED_FUNCTIONS=_kc_llm_version,_kc_llm_error,_kc_llm_open,_kc_llm_open_model,_kc_llm_close,_kc_llm_generate,_kc_llm_generate_role,_kc_llm_info_query,_kc_llm_stop,_kc_llm_stop_requested,_kc_llm_memory_clear,_kc_llm_options_default,_kc_llm_options_load_env,_kc_llm_options_free
    )
endif()

if(NOT EMSCRIPTEN)
# Static library
add_library(llm_static STATIC src/libllm.c)
set_target_properties(llm_static PROPERTIES OUTPUT_NAME "llm")
target_include_directories(llm_static PRIVATE ${KC_LLM_COMMON_INCLUDES})
target_compile_definitions(llm_static PRIVATE ${KC_LLM_COMMON_DEFS})
target_compile_options(llm_static PRIVATE -include kc_llm_version.h)
if(NOT MSVC)
    target_compile_options(llm_static PRIVATE -Wall -Wextra -Werror)
endif()

# Shared library
add_library(llm_shared SHARED src/libllm.c)
set_target_properties(llm_shared PROPERTIES OUTPUT_NAME "llm")
set_target_properties(llm_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_include_directories(llm_shared PRIVATE ${KC_LLM_COMMON_INCLUDES})
target_compile_definitions(llm_shared PRIVATE ${KC_LLM_COMMON_DEFS})
target_compile_options(llm_shared PRIVATE -include kc_llm_version.h)
target_link_libraries(llm_shared PRIVATE ${KC_LLM_COMMON_LINKS} llama-common)
if(APPLE)
    target_link_libraries(llm_shared PRIVATE stdc++)
elseif(ANDROID)
    target_link_libraries(llm_shared PRIVATE stdc++)
elseif(UNIX)
    target_link_libraries(llm_shared PRIVATE stdc++ supc++)
endif()
if(UNIX AND NOT ANDROID AND NOT APPLE)
    target_link_libraries(llm_shared PRIVATE pthread m)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "armv7|arm|mips")
        target_link_libraries(llm_shared PRIVATE atomic)
    endif()
endif()
if(NOT MSVC)
    target_compile_options(llm_shared PRIVATE -Wall -Wextra -Werror)
endif()

# CLI executable
add_executable(llm src/llm.c)
target_include_directories(llm PRIVATE ${KC_LLM_COMMON_INCLUDES})
target_compile_definitions(llm PRIVATE ${KC_LLM_COMMON_DEFS})
target_compile_options(llm PRIVATE -include kc_llm_version.h)
target_link_libraries(llm PRIVATE llm_static ${KC_LLM_COMMON_LINKS} llama-common)
if(APPLE)
    target_link_libraries(llm PRIVATE stdc++)
elseif(ANDROID)
    target_link_libraries(llm PRIVATE stdc++)
elseif(UNIX)
    target_link_libraries(llm PRIVATE stdc++ supc++)
endif()
if(UNIX AND NOT ANDROID AND NOT APPLE)
    target_link_libraries(llm PRIVATE pthread m)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "armv7|arm|mips")
        target_link_libraries(llm PRIVATE atomic)
    endif()
endif()
if(NOT MSVC)
    target_compile_options(llm PRIVATE -Wall -Wextra -Werror)
endif()

# Standalone static archive.
# libllm.a must be self-contained: a caller that links only this archive
# gets the full library without also linking llama.cpp libraries. Merge
# libllm.c.o with the llama.cpp static archives in link order.
if(CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
    set(KC_LLM_MERGED_DIR "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}/merged")
else()
    set(KC_LLM_MERGED_DIR "${CMAKE_CURRENT_BINARY_DIR}/merged")
endif()
set(KC_LLM_MERGED_ARCHIVE "${KC_LLM_MERGED_DIR}/libllm.a")

set(KC_LLM_MERGED_ARCHIVES
    llama
    ggml
    ggml-cpu
    ggml-base
    mtmd
    llama-common
    llama-common-base
    cpp-httplib
)
if(LLM_CUDA_ENABLED AND TARGET ggml-cuda)
    list(APPEND KC_LLM_MERGED_ARCHIVES ggml-cuda)
endif()

# Merge by per-occurrence `ar xN` extraction. Flat `ar x` is not enough:
# llama.cpp archives contain duplicate member names (two llama.cpp.o), and
# extraction to a flat directory would silently drop one occurrence. The
# merge tool must be a GNU or LLVM ar (xN support); cctools ar lacks it, so
# on Apple targets prefer llvm-ar, falling back to the host GNU ar when
# crossing from another host.
set(LLM_MERGE_AR_EXEC "${CMAKE_AR}")
if(APPLE)
    find_program(KC_LLM_LLVM_AR_EXEC NAMES llvm-ar)
    if(KC_LLM_LLVM_AR_EXEC)
        set(LLM_MERGE_AR_EXEC "${KC_LLM_LLVM_AR_EXEC}")
    elseif(NOT CMAKE_HOST_APPLE)
        find_program(KC_LLM_HOST_AR_EXEC NAMES ar)
        if(KC_LLM_HOST_AR_EXEC)
            set(LLM_MERGE_AR_EXEC "${KC_LLM_HOST_AR_EXEC}")
        endif()
    endif()
endif()

set(KC_LLM_MERGED_ARCHIVES_GENEX "")
foreach(KC_LLM_MERGED_LIB ${KC_LLM_MERGED_ARCHIVES})
    string(APPEND KC_LLM_MERGED_ARCHIVES_GENEX "\n    \"\$<TARGET_FILE:${KC_LLM_MERGED_LIB}>\"")
endforeach()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/libllm.merge.cmake" CONTENT
"# Merge llm static objects with llama.cpp archives into one self-contained archive.
# Runs via cmake -P. Uses per-occurrence ar xN to handle duplicate member names.
set(LLM_AR \"${CMAKE_AR}\")
set(LLM_RANLIB \"${CMAKE_RANLIB}\")
set(LLM_MERGE_AR \"${LLM_MERGE_AR_EXEC}\")
set(LLM_OUT \"${KC_LLM_MERGED_ARCHIVE}\")
set(LLM_OBJECTS \"\$<TARGET_OBJECTS:llm_static>\")
set(LLM_ARCHIVES ${KC_LLM_MERGED_ARCHIVES_GENEX}
)

set(_tmp \"\${LLM_OUT}.merge.tmp\")
file(REMOVE_RECURSE \"\${_tmp}\")
file(MAKE_DIRECTORY \"\${_tmp}\")
get_filename_component(_outdir \"\${LLM_OUT}\" DIRECTORY)
file(MAKE_DIRECTORY \"\${_outdir}\")
file(REMOVE \"\${LLM_OUT}\")
set(_objects \"\")
set(_idx 0)

foreach(_o IN LISTS LLM_OBJECTS)
    if(NOT IS_ABSOLUTE \"\${_o}\")
        get_filename_component(_o ABSOLUTE \"\${_o}\")
    endif()
    get_filename_component(_base \"\${_o}\" NAME)
    file(COPY \"\${_o}\" DESTINATION \"\${_tmp}\")
    set(_renamed \"\${_tmp}/\${_idx}_\${_base}\")
    file(RENAME \"\${_tmp}/\${_base}\" \"\${_renamed}\")
    list(APPEND _objects \"\${_renamed}\")
    math(EXPR _idx \"\${_idx} + 1\")
endforeach()

set(_ai 0)
foreach(_ar IN LISTS LLM_ARCHIVES)
    if(NOT IS_ABSOLUTE \"\${_ar}\")
        get_filename_component(_ar ABSOLUTE \"\${_ar}\")
    endif()
    execute_process(COMMAND \"\${LLM_MERGE_AR}\" t \"\${_ar}\"
                    OUTPUT_VARIABLE _members RESULT_VARIABLE _r)
    if(NOT _r EQUAL 0)
        message(FATAL_ERROR \"llm: ar t failed for \${_ar}: \${_r}\")
    endif()
    string(REPLACE \";\" \"\\\\;\" _members \"\${_members}\")
    string(REPLACE \"\\n\" \";\" _members \"\${_members}\")
    set(_names \"\")
    set(_counts \"\")
    set(_oi 0)
    foreach(_m IN LISTS _members)
        string(FIND \"\${_m}\" \"__.SYMDEF\" _s0)
        string(SUBSTRING \"\${_m}\" 0 1 _first)
        if(_m STREQUAL \"\" OR _s0 EQUAL 0 OR _first STREQUAL \"/\")
            continue()
        endif()
        list(FIND _names \"\${_m}\" _pos)
        if(_pos EQUAL -1)
            list(APPEND _names \"\${_m}\")
            set(_occ 1)
            list(APPEND _counts \"1\")
        else()
            list(GET _counts \${_pos} _seen)
            math(EXPR _occ \"\${_seen} + 1\")
            list(REMOVE_AT _counts \${_pos})
            list(INSERT _counts \${_pos} \"\${_occ}\")
        endif()
        set(_d \"\${_tmp}/e\${_ai}_\${_oi}\")
        file(MAKE_DIRECTORY \"\${_d}\")
        execute_process(COMMAND \"\${LLM_MERGE_AR}\" xN \"\${_occ}\" \"\${_ar}\" \"\${_m}\"
                        WORKING_DIRECTORY \"\${_d}\" RESULT_VARIABLE _r)
        if(NOT _r EQUAL 0)
            message(FATAL_ERROR
                \"llm: ar xN failed for \${_ar} [#\${_occ}] \${_m}: \${_r}; \"
                \"the merge tool must be a GNU or LLVM ar with xN support\")
        endif()
        file(GLOB _x RELATIVE \"\${_d}\" \"\${_d}/*\")
        if(_x)
            list(GET _x 0 _xname)
            file(RENAME \"\${_d}/\${_xname}\" \"\${_tmp}/\${_ai}_\${_oi}_\${_xname}\")
            list(APPEND _objects \"\${_tmp}/\${_ai}_\${_oi}_\${_xname}\")
        endif()
        math(EXPR _oi \"\${_oi} + 1\")
    endforeach()
    math(EXPR _ai \"\${_ai} + 1\")
endforeach()

execute_process(COMMAND \"\${LLM_AR}\" rcs \"\${LLM_OUT}\" \${_objects} RESULT_VARIABLE _r)
if(NOT _r EQUAL 0)
    message(FATAL_ERROR \"llm: ar rcs failed: \${_r}\")
endif()
execute_process(COMMAND \"\${LLM_RANLIB}\" \"\${LLM_OUT}\" RESULT_VARIABLE _r)
if(NOT _r EQUAL 0)
    message(FATAL_ERROR \"llm: ranlib failed: \${_r}\")
endif()
file(REMOVE_RECURSE \"\${_tmp}\")
")

add_custom_command(
    OUTPUT "${KC_LLM_MERGED_ARCHIVE}"
    COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_BINARY_DIR}/libllm.merge.cmake"
    DEPENDS llm_static ${KC_LLM_MERGED_ARCHIVES}
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "Merging standalone libllm.a"
    VERBATIM
)
add_custom_target(llm_merged ALL DEPENDS "${KC_LLM_MERGED_ARCHIVE}")
endif()

if(LLM_BUILD_TESTS)
    if(EMSCRIPTEN)
        add_executable(llm_contract_test src/test.c src/libllm.c)
        target_include_directories(llm_contract_test PRIVATE ${KC_LLM_COMMON_INCLUDES})
        target_compile_definitions(llm_contract_test PRIVATE KC_LLM_TEST_EXTERNAL_ARTIFACTS=1)
        target_compile_options(llm_contract_test PRIVATE -include kc_llm_version.h -sMEMORY64=1)
        if(LLM_TEST_MODEL)
            target_compile_definitions(llm_contract_test PRIVATE
                KC_LLM_TEST_MODEL="${LLM_TEST_MODEL}"
            )
        endif()
        if(LLM_TEST_LORA)
            target_compile_definitions(llm_contract_test PRIVATE
                KC_LLM_TEST_LORA="${LLM_TEST_LORA}"
            )
        endif()
        target_link_libraries(llm_contract_test PRIVATE ${KC_LLM_COMMON_LINKS} llama-common)
        target_link_options(llm_contract_test PRIVATE
            -sNODERAWFS=1
            -sMEMORY64=1
            -sALLOW_MEMORY_GROWTH=1
            -Wno-deprecated
            -sINITIAL_MEMORY=268435456
            -sSTACK_SIZE=1048576
        )
    else()
    set(LLM_TEST_EXTERNAL_ARTIFACTS OFF)
    if(LLM_TEST_SHARED_LIBRARY)
        set(LLM_TEST_EXTERNAL_ARTIFACTS ON)
    endif()

    if(LLM_TEST_EXTERNAL_ARTIFACTS)
        add_library(llm_test_shared SHARED IMPORTED)
        set_target_properties(llm_test_shared PROPERTIES
            IMPORTED_LOCATION "${LLM_TEST_SHARED_LIBRARY}"
        )
        if(LLM_TEST_IMPORT_LIBRARY)
            set_target_properties(llm_test_shared PROPERTIES
                IMPORTED_IMPLIB "${LLM_TEST_IMPORT_LIBRARY}"
            )
        endif()
        set(LLM_TEST_SHARED_TARGET llm_test_shared)
    else()
        set(LLM_TEST_SHARED_TARGET llm_shared)
    endif()

    add_executable(llm_contract_test src/test.c)
    target_include_directories(llm_contract_test PRIVATE src ${CMAKE_CURRENT_BINARY_DIR})
    if(LLM_TEST_EXTERNAL_ARTIFACTS)
        target_compile_definitions(llm_contract_test PRIVATE
            KC_LLM_TEST_EXTERNAL_ARTIFACTS=1
        )
    endif()
    target_link_libraries(llm_contract_test PRIVATE ${LLM_TEST_SHARED_TARGET})

    if(LLM_TEST_MODEL)
        target_compile_definitions(llm_contract_test PRIVATE
            KC_LLM_TEST_MODEL="${LLM_TEST_MODEL}"
        )
    endif()
    if(LLM_TEST_LORA)
        target_compile_definitions(llm_contract_test PRIVATE
            KC_LLM_TEST_LORA="${LLM_TEST_LORA}"
        )
    endif()

    add_custom_command(TARGET llm_contract_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${LLM_TEST_SHARED_TARGET}>
            $<TARGET_FILE_DIR:llm_contract_test>
    )
    endif()
endif()
