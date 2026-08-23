/**
 * @file dxdrawpoolbump.h
 * @brief Fresh DX11-native implementation of LLDrawPoolBump's deferred bump
 * and fullbright-shiny/emboss-bump render paths.
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

class LLDrawPoolBump;

// Staged full duplicate, same pattern as dxdrawpoolalpha.{h,cpp} (stage 5
// phase 5.3) - chosen deliberately over a thin redirect even though most of
// the GL body is already DX-safe by composition (LLCubeMap's
// enable/disable/bind all route through LLTexUnit and already guard on
// stage>=0; LLGLSLShader::bind()/enableTexture()/uniform*() are all
// DX-safe; LLPipeline::bindReflectionProbes()/setEnvMat() are safe). Kept
// as a full copy rather than in-place edits per the user's explicit
// preference (2026-07-18): API separation between pools pays off later
// when GL is retired, even where the GL body needs little/no change today.
//
// Real, non-cosmetic differences from the GL source, found by reading:
// - LLDrawPoolBump::renderDeferred() has a genuine unguarded chokepoint:
//   diffuse_channel/bump_channel come from LLGLSLShader::enableTexture(),
//   which now safely returns -1 under DX_RENDER (see llglslshader.cpp) -
//   but the GL source then does gGL.getTexUnit(diffuse_channel)->unbind(...)
//   with NO `channel > -1` guard (unlike LLPipeline::bindDeferredShader()'s
//   defensive style). Never crashes on the GL path only because GL's
//   enableTexture() never legitimately returns -1 there. This DX version
//   sidesteps the whole question: no per-material texture-channel
//   registration exists yet under DX_RENDER (same standing gap as
//   LLDrawPoolMaterials, phase 5.2), so only the primary diffuse texture is
//   bound, to the same hardcoded unit every other converted pool uses;
//   the bump/normal-map texture is skipped entirely (documented visual
//   gap - deferred bump/normal geometry renders flat until real per-
//   material channel binding exists).
// - S24 (2026-08-19, degenerate-triangle foliage investigation): FIXED -
//   the emboss-bump pass's raw glPolygonOffset(-1.0f, -1.0f) call
//   (LLDrawPoolBump::renderBump()) really did nothing under DX_RENDER (not
//   a "no equivalent exists" gap - D3D11's rasterizer-state DepthBias/
//   SlopeScaledDepthBias fields ARE the real equivalent, they just needed
//   wiring up, see DXStateCache::getRasterizerState()'s depth_bias_enabled
//   param). This was real, visible z-fighting between the bump pass and
//   the base surface, most visible on curved/thin mesh geometry (reported:
//   blotchy, triangulated darkening on mesh foliage leaves). renderBump()/
//   renderBumpRigged() (the .cpp) now apply and restore the biased state
//   directly around their draw calls. LLDrawPoolGlow's OWN separate
//   polygon-offset call site is a different, still-unconverted case - not
//   fixed by this change.
// - S24 (2026-08-09, task #170): rigged batches are no longer skipped -
//   DXVertexLayout's MAP_WEIGHT4 rejection was fixed by task #168.
//   renderDeferred()/renderPostDeferred() now run both a static and a
//   rigged pass, matching lldrawpoolbump.cpp exactly. Since
//   LLDrawPoolBump::mRigged is private (this DX pool can't set it),
//   renderBump()'s rigged counterpart (renderBumpRigged(), in the .cpp)
//   reimplements pushBumpBatches()'s rigged branch directly using only
//   public members (bindBumpMap()/uploadMatrixPalette()/pushBumpBatch())
//   rather than calling the real member function a second time.
//
// LLDrawPoolBump::bindBumpMap()/pushBumpBatches()/LLRenderPass::
// pushBumpBatch() and LLDrawPoolBump::bindCubeMap()/unbindCubeMap() are
// NOT duplicated here - the first three are called only via the always-
// safe channel==-2 sentinel path in this pool's own use (see class comment
// in the .cpp), and bindCubeMap()/unbindCubeMap() have no callers anywhere
// in the codebase (dead code) - so they're called/left as-is rather than
// copied.
class DXDrawPoolBump
{
public:
    static void renderDeferred(LLDrawPoolBump& pool, S32 pass);
    static void renderPostDeferred(LLDrawPoolBump& pool, S32 pass);
};
