#pragma once

#include "llgl.h"
#include "llimagedx.h"
#include "DXCubeArrayTexture.h"

#include <vector>

class LLVector3;

// S24: DX_RENDER-native replacement for LLCubeMapArray. Composes the
// DXCubeArrayTexture resource wrapper.
//
// Lives in llrender/, not dxrender/resources/, because it needs LLImageDX/
// LLGLenum/LLTexUnit friendship - dxrender must never depend back on llrender.
//
// LLCubeMapArray's static per-face tables (sTargets, sLookVecs, sUpVecs,
// sClipToCubeLookVecs, sClipToCubeUpVecs) are deliberately not carried over;
// llheroprobemanager.cpp still needs the clip-to-cube vecs and keeps its own
// local copy (sHeroClipToCubeLookVecs/sHeroClipToCubeUpVecs).
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
    LLPointer<LLImageDX> mImage;
    U32 mWidth = 0;
    U32 mCount = 0;
    S32 mTextureStage;
    bool mHDR;
    DXCubeArrayTexture mDXTexture;
};
