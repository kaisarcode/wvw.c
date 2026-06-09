cmake_minimum_required(VERSION 3.14)
project(wvw C)

set(CMAKE_C_STANDARD 11)

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

set(WVW_SOURCES
    src/wvw.c
    src/libwvw.c
)

if(WIN32)
    add_executable(wvw ${WVW_SOURCES})
    target_include_directories(wvw PRIVATE src)
    target_link_libraries(wvw ole32 oleaut32 uuid user32 gdi32 shell32)

    add_library(wvw_static STATIC src/libwvw.c)
    set_target_properties(wvw_static PROPERTIES OUTPUT_NAME "wvw")
    target_include_directories(wvw_static PRIVATE src)
    target_link_libraries(wvw_static ole32 oleaut32 uuid user32 gdi32 shell32)

    add_library(wvw_shared SHARED src/libwvw.c)
    set_target_properties(wvw_shared PROPERTIES OUTPUT_NAME "wvw")
    target_include_directories(wvw_shared PRIVATE src)
    target_link_libraries(wvw_shared ole32 oleaut32 uuid user32 gdi32 shell32)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    pkg_check_modules(WEBKIT REQUIRED webkit2gtk-4.1)

    add_executable(wvw ${WVW_SOURCES})
    target_include_directories(wvw PRIVATE src ${GTK3_INCLUDE_DIRS} ${WEBKIT_INCLUDE_DIRS})
    target_compile_options(wvw PRIVATE ${GTK3_CFLAGS_OTHER} ${WEBKIT_CFLAGS_OTHER})
    target_link_directories(wvw PRIVATE ${GTK3_LIBRARY_DIRS} ${WEBKIT_LIBRARY_DIRS})
    target_link_libraries(wvw ${GTK3_LIBRARIES} ${WEBKIT_LIBRARIES} pthread m)

    add_library(wvw_static STATIC src/libwvw.c)
    set_target_properties(wvw_static PROPERTIES OUTPUT_NAME "wvw")
    target_include_directories(wvw_static PRIVATE src ${GTK3_INCLUDE_DIRS} ${WEBKIT_INCLUDE_DIRS})
    target_compile_options(wvw_static PRIVATE ${GTK3_CFLAGS_OTHER} ${WEBKIT_CFLAGS_OTHER})
    target_link_directories(wvw_static PRIVATE ${GTK3_LIBRARY_DIRS} ${WEBKIT_LIBRARY_DIRS})
    target_link_libraries(wvw_static ${GTK3_LIBRARIES} ${WEBKIT_LIBRARIES} pthread m)

    add_library(wvw_shared SHARED src/libwvw.c)
    set_target_properties(wvw_shared PROPERTIES OUTPUT_NAME "wvw")
    target_include_directories(wvw_shared PRIVATE src ${GTK3_INCLUDE_DIRS} ${WEBKIT_INCLUDE_DIRS})
    target_compile_options(wvw_shared PRIVATE ${GTK3_CFLAGS_OTHER} ${WEBKIT_CFLAGS_OTHER})
    target_link_directories(wvw_shared PRIVATE ${GTK3_LIBRARY_DIRS} ${WEBKIT_LIBRARY_DIRS})
    target_link_libraries(wvw_shared ${GTK3_LIBRARIES} ${WEBKIT_LIBRARIES} pthread m)
endif()
