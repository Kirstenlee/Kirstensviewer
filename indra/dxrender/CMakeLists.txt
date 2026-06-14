# -*- cmake -*-

project(dxrender)

# -------------------------------------------------------------------------
# Includes (mirroring LLRender style)
# -------------------------------------------------------------------------

include(00-Common)
include(LLCommon)
include(LLImage)
include(LLMath)
include(LLXML)
include(LLFileSystem)

# DX11 does not need OpenGL, FreeType, or GLH
# Add DX-specific includes here if needed later

include_directories(
    ${LLCOMMON_INCLUDE_DIRS}
    ${LLIMAGE_INCLUDE_DIRS}
    ${LLMATH_INCLUDE_DIRS}
    ${LLFILESYSTEM_INCLUDE_DIRS}
    ${LLXML_INCLUDE_DIRS}
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
    core/DXContext.cpp
    core/DXPipelineState.cpp
    core/DXRenderPass.cpp
    core/DXSwapChain.cpp
    core/DXStateCache.cpp
    core/DXUtils.cpp
)

set(dxrender_RESOURCES
    resources/DXBuffer.cpp
    resources/DXTexture.cpp
    resources/DXTextureCube.cpp
    resources/DXShader.cpp
    resources/DXRenderTarget.cpp
    resources/DXSampler.cpp
    resources/DXVertexLayout.cpp
)

set(dxrender_HEADER_FILES
    core/DXDevice.h
    core/DXContext.h
    core/DXPipelineState.h
    core/DXRenderPass.h
    core/DXSwapChain.h
    core/DXStateCache.h
    core/DXUtils.h

    resources/DXBuffer.h
    resources/DXTexture.h
    resources/DXTextureCube.h
    resources/DXShader.h
    resources/DXRenderTarget.h
    resources/DXSampler.h
    resources/DXVertexLayout.h
)

set_source_files_properties(${dxrender_HEADER_FILES}
    PROPERTIES HEADER_FILE_ONLY TRUE
)

set(dxrender_SOURCE_FILES
    ${dxrender_CORE}
    ${dxrender_RESOURCES}
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
        d3d11
        dxgi
        d3dcompiler
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

    # DX11 system libs
    d3d11
    dxgi
    d3dcompiler
)

target_compile_options(dxrender PRIVATE "/Zc:wchar_t-")
