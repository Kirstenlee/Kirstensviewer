# -*- cmake -*-

project(krlv)

# -------------------------------------------------------------------------
# KRLV - self-contained RLVa implementation. See krlv/README.md for the
# design note and the full list of touchpoints this module has into the
# rest of the viewer. Modeled on dxrender's own CMakeLists.txt.
# -------------------------------------------------------------------------

include(00-Common)
include(LLCommon)
include(LLFileSystem)
include(LLMath)
include(LLXML)
include(LLUI)

include_directories(
    ${LLCOMMON_INCLUDE_DIRS}
    ${LLFILESYSTEM_INCLUDE_DIRS}
    ${LLMATH_INCLUDE_DIRS}
    ${LLXML_INCLUDE_DIRS}
    ${LLUI_INCLUDE_DIRS}
    # so behaviours/*.cpp can #include core/*.h and vice versa without
    # every file needing a relative "../core/" path
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/behaviours
)

include_directories(SYSTEM
    ${LLCOMMON_SYSTEM_INCLUDE_DIRS}
)

# -------------------------------------------------------------------------
# Source files
# -------------------------------------------------------------------------

set(krlv_CORE
    core/krlvcommand.cpp
    core/krlvhandler.cpp
    core/krlvpersist.cpp
    core/krlvowners.cpp
    core/krlvnotice.cpp
    core/krlvsafeword.cpp
    core/krlvintegrity.cpp
    core/krlvtamper.cpp
    core/krlvautoresponse.cpp
    core/krlvobjects.cpp
)

set(krlv_BEHAVIOURS
    behaviours/krlvversion.cpp
    behaviours/krlvblacklist.cpp
    behaviours/krlvmisc.cpp
    behaviours/krlvmovement.cpp
    behaviours/krlvcamera.cpp
    behaviours/krlvchat.cpp
    behaviours/krlvteleport.cpp
    behaviours/krlvinventory.cpp
    behaviours/krlvsitting.cpp
    behaviours/krlvattachment.cpp
    behaviours/krlvsharedfolders.cpp
    behaviours/krlvtouch.cpp
    behaviours/krlvlocation.cpp
    behaviours/krlvname.cpp
    behaviours/krlvgroup.cpp
    behaviours/krlvviewercontrol.cpp
    behaviours/krlvunofficial.cpp
)

set(krlv_HEADER_FILES
    core/krlvcommand.h
    core/krlvhandler.h
    core/krlvpersist.h
    core/krlvowners.h
    core/krlvnotice.h
    core/krlvsafeword.h
    core/krlvintegrity.h
    core/krlvtamper.h
    core/krlvstatus.h
    core/krlvautoresponse.h
    core/krlvobjects.h
    core/krlvrestriction.h

    behaviours/krlvbehaviours.h
    behaviours/krlvversion.h
    behaviours/krlvcamera.h
    behaviours/krlvteleport.h
    behaviours/krlvchat.h
    behaviours/krlvattachment.h
    behaviours/krlvname.h
    behaviours/krlvsharedfolders.h
    behaviours/krlvtouch.h
)

set_source_files_properties(${krlv_HEADER_FILES}
    PROPERTIES HEADER_FILE_ONLY TRUE
)

set(krlv_SOURCE_FILES
    ${krlv_CORE}
    ${krlv_BEHAVIOURS}
    ${krlv_HEADER_FILES}
)

# -------------------------------------------------------------------------
# Library
# -------------------------------------------------------------------------

add_library(krlv ${krlv_SOURCE_FILES})

target_link_libraries(krlv
    ${LLCOMMON_LIBRARIES}
    ${LLFILESYSTEM_LIBRARIES}
    ${LLMATH_LIBRARIES}
    ${LLXML_LIBRARIES}
    ${LLUI_LIBRARIES}
)
