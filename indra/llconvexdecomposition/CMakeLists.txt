# -*- cmake -*-

project(llconvexdecomposition)

include(00-Common)
include(LLCommon)
include(LLMath)

include_directories(
    ${LLCOMMON_INCLUDE_DIRS}
    ${LLCOMMON_SYSTEM_INCLUDE_DIRS}
    ${LLMATH_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set(llconvexdecomposition_SOURCE_FILES
    llconvexdecomposition.cpp
    llconvexdecompositionvhacd.cpp
        llpathinglib.cpp
        LLPathingLibStubImpl.cpp
        llphysicsextensions.cpp
        LLPhysicsExtensionsStubImpl.cpp
)

set(llconvexdecomposition_HEADER_FILES
    CMakeLists.txt

    llconvexdecomposition.h
    llconvexdecompositionvhacd.h
        llpathinglib.h
        LLPathingLibStubImpl.h
        llphysicsextensions.h
        LLPhysicsExtensionsStubImpl.h
	VHACD.h
)

set_source_files_properties(${llconvexdecomposition_HEADER_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

list(APPEND llconvexdecomposition_SOURCE_FILES ${llconvexdecomposition_HEADER_FILES})

add_library(llconvexdecomposition ${llconvexdecomposition_SOURCE_FILES})

target_link_libraries(llconvexdecomposition
    ${LLMATH_LIBRARIES}
    ${LLCOMMON_LIBRARIES}
)

# Expose variables for include and link like other components do
set(LLCONVEXDECOMPOSITION_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} PARENT_SCOPE)
set(LLCONVEXDECOMPOSITION_LIBRARIES llconvexdecomposition PARENT_SCOPE)
