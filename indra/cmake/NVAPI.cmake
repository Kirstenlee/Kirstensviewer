# -*- cmake -*-
include_guard()
include(Prebuilt)

set(NVAPI ON CACHE BOOL "Use NVAPI.")

if(NVAPI)
    add_library(ll::nvapi INTERFACE IMPORTED)
    use_prebuilt_binary(nvapi)
    set(NVAPI_LIBRARY nvapi)
endif()