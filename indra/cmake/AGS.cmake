# -*- cmake -*-
include_guard()
include(Prebuilt)

set(AMDAGS ON CACHE BOOL "Use AMD AGS.")

if(AMDAGS)
    add_library(ll::amdags INTERFACE IMPORTED)
    use_prebuilt_binary(amdags)
    set(AMDAGS_LIBRARY amd_ags_x64_2026_MD)
endif()
