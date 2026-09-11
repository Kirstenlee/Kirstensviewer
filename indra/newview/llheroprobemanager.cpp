/**
 * @file LLHeroProbeManager.cpp
 * @brief LLHeroProbeManager class implementation
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llheroprobemanager.h"
#include "llreflectionmapmanager.h"
#include "llviewercamera.h"
#include "llspatialpartition.h"
#include "llviewerregion.h"
#include "pipeline.h"
#include "llviewershadermgr.h"
#include "llviewercontrol.h"
#include "llenvironment.h"
#include "llstartup.h"
#include "llagent.h"
#include "llagentcamera.h"
#include "llviewerwindow.h"
#include "llviewerjoystick.h"
#include "llviewermediafocus.h"

#ifdef DX_RENDER
#include "DXDevice.h"
#include "DXReadback.h"
#endif

extern bool gCubeSnapshot;
extern bool gTeleportDisplay;

// get the next highest power of two of v (or v if v is already a power of two)
//defined in llvertexbuffer.cpp
extern U32 nhpo2(U32 v);

static void touch_default_probe(LLReflectionMap* probe)
{
    if (LLViewerCamera::getInstance())
    {
        LLVector3 origin = LLViewerCamera::getInstance()->getOrigin();
        origin.mV[2] += 64.f;

        probe->mOrigin.load3(origin.mV);
    }
}

LLHeroProbeManager::LLHeroProbeManager()
{
}

LLHeroProbeManager::~LLHeroProbeManager()
{
    cleanup();

    mHeroVOList.clear();
    mNearestHero = nullptr;
}

// helper class to seed octree with probes
void LLHeroProbeManager::update()
{
    if (!LLPipeline::RenderMirrors || !LLPipeline::sReflectionProbesEnabled || gTeleportDisplay || LLStartUp::getStartupState() < STATE_PRECACHE)
    {
        return;
    }

    LL_PROFILE_GPU_ZONE("hero manager update");
    llassert(!gCubeSnapshot); // assert a snapshot is not in progress
    if (LLAppViewer::instance()->logoutRequestSent())
    {
        return;
    }

    // S24 (2026-09-08, task #316): initReflectionMaps() now runs BEFORE the
    // one-time shader-reload workaround below (previously ran after it).
    // setShaders() (re)binds shader/texture-unit state; if the hero-probe
    // texture is reallocated to a new resolution AFTER that bind, the
    // workaround's shader rebind happens against a stale/mismatched probe
    // texture with nothing forcing a second rebind once the real texture is
    // ready - a second, structural reason mirrors could end up black after
    // a resolution change, independent of the workaround's own gating.
    initReflectionMaps();

    // Part of a hacky workaround to fix #3331.
    // For some reason clearing shaders will cause mirrors to actually work.
    // There's likely some deeper state issue that needs to be resolved.
    // - Geenz 2025-02-25
    //
    // S24 (2026-09-08, task #316): re-arms only on a genuine RenderMirrors
    // off->on transition or a hero probe resolution change
    // (LLHeroProbeManager::requireShaderReinit(), called from
    // llviewercontrol.cpp) - re-arming on every reset() from every caller
    // (e.g. an unrelated HDR toggle) turned every settings tweak into a
    // ~19s synchronous stall.
    if (!mInitialized && LLStartUp::getStartupState() > STATE_PRECACHE)
    {
        LLViewerShaderMgr::instance()->clearShaderCache();
        LLViewerShaderMgr::instance()->setShaders();
        mInitialized = true;
    }

    static LLCachedControl<bool> render_hdr(gSavedSettings, "RenderHDREnabled", true);

    if (!mRenderTarget.isComplete())
    {
        U32 color_fmt = render_hdr ? GL_RGBA16F : GL_RGBA8;
        mRenderTarget.allocate(mProbeResolution, mProbeResolution, color_fmt, true);
    }

    if (mMipChain.empty())
    {
        U32 res = mProbeResolution;

        // S24 (2026-09-03, task #266/#271): was an independently recomputed
        // `(U32)(log2((F32)res) + 0.5f)` guess - see mMaxProbeLOD's identical
        // fix in initReflectionMaps() above for the full explanation. Read
        // the texture's REAL allocated mip count instead.
        U32 count = mTexture->getDXTexture()->getMipLevels();

        mMipChain.resize(count);
        for (U32 i = 0; i < count; ++i)
        {
            mMipChain[i].allocate(res, res, render_hdr ? GL_RGBA16F : GL_RGBA8);
            res /= 2;
        }
    }

    llassert(mProbes[0] == mDefaultProbe);

    LLVector4a probe_pos;
    LLVector3 camera_pos = LLViewerCamera::instance().mOrigin;
    bool       probe_present = false;
    LLQuaternion cameraOrientation = LLViewerCamera::instance().getQuaternion();
    LLVector3    cameraDirection   = LLVector3::z_axis * cameraOrientation;

    if (mHeroVOList.size() > 0)
    {
        // Find our nearest hero candidate.
        float last_distance = 99999.f;
        float camera_center_distance = 99999.f;
        mNearestHero = nullptr;
        for (auto vo : mHeroVOList)
        {
            if (vo && !vo->isDead() && vo->mDrawable.notNull() && vo->isReflectionProbe() && vo->getReflectionProbeIsBox())
            {
                float distance = (LLViewerCamera::instance().getOrigin() - vo->getPositionAgent()).magVec();
                float center_distance = cameraDirection * (vo->getPositionAgent() - camera_pos);

                if (distance > LLViewerCamera::instance().getFar())
                    continue;

                LLVector4a center;
                center.load3(vo->getPositionAgent().mV);
                LLVector4a size;

                size.load3(vo->getScale().mV);

                bool visible = LLViewerCamera::instance().AABBInFrustum(center, size);

                if (distance < last_distance && center_distance < camera_center_distance && visible)
                {
                    probe_present = true;
                    mNearestHero = vo;
                    last_distance = distance;
                    camera_center_distance = center_distance;
                }
            }
            else
            {
                unregisterViewerObject(vo);
            }
        }

        // Don't even try to do anything if we didn't find a single mirror present.
        if (!probe_present)
            return;

        if (mNearestHero != nullptr && !mNearestHero->isDead() && mNearestHero->mDrawable.notNull())
        {
            LLVector3 hero_pos = mNearestHero->getPositionAgent();
            LLVector3 face_normal = LLVector3(0, 0, 1);

            face_normal *= mNearestHero->mDrawable->getWorldRotation();
            face_normal.normalize();

            LLVector3 offset = camera_pos - hero_pos;
            LLVector3 project = face_normal * (offset * face_normal);
            LLVector3 reject  = offset - project;
            LLVector3 point   = (reject - project) + hero_pos;

            mCurrentClipPlane.setVec(hero_pos, face_normal);
            mMirrorPosition = hero_pos;
            mMirrorNormal   = face_normal;

            probe_pos.load3(point.mV);

            // Detect visible faces of a cube based on camera direction and distance

            // Define the cube faces
            static LLVector3 cubeFaces[6] = {
                LLVector3(1, 0, 0),
                LLVector3(-1, 0, 0),
                LLVector3(0, 1, 0),
                LLVector3(0, -1, 0),
                LLVector3(0, 0, 1),
                LLVector3(0, 0, -1)
            };

            mProbes[0]->mOrigin = probe_pos;
            mProbes[0]->mRadius = mNearestHero->getScale().magVec() * 0.5f;
        }
        else
        {
            mNearestHero = nullptr;
            mDefaultProbe->mViewerObject = nullptr;
        }

        mHeroProbeStrength = 1;
    }
    else
    {
        mNearestHero = nullptr;
        mDefaultProbe->mViewerObject = nullptr;
    }
}

void LLHeroProbeManager::renderProbes()
{
    if (!LLPipeline::RenderMirrors || !LLPipeline::sReflectionProbesEnabled || gTeleportDisplay ||
        LLStartUp::getStartupState() < STATE_PRECACHE)
    {
        return;
    }

    static LLCachedControl<S32> sDetail(gSavedSettings, "RenderHeroReflectionProbeDetail", -1);
    static LLCachedControl<S32> sLevel(gSavedSettings, "RenderHeroReflectionProbeLevel", 3);
    static LLCachedControl<S32> sUpdateRate(gSavedSettings, "RenderHeroProbeUpdateRate", 0);

    // S24 (2026-08-22): a temporary diagnostic lived here during the "mirrors
    // render solid black" investigation - found mProbes[0]->mOccluded
    // flickering true/stuck-true for long stretches even with mNearestHero
    // valid, which skipped capture entirely (see the capture gate below for
    // the real fix/explanation). Removed per diagnostic-lifecycle convention.

    F32 near_clip = 0.01f;
    if (mNearestHero != nullptr && !mNearestHero->isDead() &&
        !gTeleportDisplay && !gDisconnected && !LLAppViewer::instance()->logoutRequestSent())
    {

        bool radiance_pass = gPipeline.mReflectionMapManager.isRadiancePass();

        gPipeline.mReflectionMapManager.mRadiancePass = true;
        mRenderingMirror = true;

        S32 rate = sUpdateRate;

        // rate must be divisor of 6 (1, 2, 3, or 6)
        if (rate < 1)
        {
            rate = 1;
        }
        else if (rate > 3)
        {
            rate = 6;
        }

        S32 face = gFrameCount % 6;

        // S24 (2026-08-22, task #156 follow-up): dropped the `!mOccluded`
        // gate - confirmed via diagnostic (S24Diag) that mProbes[0]->mOccluded
        // flickers true/false rapidly and gets stuck true for many
        // consecutive seconds even while mNearestHero is simultaneously
        // valid (i.e. the mirror IS the frustum-confirmed nearest visible
        // one). Root cause: mNearestHero selection (above, in update())
        // already does a real LLViewerCamera::AABBInFrustum() visibility
        // check before this ever runs, making the separate occlusion QUERY
        // redundant here - and actively harmful, since the query's box sits
        // right at the mirror's own opaque surface, making self-occlusion
        // (the mirror occluding its own probe test) the likely mechanism.
        // Unlike the main reflection manager's hundreds of scattered
        // automatic probes (where occlusion culling is a real, needed
        // optimization - task #182/#245/#249/#250), there is only ever ONE
        // hero probe, already selected as "the nearest visible mirror" -
        // skipping capture on a false-occluded frame just leaves the
        // texture stale/empty (confirmed root cause of mirrors rendering
        // solid black) for no performance benefit.
        if (!mProbes.empty() && !mProbes[0].isNull())
        {
            LL_PROFILE_ZONE_NUM(gFrameCount % rate);
            LL_PROFILE_ZONE_NUM(rate);

            bool dynamic = mNearestHero->getReflectionProbeIsDynamic() && sDetail() > 0;
            for (U32 i = 0; i < 6; ++i)
            {
                if ((gFrameCount % rate) == (i % rate))
                { // update 6/rate faces per frame
                    LL_PROFILE_ZONE_NUM(i);
                    updateProbeFace(mProbes[0], i, dynamic, near_clip);
                }
            }
            generateRadiance(mProbes[0]);
        }

        mRenderingMirror = false;

        gPipeline.mReflectionMapManager.mRadiancePass = radiance_pass;

        mProbes[0]->mViewerObject = mNearestHero;
        mProbes[0]->autoAdjustOrigin();
    }
}

// Do the reflection map update render passes.
// For every 12 calls of this function, one complete reflection probe radiance map and irradiance map is generated
// First six passes render the scene with direct lighting only into a scratch space cube map at the end of the cube map array and generate
// a simple mip chain (not convolution filter).
// At the end of these passes, an irradiance map is generated for this probe and placed into the irradiance cube map array at the index for this probe
// The next six passes render the scene with both radiance and irradiance into the same scratch space cube map and generate a simple mip chain.
// At the end of these passes, a radiance map is generated for this probe and placed into the radiance cube map array at the index for this probe.
// In effect this simulates single-bounce lighting.
void LLHeroProbeManager::updateProbeFace(LLReflectionMap* probe, U32 face, bool is_dynamic, F32 near_clip)
{
    LL_PROFILE_GPU_ZONE("hero probe update");

    // hacky hot-swap of camera specific render targets
    gPipeline.mRT = &gPipeline.mHeroProbeRT;

    probe->update(mRenderTarget.getWidth(), face, is_dynamic, near_clip);

    gPipeline.mRT = &gPipeline.mMainRT;

    S32 sourceIdx = mReflectionProbeCount;

    // Unlike the reflectionmap manager, all probes are considered "realtime" for hero probes.
    sourceIdx += 1;

        gDX.setColorMask(true, true);
    LLGLDepthTest depth(GL_FALSE, GL_FALSE);
    LLGLDisable cull(GL_CULL_FACE);
    LLGLDisable blend(GL_BLEND);

    // downsample to placeholder map
    {
        gDX.matrixMode(gDX.MM_MODELVIEW);
        gDX.pushMatrix();
        gDX.loadIdentity();

        gDX.matrixMode(gDX.MM_PROJECTION);
        gDX.pushMatrix();
        gDX.loadIdentity();

        gDX.flush();
        // S24 (2026-09-08, task #316): REVERTED same session - changing
        // this to `mProbeResolution` (matching gPipeline.mHeroProbeRT's
        // real 1x allocation) was live-tested and made the ONE previously-
        // working case (mirrors off at login, manually enabled) display
        // incorrectly (a static, angle-independent single cube face
        // visible) instead of fixing anything - net regression, not
        // progress. Back to the original `* 2` pending further
        // investigation into why that mismatch doesn't actually manifest
        // as the theory predicted.
        U32 res = mProbeResolution * 2;

        static LLStaticHashedString resScale("resScale");
        static LLStaticHashedString direction("direction");
        static LLStaticHashedString znear("znear");
        static LLStaticHashedString zfar("zfar");

        LLRenderTarget *screen_rt = &gPipeline.mHeroProbeRT.screen;
        LLRenderTarget *depth_rt  = &gPipeline.mHeroProbeRT.deferredScreen;

        // perform a gaussian blur on the super sampled render before downsampling
        {
            gGaussianProgram.bind();
            gGaussianProgram.uniform1f(resScale, 1.f / (mProbeResolution * 2));
            S32 diffuseChannel = gGaussianProgram.enableTexture(LLShaderMgr::DEFERRED_DIFFUSE, LLTexUnit::TT_TEXTURE);

            // horizontal
            gGaussianProgram.uniform2f(direction, 1.f, 0.f);
            gDX.getTexUnit(diffuseChannel)->bind(screen_rt);
            mRenderTarget.bindTarget();
            gPipeline.mScreenTriangleVB->setBuffer();
            gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
            mRenderTarget.flush();

            // vertical
            gGaussianProgram.uniform2f(direction, 0.f, 1.f);
            gDX.getTexUnit(diffuseChannel)->bind(&mRenderTarget);
            screen_rt->bindTarget();
            gPipeline.mScreenTriangleVB->setBuffer();
            gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
            screen_rt->flush();
            gGaussianProgram.unbind();
        }

        // S24 (2026-09-03, task #266/#271): was an independently recomputed
        // `(S32)(log2((F32)mProbeResolution) + 0.5f)` guess - see
        // LLReflectionMapManager's identical fix for the full explanation
        // (this variable feeds `mip` a few lines below, which would
        // otherwise be misaligned against mMipChain.size() now that it
        // correctly reads the real allocated mip count). Read the same real
        // count instead.
        S32 mips = (S32)mTexture->getDXTexture()->getMipLevels();

        gReflectionMipProgram.bind();
        S32 diffuseChannel = gReflectionMipProgram.enableTexture(LLShaderMgr::DEFERRED_DIFFUSE, LLTexUnit::TT_TEXTURE);
        S32 depthChannel   = gReflectionMipProgram.enableTexture(LLShaderMgr::DEFERRED_DEPTH, LLTexUnit::TT_TEXTURE);

        for (int i = 0; i < mMipChain.size(); ++i)
        {
            LL_PROFILE_GPU_ZONE("hero probe mip");
            mMipChain[i].bindTarget();
            if (i == 0)
            {
                gDX.getTexUnit(diffuseChannel)->bind(screen_rt);
            }
            else
            {
                gDX.getTexUnit(diffuseChannel)->bind(&(mMipChain[i - 1]));
            }

            gDX.getTexUnit(depthChannel)->bind(depth_rt, true);

            gReflectionMipProgram.uniform1f(resScale, 1.f / (mProbeResolution * 2));
            gReflectionMipProgram.uniform1f(znear, probe->getNearClip());
            gReflectionMipProgram.uniform1f(zfar, MAX_FAR_CLIP);

            gPipeline.mScreenTriangleVB->setBuffer();
            gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

            res /= 2;

            llassert(mMipChain.size() <= size_t(S32_MAX));
            GLint mip = i - (S32(mMipChain.size()) - mips);

            if (mip >= 0)
            {
                LL_PROFILE_GPU_ZONE("hero probe mip copy");
                mTexture->bind(0);

                // S24 (task #194 follow-up, 2026-08-13): unguarded raw GL -
                // this whole file had zero DX_RENDER support (gated behind
                // RenderMirrors, so never yet hit at runtime). Mirrors the
                // identical fix already landed in
                // llreflectionmapmanager.cpp's own mip-copy block (task
                // #147 step 5) - no SRV bind of mTexture needed for the
                // copy, see DXCubeArrayTexture's own header comment.
#ifdef DX_RENDER
                mTexture->getDXTexture()->copySliceFromBoundRenderTarget(mip, sourceIdx * 6 + face, res, res);
#else
                glCopyTexSubImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, mip, 0, 0, sourceIdx * 6 + face, 0, 0, res, res);
#endif

                mTexture->unbind();
            }
            mMipChain[i].flush();
        }

        gDX.popMatrix();
        gDX.matrixMode(gDX.MM_MODELVIEW);
        gDX.popMatrix();

        gDX.getTexUnit(diffuseChannel)->unbind(LLTexUnit::TT_TEXTURE);
        gReflectionMipProgram.unbind();
    }
}

// Separate out radiance generation as a separate stage.
// This is to better enable independent control over how we generate radiance vs. having it coupled with processing the final face of the probe.
// Useful when we may not always be rendering a full set of faces of the probe.
void LLHeroProbeManager::generateRadiance(LLReflectionMap* probe)
{
    S32 sourceIdx = mReflectionProbeCount;

    // Unlike the reflectionmap manager, all probes are considered "realtime" for hero probes.
    sourceIdx += 1;
    {
        mMipChain[0].bindTarget();
        static LLStaticHashedString sSourceIdx("sourceIdx");

        {
            // generate radiance map (even if this is not the irradiance map, we need the mip chain for the irradiance map)
            gHeroRadianceGenProgram.bind();
            mVertexBuffer->setBuffer();

            S32 channel = gHeroRadianceGenProgram.enableTexture(LLShaderMgr::REFLECTION_PROBES, LLTexUnit::TT_CUBE_MAP_ARRAY);
            mTexture->bind(channel);
            gHeroRadianceGenProgram.uniform1i(sSourceIdx, sourceIdx);
            gHeroRadianceGenProgram.uniform1f(LLShaderMgr::REFLECTION_PROBE_MAX_LOD, mMaxProbeLOD);
            gHeroRadianceGenProgram.uniform1f(LLShaderMgr::REFLECTION_PROBE_STRENGTH, mHeroProbeStrength);

            U32 res = mMipChain[0].getWidth();

            // S24 (2026-09-08, task #316): briefly changed to the full
            // mMipChain.size() (no division) after a cross-check against
            // LLReflectionMapManager's own radiance-gen loop looked like a
            // porting divergence - REVERTED same session. reflectionProbeF.
            // hlsl's tapHeroProbe() (`w = lerp(0, w, clamp(glossiness-0.75,
            // 0,1)*4)`, its own comment: "We only generate a quarter of the
            // mips for the hero probes") shows this /4 is deliberate,
            // intentionally matched by the shader's own glossiness gate -
            // not a bug. Confirmed unrelated to the real black-mirror cause
            // (LLRenderTarget::isComplete()/release(), fixed separately).
            for (int i = 0; i < mMipChain.size() / 4; ++i)
            {
                LL_PROFILE_GPU_ZONE("hero probe radiance gen");
                static LLStaticHashedString sMipLevel("mipLevel");
                static LLStaticHashedString sRoughness("roughness");
                static LLStaticHashedString sWidth("u_width");
                static LLStaticHashedString sStrength("probe_strength");

                gHeroRadianceGenProgram.uniform1f(sRoughness, (F32) i / (F32) (mMipChain.size() - 1));
                gHeroRadianceGenProgram.uniform1f(sMipLevel, (F32)i);
                gHeroRadianceGenProgram.uniform1i(sWidth, mProbeResolution);
                gHeroRadianceGenProgram.uniform1f(sStrength, 1);

                for (int cf = 0; cf < 6; ++cf)
                {  // for each cube face
                    // S24 (2026-08-31, DXCubeMap rewrite plan, Step 3): this
                    // loop used to build a per-face LLCoordFrame rotation
                    // matrix and load it via gDX.loadMatrix() - but
                    // radianceGenV.hlsl (gHeroRadianceGenProgram's vertex
                    // shader, shared with gRadianceGenProgram) has never
                    // read any matrix uniform at all since the task #147
                    // closed-form rewrite - it only reads `cubeFace`, which
                    // this loop never set. Every hero-probe face draw was
                    // therefore using whatever `cubeFace` happened to still
                    // be resident from the last program that set it (likely
                    // stale/wrong), silently corrupting hero-probe mirror
                    // content - a real, live bug, not dead code. Fixed by
                    // setting `cubeFace` directly, matching
                    // llreflectionmapmanager.cpp's own radiance/irradiance
                    // loops.
                    static LLStaticHashedString sHeroCubeFace("cubeFace");
                    gHeroRadianceGenProgram.uniform1i(sHeroCubeFace, cf);

                    mVertexBuffer->drawArrays(gDX.TRIANGLE_STRIP, 0, 4);

                    // S24 (task #194 follow-up, 2026-08-13): unguarded raw
                    // GL - mirrors the identical fix already landed in
                    // llreflectionmapmanager.cpp's own radiance-gen loop
                    // (task #147 step 5/#147-#184 follow-up). mMipChain[0]
                    // is bound as render target once before this whole
                    // face/mip loop (see mMipChain[0].bindTarget() a few
                    // lines up in this same function) - no SRV bind of
                    // mTexture needed for the copy, see
                    // DXCubeArrayTexture's own header comment.
#ifdef DX_RENDER
                    mTexture->getDXTexture()->copySliceFromBoundRenderTarget(i, probe->mCubeIndex * 6 + cf, res, res);
#else
                    glCopyTexSubImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, i, 0, 0, probe->mCubeIndex * 6 + cf, 0, 0, res, res);
#endif
                }

                if (i != mMipChain.size() - 1)
                {
                    res /= 2;
                    // S24 (task #194 follow-up, 2026-08-13): unguarded raw
                    // GL - same crash class as every other unfenced GL call
                    // found this session (null function pointer at runtime,
                    // since OpenGL is fully delinked from DX_RENDER=ON
                    // builds). Mirrors llreflectionmapmanager.cpp's own
                    // identical fix at its sibling radiance-gen loop.
#ifdef DX_RENDER
                    // S24 (2026-09-02, REVERTED same day): the negative-
                    // height-to-normal-viewport relocation attempt made
                    // zero difference to the empty-mip symptom it targeted
                    // and broke hero-probe mirror orientation live
                    // (confirmed, "Mirror Fault.PNG"). Back to the
                    // proven-correct negative-height viewport, matching
                    // llreflectionmapmanager.cpp's sibling sites
                    // (task #147/#184/#163).
                    {
                        D3D11_VIEWPORT vp = {};
                        vp.TopLeftX = 0.0f;
                        vp.TopLeftY = (float)res;
                        vp.Width = (float)res;
                        vp.Height = -(float)res;
                        vp.MinDepth = 0.0f;
                        vp.MaxDepth = 1.0f;
                        gDXDevice.getContext()->RSSetViewports(1, &vp);
                    }
#else
                    glViewport(0, 0, res, res);
#endif
                }
            }

            gHeroRadianceGenProgram.unbind();
        }

        mMipChain[0].flush();
    }
}

void LLHeroProbeManager::updateUniforms()
{
    if (!gPipeline.RenderMirrors)
    {
        return;
    }

    LL_PROFILE_GPU_ZONE("hpmu - uniforms");

    LLMatrix4a modelview;
    modelview.loadu(gGLModelView);
    LLVector4a oa; // scratch space for transformed origin
    oa.set(0, 0, 0, 0);
    mHeroData.heroProbeCount = 1;

    if (mNearestHero != nullptr && !mNearestHero->isDead())
    {
        if (mNearestHero->getReflectionProbeIsBox())
        {
            LLVector3 s = mNearestHero->getScale().scaledVec(LLVector3(0.5f, 0.5f, 0.5f));
            mProbes[0]->mRadius = s.magVec();
        }
        else
        {
            mProbes[0]->mRadius = mNearestHero->getScale().mV[0] * 0.5f;
        }

        modelview.affineTransform(mProbes[0]->mOrigin, oa);
        mHeroData.heroShape = 0;
        if (!mProbes[0]->getBox(mHeroData.heroBox))
        {
            mHeroData.heroShape = 1;
        }

        mHeroData.heroSphere.set(oa.getF32ptr());
        mHeroData.heroSphere.mV[3] = mProbes[0]->mRadius;
    }

    llassert(mMipChain.size() <= size_t(S32_MAX));
    mHeroData.heroMipCount = S32(mMipChain.size());
}

void LLHeroProbeManager::renderDebug()
{
    gDebugProgram.bind();

    for (auto& probe : mProbes)
    {
        renderReflectionProbe(probe);
    }

    gDebugProgram.unbind();
}


void LLHeroProbeManager::initReflectionMaps()
{
    U32 count = LL_MAX_HERO_PROBE_COUNT;

    if ((mTexture.isNull() || mReflectionProbeCount != count || mReset) && LLPipeline::RenderMirrors)
    {
        if (mReset)
        {
            cleanup();
        }

        mReset = false;
        mReflectionProbeCount = count;
        mProbeResolution      = gSavedSettings.getS32("RenderHeroProbeResolution");

        // S24 (2026-09-08, task #316): mRenderTarget/mMipChain are the
        // actual render targets the probe-capture pass writes into - they
        // were previously only invalidated by cleanup() (mReset-gated,
        // above), but mTexture is unconditionally replaced with a NEW
        // DXCubeMapArray instance every time this block runs, including the
        // mReset==false path taken when LLViewerShaderMgr::setShaders()'s
        // OWN internal releaseGLBuffers()/createGLBuffers() cycle (part of
        // update()'s "hacky workaround to fix #3331") reallocates a second
        // time after the mReset-triggered allocation above. Without this,
        // that second reallocation left mRenderTarget/mMipChain pointing at
        // the ORPHANED previous mTexture instance - the capture pass kept
        // rendering into stale targets nothing ever copied into the live
        // texture, which stayed at its cleared/default black. Now tied to
        // "did mTexture just get replaced" instead of "did mReset request
        // a reset", so they always track whichever mTexture instance is
        // actually live.
        mRenderTarget.release();
        mMipChain.clear();

        mTexture = new DXCubeMapArray();

        static LLCachedControl<bool> render_hdr(gSavedSettings, "RenderHDREnabled", true);

        // store mReflectionProbeCount+2 cube maps, final two cube maps are used for render target and radiance map generation source)
        mTexture->allocate(mProbeResolution, 3, mReflectionProbeCount + 2, true, render_hdr);

        // S24 (2026-09-03, task #266/#271): was `log2f((F32)mProbeResolution) - 1.f`
        // (an independently recomputed guess) - see LLReflectionMapManager's
        // identical fix (llreflectionmapmanager.cpp) for the full explanation:
        // DXCubeArrayTexture::create()'s generate_mips=true path requests
        // D3D11's full auto mip chain (MipLevels=0), which allocates one more
        // level than that formula assumed for a power-of-two resolution. Read
        // the texture's REAL allocated mip count instead, now that mTexture
        // exists.
        mMaxProbeLOD = (F32)mTexture->getDXTexture()->getMipLevels() - 1.f; // number of mips - 1

        if (mDefaultProbe.isNull())
        {
            llassert(mProbes.empty()); // default probe MUST be the first probe created
            mDefaultProbe = new LLReflectionMap();
            mProbes.push_back(mDefaultProbe);
        }

        llassert(mProbes[0] == mDefaultProbe);

        // For hero probes, we treat this as the main mirror probe.

        mDefaultProbe->mCubeIndex = 0;
        mDefaultProbe->mCubeArray = mTexture;
        mDefaultProbe->mDistance  = gSavedSettings.getF32("RenderHeroProbeDistance");
        mDefaultProbe->mRadius = 4096.f;
        mDefaultProbe->mProbeIndex = 0;
        touch_default_probe(mDefaultProbe);

        // S24 (2026-08-22): removed a duplicate mProbes.push_back(mDefaultProbe)
        // that lived here - mDefaultProbe is already the sole entry in
        // mProbes via the isNull() branch above on first init; this
        // unconditional second push ran every time this function re-entered
        // (e.g. RenderHeroProbeResolution changes), appending another
        // duplicate reference to the SAME probe each time with no bound,
        // growing mProbes indefinitely over a session. mProbes[0] is the
        // only entry anything reads (renderProbes()/updateProbeFace()), so
        // this was silent bloat rather than a visible symptom, but real.
    }

    if (mVertexBuffer.isNull())
    {
        U32 mask = LLVertexBuffer::MAP_VERTEX;
        LLPointer<LLVertexBuffer> buff = new LLVertexBuffer(mask);
        buff->allocateBuffer(4, 0);

        LLStrider<LLVector3> v;

        buff->getVertexStrider(v);

        v[0] = LLVector3(-1, -1, -1);
        v[1] = LLVector3(1, -1, -1);
        v[2] = LLVector3(-1, 1, -1);
        v[3] = LLVector3(1, 1, -1);

        buff->unmapBuffer();

        mVertexBuffer = buff;
    }
}

void LLHeroProbeManager::cleanup()
{
    mVertexBuffer = nullptr;
    mRenderTarget.release();

    mMipChain.clear();

    mTexture = nullptr;

    mProbes.clear();

    mDefaultProbe = nullptr;
}

void LLHeroProbeManager::doOcclusion()
{
    LLVector4a eye;
    eye.load3(LLViewerCamera::instance().getOrigin().mV);

    for (auto& probe : mProbes)
    {
        if (probe != nullptr)
        {
            probe->doOcclusion(eye);
        }
    }
}

void LLHeroProbeManager::reset()
{
    mReset = true;
}

bool LLHeroProbeManager::registerViewerObject(LLVOVolume* drawablep)
{
    llassert(drawablep != nullptr);

    if (std::find(mHeroVOList.begin(), mHeroVOList.end(), drawablep) == mHeroVOList.end())
    {
        // Probe isn't in our list for consideration.  Add it.
        mHeroVOList.push_back(drawablep);
        return true;
    }

    return false;
}

void LLHeroProbeManager::unregisterViewerObject(LLVOVolume* drawablep)
{
    std::vector<LLPointer<LLVOVolume>>::iterator found_itr = std::find(mHeroVOList.begin(), mHeroVOList.end(), drawablep);
    if (found_itr != mHeroVOList.end())
    {
        mHeroVOList.erase(found_itr);
    }
}
