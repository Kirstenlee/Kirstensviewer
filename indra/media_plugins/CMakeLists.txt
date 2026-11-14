# -*- cmake -*-

add_subdirectory(base)



if (WINDOWS)
    add_subdirectory(cef)
    add_subdirectory(webview2)
    add_subdirectory(libvlc)
endif (WINDOWS)
