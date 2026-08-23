/**
 * @file dxdrawpoolmaterials.h
 * @brief Fresh DX11-native implementation of LLDrawPoolMaterials::renderDeferred()
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

class LLDrawPoolMaterials;

// Only LLDrawPoolMaterials::renderDeferred() needs real replacement logic
// (see lldrawpoolmaterials.cpp's thin redirect) - beginDeferredPass()/
// endDeferredPass() are left completely untouched there because they're
// already DX-safe by composition: LLPipeline::bindDeferredShader() (which
// they call) is entirely gated behind LLGLSLShader::enableTexture()'s
// `channel > -1` checks and the already-DX-safe uniform setters, so it
// degrades to a real (if inert) shader bind under DX_RENDER with no changes
// needed. Phase 5.2's other 2 real pools in this group
// (LLDrawPoolWaterExclusion, LLDrawPoolTree) turned out to need zero code
// changes at all for the same reason - see the stage 5 hitlist memory.
//
// S24 (2026-08-09, task #134): the "diffuse only" gap noted here previously
// is fixed - normal/specular maps and the per-material shading uniforms
// (environment intensity, emissive brightness, alpha cutoff, specular
// color) are now bound for real, via LLGLSLShader::sCurBoundShaderPtr (mShader
// is private to LLDrawPoolMaterials, same "read whatever's currently bound"
// pattern already used by DXUIBatch/LLSelectNode::renderOneSilhouette())
// and the already-DX-safe enableTexture()/uniform1f(U32,...)/uniform4fv(U32,...)
// member overloads - mirrors LLDrawPoolMaterials::renderDeferred()'s GL body,
// see dxdrawpoolmaterials.cpp's own comment for the two real deviations
// (skips GL's getUniformLocation()+raw glUniform*() calls entirely - both
// broken under DX_RENDER for reasons unrelated to this pool specifically).
// S24 (2026-08-09, task #170): rigged batches (pass>=12) are no longer
// skipped - DXVertexLayout's MAP_WEIGHT4 rejection was fixed by task #168.
// beginDeferredPass() already unconditionally selected the rigged shader
// variant for pass>=12 on both backends (no DX_RENDER gate there at all),
// so LLGLSLShader::sCurBoundShaderPtr was already correct going into this
// function - the only missing piece was renderDeferred() itself using the
// rigged pass-type constant and calling uploadMatrixPalette() per batch,
// both now ported directly from lldrawpoolmaterials.cpp.
class DXDrawPoolMaterials
{
public:
    static void renderDeferred(LLDrawPoolMaterials& pool, S32 pass);
};
