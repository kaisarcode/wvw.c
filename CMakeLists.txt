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
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    if(LIBWVW_NATIVE)
        add_compile_options(-march=native)
    endif()
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

if(NOT WVW_TEST_EXTERNAL_ARTIFACTS)
    if(WIN32)
        set(WVW_SOURCES src/libwvw.c)
        set(WVW_WEBVIEW2_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/webview2/include")
        set(WVW_WEBVIEW2_LOADER "${CMAKE_CURRENT_SOURCE_DIR}/lib/webview2/bin/x86_64/WebView2Loader.dll")

        set_source_files_properties(src/libwvw.c PROPERTIES
            COMPILE_DEFINITIONS KC_WVW_BUILD_VERSION=${WVW_BUILD_VERSION}
        )

        add_executable(wvw src/wvw.c ${WVW_SOURCES})
        target_include_directories(wvw PRIVATE src)
        target_include_directories(wvw SYSTEM PRIVATE ${WVW_WEBVIEW2_INCLUDE_DIR})
        target_link_libraries(wvw ole32 uuid user32 gdi32 shell32)

        add_library(wvw_static STATIC ${WVW_SOURCES})
        set_target_properties(wvw_static PROPERTIES OUTPUT_NAME "wvw")
        target_include_directories(wvw_static PRIVATE src)
        target_include_directories(wvw_static SYSTEM PRIVATE ${WVW_WEBVIEW2_INCLUDE_DIR})
        target_link_libraries(wvw_static ole32 uuid user32 gdi32 shell32)

        add_library(wvw_shared SHARED ${WVW_SOURCES})
        set_target_properties(wvw_shared PROPERTIES OUTPUT_NAME "wvw")
        set_target_properties(wvw_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
        target_include_directories(wvw_shared PRIVATE src)
        target_include_directories(wvw_shared SYSTEM PRIVATE ${WVW_WEBVIEW2_INCLUDE_DIR})
        target_link_libraries(wvw_shared ole32 uuid user32 gdi32 shell32)
    else()
        set(WVW_SOURCES src/libwvw.c)

        set_source_files_properties(src/libwvw.c PROPERTIES
            COMPILE_DEFINITIONS KC_WVW_BUILD_VERSION=${WVW_BUILD_VERSION}
        )

        if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            set(WVW_SOURCES src/macos.m)
            set_source_files_properties(src/libwvw.c PROPERTIES LANGUAGE OBJC)
            add_executable(wvw src/wvw.c ${WVW_SOURCES})
            target_include_directories(wvw PRIVATE src)
            target_link_libraries(wvw "-framework WebKit" "-framework Cocoa" "-framework Foundation" pthread m)

            add_library(wvw_static STATIC ${WVW_SOURCES})
            set_target_properties(wvw_static PROPERTIES OUTPUT_NAME "wvw")
            target_include_directories(wvw_static PRIVATE src)
            target_link_libraries(wvw_static pthread m)

            add_library(wvw_shared SHARED ${WVW_SOURCES})
            set_target_properties(wvw_shared PROPERTIES OUTPUT_NAME "wvw")
            set_target_properties(wvw_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
            target_include_directories(wvw_shared PRIVATE src)
            target_link_libraries(wvw_shared "-framework WebKit" "-framework Cocoa" "-framework Foundation" pthread m)
        else()
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
        set_target_properties(wvw_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
        target_include_directories(wvw_shared PRIVATE src ${GTK3_INCLUDE_DIRS} ${WEBKIT_INCLUDE_DIRS})
        target_compile_options(wvw_shared PRIVATE ${GTK3_CFLAGS_OTHER} ${WEBKIT_CFLAGS_OTHER})
        target_link_directories(wvw_shared PRIVATE ${GTK3_LIBRARY_DIRS} ${WEBKIT_LIBRARY_DIRS})
        target_link_libraries(wvw_shared ${GTK3_LIBRARIES} ${WEBKIT_LIBRARIES} pthread m)
        endif()
    endif()
endif()

if(WVW_BUILD_TESTS)
    enable_testing()

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
    target_include_directories(wvw_contract_test PRIVATE src)
    target_link_libraries(wvw_contract_test PRIVATE ${WVW_TEST_SHARED_TARGET})

    if(UNIX AND NOT ANDROID AND NOT APPLE)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GTK3_TEST gtk+-3.0)
        pkg_check_modules(WEBKIT_TEST webkit2gtk-4.1)
        target_include_directories(wvw_contract_test PRIVATE ${GTK3_TEST_INCLUDE_DIRS} ${WEBKIT_TEST_INCLUDE_DIRS})
        target_compile_options(wvw_contract_test PRIVATE ${GTK3_TEST_CFLAGS_OTHER} ${WEBKIT_TEST_CFLAGS_OTHER})
        target_link_directories(wvw_contract_test PRIVATE ${GTK3_TEST_LIBRARY_DIRS} ${WEBKIT_TEST_LIBRARY_DIRS})
        target_link_libraries(wvw_contract_test PRIVATE ${GTK3_TEST_LIBRARIES} ${WEBKIT_TEST_LIBRARIES} pthread m)
    endif()

    if(WIN32)
        target_link_libraries(wvw_contract_test PRIVATE ole32 uuid user32 gdi32 shell32)
    endif()

    add_custom_command(TARGET wvw_contract_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${WVW_TEST_SHARED_TARGET}>
            $<TARGET_FILE_DIR:wvw_contract_test>
    )

    add_test(NAME library-version COMMAND wvw_contract_test version)
    add_test(NAME library-options-default COMMAND wvw_contract_test options-default)
    add_test(NAME library-options-load-env COMMAND wvw_contract_test options-load-env)
    add_test(NAME library-options-free COMMAND wvw_contract_test options-free)
    add_test(NAME library-open COMMAND wvw_contract_test open)
    add_test(NAME library-close COMMAND wvw_contract_test close)
    add_test(NAME library-stop COMMAND wvw_contract_test stop)
    add_test(NAME library-navigate COMMAND wvw_contract_test navigate)
    add_test(NAME library-enable-bridge COMMAND wvw_contract_test enable-bridge)
    add_test(NAME library-bridge-empty-registry COMMAND wvw_contract_test bridge-empty-registry)
    add_test(NAME library-bridge-negative-method-count COMMAND wvw_contract_test bridge-negative-method-count)
    add_test(NAME library-bridge-positive-without-methods COMMAND wvw_contract_test bridge-positive-without-methods)
    add_test(NAME library-bridge-positive-without-callback COMMAND wvw_contract_test bridge-positive-without-callback)
    add_test(NAME library-post-bridge-event COMMAND wvw_contract_test post-bridge-event)
    add_test(NAME library-tray-init COMMAND wvw_contract_test tray-init)
    add_test(NAME library-tray-remove COMMAND wvw_contract_test tray-remove)
    add_test(NAME library-hide COMMAND wvw_contract_test hide)
    add_test(NAME library-show COMMAND wvw_contract_test show)
    add_test(NAME library-minimize COMMAND wvw_contract_test minimize)
    add_test(NAME library-tray-set-menu COMMAND wvw_contract_test tray-set-menu)
    add_test(NAME library-maximize COMMAND wvw_contract_test maximize)
    add_test(NAME library-restore COMMAND wvw_contract_test restore)
    add_test(NAME library-set-title COMMAND wvw_contract_test set-title)
    add_test(NAME library-set-size COMMAND wvw_contract_test set-size)
    add_test(NAME library-get-state COMMAND wvw_contract_test get-state)
endif()
