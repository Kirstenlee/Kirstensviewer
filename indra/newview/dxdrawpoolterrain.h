/**
 * @file dxdrawpoolterrain.h
 * @brief Fresh DX11-native implementation of LLDrawPoolTerrain's deferred
 * render path.
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

class LLDrawPoolTerrain;

// Staged full duplicate, same pattern as dxdrawpoolalpha/dxdrawpoolbump/
// dxdrawpoolwlsky (stage 5 phase 5.4b). beginDeferredPass()/
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
// renderFull2TU()/renderFull4TU()/renderSimple() are NOT duplicated -
// confirmed by grep they have zero callers anywhere in the codebase
// (dead legacy fixed-function multitexture paths, predating the
// always-shader-based rendering this pool actually uses via
// renderFullShader() -> renderFullShaderTextures()/renderFullShaderPBR()).
// They also rely heavily on glEnable(GL_TEXTURE_GEN_S)/glTexGeni()/
// glTexGenfv() (fixed-function texture coordinate generation) which has
// no DX11 equivalent at all - moot since nothing calls them.
//
// S24 (2026-08-06): renderFullShaderTextures() used to only ever bind
// detail_0, to hardcoded unit 0 - a phase-5.2-era workaround for
// LLGLSLShader::enableTexture() being a hardcoded -1 no-op at the time
// this file was written. Task #103 (same session, well before this fix)
// gave enableTexture() a real D3D-reflection-based channel mapping - this
// function now uses it for real, binding all 4 detail textures + the
// alpha-ramp blend-weight texture, matching lldrawpoolterrain.cpp's GL
// implementation exactly. The stale "only unit 0" state left detail_1-3/
// alpha_ramp permanently unbound, so those slots held whatever an
// unrelated, previously-drawn object last left there - the actual root
// cause of a real, reported bug (terrain visibly showing other scene
// objects' textures, changing live with camera rotation/freecam, solid
// red on an empty test sim). renderFullShaderPBR() still only binds the
// first material's base color texture (documented visual gap: PBR terrain
// renders flat, no multi-layer/normal/roughness/emissive) - not fixed
// here, same shape of gap, lower priority since legacy TEXTURE-type
// terrain (not PBR paint materials) was the one actually reported broken.
// renderOwnership() (used by the "ShowParcelOwners" debug overlay) needed
// no such fix - it only ever binds to hardcoded unit 0 already, by design.
//
// hilightParcelOwners()'s glPolygonOffset(-1.0f, -1.0f) call is real again
// (2026-08-28, task #242) via LLRender::setPolygonOffset() - was skipped
// entirely before ("no DX11 runtime equivalent").
//
// beginShadowPass()/endShadowPass()/renderShadow() are out of scope here -
// shadow pass is phase 5.7.
class DXDrawPoolTerrain
{
public:
    static void beginDeferredPass(LLDrawPoolTerrain& pool, S32 pass);
    static void endDeferredPass(LLDrawPoolTerrain& pool, S32 pass);
    static void renderDeferred(LLDrawPoolTerrain& pool, S32 pass);
};
