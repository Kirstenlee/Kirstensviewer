/**
 * @file llcubemaparray.h
 * @brief LLCubeMap class definition
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#pragma once

#include "llgl.h"

#include <vector>

#ifdef DX_RENDER
#include "DXCubeArrayTexture.h"
#endif

class LLVector3;

class LLCubeMapArray : public LLRefCount
{
public:
    LLCubeMapArray();
    LLCubeMapArray(LLCubeMapArray& lhs, U32 width, U32 count);

    static GLenum sTargets[6];

    // look and up vectors for each cube face (agent space)
    static LLVector3 sLookVecs[6];
    static LLVector3 sUpVecs[6];

    // look and up vectors for each cube face (clip space)
    static LLVector3 sClipToCubeLookVecs[6];
    static LLVector3 sClipToCubeUpVecs[6];

    // allocate a cube map array
    // res - resolution of each cube face
    // components - number of components per pixel
    // count - number of cube maps in the array
    // use_mips - if true, mipmaps will be allocated for this cube map array and anisotropic filtering will be used
    void allocate(U32 res, U32 components, U32 count, bool use_mips = true, bool hdr = true);
    void bind(S32 stage);
    void unbind();

    GLuint getGLName();

    void destroyGL();

    // get width of cubemaps in array (they're cubes, so this is also the height)
    U32 getWidth() const { return mWidth; }

    // get number of cubemaps in the array
    U32 getCount() const { return mCount; }

#ifdef DX_RENDER
    // S24 (2026-08-09, task #147 step 3): real cube-array SRV, populated
    // per-slice by LLReflectionMapManager's capture path via
    // DXCubeArrayTexture::copySliceFromBoundRenderTarget() (see that
    // class's own comment). Used by LLTexUnit::bind(LLCubeMapArray*)'s
    // DX_RENDER branch. Returns nullptr if allocate() hasn't been called
    // yet or failed.
    ID3D11ShaderResourceView* getDXSRV() const { return mDXTexture.getSRV(); }
    DXCubeArrayTexture* getDXTexture() { return &mDXTexture; }
#endif

protected:
    friend class LLTexUnit;
    ~LLCubeMapArray();
    LLPointer<LLImageGL> mImage;
    U32 mWidth = 0;
    U32 mCount = 0;
    S32 mTextureStage;
    bool mHDR;
#ifdef DX_RENDER
    DXCubeArrayTexture mDXTexture;
#endif
};
