cmake_minimum_required(VERSION 3.16)
project(kc C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT DEFINED KC_TARGET)
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(KC_PLATFORM "windows")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(KC_PLATFORM "macos")
    else()
        set(KC_PLATFORM "linux")
    endif()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" KC_ARCH)
    if(KC_ARCH STREQUAL "amd64")
        set(KC_ARCH "x86_64")
    elseif(KC_ARCH STREQUAL "arm64")
        set(KC_ARCH "aarch64")
    endif()
    set(KC_TARGET "${KC_PLATFORM}/${KC_ARCH}")
endif()

add_executable(kc src/kc.c src/packages.c src/sha256.c)
set_target_properties(kc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/${KC_TARGET}"
)

option(KC_BUILD_TESTS "Build kc contract tests" OFF)

if(KC_BUILD_TESTS)
    enable_testing()
    add_executable(kc_contract_test src/test.c)
    target_compile_definitions(kc_contract_test PRIVATE
        KC_CLI_PATH="$<TARGET_FILE:kc>"
    )
    add_test(NAME kc_contract_test COMMAND kc_contract_test all)
endif()
