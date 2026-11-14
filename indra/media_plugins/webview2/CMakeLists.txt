# -*- cmake -*-

project(media_plugin_webview2)

include(Boost)
include(00-Common)
include(LLCommon)
include(LLPlugin)
include(LLMath)
include(LLWindow)
include(Linking)
include(PluginAPI)
include(MediaPluginBase)
include(DXRender)

include(WebView2Plugin)

include_directories(
    ${LLPLUGIN_INCLUDE_DIRS}
    ${MEDIA_PLUGIN_BASE_INCLUDE_DIRS}
    ${LLCOMMON_INCLUDE_DIRS}
    ${LLMATH_INCLUDE_DIRS}
    ${DXRENDER_INCLUDE_DIRS}
    ${LLWINDOW_INCLUDE_DIRS}
    ${WEBVIEW2_INCLUDE_DIR}
)
include_directories(SYSTEM
    ${LLCOMMON_SYSTEM_INCLUDE_DIRS}
    )

### media_plugin_webview2

# webview2_host.cpp holds all the WinRT / WebView2 / D3D11 code and deliberately includes no
# Linden headers; media_plugin_webview2.cpp is the thin plugin-protocol layer over it.
set(media_plugin_webview2_SOURCE_FILES
    media_plugin_webview2.cpp
    webview2_host.cpp
    )

set(media_plugin_webview2_HEADER_FILES
    webview2_host.h
    )

set_source_files_properties(${media_plugin_webview2_HEADER_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

list(APPEND media_plugin_webview2_SOURCE_FILES ${media_plugin_webview2_HEADER_FILES})

add_library(media_plugin_webview2
    SHARED
    ${media_plugin_webview2_SOURCE_FILES}
    )

target_link_libraries(media_plugin_webview2
  ${LLPLUGIN_LIBRARIES}
  ${MEDIA_PLUGIN_BASE_LIBRARIES}
  ${LLCOMMON_LIBRARIES}
  ${PLUGIN_API_WINDOWS_LIBRARIES}
  ${WEBVIEW2_LIBRARIES}
  shell32
  rpcrt4
  winmm
  ${DXRENDER_LIBRARIES}
)

# Unlike the other plugins, no /Zc:wchar_t- here: the WinRT and WebView2 headers need native wchar_t.
# Nothing in this target calls an llcommon function that takes wide characters.

if (WINDOWS)
  set_target_properties(
    media_plugin_webview2
    PROPERTIES
    LINK_FLAGS "/MANIFEST:NO /SAFESEH:NO /NODEFAULTLIB:LIBCMT /IGNORE:4099"
    LINK_FLAGS_DEBUG "/MANIFEST:NO /SAFESEH:NO /NODEFAULTLIB:LIBCMTD /IGNORE:4099"
    )
  target_link_options(media_plugin_webview2 PRIVATE $<$<CONFIG:Release>:/LTCG>)
endif (WINDOWS)
