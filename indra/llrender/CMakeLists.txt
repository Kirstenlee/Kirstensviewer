# -*- cmake -*-

project(llrender)

include(00-Common)
include(OpenGL)
include(FreeType)
include(LLCommon)
include(LLImage)
include(LLMath)
include(LLRender)
include(LLWindow)
include(LLXML)
include(LLFileSystem)
if(DX_RENDER)
    include(DXRender)
endif()

include_directories(
    ${FREETYPE_INCLUDE_DIRS}
    ${LLCOMMON_INCLUDE_DIRS}
    ${LLIMAGE_INCLUDE_DIRS}
    ${LLMATH_INCLUDE_DIRS}
    ${LLRENDER_INCLUDE_DIRS}
    ${LLFILESYSTEM_INCLUDE_DIRS}
    ${LLWINDOW_INCLUDE_DIRS}
    ${LLXML_INCLUDE_DIRS}
    )
if(DX_RENDER)
    include_directories(${DXRENDER_INCLUDE_DIRS})
endif()
include_directories(SYSTEM
    ${LLCOMMON_SYSTEM_INCLUDE_DIRS}
    ${LLXML_SYSTEM_INCLUDE_DIRS}
    )

set(llrender_SOURCE_FILES
    llfontbitmapcache.cpp
    llfontfreetype.cpp
    llfontfreetypesvg.cpp
    llfontgl.cpp
    llfontvertexbuffer.cpp
    llfontregistry.cpp
    llgl.cpp
    llhlslshader.cpp
    llgltexture.cpp
    llimagegl.cpp
    llpostprocess.cpp
    llrender.cpp
    llrender2dutils.cpp
    llrendernavprim.cpp
    llrendersphere.cpp
    llrendertarget.cpp
    llshadermgr.cpp
    lltexture.cpp
    lltexturemanagerbridge.cpp
    lluiimage.cpp
    llvertexbuffer.cpp
    llglcommonfunc.cpp
    DXCubeMap.cpp
    DXCubeMapArray.cpp
    )
    
set(llrender_HEADER_FILES
    CMakeLists.txt

    llfontgl.h
    llfontvertexbuffer.h
    llfontfreetype.h
    llfontfreetypesvg.h
    llfontbitmapcache.h
    llfontregistry.h
    llgl.h
    llglheaders.h
    llhlslshader.h
    llglstates.h
    llgltexture.h
    llgltypes.h
    llimagegl.h
    llpostprocess.h
    llrender.h
    llrender2dutils.h
    llrendernavprim.h
    llrendersphere.h
    llshadermgr.h
    lltexture.h
    lltexturemanagerbridge.h
    lluiimage.h
    lluiimage.inl
    llvertexbuffer.h
    llglcommonfunc.h
    DXCubeMap.h
    DXCubeMapArray.h
    )

set_source_files_properties(${llrender_HEADER_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

list(APPEND llrender_SOURCE_FILES ${llrender_HEADER_FILES})

add_library (llrender ${llrender_SOURCE_FILES})

# Libraries on which this library depends,
# Sort by high-level to low-level
target_link_libraries(llrender
      ${LLCOMMON_LIBRARIES}
      ${LLIMAGE_LIBRARIES}
      ${LLMATH_LIBRARIES}
      ${LLFILESYSTEM_LIBRARIES}
      ${LLXML_LIBRARIES}
      ${LLWINDOW_LIBRARIES}
      ${FREETYPE_LIBRARIES}
)
if(DX_RENDER)
  target_link_libraries(llrender ${DXRENDER_LIBRARIES})
else()
  # S24 (DX_RENDER, 2026-07-24): only linked for the GL build. A DX_RENDER=ON
  # build deliberately does NOT link OpenGL at all - any raw gl*() call left
  # unconverted anywhere in the tree (this static lib doesn't need its own
  # symbols resolved, but the final viewer executable does - see the matching
  # newview/CMakeLists.txt change) becomes an unresolved external symbol at
  # final link time instead of a silent no-op/crash at runtime. This is the
  # "flip GL off via CMake/linker to reveal stragglers" technique agreed
  # earlier in the dxrender open-issues ledger - implemented for real here.
  target_link_libraries(llrender ${OPENGL_LIBRARIES})
endif()
target_compile_options(llrender PRIVATE "/Zc:wchar_t-")
