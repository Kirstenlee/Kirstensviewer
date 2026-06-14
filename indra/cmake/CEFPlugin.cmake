# -*- cmake -*-
include(Linking)
include(Prebuilt)

include_guard()
add_library( ll::cef INTERFACE IMPORTED )

use_prebuilt_binary(dullahan)
    set(CEF_INCLUDE_DIR ${LIBS_PREBUILT_DIR}/include/cef)


    set(CEF_PLUGIN_LIBRARIES
        libcef.lib
        libcef_dll_wrapper.lib
        dullahan.lib
    )

