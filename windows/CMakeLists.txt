cmake_minimum_required(VERSION 3.18)  # file(ARCHIVE_EXTRACT) for the WebView2 package
project(cronq_windows CXX)

if(NOT WIN32)
    message(FATAL_ERROR "windows/ targets Windows (MSVC or a MinGW / llvm-mingw cross compiler).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Version configuration from windows/VERSION
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" CRONQ_VERSION)
string(STRIP "${CRONQ_VERSION}" CRONQ_VERSION)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/version.h"
    @ONLY
)

# Static CRT so cronq.exe runs on a clean machine without the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CORE_SOURCES
    src/common.cpp
    src/crash_handler.cpp
    src/geometry.cpp
    src/log_rotate.cpp
    src/config.cpp
    src/input.cpp
    src/input_logic.cpp
    src/lock_logic.cpp
    src/theme.cpp
    src/theme_logic.cpp
    src/killswitch.cpp
    src/image_logic.cpp
    src/image_codec.cpp
    src/overlay.cpp
    src/raise.cpp
    src/hotkey.cpp
    src/ipc.cpp
    src/app.cpp
    src/browser_logic.cpp
    src/jsonlite.cpp
    src/browser.cpp
    src/ui_bridge.cpp
    src/focus_guard.cpp
    src/ghost.cpp
    src/ghost_logic.cpp
    src/ini_writer.cpp
    src/key_translate.cpp
    src/snap.cpp
    src/snap_logic.cpp
    src/purge.cpp
    src/purge_logic.cpp
    src/update.cpp
    src/update_logic.cpp
)

add_library(cronq_core STATIC ${CORE_SOURCES})
target_include_directories(cronq_core PUBLIC src "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(cronq_core PUBLIC user32 gdi32 dwmapi shell32 advapi32 ole32 oleaut32 wtsapi32 windowscodecs winhttp bcrypt)
target_compile_definitions(cronq_core PUBLIC UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)

# cronq.exe  - console build: the command line tool.
add_executable(cronq src/main.cpp)
target_compile_definitions(cronq PRIVATE CRONQ_CONSOLE)
target_link_libraries(cronq PRIVATE cronq_core)

# cronqw.exe - windowless build: the overlay process (autostart, Start Menu, supervisor).
add_executable(cronqw WIN32 src/main.cpp)
target_link_libraries(cronqw PRIVATE cronq_core)

if(MSVC)
    foreach(t cronq_core cronq cronqw)
        target_compile_options(${t} PRIVATE /W3 /utf-8 /permissive-)
    endforeach()
    # main() is the entry point of the windowless build too.
    set_target_properties(cronqw PROPERTIES LINK_FLAGS "/ENTRY:mainCRTStartup")
else()
    foreach(t cronq_core cronq cronqw)
        target_compile_options(${t} PRIVATE -Wall -Wextra)
    endforeach()
endif()

# Dev tool for exercising the hotkey hook: sendkeys alt+x
add_executable(sendkeys tests/sendkeys.cpp)
target_link_libraries(sendkeys PRIVATE user32)

# ---- WebView2 (Phase 2). Can be disabled for a shell-only build.
option(CRONQ_WEBVIEW2 "Build with WebView2 support (downloads the SDK from NuGet)" ON)
if(CRONQ_WEBVIEW2)
    include(cmake/FetchWebView2.cmake)
    target_include_directories(cronq_core PUBLIC "${WEBVIEW2_INCLUDE_DIR}")
    if(NOT MSVC)
        target_include_directories(cronq_core PUBLIC compat)  # EventToken.h shim
    endif()
    target_compile_definitions(cronq_core PUBLIC CRONQ_WEBVIEW2=1)
    target_link_libraries(cronq_core PUBLIC ole32 oleaut32)
    foreach(t cronq cronqw)
        add_custom_command(TARGET ${t} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${WEBVIEW2_LOADER_DLL}" "$<TARGET_FILE_DIR:${t}>")
    endforeach()
    add_executable(webview2_compile_check tests/webview2_compile_check.cpp)
    target_link_libraries(webview2_compile_check PRIVATE cronq_core)
    add_executable(browser_runtime_test tests/browser_runtime_test.cpp)
    add_executable(theme_runtime_test tests/theme_runtime_test.cpp)
    target_link_libraries(theme_runtime_test PRIVATE cronq_core)
    add_dependencies(theme_runtime_test cronq_ui)
    if(MSVC)
        target_compile_options(theme_runtime_test PRIVATE /utf-8)
    else()
        target_link_options(theme_runtime_test PRIVATE -static)
    endif()
    target_link_libraries(browser_runtime_test PRIVATE cronq_core shlwapi)
    if(MSVC)
        target_compile_options(browser_runtime_test PRIVATE /utf-8)
    else()
        target_link_options(browser_runtime_test PRIVATE -static)
    endif()
    add_dependencies(browser_runtime_test cronq_ui)
    if(NOT MSVC)
        target_link_options(webview2_compile_check PRIVATE -static)
    endif()
endif()

# DECISION: one asset-copy target avoids races when both executables build in parallel,
# and refreshes HTML/CSS even when no C++ file needs relinking.
set(CRONQ_RUNTIME_DIR "${CMAKE_CURRENT_BINARY_DIR}")
if(CMAKE_CONFIGURATION_TYPES)
    string(APPEND CRONQ_RUNTIME_DIR "/$<CONFIG>")
endif()
add_custom_target(cronq_ui ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/ui" "${CRONQ_RUNTIME_DIR}/ui")
add_dependencies(cronq cronq_ui)
add_dependencies(cronqw cronq_ui)

# Optional UIAccess build (Task 4.4)
option(CRONQ_UIACCESS "Build UIAccess overlay executable" OFF)
if(CRONQ_UIACCESS)
    add_executable(cronqw_uia WIN32 src/main.cpp)
    target_link_libraries(cronqw_uia PRIVATE cronq_core)
    target_compile_definitions(cronqw_uia PRIVATE CRONQ_UIACCESS=1)
    add_dependencies(cronqw_uia cronq_ui)
    if(CRONQ_WEBVIEW2)
        add_custom_command(TARGET cronqw_uia POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${WEBVIEW2_LOADER_DLL}" "$<TARGET_FILE_DIR:cronqw_uia>")
    endif()
    if(MSVC)
        target_compile_options(cronqw_uia PRIVATE /W3 /utf-8 /permissive-)
        target_link_options(cronqw_uia PRIVATE /ENTRY:mainCRTStartup /MANIFEST:EMBED
            "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/installer/cronqw-uia.manifest")
    else()
        enable_language(RC)
        configure_file(installer/cronqw-uia.rc.in "${CMAKE_CURRENT_BINARY_DIR}/cronqw-uia.rc" @ONLY)
        target_sources(cronqw_uia PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/cronqw-uia.rc")
        target_compile_options(cronqw_uia PRIVATE -Wall -Wextra)
        target_link_options(cronqw_uia PRIVATE -static)
    endif()
endif()

add_executable(runtime_test tests/runtime_test.cpp)
target_link_libraries(runtime_test PRIVATE cronq_core psapi)
if(NOT MSVC)
    target_link_options(runtime_test PRIVATE -static)
endif()

enable_testing()
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME config_key_parity COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/config_key_parity.py")
endif()
add_executable(config_test tests/config_test.cpp)
target_link_libraries(config_test PRIVATE cronq_core)
set(CRONQ_FIXTURE "${CMAKE_CURRENT_SOURCE_DIR}/../shared/fixtures/config_defaults.ini")
if(CMAKE_CROSSCOMPILING AND NOT CMAKE_HOST_WIN32)
    set(CRONQ_FIXTURE "Z:${CRONQ_FIXTURE}")
endif()
add_test(NAME config_parser COMMAND config_test "${CRONQ_FIXTURE}")

foreach(t browser_logic_test jsonlite_test log_rotate_test geometry_test ghost_logic_test snap_logic_test ini_writer_test key_translate_test input_logic_test lock_logic_test theme_logic_test image_logic_test image_codec_test crash_report_test recovery_logic_test purge_logic_test update_logic_test)
    add_executable(${t} tests/${t}.cpp)
    target_link_libraries(${t} PRIVATE cronq_core)
    if(MSVC)
        target_compile_options(${t} PRIVATE /W3 /utf-8 /permissive-)
    else()
        target_compile_options(${t} PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME ${t} COMMAND ${t})
endforeach()

add_executable(parity_config_test tests/parity_config_test.cpp)
target_link_libraries(parity_config_test PRIVATE cronq_core)
if(MSVC)
    target_compile_options(parity_config_test PRIVATE /W3 /utf-8 /permissive-)
else()
    target_compile_options(parity_config_test PRIVATE -Wall -Wextra)
endif()
add_test(NAME parity_config
         COMMAND parity_config_test "${CRONQ_FIXTURE}")

# MinGW / llvm-mingw: link every executable statically so there are no libc++/libgcc DLLs to ship or lose.
if(NOT MSVC)
    foreach(t cronq cronqw sendkeys config_test browser_logic_test jsonlite_test log_rotate_test geometry_test parity_config_test ghost_logic_test snap_logic_test ini_writer_test key_translate_test input_logic_test lock_logic_test theme_logic_test image_logic_test image_codec_test crash_report_test recovery_logic_test purge_logic_test update_logic_test)
        target_link_options(${t} PRIVATE -static)
    endforeach()
endif()
