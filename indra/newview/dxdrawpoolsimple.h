/**
 * @file dxdrawpoolsimple.h
 * @brief Fresh DX11-native implementations of lldrawpoolsimple.h's 5 pool
 * classes that don't have their own in-place DX_RENDER branch yet
 * (LLDrawPoolSimple itself was converted before the 2026-07-17 architecture
 * pivot and is grandfathered as an in-place #ifdef branch - see
 * lldrawpoolsimple.cpp).
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

class LLDrawPoolGrass;
class LLDrawPoolAlphaMask;
class LLDrawPoolFullbrightAlphaMask;
class LLDrawPoolFullbright;
class LLDrawPoolGlow;

// One consolidated file for all 5 pools per the "small pools may share one
// dx file rather than one-file-per-pool" principle (stage 5 hitlist memory,
// Phase 5.1). Each method mirrors its GL sibling's behavior in
// lldrawpoolsimple.cpp, called via a thin #ifdef DX_RENDER redirect there.
//
// S24 (2026-08-09, task #170): rigged (skinned) batches are no longer
// skipped - DXVertexLayout's MAP_WEIGHT4 rejection was fixed by task #168,
// so every pool here now renders both its static and rigged pass, mirroring
// lldrawpoolsimple.cpp's GL bodies exactly.
//
// One standing gap, unrelated to skinning:
// - LL::GLTFSceneManager calls (LLDrawPoolFullbrightAlphaMask only) are
//   skipped: a separate, unconverted rendering subsystem, explicitly
//   deferred (see stage 5 hitlist memory, Phase 5.1 decision).
class DXDrawPoolSimple
{
public:
    static void renderGrassDeferred(LLDrawPoolGrass& pool, S32 pass);
    static void renderAlphaMaskDeferred(LLDrawPoolAlphaMask& pool, S32 pass);
    static void renderFullbrightAlphaMaskPostDeferred(LLDrawPoolFullbrightAlphaMask& pool, S32 pass);
    static void renderFullbrightPostDeferred(LLDrawPoolFullbright& pool, S32 pass);
    static void renderGlowPostDeferred(LLDrawPoolGlow& pool, S32 pass);
};
