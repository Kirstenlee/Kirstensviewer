# -*- cmake -*-
if (LIBVLCPLUGIN_CMAKE_INCLUDED)
  return()
endif (LIBVLCPLUGIN_CMAKE_INCLUDED)
set (LIBVLCPLUGIN_CMAKE_INCLUDED TRUE)

include(Linking)
include(Prebuilt)

use_prebuilt_binary(vlc-bin)

    set(LIBVLCPLUGIN ON CACHE BOOL
        "LIBVLCPLUGIN support for the llplugin/llmedia test apps.")
        set(VLC_INCLUDE_DIR ${LIBS_PREBUILT_DIR}/include/vlc)


if (WINDOWS)
    set(VLC_PLUGIN_LIBRARIES
        libvlc.lib
        libvlccore.lib
    )
endif (WINDOWS)
