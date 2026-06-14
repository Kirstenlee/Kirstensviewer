# -*- cmake -*-

project(llprimitive)

include(00-Common)
include(LLCommon)
include(LLCharacter)
include(LLCoreHttp)
include(LLMath)
include(LLMessage)
include(LLPrimitive)
include(Glm)
include(TinyGLTF)
include(LLConvexDecomposition)
include(Linking)

include_directories(
    ${LLCOMMON_INCLUDE_DIRS}
    ${LLMATH_INCLUDE_DIRS}
    ${LLCONVEXDECOMPOSITION_INCLUDE_DIRS}
    ${LLMESSAGE_INCLUDE_DIRS}
    ${LLXML_INCLUDE_DIRS}
    ${LLRENDER_INCLUDE_DIRS}
    ${LLPRIMITIVE_INCLUDE_DIRS}
    ${LLCHARACTER_INCLUDE_DIRS}
    )
include_directories(SYSTEM
    ${LLCOMMON_SYSTEM_INCLUDE_DIRS}
    ${LLXML_SYSTEM_INCLUDE_DIRS}
    )

set(llprimitive_SOURCE_FILES
    lldaeloader.cpp
    llgltfmaterial.cpp
    llmaterialid.cpp
    llmaterial.cpp
    llmaterialtable.cpp
    llmediaentry.cpp
    llmodel.cpp
    llmodelloader.cpp
    llprimitive.cpp
    llprimtexturelist.cpp
    lltextureanim.cpp
    lltextureentry.cpp
    lltreeparams.cpp
    llvolumemessage.cpp
    material_codes.cpp
    )

set(llprimitive_HEADER_FILES
    CMakeLists.txt
    lldaeloader.h
    llgltfmaterial.h
    llgltfmaterial_templates.h
    legacy_object_types.h
    llmaterial.h
    llmaterialid.h
    llmaterialtable.h
    llmediaentry.h
    llmodel.h
    llmodelloader.h
    llprimitive.h
    llprimtexturelist.h
    lllslconstants.h
    lltextureanim.h
    lltextureentry.h
    lltreeparams.h
    lltree_common.h
    llvolumemessage.h
    material_codes.h
    object_flags.h
    )
        
set_source_files_properties(${llprimitive_HEADER_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

list(APPEND llprimitive_SOURCE_FILES ${llprimitive_HEADER_FILES})

add_library (llprimitive ${llprimitive_SOURCE_FILES})

# Ensure the llprimitive target gets the convex-decomposition include dir
target_include_directories(llprimitive PRIVATE ${LLCONVEXDECOMPOSITION_INCLUDE_DIRS})

target_link_libraries(llprimitive
    ${LLCOMMON_LIBRARIES}
    ${LLMATH_LIBRARIES}
    ${LLCONVEXDECOMPOSITION_LIBRARIES}
    ${LLMESSAGE_LIBRARIES}
    ${LLCOREHTTP_LIBRARIES}
    ${LLXML_LIBRARIES}
    ${LLCHARACTER_LIBRARIES}
    ${LLRENDER_LIBRARIES}
    ${LLPRIMITIVE_LINK_LIBRARIES}
    )
