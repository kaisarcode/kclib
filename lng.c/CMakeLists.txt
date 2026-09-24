cmake_minimum_required(VERSION 3.14)
project(lng C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED LNG_BUILD_VERSION)
    set(LNG_BUILD_VERSION 0)
endif()

option(LNG_NATIVE "Enable native CPU optimizations" OFF)
option(LNG_BUILD_TESTS "Build lng tests" OFF)

if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc")
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror -fno-finite-math-only)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    add_definitions(-D_GNU_SOURCE)
    if(LNG_NATIVE)
        add_compile_options(-march=native)
    endif()
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set(LNG_SOURCES src/liblng.c)

set_source_files_properties(src/liblng.c PROPERTIES
    COMPILE_DEFINITIONS KC_LNG_BUILD_VERSION=${LNG_BUILD_VERSION}
)

if(EMSCRIPTEN)
    add_executable(lng_wasm ${LNG_SOURCES})
    target_include_directories(lng_wasm PRIVATE src)
    set_target_properties(lng_wasm PROPERTIES
        OUTPUT_NAME "lng"
        SUFFIX ".wasm"
    )
    target_link_options(lng_wasm PRIVATE
        --no-entry
        -sSTANDALONE_WASM=1
        -sEXPORTED_FUNCTIONS=_kc_lng_detect,_kc_lng_free,_kc_lng_version
    )
endif()

if(NOT EMSCRIPTEN)
add_library(lng_shared SHARED ${LNG_SOURCES})
set_target_properties(lng_shared PROPERTIES
    OUTPUT_NAME lng
    PREFIX "lib"
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)
target_include_directories(lng_shared PRIVATE src)

add_library(lng_static STATIC ${LNG_SOURCES})
set_target_properties(lng_static PROPERTIES
    OUTPUT_NAME lng
    PREFIX "lib"
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(lng_static PRIVATE src)

add_executable(lng_exe src/lng.c ${LNG_SOURCES})
set_target_properties(lng_exe PROPERTIES OUTPUT_NAME lng)
target_include_directories(lng_exe PRIVATE src)

if(UNIX AND NOT ANDROID AND NOT APPLE)
    target_link_libraries(lng_shared pthread m)
    target_link_libraries(lng_static pthread m)
    target_link_libraries(lng_exe pthread m)
elseif(ANDROID)
    target_link_libraries(lng_shared m)
    target_link_libraries(lng_static m)
    target_link_libraries(lng_exe m)
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|armv7)")
    if(NOT ANDROID)
        target_link_libraries(lng_shared atomic)
        target_link_libraries(lng_static atomic)
        target_link_libraries(lng_exe atomic)
    endif()
endif()
endif()

if(LNG_BUILD_TESTS)
    enable_testing()

    if(EMSCRIPTEN)
        add_executable(lng_contract_test src/test.c ${LNG_SOURCES})
        target_include_directories(lng_contract_test PRIVATE src)
    else()
        if(LNG_TEST_SHARED_LIBRARY)
            add_library(lng_test_shared SHARED IMPORTED)
            set_target_properties(lng_test_shared PROPERTIES
                IMPORTED_LOCATION "${LNG_TEST_SHARED_LIBRARY}"
            )
            if(LNG_TEST_IMPORT_LIBRARY)
                set_target_properties(lng_test_shared PROPERTIES
                    IMPORTED_IMPLIB "${LNG_TEST_IMPORT_LIBRARY}"
                )
            endif()
            set(LNG_TEST_SHARED_TARGET lng_test_shared)
        else()
            set(LNG_TEST_SHARED_TARGET lng_shared)
        endif()

        add_executable(lng_contract_test src/test.c)
        target_include_directories(lng_contract_test PRIVATE src)
        target_link_libraries(lng_contract_test ${LNG_TEST_SHARED_TARGET})
        if(UNIX AND NOT ANDROID AND NOT APPLE)
            target_link_libraries(lng_contract_test pthread m)
        elseif(ANDROID)
            target_link_libraries(lng_contract_test m)
        endif()

        add_custom_command(TARGET lng_contract_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${LNG_TEST_SHARED_TARGET}>
                $<TARGET_FILE_DIR:lng_contract_test>
        )
    endif()
endif()

if(LNG_BUILD_TESTS)
    if(LNG_TEST_CLI)
        set(LNG_TEST_CLI_TARGET_PATH "${LNG_TEST_CLI}")
    else()
        set(LNG_TEST_CLI_TARGET_PATH "")
    endif()
    target_compile_definitions(lng_contract_test PRIVATE
        LNG_TEST_CLI="${LNG_TEST_CLI_TARGET_PATH}"
    )
endif()
