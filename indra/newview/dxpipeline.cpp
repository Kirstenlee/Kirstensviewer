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

// Real fast-timer handles, defined in pipeline.cpp.
extern LLTrace::BlockTimerStatHandle FTM_DEFERRED_POOLRENDER;
extern LLTrace::BlockTimerStatHandle FTM_POST_DEFERRED_POOLRENDER;
extern LLTrace::BlockTimerStatHandle FTM_RENDER_DEFERRED;

// Matches pipeline.cpp's own extern (defined in llviewerwindow.cpp) - needed
// so presentDeferredScreen()'s tonemap-program selection matches
// LLPipeline::tonemap()'s no_post logic exactly.
extern bool gSnapshotNoPost;

// Matches pipeline.cpp's own extern (set there during reflection-probe
// cube-face capture) - needed so renderDeferredLighting()'s sun-shadow/SSAO
// lightmap pass picks gDeferredSunProbeProgram during capture, mirroring
// LLPipeline::renderDeferredLighting()'s GL body.
extern bool gCubeSnapshot;

// Matches llviewerdisplay.cpp's own extern (defined there, next to
// gSnapshotNoPost) - set right before both presentFinal() call sites, so
// rawSnapshot() (llviewerwindow.cpp) can read the true final composited
// render target directly.
extern LLRenderTarget* gLastCompositedPostTarget;

// GL's renderFinalize() calls updateEffectMask() then reads effectsMask
// itself, but DX_RENDER's renderFinalize() early-returns before that body
// runs, so presentDeferredScreen() has to do both itself. effectsMask is
// non-static (not file-scope-only) for that reason - see its own comment in
// pipeline.cpp.
extern int effectsMask;
void updateEffectMask();

#include "DXDevice.h"
#include "DXReadback.h"
#include "DXRenderTarget.h"
#include "DXStateCache.h"
#include "DXShader.h"
#include "DXSwapChain.h"
#include "DXSampler.h"

namespace
{
    // Tracks whether renderDeferredLighting() actually completed and wrote
    // real content into mRT->screen THIS frame. mRT->screen is allocated
    // unconditionally so its SRV is always non-null even when
    // renderDeferredLighting() early-returned without writing anything -
    // presentDeferredScreen() needs this flag, not an SRV-null check, to
    // avoid blitting stale content.
    bool sScreenLitThisFrame = false;

    // D3D11 has no TRIANGLE_FAN topology (llvertexbuffer.cpp's sDXMode[]
    // maps it to D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED; drawRange()/drawArrays()
    // llassert() against that). GL's local-lights box draw relies on
    // llvieweroctree.cpp's private sOcclusionIndices fan table (8
    // camera-relative "cypher" variants, each an 8-vertex fan). Expanded
    // below into the triangle-list equivalent (6 triangles/18 vertices per
    // cypher, standard fan-to-list expansion), same vertex order per cypher
    // as GL's fan so winding/culling matches. Built as a flat, non-indexed
    // vertex list, owned separately from GL's mCubeVB.
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
    // POOL_GLTF_PBR/POOL_GLTF_PBR_ALPHA_MASK (LLDrawPoolGLTFPBR) has a real
    // DX_RENDER branch (see lldrawpoolpbropaque.cpp) -
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

    // Whitelist for renderGeomPostDeferred()'s pools (the "forward"/
    // post-deferred pass: translucent/alpha content, water, glow).
    // POOL_ALPHA_PRE_WATER/POOL_ALPHA_POST_WATER -> dxdrawpoolalpha.cpp/.h.
    // POOL_FULLBRIGHT/POOL_FULLBRIGHT_ALPHA_MASK/POOL_GLOW ->
    // dxdrawpoolsimple.cpp. POOL_WATER -> dxdrawpoolwater.cpp/.h (relies on
    // LLHLSLShader::bindTexture(S32, LLRenderTarget*, ...) for
    // WATER_SCREENTEX/WATER_EXCLUSIONTEX). POOL_GLTF_PBR/
    // POOL_GLTF_PBR_ALPHA_MASK's own renderPostDeferred() (the glow
    // sub-pass) stays GL-only, not reachable here.
    bool isConvertedPostDeferredPool(U32 type)
    {
        return type == LLDrawPool::POOL_ALPHA_PRE_WATER
            || type == LLDrawPool::POOL_ALPHA_POST_WATER
            || type == LLDrawPool::POOL_WATER
            || type == LLDrawPool::POOL_FULLBRIGHT
            || type == LLDrawPool::POOL_FULLBRIGHT_ALPHA_MASK
            || type == LLDrawPool::POOL_GLOW
            // LLDrawPoolAvatar::beginPostDeferredPass()/renderPostDeferred()/
            // endPostDeferredPass() (lldrawpoolavatar.cpp) is a complete,
            // already-safe implementation; without this entry alpha-blended
            // avatar content (transparent clothing, hair, alpha-masked
            // attachments) never renders.
            || type == LLDrawPool::POOL_AVATAR
            // DXDrawPoolBump::renderPostDeferred() (dxdrawpoolbump.cpp,
            // shiny-fullbright + emboss-bump) is a complete implementation
            // that simply wasn't whitelisted.
            || type == LLDrawPool::POOL_BUMP
            // POOL_GLTF_PBR/POOL_GLTF_PBR_ALPHA_MASK's own
            // renderPostDeferred() (lldrawpoolpbropaque.cpp) is the PBR
            // glow/emissive sub-pass (gPBRGlowProgram, static + rigged via
            // pushRiggedGLTFBatches()) plus HUD-attached PBR content -
            // complete implementation, just wasn't whitelisted.
            || type == LLDrawPool::POOL_GLTF_PBR
            || type == LLDrawPool::POOL_GLTF_PBR_ALPHA_MASK;
    }
}

// static
void DXPipeline::renderGeomDeferred(LLPipeline& pipeline, LLCamera& camera, bool do_occlusion)
{

    // Rigged-mesh invisibility root cause: LLRender::syncMatrices()'s
    // DX_RENDER branch (llrender.cpp) was missing the projection_matrix
    // uniform upload; see also objectSkinV.hlsl for two related
    // matrix-layout fixes.

    // DXContext::beginFrame() resets rasterizer state to cull_enabled=false
    // at the start of every frame (matching GL's default). Must explicitly
    // re-enable it here - GL's own renderGeomDeferred() wraps its entire
    // deferred-pools block in the same LLGLEnable cull(GL_CULL_FACE); -
    // without it, all deferred pools render with backface culling off,
    // which double-renders thin double-sided-style geometry (e.g. foliage)
    // as visible z-fighting.
    LLGLEnable cull(GL_CULL_FACE);

    // S24: real DX11 wireframe mode (Develop > Rendering > Wireframe, gUseWireframe) - GL's own
    // renderGeomDeferred() brackets this exact block the same way (glPolygonMode(GL_LINE) at the
    // top, GL_FILL at the bottom). D3D11 has no per-draw fill-mode call like glPolygonMode() - fill
    // mode is a rasterizer-state CREATION-time field. Setting DXStateCache::sWireframeScopeActive
    // (rather than binding a one-off wireframe rasterizer state directly here) is deliberate: cull/
    // scissor/depth-clamp toggle constantly during normal rendering (every LLGLEnable/LLGLDisable),
    // each one forcing LLRender::applyDXRasterizerState() to rebuild the WHOLE bundled rasterizer
    // state object - a direct RSSetState() call here gets silently clobbered back to solid fill by
    // the very first such toggle (the first drawpool), before anything actually draws wireframed.
    // The explicit applyDXRasterizerState() call forces the new scope to take effect immediately
    // rather than waiting on the next incidental toggle. No D3D11 line-width equivalent for
    // wireframe edges (always 1px) - a real, smaller residual gap vs GL.
    if (gUseWireframe)
    {
        DXStateCache::sWireframeScopeActive = true;
        gDX.applyDXRasterizerState();
    }

    // bindDeferredShader() (pipeline.cpp) uploads MODELVIEW_DELTA_MATRIX/
    // INVERSE_MODELVIEW_DELTA_MATRIX AFTER its call to
    // bindReflectionProbes(), so gGLDeltaModelView/gGLInverseDeltaModelView
    // must be computed here, at the top of this function, mirroring GL's
    // own renderGeomDeferred() body (pipeline.cpp) exactly. This delta must
    // be a real per-frame value, not identity - screenSpaceReflUtil.hlsl's
    // traceScreenRay() depends on it for correct reprojection.
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

    // GL calls mReflectionMapManager.updateUniformsPerFrame()/
    // mHeroProbeManager.updateUniformsPerFrame() once per frame here.
    // Mirrored explicitly - nothing else refreshes probe data (positions,
    // bucket assignments, hero-probe box/sphere/mip data) per frame under
    // DX_RENDER, so without this call reflections freeze after the first
    // shader bind.
    if (LLViewerShaderMgr::instance()->mShaderLevel[LLViewerShaderMgr::SHADER_DEFERRED] > 1)
    {
        pipeline.mReflectionMapManager.updateUniformsPerFrame();
        pipeline.mHeroProbeManager.updateUniformsPerFrame();
    }

    // Mirrors GL's doOcclusion(camera) call site/gating (sUseOcclusion/
    // do_occlusion/sProfileEnabled/!gCubeSnapshot), triggered once when the
    // loop reaches the first pool with getType() >= POOL_GRASS. The
    // gGLLastMatrix reset + reload mirrors GL's pre-doOcclusion() matrix
    // refresh - the occlusion proxy-box shader needs the current camera's
    // modelview bound correctly.
    bool occlude = LLPipeline::sUseOcclusion > 1 && do_occlusion && !LLHLSLShader::sProfileEnabled && !gCubeSnapshot;

    LL_RECORD_BLOCK_TIME(FTM_DEFERRED_POOLRENDER);
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

    if (gUseWireframe)
    {
        DXStateCache::sWireframeScopeActive = false;
        gDX.applyDXRasterizerState();
    }
}

// static
void DXPipeline::renderGeomPostDeferred(LLPipeline& pipeline, LLCamera& camera)
{
    // Draws only isConvertedPostDeferredPool()'s whitelist; GL's grouped-by-
    // type begin/render/end structure is not ported (this loop does
    // begin/render/end once per pool instance instead - harmless).
    // renderDebug() is now called at the tail below (Develop > Render
    // Metadata's 22 checkboxes - see LLPipeline::renderDebug()'s own
    // comment, task #304's consolidation pass) - it had ZERO callers
    // anywhere in the engine (confirmed by grep, not assumed), so every one
    // of those checkboxes toggled real state that nothing ever read.
    // renderHighlights() (build/edit-mode selection outline) is a separate,
    // still-unwired gap noted here but NOT fixed by this pass - it isn't
    // driven by any Render Metadata checkbox. Wireframe mode (gUseWireframe)
    // IS now wired - see the matching bracket in renderGeomDeferred() above
    // and the one at the tail of this function.
    //
    // doAtmospherics()/doWaterHaze() contain no raw GL calls, so mirroring
    // GL's ordering was enough: atmospherics fires before POOL_WATER when
    // underwater, else before POOL_ALPHA_POST_WATER; water haze always
    // fires before POOL_ALPHA_PRE_WATER; both are skipped for HUD
    // attachments (sRenderingHUDs) and low-detail reflection-probe capture
    // (gCubeSnapshot + RenderReflectionProbeLevel==0).
    (void)camera;

    if (gUseWireframe)
    {
        DXStateCache::sWireframeScopeActive = true;
        gDX.applyDXRasterizerState();
    }

    // LLDrawPoolWaterExclusion::render() has no raw GL calls, so it needed
    // no porting, just to actually be called - mirrored here as an
    // unconditional call before this loop, matching GL's earliest-pool
    // placement. See LLPipeline::doWaterExclusionMask()'s DX_RENDER branch
    // (pipeline.cpp): its glClearColor(1,1,1,1) is GL-only and skipped
    // under DX_RENDER, so it uses LLRenderTarget::clearColor() explicitly -
    // LLRenderTarget::clear() clears to transparent black, which would
    // invert the mask's meaning (white=included) and exclude the whole
    // screen by default.
    pipeline.doWaterExclusionMask();

    bool done_atmospherics = LLPipeline::sRenderingHUDs;
    bool done_water_haze = done_atmospherics;

    U32 atmospherics_pass = LLPipeline::sUnderWaterRender ? (U32)LLDrawPool::POOL_WATER : (U32)LLDrawPool::POOL_ALPHA_POST_WATER;
    U32 water_haze_pass = LLDrawPool::POOL_ALPHA_PRE_WATER;

    static LLCachedControl<S32> atmospherics_probe_level(gSavedSettings, "RenderReflectionProbeLevel", 0);
    bool low_detail_probe = atmospherics_probe_level == 0 && gCubeSnapshot;
    done_atmospherics = done_atmospherics || low_detail_probe;
    done_water_haze = done_water_haze || low_detail_probe;

    // Called for the main scene from DXPipeline::renderDeferredLighting()
    // (right before its own final screen_target->flush()), and for HUD
    // attachments from render_hud_attachments().

    // GL's renderGeomPostDeferred() also wraps its whole pool loop in
    // LLGLEnable cull(GL_CULL_FACE); - individual post-deferred pools (e.g.
    // dxdrawpoolalpha.cpp) only ever explicitly DISABLE cull for confirmed
    // double-sided materials, assuming it's already on from this outer
    // scope, matching GL.
    LLGLEnable cull(GL_CULL_FACE);

    pipeline.calcNearbyLights(camera);
    pipeline.setupHWLights();

    gDX.setSceneBlendType(LLRender::BT_ALPHA);
    gDX.setColorWriteMask(true, false);

    LL_RECORD_BLOCK_TIME(FTM_POST_DEFERRED_POOLRENDER);
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

        // gGLLastMatrix reset + reload mirrors GL's own per-pool-type-group
        // reset (pipeline.cpp). Pools that call LLRenderPass::
        // applyModelMatrix() per draw item (lldrawpool.cpp) self-correct
        // regardless, but LLDrawPoolWater does NOT call it (its vertices
        // are pre-baked in world/agent space by LLVOWater::updateGeometry(),
        // expecting only the camera view matrix) - without this reset,
        // water inherits whatever model matrix the last alpha-blended
        // object drawn by POOL_ALPHA_PRE_WATER happened to leave bound.
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

    // Restores colorWriteMask to (true,true) after it was set to
    // (true,false) at the top of this function for the alpha/fullbright/
    // glow pool loop - real, sticky D3D11 blend state (OMSetBlendState) that
    // nothing else resets before the UI draw pass. This function runs both
    // for the main scene and for HUD attachments (render_hud_attachments()).
    gDX.setColorWriteMask(true, true);

    // Mirrors GL's own placement exactly (right after the post-deferred pool
    // loop, before restoring wireframe fill mode) - see the GL reference
    // baseline's LLPipeline::renderGeomPostDeferred(). gCubeSnapshot-gated
    // like GL: reflection-probe/snapshot captures shouldn't show debug
    // overlays baked into the probe's result.
    if (!gCubeSnapshot)
    {
        pipeline.renderDebug();
    }

    if (gUseWireframe)
    {
        DXStateCache::sWireframeScopeActive = false;
        gDX.applyDXRasterizerState();
    }

    // gGLLastModelView/gGLLastProjection's "advance to current, for next
    // frame's reprojection" bookkeeping lives in
    // DXPipeline::renderDeferredLighting()'s tail instead of here - this
    // function runs from multiple contexts (world camera early and late,
    // and the HUD camera via render_hud_attachments()), and letting the
    // HUD-camera call clobber these each frame broke SSAO's temporal
    // resolve and SSR's ray march, which must reproject off the previous
    // frame's WORLD camera, not the HUD's.
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

    // DXRenderTarget::bindSwapChainBackBuffer() (called by both
    // presentDeferredScreen() draw paths below) always sets a viewport
    // covering the FULL swap-chain/back-buffer size - correct for its other
    // caller (LLRenderTarget::flush()'s generic restore case), but wrong for
    // the final present-to-screen step, which must confine 3D content to
    // LLViewerWindow::mWorldViewRectRaw (the area left over after menu/
    // location bar chrome, matching GL's glViewport()). Overridden here
    // instead of changing bindSwapChainBackBuffer()'s own default.
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

    // Single, shared "put this render target on screen" chokepoint for both
    // the fallback (unlit/no-gamma-shader) case and the real gamma-corrected/
    // post-fx'd case. Primary path uses gDeferredPostNoDoFNoiseProgram
    // (depth-aware noise dithering, same permutation GL's renderFinalize()
    // tail uses); the placeholder VS/PS pair is kept only as a fallback if
    // that shader failed to compile.
    //
    // Selects which offscreen eye target (if any) this frame's world-scene
    // composite should land in, via LLViewerWindow::mMaskMode/getMaskMode()
    // (same enum LLViewerCamera's stereo frustum math reads).
    LLRenderTarget* getCurrentStereoEyeTarget(LLPipeline& pipeline)
    {
        S32 mode = gViewerWindow->getMaskMode();
        if (mode == MASK_MODE_LEFT) { return &pipeline.mStereoEyeL; }
        if (mode == MASK_MODE_RIGHT) { return &pipeline.mStereoEyeR; }
        return nullptr;
    }

    void presentFinal(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dest = nullptr)
    {
        if (!src || !src->getColorSRV(0))
        {
            return;
        }

        // When dest is given (stereo mode, one eye's pass), capture this
        // eye's final composited frame into its own offscreen target
        // instead of the swap chain - see stereoAnaglyphF.hlsl's comment.
        // dest is exactly world_rect-sized (LLPipeline::mStereoEyeL/R, the
        // WORLD VIEW resolution not the full chrome-included window), so no
        // world_rect viewport offset is needed here, unlike the swap-chain
        // case below. A real LLRenderTarget::bindTarget() call already
        // manages LLRenderTarget::sBoundTarget bookkeeping on its own.
        if (dest)
        {
            dest->bindTarget(false);

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
            }
            // Deliberately no raw-placeholder fallback here (unlike the
            // swap-chain path below) - this exact shader already backs the
            // ordinary non-stereo present path too, so if it ever failed to
            // compile the whole viewer would already be in far worse shape
            // than a stereo-only gap.
            dest->flush();
            return;
        }

        DXRenderTarget::bindSwapChainBackBuffer();
        // This call updates the real D3D11 render target directly via
        // DXRenderTarget, bypassing LLRenderTarget::bindTarget()/flush()'s
        // sBoundTarget/mPreviousRT bookkeeping. Whatever LLRenderTarget was
        // last left bound-but-not-flushed (e.g. mPostPongMap, left bound by
        // combineGlow()) stays "remembered" as sBoundTarget otherwise - a
        // silent bug when a HUD is attached and this function runs a second
        // time that frame: doWaterExclusionMask()'s bindTarget()/flush()
        // cycle would capture/restore that stale target instead of the real
        // back buffer, and nothing downstream ever rebinds it, so the whole
        // UI silently lands in a stale offscreen texture. Reset the
        // bookkeeping here (same pattern as LLHLSLShader::sCurBoundShaderPtr
        // below).
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

        // Fallback: real shader failed to compile - original placeholder blit.
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

        // This fallback binds its placeholder VS/PS via raw
        // ctx->VSSetShader()/PSSetShader() calls, bypassing
        // LLHLSLShader::bind() - so LLHLSLShader::sCurBoundShaderPtr (which
        // bind() uses to skip redundant VSSetShader/PSSetShader calls) never
        // learns the real bound shader changed, and the next real bind()
        // call of that same shader object would wrongly skip the rebind.
        // Calling unbind() here nulls sCurBoundShaderPtr, forcing a genuine
        // rebind next time.
        LLHLSLShader::unbind();
    }
}

// static
void DXPipeline::presentDeferredScreen(LLPipeline& pipeline)
{
    // Prefers the real lit result (mRT->screen, written by
    // renderDeferredLighting()) over deferredScreen's raw, unlit diffuse
    // attachment - but ONLY when renderDeferredLighting() actually ran and
    // completed THIS frame (sScreenLitThisFrame). A getColorSRV(0)!=nullptr
    // check alone is not sufficient: mRT->screen is allocated
    // unconditionally every frame, so its SRV is always non-null even when
    // renderDeferredLighting() early-returned without writing anything.
    bool use_lit = sScreenLitThisFrame && pipeline.mRT->screen.getColorSRV(0) != nullptr;

    LLRenderTarget* diffuse_rt = use_lit ? &pipeline.mRT->screen : &pipeline.mRT->deferredScreen;
    if (!diffuse_rt->getColorSRV(0))
    {
        // Nothing has allocated the deferred screen's diffuse attachment
        // yet (e.g. very first frame(s) during startup) - nothing to
        // present, leave the back buffer as DXContext::beginFrame() left it.
        return;
    }

    // Only the genuinely lit result needs gamma correction - the unlit
    // fallback (deferredScreen's raw diffuse attachment, shown when
    // renderDeferredLighting() didn't run this frame) is a debug/startup
    // view, so it keeps going through the raw placeholder blit unchanged.
    //
    // LLPipeline::tonemap() (pipeline.cpp) is GL's real per-frame present
    // pass when RenderHDREnabled is on - postDeferredTonemap.hlsl, which
    // tonemaps THEN gamma-corrects. When RenderHDREnabled is off, GL falls
    // back to LLPipeline::gammaCorrect() (postDeferredGammaCorrect.hlsl -
    // linear_to_srgb() + a hard clamp(0,1), no tonemap curve). This mirrors
    // LLPipeline::tonemap()'s own no_post/legacy_gamma shader selection and
    // uniform uploads, gated the same way GL gates it (RenderHDREnabled).
    // Without the real tonemap curve, bright HDR values (e.g. an
    // unattenuated sky) hard-clip to white instead of compressing
    // gracefully.
    //
    // Known limitation: generateExposure()/generateLuminance() (the
    // temporal auto-exposure chain feeding mExposureMap when HDR is on) are
    // still DX_RENDER no-ops - mExposureMap stays at its static neutral
    // (1,1,1,0) clear value rather than adapting to scene luminance. The
    // tonemap curve itself is correct either way; only auto-exposure
    // adaptation is a placeholder.
    //
    // copyScreenSpaceReflections() is already fully DX-safe as written
    // (gDX.getTexUnit()->bind()/LLGLDepthTest/mScreenTriangleVB/
    // gCopyDepthProgram); it just needed a DX_RENDER call site here, since
    // GL's own call to it lives inside renderFinalize(), whose GL body this
    // function replaces. Mirrors GL's order (copy happens before tonemap) -
    // mSceneMap gets this frame's just-finished lit
    // scene, consumed by the next frame's SSR ray march (standard
    // temporal-reuse, not a bug).
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
            // Writes to mPostPingMap (the same GL_RGBA scratch buffer GL's
            // post-fx chain ping-pongs through), not the back buffer
            // directly, so later stages (glow, DoF, FXAA/SMAA) have a real
            // intermediate to read/write before anything reaches the
            // screen - matches GL's gammaCorrect(&mRT->screen, &mPostPingMap)
            // shape. presentFinal() below is the single "put this on
            // screen" chokepoint every stage eventually feeds.
            // false: mPostPingMap has no depth attachment of its own.
            pipeline.mPostPingMap.bindTarget(false);

            gamma_shader.bind();
            gamma_shader.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, &pipeline.mRT->screen, false, LLTexUnit::TFO_POINT);
            gamma_shader.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (F32)pipeline.mRT->screen.getWidth(), (F32)pipeline.mRT->screen.getHeight());

            // Only the tonemap family (postDeferredTonemap.hlsl) declares/
            // uses exposureMap/exposure/tonemap_mix/tonemap_type;
            // postDeferredGammaCorrect.hlsl does not.
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

            // LLPipeline::generateGlow()/combineGlow() (pipeline.cpp) are
            // called directly here, unmodified - everything they touch was
            // already DX-hardened. The only real gap was uniform2f(U32,...)/
            // uniform3f(U32,...) (GLOW_DELTA, GLOW_LUM_WEIGHTS,
            // GLOW_WARMTH_WEIGHTS, DEFERRED_SCREEN_RES) being a silent no-op
            // under DX_RENDER - fixed at the source (llhlslshader.cpp) so
            // every caller benefits, not just glow.
            pipeline.generateGlow(&pipeline.mPostPingMap);

            // NOTE: glowExtractV.hlsl must stay position-only input with
            // texcoord derived procedurally (see postDeferredV.hlsl) - it
            // draws with mScreenTriangleVB, which supplies no TEXCOORD0
            // attribute; declaring one there fails CreateInputLayout()
            // (E_INVALIDARG) and silently skips the extract draw entirely.

            LLRenderTarget* sourceBuffer = &pipeline.mPostPingMap;
            LLRenderTarget* targetBuffer = &pipeline.mPostPongMap;

            pipeline.combineGlow(sourceBuffer, targetBuffer);
            std::swap(sourceBuffer, targetBuffer);

            // DoF and FXAA/SMAA are called directly, unmodified - the only
            // real DX_RENDER gap was 3 raw glViewport() calls (renderDoF()
            // x2, applyFXAA() x1) assuming an implicit GL default-framebuffer
            // viewport, fixed at the source in pipeline.cpp (guarded #ifdef
            // DX_RENDER, using DXContext::setViewport()). Mirrors GL's
            // renderFinalize() order: DoF is optional (RenderDepthOfField),
            // then exactly one of FXAA or SMAA runs per RenderFSAAType.
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

            // Develop > Rendering > Buffer Visualization: visualizeBuffers()
            // is already backend-agnostic, it just needed a call site here
            // (GL's only call sites sit in renderFinalize()'s GL-only tail,
            // unreachable under DX_RENDER). Mirrors GL's ordering: after
            // FXAA/SMAA, before the OpenCL effects block.
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

            // OpenCL post-fx effects (kveffects.cpp): mirrors GL's
            // renderFinalize() "apply effects before final draw" block.
            // updateEffectMask()/effectsMask are normally refreshed by GL's
            // renderFinalize() body, which never runs under DX_RENDER, so
            // both must be called/read from here instead. No
            // glFinish()-equivalent needed before the read:
            // DXReadback::readPixels()'s Map(D3D11_MAP_READ) is already a
            // synchronous GPU/CPU coherence point.
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

            gLastCompositedPostTarget = sourceBuffer;
            presentFinal(pipeline, sourceBuffer, getCurrentStereoEyeTarget(pipeline));
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
    gLastCompositedPostTarget = diffuse_rt;
    presentFinal(pipeline, diffuse_rt, getCurrentStereoEyeTarget(pipeline));
}

// static
void DXPipeline::presentStereoComposite(LLPipeline& pipeline)
{
    if (!pipeline.mStereoEyeL.getColorSRV(0) || !pipeline.mStereoEyeR.getColorSRV(0))
    {
        // One or both eyes never actually captured anything this frame
        // (e.g. very first frame(s) during startup) - nothing to compose,
        // leave the back buffer as DXContext::beginFrame() left it, same
        // convention as presentDeferredScreen()'s own early-out.
        return;
    }

    DXRenderTarget::bindSwapChainBackBuffer();
    // See presentFinal()'s matching comment - a raw DXRenderTarget bind
    // bypasses LLRenderTarget::bindTarget()'s own sBoundTarget bookkeeping.
    LLRenderTarget::sBoundTarget = nullptr;
    setPresentViewport();

    if (!gStereoAnaglyphProgram.isComplete())
    {
        // Shader failed to compile - nothing sane to present in this case
        // (unlike presentFinal()'s raw-placeholder fallback, there's no
        // single-eye image to fall back to that wouldn't just look like a
        // half-broken mono frame) - leave the back buffer as-is rather than
        // guess.
        return;
    }

    gStereoAnaglyphProgram.bind();
    // Bound via LLShaderMgr's existing DIFFUSE_MAP/ALTERNATE_DIFFUSE_MAP
    // reserved-uniform slots (LLHLSLShader::bindTexture(S32,...)) rather
    // than a custom texture name - see stereoAnaglyphF.hlsl's comment for
    // why the string-keyed overload doesn't work under DX_RENDER.
    gStereoAnaglyphProgram.bindTexture(LLShaderMgr::DIFFUSE_MAP, &pipeline.mStereoEyeL);
    gStereoAnaglyphProgram.bindTexture(LLShaderMgr::ALTERNATE_DIFFUSE_MAP, &pipeline.mStereoEyeR);

    // True-color red/cyan default - see stereoAnaglyphF.hlsl's comment.
    // Both diagonal, so row-vs-column-major layout is a non-issue (a
    // diagonal matrix is its own transpose) - verify this before ever
    // shipping a non-diagonal preset. 4x4, not 3x3: uniformMatrix4fv's
    // name-based overload is the only one with a real DX_RENDER upload path.
    static const F32 s_left_eye_matrix[16] = {
        1.f, 0.f, 0.f, 0.f,
        0.f, 0.f, 0.f, 0.f,
        0.f, 0.f, 0.f, 0.f,
        0.f, 0.f, 0.f, 0.f,
    };
    static const F32 s_right_eye_matrix[16] = {
        0.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 0.f,
    };
    static LLStaticHashedString s_left_matrix_name("left_eye_matrix");
    static LLStaticHashedString s_right_matrix_name("right_eye_matrix");

    gStereoAnaglyphProgram.uniformMatrix4fv(s_left_matrix_name, 1, false, s_left_eye_matrix);
    gStereoAnaglyphProgram.uniformMatrix4fv(s_right_matrix_name, 1, false, s_right_eye_matrix);

    {
        // No depth test/write - this target has no meaningful depth of its
        // own (each eye's real depth stayed in its own offscreen pass, not
        // carried into the composite). Known limitation: 3D-in-UI-space
        // content (manipulator gizmos etc.) won't depth-test correctly
        // against the composited result yet in stereo mode - see task
        // #220's memory for the full list of deferred follow-up work.
        LLGLDepthTest depth_test(GL_FALSE, GL_FALSE);
        pipeline.mScreenTriangleVB->setBuffer();
        pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
    }

    gStereoAnaglyphProgram.unbind();
}

// static
void DXPipeline::renderDeferredLighting(LLPipeline& pipeline)
{
    LL_RECORD_BLOCK_TIME(FTM_RENDER_DEFERRED);

    // Ambient+sun/atmospherics (softenLightF/V.hlsl) mirrors the
    // "RenderDeferredAtmospheric" block of LLPipeline::renderDeferredLighting()'s
    // GL body; the sun-shadow/SSAO lightmap pass and local lights/spotlights
    // are further down in this same function.
    //
    // Must reset every frame BEFORE any early-return below, so
    // presentDeferredScreen() never mistakes a skipped frame for a lit one
    // - see sScreenLitThisFrame's own comment.
    sScreenLitThisFrame = false;

    if (!gDeferredSoftenProgram.isComplete())
    {
        // Shader didn't compile (or isn't loaded) - leave mRT->screen
        // untouched. presentDeferredScreen() falls back to its existing
        // raw deferredScreen blit, so this is a graceful no-op, not a
        // missing frame.
        return;
    }

    // GL's RenderDeferredAtmospheric gate (pipeline.cpp) wraps ONLY the
    // soften/ambient shader draw call itself - not the sun/moon-dir
    // transform, the shadow/SSAO lightmap pass, or the local-light/
    // spotlight loops after it. Must NOT early-return the whole function
    // on this gate, or all local/projected lights vanish whenever
    // RenderDeferredAtmospheric is off.

    // NOTE: mTransformedSunDir/mTransformedMoonDir (feed shadowUtil.hlsl's
    // sun_dir/moon_dir uniforms) must be computed every call, even though
    // setupHWLights() is also called elsewhere under DX_RENDER. If left at
    // their zero-initialized default, normalize((0,0,0)) is NaN in HLSL
    // (not a zero vector), and any comparison against that NaN - including
    // sampleDirectionalShadow()'s cascade-select guard - is unconditionally
    // false per IEEE 754, silently disabling all shadow sampling.
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

    // Real sun-shadow/SSAO lightmap pass, replacing an earlier neutral-white
    // stopgap (still used as the "shadows off" fallback below). Mirrors
    // GL's `if ((RenderDeferredSSAO && !gCubeSnapshot) || RenderShadowDetail
    // > 0)` block: gDeferredSunProgram/gDeferredSunProbeProgram reads
    // shadowMap0-5 (bound by bindDeferredShader()'s bindShadowMaps()) and
    // writes per-pixel shadow/spot-shadow occlusion into mRT->deferredLight.
    LLRenderTarget& deferred_light_target = pipeline.mRT->deferredLight;

    // Snapshots last frame's fully-resolved lightmap into mSSAOHistory
    // BEFORE either branch below overwrites deferred_light_target - same
    // pattern as LLPipeline::generateExposure()'s mLastExposure refresh. No
    // first-frame guard needed: mSSAOHistory is pre-seeded to (1,1,1,1) at
    // allocation time.
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
        // Shadows AND SSAO both off: skip the real lightmap pass, same as
        // GL's early-out. Neutral "fully lit" fill - softenLightF.hlsl's
        // scol_ambocc read must never come back (0,0) ("fully shadowed/
        // occluded") when there's no real lightmap data behind it.
        deferred_light_target.bindTarget();
        deferred_light_target.clearColor(1.0f, 1.0f, 1.0f, 1.0f);
        deferred_light_target.flush();
    }

    LLRenderTarget* screen_target = &pipeline.mRT->screen;

    // Gaussian blur/soften pass for the sun-shadow/SSAO lightmap - without
    // it, calcAmbientOcclusion()'s raw per-pixel term (aoUtil.hlsl) reaches
    // softenLightF.hlsl unsmoothed; since the noise sample is keyed to
    // SCREEN position, a moving camera reads as flicker that settles once
    // it stops. Mirrors GL's gDeferredBlurLightProgram/blurLightV+F.hlsl
    // block - a depth/normal-aware bilateral blur that rejects samples
    // across depth discontinuities, so it won't smear AO across real edges.
    // Purely additive; the underlying SSAO computation is untouched.
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

        // pipeline.cpp's own sDelta/sDistFactor/sKern/sKernScale are
        // file-static there; redeclared here with identical string literals
        // matching blurLightF.hlsl's uniform names exactly.
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

    // Temporal-resolve pass: reprojects mSSAOHistory (last frame's value)
    // into this frame's screen space and blends it with the just-blurred
    // deferred_light_target, then blits the result back so the soften pass
    // below sees the final value. Reads depth as an SRV (getPosition() in
    // temporalResolveSSAOF.hlsl), so - like the soften pass below -
    // screen_target must be bound WITHOUT its shared depth attached
    // (bindTarget(false)); binding it with depth hits the D3D11 OM/SRV
    // hazard described in that pass's comment.
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

        // Uploaded manually rather than relying on bindDeferredShader()'s
        // bindReflectionProbes() call for inv_modelview_delta -
        // bindReflectionProbes() early-returns entirely when
        // sReflectionProbesEnabled is false, which would leave this
        // shader's inv_modelview_delta uninitialized whenever reflection
        // probes are off. Same formula, computed independently so this
        // pass never depends on that unrelated toggle.
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

    // screen_target shares its depth buffer with deferredScreen, and this
    // pass reads that SAME depth buffer as an SRV via bindDeferredShader()'s
    // DEFERRED_DEPTH bind just below. D3D11 forbids a resource being bound
    // as an OM output and a shader-stage SRV input at the same time -
    // binding with the DSV attached here silently forces the depth SRV to
    // NULL, reading back as a flat 0 in softenLightF.hlsl's getDepth().
    // This pass doesn't depth-test/write anyway, so leaving the DSV unbound
    // is correct, not a workaround.
    screen_target->bindTarget(false);
    // Matches GL's screen_target->clear(GL_COLOR_BUFFER_BIT) - color only. A
    // default-mask clear() here would also clear the shared depth buffer
    // (mUseDepth is true via shareDepthBuffer()) back to its far-clear
    // value, destroying the real depth just written by the opaque pass.
    screen_target->clear(GL_COLOR_BUFFER_BIT);

    LLHLSLShader& soften_shader = gDeferredSoftenProgram;

    // Only the soften/ambient draw itself is atmospheric-gated (matches
    // GL's real scope). When off, screen_target stays at the plain clear()
    // above - local lights then additively blend onto a black base with no
    // ambient/sun contribution, not a fully blank frame.
    if (LLPipeline::RenderDeferredAtmospheric)
    {
        // NOTE: if ground ambient looks frozen regardless of the
        // Reflection Probe Ambiance slider, check llsettingsvo.cpp's
        // applySpecial() - it substitutes a fixed fallback constant for any
        // nonzero ambiance whenever LLPipeline::sReflectionProbesEnabled is
        // false. This C++/shader uniform-upload chain is not the culprit.
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
            // Fullscreen post-process triangles shouldn't rely on ambient
            // rasterizer state (this is a raw mScreenTriangleVB->drawArrays()
            // call, not wrapped by any LLDrawPool's own state setup).
            LLGLDisable   cullface(GL_CULL_FACE);

            // full screen blit
            pipeline.mScreenTriangleVB->setBuffer();
            pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
        }

        pipeline.unbindDeferredShader(soften_shader);
    }

    sScreenLitThisFrame = true;

    // NOTE: screen_target was left with no depth-stencil view attached
    // (bindTarget(false) above, for the soften pass's depth-as-SRV-while-DSV
    // hazard). The local-light loops below need LLGLDepthTest(GL_TRUE,
    // GL_FALSE) (test on, write off) to actually occlude against opaque
    // geometry, so it must be re-attached here - but read-only (mReadOnlyDSV
    // via rebindWithDepth(true, true)), not the normal read/write mDSV: the
    // light shaders (pointLightF.hlsl/spotLightF.hlsl) also sample this same
    // depth buffer as an SRV via bindDeferredShader()'s DEFERRED_DEPTH bind,
    // and a read/write DSV would re-trigger the OM-output-vs-SRV-input
    // hazard. Covers both the point-light and spotlight loops below (nothing
    // rebinds screen_target in between); superseded later in this function
    // by a non-read-only rebindWithDepth(true) for real 3D geometry.
    screen_target->rebindWithDepth(true, true);

    // Local point lights: mirrors GL's RenderLocalLightCount-gated block,
    // non-spotlight lights drawn as additively-blended cube-volume geometry
    // (sDXBoxLightVB, see its own comment for why this couldn't just reuse
    // GL's mCubeVB directly) via gDeferredLightProgram/pointLightV/F.hlsl.
    // Spotlights/projectors (gDeferredSpotLightProgram, see the spot_lights
    // block below) reuse setupSpotLight() (pipeline.cpp) directly - pure
    // math + already-DX-safe uniform/texture calls.
    {
        static LLCachedControl<S32> local_light_count(gSavedSettings, "RenderLocalLightCount", 256);
        static LLCachedControl<S32> probe_level(gSavedSettings, "RenderReflectionProbeLevel", 0);

        // GL's matching gate is `local_light_count > 0 && (!gCubeSnapshot ||
        // probe_level > 0)` - local lights must be suppressed during
        // reflection-probe capture unless probe ambiance is above 0.
        // light_scale below must also mirror GL's
        // mReflectionMapManager.mLightScale multiply during capture, or
        // probe captures come out double-lit/washed-out relative to GL.
        F32 light_scale = 1.f;
        if (gCubeSnapshot)
        {
            light_scale = pipeline.mReflectionMapManager.getLightScale();
        }

        if (local_light_count > 0 && (!gCubeSnapshot || probe_level > 0))
        {
            // GL resets both spot-shadow target slots every frame BEFORE
            // the priority competition in setupSpotLight() runs. Without
            // this, that competition (called from both the box-path and
            // fullscreen-path spotlight blocks below) compares each frame's
            // candidate against whatever won a slot on some previous frame
            // instead of a clean slate - generateSunShadow()'s fade ramp
            // drives PROJECTOR_SHADOW_FADE from this same state, so a stale
            // competition causes visible brightness swings as a projected
            // light's priority changes (e.g. on zoom-in).
            if (!gCubeSnapshot)
            {
                for (U32 i = 0; i < 2; i++)
                {
                    pipeline.mTargetShadowSpotLight[i] = nullptr;
                }
            }

            // DEFERRED_LIGHT_FALLOFF is a pipeline.cpp file-scope const
            // (not exposed to other translation units) - duplicated here;
            // a fixed tuning constant GL itself never varies.
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

            LLGLDepthTest depth(GL_TRUE, GL_FALSE);

            // setSceneBlendType(BT_ADD) above only sets the blend FACTORS
            // (blendFunc(ONE,ONE)) - it does not enable blending itself.
            // LLRender::applyDXBlendState() reads DXState::isEnabled(GL_BLEND)
            // to decide whether to build an enabled D3D11 blend state at
            // all; if blend was left disabled by an earlier pass (e.g. the
            // ambient pass disables it for its own fullscreen draw), every
            // light-volume draw here would REPLACE the pixel instead of
            // adding to it - only the last light drawn at a pixel would
            // show. Enable explicitly rather than trust inherited state.
            LLGLEnable blend(GL_BLEND);

            // Same reasoning as the blend fix above, for GL_CULL_FACE: this
            // 3D box-volume geometry has no explicit cull-state management
            // of its own, so it silently inherits whatever an earlier pass
            // (e.g. the opaque G-buffer pass, which legitimately enables
            // cull for solid meshes) left behind. The octant/cypher
            // technique already selects the correct visible triangles via
            // DATA (which cypher/winding to use, per camera-relative
            // octant) - it needs no runtime backface elimination, so
            // disabling cull entirely here is correct, not just a
            // convention match.
            LLGLDisable cullface(GL_CULL_FACE);

            // Collected during this same pass (mirrors GL's own spot_lights
            // vector) and rendered afterward in its own block, once
            // light_shader (gDeferredLightProgram) is unbound - see below.
            std::vector<LLDrawable*> spot_lights;

            // Camera-inside-light-box buckets - mirrors GL's
            // fullscreen_lights/fullscreen_spot_lights and the modelview
            // matrix used to transform their centers into view space for
            // the fullscreen multi-light/multi-spotlight shaders.
            glm::mat4 mat = get_current_modelview();
            std::vector<LLVector4> fullscreen_lights;
            std::vector<LLVector4> fullscreen_light_colors;
            std::vector<LLDrawable*> fullscreen_spot_lights;

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

                // Small decorative local lights (e.g. walkway lanterns) are
                // commonly authored with a tiny real radius - `s` feeds
                // BOTH the dist<=1.0 shader gate (multiPointLightF.hlsl/
                // pointLightF.hlsl) AND this loop's own AABB/
                // camera_outside_box tests, so a small authored radius means
                // the camera has to be almost standing inside the light
                // before it contributes any color. Floors the EFFECTIVE
                // radius used for downstream distance/falloff math to at
                // least RenderLocalLightMinRadius, while the light-is-on
                // check above still uses the TRUE raw radius (a genuinely
                // radius=0 light stays off). 0 = original behavior (no
                // floor).
                static LLCachedControl<F32> min_light_radius(gSavedSettings, "RenderLocalLightMinRadius", 15.f);
                F32 s = llmax(raw_radius, (F32)min_light_radius);

                // RenderLocalLightFrustumCulling now also gates this
                // render-loop's own AABB check (previously only gated
                // calcNearbyLights()'s candidate-list prefilter);
                // RenderLocalLightFrustumMargin adds a spatial margin to it.
                // Both are real, working knobs.
                static LLCachedControl<bool> frustum_culling_enabled(gSavedSettings, "RenderLocalLightFrustumCulling", true);
                static LLCachedControl<F32> frustum_margin(gSavedSettings, "RenderLocalLightFrustumMargin", 32.f);
                S32 aabb_result = 1;
                if (frustum_culling_enabled)
                {
                    LLVector4a sa;
                    sa.splat(s + frustum_margin);
                    aabb_result = camera->AABBInFrustumNoFarClip(center, sa);
                }

                // Whole-light counterpart to multiPointLightF.hlsl's
                // per-pixel N.L smoothstep: aabb_result is a hard, zero-
                // margin binary test recomputed every frame from camera-
                // relative geometry, which pops under continuous camera
                // micro-motion. It's a single decision for the entire light
                // (unlike a per-pixel shader value), so a real per-light
                // temporal hold is meaningful here. RenderLocalLightFrustumCulling
                // defaults OFF, so aabb_result is 1 unconditionally for most
                // users and this hold is inert by default - it only matters
                // once frustum culling is turned on (paired with it for
                // lower-end systems: cull aggressively, hold briefly to
                // avoid flicker).
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

                // v1 drew every light through the box-mesh path
                // unconditionally, which silently drops a light whenever
                // the camera is inside its box - the box's near-camera-
                // facing triangles (selected by getBoxLightCypher()'s
                // octant test) end up behind the camera/near-plane and get
                // clipped away. Mirrors GL's camera-inside-vs-outside split:
                // outside keeps the box-mesh draw below; inside collects
                // into fullscreen_lights/fullscreen_spot_lights, drawn via
                // the fullscreen multi-light/multi-spotlight passes further
                // down. Also applies to projected/spot lights.
                LLVector3 cam_origin = camera->getOrigin();

                // NOTE: this is real Euclidean clearance (2x the light's
                // own size), not GL's flat per-axis 0.2m margin. A per-axis
                // OR test only guarantees the camera clears the box in AT
                // LEAST ONE world axis - the camera can sit deep inside 2 of
                // 3 axes, barely past the margin in the third, and still
                // pass as "outside," at which point half the box's corners
                // land on the wrong side of the camera's eye-plane and
                // produce corrupted, screen-spanning clip geometry. Scaling
                // the margin to the light's own size keeps close-in lights
                // on the fullscreen multi-light path (immune to this clip
                // corruption) instead of ever reaching the box-mesh path in
                // this degenerate zone.
                F32 dist_to_center = (cam_origin - LLVector3(c[0], c[1], c[2])).length();
                bool camera_outside_box = dist_to_center > (s * 2.0f + 0.2f);

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

            // setupSpotLight() (pipeline.cpp) is pure math + already-DX-safe
            // uniform/enableTexture/bind() calls, reused as-is by
            // composition. Reuses the same box-volume draw path as the
            // point lights above (sDXBoxLightVB/getBoxLightCypher()) rather
            // than GL's separate mCubeVB TRIANGLE_FAN. Only ever holds
            // camera-OUTSIDE-box spotlights - camera-inside ones are
            // diverted to fullscreen_spot_lights above and drawn via the
            // gDeferredMultiSpotLightProgram fullscreen pass below.
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

            // Fullscreen multi-light pass for lights the camera is inside
            // the box of. Mirrors GL: batch up to
            // LL_DEFERRED_MULTI_LIGHT_COUNT lights per draw, picking the
            // shader permutation compiled for exactly that many
            // (gDeferredMultiLightProgram[idx], idx = count-1). Depth test
            // off - this is a fullscreen triangle, not per-object geometry;
            // the shader reconstructs per-pixel position from the real
            // depth buffer via bindDeferredShader()'s DEFERRED_DEPTH channel.
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

            // Fullscreen multi-spotlight pass for spotlights the camera is
            // inside the box of. Mirrors GL: one draw per light (not
            // batched, unlike the point-light case above), via
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

    // Calls renderGeomPostDeferred() here, before this function's own
    // final flush() below, mirroring GL's structure exactly: GL calls it
    // from inside renderDeferredLighting() with screen_target still bound,
    // then flushes once. This draws into the still-bound mRT->screen (the
    // lit result), so alpha/water/fullbright/glow content reaches the
    // world scene without separate re-bind/flush plumbing in display().
    //
    // screen_target was bound with bind_depth=false above (see that
    // bindTarget() call's comment) to avoid the depth-as-SRV-while-DSV
    // hazard for the ambient pass's fullscreen draw - but that leaves no
    // depth-stencil view attached, so the real 3D alpha/fullbright/glow
    // geometry drawn here needs depth re-attached via rebindWithDepth()
    // (not a second bindTarget() call, which would trip its "not already
    // bound" assert) to depth-test correctly against opaque geometry.
    screen_target->rebindWithDepth(true);
    DXPipeline::renderGeomPostDeferred(pipeline, *LLViewerCamera::getInstance());

    // LLRenderTarget::flush() restores the previously-bound render target,
    // so it must run exactly once, after BOTH the ambient pass and the
    // local-lights loop (plus the post-deferred pass above) are done
    // writing to screen_target - unconditionally, since the ambient pass
    // always writes to it regardless of whether local_light_count gates
    // the local-lights block off.
    screen_target->flush();

    // Advances gGLLastModelView/gGLLastProjection to this frame's real
    // world camera, here at the single true end of the world-camera scene
    // render - NOT inside renderGeomPostDeferred(), which also runs from
    // an early world-camera pass and from render_hud_attachments() (HUD
    // camera). Doing it in either of those places clobbers the value SSAO's
    // temporal resolve and SSR depend on for reprojecting off the previous
    // frame's real world camera.
    if (!gCubeSnapshot)
    {
        for (U32 i = 0; i < 16; i++)
        {
            gGLLastModelView[i] = gGLModelView[i];
            gGLLastProjection[i] = gGLProjection[i];
        }
    }
}

void DXPipeline::bindSMAAStaticSamplers()
{
    DXSampler::bindStatic(13, 2, 1);
    DXSampler::bindStatic(14, 2, 0);
}
