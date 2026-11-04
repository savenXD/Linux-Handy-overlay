# Downloads the pinned Microsoft.Web.WebView2 NuGet package (a zip) into the build tree and exposes:
#   WEBVIEW2_INCLUDE_DIR   directory containing WebView2.h
#   WEBVIEW2_LOADER_DLL    x64 WebView2Loader.dll (ship it next to cronq.exe / cronqw.exe)
# If you change the version, also regenerate src/webview2_mingw_uuids.h:
#   python3 windows/tools/gen_webview2_uuids.py <WebView2.h> <version> windows/src/webview2_mingw_uuids.h
set(CRONQ_WEBVIEW2_VERSION "1.0.3967.48" CACHE STRING "Microsoft.Web.WebView2 SDK version")

set(_wv2_dir "${CMAKE_BINARY_DIR}/webview2-${CRONQ_WEBVIEW2_VERSION}")
if(NOT EXISTS "${_wv2_dir}/build/native/include/WebView2.h")
    set(_wv2_pkg "${CMAKE_BINARY_DIR}/webview2-${CRONQ_WEBVIEW2_VERSION}.nupkg")
    set(_wv2_url "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/${CRONQ_WEBVIEW2_VERSION}/microsoft.web.webview2.${CRONQ_WEBVIEW2_VERSION}.nupkg")
    message(STATUS "Downloading WebView2 SDK ${CRONQ_WEBVIEW2_VERSION} ...")
    file(DOWNLOAD "${_wv2_url}" "${_wv2_pkg}" TIMEOUT 300 STATUS _wv2_status)
    list(GET _wv2_status 0 _wv2_code)
    if(NOT _wv2_code EQUAL 0)
        message(FATAL_ERROR "Could not download ${_wv2_url}: ${_wv2_status}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${_wv2_pkg}" DESTINATION "${_wv2_dir}")
endif()

set(WEBVIEW2_INCLUDE_DIR "${_wv2_dir}/build/native/include")
set(WEBVIEW2_LOADER_DLL "${_wv2_dir}/build/native/x64/WebView2Loader.dll")
if(NOT EXISTS "${WEBVIEW2_INCLUDE_DIR}/WebView2.h" OR NOT EXISTS "${WEBVIEW2_LOADER_DLL}")
    message(FATAL_ERROR "WebView2 SDK layout not as expected under ${_wv2_dir}")
endif()
