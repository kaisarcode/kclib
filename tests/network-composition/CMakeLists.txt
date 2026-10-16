cmake_minimum_required(VERSION 3.14)
project(kclib_network_composition C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(NETS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NETL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TLSIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(HTTP_BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory(
    ${CMAKE_CURRENT_LIST_DIR}/../../proj/libnets.c
    ${CMAKE_CURRENT_BINARY_DIR}/nets
)
add_subdirectory(
    ${CMAKE_CURRENT_LIST_DIR}/../../proj/libnetl.c
    ${CMAKE_CURRENT_BINARY_DIR}/netl
)
add_subdirectory(
    ${CMAKE_CURRENT_LIST_DIR}/../../proj/libtlsio.c
    ${CMAKE_CURRENT_BINARY_DIR}/tlsio
)
add_subdirectory(
    ${CMAKE_CURRENT_LIST_DIR}/../../proj/libhttp.c
    ${CMAKE_CURRENT_BINARY_DIR}/http
)

add_executable(network_composition_test test.c)

target_include_directories(network_composition_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../proj/libnets.c/src
    ${CMAKE_CURRENT_LIST_DIR}/../../proj/libnetl.c/src
    ${CMAKE_CURRENT_LIST_DIR}/../../proj/libtlsio.c/src
    ${CMAKE_CURRENT_LIST_DIR}/../../proj/libhttp.c/src
)

target_link_libraries(network_composition_test PRIVATE
    nets_static
    netl_static
    tlsio_static
    http_static
)

if(MSVC)
    target_compile_options(network_composition_test PRIVATE /W3 /WX)
else()
    target_compile_options(network_composition_test PRIVATE
        -Wall
        -Wextra
        -Werror
    )
endif()

if(WIN32)
    target_link_libraries(network_composition_test PRIVATE
        ws2_32
        bcrypt
        crypt32
    )
else()
    find_package(Threads REQUIRED)
    target_link_libraries(network_composition_test PRIVATE Threads::Threads)

    if(APPLE)
        target_link_libraries(network_composition_test PRIVATE
            "-framework Security"
            "-framework CoreFoundation"
        )
    elseif(UNIX AND NOT ANDROID)
        target_link_libraries(network_composition_test PRIVATE m)
    endif()
endif()

enable_testing()
add_test(
    NAME network_building_block_composition
    COMMAND network_composition_test
)
