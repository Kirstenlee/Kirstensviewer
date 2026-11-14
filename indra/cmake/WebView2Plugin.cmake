# -*- cmake -*-
include_guard()

# S24 : WebView2 SDK (headers + static loader) from the vcpkg "webview2" port, placed into
# include/ and lib/release/ by scripts/DownloadAndUnpack.ps1. The engine itself is the
# Evergreen WebView2 runtime that ships with Windows.
set(WEBVIEW2_INCLUDE_DIR ${LIBS_PREBUILT_DIR}/include)

if (WINDOWS)
    set(WEBVIEW2_LIBRARIES
        ${LIBS_PREBUILT_DIR}/lib/release/WebView2LoaderStatic.lib
        d3d11.lib
        dxgi.lib
        CoreMessaging.lib
        windowsapp.lib
        runtimeobject.lib
        shlwapi.lib
        version.lib
        ole32.lib
        oleaut32.lib
        user32.lib
        advapi32.lib
    )
endif (WINDOWS)
