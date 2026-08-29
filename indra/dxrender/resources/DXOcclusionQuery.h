/**
 * @file DXOcclusionQuery.h
 * @brief Real D3D11 occlusion queries behind the GLuint-handle-shaped API
 * llvieweroctree.cpp's occlusion-culling code already expects.
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

// S24 (2026-08-19, occlusion-culling investigation): LLOcclusionCullingGroup
// (llvieweroctree.cpp) issues real GPU occlusion queries via raw
// glGenQueries()/glBeginQuery()/glEndQuery()/glGetQueryObjectuiv() calls -
// zero #ifdef DX_RENDER anywhere in that file. Under DX_RENDER those are
// silent no-ops (no live GL context, same statically-linked-core-GL-symbol
// behavior already confirmed for glPolygonOffset - see DXStateCache.h's
// depth_bias_enabled comment), which left every "GLuint available"/
// "GLuint query_result" local UNINITIALIZED - reading garbage stack memory
// and driving real, silent, random OCCLUDED decisions on whole spatial
// groups. Gated behind LLFeatureManager::isFeatureAvailable("UseOcclusion"),
// this was almost certainly dormant for the whole DX_RENDER port until the
// GPU-detection fix (2026-08-05/06) started reporting a real GPU/feature
// set instead of the always-blank fallback - same "unmasked a chain of
// dormant bugs" pattern as that fix's own history.
//
// This class is the minimal real fix: a D3D11_QUERY_OCCLUSION-backed
// implementation of the exact 4 GL entry points llvieweroctree.cpp already
// calls, so that file's existing pooling/recycling/state-machine logic
// (LLOcclusionCullingGroup::sFreeQueries, QUERY_PENDING/DISCARD_QUERY,
// mOcclusionCheckCount timeout) needed zero changes - only the raw GL calls
// underneath it were swapped for calls into here, behind #ifdef DX_RENDER.
// Deliberately NOT D3D11_QUERY_OCCLUSION_PREDICATE - that's GPU-side
// SetPredication() conditional rendering, a different usage model than the
// CPU-readback-then-branch architecture this whole codebase (both backends)
// already uses for occlusion results.
class DXOcclusionQuery
{
public:
    // Allocates `count` new real D3D11 occlusion queries, writes their
    // opaque handles into out_names[0..count-1]. Mirrors glGenQueries()'s
    // signature - 0 is never a valid returned name (matches GL convention,
    // and LLOcclusionCullingGroup's own "if (!mOcclusionQuery[...])" checks
    // already rely on that). A CreateQuery() failure writes 0 for that slot
    // rather than aborting the whole batch.
    static void genQueries(int count, unsigned int* out_names);

    // Releases `count` queries named in `names` - mirrors glDeleteQueries().
    // A name with no matching live query (0, or already deleted) is silently
    // skipped, matching GL's own "deleting invalid/already-deleted names is
    // a no-op" behavior.
    static void deleteQueries(int count, const unsigned int* names);

    // Begin/end the occlusion query identified by `name` - mirrors
    // glBeginQuery()/glEndQuery() bracketing the proxy-geometry draw call in
    // LLOcclusionCullingGroup::doOcclusion(). Unlike glEndQuery(GLenum
    // target) (which implicitly ends whatever query is current for that
    // target), endQuery() takes the same name explicitly - avoids a hidden
    // "currently active query" static, and every real call site already has
    // the name in scope (mOcclusionQuery[LLViewerCamera::sCurCameraID]).
    static void beginQuery(unsigned int name);
    static void endQuery(unsigned int name);

    // GL_QUERY_RESULT_AVAILABLE equivalent - true once the result is ready
    // without stalling the GPU (D3D11_ASYNC_GETDATA_DONOTFLUSH).
    static bool isResultAvailable(unsigned int name);

    // GL_QUERY_RESULT equivalent - real sample count (0 = fully occluded),
    // same meaning callers already give GL_SAMPLES_PASSED/
    // GL_ANY_SAMPLES_PASSED (both are only ever compared ">0"). Spins until
    // available if called before isResultAvailable() says so - real callers
    // already gate on that first via the timeout-checked branch in
    // LLOcclusionCullingGroup::checkOcclusion(), so this is a safety net,
    // not the expected path.
    static unsigned long long getResult(unsigned int name);
};
