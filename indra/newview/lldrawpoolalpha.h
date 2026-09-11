/**
 * @file lldrawpoolalpha.h
 * @brief LLDrawPoolAlpha class definition
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#ifndef LL_LLDRAWPOOLALPHA_H
#define LL_LLDRAWPOOLALPHA_H

#include "lldrawpool.h"
#include "llrender.h"
#include "llframetimer.h"

class LLFace;
class LLColor4;
class LLHLSLShader;

// S24 (2026-09-05, task #291): gutted down to what's actually reachable
// under DX_RENDER - see lldrawpoolalpha.cpp's own top comment. The real
// alpha-pool implementation (forwardRender/renderAlpha/renderDebugAlpha/
// renderAlphaHighlight/the emissive helpers/TexSetup, and every shader-
// pointer/blend-factor member that only existed to support them) now lives
// entirely in dxdrawpoolalpha.cpp/DXDrawPoolAlpha - this class is just the
// LLDrawPool-required scaffolding plus the two functions that bridge to it.
class LLDrawPoolAlpha final: public LLRenderPass
{
public:

    // set by llsettingsvo so lldrawpoolalpha has quick access to the water plane in eye space
    static LLVector4 sWaterPlane;

    enum
    {
        VERTEX_DATA_MASK =  LLVertexBuffer::MAP_VERTEX |
                            LLVertexBuffer::MAP_NORMAL |
                            LLVertexBuffer::MAP_COLOR |
                            LLVertexBuffer::MAP_TEXCOORD0
    };
    virtual U32 getVertexDataMask() { return VERTEX_DATA_MASK; }

    LLDrawPoolAlpha(U32 type);
    /*virtual*/ ~LLDrawPoolAlpha();

    /*virtual*/ S32 getNumPostDeferredPasses();
    /*virtual*/ void renderPostDeferred(S32 pass);
    /*virtual*/ S32  getNumPasses() { return 1; }

    /*virtual*/ void prerender();

    static bool sShowDebugAlpha;
};

#endif // LL_LLDRAWPOOLALPHA_H
