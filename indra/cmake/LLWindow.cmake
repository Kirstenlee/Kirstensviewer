include_guard()

include(Variables)

set(LLWINDOW_INCLUDE_DIRS
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