/**
 * @file dxdrawpoolalpha.h
 * @brief Fresh DX11-native implementation of LLDrawPoolAlpha's forward-alpha
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

class LLDrawPoolAlpha;

// Deliberate exception to this stage's usual "thin redirect, minimal diff"
// shape: this is a staged, purpose-built DUPLICATE of
// LLDrawPoolAlpha::renderPostDeferred()'s call graph (forwardRender(),
// renderAlpha(), the emissive helpers, renderDebugAlpha()/
// renderAlphaHighlight()), not a from-scratch redesign. Chosen deliberately
// over an in-place #ifdef because most of that call graph is already
// DX-safe by composition (LLGLDepthTest/blendFunc/LLGLDisable/
// LLGLSLShader::bind()+bindTexture()/LLVertexBuffer - all fixed in earlier
// phases) and only rigged-batch handling needed to change - see the stage 5
// hitlist memory for the full reasoning. Understand this means the two
// copies can and will drift on future GL-side edits/LL merges; that's an
// accepted tradeoff for keeping the pools API-distinct going forward.
//
// S24 (2026-08-09, task #170): rigged (skinned) batch handling - mesh
// bodies/clothing/attachments, virtually everything modern avatars wear -
// is now real, mirroring lldrawpoolalpha.cpp's renderAlpha(mask,
// depth_only, rigged)/forwardRender(rigged) two-pass shape exactly
// (PASS_ALPHA vs PASS_ALPHA_RIGGED draw maps, beginAlphaGroups() vs
// beginRiggedAlphaGroups(), mRiggedVariant shader selection,
// uploadMatrixPalette() per-batch). This was blocked until now by
// DXVertexLayout rejecting MAP_WEIGHT4 outright (task #168 fixed that) -
// every rigged-only code path that was previously omitted here
// (renderRiggedEmissives/renderRiggedPbrEmissives, the rigged half of
// renderAlphaHighlight()/renderDebugAlpha(), the rigged GLTF-scene-to-
// depth-buffer pre-pass in forwardRender()) is now ported directly.
// S24 (2026-08-09): the glow/emissive-accumulation blend call
// (gGL.blendFunc(BF_ZERO, BF_ONE, BF_ONE, BF_ONE)) is no longer a gap -
// DXStateCache::getBlendState() gained real, separate alpha_src/alpha_dst
// parameters in an earlier session (2026-08-06, see DXStateCache.h's own
// comment), and LLRender::applyDXBlendState() already threads the 4-factor
// blendFunc() overload's real alpha factors through correctly.
class DXDrawPoolAlpha
{
public:
    static void renderPostDeferred(LLDrawPoolAlpha& pool, S32 pass);
};
