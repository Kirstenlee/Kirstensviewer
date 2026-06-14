include_guard()

include(Variables)
include(GlExt)

set(LLWINDOW_INCLUDE_DIRS
    ${GLEXT_INCLUDE_DIR}
    ${LIBS_OPEN_DIR}/llwindow
)

set(LLWINDOW_LIBRARIES
    llwindow
)

if(BUILD_HEADLESS)
    set(LLWINDOW_HEADLESS_LIBRARIES
        llwindowheadless
    )
endif()