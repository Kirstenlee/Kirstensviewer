/**
 * @file dxdrawpoolterrain.h
 * @brief LLDrawPoolTerrain class definition plus its DX11-native deferred
 * render path - lldrawpoolterrain.h/.cpp retired entirely, 2026-10-13.
 *
 * Copyright (c) 2025 Kirstenlee Cinquetti (Lee Quick)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include "dxdrawpool.h"

class LLDrawPoolTerrain : public LLFacePool
{
    LLPointer<LLViewerTexture> mTexturep;
public:
    enum
    {
        VERTEX_DATA_MASK = LLVertexBuffer::MAP_VERTEX |
                    LLVertexBuffer::MAP_NORMAL |
                    LLVertexBuffer::MAP_TANGENT | // Only PBR terrain uses this currently
                    LLVertexBuffer::MAP_TEXCOORD0 | // Ownership overlay
                    LLVertexBuffer::MAP_TEXCOORD1
    };

    virtual U32 getVertexDataMask();
    LLDrawPoolTerrain(LLViewerTexture *texturep);
    virtual ~LLDrawPoolTerrain();

    /*virtual*/ S32 getNumDeferredPasses() { return 1; }
    /*virtual*/ void beginDeferredPass(S32 pass);
    /*virtual*/ void endDeferredPass(S32 pass);
    /*virtual*/ void renderDeferred(S32 pass);

    /*virtual*/ S32 getNumShadowPasses() { return 1; }
    /*virtual*/ void beginShadowPass(S32 pass);
    /*virtual*/ void endShadowPass(S32 pass);
    /*virtual*/ void renderShadow(S32 pass);

    /*virtual*/ void prerender();
    /*virtual*/ void dirtyTextures(const std::set<LLViewerFetchedTexture*>& textures);
    /*virtual*/ LLViewerTexture *getTexture();
    /*virtual*/ LLViewerTexture *getDebugTexture();
    /*virtual*/ LLColor3 getDebugColor() const; // For AGP debug display

    LLPointer<LLViewerTexture> mAlphaRampImagep;
    LLPointer<LLViewerTexture> m2DAlphaRampImagep;
    LLPointer<LLViewerTexture> mAlphaNoiseImagep;

    static S32 sPBRDetailMode;
    static F32 sDetailScale; // textures per meter
    static F32 sPBRDetailScale; // textures per meter

protected:
    // Still real - LLDrawPoolTerrain::renderShadow() (no DX_RENDER redirect, genuinely shared)
    // calls this directly. Every other GL-only render helper that used to live here
    // (renderFullShader()/renderFullShaderTextures()/renderFullShaderPBR()/hilightParcelOwners()/
    // renderOwnership()/boostTerrainDetailTextures() and the already-dead-in-both-builds
    // renderFull2TU()/renderFull4TU()/renderSimple()) was removed - see dxdrawpoolterrain.cpp.
    void drawLoop();
};

// Staged full duplicate, same pattern as dxdrawpoolalpha/dxdrawpoolbump/
// dxdrawpoolwlsky. beginDeferredPass()/
// endDeferredPass() are duplicated too (not left zero-touch like
// LLDrawPoolTree/LLDrawPoolWLSky's begin/end in earlier phases) because
// endDeferredPass() calls `sShader->unbind()` on a file-static shader
// pointer set by renderDeferred() - if renderDeferred() were redirected to
// a duplicate that sets its own separate sShader, the GL file's
// endDeferredPass() would unbind whatever the GL-side sShader last held
// (likely null, since renderFullShader() never runs under DX_RENDER) -
// a real null-pointer risk. Duplicating all three keeps the shader
// hand-off self-contained.
//
// renderFull2TU()/renderFull4TU()/renderSimple() (legacy fixed-function
// multitexture, glEnable(GL_TEXTURE_GEN_S)/glTexGeni()/glTexGenfv() with no
// DX11 equivalent) had zero callers anywhere in the codebase, predating the
// always-shader-based rendering this pool actually uses via
// renderFullShader() -> renderFullShaderTextures()/renderFullShaderPBR() -
// deleted outright in lldrawpoolterrain.cpp rather than kept dead, along
// with the redirect's now-unreachable GL bodies.
//
// renderFullShaderTextures() binds all 5 textures (detail_0-3 + alpha_ramp) via
// enableTexture()'s real channel mapping; binding only unit 0 would leave the
// other slots holding whatever a previously-drawn, unrelated object left bound.
// renderFullShaderPBR() still only binds the first material's base color texture
// (PBR terrain renders flat - no multi-layer/normal/roughness/emissive).
// renderOwnership() needs no such handling - it only ever binds unit 0, by design.
//
// hilightParcelOwners()'s polygon offset is real via LLRender::setPolygonOffset(),
// no #ifdef needed.
//
// beginShadowPass()/endShadowPass()/renderShadow() are out of scope here.
class DXDrawPoolTerrain
{
public:
    static void beginDeferredPass(LLDrawPoolTerrain& pool, S32 pass);
    static void endDeferredPass(LLDrawPoolTerrain& pool, S32 pass);
    static void renderDeferred(LLDrawPoolTerrain& pool, S32 pass);
};
