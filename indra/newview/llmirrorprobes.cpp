/**
 * @file LLMirrorProbes.cpp
 * @brief LLMirrorProbes class implementation
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

#include "llmirrorprobes.h"
#include "llsphereprobes.h"
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

#include "DXDevice.h"
#include "DXReadback.h"

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

LLMirrorProbes::LLMirrorProbes()
{
}

LLMirrorProbes::~LLMirrorProbes()
{
    cleanup();

    mHeroVOList.clear();
    mNearestHero = nullptr;
}

// helper class to seed octree with probes
void LLMirrorProbes::update()
{
    if (!LLPipeline::RenderMirrors || !LLPipeline::sReflectionProbesEnabled || gTeleportDisplay || LLStartUp::getStartupState() < STATE_PRECACHE)
    {
        return;
    }

    llassert(!gCubeSnapshot); // assert a snapshot is not in progress
    if (LLAppViewer::instance()->logoutRequestSent())
    {
        return;
    }

    // Must run before the one-time shader-reload workaround below: setShaders()
    // (re)binds shader/texture-unit state, and if the hero-probe texture is
    // reallocated to a new resolution after that bind, the rebind targets a
    // stale probe texture.
    initReflectionMaps();

    // Part of a hacky workaround to fix #3331.
    // For some reason clearing shaders will cause mirrors to actually work.
    // There's likely some deeper state issue that needs to be resolved.
    // - Geenz 2025-02-25
    //
    // Only re-arms on a genuine RenderMirrors off->on transition or a hero
    // probe resolution change (see requireShaderReinit(), llviewercontrol.cpp) -
    // re-arming on every reset() would make an unrelated settings tweak
    // trigger a full synchronous shader-cache stall.
    if (!mInitialized && LLStartUp::getStartupState() > STATE_PRECACHE)
    {
        // S24: no longer calling LLViewerShaderMgr::clearShaderCache() here - that deletes the
        // ENTIRE on-disk DX bytecode cache (every one of ~150-200 shader programs, not just
        // reflection/mirror-related ones), forcing a real D3DCompile of the whole engine on every
        // single Mirrors enable - the actual cause of "mirrors take an eternity to enable"
        // (GLTF PBR Metallic Roughness's 16 real permutation variants + Deferred Bump were just the
        // most visible/slowest of the ~150-200 shaders needlessly getting swept up in it, not
        // themselves special-cased). buildDXShaderHeader()'s define-reference filter (llhlslshader.cpp)
        // already makes each shader's own on-disk cache entry correctly stable unless that shader's
        // own text actually reads a define this transition changes (HERO_PROBES, only
        // reflectionProbeF.hlsl) - the still-below setShaders() call (full GPU shader-object
        // release+recreate) is kept, since that - not literally deleting cached bytecode - is the
        // more likely actual fix for #3331's "clearing shaders makes mirrors work" symptom. Revert
        // this one line (re-add the clearShaderCache() call) if mirrors regress after this change.

        // S24: handleReflectionProbeDetailChanged() (llviewercontrol.cpp) already queues its own
        // deferred setShaders() for this same RenderMirrors on-transition (the trigger for
        // requireShaderReinit()/mInitialized==false here). If that's still pending, let it do the
        // recompile next frame against the now-cleared cache instead of compiling here too - user-
        // observed: shader compilation visibly ran through twice, full engine recompile back-to-back,
        // when enabling Mirrors. Still call setShaders() directly when nothing is pending (e.g. a
        // resolution change re-arming this via requireShaderReinit() with no accompanying deferred
        // reload queued), so this path stays correct on its own.
        if (!LLViewerShaderMgr::instance()->isDeferredShaderReloadPending())
        {
            LLViewerShaderMgr::instance()->setShaders();
        }
        mInitialized = true;
    }

    static LLCachedControl<bool> render_hdr(gSavedSettings, "RenderHDREnabled", true);

    if (!mCapture.isAllocated())
    {
        // Mirror blur is 1x (not super-sampled) and uses RGBA16F/RGBA8, unlike the sphere capture.
        mCapture.allocate(mProbeResolution, mCubes.getRadianceMipCount(), render_hdr, 1,
                          render_hdr ? GL_RGBA16F : GL_RGBA8);
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

void LLMirrorProbes::renderProbes()
{
    if (!LLPipeline::RenderMirrors || !LLPipeline::sReflectionProbesEnabled || gTeleportDisplay ||
        LLStartUp::getStartupState() < STATE_PRECACHE)
    {
        return;
    }

    static LLCachedControl<S32> sDetail(gSavedSettings, "RenderHeroReflectionProbeDetail", -1);
    static LLCachedControl<S32> sLevel(gSavedSettings, "RenderHeroReflectionProbeLevel", 3);
    static LLCachedControl<S32> sUpdateRate(gSavedSettings, "RenderHeroProbeUpdateRate", 0);

    F32 near_clip = 0.01f;
    if (mNearestHero != nullptr && !mNearestHero->isDead() &&
        !gTeleportDisplay && !gDisconnected && !LLAppViewer::instance()->logoutRequestSent())
    {

        bool radiance_pass = gPipeline.mSphereProbes.setRadiancePass(true);
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

        // No `!mOccluded` gate here deliberately: mNearestHero selection above
        // already does a real frustum visibility check, and the occlusion
        // query's box sits at the mirror's own opaque surface, so it tends to
        // self-occlude. Unlike the main reflection manager's many automatic
        // probes (where occlusion culling is a real optimization), there is
        // only ever one hero probe, already known visible.
        if (!mProbes.empty() && !mProbes[0].isNull())
        {

            bool dynamic = mNearestHero->getReflectionProbeIsDynamic() && sDetail() > 0;
            for (U32 i = 0; i < 6; ++i)
            {
                if ((gFrameCount % rate) == (i % rate))
                { // update 6/rate faces per frame
                    updateProbeFace(mProbes[0], i, dynamic, near_clip);
                }
            }
            generateRadiance(mProbes[0]);
        }

        mRenderingMirror = false;

        gPipeline.mSphereProbes.setRadiancePass(radiance_pass);

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
void LLMirrorProbes::updateProbeFace(LLReflectionMap* probe, U32 face, bool is_dynamic, F32 near_clip)
{

    // hacky hot-swap of camera specific render targets
    LLPipeline::RenderTargetPack* prev_rt = gPipeline.mRT;
    gPipeline.mRT = &gPipeline.mHeroProbeRT;

    probe->update(mCapture.getSuperSampleResolution(), face, is_dynamic, near_clip);

    gPipeline.mRT = prev_rt;

    S32 sourceIdx = mReflectionProbeCount;

    // Unlike the reflectionmap manager, all probes are considered "realtime" for hero probes.
    sourceIdx += 1;

    gDX.setColorWriteMask(true, true);
    LLGLDepthTest depth(GL_FALSE, GL_FALSE);
    LLGLDisable cull(GL_CULL_FACE);
    LLGLDisable blend(GL_BLEND);

    // Blur, downsample and copy this face into the scratch cube (see LLProbeCapture).
    mCapture.resolveFace(gPipeline.mHeroProbeRT.screen, mCubes.radiance(), sourceIdx * 6 + face);
}

// Separate out radiance generation as a separate stage.
// This is to better enable independent control over how we generate radiance vs. having it coupled with processing the final face of the probe.
// Useful when we may not always be rendering a full set of faces of the probe.
void LLMirrorProbes::generateRadiance(LLReflectionMap* probe)
{
    S32 sourceIdx = mReflectionProbeCount;

    // Unlike the reflectionmap manager, all probes are considered "realtime" for hero probes.
    sourceIdx += 1;
    {
        mCapture.getMips()[0].bindTarget();
        static LLStaticHashedString sSourceIdx("sourceIdx");

        {
            // generate radiance map (even if this is not the irradiance map, we need the mip chain for the irradiance map)
            gHeroRadianceGenProgram.bind();
            LLProbeCubeStore::setGeneratorConvention(gHeroRadianceGenProgram);
            mVertexBuffer->setBuffer();

            S32 channel = gHeroRadianceGenProgram.enableTexture(LLShaderMgr::REFLECTION_PROBES, LLTexUnit::TT_CUBE_MAP_ARRAY);
            bindRadiance(channel);
            gHeroRadianceGenProgram.uniform1i(sSourceIdx, sourceIdx);
            gHeroRadianceGenProgram.uniform1f(LLShaderMgr::REFLECTION_PROBE_MAX_LOD, mMaxProbeLOD);
            gHeroRadianceGenProgram.uniform1f(LLShaderMgr::REFLECTION_PROBE_STRENGTH, mHeroProbeStrength);

            U32 res = mCapture.getMips()[0].getWidth();

            // The /4 is deliberate, not a porting bug: reflectionProbeF.hlsl's
            // tapHeroProbe() only samples a quarter of the mips for hero
            // probes (its glossiness gate is matched to this range).
            for (int i = 0; i < (int)mCapture.getMips().size() / 4; ++i)
            {
                static LLStaticHashedString sMipLevel("mipLevel");
                static LLStaticHashedString sWidth("u_width");
                static LLStaticHashedString sStrength("probe_strength");

                gHeroRadianceGenProgram.uniform1f(sMipLevel, (F32)i);
                gHeroRadianceGenProgram.uniform1i(sWidth, mProbeResolution);
                gHeroRadianceGenProgram.uniform1f(sStrength, 1);

                for (int cf = 0; cf < 6; ++cf)
                {  // for each cube face
                    // radianceGenV.hlsl (shared with gRadianceGenProgram) reads
                    // only the `cubeFace` uniform, not a rotation matrix -
                    // must be set explicitly each face, matching
                    // llsphereprobes.cpp's radiance/irradiance loops.
                    static LLStaticHashedString sHeroCubeFace("cubeFace");
                    gHeroRadianceGenProgram.uniform1i(sHeroCubeFace, cf);

                    mVertexBuffer->drawArrays(gDX.TRIANGLE_STRIP, 0, 4);

                    // mCapture.getMips()[0] is bound as render target once before this
                    // whole face/mip loop - no SRV bind of the radiance array needed
                    // for the copy, see DXCubeArrayTexture's header comment.
#ifdef DX_RENDER
                    mCubes.radiance().copySliceFromBoundRenderTarget(i, probe->mCubeIndex * 6 + cf, res, res);
#else
                    glCopyTexSubImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, i, 0, 0, probe->mCubeIndex * 6 + cf, 0, 0, res, res);
#endif
                }

                if (i != (int)mCapture.getMips().size() - 1)
                {
                    res /= 2;
#ifdef DX_RENDER
                    // Negative height is required here - matches
                    // llsphereprobes.cpp's sibling viewport setup.
                    // Do not "normalize" to a positive height; that breaks
                    // hero-probe mirror orientation.
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

        mCapture.getMips()[0].flush();
    }
}

void LLMirrorProbes::updateUniforms()
{
    if (!gPipeline.RenderMirrors)
    {
        return;
    }


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

    mHeroData.heroMipCount = (S32)mCapture.getMips().size();
}

void LLMirrorProbes::renderDebug()
{
    gDebugProgram.bind();

    for (auto& probe : mProbes)
    {
        renderReflectionProbe(probe);
    }

    gDebugProgram.unbind();
}


void LLMirrorProbes::bindRadiance(S32 stage)
{
    gDX.getTexUnit(stage)->bindCubeArraySRV(mCubes.radianceSRV());
}

void LLMirrorProbes::unbindProbeCubes(S32 stage)
{
    gDX.getTexUnit(stage)->unbind(LLTexUnit::TT_CUBE_MAP_ARRAY);
}

void LLMirrorProbes::initReflectionMaps()
{
    U32 count = LL_MAX_HERO_PROBE_COUNT;

    if ((!mCubes.isAllocated() || mReflectionProbeCount != count || mReset) && LLPipeline::RenderMirrors)
    {
        if (mReset)
        {
            cleanup();
        }

        mReset = false;
        mReflectionProbeCount = count;
        mProbeResolution      = gSavedSettings.getS32("RenderHeroProbeResolution");

        // The capture must be rebuilt whenever the radiance storage is reallocated, including the
        // second reallocation setShaders() can trigger on its own.
        mCapture.release();

        static LLCachedControl<bool> render_hdr(gSavedSettings, "RenderHDREnabled", true);

        // radiance storage: mReflectionProbeCount cubes plus two scratch cubes (see LLProbeCubeStore)
        mCubes.allocate(mProbeResolution, mReflectionProbeCount, render_hdr);

        // Real allocated mip count (see LLSphereProbes::initReflectionMaps()).
        mMaxProbeLOD = (F32)mCubes.getRadianceMipCount() - 1.f;

        if (mDefaultProbe.isNull())
        {
            llassert(mProbes.empty()); // default probe MUST be the first probe created
            mDefaultProbe = new LLReflectionMap();
            mProbes.push_back(mDefaultProbe);
        }

        llassert(mProbes[0] == mDefaultProbe);

        // For hero probes, we treat this as the main mirror probe.

        mDefaultProbe->mCubeIndex = 0;
        mDefaultProbe->mDistance  = gSavedSettings.getF32("RenderHeroProbeDistance");
        mDefaultProbe->mRadius = 4096.f;
        mDefaultProbe->mProbeIndex = 0;
        touch_default_probe(mDefaultProbe);

        // Do not push mDefaultProbe onto mProbes again here - it's already
        // the sole entry via the isNull() branch above, and mProbes[0] is
        // the only entry anything reads.
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

void LLMirrorProbes::cleanup()
{
    mVertexBuffer = nullptr;
    mCapture.release();
    mCubes.release();

    mProbes.clear();

    mDefaultProbe = nullptr;
}

void LLMirrorProbes::doOcclusion()
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

void LLMirrorProbes::reset()
{
    mReset = true;
}

bool LLMirrorProbes::registerViewerObject(LLVOVolume* drawablep)
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

void LLMirrorProbes::unregisterViewerObject(LLVOVolume* drawablep)
{
    std::vector<LLPointer<LLVOVolume>>::iterator found_itr = std::find(mHeroVOList.begin(), mHeroVOList.end(), drawablep);
    if (found_itr != mHeroVOList.end())
    {
        mHeroVOList.erase(found_itr);
    }
}
