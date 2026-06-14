include_guard()

include(Variables)

# Build options shared by all viewer components

# Static library linking
set(LLCOMMON_LINK_SHARED OFF)

# FMOD Studio audio support
set(BUILD_WITH_FMODSTUDIO ON)

# VLC plugin (disabled)
set(BUILD_VLC_PLUGIN OFF)

# Web browser plugin - require at least one
if(NOT BUILD_CEF_PLUGIN AND NOT BUILD_WEBKIT_PLUGIN)
    set(BUILD_CEF_PLUGIN ON)
endif()


