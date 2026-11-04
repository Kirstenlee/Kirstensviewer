# -*- cmake -*-

project(dxrender)

# -------------------------------------------------------------------------
# Includes (mirroring LLRender style)
# -------------------------------------------------------------------------

include(00-Common)
include(OpenGL)
include(FreeType)
include(LLCommon)
include(LLImage)
include(LLMath)
include(LLWindow)
include(LLXML)
include(LLFileSystem)

include_directories(
    ${FREETYPE_INCLUDE_DIRS}
    ${LLCOMMON_INCLUDE_DIRS}
    ${LLIMAGE_INCLUDE_DIRS}
    ${LLMATH_INCLUDE_DIRS}
    ${LLFILESYSTEM_INCLUDE_DIRS}
    ${LLWINDOW_INCLUDE_DIRS}
    ${LLXML_INCLUDE_DIRS}
    # so resources/*.cpp can #include core/*.h (e.g. DXShader.cpp -> DXDevice.h)
    # and vice versa, without every file needing a relative "../core/" path.
    # dxrender root itself must be explicit too, not just core/resources/text -
    # root-level files could always see each other via the compiler's
    # implicit "search the including file's own directory first" behavior,
    # but that stopped covering the root once text/ moved one directory
    # deeper (e.g. text/llfontdx.cpp needing the root's llimagedx.h).
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/resources
    ${CMAKE_CURRENT_SOURCE_DIR}/text
)

include_directories(SYSTEM
    ${LLCOMMON_SYSTEM_INCLUDE_DIRS}
    ${LLXML_SYSTEM_INCLUDE_DIRS}
)

# -------------------------------------------------------------------------
# Source files
# -------------------------------------------------------------------------

set(dxrender_CORE
    core/DXDevice.cpp
    core/DXDeviceInfo.cpp
    core/DXContext.cpp
    core/DXSwapChain.cpp
    core/DXStateCache.cpp
    core/DXState.cpp
    core/DXCompositionSurface.cpp
)

set(dxrender_RESOURCES
    resources/DXBuffer.cpp
    resources/DXTexture.cpp
    resources/DXCubeTexture.cpp
    resources/DXCubeArrayTexture.cpp
    resources/DXCubeMapFaces.cpp
    resources/DXShader.cpp
    resources/DXRenderTarget.cpp
    resources/DXSampler.cpp
    resources/DXVertexLayout.cpp
    resources/DXUIBatch.cpp
    resources/DXReadback.cpp
    resources/DXRender2DUtils.cpp
    resources/DXQuery.cpp
    resources/DXOcclusionQuery.cpp
)

# S24: the font cluster - none of these individually fit core/ (device/
# pipeline-state plumbing) or resources/ (a GPU resource type a caller
# creates/binds) per that split's own rule, but together they're a cohesive
# subsystem that only sat at dxrender's root because the pre-merge llrender/
# never had a third bucket. See project_dxrender_consolidation_design.
set(dxrender_TEXT
    text/llfontbitmapcache.cpp
    text/llfontfreetype.cpp
    text/llfontfreetypesvg.cpp
    text/llfontvertexbuffer.cpp
    text/llfontregistry.cpp
    text/llfontdx.cpp
)

# S24: absorbed from the now-retired indra/llrender/ library (root level,
# alongside core/ and resources/ - not a llrender/ subfolder, per the move's
# own intent: dxrender is the one truth for render components). Still
# GL-vocabulary-named in places (llgl.cpp/llrender.cpp/LLRender/LLGL* etc.) -
# that's a separate, later renaming effort, not part of this physical move.
set(dxrender_LLRENDER
    llgl.cpp
    llhlslshader.cpp
    lldxtexture.cpp
    llimagedx.cpp
    llrender.cpp
    llrender2dutils.cpp
    llrendersphere.cpp
    llrendertarget.cpp
    llshadermgr.cpp
    lltexturemanagerbridge.cpp
    lluiimage.cpp
    llvertexbuffer.cpp
    DXCubeMap.cpp
    DXCubeMapArray.cpp
)

set(dxrender_HEADER_FILES
    core/DXDevice.h
    core/DXDeviceInfo.h
    core/DXContext.h
    core/DXSwapChain.h
    core/DXStateCache.h
    core/DXState.h
    core/DXCompositionSurface.h

    resources/DXBuffer.h
    resources/DXTexture.h
    resources/DXCubeTexture.h
    resources/DXCubeArrayTexture.h
    resources/DXCubeMapFaces.h
    resources/DXShader.h
    resources/DXRenderTarget.h
    resources/DXSampler.h
    resources/DXVertexLayout.h
    resources/DXUIBatch.h
    resources/DXReadback.h
    resources/DXRender2DUtils.h
    resources/DXQuery.h
    resources/DXOcclusionQuery.h

    text/llfontdx.h
    text/llfontvertexbuffer.h
    text/llfontfreetype.h
    text/llfontfreetypesvg.h
    text/llfontbitmapcache.h
    text/llfontregistry.h

    llgl.h
    llglheaders.h
    llhlslshader.h
    llglstates.h
    lldxtexture.h
    llgltypes.h
    llimagedx.h
    llrender.h
    llrender2dutils.h
    llrendersphere.h
    llshadermgr.h
    lltexturemanagerbridge.h
    lluiimage.h
    lluiimage.inl
    llvertexbuffer.h
    DXCubeMap.h
    DXCubeMapArray.h
)

set_source_files_properties(${dxrender_HEADER_FILES}
    PROPERTIES HEADER_FILE_ONLY TRUE
)

set(dxrender_SOURCE_FILES
    ${dxrender_CORE}
    ${dxrender_RESOURCES}
    ${dxrender_TEXT}
    ${dxrender_LLRENDER}
    ${dxrender_HEADER_FILES}
)

# -------------------------------------------------------------------------
# Headless build (optional, mirrors LLRender)
# -------------------------------------------------------------------------

if (BUILD_HEADLESS)
    add_library(dxrenderheadless
        ${dxrender_SOURCE_FILES}
    )

    set_property(TARGET dxrenderheadless
        PROPERTY COMPILE_DEFINITIONS LL_MESA=1 LL_MESA_HEADLESS=1
    )

    target_link_libraries(dxrenderheadless
        ${LLCOMMON_LIBRARIES}
        ${LLIMAGE_LIBRARIES}
        ${LLMATH_LIBRARIES}
        ${LLFILESYSTEM_LIBRARIES}
        ${LLXML_LIBRARIES}
        ${LLWINDOW_LIBRARIES}
        ${FREETYPE_LIBRARIES}
        d3d11
        dxgi
        d3dcompiler
        dcomp
    )

    target_compile_options(dxrenderheadless PRIVATE "/Zc:wchar_t-")
endif()

# -------------------------------------------------------------------------
# Main DXRender library
# -------------------------------------------------------------------------

add_library(dxrender ${dxrender_SOURCE_FILES})

# -------------------------------------------------------------------------
# Dependencies (mirrors LLRender ordering)
# -------------------------------------------------------------------------

target_link_libraries(dxrender
    ${LLCOMMON_LIBRARIES}
    ${LLIMAGE_LIBRARIES}
    ${LLMATH_LIBRARIES}
    ${LLFILESYSTEM_LIBRARIES}
    ${LLXML_LIBRARIES}
    ${LLWINDOW_LIBRARIES}
    ${FREETYPE_LIBRARIES}

    # DX11 system libs
    d3d11
    dxgi
    d3dcompiler
    dcomp
)
if(NOT DX_RENDER)
  # S24 (carried over from llrender/CMakeLists.txt): only linked for the GL
  # build. A DX_RENDER=ON build deliberately does NOT link OpenGL at all -
  # any raw gl*() call left unconverted anywhere in the tree becomes an
  # unresolved external symbol at final link time instead of a silent
  # no-op/crash at runtime. This is the "flip GL off via CMake/linker to
  # reveal stragglers" technique.
  target_link_libraries(dxrender ${OPENGL_LIBRARIES})
endif()

target_compile_options(dxrender PRIVATE "/Zc:wchar_t-")
