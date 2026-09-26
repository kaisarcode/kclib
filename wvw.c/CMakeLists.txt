cmake_minimum_required(VERSION 3.14)
project(wvw C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED WVW_BUILD_VERSION)
    set(WVW_BUILD_VERSION 0)
endif()

option(LIBWVW_NATIVE "Enable native hardware optimizations" OFF)
option(WVW_BUILD_TESTS "Build wvw tests" OFF)

set(WVW_TEST_EXTERNAL_ARTIFACTS OFF)
if(WVW_BUILD_TESTS AND WVW_TEST_SHARED_LIBRARY)
    set(WVW_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(LIBWVW_NATIVE)
        add_compile_options(-march=native)
    endif()
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

if(NOT WVW_TEST_EXTERNAL_ARTIFACTS)
    if(WIN32)
        set(WVW_SOURCES src/libwvw.c lib/parson/parson.c)
        set(WVW_WEBVIEW2_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/webview2/include")
        set(WVW_WEBVIEW2_LOADER "${CMAKE_CURRENT_SOURCE_DIR}/lib/webview2/bin/x86_64/WebView2Loader.dll")

        set_source_files_properties(src/libwvw.c PROPERTIES
            COMPILE_DEFINITIONS KC_WVW_BUILD_VERSION=${WVW_BUILD_VERSION}
        )

        add_executable(wvw src/wvw.c ${WVW_SOURCES})
        target_include_directories(wvw PRIVATE src lib/parson)
        target_include_directories(wvw SYSTEM PRIVATE ${WVW_WEBVIEW2_INCLUDE_DIR})
        target_link_libraries(wvw ole32 uuid user32 gdi32 shell32)

        add_library(wvw_static STATIC ${WVW_SOURCES})
        set_target_properties(wvw_static PROPERTIES OUTPUT_NAME "wvw")
        target_include_directories(wvw_static PRIVATE src lib/parson)
        target_include_directories(wvw_static SYSTEM PRIVATE ${WVW_WEBVIEW2_INCLUDE_DIR})
        target_link_libraries(wvw_static ole32 uuid user32 gdi32 shell32)

        add_library(wvw_shared SHARED ${WVW_SOURCES})
        set_target_properties(wvw_shared PROPERTIES OUTPUT_NAME "wvw")
        set_target_properties(wvw_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
        target_include_directories(wvw_shared PRIVATE src lib/parson)
        target_include_directories(wvw_shared SYSTEM PRIVATE ${WVW_WEBVIEW2_INCLUDE_DIR})
        target_link_libraries(wvw_shared ole32 uuid user32 gdi32 shell32)
    else()
        set(WVW_SOURCES src/libwvw.c lib/parson/parson.c)

        set_source_files_properties(src/libwvw.c PROPERTIES
            COMPILE_DEFINITIONS KC_WVW_BUILD_VERSION=${WVW_BUILD_VERSION}
        )

        if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            set(WVW_SOURCES src/libwvw.c lib/parson/parson.c)
            set_source_files_properties(src/libwvw.c PROPERTIES
                COMPILE_OPTIONS "-x;objective-c"
            )
            add_executable(wvw src/wvw.c ${WVW_SOURCES})
            target_include_directories(wvw PRIVATE src lib/parson)
            target_link_libraries(wvw "-framework WebKit" "-framework Cocoa" "-framework Foundation" pthread m)

            add_library(wvw_static STATIC ${WVW_SOURCES})
            set_target_properties(wvw_static PROPERTIES OUTPUT_NAME "wvw")
            target_include_directories(wvw_static PRIVATE src lib/parson)
            target_link_libraries(wvw_static pthread m)

            add_library(wvw_shared SHARED ${WVW_SOURCES})
            set_target_properties(wvw_shared PROPERTIES OUTPUT_NAME "wvw")
            set_target_properties(wvw_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
            target_include_directories(wvw_shared PRIVATE src lib/parson)
            target_link_libraries(wvw_shared "-framework WebKit" "-framework Cocoa" "-framework Foundation" pthread m)
        else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
        pkg_check_modules(WEBKIT REQUIRED webkit2gtk-4.1)

        add_executable(wvw src/wvw.c ${WVW_SOURCES})
        target_include_directories(wvw PRIVATE src lib/parson ${GTK3_INCLUDE_DIRS} ${WEBKIT_INCLUDE_DIRS})
        target_compile_options(wvw PRIVATE ${GTK3_CFLAGS_OTHER} ${WEBKIT_CFLAGS_OTHER})
        target_link_directories(wvw PRIVATE ${GTK3_LIBRARY_DIRS} ${WEBKIT_LIBRARY_DIRS})
        target_link_libraries(wvw ${GTK3_LIBRARIES} ${WEBKIT_LIBRARIES} pthread m dl)

        add_library(wvw_static STATIC ${WVW_SOURCES})
        set_target_properties(wvw_static PROPERTIES OUTPUT_NAME "wvw")
        target_include_directories(wvw_static PRIVATE src lib/parson ${GTK3_INCLUDE_DIRS} ${WEBKIT_INCLUDE_DIRS})
        target_compile_options(wvw_static PRIVATE ${GTK3_CFLAGS_OTHER} ${WEBKIT_CFLAGS_OTHER})
        target_link_directories(wvw_static PRIVATE ${GTK3_LIBRARY_DIRS} ${WEBKIT_LIBRARY_DIRS})
        target_link_libraries(wvw_static ${GTK3_LIBRARIES} ${WEBKIT_LIBRARIES} pthread m dl)

        add_library(wvw_shared SHARED ${WVW_SOURCES})
        set_target_properties(wvw_shared PROPERTIES OUTPUT_NAME "wvw")
        set_target_properties(wvw_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
        target_include_directories(wvw_shared PRIVATE src lib/parson ${GTK3_INCLUDE_DIRS} ${WEBKIT_INCLUDE_DIRS})
        target_compile_options(wvw_shared PRIVATE ${GTK3_CFLAGS_OTHER} ${WEBKIT_CFLAGS_OTHER})
        target_link_directories(wvw_shared PRIVATE ${GTK3_LIBRARY_DIRS} ${WEBKIT_LIBRARY_DIRS})
        target_link_libraries(wvw_shared ${GTK3_LIBRARIES} ${WEBKIT_LIBRARIES} pthread m dl)
        endif()
    endif()
endif()

if(WVW_BUILD_TESTS)
    if(WVW_TEST_SHARED_LIBRARY)
        add_library(wvw_test_shared SHARED IMPORTED)
        set_target_properties(wvw_test_shared PROPERTIES
            IMPORTED_LOCATION "${WVW_TEST_SHARED_LIBRARY}"
        )
        if(WVW_TEST_IMPORT_LIBRARY)
            set_target_properties(wvw_test_shared PROPERTIES
                IMPORTED_IMPLIB "${WVW_TEST_IMPORT_LIBRARY}"
            )
        endif()
    else()
        set(WVW_TEST_SHARED_TARGET wvw_shared)
    endif()

    if(TARGET wvw_test_shared)
        set(WVW_TEST_SHARED_TARGET wvw_test_shared)
    endif()

    add_executable(wvw_contract_test src/test.c)
    target_include_directories(wvw_contract_test PRIVATE src lib/parson)
    target_link_libraries(wvw_contract_test PRIVATE ${WVW_TEST_SHARED_TARGET})
    if(WVW_TEST_CLI)
        target_compile_definitions(wvw_contract_test PRIVATE WVW_TEST_CLI="${WVW_TEST_CLI}")
    endif()

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GTK3_TEST gtk+-3.0)
        pkg_check_modules(WEBKIT_TEST webkit2gtk-4.1)
        target_include_directories(wvw_contract_test PRIVATE lib/parson ${GTK3_TEST_INCLUDE_DIRS} ${WEBKIT_TEST_INCLUDE_DIRS})
        target_compile_options(wvw_contract_test PRIVATE ${GTK3_TEST_CFLAGS_OTHER} ${WEBKIT_TEST_CFLAGS_OTHER})
        target_link_directories(wvw_contract_test PRIVATE ${GTK3_TEST_LIBRARY_DIRS} ${WEBKIT_TEST_LIBRARY_DIRS})
        target_link_libraries(wvw_contract_test PRIVATE ${GTK3_TEST_LIBRARIES} ${WEBKIT_TEST_LIBRARIES} pthread m dl)
    endif()

    if(WIN32)
        target_link_libraries(wvw_contract_test PRIVATE ole32 uuid user32 gdi32 shell32)
    endif()

    if(WVW_TEST_SHARED_LIBRARY)
        get_filename_component(WVW_TEST_SHARED_DIR "${WVW_TEST_SHARED_LIBRARY}" DIRECTORY)
        if(EXISTS "${WVW_TEST_SHARED_DIR}/WebView2Loader.dll")
            add_custom_command(TARGET wvw_contract_test POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${WVW_TEST_SHARED_DIR}/WebView2Loader.dll"
                    $<TARGET_FILE_DIR:wvw_contract_test>
            )
        endif()
    endif()

    add_custom_command(TARGET wvw_contract_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${WVW_TEST_SHARED_TARGET}>
            $<TARGET_FILE_DIR:wvw_contract_test>
    )
endif()
