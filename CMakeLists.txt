cmake_minimum_required(VERSION 3.14)
project(wvw C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)

if(NOT DEFINED WVW_BUILD_VERSION)
    set(WVW_BUILD_VERSION 0)
endif()

option(LIBWVW_NATIVE "Enable native hardware optimizations" OFF)

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(LIBWVW_NATIVE)
        add_compile_options(-march=native)
    endif()
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

if(WIN32)
    set(WVW_SOURCES
        src/libwvw-win.cpp
    )
    set(WVW_WEBVIEW2_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/windows/x86_64")
    if(NOT EXISTS "${WVW_WEBVIEW2_SDK_DIR}/include/WebView2.h")
        message(FATAL_ERROR "Missing ${WVW_WEBVIEW2_SDK_DIR}/include/WebView2.h")
    endif()
    if(NOT EXISTS "${WVW_WEBVIEW2_SDK_DIR}/bin/WebView2Loader.dll")
        message(FATAL_ERROR "Missing ${WVW_WEBVIEW2_SDK_DIR}/bin/WebView2Loader.dll")
    endif()

    set_source_files_properties(src/libwvw-win.cpp PROPERTIES
        COMPILE_DEFINITIONS KC_WVW_BUILD_VERSION=${WVW_BUILD_VERSION}
    )

    add_executable(wvw src/wvw.c ${WVW_SOURCES})
    target_include_directories(wvw PRIVATE src ${WVW_WEBVIEW2_SDK_DIR}/include)
    target_compile_options(wvw PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-unknown-pragmas>)
    target_link_libraries(wvw ole32 uuid user32 gdi32 shell32)

    add_library(wvw_static STATIC ${WVW_SOURCES})
    set_target_properties(wvw_static PROPERTIES OUTPUT_NAME "wvw")
    target_include_directories(wvw_static PRIVATE src ${WVW_WEBVIEW2_SDK_DIR}/include)
    target_compile_options(wvw_static PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-unknown-pragmas>)
    target_link_libraries(wvw_static ole32 uuid user32 gdi32 shell32)

    add_library(wvw_shared SHARED ${WVW_SOURCES})
    set_target_properties(wvw_shared PROPERTIES OUTPUT_NAME "wvw")
    target_include_directories(wvw_shared PRIVATE src ${WVW_WEBVIEW2_SDK_DIR}/include)
    target_compile_options(wvw_shared PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-unknown-pragmas>)
    target_link_libraries(wvw_shared ole32 uuid user32 gdi32 shell32)
else()
    set(WVW_SOURCES
        src/libwvw.c
    )
    set_source_files_properties(src/libwvw.c PROPERTIES
        COMPILE_DEFINITIONS KC_WVW_BUILD_VERSION=${WVW_BUILD_VERSION}
    )

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    pkg_check_modules(WEBKIT REQUIRED webkit2gtk-4.1)

    add_executable(wvw src/wvw.c ${WVW_SOURCES})
    target_include_directories(wvw PRIVATE src ${GTK3_INCLUDE_DIRS} ${WEBKIT_INCLUDE_DIRS})
    target_compile_options(wvw PRIVATE ${GTK3_CFLAGS_OTHER} ${WEBKIT_CFLAGS_OTHER})
    target_link_directories(wvw PRIVATE ${GTK3_LIBRARY_DIRS} ${WEBKIT_LIBRARY_DIRS})
    target_link_libraries(wvw ${GTK3_LIBRARIES} ${WEBKIT_LIBRARIES} pthread m)

    add_library(wvw_static STATIC ${WVW_SOURCES})
    set_target_properties(wvw_static PROPERTIES OUTPUT_NAME "wvw")
    target_include_directories(wvw_static PRIVATE src ${GTK3_INCLUDE_DIRS} ${WEBKIT_INCLUDE_DIRS})
    target_compile_options(wvw_static PRIVATE ${GTK3_CFLAGS_OTHER} ${WEBKIT_CFLAGS_OTHER})
    target_link_directories(wvw_static PRIVATE ${GTK3_LIBRARY_DIRS} ${WEBKIT_LIBRARY_DIRS})
    target_link_libraries(wvw_static ${GTK3_LIBRARIES} ${WEBKIT_LIBRARIES} pthread m)

    add_library(wvw_shared SHARED ${WVW_SOURCES})
    set_target_properties(wvw_shared PROPERTIES OUTPUT_NAME "wvw")
    target_include_directories(wvw_shared PRIVATE src ${GTK3_INCLUDE_DIRS} ${WEBKIT_INCLUDE_DIRS})
    target_compile_options(wvw_shared PRIVATE ${GTK3_CFLAGS_OTHER} ${WEBKIT_CFLAGS_OTHER})
    target_link_directories(wvw_shared PRIVATE ${GTK3_LIBRARY_DIRS} ${WEBKIT_LIBRARY_DIRS})
    target_link_libraries(wvw_shared ${GTK3_LIBRARIES} ${WEBKIT_LIBRARIES} pthread m)
endif()
