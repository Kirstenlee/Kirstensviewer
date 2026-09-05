/**
 * @file DXCubeMapArray.cpp
 * @brief DXCubeMapArray class implementation
 *
 * S24 (2026-08-31): real DX_RENDER-native replacement for LLCubeMapArray -
 * see DXCubeMapArray.h's header comment. Mechanical swap: the DX_RENDER
 * branch of every method below is carried over unchanged from
 * LLCubeMapArray's own DX_RENDER branch, with the (already-dead-under-
 * DX_RENDER) GL bodies simply not carried over.
 */
#include "linden_common.h"

#include "llworkerthread.h"

#include "DXCubeMapArray.h"

#include "v4coloru.h"
#include "v3math.h"
#include "v3dmath.h"
#include "m3math.h"
#include "m4math.h"

#include "llrender.h"
#include "llhlslshader.h"

#include "llglheaders.h"

using namespace LLImageGLMemory;

DXCubeMapArray::DXCubeMapArray()
    : mTextureStage(0)
{}

DXCubeMapArray::DXCubeMapArray(DXCubeMapArray& lhs, U32 width, U32 count) : mTextureStage(0)
{
    mWidth = width;
    mCount = count;

    // Allocate a new cubemap array with the same criteria as the incoming
    // cubemap array. S24: matches LLCubeMapArray's own DX_RENDER behavior -
    // this project's D3D11 path has never implemented a real GPU-side
    // slice-by-slice resize/copy for this constructor (the old class's
    // copy loop was entirely `#ifndef DX_RENDER`), so callers get a freshly
    // allocated, uninitialized-content array at the new size/count, same as
    // before this rewrite. Not a regression introduced here - a
    // pre-existing gap, out of scope for this mechanical swap.
    allocate(mWidth, lhs.mImage->getComponents(), count, lhs.mImage->getUseMipMaps(), lhs.mHDR);
}

DXCubeMapArray::~DXCubeMapArray()
{}

void DXCubeMapArray::allocate(U32 resolution, U32 components, U32 count, bool use_mips, bool hdr)
{
    mWidth = resolution;
    mCount = count;
    mHDR = hdr;

    // mImage is still constructed (texname 0, unused) so callers reading
    // its component/mipmap settings for the resize-copy constructor above
    // keep working unchanged.
    mImage = new LLImageGL(resolution, resolution, components, use_mips);

    if (!mDXTexture.create((int)resolution, (int)resolution, (int)count, hdr, use_mips))
    {
        LL_WARNS("Texture") << "DXCubeMapArray::allocate: DXCubeArrayTexture::create failed (resolution=" << resolution << " count=" << count << ")" << LL_ENDL;
    }
}

void DXCubeMapArray::bind(S32 stage)
{
    mTextureStage = stage;
    gDX.getTexUnit(stage)->bind(this);
}

void DXCubeMapArray::unbind()
{
    // S24: unbind(TT_CUBE_MAP_ARRAY) is a safe no-op under DX_RENDER
    // (LLTexUnit::unbind() only acts on type==TT_TEXTURE) - kept for call-
    // site symmetry with bind()/other texture types.
    gDX.getTexUnit(mTextureStage)->unbind(LLTexUnit::TT_CUBE_MAP_ARRAY);
    mTextureStage = -1;
}

void DXCubeMapArray::destroy()
{
    mImage = NULL;
    mDXTexture.destroy();
}
