/**
 * @file dxdrawpoolwlsky.h
 * @brief Fresh DX11-native implementation of LLDrawPoolWLSky's windlight
 * sky dome/clouds/stars/sun/moon render path.
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

class LLDrawPoolWLSky;

// Staged full duplicate, same pattern as dxdrawpoolalpha/dxdrawpoolbump
// (stage 5 phase 5.3/5.4) - chosen per the user's standing preference
// (2026-07-18) to keep pools API-distinct even where the GL body needs
// little change.
//
// Unlike LLDrawPoolAlpha/LLDrawPoolBump, this pool has no rigged-vs-static
// distinction at all (sky/cloud/star/sun/moon geometry isn't per-object),
// so there's no rigged-skip gap here. Real reading found the whole render
// path (renderDome()/renderSkyHazeDeferred()/renderSkyCloudsDeferred()/
// renderStarsDeferred()/renderHeavenlyBodies()) is already DX-safe by
// composition: every matrix op goes through LLRender's CPU-side matrix
// stack (translatef/rotatef/scalef/push/popMatrix, LLGLSquashToFarClip -
// none of it touches GL directly), every draw goes through
// LLVOWLSky::drawDome()/drawStars() which are plain LLVertexBuffer calls,
// and every shader/texture call is through the already-DX-safe
// LLGLSLShader/LLTexUnit wrappers.
//
// One real chokepoint found: LLDrawPoolWLSky::endDeferredPass() has a raw,
// unconditional glClear(GL_DEPTH_BUFFER_BIT) call ("clear the depth buffer
// so haze shaders can use unwritten depth as a mask"). Replaced here with
// gPipeline.mRT->deferredScreen.clear(GL_DEPTH_BUFFER_BIT) - the deferred
// G-buffer target is what's bound throughout this pass, and
// LLRenderTarget::clear() already has a DX_RENDER branch
// (mDXRenderTarget.clear(), stage 3) that does the equivalent depth-only
// clear correctly.
class DXDrawPoolWLSky
{
public:
    static void beginDeferredPass(LLDrawPoolWLSky& pool, S32 pass);
    static void endDeferredPass(LLDrawPoolWLSky& pool, S32 pass);
    static void renderDeferred(LLDrawPoolWLSky& pool, S32 pass);
};
