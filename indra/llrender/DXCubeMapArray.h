#pragma once

#include "llgl.h"
#include "llimagegl.h"
#include "DXCubeArrayTexture.h"

#include <vector>

class LLVector3;

// S24 (2026-08-31, task #300-adjacent "stop this insanity" rewrite): real
// DX_RENDER-native replacement for LLCubeMapArray (LL's original GL-era
// class, copyright 2022). No GL body, no #ifdef DX_RENDER - this project is
// DX_RENDER-only now. Composes the existing, already-correct
// DXCubeArrayTexture resource wrapper (unchanged, task #147) rather than
// reinventing the raw D3D11 resource layer.
//
// S24 (2026-08-31, layering fix): lives in llrender/, not dxrender/resources/
// - this class needs LLImageGL/LLGLenum/LLTexUnit friendship, all llrender
// types, and dxrender is a lower-level module that must never depend back on
// llrender (llrender already depends on dxrender via DXRENDER_INCLUDE_DIRS/
// DXRENDER_LIBRARIES - see llrender/CMakeLists.txt). Composes the still-
// dxrender-resident DXCubeArrayTexture unchanged via its already-exported
// include path, matching where the original LLCubeMapArray lived.
//
// S24: LLCubeMapArray's static per-face convention tables (sTargets,
// sLookVecs, sUpVecs, sClipToCubeLookVecs, sClipToCubeUpVecs) are
// DELIBERATELY NOT carried over here. sTargets/sLookVecs/sUpVecs had zero
// live readers (only the old class's own now-deleted GL-only #else
// branches). sClipToCubeLookVecs/sClipToCubeUpVecs turned out to be a mixed
// bag: llreflectionmapmanager.cpp's two readers WERE GL-only dead code
// (already removed there) and needed no replacement, but
// llheroprobemanager.cpp's single reader was live, unconditional DX_RENDER
// code (that manager never got the task #147 closed-form-rewrite treatment
// llreflectionmapmanager.cpp did) - preserved there as a local
// sHeroClipToCubeLookVecs/sHeroClipToCubeUpVecs byte-identical copy instead
// of resurrecting it here, since nothing else needs it.
class DXCubeMapArray : public LLRefCount
{
public:
    DXCubeMapArray();
    DXCubeMapArray(DXCubeMapArray& lhs, U32 width, U32 count);

    // allocate a cube map array
    // res - resolution of each cube face
    // components - number of components per pixel
    // count - number of cube maps in the array
    // use_mips - if true, mipmaps will be allocated for this cube map array and anisotropic filtering will be used
    void allocate(U32 res, U32 components, U32 count, bool use_mips = true, bool hdr = true);
    void bind(S32 stage);
    void unbind();

    void destroy();

    // get width of cubemaps in array (they're cubes, so this is also the height)
    U32 getWidth() const { return mWidth; }

    // get number of cubemaps in the array
    U32 getCount() const { return mCount; }

    // Real cube-array SRV, populated per-slice by LLReflectionMapManager's/
    // LLHeroProbeManager's capture path via
    // DXCubeArrayTexture::copySliceFromBoundRenderTarget(). Used by
    // LLTexUnit::bind(DXCubeMapArray*). Returns nullptr if allocate()
    // hasn't been called yet or failed.
    ID3D11ShaderResourceView* getDXSRV() const { return mDXTexture.getSRV(); }
    DXCubeArrayTexture* getDXTexture() { return &mDXTexture; }

protected:
    friend class LLTexUnit;
    ~DXCubeMapArray();
    LLPointer<LLImageGL> mImage;
    U32 mWidth = 0;
    U32 mCount = 0;
    S32 mTextureStage;
    bool mHDR;
    DXCubeArrayTexture mDXTexture;
};
