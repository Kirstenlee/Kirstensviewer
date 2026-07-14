# -*- cmake -*-
#
# Compared to other libraries, compiling this one is a mess.  The
# reason is that we have several source files that have two different
# sets of behaviour, depending on whether they're intended to be part
# of the viewer or the map server.
#
# Unfortunately, the affected code is a rat's nest of #ifdefs, so it's
# easier to play compilation tricks than to actually fix the problem.

project(llwindow)

include(00-Common)
include(DragDrop)
include(LLCommon)
include(LLImage)
include(LLMath)
include(LLRender)
include(LLFileSystem)
include(LLWindow)
include(LLXML)
include(UI)
if(DX_RENDER)
    include(DXRender)
endif()

include_directories(
    ${LLCOMMON_INCLUDE_DIRS}
    ${LLIMAGE_INCLUDE_DIRS}
    ${LLMATH_INCLUDE_DIRS}
    ${LLRENDER_INCLUDE_DIRS}
    ${LLFILESYSTEM_INCLUDE_DIRS}
    ${LLWINDOW_INCLUDE_DIRS}
    ${LLXML_INCLUDE_DIRS}
    )
include_directories(SYSTEM
    ${LLCOMMON_SYSTEM_INCLUDE_DIRS}
    ${LLXML_SYSTEM_INCLUDE_DIRS}
    )

if(DX_RENDER)
  include_directories(${DXRENDER_INCLUDE_DIRS})
endif()

set(llwindow_SOURCE_FILES
    llcursortypes.cpp
    llkeyboard.cpp
    llkeyboardheadless.cpp
    llwindowheadless.cpp
    llwindowcallbacks.cpp
    llwindow.cpp
    )

set(llwindow_HEADER_FILES
    CMakeLists.txt

    llcursortypes.h
    llkeyboard.h
    llkeyboardheadless.h
    llwindowheadless.h
    llwindowcallbacks.h
    )

set(viewer_SOURCE_FILES
    llmousehandler.cpp
    )

set(viewer_HEADER_FILES
    llwindow.h
    llpreeditor.h
    llmousehandler.h
    )

# Windows-specific sources
list(APPEND llwindow_SOURCE_FILES
     AresWin32.cpp
     llwindowwin32.cpp
     lldxhardware.cpp
     llkeyboardwin32.cpp
     lldragdropwin32.cpp
     )
list(APPEND llwindow_HEADER_FILES
     AresWin32.h
     llwindowwin32.h
     lldxhardware.h
     llkeyboardwin32.h
     lldragdropwin32.h
     )
list(APPEND llwindow_LINK_LIBRARIES
     comdlg32     # Common Dialogs for ChooseColor
     ole32
     dxgi
     d3d9
     )

set_source_files_properties(${llwindow_HEADER_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

list(APPEND llwindow_SOURCE_FILES ${llwindow_HEADER_FILES})
list(APPEND viewer_SOURCE_FILES ${viewer_HEADER_FILES})

add_library (llwindow
  ${llwindow_SOURCE_FILES}
  ${viewer_SOURCE_FILES}
  )

target_link_libraries (llwindow ${llwindow_LINK_LIBRARIES})
if(DX_RENDER)
  target_link_libraries(llwindow ${DXRENDER_LIBRARIES})
endif()
target_compile_options(llwindow PRIVATE "/Zc:wchar_t-")

