/**
 * @file dxpipeline.cpp
 * @brief Fresh DX11-native implementation of LLPipeline's deferred
 * render-loop drawing logic.
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

#include "llviewerprecompiledheaders.h"

#include "dxpipeline.h"

#include "pipeline.h"
#include "lldrawpool.h"
#include "lldrawpoolpbropaque.h"
#include "lldrawpoolalpha.h"
#include "llspatialpartition.h"
#include "llvertexbuffer.h"
#include "llrendertarget.h"
#include "llhlslshader.h"
#include "llviewercontrol.h"
#include "llenvironment.h"
#include "llviewershadermgr.h"
#include "lldrawable.h"
#include "llviewercamera.h"
#include "llvovolume.h"
#include "llviewerwindow.h"
#include "llfloatertools.h"
#include "lltoolmgr.h"
#include "kveffects.h"
#include <unordered_map>
#include <unordered_set>

// S24 (2026-08-09, task #164): matches pipeline.cpp's own extern declaration
// (llviewerwindow.cpp defines it) - needed for presentDeferredScreen()'s
// tonemap-program selection to match LLPipeline::tonemap()'s no_post logic
// exactly.
extern bool gSnapshotNoPost;

// S24 (2026-08-10, task #158 milestone 1): matches pipeline.cpp's own extern
// declaration (pipeline.cpp defines/sets it during reflection-probe cube-face
// capture) - needed so renderDeferredLighting()'s new sun-shadow/SSAO
// lightmap pass picks gDeferredSunProbeProgram during capture, exactly
// mirroring LLPipeline::renderDeferredLighting()'s own GL body.
extern bool gCubeSnapshot;

// S24 (2026-08-26, task #263): matches llviewerdisplay.cpp's own extern
// declaration (defined there, next to gSnapshotNoPost) - set below, right
// before both presentFinal() call sites, so rawSnapshot() (llviewerwindow.cpp)
// can read the true final composited render target directly instead of the
// broken scratch_space/swap-chain indirection it used to rely on.
extern LLRenderTarget* gLastCompositedPostTarget;

// S24 (2026-08-17): pipeline.cpp's OpenCL post-fx effect mask/refresh -
// GL's own renderFinalize() calls updateEffectMask() then reads effectsMask
// itself, but DX_RENDER's renderFinalize() early-returns before that body
// ever runs, so presentDeferredScreen() has to do both itself. effectsMask
// was `static` (file-scope only) until this same change - see its own
// comment in pipeline.cpp.
extern int effectsMask;
void updateEffectMask();

#include "DXDevice.h"
#include "DXReadback.h"
#include "DXRenderTarget.h"
#include "DXStateCache.h"
#include "DXShader.h"
#include "DXSwapChain.h"

namespace
{
    // S24 (2026-08-04): tracks whether renderDeferredLighting() actually
    // completed and wrote real content into mRT->screen THIS frame - see
    // presentDeferredScreen()'s comment for why this replaced a weaker
    // "shader compiled + target allocated" proxy check (root cause of a
    // black-screen regression: mRT->screen is allocated unconditionally,
    // so its SRV is always non-null even on a frame where
    // renderDeferredLighting() early-returned without writing anything -
    // presentDeferredScreen() was blitting stale/never-written content).
    bool sScreenLitThisFrame = false;

    // S24 (2026-08-05): D3D11 has no TRIANGLE_FAN topology
    // (llvertexbuffer.cpp's sDXMode[] maps it to
    // D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED, and both drawRange()/drawArrays()
    // llassert() against that, so passing TRIANGLE_FAN through would abort
    // rather than draw). GL's own local-lights box draw (LLPipeline::
    // mCubeVB->drawRange(TRIANGLE_FAN, 0, 7, 8, get_box_fan_indices(...)))
    // relies on llvieweroctree.cpp's sOcclusionIndices table (private to
    // that file, not reachable here) - 8 camera-relative "cypher" variants,
    // each an 8-vertex fan tracing the visible ~3 faces of a unit cube from
    // that octant. Expanded below into the triangle-list equivalent (6
    // triangles/18 vertices per cypher, standard fan-to-list expansion:
    // (v0,v1,v2),(v0,v2,v3),(v0,v3,v4),(v0,v4,v5),(v0,v5,v6),(v0,v6,v7)),
    // hand-derived from sOcclusionIndices' actual published values (its
    // bNNN names are just binary vertex indices 0-7 - b000=0, b111=7, etc,
    // confirmed via llvieweroctree.cpp's own eLoveTheBits enum) - same
    // vertex order per cypher as GL's fan, so winding/culling is identical.
    // Built as a flat, non-indexed vertex list (drawArrays(), not
    // drawRange()) DXPipeline owns separately from GL's mCubeVB (which
    // stays exactly as-is, untouched, still used by the GL build).
    LLPointer<LLVertexBuffer> sDXBoxLightVB;

    const U8 sBoxLightTriangleIndices[8][18] =
    {
        { 7,6,2, 7,2,3, 7,3,1, 7,1,5, 7,5,4, 7,4,6 }, // 000
        { 3,2,0, 3,0,1, 3,1,5, 3,5,7, 3,7,6, 3,6,2 }, // 001
        { 5,4,6, 5,6,7, 5,7,3, 5,3,1, 5,1,0, 5,0,4 }, // 010
        { 1,0,4, 1,4,5, 1,5,7, 1,7,3, 1,3,2, 1,2,0 }, // 011
        { 6,0,2, 6,2,3, 6,3,7, 6,7,5, 6,5,4, 6,4,0 }, // 100
        { 2,4,0, 2,0,1, 2,1,3, 2,3,7, 2,7,6, 2,6,4 }, // 101
        { 4,2,6, 4,6,7, 4,7,5, 4,5,1, 4,1,0, 4,0,2 }, // 110
        { 0,6,4, 0,4,5, 0,5,1, 0,1,3, 0,3,2, 0,2,6 }, // 111
    };

    U32 getBoxLightCypher(LLCamera* camera, const LLVector4a& center)
    {
        LLVector4a origin;
        origin.load3(camera->getOrigin().mV);
        return center.greaterThan(origin).getGatheredBits() & 0x7;
    }

    // Only pools in this list are drawn under DX_RENDER - see DXPipeline.h's
    // class comment. Everything else is silently skipped, not a bug: those
    // pools haven't had their own DX_RENDER branch written yet.
    //
    // POOL_GRASS/POOL_ALPHA_MASK (LLDrawPoolGrass/LLDrawPoolAlphaMask) got
    // their DX_RENDER branch in stage 5 phase 5.1 (see dxdrawpoolsimple.cpp)
    // and hook renderDeferred() same as POOL_SIMPLE, so they fit this
    // existing loop as-is. The other 3 pools converted in that same phase
    // (POOL_FULLBRIGHT_ALPHA_MASK/POOL_FULLBRIGHT/POOL_GLOW) hook
    // renderPostDeferred() instead - deliberately NOT whitelisted here yet,
    // since renderGeomPostDeferred()'s loop (with its atmospherics/water-
    // haze/exclusion interleaving) doesn't have a DXPipeline equivalent
    // yet - that's later stage-5 orchestration work, not this phase.
    //
    // POOL_TREE (LLDrawPoolTree) needed zero code changes for phase 5.2 -
    // its renderDeferred()/beginDeferredPass()/endDeferredPass() already
    // route entirely through already-DX-safe primitives (see the stage 5
    // hitlist memory). POOL_MATERIALS (LLDrawPoolMaterials) got a real
    // DX_RENDER branch that phase too (see dxdrawpoolmaterials.cpp) - both
    // hook renderDeferred(), so both fit this loop as-is.
    //
    // POOL_WATEREXCLUSION (LLDrawPoolWaterExclusion) also needed zero code
    // changes that phase, but is deliberately NOT whitelisted here: its
    // render() isn't called from this loop at all - it's invoked from
    // LLPipeline::doWaterExclusionMask(), itself called from
    // renderGeomPostDeferred()'s loop, which has no DXPipeline equivalent
    // yet (same "not wired up yet" situation as the 3 post-deferred pools
    // above).
    //
    // POOL_GLTF_PBR/POOL_GLTF_PBR_ALPHA_MASK (LLDrawPoolGLTFPBR) got a real
    // DX_RENDER branch in stage 8 (2026-08-02, see lldrawpoolpbropaque.cpp) -
    // renderDeferred()'s DX_RENDER branch calls pushGLTFBatches() (ordinary
    // PBR-materialed prims/mesh - routes through LLFetchedGLTFMaterial::
    // bind()'s shader->uniform*()/bindTexture() calls and the same
    // LLVertexBuffer::drawRange() chokepoint every other pool uses, already
    // DX-safe) but explicitly skips LL::GLTFSceneManager::instance().
    // render()/renderOpaque() - that's a SEPARATE mechanism, only used for
    // imported .glb/.gltf SCENE-FILE assets specifically, which calls
    // glBindBufferBase() (a GLEW-style fn ptr null under DX_RENDER - see
    // project_dxrender_open_issues memory, task "Fix GLTFSceneManager UBO
    // binding", still open). Rigged variant skipped, same limitation as
    // every other pool (DXVertexLayout has no skinned attributes yet).
    // renderPostDeferred() (the glow pass) is NOT whitelisted here - same
    // "no post-deferred orchestration loop yet" situation as the other
    // post-deferred pools noted above.
    //
    // POOL_BUMP (LLDrawPoolBump) got a real DX_RENDER branch in stage 5
    // phase 5.3 (see dxdrawpoolbump.cpp, a staged full duplicate like
    // dxdrawpoolalpha) - its renderDeferred() hooks the deferred pass, so
    // it fits this loop as-is (beginDeferredPass()/endDeferredPass() are
    // unoverridden no-ops from the LLDrawPool base). LLDrawPoolAlpha and
    // LLDrawPoolBump's own renderPostDeferred() are NOT whitelisted -
    // same "no orchestration loop yet" situation as the other post-
    // deferred pools above.
    //
    // POOL_WL_SKY (LLDrawPoolWLSky) got a real DX_RENDER branch in stage 5
    // phase 5.4a (see dxdrawpoolwlsky.cpp, same staged-full-duplicate
    // pattern) - it DOES override beginDeferredPass()/endDeferredPass()
    // (unlike POOL_BUMP), both of which also got their own thin redirect,
    // so it fits this loop as-is too. POOL_SKY (LLDrawPoolSky) needed no
    // work at all - every method on that class is an empty, `// DEPRECATED`
    // stub already, GL or DX - so it's not listed here (nothing to whitelist,
    // it draws nothing regardless of backend).
    //
    // POOL_TERRAIN (LLDrawPoolTerrain) got a real DX_RENDER branch in stage
    // 5 phase 5.4b (see dxdrawpoolterrain.cpp) - same shape as POOL_WL_SKY,
    // beginDeferredPass()/endDeferredPass()/renderDeferred() all redirected
    // together (endDeferredPass() unbinds a file-static shader pointer set
    // by renderDeferred(), so all three had to move together - see
    // dxdrawpoolterrain.h class comment). Fits this loop as-is.
    //
    // POOL_AVATAR (LLDrawPoolAvatar) got surgical in-place guards in stage 5
    // phase 5.10c, NOT a dedicated dx*.cpp full duplicate like the pools
    // above - unlike Terrain/WLSky/Alpha/Bump (which synthesize their own
    // geometry and own their render logic directly), LLDrawPoolAvatar is a
    // thin dispatcher into a *different* class (LLVOAvatar's renderSkinned()/
    // renderRigid()/renderImpostor()) whose actual GL calls were confirmed
    // (not assumed) to already be safe by composition once the shared
    // primitives (LLTexUnit::bind(), LLVertexBuffer, uniform4fv's
    // AVATAR_MATRIX handling) were fixed in phases 5.5/5.10a/5.10b - nothing
    // pool-side needed a fresh DX-native reimplementation. Also, its shader-
    // hand-off statics (sRenderingSkinned etc.) are file-static, not class
    // members, so a separate dxdrawpoolavatar.cpp couldn't reach them anyway.
    // Scoped to pass 2 ("skinned", the avatar body mesh) only - passes 0
    // (impostor) and 1 (rigid/eyeballs) are deliberately deferred, same
    // umbrella as rigged mesh-attachment skinning (see the hitlist memory's
    // phase 5.10 system-1-vs-system-2 note). Fits this loop as-is since
    // begin/endDeferredPass() now no-op for the deferred passes.
    // POOL_CONTROL_AV (Animesh) constructs the exact same LLDrawPoolAvatar
    // class (see LLDrawPool::createPool()'s shared
    // `case POOL_AVATAR: case POOL_CONTROL_AV:` branch) - the pass-keyed
    // guards above apply identically, so it's whitelisted alongside
    // POOL_AVATAR with no extra work.
    bool isConvertedPool(U32 type)
    {
        return type == LLDrawPool::POOL_SIMPLE
            || type == LLDrawPool::POOL_GRASS
            || type == LLDrawPool::POOL_ALPHA_MASK
            || type == LLDrawPool::POOL_TREE
            || type == LLDrawPool::POOL_MATERIALS
            || type == LLDrawPool::POOL_BUMP
            || type == LLDrawPool::POOL_WL_SKY
            || type == LLDrawPool::POOL_TERRAIN
            || type == LLDrawPool::POOL_AVATAR
            || type == LLDrawPool::POOL_CONTROL_AV
            || type == LLDrawPool::POOL_GLTF_PBR
            || type == LLDrawPool::POOL_GLTF_PBR_ALPHA_MASK;
    }

    // S24 (2026-08-03): whitelist for renderGeomPostDeferred()'s equivalent
    // below - the "forward"/post-deferred pass (translucent/alpha content,
    // water, glow), previously entirely unwired (see the class comment in
    // dxpipeline.h and this file's own long-standing comments above
    // explaining why POOL_ALPHA/POOL_BUMP/POOL_GLTF_PBR's renderPostDeferred()
    // weren't whitelisted anywhere - there was no loop to whitelist them
    // INTO). Confirmed via direct inspection which pools already have a
    // real, working DX_RENDER renderPostDeferred() branch vs. which don't:
    //
    // POOL_ALPHA_PRE_WATER/POOL_ALPHA_POST_WATER (LLDrawPoolAlpha) - full,
    // sophisticated DX_RENDER implementation already exists
    // (dxdrawpoolalpha.cpp/.h, built in stage 5 phase 5.3) - handles PBR
    // alpha materials, emissive accumulation, fullbright/material/HUD
    // variants, impostor rendering. Confirmed working, just never called
    // until now.
    //
    // POOL_FULLBRIGHT/POOL_FULLBRIGHT_ALPHA_MASK/POOL_GLOW (LLDrawPoolFullbright/
    // LLDrawPoolFullbrightAlphaMask/LLDrawPoolGlow) - all three already
    // redirect to real DX_RENDER implementations in dxdrawpoolsimple.cpp
    // (renderFullbrightPostDeferred()/renderFullbrightAlphaMaskPostDeferred()/
    // renderGlowPostDeferred(), also stage 5 phase 5.1) - same situation,
    // built and waiting.
    //
    // POOL_WATER (LLDrawPoolWater) - task #109 (2026-08-04): real
    // DX_RENDER implementation now exists (dxdrawpoolwater.cpp/.h) -
    // almost the entire GL body was already backend-agnostic
    // (bindDeferredShader()/LLHLSLShader::bindTexture()/uniform*() are all
    // already DX-safe); the one real gap found was
    // LLHLSLShader::bindTexture(S32, LLRenderTarget*, ...) - used for
    // WATER_SCREENTEX/WATER_EXCLUSIONTEX - being a hardcoded no-op, fixed
    // in llhlslshader.cpp alongside this.
    //
    // Deliberately NOT whitelisted yet (real, separate follow-up work, not
    // a quick add): POOL_WATEREXCLUSION (LLDrawPoolWaterExclusion) - its
    // render() still has zero DX_RENDER handling, AND its only caller
    // (LLPipeline::doWaterExclusionMask()) isn't called from this loop or
    // anywhere else under DX_RENDER - that interleaved-call wiring is a
    // separate orchestration change. Until both land, mWaterExclusionMask
    // is allocated but never filled under DX_RENDER, so POOL_WATER's
    // WATER_EXCLUSIONTEX bind above reads stale/cleared content rather
    // than a real mask - a visual completeness gap, not a crash risk.
    // POOL_GLTF_PBR/POOL_GLTF_PBR_ALPHA_MASK's OWN renderPostDeferred()
    // (the glow sub-pass) - explicitly left GL-only when the rest of that
    // pool was converted (see this file's comment above), not reachable
    // here either.
    bool isConvertedPostDeferredPool(U32 type)
    {
        return type == LLDrawPool::POOL_ALPHA_PRE_WATER
            || type == LLDrawPool::POOL_ALPHA_POST_WATER
            || type == LLDrawPool::POOL_WATER
            || type == LLDrawPool::POOL_FULLBRIGHT
            || type == LLDrawPool::POOL_FULLBRIGHT_ALPHA_MASK
            || type == LLDrawPool::POOL_GLOW
            // S24 (2026-08-09, task #157): POOL_AVATAR was missing from this
            // whitelist entirely - LLDrawPoolAvatar::beginPostDeferredPass()/
            // renderPostDeferred()/endPostDeferredPass() (gDeferredAvatarAlphaProgram,
            // lldrawpoolavatar.cpp:260-302) are a real, complete, already-safe
            // implementation (bindDeferredShader()/unbindDeferredShader() are
            // the same already-DX-safe chokepoints POOL_WATER/POOL_BUMP use)
            // that simply never got called - without this, alpha-blended
            // avatar content (transparent clothing, hair, alpha-masked
            // attachments) can never render even once the underlying
            // skinning/vertex-layout/cloud-status blockers are fixed. Found
            // via a user-compiled avatar-appearance report, verified against
            // source before applying (same pattern as the POOL_BUMP fix
            // just above).
            || type == LLDrawPool::POOL_AVATAR
            // S24 (2026-08-09): POOL_BUMP (shiny-fullbright + emboss-bump)
            // was left off this whitelist when it was first built (task
            // #108) - DXDrawPoolBump::renderPostDeferred() (dxdrawpoolbump.cpp)
            // was already a complete, working implementation
            // (beginFullbrightShiny/renderFullbrightShiny/endFullbrightShiny
            // + beginBump/renderBump/endBump, confirmed by direct read) that
            // simply never got called - the pool loop below skips anything
            // not in this list before ever reaching renderPostDeferred().
            // Found via a user-compiled deep-dive report on missing shiny
            // prims, verified against source before applying.
            || type == LLDrawPool::POOL_BUMP
            // S24 (2026-08-09): POOL_GLTF_PBR/POOL_GLTF_PBR_ALPHA_MASK's own
            // renderPostDeferred() (lldrawpoolpbropaque.cpp) - the PBR
            // material glow/emissive sub-pass (gPBRGlowProgram, both static
            // and rigged via pushRiggedGLTFBatches()) plus HUD-attached PBR
            // content - was explicitly left off this whitelist when the
            // pool was first wired in (task #100), before this
            // isConvertedPostDeferredPool() loop even existed. Real,
            // complete, already-safe implementation (no beginPostDeferredPass()/
            // endPostDeferredPass() overrides - uses trivial base-class
            // defaults, same as other already-whitelisted pools) that
            // simply never got called. User connected this directly to a
            // real, currently-broken feature ("eyes can have glow") before
            // this fix landed - not a coincidence, a real gap.
            || type == LLDrawPool::POOL_GLTF_PBR
            || type == LLDrawPool::POOL_GLTF_PBR_ALPHA_MASK;
    }
}

// static
void DXPipeline::renderGeomDeferred(LLPipeline& pipeline, LLCamera& camera, bool do_occlusion)
{

    // S24: the black-screen/PBR-render-map and rigged-mesh-invisible
    // investigations that lived here as periodic-sampling diagnostics are
    // both resolved - see LLRender::syncMatrices()'s DX_RENDER branch
    // (llrender.cpp) for the actual rigged-mesh root cause (missing
    // "projection_matrix" uniform upload) and objectSkinV.hlsl for the two
    // matrix-layout bugs fixed alongside it.

    // S24 (2026-08-19, degenerate-triangle foliage investigation): FOUND -
    // this "fresh, deliberately simplified" reimplementation never carried
    // over LLPipeline::renderGeomDeferred()'s own `LLGLEnable cull(GL_CULL_FACE);`
    // (pipeline.cpp, wraps its entire deferred-pools render block) at all.
    // DXContext::beginFrame() explicitly resets the rasterizer state to
    // cull_enabled=false once at the very start of every frame (matching
    // GL's true default, intended as a safe baseline for 2D UI code that
    // never toggles culling itself) - with nothing here re-enabling it,
    // EVERY deferred pool (opaque, materials, bump, PBR, everything) has
    // been rendering with backface culling OFF this whole time, unless some
    // earlier pass in the same frame happened to leave it on by accident.
    // Real, wide-reaching consequence: any mesh authored assuming backface
    // culling would hide its "inside" (the classic two-single-sided-layers
    // technique used for double-sided-looking thin geometry like foliage
    // leaves, since the legacy Materials/Bump system has no per-material
    // double-sided flag) renders BOTH layers at once - two near-coincident,
    // mirror-image surfaces genuinely competing in the depth buffer, which
    // is real geometric z-fighting, camera-movement-sensitive (confirmed by
    // live testing: the reported blotchy leaf patches flicker/shift with
    // small camera moves, and the same patch pattern appears on both the
    // leaf's front and back - exactly what two culled-off mirrored layers
    // would produce). Root cause, not just a foliage-specific issue -
    // matches GL's own scope exactly (cull enabled for the whole
    // deferred-pools render, not just this one pool).
    LLGLEnable cull(GL_CULL_FACE);

    // S24 (2026-08-21, task #156 follow-up): LLPipeline::renderGeomDeferred()'s
    // GL body (pipeline.cpp:4193-4207) computes gGLDeltaModelView/
    // gGLInverseDeltaModelView here, right at the top of the function - this
    // DX body early-returns out of that GL body entirely (see the redirect
    // at pipeline.cpp:4184), so those two globals were never populated under
    // DX_RENDER and stayed at their default-constructed identity matrix
    // forever. bindDeferredShader() (pipeline.cpp:8993-8994) uploads exactly
    // these two globals as MODELVIEW_DELTA_MATRIX/INVERSE_MODELVIEW_DELTA_MATRIX
    // - AFTER its own call to bindReflectionProbes() (line 8944), silently
    // clobbering task #156's own correctly-computed per-call delta
    // (get_current_modelview()*inverse(get_last_modelview())) back to
    // identity for every shader that goes through bindDeferredShader() -
    // which is the main deferred-lighting/soften shader lighting the WHOLE
    // screen (regular PBR/materials/terrain). Only dxdrawpoolbump.cpp's
    // legacy-shiny pool (which calls bindReflectionProbes() directly,
    // bypassing bindDeferredShader()) escaped this clobber - matching the
    // reported "SSR primarily not working" (identity delta means
    // screenSpaceReflUtil.hlsl's traceScreenRay() reprojects into last
    // frame's mSceneMap as if the camera never moved, so rays mostly miss
    // the instant the camera actually moves). Mirrored verbatim, same
    // formula, same gate.
    if (&camera == LLViewerCamera::getInstance())
    {
        glm::mat4 last_modelview = get_last_modelview();
        glm::mat4 cur_modelview = get_current_modelview();

        glm::mat4 m = glm::inverse(last_modelview);  // last camera space to world space
        m = cur_modelview * m; // world space to camera space

        glm::mat4 n = glm::inverse(m);

        gGLDeltaModelView = m;
        gGLInverseDeltaModelView = n;
    }

    // S24 (2026-08-27, task #267 follow-up): GL's renderGeomDeferred() body
    // (pipeline.cpp:4289-4294, on the "GL-specific and left untouched/dead
    // under DX_RENDER" list this function's own top-of-function comment
    // names explicitly) calls mReflectionMapManager.updateUniforms()/
    // mHeroProbeManager.updateUniforms() once per frame here, unconditionally
    // early-returned past under DX_RENDER. That was masked as long as
    // LLReflectionMapManager::setUniforms()'s `mUBO == 0` guard was
    // (accidentally) always true under DX_RENDER, which made it call
    // updateUniforms() itself on every reflection shader bind instead - the
    // task #267 fix (llreflectionmapmanager.cpp) corrected that guard to only
    // run once at bootstrap, which is only correct if something else refreshes
    // per frame. Nothing did, under DX_RENDER - probe data (positions, bucket
    // assignments, hero-probe box/sphere/mip data) froze after the very first
    // bind and never updated again, live-confirmed as reflections/mirror
    // vanishing entirely a few frames in. Real fix: mirror GL's per-frame call
    // site here, same shader-level gate.
    if (LLViewerShaderMgr::instance()->mShaderLevel[LLViewerShaderMgr::SHADER_DEFERRED] > 1)
    {
        pipeline.mReflectionMapManager.updateUniformsPerFrame();
        pipeline.mHeroProbeManager.updateUniformsPerFrame();
    }

    // S24 (2026-08-19, task #182): occlusion culling was on dxpipeline.h's
    // "still-unconverted" list - real GL (pipeline.cpp) calls doOcclusion(camera)
    // exactly once mid-loop, when it first reaches a pool with
    // getType() >= POOL_GRASS, gated on sUseOcclusion/do_occlusion/
    // sProfileEnabled/!gCubeSnapshot. Mirrored here, same trigger point and
    // same gating - DXOcclusionQuery (task #245) already builds a real,
    // correct D3D11_QUERY_OCCLUSION-backed implementation underneath
    // LLOcclusionCullingGroup, but had zero reachable callers under
    // DX_RENDER until this call site existed. gGLLastMatrix reset + reload
    // mirrors GL's own pre-doOcclusion() matrix refresh exactly (the proxy-
    // box shader needs the current camera's modelview bound correctly).
    bool occlude = LLPipeline::sUseOcclusion > 1 && do_occlusion && !LLHLSLShader::sProfileEnabled && !gCubeSnapshot;

    for (LLDrawPool* poolp : pipeline.getPools())
    {
        if (occlude && poolp->getType() >= LLDrawPool::POOL_GRASS)
        {
            occlude = false;
            gGLLastMatrix = nullptr;
            gDX.loadMatrix(gGLModelView);
            pipeline.doOcclusion(camera);
        }

        if (!isConvertedPool(poolp->getType()))
        {
            continue;
        }

        if (!pipeline.hasRenderType(poolp->getType()) || poolp->getSkipRenderFlag())
        {
            continue;
        }

        S32 pass_count = poolp->getNumDeferredPasses();
        for (S32 i = 0; i < pass_count; ++i)
        {
            LLVertexBuffer::unbind();
            poolp->beginDeferredPass(i);
            poolp->renderDeferred(i);
            poolp->endDeferredPass(i);
        }
    }

    LLVertexBuffer::unbind();
}

// static
void DXPipeline::renderGeomPostDeferred(LLPipeline& pipeline, LLCamera& camera)
{
    // S24 (2026-08-03): fresh, deliberately simplified equivalent of
    // LLPipeline::renderGeomPostDeferred()'s GL loop - same "not a fenced
    // copy" philosophy as renderGeomDeferred() above (see its own comment
    // and dxpipeline.h's class comment). Only pools in
    // isConvertedPostDeferredPool()'s whitelist are drawn; everything else
    // (GLTF PBR's own glow sub-pass) is silently skipped - not a bug, just
    // not converted yet (see that function's comment for exactly what's
    // missing and why).
    //
    // Deliberately NOT ported in this first pass (all GL-only, unconverted,
    // and independently risky/large - real follow-up work, not folded in
    // here): wireframe mode, renderHighlights()/renderDebug() (debug-only),
    // and the "grouped by type" begin/render/end-once-per-run structure the
    // GL loop uses (mirrors renderGeomDeferred()'s existing simplification:
    // begin/render/end once PER POOL INSTANCE instead - harmless, since
    // nothing here relies on the grouping behavior itself, just each pool
    // getting its begin/render/end call in order).
    //
    // S24 (2026-08-17, task #181): doAtmospherics()/doWaterHaze() - the
    // other two depth-buffer effects the GL loop interleaves - WERE on this
    // deferred list (this comment used to say so, citing "meaningless
    // without a working atmospherics pipeline behind them"), but that
    // dismissal went stale once task #115/#158 landed a real atmospherics
    // pipeline (ambient+sun/atmospherics via softenLightF.hlsl, then real
    // sun-shadow/SSAO). Re-audited: neither LLPipeline::doAtmospherics()
    // nor doWaterHaze() (pipeline.cpp) contains a single raw GL call - both
    // are built entirely from primitives already proven DX-safe throughout
    // this file (LLGLDepthTest, bindDeferredShader()/unbindDeferredShader(),
    // mScreenTriangleVB, gDX.setColorMask/blendFunc/setSceneBlendType) - so
    // exactly like task #116's doWaterExclusionMask(), the real gap was
    // never "port this", just "actually call it". hazeF.hlsl/waterHazeF.hlsl/
    // waterHazeV.hlsl (class3/deferred) were already ported as part of the
    // original 237-file GLSL->HLSL sweep but sat completely unused until
    // now - zero prior references to gHazeProgram/gHazeWaterProgram
    // anywhere in this file. Threshold/ordering mirrored exactly from
    // LLPipeline::renderGeomPostDeferred() (pipeline.cpp ~4425-4488):
    // atmospherics fires before POOL_WATER when underwater (haze needs to
    // affect the water surface itself), otherwise before
    // POOL_ALPHA_POST_WATER; water haze always fires before
    // POOL_ALPHA_PRE_WATER; both are skipped entirely while rendering HUD
    // attachments (sRenderingHUDs - a HUD has no world-space atmosphere to
    // haze into - this function runs for HUD attachments too, see this
    // function's own comment above doWaterExclusionMask()'s call) or during
    // a low-detail reflection-probe capture (gCubeSnapshot +
    // RenderReflectionProbeLevel==0), same as GL.
    (void)camera;

    // S24 (2026-08-09, task #116): the third depth-buffer effect (see
    // above) turned out NOT to need the same deferral -
    // LLDrawPoolWaterExclusion::render() is entirely composed of already-
    // DX-safe primitives (LLHLSLShader::bind/uniform4f/uniform1f,
    // LLGLDepthTest/LLGLDisable, LLDrawPoolWater::pushWaterPlanes() -
    // already reused as-is by the real DX water pool itself -
    // pushBatches()) - zero raw GL calls of its own, so it needed zero
    // changes, just to actually be called. GL calls this once, early,
    // "before rendering alpha" (pipeline.cpp's own comment,
    // water_exclusion_pass = POOL_WATEREXCLUSION, the lowest real pool
    // type value so it fires on the very first pool the GL loop sees) -
    // mirrored here as an unconditional call before this loop, matching
    // that placement/intent. See LLPipeline::doWaterExclusionMask()'s own
    // DX_RENDER branch (pipeline.cpp) for the one real gap that DID need
    // fixing: its glClearColor(1,1,1,1) call is GL-only and silently
    // skipped under DX_RENDER, and LLRenderTarget::clear() always clears
    // to transparent black - inverting this mask's meaning (white =
    // included, so defaulting to black would exclude the whole screen by
    // default) until clearColor() was used instead.
    // S24 (2026-08-09, task #148): first bisection attempt disabled this
    // whole call - inconclusive, since it also skips the mask's clear-to-
    // white, and water's own shader discards every pixel when the mask
    // reads all-black in non-transparent-water mode ("no water at all" -
    // a confound, not a real signal). Call re-enabled; the actual
    // surgical skip (mWaterExclusionPool->render() only, mask still
    // cleared correctly) now lives inside LLPipeline::doWaterExclusionMask()
    // itself (pipeline.cpp) - see its own comment.
    pipeline.doWaterExclusionMask();

    // S24 (2026-08-17, task #181): see the big comment above.
    bool done_atmospherics = LLPipeline::sRenderingHUDs;
    bool done_water_haze = done_atmospherics;

    U32 atmospherics_pass = LLPipeline::sUnderWaterRender ? (U32)LLDrawPool::POOL_WATER : (U32)LLDrawPool::POOL_ALPHA_POST_WATER;
    U32 water_haze_pass = LLDrawPool::POOL_ALPHA_PRE_WATER;

    static LLCachedControl<S32> atmospherics_probe_level(gSavedSettings, "RenderReflectionProbeLevel", 0);
    bool low_detail_probe = atmospherics_probe_level == 0 && gCubeSnapshot;
    done_atmospherics = done_atmospherics || low_detail_probe;
    done_water_haze = done_water_haze || low_detail_probe;

    // S24 (2026-08-06): this function was structurally unreachable from the
    // main per-frame display() - only ever called from display_cube_face()
    // (reflection-probe/snapshot path). Fixed by calling it from
    // DXPipeline::renderDeferredLighting(), right before that function's own
    // final screen_target->flush() - see there. (An earlier diagnostic here
    // briefly looked like a render-type gating bug caused by
    // LLPipeline::generateImpostor() contamination - red herring, the real
    // cause was simply this call site never existing for the world scene.)

    // S24 (2026-08-19, degenerate-triangle foliage investigation, same bug
    // class as renderGeomDeferred() above): GL's real renderGeomPostDeferred()
    // (pipeline.cpp:4427) also wraps its whole pool loop in
    // `LLGLEnable cull(GL_CULL_FACE);` - never carried over here either.
    // Alpha/water/fullbright/glow content that relies on backface culling
    // being ON by default (individual pools like dxdrawpoolalpha.cpp only
    // ever explicitly DISABLE cull for confirmed double-sided materials -
    // they don't separately re-enable it, they assume it's already on from
    // this outer scope, matching GL) was rendering both sides of anything
    // single-sided too.
    LLGLEnable cull(GL_CULL_FACE);

    pipeline.calcNearbyLights(camera);
    pipeline.setupHWLights();

    gDX.setSceneBlendType(LLRender::BT_ALPHA);
    gDX.setColorMask(true, false);

    for (LLDrawPool* poolp : pipeline.getPools())
    {
        if (!isConvertedPostDeferredPool(poolp->getType())) { continue; }
        if (!pipeline.hasRenderType(poolp->getType())) { continue; }

        U32 cur_type = poolp->getType();

        if (cur_type >= atmospherics_pass && !done_atmospherics)
        { // do atmospherics against depth buffer before rendering alpha
            pipeline.doAtmospherics();
            done_atmospherics = true;
        }

        if (cur_type >= water_haze_pass && !done_water_haze)
        { // do water haze against depth buffer before rendering alpha
            pipeline.doWaterHaze();
            done_water_haze = true;
        }

        // S24 (2026-08-09, task #148): real root cause of the "water tide
        // rises/rocks like a seesaw/disappears" bug, found via a live
        // shader-color diagnostic that painted water solid yellow (removing
        // all lighting/shading variation) and still showed the same
        // hover/rock motion - proving the RASTERIZED GEOMETRY itself was
        // moving, not a shading artifact. GL's own renderGeomPostDeferred()
        // (pipeline.cpp ~line 4475) resets "gGLLastMatrix = NULL;
        // gDX.loadMatrix(gGLModelView);" once per pool-type group, before
        // that group's pass loop - this DX_RENDER loop, built as a
        // deliberately simplified mirror, dropped that reset entirely.
        // Pools that draw many distinct objects (alpha/fullbright/glow) call
        // LLRenderPass::applyModelMatrix(params) per draw item (lldrawpool.cpp),
        // which reloads gGLModelView * that object's own model matrix any
        // time the model-matrix pointer differs from gGLLastMatrix - so
        // those pools self-correct every draw and never depended on this
        // reset for correctness. LLDrawPoolWater is the one post-deferred
        // pool that does NOT call applyModelMatrix() at all (its vertices
        // are already baked in world/agent space by LLVOWater::
        // updateGeometry(), expecting to be transformed by the pure camera
        // view matrix alone) - it silently relied on the OUTER loop having
        // already loaded a clean gGLModelView before its draw. Without this
        // reset, water inherited whatever model matrix the LAST alpha-
        // blended object drawn by POOL_ALPHA_PRE_WATER happened to leave
        // bound - an arbitrary, ever-changing transform (varying by camera
        // angle/distance as different alpha objects entered/left the draw
        // list, or animating if that last object itself moved/rotated) -
        // exactly matching every reported symptom: disappearing, "rising to
        // inundate the house", and rocking like a seesaw.
        gGLLastMatrix = nullptr;
        gDX.matrixMode(LLRender::MM_MODELVIEW);
        gDX.loadMatrix(gGLModelView);

        S32 pass_count = poolp->getNumPostDeferredPasses();
        for (S32 i = 0; i < pass_count; ++i)
        {
            LLVertexBuffer::unbind();
            poolp->beginPostDeferredPass(i);
            poolp->renderPostDeferred(i);
            poolp->endPostDeferredPass(i);
        }
    }

    LLVertexBuffer::unbind();

    // S24 (task #160, glow/bloom investigation - see project memory for
    // full status): a temporary deferredScreen alpha readback lived here
    // briefly (same 3x3-grid technique as renderGeomDeferred()'s own
    // readback above, but placed AFTER this function's pool loop, since
    // that's what actually draws LLDrawPoolGlow's additive alpha-only
    // emissive pass - the earlier readback fires too early to see it).
    // Removed for now; next step if picking this back up is reading back
    // mGlow[1] (the extracted+blurred glow buffer) right before
    // combineGlow() composites it, to split "extract/blur never ran
    // correctly" from "combineGlow() itself doesn't read it correctly".

    // S24 (2026-08-10, task #167/#178): GL's own renderGeomPostDeferred()
    // (pipeline.cpp ~line 9472) ends with gDX.setColorMask(true, true) -
    // this DX_RENDER mirror set (true, false) at the top (line 422, alpha
    // writes disabled for the alpha/fullbright/glow pool loop above) but
    // never restored it, unlike GL's own function boundary. Under
    // DX_RENDER this is real, sticky D3D11 blend state (OMSetBlendState),
    // and nothing between here and the UI draw resets it - GL's own
    // LLPipeline::renderFinalize() has an equivalent reset, but DX_RENDER's
    // renderFinalize() early-returns into DXPipeline::presentDeferredScreen()
    // and never reaches it. This function is called both for the main
    // scene (via renderDeferredLighting()) and for HUD attachments (via
    // render_hud_attachments()) - attaching a HUD guarantees this state
    // gets re-mutated immediately before the UI draw pass runs, the
    // leading suspect for "attaching a HUD obliterates the UI."
    gDX.setColorMask(true, true);

    // S24 (2026-09-09, SSAO flicker investigation): the gGLLastModelView/
    // gGLLastProjection "advance to current, for next frame's reprojection"
    // capture that used to live here was moved to the single real end-of-
    // world-scene point, DXPipeline::renderDeferredLighting()'s own tail
    // (right after this function's OWN call from there returns) - see that
    // call site's comment for why. This function runs from too many
    // different contexts (the main world camera, EARLY, from
    // llviewerdisplay.cpp; the main world camera again, LATE, from
    // renderDeferredLighting() below; and the HUD camera, from
    // render_hud_attachments()) to safely own a "the real frame's camera
    // just finished" side effect - the HUD-camera call in particular was
    // clobbering gGLLastModelView with the HUD's own matrix every single
    // frame, AFTER the main scene, so anything reprojecting off "last
    // frame's real camera" (SSAO's temporal resolve, task #190; SSR's ray
    // march) was silently working off the previous frame's HUD transform
    // instead of the previous frame's WORLD camera - a real, confirmed bug.
}

namespace
{
    // Self-contained placeholder shader pair for presentDeferredScreen()
    // below - deliberately NOT registered with LLViewerShaderMgr (this
    // isn't part of the real ~150-shader set, just a one-off blit used
    // only by this stopgap). Draws a fullscreen triangle purely from
    // SV_VertexID (no vertex buffer needed), matching the same technique
    // proven in stage 3 milestone 2's vertical-slice verification.
    const char* const kPresentVS =
        "struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };\n"
        "VSOut main(uint vid : SV_VertexID)\n"
        "{\n"
        "    VSOut o;\n"
        "    o.uv = float2((vid << 1) & 2, vid & 2);\n"
        "    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
        "    return o;\n"
        "}\n";

    const char* const kPresentPS =
        "Texture2D srcTex : register(t0);\n"
        "SamplerState srcSampler : register(s0);\n"
        "float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET\n"
        "{\n"
        "    return srcTex.Sample(srcSampler, uv);\n"
        "}\n";

    // Lazily compiled on first use, then cached for the process lifetime -
    // mirrors DXStateCache's "compile/create once" approach for other
    // one-off D3D11 objects.
    bool getPresentShader(DXShader** out_shader, ID3D11SamplerState** out_sampler)
    {
        static DXShader s_shader;
        static ID3D11SamplerState* s_sampler = nullptr;
        static bool s_attempted = false;
        static bool s_ready = false;

        if (!s_attempted)
        {
            s_attempted = true;

            bool compiled = s_shader.compileVertexShader(kPresentVS, "DXPipeline present VS")
                && s_shader.compilePixelShader(kPresentPS, "DXPipeline present PS");

            D3D11_SAMPLER_DESC sampler_desc = {};
            sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
            sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
            HRESULT hr = gDXDevice.getDevice()->CreateSamplerState(&sampler_desc, &s_sampler);

            s_ready = compiled && SUCCEEDED(hr) && s_sampler != nullptr;
            if (!s_ready)
            {
                LL_WARNS("DXPipeline") << "presentDeferredScreen: failed to build placeholder present shader/sampler" << LL_ENDL;
            }
        }

        *out_shader = &s_shader;
        *out_sampler = s_sampler;
        return s_ready;
    }

    // S24 (2026-08-06, task #110): DXRenderTarget::bindSwapChainBackBuffer()
    // (called by both presentDeferredScreen() draw paths below) always sets
    // a viewport covering the FULL swap-chain/back-buffer size - correct
    // for its other caller (LLRenderTarget::flush()'s generic "restore the
    // back buffer" case, llrendertarget.cpp), but wrong for the final
    // present-to-screen step specifically: GL's own glViewport() call
    // confines 3D content to LLViewerWindow::mWorldViewRectRaw (the area
    // left over after the menu bar/location bar chrome), while this always
    // stretched over the whole client area. Deliberately NOT changing
    // bindSwapChainBackBuffer()'s own default (would wrongly affect its
    // other caller) - overriding the viewport here instead, right after
    // each of this function's own bindSwapChainBackBuffer() calls.
    //
    // Coordinate conversion: LLRect (mWorldViewRectRaw) uses GL's bottom-up
    // convention (Y grows upward, origin bottom-left, mTop/mBottom measured
    // from the bottom) - D3D11_VIEWPORT uses top-down screen-space (Y grows
    // downward, origin top-left). TopLeftX matches directly (both left-
    // origin); TopLeftY = full window height - mTop (the gap between the
    // window's top edge and the world-view rect's top edge, in D3D11's
    // convention).
    void setPresentViewport()
    {
        LLRect world_rect = gViewerWindow ? gViewerWindow->getWorldViewRectRaw() : LLRect();
        if (world_rect.getWidth() <= 0 || world_rect.getHeight() <= 0)
        {
            return; // not ready yet (e.g. very early startup) - leave the full-window viewport bindSwapChainBackBuffer() already set
        }

        D3D11_VIEWPORT vp = {};
        vp.TopLeftX = (float)world_rect.mLeft;
        vp.TopLeftY = (float)(gDXSwapChain.getHeight() - world_rect.mTop);
        vp.Width = (float)world_rect.getWidth();
        vp.Height = (float)world_rect.getHeight();
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        gDXDevice.getContext()->RSSetViewports(1, &vp);
    }

    // S24 (2026-08-09, task #137, extended 2026-08-17 task #139): extracted
    // from presentDeferredScreen()'s original fallback-only blit path - the
    // single, shared "put this render target on screen" chokepoint for BOTH
    // the fallback (unlit/no-gamma-shader) case and the real gamma-corrected/
    // post-fx'd case. Primary path now uses the REAL gDeferredPostNoDoFNoiseProgram
    // (depth-aware noise dithering, same shader/permutation GL's own
    // renderFinalize() tail end uses - pipeline.cpp:8607-8623) instead of the
    // placeholder VS/PS pair; the placeholder is kept only as a graceful
    // fallback if that shader somehow failed to compile, mirroring the
    // gamma_shader.isComplete() degrade-gracefully pattern already used above
    // in presentDeferredScreen().
    void presentFinal(LLPipeline& pipeline, LLRenderTarget* src)
    {
        if (!src || !src->getColorSRV(0))
        {
            return;
        }

        DXRenderTarget::bindSwapChainBackBuffer();
        // S24 (2026-08-10, task #167/#178): the same raw-bind-bypasses-
        // bookkeeping bug class as LLHLSLShader::sCurBoundShaderPtr below
        // (see that fix's own comment, task from 2026-07-26) - this call
        // updates the REAL D3D11 render target directly via DXRenderTarget,
        // completely bypassing LLRenderTarget::bindTarget()/flush()'s
        // sBoundTarget/mPreviousRT stack bookkeeping. Whatever LLRenderTarget
        // was last left bound-but-not-flushed before this point (e.g.
        // mPostPongMap, left bound by combineGlow() so its content can be
        // read as an SRV right after) stays "remembered" as sBoundTarget for
        // the rest of the frame - harmless for the main scene's own
        // renderGeomPostDeferred()->doWaterExclusionMask() call (which always
        // runs BEFORE this function does, every frame), but a real, silent
        // bug for the HUD's SECOND same-frame call to the same function
        // (render_hud_attachments() calling renderGeomPostDeferred() again,
        // only when a HUD is attached): LLPipeline::doWaterExclusionMask()'s
        // mWaterExclusionMask.bindTarget()/flush() cycle captures/restores
        // that STALE render target instead of the real back buffer, and
        // nothing downstream (the rest of that pool loop, render_hud_elements(),
        // finally the whole UI draw in render_ui_2d()) ever rebinds the real
        // back buffer again - so every subsequent draw call that frame,
        // including the ENTIRE UI, silently lands in that stale offscreen
        // scratch texture instead of what's actually presented. Root cause
        // of "attaching a HUD obliterates the UI" (task #167), found via a
        // bracketed sequence of render-target-identity checkpoints proving
        // the leak traces to exactly this raw bind. Resetting the
        // bookkeeping here, mirroring the sCurBoundShaderPtr fix exactly.
        LLRenderTarget::sBoundTarget = nullptr;
        setPresentViewport();

        if (gDeferredPostNoDoFNoiseProgram.isComplete())
        {
            gDeferredPostNoDoFNoiseProgram.bind();
            gDeferredPostNoDoFNoiseProgram.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, src);
            gDeferredPostNoDoFNoiseProgram.bindTexture(LLShaderMgr::DEFERRED_DEPTH, &pipeline.mRT->deferredScreen, true);
            gDeferredPostNoDoFNoiseProgram.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES,
                (F32)src->getWidth(), (F32)src->getHeight());

            {
                LLGLDepthTest depth_test(GL_TRUE, GL_TRUE, GL_ALWAYS);
                pipeline.mScreenTriangleVB->setBuffer();
                pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
            }

            gDeferredPostNoDoFNoiseProgram.unbind();
            return;
        }

        // Fallback: real shader failed to compile - original placeholder
        // blit, unchanged since task #137.
        DXShader* shader = nullptr;
        ID3D11SamplerState* sampler = nullptr;
        if (!getPresentShader(&shader, &sampler))
        {
            return;
        }

        ID3D11DeviceContext* ctx = gDXDevice.getContext();
        ID3D11ShaderResourceView* srv = src->getColorSRV(0);

        ctx->VSSetShader(shader->getVS(), nullptr, 0);
        ctx->PSSetShader(shader->getPS(), nullptr, 0);
        ctx->IASetInputLayout(nullptr);
        DXStateCache::setPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        ID3D11Buffer* null_vb = nullptr;
        UINT stride = 0;
        UINT offset = 0;
        ctx->IASetVertexBuffers(0, 1, &null_vb, &stride, &offset);

        ctx->PSSetShaderResources(0, 1, &srv);
        ctx->PSSetSamplers(0, 1, &sampler);

        ctx->Draw(3, 0);

        ID3D11ShaderResourceView* null_srv = nullptr;
        ctx->PSSetShaderResources(0, 1, &null_srv);

        // S24 (2026-07-26): THE root cause of "zero textures, zero fonts"
        // system-wide. This fallback binds its placeholder VS/PS via raw
        // ctx->VSSetShader()/PSSetShader() calls, completely bypassing
        // LLHLSLShader::bind() - so LLHLSLShader::sCurBoundShaderPtr (the
        // bookkeeping bind() uses to skip redundant VSSetShader/PSSetShader
        // calls, see its "if (sCurBoundShaderPtr != this)" check) never learns
        // the real bound shader changed. Every later gUIProgram.bind() call
        // this frame (fonts, icons - anything routed through DXUIBatch) would
        // see sCurBoundShaderPtr still (wrongly) claiming gUIProgram is
        // current, skip the real rebind, and silently keep running THIS
        // placeholder shader instead - which takes no vertex-buffer input at
        // all (builds its fullscreen triangle purely from SV_VertexID),
        // ignoring every real position/UV/color the rest of the frame feeds
        // it. gSolidColorProgram.bind() only "worked" because it's a
        // different object, so its identity check always failed and forced a
        // real rebind - explaining why solid-color UI rendered fine while
        // every textured draw silently didn't. Calling the real unbind() here
        // (flushes, zeroes the actual VS/PS, nulls sCurBoundShaderPtr) forces
        // the next real bind() call, whichever shader it is, to issue a
        // genuine VSSetShader/PSSetShader instead of trusting stale state.
        LLHLSLShader::unbind();
    }
}

// static
void DXPipeline::presentDeferredScreen(LLPipeline& pipeline)
{
    // S24 (2026-08-04): prefer the real lit result (mRT->screen, written by
    // renderDeferredLighting()) over deferredScreen's raw, unlit diffuse
    // attachment - but ONLY when renderDeferredLighting() actually ran and
    // completed THIS frame (sScreenLitThisFrame). Originally this checked
    // `gDeferredSoftenProgram.isComplete() && mRT->screen.getColorSRV(0)
    // != nullptr` - a real bug: mRT->screen is allocated unconditionally
    // every frame regardless of DX_RENDER (see allocateScreenBufferInternal()),
    // so its SRV is always non-null even on a frame where
    // renderDeferredLighting() early-returned (e.g. RenderDeferredAtmospheric
    // off) without writing anything - that combination made this branch
    // blit whatever stale/never-written content mRT->screen happened to
    // hold, causing a full black-screen regression. sScreenLitThisFrame is
    // reset to false at the top of every renderDeferredLighting() call and
    // only set true after a real, completed write - a correctness
    // guarantee the old check never had.
    bool use_lit = sScreenLitThisFrame && pipeline.mRT->screen.getColorSRV(0) != nullptr;

    LLRenderTarget* diffuse_rt = use_lit ? &pipeline.mRT->screen : &pipeline.mRT->deferredScreen;
    if (!diffuse_rt->getColorSRV(0))
    {
        // Nothing has allocated the deferred screen's diffuse attachment
        // yet (e.g. very first frame(s) during startup) - nothing to
        // present, leave the back buffer as DXContext::beginFrame() left it.
        return;
    }

    // S24 (2026-08-05): only the genuinely lit result needs gamma
    // correction - the unlit fallback (deferredScreen's raw diffuse
    // attachment, shown when renderDeferredLighting() didn't run this
    // frame) is a debug/startup view, not real lighting output, so it
    // keeps going through the existing raw placeholder blit below
    // unchanged.
    //
    // LLPipeline::tonemap() (pipeline.cpp) is GL's real per-frame present
    // pass when RenderHDREnabled is on - postDeferredTonemap.hlsl, which
    // tonemaps THEN gamma-corrects. When RenderHDREnabled is off, GL falls
    // back to LLPipeline::gammaCorrect() (postDeferredGammaCorrect.hlsl -
    // linear_to_srgb() + a hard clamp(0,1), no tonemap curve at all) -
    // see renderFinalize() (pipeline.cpp:8482-8506): `if (hdr) { ...
    // tonemap(...); } else { gammaCorrect(...); }`. presentDeferredScreen()
    // previously called ONLY the gammaCorrect family, unconditionally -
    // correct for the HDR-off case (matches GL exactly), but wrong for the
    // HDR-on case (RenderHDREnabled defaults to true), where GL's real
    // tonemap curve gracefully compresses bright values instead of hard-
    // clamping them - root cause of task #164's "sky streaks/disable haze
    // -> pure white" (any linear value over 1.0, exactly what an
    // unattenuated/bright sky produces, clipped straight to white). The
    // tonemap shaders were already fully HLSL-ported
    // (postDeferredTonemap.hlsl/tonemapUtilF.hlsl) but never wired here,
    // and tonemapUtilF.hlsl's own port was itself incomplete (hardcoded
    // ACES Hill only, no RenderTonemapType switch, no exposure/tonemap_mix)
    // until this same task #164 fix completed it. Mirrors
    // LLPipeline::tonemap()'s own no_post/legacy_gamma shader selection and
    // uniform uploads exactly (pipeline.cpp:7580-7646), gated the same way
    // GL gates it.
    //
    // Known limitation, not yet fixed: generateExposure()/generateLuminance()
    // (the temporal auto-exposure chain feeding mExposureMap every frame
    // when HDR is on) are still DX_RENDER no-ops - not part of tasks
    // #137-141's post-fx chain scope (see dxpipeline.h's presentDeferredScreen()
    // comment) - mExposureMap stays at its static neutral (1,1,1,0) clear
    // value (see allocateScreenBufferInternal()'s DX_RENDER clearColor()
    // fix, task #164) rather than genuinely adapting to scene luminance.
    // The tonemap CURVE itself is correct either way; only the auto-
    // exposure adaptation is a placeholder for now.
    //
    // S24 (2026-08-11, task #156, SSR milestone 4): copyScreenSpaceReflections()
    // is DIFFERENT from the two above - already fully DX-safe as written
    // (gDX.getTexUnit()->bind()/LLGLDepthTest/mScreenTriangleVB/
    // gCopyDepthProgram, all already-proven primitives), it just never had
    // a DX_RENDER call site since GL's own call to it lives inside
    // renderFinalize(), whose GL body this function replaces entirely.
    // Mirrors GL's own order (copy happens before tonemap in
    // renderFinalize()) - mSceneMap gets this frame's just-finished lit
    // scene, consumed by the next frame's SSR ray march (a standard
    // temporal-reuse technique, not a bug - see the session plan's task
    // #156 section).
    if (use_lit && LLPipeline::RenderScreenSpaceReflections)
    {
        pipeline.copyScreenSpaceReflections(&pipeline.mRT->screen, &pipeline.mSceneMap);
    }

    if (use_lit)
    {
        static LLCachedControl<bool> should_auto_adjust(gSavedSettings, "RenderSkyAutoAdjustLegacy", false);
        static LLCachedControl<bool> buildNoPost(gSavedSettings, "RenderDisablePostProcessing", false);
        static LLCachedControl<F32> exposure_setting(gSavedSettings, "RenderExposure", 1.f);
        static LLCachedControl<U32> tonemap_type_setting(gSavedSettings, "RenderTonemapType", 0U);
        static LLCachedControl<bool> has_hdr_setting(gSavedSettings, "RenderHDREnabled", true);
        LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();

        bool use_tonemap = has_hdr_setting;
        bool legacy_gamma = (!psky || psky->getReflectionProbeAmbiance(should_auto_adjust) == 0.f);
        bool no_post = gSnapshotNoPost || legacy_gamma || (buildNoPost && gFloaterTools && gFloaterTools->isAvailable());

        LLHLSLShader& gamma_shader = use_tonemap
            ? (legacy_gamma
                ? (no_post ? gNoPostTonemapLegacyGammaCorrectProgram : gDeferredPostTonemapLegacyGammaCorrectProgram)
                : (no_post ? gNoPostTonemapGammaCorrectProgram : gDeferredPostTonemapGammaCorrectProgram))
            : (legacy_gamma ? gLegacyPostGammaCorrectProgram : gDeferredPostGammaCorrectProgram);

        if (gamma_shader.isComplete())
        {
            // S24 (2026-08-09, task #137): was writing directly to the back
            // buffer here - now writes to mPostPingMap (a real LLRenderTarget,
            // already unconditionally allocated regardless of DX_RENDER, see
            // allocateScreenBufferInternal() - same GL_RGBA scratch buffer
            // GL's own post-fx chain ping-pongs through) instead, so later
            // stages (glow, DoF, FXAA/SMAA) have a real intermediate to read
            // from and write to before anything reaches the screen, matching
            // GL's own gammaCorrect(&mRT->screen, &mPostPingMap) shape
            // (pipeline.cpp:8491) instead of fusing gamma-correct and present
            // into one step. presentFinal() below is the single "put this on
            // screen" chokepoint every stage below eventually feeds.
            // false: mPostPingMap has no depth attachment of its own (allocated
            // as a plain GL_RGBA scratch buffer, see allocateScreenBufferInternal())
            // - explicit here rather than relying on bindTarget()'s bind_depth=true
            // default incidentally being harmless against a null mDSV.
            pipeline.mPostPingMap.bindTarget(false);

            gamma_shader.bind();
            gamma_shader.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, &pipeline.mRT->screen, false, LLTexUnit::TFO_POINT);
            gamma_shader.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (F32)pipeline.mRT->screen.getWidth(), (F32)pipeline.mRT->screen.getHeight());

            // S24 (task #164): matches LLPipeline::gammaCorrect()'s own
            // shader (postDeferredGammaCorrect.hlsl) not declaring/using
            // exposureMap/exposure/tonemap_mix/tonemap_type at all - only
            // the tonemap family (postDeferredTonemap.hlsl) needs these.
            if (use_tonemap)
            {
                gamma_shader.bindTexture(LLShaderMgr::EXPOSURE_MAP, &pipeline.mExposureMap);

                static LLStaticHashedString s_exposure("exposure");
                static LLStaticHashedString s_tonemap_mix("tonemap_mix");
                static LLStaticHashedString s_tonemap_type("tonemap_type");
                F32 e = llclamp((F32)exposure_setting(), 0.5f, 4.f);
                gamma_shader.uniform1f(s_exposure, e);
                gamma_shader.uniform1i(s_tonemap_type, tonemap_type_setting);
                gamma_shader.uniform1f(s_tonemap_mix, psky ? psky->getTonemapMix(should_auto_adjust()) : 1.f);
            }

            pipeline.mScreenTriangleVB->setBuffer();
            pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

            gamma_shader.unbind();
            pipeline.mPostPingMap.flush();

            // S24 (2026-08-09, task #138): glow/bloom. LLPipeline::generateGlow()/
            // combineGlow() (pipeline.cpp:7702/8152) are called directly here,
            // unmodified - not reimplemented as DXPipeline:: versions like
            // renderGeomDeferred()/renderDeferredLighting() were, because
            // every single thing they touch (mGlow[0..2] - real LLRenderTarget
            // members, unconditionally allocated same as mPostPingMap;
            // gGlowExtractProgram/gGlowProgram/gGlowCombineProgram - real,
            // complete HLSL, not stubs, confirmed by reading all three;
            // enableTexture()/bindTexture(S32,LLRenderTarget*,...)/GL_BLEND -
            // all already DX-safe from earlier sessions; mDXTrueNoiseMap -
            // already built and wired for exactly this call) was already
            // individually hardened for DX_RENDER over the course of this
            // whole port. The only real gap was uniform2f(U32,...)/
            // uniform3f(U32,...) (GLOW_DELTA, GLOW_LUM_WEIGHTS,
            // GLOW_WARMTH_WEIGHTS, DEFERRED_SCREEN_RES) being silently a
            // no-op under DX_RENDER - fixed at the source (llhlslshader.cpp)
            // rather than worked around here, so every other existing/future
            // caller benefits too, not just glow.
            pipeline.generateGlow(&pipeline.mPostPingMap);

            // S24 (2026-08-23, task #160 CLOSED): a temporary mGlow[1]
            // readback diagnostic lived here briefly and answered its
            // question decisively - mGlow[1] was unconditionally empty
            // (0,0,0,0 across a 3x3 grid) regardless of scene content, even
            // with a full-frame 100%-glow test object on screen. Traced to
            // the real root cause: glowExtractV.hlsl (the extract pass's
            // vertex shader) declared a `texcoord0 : TEXCOORD0` vertex
            // attribute that LLPipeline::mScreenTriangleVB (the buffer this
            // pass actually draws with) never supplied - confirmed via a
            // live D3D11 debug-layer log, CreateInputLayout() failing
            // outright with E_INVALIDARG for "Glow Extract Shader (Post)
            // (+Noise)". With no valid input layout, the extract draw call
            // never ran at all, so mGlow[2] (and everything downstream)
            // stayed permanently empty - independent of what was on screen,
            // exactly matching the diagnostic's result. Fixed at the source
            // (glowExtractV.hlsl) to match this codebase's own established,
            // proven-working full-screen-triangle convention (see
            // postDeferredV.hlsl): position-only input, texcoord derived
            // procedurally, matching the real GLSL source exactly.

            LLRenderTarget* sourceBuffer = &pipeline.mPostPingMap;
            LLRenderTarget* targetBuffer = &pipeline.mPostPongMap;

            pipeline.combineGlow(sourceBuffer, targetBuffer);
            std::swap(sourceBuffer, targetBuffer);

            // S24 (2026-08-17, tasks #140/#141): DoF and FXAA/SMAA. Both
            // called directly, unmodified, exactly like generateGlow()/
            // combineGlow() above (task #138) - every primitive they touch
            // (bindTarget()/flush(), LLGLDepthTest, mScreenTriangleVB,
            // bindTexture(S32,LLRenderTarget*,...), the CoF/DoF-combine/FXAA/
            // SMAA HLSL shaders themselves) was already DX-hardened by
            // earlier stages of this port. The only real DX_RENDER gap in
            // either function was 3 raw glViewport() calls (renderDoF() x2,
            // applyFXAA() x1) which assumed an implicit GL default framebuffer
            // viewport - fixed at the source in pipeline.cpp itself (guarded
            // #ifdef DX_RENDER, using DXContext::setViewport()), the same
            // "fix at the source, not here" approach task #138 used for the
            // uniform2f/3f no-op gap. Mirrors GL's own renderFinalize() order
            // exactly (pipeline.cpp:8524-8542): DoF is optional (RenderDepthOfField),
            // then exactly one of FXAA or SMAA runs depending on RenderFSAAType.
            if ((LLPipeline::RenderDepthOfFieldInEditMode || !LLToolMgr::getInstance()->inBuildMode()) &&
                LLPipeline::RenderDepthOfField &&
                !gCubeSnapshot)
            {
                pipeline.renderDoF(sourceBuffer, targetBuffer);
                std::swap(sourceBuffer, targetBuffer);
            }

            if (LLPipeline::RenderFSAAType == 1)
            {
                pipeline.applyFXAA(sourceBuffer, targetBuffer);
                std::swap(sourceBuffer, targetBuffer);
            }
            else if (LLPipeline::RenderFSAAType == 2)
            {
                pipeline.generateSMAABuffers(sourceBuffer);
                pipeline.applySMAA(sourceBuffer, targetBuffer);
                std::swap(sourceBuffer, targetBuffer);
            }

            // S24 (2026-08-27, task #267 GL-tail audit follow-up):
            // Develop > Rendering > Buffer Visualization had zero call sites
            // under DX_RENDER - its only 4 call sites (pipeline.cpp) sit
            // inside LLPipeline::renderFinalize()'s GL-only tail, unreachable
            // past this function's own early return. visualizeBuffers()
            // itself was already backend-agnostic (bindTarget()/flush(),
            // gDeferredBufferVisualProgram.bind(), mScreenTriangleVB - all
            // already DX-hardened, task #261's lazy-compile fix already
            // covers this exact shader) - it just needed a caller. Mirrors
            // GL's own renderFinalize() ordering exactly (pipeline.cpp:8676-
            // 8704): after FXAA/SMAA, before the OpenCL effects block.
            if (LLPipeline::RenderBufferVisualization > -1)
            {
                switch (LLPipeline::RenderBufferVisualization)
                {
                case 0:
                case 1:
                case 2:
                case 3:
                    pipeline.visualizeBuffers(&pipeline.mRT->deferredScreen, sourceBuffer, LLPipeline::RenderBufferVisualization);
                    break;
                case 4:
                    pipeline.visualizeBuffers(&pipeline.mLuminanceMap, sourceBuffer, 0);
                    break;
                case 5:
                    if (LLPipeline::RenderFSAAType > 0)
                    {
                        pipeline.visualizeBuffers(&pipeline.mFXAAMap, sourceBuffer, 0);
                    }
                    break;
                case 6:
                    if (LLPipeline::RenderFSAAType == 2)
                    {
                        pipeline.visualizeBuffers(&pipeline.mSMAABlendBuffer, sourceBuffer, 0);
                    }
                    break;
                default:
                    break;
                }
            }

            // S24 (2026-08-17): OpenCL post-fx effects (kveffects.cpp) -
            // mirrors GL's own renderFinalize() "Apply effects BEFORE final
            // draw" block exactly (pipeline.cpp, right before the
            // gDeferredPostNoDoFNoiseProgram present). updateEffectMask()/
            // effectsMask live in pipeline.cpp and are normally refreshed by
            // GL's own renderFinalize() body - that body never runs under
            // DX_RENDER (early-returns into this function instead), so both
            // have to be called/read from here. The OpenCL kernel pipeline
            // itself (kvopencl.cpp) was already 100% backend-agnostic - only
            // the read/write bookends needed a DX_RENDER branch (real
            // D3D11 texture, not the GL-CL interop the class comments
            // mention but which the actual implementation never used - see
            // kveffects.h's class comment). No glFinish()-equivalent needed
            // before the read: DXReadback::readPixels()'s Map(D3D11_MAP_READ)
            // is already a synchronous GPU/CPU coherence point.
            updateEffectMask();
            if (effectsMask != 0)
            {
                ID3D11Texture2D* effects_tex = sourceBuffer->getDXColorTexture(0);
                if (effects_tex)
                {
                    int width = sourceBuffer->getWidth();
                    int height = sourceBuffer->getHeight();

                    if (effectsMask & DESATURATION) ImageProcessor::desaturateImageGPU(effects_tex, width, height);
                    if (effectsMask & INVERT) ImageProcessor::invertImageGPU(effects_tex, width, height);
                    if (effectsMask & RGB_CONTROL) ImageProcessor::RGBControlGPU(effects_tex, width, height);
                    if (effectsMask & CEL_SHADING) ImageProcessor::celShadeImageGPU(effects_tex, width, height);
                    if (effectsMask & VIGNETTE) ImageProcessor::vignetteGPU(effects_tex, width, height);
                    if (effectsMask & EDGE_GLOW) ImageProcessor::edgeGlowGPU(effects_tex, width, height);
                    if (effectsMask & NIGHT_VISION) ImageProcessor::nightVisionGPU(effects_tex, width, height);
                    if (effectsMask & MOTION_BLUR) ImageProcessor::motionBlurGPU(effects_tex, width, height);
                }
            }

            // S24 (2026-08-17, task #139): real final present - see
            // presentFinal()'s own comment for what changed.
            // S24 (2026-08-26, task #263): expose which buffer this was for
            // rawSnapshot() - see gLastCompositedPostTarget's own comment.
            gLastCompositedPostTarget = sourceBuffer;
            presentFinal(pipeline, sourceBuffer);
            return;
        }
        // gamma_shader failed to compile - fall through to the raw
        // placeholder blit below rather than presenting nothing.
    }

    // Startup/fallback case (not lit yet this frame, or gamma shader failed
    // to compile) - present deferredScreen's raw diffuse straight through,
    // same as always. No post-fx chain to run in this case (nothing has
    // been gamma-corrected yet, so glow/DoF/FXAA would have nothing
    // meaningful to operate on either) - keep this path exactly as simple
    // as it always was.
    // S24 (2026-08-26, task #263): see the other presentFinal() call site's
    // comment above - startup/fallback frames still need a valid pointer.
    gLastCompositedPostTarget = diffuse_rt;
    presentFinal(pipeline, diffuse_rt);
}

// static
void DXPipeline::renderDeferredLighting(LLPipeline& pipeline)
{
    // S24 (2026-08-04): v1 - ambient+sun/atmospherics only (softenLightF/
    // V.hlsl), mirroring the "RenderDeferredAtmospheric" block of
    // LLPipeline::renderDeferredLighting()'s GL body (pipeline.cpp,
    // ~line 9075) exactly.
    // S24 (2026-08-27, GL-tail audit): the note that used to sit here
    // ("deliberately NOT here yet: sun-shadow/SSAO lightmap pass, local
    // lights") is stale - both were added later, further down in this same
    // function: the sun-shadow/SSAO lightmap pass at task #158's fix (see
    // below), local lights/spotlights at task #165 (see below). Left as a
    // pointer for anyone still relying on the old note: read the rest of
    // this function, not dxpipeline.h's now-outdated summary of it.
    // S24 (2026-08-04): reset every frame BEFORE any early-return below,
    // so presentDeferredScreen() never mistakes a skipped frame for a lit
    // one - see sScreenLitThisFrame's own comment (root cause of a
    // black-screen regression this fixes).
    sScreenLitThisFrame = false;

    if (!gDeferredSoftenProgram.isComplete())
    {
        // Shader didn't compile (or isn't loaded) - leave mRT->screen
        // untouched. presentDeferredScreen() falls back to its existing
        // raw deferredScreen blit, so this is a graceful no-op, not a
        // missing frame.
        return;
    }

    // S24 (2026-08-15, task #165): this used to be a whole-function early
    // return - GL's own RenderDeferredAtmospheric gate (pipeline.cpp:9134)
    // wraps ONLY the soften/ambient shader draw call itself, not the sun/
    // moon-dir transform, not the shadow/SSAO lightmap pass, and NOT the
    // local-light/spotlight loops after it (confirmed: GL's local-light
    // gate at pipeline.cpp:9172 sits entirely outside/after the atmospheric
    // block). Returning here unconditionally skipped ALL local and
    // projected lights the moment a user/config had RenderDeferredAtmospheric
    // off - a real, confirmed, structural bug (found via a user-compiled
    // deep-dive report on task #165, verified against source before
    // applying). The gate is now narrowed to just the soften-shader block
    // below, matching GL's actual scope exactly.

    // S24 (2026-08-10, task #158, ROOT CAUSE FIX): mTransformedSunDir/
    // mTransformedMoonDir (the eye-space-transformed light directions that
    // feed shadowUtil.hlsl's sun_dir/moon_dir uniforms, via bindDeferredShader()'s
    // shader.uniform3fv(DEFERRED_SUN_DIR/MOON_DIR, ...) calls) were only ever
    // computed inside LLPipeline::renderDeferredLighting()'s GL body
    // (pipeline.cpp ~9028-9038: setupHWLights() + `mat * tc` eye-space
    // transform) - entirely skipped under DX_RENDER since that function
    // redirects here instead. Left at their zero-initialized default, this
    // meant sun_dir/moon_dir arrived in the shader as (0,0,0) - and
    // normalize((0,0,0)) is NaN in HLSL, not a zero vector. That NaN then
    // propagated into sampleDirectionalShadow()'s `spos.z`, and any
    // comparison against NaN (including the `spos.z > -shadow_clip.w` guard
    // gating all 4 shadow cascades) is unconditionally false per IEEE 754 -
    // so pcfShadow() was NEVER being called, for any pixel, any frame, any
    // sun position, regardless of shadow_matrix/shadow_clip/comparison-
    // sampler correctness (all separately confirmed fine this session).
    // Root cause of task #158's entire "shadows never show up" mystery -
    // confirmed via a temporary shader-side diagnostic tap (see
    // shadowUtil.hlsl's history) showing spos.z reading exactly "nan" in
    // every single logged sample. Fixed by porting GL's own two-step
    // computation verbatim: setupHWLights() refreshes mSunDir/mMoonDir from
    // the current environment (also already called elsewhere under
    // DX_RENDER, per dxpipeline.cpp's own renderGeomPostDeferred() (line
    // ~597) - calling it again here is redundant-but-harmless, matching
    // GL's own unconditional placement rather than trying to prove it's
    // unnecessary), then the
    // current modelview matrix transforms both into eye-space.
    pipeline.setupHWLights();
    {
        glm::mat4 mat = get_current_modelview();

        glm::vec4 tc(pipeline.mSunDir);
        tc = mat * tc;
        pipeline.mTransformedSunDir.set(tc);

        glm::vec4 tc_moon(pipeline.mMoonDir);
        tc_moon = mat * tc_moon;
        pipeline.mTransformedMoonDir.set(tc_moon);
    }

    // S24 (2026-08-10, task #158 milestone 1): real sun-shadow/SSAO lightmap
    // pass, replacing the earlier neutral-white stopgap (see the open-issues
    // ledger for that fix's original reasoning, still valid as the
    // "shadows off" fallback below). Mirrors LLPipeline::renderDeferredLighting()'s
    // GL body (pipeline.cpp, ~line 9033's `if ((RenderDeferredSSAO &&
    // !gCubeSnapshot) || RenderShadowDetail > 0)` block) - gDeferredSunProgram
    // (sunLightF/SSAOF.hlsl, already-ported HLSL, chosen between the two at
    // shader-load time via llviewershadermgr.cpp's own use_ao flag) reads
    // shadowMap0-5 (bound automatically by bindDeferredShader()'s existing
    // bindShadowMaps() call, task #124 - the comparison-sampler chokepoint
    // was already fixed there, this pass is simply the first real caller of
    // it) and writes a per-pixel shadow/spot-shadow occlusion factor into
    // mRT->deferredLight. Deliberately NOT ported in this v1: the gaussian
    // blur/soften pass GL runs afterward when RenderDeferredSSAO is on
    // (pipeline.cpp ~9062-9122, gDeferredBlurLightProgram) - the per-pixel
    // SSAO term itself is computed correctly without it (SSAO's math lives
    // in sunLightSSAOF.hlsl's main(), not the blur), just less smoothed.
    // Real follow-up, not a silently-wrong placeholder - same v1-then-extend
    // pattern already used for local lights/spotlights earlier this session.
    LLRenderTarget& deferred_light_target = pipeline.mRT->deferredLight;

    // S24 (2026-08-23, task #190 temporal-SSAO follow-up): snapshot last
    // frame's fully-resolved lightmap into mSSAOHistory BEFORE either branch
    // below overwrites deferred_light_target with this frame's data - same
    // "blit the still-intact previous value before it's clobbered" pattern
    // as LLPipeline::generateExposure()'s mLastExposure refresh
    // (pipeline.cpp:7509-7520), reused verbatim (gCopyProgram + texunit 0 +
    // mScreenTriangleVB). No first-frame guard needed: mSSAOHistory is
    // pre-seeded to (1,1,1,1) at allocation time (allocateScreenBufferInternal(),
    // pipeline.cpp) for exactly this reason, mirroring mExposureMap's own
    // DX_RENDER seed.
    if (LLPipeline::RenderDeferredSSAO && !gCubeSnapshot)
    {
        pipeline.mSSAOHistory.bindTarget();
        gCopyProgram.bind();
        gDX.getTexUnit(0)->bind(&deferred_light_target);
        pipeline.mScreenTriangleVB->setBuffer();
        pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
        pipeline.mSSAOHistory.flush();
    }

    if ((LLPipeline::RenderDeferredSSAO && !gCubeSnapshot) || LLPipeline::RenderShadowDetail > 0)
    {
        deferred_light_target.bindTarget();
        deferred_light_target.clearColor(1.0f, 1.0f, 1.0f, 1.0f);

        LLHLSLShader& sun_shader = gCubeSnapshot ? gDeferredSunProbeProgram : gDeferredSunProgram;
        if (sun_shader.isComplete())
        {
            pipeline.bindDeferredShader(sun_shader, &deferred_light_target);

            sun_shader.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES,
                (F32)deferred_light_target.getWidth(),
                (F32)deferred_light_target.getHeight());

            {
                LLGLDisable   blend(GL_BLEND);
                LLGLDepthTest depth(GL_TRUE, GL_FALSE, GL_ALWAYS);
                LLGLDisable   cullface(GL_CULL_FACE);
                pipeline.mScreenTriangleVB->setBuffer();
                pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
            }

            pipeline.unbindDeferredShader(sun_shader);
        }
        deferred_light_target.flush();
    }
    else
    {
        // S24 (2026-08-05): shadows AND SSAO both off (the common default
        // case) - real lightmap pass above is skipped entirely, same as GL's
        // own early-out. Neutral "fully lit, no shadow/occlusion" fill, same
        // reasoning as the original stopgap: softenLightF.hlsl's scol_ambocc
        // read must never come back (0,0) ("fully shadowed/occluded") when
        // there's no real lightmap data behind it.
        deferred_light_target.bindTarget();
        deferred_light_target.clearColor(1.0f, 1.0f, 1.0f, 1.0f);
        deferred_light_target.flush();
    }

    LLRenderTarget* screen_target = &pipeline.mRT->screen;

    // S24 (2026-08-23, task #190 follow-up): real gaussian blur/soften pass
    // for the sun-shadow/SSAO lightmap - the "real follow-up" this file's
    // own sun-shadow block above already flagged as deliberately deferred
    // (task #158 milestone 1). Without this, calcAmbientOcclusion()'s raw
    // per-pixel occlusion term (aoUtil.hlsl) reaches softenLightF.hlsl
    // completely unsmoothed - correct math, but visibly noisy, and since
    // the noise sample is keyed to SCREEN position (not world position), a
    // moving camera sweeps each world point across different noise texels
    // frame to frame, reading as flicker that settles once the camera stops
    // (user-reported). Mirrors GL's own block exactly (pipeline.cpp
    // ~9178-9238, gDeferredBlurLightProgram / blurLightV+F.hlsl - already-
    // ported HLSL, confirmed correct: a real depth/normal-aware bilateral
    // blur that rejects samples across depth discontinuities via the
    // point-plane-distance test, so it won't smear AO across real edges).
    // Purely additive - only smooths the lightmap's RESULT; the underlying
    // SSAO computation itself is untouched.
    if (LLPipeline::RenderDeferredSSAO && !gCubeSnapshot)
    {
        screen_target->bindTarget();
        screen_target->clearColor(1.0f, 1.0f, 1.0f, 1.0f);

        pipeline.bindDeferredShader(gDeferredBlurLightProgram);

        LLVector3 go = LLPipeline::RenderShadowGaussian;
        const U32 kern_length = 4;
        F32 blur_size = LLPipeline::RenderShadowBlurSize;
        F32 dist_factor = LLPipeline::RenderShadowBlurDistFactor;

        // sample symmetrically with the middle sample falling exactly on 0.0
        F32 x = 0.f;
        LLVector3 gauss[32]; // xweight, yweight, offset
        for (U32 i = 0; i < kern_length; i++)
        {
            gauss[i].mV[0] = llgaussian(x, go.mV[0]);
            gauss[i].mV[1] = llgaussian(x, go.mV[1]);
            gauss[i].mV[2] = x;
            x += 1.f;
        }

        // S24: pipeline.cpp's own sDelta/sDistFactor/sKern/sKernScale are
        // file-static there, not visible from this file - redeclared here
        // with identical string literals (must match blurLightF.hlsl's
        // uniform names exactly: delta/dist_factor/kern/kern_scale).
        static LLStaticHashedString sBlurDelta("delta");
        static LLStaticHashedString sBlurDistFactor("dist_factor");
        static LLStaticHashedString sBlurKern("kern");
        static LLStaticHashedString sBlurKernScale("kern_scale");

        gDeferredBlurLightProgram.uniform2f(sBlurDelta, 1.f, 0.f);
        gDeferredBlurLightProgram.uniform1f(sBlurDistFactor, dist_factor);
        gDeferredBlurLightProgram.uniform3fv(sBlurKern, kern_length, gauss[0].mV);
        gDeferredBlurLightProgram.uniform1f(sBlurKernScale, blur_size * (kern_length / 2.f - 0.5f));

        {
            LLGLDisable   blend(GL_BLEND);
            LLGLDepthTest depth(GL_TRUE, GL_FALSE, GL_ALWAYS);
            pipeline.mScreenTriangleVB->setBuffer();
            pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
        }

        screen_target->flush();
        pipeline.unbindDeferredShader(gDeferredBlurLightProgram);

        // Second (vertical) pass: read the horizontally-blurred result back
        // from screen_target, write the final blurred lightmap into
        // deferred_light_target - matches GL's own ping-pong exactly.
        pipeline.bindDeferredShader(gDeferredBlurLightProgram, screen_target);

        deferred_light_target.bindTarget();

        gDeferredBlurLightProgram.uniform2f(sBlurDelta, 0.f, 1.f);

        {
            LLGLDisable   blend(GL_BLEND);
            LLGLDepthTest depth(GL_TRUE, GL_FALSE, GL_ALWAYS);
            pipeline.mScreenTriangleVB->setBuffer();
            pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
        }
        deferred_light_target.flush();
        pipeline.unbindDeferredShader(gDeferredBlurLightProgram);
    }

    // S24 (2026-08-23, task #190 temporal-SSAO follow-up): temporal-resolve
    // pass - reprojects mSSAOHistory (blitted above, still last frame's
    // value) into this frame's screen space and blends it with the
    // just-blurred deferred_light_target, then blits the result back so the
    // soften pass below (unmodified) sees the final value. Reads depth as
    // an SRV (via getPosition() in temporalResolveSSAOF.hlsl), so - exactly
    // like the soften pass immediately below - screen_target must be bound
    // WITHOUT its shared depth attached (bindTarget(false)); binding it with
    // depth here would hit the identical D3D11 OM/SRV hazard already found
    // and fixed for the soften pass (see that pass's own comment just below).
    if (LLPipeline::RenderDeferredSSAO && !gCubeSnapshot)
    {
        screen_target->bindTarget(false);
        screen_target->clear(GL_COLOR_BUFFER_BIT);

        pipeline.bindDeferredShader(gDeferredTemporalResolveSSAOProgram, &deferred_light_target);

        S32 history_channel = gDeferredTemporalResolveSSAOProgram.enableTexture(LLShaderMgr::DEFERRED_SSAO_HISTORY_MAP);
        if (history_channel > -1)
        {
            gDX.getTexUnit(history_channel)->bind(&pipeline.mSSAOHistory);
        }

        // S24: uploaded manually rather than relying on bindDeferredShader()'s
        // own bindReflectionProbes() call for inv_modelview_delta -
        // bindReflectionProbes() early-returns entirely when
        // sReflectionProbesEnabled is false (pipeline.cpp:10035-10038), which
        // would leave this shader's inv_modelview_delta uninitialized
        // whenever the user has reflection probes off. Same formula as
        // bindReflectionProbes() (pipeline.cpp:10130-10133), computed
        // independently so this pass never depends on that unrelated toggle.
        {
            glm::mat4 cur_modelview = get_current_modelview();
            glm::mat4 last_modelview = get_last_modelview();
            glm::mat4 inv_modelview_delta = glm::inverse(cur_modelview * glm::inverse(last_modelview));
            glm::mat4 last_projection = get_last_projection();
            gDeferredTemporalResolveSSAOProgram.uniformMatrix4fv(LLShaderMgr::INVERSE_MODELVIEW_DELTA_MATRIX, 1, false, glm::value_ptr(inv_modelview_delta));
            gDeferredTemporalResolveSSAOProgram.uniformMatrix4fv(LLShaderMgr::LAST_PROJECTION_MATRIX, 1, false, glm::value_ptr(last_projection));
        }

        {
            LLGLDisable   blend(GL_BLEND);
            LLGLDepthTest depth(GL_TRUE, GL_FALSE, GL_ALWAYS);
            LLGLDisable   cullface(GL_CULL_FACE);
            pipeline.mScreenTriangleVB->setBuffer();
            pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
        }

        screen_target->flush();
        pipeline.unbindDeferredShader(gDeferredTemporalResolveSSAOProgram);

        // Blit the resolved result back into deferred_light_target so the
        // soften pass below needs no changes.
        deferred_light_target.bindTarget();
        gCopyProgram.bind();
        gDX.getTexUnit(0)->bind(screen_target);
        pipeline.mScreenTriangleVB->setBuffer();
        pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
        deferred_light_target.flush();
    }

    // S24 (2026-08-05): bindTarget(false) - screen_target shares its depth
    // buffer with deferredScreen (LLPipeline::allocateScreenBufferInternal(),
    // pipeline.cpp:911), and this pass reads that SAME depth buffer as an
    // SRV via bindDeferredShader()'s DEFERRED_DEPTH bind just below. D3D11
    // forbids a resource being bound as an OM (render target/depth-stencil)
    // output and a shader-stage SRV input at the same time - binding with
    // the DSV attached here silently forced the depth SRV to NULL
    // (DXDebugLayer: "Resource being set to PS shader resource slot 1 is
    // still bound on output! Forcing to NULL", context='Deferred Soften
    // Shader'), which is the actual root cause of depth reading back as a
    // flat 0 in softenLightF.hlsl's getDepth() - not a coordinate/format
    // bug in the depth SRV itself (that part was already fixed correctly).
    // This pass doesn't depth-test/write anyway (LLGLDepthTest(GL_FALSE)
    // below), so leaving the DSV unbound here is correct, not a workaround.
    screen_target->bindTarget(false);
    // S24: matches GL's screen_target->clear(GL_COLOR_BUFFER_BIT) (see
    // pipeline.cpp:9079) - color only. The previous default-mask clear()
    // here also cleared the shared depth buffer (mUseDepth is true, having
    // been propagated by shareDepthBuffer()) back to its 1.0 far-clear
    // value on every call, destroying the real depth just written by the
    // opaque pass - a second, independent bug alongside the DSV-hazard one
    // above, both hit by the same line.
    screen_target->clear(GL_COLOR_BUFFER_BIT);

    LLHLSLShader& soften_shader = gDeferredSoftenProgram;

    // S24 (2026-08-15, task #165): narrowed from a whole-function early
    // return (see this function's own opening comment) to match GL's real
    // scope (pipeline.cpp:9134-9167) - only the soften/ambient draw itself
    // is atmospheric-gated. When off, screen_target stays at the plain
    // clear() above (matches GL: local lights then additively blend onto a
    // black base with no ambient/sun contribution, not a fully blank frame).
    if (LLPipeline::RenderDeferredAtmospheric)
    {
        // S24 (2026-08-05): the "ground ambient frozen regardless of Reflection
        // Probe Ambiance slider" mystery this diagnostic chased is resolved -
        // the C++/shader uniform-upload chain was always correct; the real
        // cause was llsettingsvo.cpp's applySpecial() silently substituting a
        // fixed fallback constant for any nonzero ambiance whenever
        // LLPipeline::sReflectionProbesEnabled is false, which it always was
        // under DX_RENDER (LLFeatureManager had no real GPU data - see
        // LLGLManager::initGLDX(), llgl.cpp). Diagnostic removed.
        pipeline.bindDeferredShader(soften_shader);

        static LLCachedControl<F32> ssao_scale(gSavedSettings, "RenderSSAOIrradianceScale", 0.5f);
        static LLCachedControl<F32> ssao_max(gSavedSettings, "RenderSSAOIrradianceMax", 0.25f);
        static LLStaticHashedString ssao_scale_str("ssao_irradiance_scale");
        static LLStaticHashedString ssao_max_str("ssao_irradiance_max");

        soften_shader.uniform1f(ssao_scale_str, ssao_scale);
        soften_shader.uniform1f(ssao_max_str, ssao_max);

        LLEnvironment& environment = LLEnvironment::instance();

        soften_shader.uniform1i(LLShaderMgr::SUN_UP_FACTOR, environment.getIsSunUp() ? 1 : 0);
        soften_shader.uniform3fv(LLShaderMgr::LIGHTNORM, 1, environment.getClampedLightNorm().mV);

        soften_shader.uniform4fv(LLShaderMgr::WATER_WATERPLANE, 1, LLDrawPoolAlpha::sWaterPlane.mV);

        {
            LLGLDepthTest depth(GL_FALSE);
            LLGLDisable   blend(GL_BLEND);
            // S24 (2026-08-04): fullscreen post-process triangles shouldn't
            // rely on ambient rasterizer state (this is a raw
            // mScreenTriangleVB->drawArrays() call, not wrapped by any
            // LLDrawPool's own per-pool state setup) - disable explicitly.
            LLGLDisable   cullface(GL_CULL_FACE);

            // full screen blit
            pipeline.mScreenTriangleVB->setBuffer();
            pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
        }

        pipeline.unbindDeferredShader(soften_shader);
    }

    sScreenLitThisFrame = true;

    // S24 (2026-08-15): screen_target was left with NO depth-stencil view
    // attached at all (bindTarget(false) above, for the soften pass's own
    // depth-as-SRV-while-DSV hazard - see that call's comment) all the way
    // through the local-light loops below, which both explicitly request
    // LLGLDepthTest(GL_TRUE, GL_FALSE) - depth TEST on, write off. With no
    // DSV bound, D3D11 has nothing to test against regardless of that state
    // object, so every light box/fan rendered with zero occlusion against
    // opaque scene geometry (lights visible through walls/floors) - found
    // via adversarial review of the earlier "depth state matches GL" OM
    // diagnostic, which only ever compared depth-stencil STATE OBJECTS and
    // was structurally blind to the attachment itself being null. Re-attach
    // read-only (mReadOnlyDSV, not mDSV) rather than a plain
    // rebindWithDepth(true): the light shaders (pointLightF.hlsl/
    // spotLightF.hlsl) also sample this SAME depth buffer as an SRV via
    // bindDeferredShader()'s DEFERRED_DEPTH bind (getPosition()/getDepth(),
    // needed for world-position reconstruction and lightDist) - binding the
    // normal read/write mDSV here would silently re-trigger that exact
    // OM-output-vs-SRV-input hazard and null the depth SRV instead, breaking
    // every light's position math. A D3D11_DSV_READ_ONLY_DEPTH view is
    // exactly the sanctioned way to depth-test (read-only, which matches
    // write_enabled=false above anyway) and SRV-sample the same resource in
    // the same draw. Covers both the point-light loop below and the
    // spotlight loop after it (same enclosing scope, nothing rebinds
    // screen_target in between) - superseded by the later
    // rebindWithDepth(true) (non-read-only, for renderGeomPostDeferred()'s
    // real 3D alpha/glow geometry) further down this function.
    screen_target->rebindWithDepth(true, true);

    // S24 (2026-08-05): v1 of local point lights - mirrors GL's
    // RenderLocalLightCount-gated block (pipeline.cpp:9116+), scoped to
    // exactly the most common case: non-spotlight lights the camera is
    // OUTSIDE the bounding box of, drawn as additively-blended cube-volume
    // geometry (sDXBoxLightVB, a DX_RENDER-only non-indexed triangle-list
    // equivalent of GL's mCubeVB - see its own comment for why this
    // couldn't just reuse mCubeVB directly) via gDeferredLightProgram/
    // pointLightV/F.hlsl. Spotlights/projectors (gDeferredSpotLightProgram)
    // got their own v1 in task #143 (2026-08-09, see the spot_lights block
    // below, after the point-light loop) - setupSpotLight() turned out to
    // be pure math + already-DX-safe uniform/texture calls, reused directly
    // rather than reimplemented. Lights the camera is INSIDE the bounding
    // box of are drawn
    // via this same box-cube path rather than GL's separate fullscreen
    // gDeferredMultiLightProgram batch (see the "camera_outside" removal
    // comment below, in the loop itself, for why) - a real user test
    // (rezzing a light and standing next to it) confirmed skipping that
    // case entirely made every close-range light invisible, which is worse
    // than the visual imperfections this simpler approach may have at
    // extreme close range.
    {
        static LLCachedControl<S32> local_light_count(gSavedSettings, "RenderLocalLightCount", 256);
        static LLCachedControl<S32> probe_level(gSavedSettings, "RenderReflectionProbeLevel", 0);

        // S24 (2026-08-15, task #165): GL's matching gate (pipeline.cpp:9172)
        // is `local_light_count > 0 && (!gCubeSnapshot || probe_level > 0)` -
        // this was missing the second half entirely, so DX baked full-
        // strength local lights into every reflection-probe capture
        // (gCubeSnapshot=true) regardless of probe ambiance level, unlike
        // GL which suppresses them there by default. Combined with the
        // matching light_scale gap below (GL multiplies every light color
        // by mReflectionMapManager.mLightScale during capture, darkening
        // them further when probe ambiance is above 1 - DX used the raw
        // color), this meant DX probe captures were double-lit/washed-out
        // relative to GL. Found via a user-compiled deep-dive report on
        // task #165, verified against source before applying.
        F32 light_scale = 1.f;
        if (gCubeSnapshot)
        {
            light_scale = pipeline.mReflectionMapManager.getLightScale();
        }

        // S24 (2026-08-22, task #156 follow-up, removed 2026-08-23 pre-alpha
        // perf sweep): temporary diagnostic (light_scale/probe_level tap on
        // gCubeSnapshot passes) was still active and firing ~once/sec during
        // ordinary play, since RenderReflectionProbeLevel defaults to 3
        // (probes on) - not gated behind an opt-in setting the way the
        // hero-probe raw-capture diagnostic in llheroprobemanager.cpp is.
        // Question was never confirmed/ruled out before removal; revisit
        // from scratch if hero-probe light_scale cross-contamination is
        // suspected again.

        if (local_light_count > 0 && (!gCubeSnapshot || probe_level > 0))
        {
            // S24 (2026-08-15, task #165 - projected-light zoom-fade
            // report): GL's own body resets both spot-shadow target slots
            // every frame (pipeline.cpp:9180-9186, `if (!gCubeSnapshot) for
            // (i<2) mTargetShadowSpotLight[i]=NULL;`) BEFORE the priority
            // competition in setupSpotLight() (pipeline.cpp:9764-9787) runs
            // - this DX body had no equivalent anywhere (confirmed via a
            // full-file grep, zero references to mTargetShadowSpotLight in
            // this file). Without it, the competition in setupSpotLight()
            // (called from both the box-path and fullscreen-path spotlight
            // blocks below) compares each frame's candidate against
            // whatever won a slot on some PREVIOUS frame - potentially many
            // frames ago - instead of a clean slate, since nothing else in
            // this DX body ever clears these two pointers. Given
            // generateSunShadow()'s fade ramp (pipeline.cpp:11228-11249,
            // shared/backend-agnostic, confirmed reachable here since this
            // user's RenderShadowDetail=2 makes gen_shadow true) drives
            // PROJECTOR_SHADOW_FADE from the SAME mTargetShadowSpotLight
            // state, a stale/never-reset competition is a direct, plausible
            // contributor to a projected light's brightness swinging as its
            // priority (which rises on zoom-in, LLVOVolume::
            // updateSpotLightPriority()) changes relative to whatever stale
            // pointers it's being compared against. Mirrors GL exactly.
            if (!gCubeSnapshot)
            {
                for (U32 i = 0; i < 2; i++)
                {
                    pipeline.mTargetShadowSpotLight[i] = nullptr;
                }
            }

            // S24: DEFERRED_LIGHT_FALLOFF is a pipeline.cpp file-scope
            // const (anonymous to other translation units) - duplicated
            // here rather than exposed, it's a fixed tuning constant GL
            // itself never varies.
            const F32 deferred_light_falloff = 0.5f;

            LLViewerCamera*      camera = LLViewerCamera::getInstance();
            LLHLSLShader&        light_shader = gDeferredLightProgram;
            LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();

            gDX.setSceneBlendType(LLRender::BT_ADD);
            pipeline.bindDeferredShader(light_shader);

            if (sDXBoxLightVB.isNull())
            {
                sDXBoxLightVB = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX);
                sDXBoxLightVB->allocateBuffer(8 * 18, 0);
                LLStrider<LLVector3> pos;
                sDXBoxLightVB->getVertexStrider(pos);
                static const LLVector3 corners[8] =
                {
                    LLVector3(-1,-1,-1), LLVector3(-1,-1, 1), LLVector3(-1, 1,-1), LLVector3(-1, 1, 1),
                    LLVector3( 1,-1,-1), LLVector3( 1,-1, 1), LLVector3( 1, 1,-1), LLVector3( 1, 1, 1),
                };
                for (int cypher = 0; cypher < 8; ++cypher)
                {
                    for (int i = 0; i < 18; ++i)
                    {
                        pos[cypher * 18 + i] = corners[sBoxLightTriangleIndices[cypher][i]];
                    }
                }
                sDXBoxLightVB->unmapBuffer();
            }
            sDXBoxLightVB->setBuffer();

            // S24 (2026-08-15, task #165): fully disabling depth test here
            // was tested as a diagnostic (does the read-only-DSV depth
            // chain from task #206 explain why box-draw-path lights vanish
            // past their own radius while fullscreen-path lights don't) -
            // user confirmed NO CHANGE, ruling depth test out. Reverted to
            // the real fix (GL_TRUE, GL_FALSE - test on, write off, correct
            // occlusion against opaque geometry).
            LLGLDepthTest depth(GL_TRUE, GL_FALSE);

            // S24 (2026-08-05): setSceneBlendType(BT_ADD) above only sets
            // the blend FACTORS (blendFunc(ONE, ONE)) - it does not enable
            // blending itself. GL's own local-lights loop never explicitly
            // enables GL_BLEND either, relying on it already being enabled
            // by whatever came before in the same larger function; this
            // DXPipeline function has no such guarantee (it's a standalone
            // entry point, not embedded in that same GL call chain), and
            // LLRender::applyDXBlendState() (llrender.cpp) explicitly reads
            // LLGLState::isEnabled(GL_BLEND) to decide whether to build an
            // enabled-vs-disabled D3D11 blend state at all - if blend
            // happened to be left disabled by an earlier pass (e.g. the
            // ambient pass above explicitly disables it for its own
            // fullscreen draw), every light-volume draw here would REPLACE
            // the pixel instead of adding to it, i.e. only the last light
            // drawn at a given pixel would show, not the sum of all of
            // them - a very plausible explanation for "lights draw but
            // contribute nothing visible". Enable explicitly rather than
            // trust inherited state.
            LLGLEnable blend(GL_BLEND);

            // S24 (2026-08-15, task #165): same reasoning as the blend fix
            // immediately above, for GL_CULL_FACE instead - found via a
            // user report of small ground-level lights getting culled
            // depending on view angle even with the camera right on top of
            // them. sBoxLightTriangleIndices' winding is a byte-for-byte
            // correct transcription of GL's own sOcclusionIndices fan data
            // (independently re-derived and verified by hand this session,
            // all 8 octant cyphers), so the geometry itself is not the bug -
            // but nothing in this function ever explicitly manages cull
            // state for this 3D volume geometry the way the two fullscreen-
            // triangle passes elsewhere in this same function already do
            // (LLGLDisable cullface at this file's soften-pass and
            // shadow-lightmap-pass draws) - confirmed via a full-file grep,
            // only those two guard sites exist. This is the exact same
            // implicit-default gap class already found and fixed once for
            // 2D UI rendering (DXContext::beginFrame() only sets a GL-
            // matching cull-disabled baseline at the very START of the
            // frame - by the time this draw runs, deep into the frame after
            // the opaque G-buffer pass legitimately enables cull for solid
            // meshes, this box draw was silently inheriting whatever cull
            // state that pass left behind instead of GL's real default).
            // The octant/cypher technique already selects exactly the
            // correct visible triangles via DATA (which cypher/winding to
            // use, chosen per camera-relative octant) - it does not need
            // runtime backface elimination to work correctly, so disabling
            // cull entirely here is the most robust fix, not just a
            // convention match.
            LLGLDisable cullface(GL_CULL_FACE);

            // S24 (2026-08-15, task #165): GL_DEPTH_CLAMP tested and ruled
            // out (user: "no joy") - reverted to no explicit management
            // (matches original code; state/inheritance genuinely isn't
            // the mechanism here after all).

            // S24 (2026-08-09, task #143): collected during this same pass
            // (mirrors GL's own spot_lights vector, pipeline.cpp:9244-9249)
            // and rendered afterward in its own block, once light_shader
            // (gDeferredLightProgram) is unbound - see below.
            std::vector<LLDrawable*> spot_lights;

            // S24 (2026-08-09, task #161): camera-inside-light-box buckets -
            // mirrors GL's fullscreen_lights/fullscreen_spot_lights
            // (pipeline.cpp:9156-9158) and the modelview matrix used to
            // transform their centers into view space for the fullscreen
            // multi-light/multi-spotlight shaders (get_current_modelview(),
            // pipeline.cpp:9007 - same function, already declared in
            // llrender.h, transitively available here).
            glm::mat4 mat = get_current_modelview();
            std::vector<LLVector4> fullscreen_lights;
            std::vector<LLVector4> fullscreen_light_colors;
            std::vector<LLDrawable*> fullscreen_spot_lights;

            // S24 (2026-08-15, task #165): the per-light LightDiag
            // instrumentation that lived in this loop across rounds 2-9 has
            // been removed - it conclusively proved AABBInFrustumNoFarClip
            // is correct and that draw-eligibility (candidate set, frustum
            // test, radius/intensity gates) is NOT the mechanism behind the
            // reported dimming (confirmed by fully disabling the frustum
            // gate below and observing zero change). Removed rather than
            // kept "just in case" per the diagnostic-lifecycle convention -
            // it was also measurably hurting frame time (LL_WARNS is a
            // synchronous file write, firing on every light's state
            // transition). The real mechanism is downstream in shader-side
            // brightness computation, not this loop's draw-eligibility
            // gates - see task #165 for where the investigation goes next.
            S32 count = 0;
            for (const auto& light : pipeline.getNearbyLights())
            {
                ++count;
                if (count > local_light_count)
                {
                    break;
                }

                LLDrawable* drawablep = light.drawable;
                LLVOVolume* volume = drawablep->getVOVolume();
                if (!volume)
                {
                    continue;
                }

                if (volume->isAttachment() && !LLPipeline::sRenderAttachedLights)
                {
                    continue;
                }

                LLVector4a center;
                center.load3(drawablep->getPositionAgent().mV);
                const F32* c = center.getF32ptr();
                F32        raw_radius = volume->getLightRadius() * 1.5f;

                LLColor3 col = volume->getLightLinearColor() * light_scale;
                if (col.magVecSquared() < 0.001f || raw_radius <= 0.001f)
                {
                    continue;
                }

                // S24 (2026-08-17, task #165): small decorative local lights
                // (e.g. walkway lanterns) are commonly authored with a tiny
                // real radius (1-3m) - `s`/`light[i].w` feeds BOTH the
                // dist<=1.0 shader gate (multiPointLightF.hlsl/pointLightF.hlsl)
                // AND this loop's own AABB/camera_outside_box tests, so a
                // small authored radius means the camera has to be almost
                // standing inside the light before it contributes ANY color
                // at all - confirmed via live testing ("have to get camera
                // real close to get them to switch on"), a real, separate
                // issue from the N.L/frustum popping fixed above (this one
                // reproduces walking straight toward/away, no camera-angle
                // change involved at all). User's direct ask: every local
                // light should be at least visible somewhere within a fixed
                // real-world distance of the avatar, irrespective of its own
                // authored radius. Floors the EFFECTIVE radius used for all
                // downstream distance/falloff math (this function's own
                // tests plus both shaders' dist normalization) to at least
                // RenderLocalLightMinRadius, while the light-is-on check
                // just above still uses the light's TRUE raw radius (a
                // genuinely radius=0 light stays off - this only extends
                // the reach of lights that are actually emitting). Applied
                // once here so it automatically propagates through both the
                // box-mesh and fullscreen draw paths plus the frustum/
                // camera_outside_box tests below - no separate shader-side
                // change needed. 0 = original behavior (no floor).
                static LLCachedControl<F32> min_light_radius(gSavedSettings, "RenderLocalLightMinRadius", 15.f);
                F32 s = llmax(raw_radius, (F32)min_light_radius);

                // S24 (2026-08-15, task #165 - "AABB brickbat" mitigation,
                // round 2): AABBInFrustumNoFarClip itself is confirmed
                // correct (a live per-light diagnostic this session traced
                // multiple reported "pops" to the camera genuinely not
                // facing the light) - a spatial margin (tried up to 15m,
                // confirmed via live A/B testing to only ever help/be
                // neutral, never hurt) made ZERO perceptible difference,
                // and fully disabling this gate via RenderLocalLightFrustumCulling
                // (below) ALSO made zero difference - conclusively proving
                // draw-eligibility was never the actual mechanism behind
                // the reported dimming. Kept both RenderLocalLightFrustumCulling
                // (properly wired now - previously only gated
                // calcNearbyLights()'s candidate-list prefilter,
                // pipeline.cpp:5841, never this render-loop's own check)
                // and RenderLocalLightFrustumMargin as real, working knobs
                // for anyone who wants them, but the search for the actual
                // cause has moved to shader-side brightness computation -
                // see task #165.
                static LLCachedControl<bool> frustum_culling_enabled(gSavedSettings, "RenderLocalLightFrustumCulling", true);
                static LLCachedControl<F32> frustum_margin(gSavedSettings, "RenderLocalLightFrustumMargin", 32.f);
                S32 aabb_result = 1;
                if (frustum_culling_enabled)
                {
                    LLVector4a sa;
                    sa.splat(s + frustum_margin);
                    aabb_result = camera->AABBInFrustumNoFarClip(center, sa);
                }

                // S24 (2026-08-17, task #165, "light hold time"): this is
                // the WHOLE-LIGHT counterpart to multiPointLightF.hlsl's
                // per-pixel N.L smoothstep fix (see that file's own detailed
                // root-cause comment - same underlying bug class: a hard,
                // zero-margin binary test recomputed fresh every frame from
                // camera-relative geometry, which aliases into visible
                // popping under continuous camera micro-motion at high
                // frame rates and looks perfectly stable when sampled
                // rarely). A per-pixel value can't be smoothed from here -
                // different pixels lit by the same light can disagree on
                // N.L simultaneously - but aabb_result is a single decision
                // for the ENTIRE light, so a real per-light temporal hold is
                // both possible and meaningful here, unlike in the shader.
                // RenderLocalLightFrustumCulling defaults OFF in this fork
                // (settings.xml), so aabb_result is 1 unconditionally for
                // most users and this hold is inert by default - it only
                // does real work for anyone who turns frustum culling on,
                // which is exactly the "lower end systems" pairing this was
                // requested for: cull aggressively for real performance
                // headroom, hold briefly to pay for it with zero visible
                // flicker cost. RenderLocalLightHoldTime=0 (or the setting
                // simply not helping because frustum culling is off) is
                // functionally identical to this block never having existed.
                static LLCachedControl<F32> hold_time_setting(gSavedSettings, "RenderLocalLightHoldTime", 0.30f);
                bool light_eligible = (aabb_result != 0);
                if (hold_time_setting > 0.f)
                {
                    struct LightHoldState
                    {
                        bool held = false;
                        F32 timeSinceIneligible = 0.f;
                        U64 lastFrameSeen = 0;
                    };
                    static std::unordered_map<const LLDrawable*, LightHoldState> sHoldStates;
                    static U64 sHoldFrame = 0;
                    ++sHoldFrame;

                    // Periodic prune (every 512 frames, drop anything not
                    // touched in the last 256) - keeps this map bounded and
                    // safe against a drawable pointer being freed and later
                    // reused by an unrelated light, without needing to hook
                    // any destruction callback.
                    if ((sHoldFrame & 0x1FF) == 0)
                    {
                        for (auto it = sHoldStates.begin(); it != sHoldStates.end(); )
                        {
                            if (sHoldFrame - it->second.lastFrameSeen > 256)
                            {
                                it = sHoldStates.erase(it);
                            }
                            else
                            {
                                ++it;
                            }
                        }
                    }

                    LightHoldState& hold = sHoldStates[drawablep];
                    hold.lastFrameSeen = sHoldFrame;
                    if (light_eligible)
                    {
                        hold.held = true;
                        hold.timeSinceIneligible = 0.f;
                    }
                    else if (hold.held)
                    {
                        hold.timeSinceIneligible += gFrameIntervalSeconds;
                        if (hold.timeSinceIneligible < (F32)hold_time_setting)
                        {
                            light_eligible = true; // still within the hold window
                        }
                        else
                        {
                            hold.held = false;
                        }
                    }
                }

                if (!light_eligible)
                {
                    continue;
                }

                LLPipeline::sVisibleLightCount++;

                // S24 (2026-08-09, task #161): real root cause found via a
                // live OM-state diagnostic (removed below, its job is done -
                // blend/depth/cull all matched GL exactly, ruling out a
                // state bug; the actual issue was that the camera was
                // geometrically inside the light's box for the whole test).
                // v1 drew every light through the box-mesh path
                // unconditionally, which silently drops a light whenever the
                // camera is inside its box (the box's near-camera-facing
                // triangles, selected by getBoxLightCypher()'s octant test,
                // end up behind the camera/near-plane and get clipped away -
                // "a box blanking the light out" as the camera moves through
                // it). Mirrors GL's own camera-inside-vs-outside split
                // exactly (pipeline.cpp's local-lights loop, same 0.2 margin)
                // - outside keeps the box-mesh draw below; inside now
                // collects into fullscreen_lights/fullscreen_spot_lights,
                // drawn via the real fullscreen multi-light/multi-spotlight
                // passes further down instead of being force-fed through the
                // box path. This also fixes projected/spot lights the same
                // way, since GL applies the identical split to spotlights.
                LLVector3 cam_origin = camera->getOrigin();

                // S24 (2026-08-15, task #165): round 22 - real root cause,
                // found via a live D3D11 constant-buffer readback + a
                // hand-verified clip-space computation: the per-axis OR test
                // this replaces only guarantees the camera clears the box in
                // AT LEAST ONE world axis - it says nothing about the other
                // two, so the camera can sit essentially inside the box's
                // true volume (deeply inside 2 of 3 axes) while barely
                // poking past the margin in the third, and still pass as
                // "outside." A live case: cam=(144,112,28.4) vs
                // center=(138.1,100,27), s=11.1 - only the Y-axis check
                // tripped (barely, by 0.7m); X and Z were both comfortably
                // INSIDE the box's range. At that real proximity, half the
                // box's 8 corners land on the wrong side of the camera's own
                // eye-plane (verified via a real GPU constant-buffer
                // readback of the actual bound MVP: w = X_world-144, giving
                // +5.2 for corner.x=+1 and -17.0 for corner.x=-1) - the box
                // straddles the camera's near/behind-eye boundary, and
                // whatever's mishandling that clip case in DX_RENDER
                // produces the enormous, screen-spanning "fan" geometry
                // photographed this round (all 4 test screenshots - even
                // the "on target" cypher, since it's the SAME underlying
                // corruption, just one that happens to visually cover the
                // real light position). A flat per-axis margin can never
                // catch this since it's fundamentally a 3D-distance/viewing
                // problem, not an axis problem. Real Euclidean clearance,
                // scaled to the light's own size (a bigger light needs more
                // real-world room before its corners can't straddle the eye
                // plane at plausible viewing angles) - 2x size margin, not
                // GL's flat 0.2m, keeps close-in lights safely on the
                // fullscreen multi-light path (screen-space, immune to this
                // whole corruption class) instead of ever reaching the
                // box-mesh path in this degenerate zone. This is very
                // likely the real root of the ORIGINAL task #165 symptom
                // too (walkway lights popping/dim/needing close approach) -
                // small-radius lights are exactly what players walk up
                // close to.
                F32 dist_to_center = (cam_origin - LLVector3(c[0], c[1], c[2])).length();
                bool camera_outside_box = dist_to_center > (s * 2.0f + 0.2f);

                // S24 (2026-08-16): task #165 round-23 path-select LL_WARNS
                // diagnostic removed (answered - confirmed round 22's fix
                // was being exercised correctly; see diagnostic-lifecycle).

                if (camera_outside_box)
                {
                    if (volume->isLightSpotlight())
                    {
                        volume->updateSpotLightPriority();
                        spot_lights.push_back(drawablep);
                        continue;
                    }

                    U32 cypher = getBoxLightCypher(camera, center);

                    light_shader.uniform3fv(LLShaderMgr::LIGHT_CENTER, 1, c);
                    light_shader.uniform1f(LLShaderMgr::LIGHT_SIZE, s);
                    light_shader.uniform3fv(LLShaderMgr::DIFFUSE_COLOR, 1, col.mV);
                    light_shader.uniform1f(LLShaderMgr::LIGHT_FALLOFF, volume->getLightFalloff(deferred_light_falloff));
                    light_shader.uniform1i(LLShaderMgr::CLASSIC_MODE, (psky && psky->canAutoAdjust()) ? 1 : 0);

                    gDX.syncMatrices();

                    sDXBoxLightVB->drawArrays(LLRender::TRIANGLES, cypher * 18, 18);
                }
                else
                {
                    if (volume->isLightSpotlight())
                    {
                        volume->updateSpotLightPriority();
                        fullscreen_spot_lights.push_back(drawablep);
                        continue;
                    }

                    glm::vec3 tc(c[0], c[1], c[2]);
                    tc = mul_mat4_vec3(mat, tc);
                    fullscreen_lights.push_back(LLVector4(tc.x, tc.y, tc.z, s));
                    fullscreen_light_colors.push_back(LLVector4(col.mV[0], col.mV[1], col.mV[2], volume->getLightFalloff(deferred_light_falloff)));
                }
            }

            pipeline.unbindDeferredShader(light_shader);

            // S24 (2026-08-09, task #143): v1 of spotlights/projectors -
            // the gap this block's own comment (above) originally flagged.
            // setupSpotLight() (pipeline.cpp) is pure math + already-DX-safe
            // uniform/enableTexture/bind() calls, no raw GL - reused as-is
            // by composition, same principle as beginDeferredPass()/
            // endDeferredPass() needing zero changes for several pools.
            // Reuses the same box-volume draw path as the point lights
            // above (sDXBoxLightVB/getBoxLightCypher()) rather than GL's
            // separate mCubeVB TRIANGLE_FAN + get_box_fan_indices() - same
            // "already own a working box-draw, don't build a second one"
            // reasoning. This only ever holds camera-OUTSIDE-box spotlights
            // now (task #161) - camera-inside ones are diverted to
            // fullscreen_spot_lights in the main loop above and drawn via
            // the real gDeferredMultiSpotLightProgram fullscreen pass below,
            // matching GL exactly instead of the old "always force through
            // the box, camera-inside not handled" v1 simplification.
            if (!spot_lights.empty())
            {
                LLGLDepthTest spot_depth(GL_TRUE, GL_FALSE);
                LLGLEnable spot_blend(GL_BLEND);

                pipeline.bindDeferredShader(gDeferredSpotLightProgram);
                sDXBoxLightVB->setBuffer();
                gDeferredSpotLightProgram.enableTexture(LLShaderMgr::DEFERRED_PROJECTION);

                for (LLDrawable* drawablep : spot_lights)
                {
                    LLVOVolume* volume = drawablep->getVOVolume();

                    LLVector4a center;
                    center.load3(drawablep->getPositionAgent().mV);
                    const F32* c = center.getF32ptr();
                    F32        s = volume->getLightRadius() * 1.5f;

                    LLPipeline::sVisibleLightCount++;

                    pipeline.setupSpotLight(gDeferredSpotLightProgram, drawablep);

                    LLColor3 col = volume->getLightLinearColor() * light_scale;

                    gDeferredSpotLightProgram.uniform3fv(LLShaderMgr::LIGHT_CENTER, 1, c);
                    gDeferredSpotLightProgram.uniform1f(LLShaderMgr::LIGHT_SIZE, s);
                    gDeferredSpotLightProgram.uniform3fv(LLShaderMgr::DIFFUSE_COLOR, 1, col.mV);
                    gDeferredSpotLightProgram.uniform1f(LLShaderMgr::LIGHT_FALLOFF, volume->getLightFalloff(deferred_light_falloff));
                    gDeferredSpotLightProgram.uniform1i(LLShaderMgr::CLASSIC_MODE, (psky && psky->canAutoAdjust()) ? 1 : 0);

                    gDX.syncMatrices();

                    U32 cypher = getBoxLightCypher(camera, center);
                    sDXBoxLightVB->drawArrays(LLRender::TRIANGLES, cypher * 18, 18);
                }

                gDeferredSpotLightProgram.disableTexture(LLShaderMgr::DEFERRED_PROJECTION);
                pipeline.unbindDeferredShader(gDeferredSpotLightProgram);
            }

            // S24 (2026-08-09, task #161): the real fix - fullscreen
            // multi-light pass for lights the camera is inside the box of
            // (collected above instead of being force-fed through the
            // box-mesh path, which silently drops them). Mirrors GL's own
            // block exactly (pipeline.cpp:9327-9364): batch up to
            // LL_DEFERRED_MULTI_LIGHT_COUNT lights per draw, picking the
            // shader permutation compiled for exactly that many
            // (gDeferredMultiLightProgram[idx], idx = count-1 - these
            // shaders already compile fine under DX_RENDER, confirmed via
            // log ("Deferred MultiLight Shader 3".."15") - this was purely
            // a missing orchestration path, not a shader gap). Depth test
            // off (LLGLDepthTest depth(GL_FALSE)) - this is a fullscreen
            // triangle, not per-object geometry; the shader reads the real
            // depth buffer itself via bindDeferredShader()'s DEFERRED_DEPTH
            // channel to reconstruct per-pixel position instead.
            if (!fullscreen_lights.empty())
            {
                LLGLDepthTest depth(GL_FALSE);

                U32 fs_count = 0;
                const U32 max_count = LL_DEFERRED_MULTI_LIGHT_COUNT;
                LLVector4 light_arr[LL_DEFERRED_MULTI_LIGHT_COUNT];
                LLVector4 col_arr[LL_DEFERRED_MULTI_LIGHT_COUNT];
                F32 far_z = 0.f;

                for (size_t li = 0; li < fullscreen_lights.size(); ++li)
                {
                    light_arr[fs_count] = fullscreen_lights[li];
                    col_arr[fs_count] = fullscreen_light_colors[li];
                    far_z = llmin(light_arr[fs_count].mV[2] - light_arr[fs_count].mV[3], far_z);
                    ++fs_count;
                    if (fs_count == max_count || li + 1 == fullscreen_lights.size())
                    {
                        U32 idx = fs_count - 1;
                        pipeline.bindDeferredShader(gDeferredMultiLightProgram[idx]);
                        gDeferredMultiLightProgram[idx].uniform1i(LLShaderMgr::MULTI_LIGHT_COUNT, fs_count);
                        gDeferredMultiLightProgram[idx].uniform4fv(LLShaderMgr::MULTI_LIGHT, fs_count, (F32*)light_arr);
                        gDeferredMultiLightProgram[idx].uniform4fv(LLShaderMgr::MULTI_LIGHT_COL, fs_count, (F32*)col_arr);
                        gDeferredMultiLightProgram[idx].uniform1f(LLShaderMgr::MULTI_LIGHT_FAR_Z, far_z);
                        gDeferredMultiLightProgram[idx].uniform1i(LLShaderMgr::CLASSIC_MODE, (psky && psky->canAutoAdjust()) ? 1 : 0);
                        far_z = 0.f;
                        fs_count = 0;
                        pipeline.mScreenTriangleVB->setBuffer();
                        pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
                        pipeline.unbindDeferredShader(gDeferredMultiLightProgram[idx]);
                    }
                }
            }

            // S24 (2026-08-09, task #161): same fix, spotlight/projector
            // side - fullscreen multi-spotlight pass for spotlights the
            // camera is inside the box of. Mirrors GL's own block exactly
            // (pipeline.cpp:9366-9401): one draw per light (not batched,
            // unlike the point-light case above), via
            // gDeferredMultiSpotLightProgram.
            if (!fullscreen_spot_lights.empty())
            {
                LLGLDepthTest depth(GL_FALSE);

                pipeline.bindDeferredShader(gDeferredMultiSpotLightProgram);
                gDeferredMultiSpotLightProgram.enableTexture(LLShaderMgr::DEFERRED_PROJECTION);
                pipeline.mScreenTriangleVB->setBuffer();

                for (LLDrawable* drawablep : fullscreen_spot_lights)
                {
                    LLVOVolume* volume = drawablep->getVOVolume();
                    LLVector3   sl_center = drawablep->getPositionAgent();
                    F32         light_size_final = volume->getLightRadius() * 1.5f;
                    F32         light_falloff_final = volume->getLightFalloff(deferred_light_falloff);

                    LLPipeline::sVisibleLightCount++;

                    glm::vec3 tc(sl_center.mV[0], sl_center.mV[1], sl_center.mV[2]);
                    tc = mul_mat4_vec3(mat, tc);
                    F32 tc_arr[3] = { tc.x, tc.y, tc.z };

                    pipeline.setupSpotLight(gDeferredMultiSpotLightProgram, drawablep);

                    LLColor3 col = volume->getLightLinearColor() * light_scale;

                    gDeferredMultiSpotLightProgram.uniform3fv(LLShaderMgr::LIGHT_CENTER, 1, tc_arr);
                    gDeferredMultiSpotLightProgram.uniform1f(LLShaderMgr::LIGHT_SIZE, light_size_final);
                    gDeferredMultiSpotLightProgram.uniform3fv(LLShaderMgr::DIFFUSE_COLOR, 1, col.mV);
                    gDeferredMultiSpotLightProgram.uniform1f(LLShaderMgr::LIGHT_FALLOFF, light_falloff_final);
                    gDeferredMultiSpotLightProgram.uniform1i(LLShaderMgr::CLASSIC_MODE, (psky && psky->canAutoAdjust()) ? 1 : 0);

                    pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
                }

                gDeferredMultiSpotLightProgram.disableTexture(LLShaderMgr::DEFERRED_PROJECTION);
                pipeline.unbindDeferredShader(gDeferredMultiSpotLightProgram);
            }
        }
    }

    // S24 (2026-08-06): root cause of "alpha/glass/water/glow entirely
    // missing" found via joint investigation with the user - GL's own body
    // (pipeline.cpp ~line 9392, "render non-deferred geometry (alpha,
    // fullbright, glow)") calls renderGeomPostDeferred() right here, still
    // inside this same function, with screen_target still bound - THEN
    // flushes once. DX_RENDER's renderGeomPostDeferred() equivalent
    // existed (task #108) and was correctly wired at exactly one call site
    // (llviewerdisplay.cpp's display_cube_face(), the reflection-probe/
    // snapshot path) - but display_cube_face() is a DIFFERENT function
    // from the main per-frame display(), which never called it at all.
    // display()'s real sequence (llviewerdisplay.cpp ~line 1062-1098) is
    // renderGeomDeferred() -> deferredScreen.flush() -> renderDeferredLighting()
    // (this function) with nothing after it - so every alpha/water/
    // fullbright/glow pool was structurally unreachable for the world
    // scene, not just badly lit. Calling it here, before this function's
    // own final flush() below, is the correct DX_RENDER mirror of GL's
    // structure - draws into the still-bound mRT->screen (the lit result),
    // exactly like GL's screen_target, instead of needing separate re-bind/
    // flush plumbing in display() itself.
    //
    // S24 (2026-08-06): first real playtest of this wiring surfaced "massive
    // alpha ordering issues" - objects that should be hidden behind walls
    // showing through them. Root cause: screen_target was bound with
    // bind_depth=false above (see that bindTarget() call's own comment) to
    // avoid the depth-as-SRV-while-DSV hazard for the ambient pass's
    // fullscreen draw - but that leaves NO depth-stencil view attached at
    // all, so the real 3D alpha/fullbright/glow geometry drawn here has
    // nothing to depth-test against, regardless of each pool's own
    // LLGLDepthTest state. Re-attaching depth (mRT->screen shares its depth
    // buffer with deferredScreen, already fully written by the opaque pass)
    // via rebindWithDepth() rather than a second bindTarget() call, which
    // would trip its "not already bound" assert.
    screen_target->rebindWithDepth(true);
    DXPipeline::renderGeomPostDeferred(pipeline, *LLViewerCamera::getInstance());

    // S24 (2026-08-05): moved here from right after the ambient pass's
    // unbindDeferredShader() call above - a real bug found via the local-
    // lights count diagnostic showing 100+ real drawArrays() calls per
    // frame yet zero visible light contribution. LLRenderTarget::flush()
    // RESTORES the previously-bound render target (mPreviousRT->
    // bindTarget(), or the swap chain back buffer if none) - calling it
    // right after the ambient pass unbound screen_target before the local-
    // lights loop even started, so every light-volume draw was landing in
    // whatever target was bound before renderDeferredLighting() ran, never
    // reaching mRT->screen at all. flush() must run exactly once, after
    // BOTH passes (plus the post-deferred pass above) are done writing to
    // screen_target - unconditionally, since the ambient pass above always
    // writes to it regardless of whether local_light_count gates the local-
    // lights block off.
    screen_target->flush();

    // S24 (2026-09-09, SSAO flicker investigation, task #190 follow-up):
    // "advance last-frame's camera to this frame's, for next frame's
    // reprojection" - moved here (the single real end of the WORLD-camera
    // scene render) from inside renderGeomPostDeferred(), which is also
    // called from llviewerdisplay.cpp for an EARLY, now-superseded world-
    // camera pass (before this function's own SSAO-temporal-resolve block
    // above even ran, which was clobbering gGLLastModelView/gGLLastProjection
    // to THIS frame's own value before this frame's SSAO temporal resolve
    // could read the real previous frame's value - making its reprojection
    // delta identity, i.e. no motion compensation at all) and from
    // render_hud_attachments() (with the HUD camera, AFTER this function -
    // clobbering the value AGAIN with the HUD's transform, so even the
    // NEXT frame's SSAO/SSR reprojection was working off last frame's HUD
    // matrix instead of the world camera). Doing it here instead - once,
    // with the real world camera, after every in-frame consumer of "last
    // frame's real camera" has already run, before render_hud_attachments()
    // gets a chance to run later in display() - is the single correct point.
    if (!gCubeSnapshot)
    {
        for (U32 i = 0; i < 16; i++)
        {
            gGLLastModelView[i] = gGLModelView[i];
            gGLLastProjection[i] = gGLProjection[i];
        }
    }
}
