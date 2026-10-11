# -*- cmake -*-

# S24: optional DXVK (Vulkan-translation) backend for Direct3D 11/DXGI.
# Two tiny proxy DLLs, each shipped as the install-root replacement for a
# real system DLL (d3d11.dll / dxgi.dll). At runtime each forwards its
# exports to either the genuine Windows copy or the bundled DXVK one
# (newview/vc145/DXVK, copied to S24-Packaged/dxvk by Invoke-Package.ps1),
# chosen by the presence of a marker file the viewer itself writes when
# RenderUseDXVK is toggled - see llviewercontrol.cpp. Deliberately not
# linked against the real d3d11.lib/dxgi.lib import libraries: every real
# function is reached only through a GetProcAddress'd pointer, resolved at
# runtime, never through a direct symbol reference.

project(dxvkproxy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(00-Common)

add_library(dxvkproxy_d3d11 SHARED
    d3d11_proxy.cpp
    dxvkproxy_common.h
    d3d11_proxy.def
    )
set_target_properties(dxvkproxy_d3d11 PROPERTIES OUTPUT_NAME "d3d11")

add_library(dxvkproxy_dxgi SHARED
    dxgi_proxy.cpp
    dxvkproxy_common.h
    dxgi_proxy.def
    )
set_target_properties(dxvkproxy_dxgi PROPERTIES OUTPUT_NAME "dxgi")
