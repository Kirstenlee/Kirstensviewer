/**
 * @file llreflectionmapmanager.cpp
 * @brief LLReflectionMapManager class implementation
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

#include "llreflectionmapmanager.h"

#include <vector>

#include "llviewercamera.h"
#include "llspatialpartition.h"
#include "llviewerregion.h"

#ifdef DX_RENDER
#include "DXOcclusionQuery.h"
#endif
#include "pipeline.h"
#include "llviewershadermgr.h"
#include "llviewercontrol.h"
#include "llenvironment.h"
#include "llstartup.h"
#include "llviewermenufile.h"
#include "llnotificationsutil.h"

#ifdef DX_RENDER
#include "DXDevice.h"
#endif

// Standard zlib for tinyexr decompression
#include "zlib/zlib.h"

// Disable miniz — we're providing zlib above
#define TINYEXR_USE_MINIZ 0

#define TINYEXR_IMPLEMENTATION
#include "tinyexr/tinyexr.h"

// S24: Fast timers for reflection probe performance tracking
static LLTrace::BlockTimerStatHandle FTM_REFLECTION_PROBE_UPDATE("Reflection Probes");
static LLTrace::BlockTimerStatHandle FTM_REFLECTION_PROBE_GEN("Probe Generation");

LLPointer<LLImageGL> gEXRImage;

void load_exr(const std::string& filename)
{
    // reset reflection maps when previewing a new HDRI
    gPipeline.mReflectionMapManager.reset();
    gPipeline.mReflectionMapManager.initReflectionMaps();

    float* out; // width * height * RGBA
    int width;
    int height;
    const char* err = NULL; // or nullptr in C++11

    int ret =  LoadEXRWithLayer(&out, &width, &height, filename.c_str(), /* layername */ nullptr, &err);
    if (ret == TINYEXR_SUCCESS)
    {
        U32 texName = 0;
        LLImageGL::generateTextures(1, &texName);

        gEXRImage = new LLImageGL(texName, 4, GL_TEXTURE_2D, GL_RGB16F, GL_RGB16F, GL_FLOAT, LLTexUnit::TAM_CLAMP);
        gEXRImage->setHasMipMaps(true);
        gEXRImage->setUseMipMaps(true);
        gEXRImage->setFilteringOption(LLTexUnit::TFO_TRILINEAR);

        gGL.getTexUnit(0)->bind(gEXRImage);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGBA, GL_FLOAT, out);

        LLImageGLMemory::alloc_tex_image(width, height, GL_RGB16F, 1);

        free(out); // release memory of image data

        glGenerateMipmap(GL_TEXTURE_2D);

        gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

    }
    else
    {
        LLSD notif_args;
        notif_args["WHAT"] = filename;
        notif_args["REASON"] = "Unknown";
        if (err)
        {
            notif_args["REASON"] = std::string(err);
            FreeEXRErrorMessage(err); // release memory of error message.
        }
        LLNotificationsUtil::add("CannotLoad", notif_args);
    }
}

void hdri_preview()
{
    LLFilePickerReplyThread::startPicker(
        [](const std::vector<std::string>& filenames, LLFilePicker::ELoadFilter load_filter, LLFilePicker::ESaveFilter save_filter)
        {
            if (LLAppViewer::instance()->quitRequested())
            {
                return;
            }
            if (filenames.size() > 0)
            {
                load_exr(filenames[0]);
            }
        },
        LLFilePicker::FFLOAD_HDRI,
        true);
}

extern bool gCubeSnapshot;
extern bool gTeleportDisplay;

static U32 sUpdateCount = 0;

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

LLReflectionMapManager::LLReflectionMapManager()
{
    mDynamicProbeCount = LL_MAX_REFLECTION_PROBE_COUNT;
    initCubeFree();
}

void LLReflectionMapManager::initCubeFree()
{
    // start at 1 because index 0 is reserved for mDefaultProbe
    for (U32 i = 1; i < mDynamicProbeCount; ++i)
    {
        mCubeFree.push_back(i);
    }
}

struct CompareProbeDistance
{
    LLReflectionMap* mDefaultProbe;

    bool operator()(const LLPointer<LLReflectionMap>& lhs, const LLPointer<LLReflectionMap>& rhs)
    {
        return lhs->mDistance < rhs->mDistance;
    }
};

static F32 update_score(LLReflectionMap* p)
{
    return gFrameTimeSeconds - p->mLastUpdateTime  - p->mDistance*0.1f;
}

// return true if a is higher priority for an update than b
static bool check_priority(LLReflectionMap* a, LLReflectionMap* b)
{
    if (a->mCubeIndex == -1)
    { // not a candidate for updating
        return false;
    }
    else if (b->mCubeIndex == -1)
    { // b is not a candidate for updating, a is higher priority by default
        return true;
    }
    else if (!a->mComplete && !b->mComplete)
    { //neither probe is complete, use distance
        return a->mDistance < b->mDistance;
    }
    else if (a->mComplete && b->mComplete)
    { //both probes are complete, use update_score metric
        return update_score(a) > update_score(b);
    }

    // a or b is not complete,
    if (sUpdateCount % 3 == 0)
    { // every third update, allow complete probes to cut in line in front of non-complete probes to avoid spammy probe generators from deadlocking scheduler (SL-20258))
        return !b->mComplete;
    }

    // prioritize incomplete probe
    return b->mComplete;
}

// helper class to seed octree with probes
void LLReflectionMapManager::update()
{
    LL_RECORD_BLOCK_TIME(FTM_REFLECTION_PROBE_UPDATE);

    // S24 (2026-08-09, task #147 v1): real DX_RENDER capture pipeline now
    // built - this function's gate ("!!! DO NOT REMOVE WITHOUT READING
    // TASK #147 !!!") is gone. What changed: LLCubeMapArray now has a real
    // D3D11 backing (DXCubeArrayTexture, llcubemaparray.h/.cpp), and
    // updateProbeFace() below has real CopySubresourceRegion-based
    // replacements for the three glCopyTexSubImage3D call sites that used
    // to have zero DX_RENDER translation (the actual missing piece - not
    // the crash itself, see DXCubeArrayTexture.h's class comment for why
    // those copies never need the destination bound as an SRV, which is
    // what avoids the "resource bound as both OM output and SRV input"
    // hazard the original gate's crash hit).
    //
    // Still deliberately NOT done here (tracked as #147b and separate
    // follow-ups, not bugs to "fix" reactively if noticed):
    //   - Real per-pixel multi-probe blend (nearest-probe search, box/
    //     sphere influence volumes, neighbor blending) - reflectionProbeF.hlsl
    //     samples a single default probe (array slice 0) for v1, not the
    //     full 900+-line GLSL-derived per-pixel selection math.
    //   - Local-light contribution during capture - inherits
    //     DXPipeline::renderDeferredLighting()'s own "v1, ambient+sun only"
    //     scope (dxpipeline.cpp), not this task's problem.
    //   - dxdrawpoolbump.cpp's/pipeline.cpp's use_legacy_env_map overrides -
    //     left as-is deliberately; revisit once this v1 is visually
    //     confirmed stable, not bundled into the same round.
    if (!LLPipeline::sReflectionProbesEnabled || gTeleportDisplay || LLStartUp::getStartupState() < STATE_PRECACHE)
    {
        return;
    }

    LL_PROFILE_GPU_ZONE("reflection manager update");
    llassert(!gCubeSnapshot); // assert a snapshot is not in progress
    if (LLAppViewer::instance()->logoutRequestSent())
    {
        return;
    }

    if (mPaused && gFrameTimeSeconds > mResumeTime)
    {
        resume();
    }

    mResetFade = llmin((F32)(mResetFade + gFrameIntervalSeconds * 2.f), 1.f);

    {
        U32 probe_count_temp = mDynamicProbeCount;
        if (mRenderReflectionProbeDynamicAllocation > -1)
        {
            if (mRenderReflectionProbeLevel == 0)
            {
                mDynamicProbeCount = 1;
            }
            else if (mRenderReflectionProbeLevel == 1)
            {
                mDynamicProbeCount = (U32)mProbes.size();
            }
            else if (mRenderReflectionProbeLevel == 2)
            {
                mDynamicProbeCount = llmax((U32)mProbes.size(), 128);
            }
            else
            {
                mDynamicProbeCount = 256;
            }

            if (mRenderReflectionProbeDynamicAllocation > 1)
            {
                // Round mDynamicProbeCount to the nearest increment of 16
                mDynamicProbeCount = ((mDynamicProbeCount + mRenderReflectionProbeDynamicAllocation / 2) / mRenderReflectionProbeDynamicAllocation) * 16;
                mDynamicProbeCount = llclamp(mDynamicProbeCount, 1, mRenderReflectionProbeCount);
            }
            else
    {
                mDynamicProbeCount = llclamp(mDynamicProbeCount + mRenderReflectionProbeDynamicAllocation, 1, mRenderReflectionProbeCount);
            }
        }
        else
        {
            mDynamicProbeCount = mRenderReflectionProbeCount;
        }

        mDynamicProbeCount = llmin(mDynamicProbeCount, LL_MAX_REFLECTION_PROBE_COUNT);

        if (mDynamicProbeCount != probe_count_temp)
            mResetFade = 1.f;
    }

    initReflectionMaps();

    static LLCachedControl<bool> render_hdr(gSavedSettings, "RenderHDREnabled", true);

    if (!mRenderTarget.isComplete())
    {
        U32 color_fmt = render_hdr ? GL_R11F_G11F_B10F : GL_RGB8;
        U32 targetRes = mProbeResolution * 4; // super sample
        mRenderTarget.allocate(targetRes, targetRes, color_fmt, true);
    }

    if (mMipChain.empty())
    {
        U32 res = mProbeResolution;
        U32 count = (U32)(log2((F32)res) + 0.5f);

        mMipChain.resize(count);
        for (U32 i = 0; i < count; ++i)
        {
            // S24 (2026-08-10, task #147/#184 follow-up): DX_RENDER-only
            // format override. GL_R11F_G11F_B10F/GL_RGB8 map to D3D11's
            // R11G11B10_FLOAT/(some 3-channel-adjacent format), but
            // DXCubeArrayTexture::create() (mTexture/mIrradianceMaps, this
            // mip chain's real copy destination) deliberately uses
            // R16G16B16A16_FLOAT/R8G8B8A8_UNORM instead - its own header
            // comment explains why (R11G11B10_FLOAT's GenerateMips support
            // isn't guaranteed at feature-level 11 baseline). Two genuinely
            // different, non-castable formats - a real, confirmed root
            // cause of "Cannot invoke CopySubresourceRegion when the
            // Formats...are not the same or castable" (D3D11 debug layer,
            // "Reflection Mip Shader"/"Irradiance Gen Shader"/"Radiance Gen
            // Shader" contexts, tens of thousands of occurrences per
            // session). GL_RGBA16F/GL_RGBA are the same format request
            // already used and proven working under DX_RENDER for
            // mRT->screen/mRT->deferredLight (pipeline.cpp's own
            // `hdr ? GL_RGBA16F : GL_RGBA` pattern) - reused here instead of
            // inventing a new mapping. GL build behavior is completely
            // unchanged (GL_R11F_G11F_B10F/GL_RGB8 kept for GL, matching
            // upstream exactly).
#ifdef DX_RENDER
            mMipChain[i].allocate(res, res, render_hdr ? GL_RGBA16F : GL_RGBA);
#else
            mMipChain[i].allocate(res, res, render_hdr ? GL_R11F_G11F_B10F : GL_RGB8);
#endif
            res /= 2;
        }
    }

    llassert(mProbes[0] == mDefaultProbe);

    LLVector4a camera_pos;
    camera_pos.load3(LLViewerCamera::instance().getOrigin().mV);

    // process kill list
    for (auto& probe : mKillList)
    {
        auto const & iter = std::find(mProbes.begin(), mProbes.end(), probe);
        if (iter != mProbes.end())
        {
            deleteProbe((U32)(iter - mProbes.begin()));
        }
    }

    mKillList.clear();

    // process create list
    for (auto& probe : mCreateList)
    {
        mProbes.push_back(probe);
    }

    mCreateList.clear();

    if (mProbes.empty())
    {
        return;
    }


    bool did_update = false;

    bool realtime = mRenderReflectionProbeDetail >= (S32)LLReflectionMapManager::DetailLevel::REALTIME;

    LLReflectionMap* closestDynamic = nullptr;

    LLReflectionMap* oldestProbe = nullptr;
    LLReflectionMap* oldestOccluded = nullptr;

    if (mUpdatingProbe != nullptr)
    {
        did_update = true;
        doProbeUpdate();
    }

    // update distance to camera for all probes
    std::sort(mProbes.begin()+1, mProbes.end(), CompareProbeDistance());
    llassert(mProbes[0] == mDefaultProbe);
    llassert(mProbes[0]->mCubeArray == mTexture);
    llassert(mProbes[0]->mCubeIndex == 0);

    // make sure we're assigning cube slots to the closest probes

    // first free any cube indices for distant probes
    for (U32 i = mReflectionProbeCount; i < mProbes.size(); ++i)
    {
        LLReflectionMap* probe = mProbes[i];
        llassert(probe != nullptr);

        if (probe && probe->mCubeIndex != -1 && mUpdatingProbe != probe)
        { // free this index
            mCubeFree.push_back(probe->mCubeIndex);

            probe->mCubeArray = nullptr;
            probe->mCubeIndex = -1;
            probe->mComplete = false;
            probe->mFadeIn = 0;
        }
    }

    // next distribute the free indices
    U32 count = llmin(mReflectionProbeCount, (U32)mProbes.size());

    for (U32 i = 1; i < count && !mCubeFree.empty(); ++i)
    {
        // find the closest probe that needs a cube index
        LLReflectionMap* probe = mProbes[i];

        if (probe->mCubeIndex == -1)
        {
            S32 idx = allocateCubeIndex();
            llassert(idx > 0); //if we're still in this loop, mCubeFree should not be empty and allocateCubeIndex should be returning good indices
            probe->mCubeArray = mTexture;
            probe->mCubeIndex = idx;
        }
    }

    mResetFade = llmin((F32)(mResetFade + gFrameIntervalSeconds * 2.f), 1.f);

    for (unsigned int i = 0; i < mProbes.size(); ++i)
    {
        LLReflectionMap* probe = mProbes[i];
        if (probe->getNumRefs() == 1)
        { // no references held outside manager, delete this probe
            deleteProbe(i);
            --i;
            continue;
        }

        if (probe != mDefaultProbe &&
            (!probe->isRelevant() || mPaused))
        { // skip irrelevant probes (or all non-default probes if paused)
            continue;
        }

        LLVector4a d;

        if (probe != mDefaultProbe)
        {
            if (probe->mViewerObject) //make sure probes track the viewer objects they are attached to
            {
                probe->mOrigin.load3(probe->mViewerObject->getPositionAgent().mV);
            }
            d.setSub(camera_pos, probe->mOrigin);
            probe->mDistance = d.getLength3().getF32() - probe->mRadius;
        }
        else if (probe->mComplete)
        {
            // make default probe have a distance of 64m for the purposes of prioritization (if it's already been generated once)
            probe->mDistance = 64.f;
        }
        else
        {
            probe->mDistance = -4096.f; //boost priority of default probe when it's not complete
        }

        if (probe->mComplete)
        {
            probe->autoAdjustOrigin();
            probe->mFadeIn = llmin((F32) (probe->mFadeIn + gFrameIntervalSeconds), 1.f);
        }
        if (probe->mOccluded && probe->mComplete)
        {
            if (oldestOccluded == nullptr)
            {
                oldestOccluded = probe;
            }
            else if (probe->mLastUpdateTime < oldestOccluded->mLastUpdateTime)
            {
                oldestOccluded = probe;
            }
        }
        else
        {
            if (!did_update &&
                i < mReflectionProbeCount &&
                (oldestProbe == nullptr ||
                    check_priority(probe, oldestProbe)))
            {
               oldestProbe = probe;
            }
        }

        if (realtime &&
            closestDynamic == nullptr &&
            probe->mCubeIndex != -1 &&
            probe->getIsDynamic())
        {
            closestDynamic = probe;
        }

        if (mRenderReflectionProbeLevel == 0)
        {
            // only update default probe when coverage is set to none
            llassert(probe == mDefaultProbe);
            break;
        }
    }

    if (realtime && closestDynamic != nullptr)
    {
        // update the closest dynamic probe realtime
        // should do a full irradiance pass on "odd" frames and a radiance pass on "even" frames
        closestDynamic->autoAdjustOrigin();

        // store and override the value of "isRadiancePass" -- parts of the render pipe rely on "isRadiancePass" to set
        // lighting values etc
        bool radiance_pass = isRadiancePass();
        mRadiancePass = mRealtimeRadiancePass;
        for (U32 i = 0; i < 6; ++i)
        {
            updateProbeFace(closestDynamic, i);
        }
        mRealtimeRadiancePass = !mRealtimeRadiancePass;

        // restore "isRadiancePass"
        mRadiancePass = radiance_pass;
    }

    static LLCachedControl<F32> sUpdatePeriod(gSavedSettings, "RenderDefaultProbeUpdatePeriod", 2.f);
    if ((gFrameTimeSeconds - mDefaultProbe->mLastUpdateTime) < sUpdatePeriod)
    {
        if (mRenderReflectionProbeLevel == 0)
        { // when probes are disabled don't update the default probe more often than the prescribed update period
            oldestProbe = nullptr;
        }
    }
    else if (mRenderReflectionProbeLevel > 0)
    { // when probes are enabled don't update the default probe less often than the prescribed update period
      oldestProbe = mDefaultProbe;
    }

    // switch to updating the next oldest probe
    if (!did_update && oldestProbe != nullptr)
    {
        LLReflectionMap* probe = oldestProbe;
        llassert(probe->mCubeIndex != -1);

        probe->autoAdjustOrigin();

        sUpdateCount++;
        mUpdatingProbe = probe;
        doProbeUpdate();
    }

    if (oldestOccluded)
    {
        // as far as this occluded probe is concerned, an origin/radius update is as good as a full update
        oldestOccluded->autoAdjustOrigin();
        oldestOccluded->mLastUpdateTime = gFrameTimeSeconds;
    }
}

void LLReflectionMapManager::refreshSettings()
{
    mRenderReflectionProbeDetail = gSavedSettings.getS32("RenderReflectionProbeDetail");
    mRenderReflectionProbeLevel = gSavedSettings.getS32("RenderReflectionProbeLevel");
    mRenderReflectionProbeCount = gSavedSettings.getU32("RenderReflectionProbeCount");
    mRenderReflectionProbeDynamicAllocation = gSavedSettings.getS32("RenderReflectionProbeDynamicAllocation");
    cleanupQueryPool();
}

LLReflectionMap* LLReflectionMapManager::addProbe(LLSpatialGroup* group)
{
    if (gGLManager.mGLVersion < 4.05f || !LLPipeline::sReflectionProbesEnabled)
    {
        return nullptr;
    }

    LLReflectionMap* probe = new LLReflectionMap();
    probe->mGroup = group;

    if (mDefaultProbe.isNull())
    {  //safety check to make sure default probe is always first probe added
        mDefaultProbe = new LLReflectionMap();
        mProbes.push_back(mDefaultProbe);
    }

    llassert(mProbes[0] == mDefaultProbe);

    if (group)
    {
        probe->mOrigin = group->getOctreeNode()->getCenter();
    }

    if (gCubeSnapshot)
    { //snapshot is in progress, mProbes is being iterated over, defer insertion until next update
        mCreateList.push_back(probe);
    }
    else
    {
        mProbes.push_back(probe);
    }

    return probe;
}

U32 LLReflectionMapManager::probeCount()
{
    return mDynamicProbeCount;
}

U32 LLReflectionMapManager::probeMemory()
{
    return (mDynamicProbeCount * 6 * (mProbeResolution * mProbeResolution) * 4) / 1024 / 1024 + (mDynamicProbeCount * 6 * (LL_IRRADIANCE_MAP_RESOLUTION * LL_IRRADIANCE_MAP_RESOLUTION) * 4) / 1024 / 1024;
}

GLuint LLReflectionMapManager::allocateQuery()
{
    if (mQueryPool.empty())
    {
        GLuint query = 0;
        // S24 (2026-08-19, task #182 CTD fix): raw glGenQueries() is a
        // null extension-function-pointer crash under DX_RENDER - see
        // LLReflectionMap::doOcclusion()'s comment for the full story.
        // This is the allocation side of the same query pool that
        // function uses, so it needs the same DXOcclusionQuery routing.
#ifdef DX_RENDER
        DXOcclusionQuery::genQueries(1, &query);
#else
        glGenQueries(1, &query);
#endif
        return query;
    }

    GLuint query = mQueryPool.front();
    mQueryPool.pop_front();
    return query;
}

void LLReflectionMapManager::recycleQuery(GLuint query)
{
    mQueryPool.push_back(query);
}

struct CompareProbeDepth
{
    bool operator()(const LLReflectionMap* lhs, const LLReflectionMap* rhs)
    {
        return lhs->mMinDepth < rhs->mMinDepth;
    }
};

void LLReflectionMapManager::getReflectionMaps(std::vector<LLReflectionMap*>& maps)
{

    LLMatrix4a modelview;
    modelview.loadu(gGLModelView);
    LLVector4a oa; // scratch space for transformed origin

    U32 count = 0;
    U32 lastIdx = 0;
    for (U32 i = 0; count < maps.size() && i < mProbes.size(); ++i)
    {
        mProbes[i]->mLastBindTime = gFrameTimeSeconds; // something wants to use this probe, indicate it's been requested
        if (mProbes[i]->mCubeIndex != -1)
        {
            if (!mProbes[i]->mOccluded && mProbes[i]->mComplete)
            {
                maps[count++] = mProbes[i];
                modelview.affineTransform(mProbes[i]->mOrigin, oa);
                mProbes[i]->mMinDepth = -oa.getF32ptr()[2] - mProbes[i]->mRadius;
                mProbes[i]->mMaxDepth = -oa.getF32ptr()[2] + mProbes[i]->mRadius;
            }
        }
        else
        {
            mProbes[i]->mProbeIndex = -1;
        }
        lastIdx = i;
    }

    // set remaining probe indices to -1
    for (U32 i = lastIdx+1; i < mProbes.size(); ++i)
    {
        mProbes[i]->mProbeIndex = -1;
    }

    if (count > 1)
    {
        std::sort(maps.begin(), maps.begin() + count, CompareProbeDepth());
    }

    for (U32 i = 0; i < count; ++i)
    {
        maps[i]->mProbeIndex = i;
    }

    // null terminate list
    if (count < maps.size())
    {
        maps[count] = nullptr;
    }
}

LLReflectionMap* LLReflectionMapManager::registerSpatialGroup(LLSpatialGroup* group)
{
    if (!group)
    {
        return nullptr;
    }
    LLSpatialPartition* part = group->getSpatialPartition();
    if (!part || part->mPartitionType != LLViewerRegion::PARTITION_VOLUME)
    {
        return nullptr;
    }
    OctreeNode* node = group->getOctreeNode();
    F32 size = node->getSize().getF32ptr()[0];
    if (size < 15.f || size > 17.f)
    {
        return nullptr;
    }
    return addProbe(group);
}

LLReflectionMap* LLReflectionMapManager::registerViewerObject(LLViewerObject* vobj)
{
    if (!LLPipeline::sReflectionProbesEnabled)
    {
        return nullptr;
    }

    llassert(vobj != nullptr);

    LLReflectionMap* probe = new LLReflectionMap();
    probe->mViewerObject = vobj;
    probe->mOrigin.load3(vobj->getPositionAgent().mV);

    if (gCubeSnapshot)
    { //snapshot is in progress, mProbes is being iterated over, defer insertion until next update
        mCreateList.push_back(probe);
    }
    else
    {
        mProbes.push_back(probe);
    }

    return probe;
}

S32 LLReflectionMapManager::allocateCubeIndex()
{
    if (!mCubeFree.empty())
    {
        S32 ret = mCubeFree.front();
        mCubeFree.pop_front();
        return ret;
    }

    return -1;
}

void LLReflectionMapManager::deleteProbe(U32 i)
{
    LLReflectionMap* probe = mProbes[i];

    llassert(probe != mDefaultProbe);

    if (probe->mCubeIndex != -1)
    { // mark the cube index used by this probe as being free
        mCubeFree.push_back(probe->mCubeIndex);
    }
    if (mUpdatingProbe == probe)
    {
        mUpdatingProbe = nullptr;
        mUpdatingFace = 0;
    }

    // remove from any Neighbors lists
    for (auto& other : probe->mNeighbors)
    {
        auto const & iter = std::find(other->mNeighbors.begin(), other->mNeighbors.end(), probe);
        llassert(iter != other->mNeighbors.end());
        other->mNeighbors.erase(iter);
    }

    // Probes are distance sorted, order matters.
    mProbes.erase(mProbes.begin() + i);
}


void LLReflectionMapManager::doProbeUpdate()
{
    LL_RECORD_BLOCK_TIME(FTM_REFLECTION_PROBE_GEN);
    llassert(mUpdatingProbe != nullptr);

    updateProbeFace(mUpdatingProbe, mUpdatingFace);

    bool debug_updates = gPipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_PROBE_UPDATES) && mUpdatingProbe->mViewerObject;

    if (++mUpdatingFace == 6)
    {
        if (debug_updates)
        {
            mUpdatingProbe->mViewerObject->setDebugText(llformat("%.1f", (F32)gFrameTimeSeconds), LLColor4(1, 1, 1, 1));
        }
        updateNeighbors(mUpdatingProbe);
        mUpdatingFace = 0;
        if (isRadiancePass())
        {
            mUpdatingProbe->mComplete = true;
            mUpdatingProbe = nullptr;
            mRadiancePass = false;
        }
        else
        {
            mRadiancePass = true;
        }
    }
    else if (debug_updates)
    {
        mUpdatingProbe->mViewerObject->setDebugText(llformat("%.1f", (F32)gFrameTimeSeconds), LLColor4(1, 1, 0, 1));
    }
}

// Do the reflection map update render passes.
#ifdef DX_RENDER
namespace
{
    // S24 (2026-08-23, LIVE ORIENTATION TUNER): per-face swap/negate knobs
    // for radianceGenV.hlsl/irradianceGenV.hlsl's dbgSwap/dbgSignA/dbgSignB
    // uniforms, live-editable from KVTweaks (Advanced -> Reflections tab,
    // "Cubemap Face Orientation (Debug)"). Defaults reproduce the proven
    // baseline formula exactly (see those shaders' comments) - this exists
    // purely so task #194's remaining +X/-X defect can be explored live, one
    // checkbox at a time, instead of one full rebuild per hypothesis.
    struct S24CubeOrientDebug { bool swap; bool negA; bool negB; };

    S24CubeOrientDebug s24GetCubeOrientDebug(S32 face)
    {
        static LLCachedControl<bool> swap0(gSavedSettings, "S24CubeOrientSwap0", false);
        static LLCachedControl<bool> negA0(gSavedSettings, "S24CubeOrientNegA0", true);
        static LLCachedControl<bool> negB0(gSavedSettings, "S24CubeOrientNegB0", false);
        static LLCachedControl<bool> swap1(gSavedSettings, "S24CubeOrientSwap1", false);
        static LLCachedControl<bool> negA1(gSavedSettings, "S24CubeOrientNegA1", false);
        static LLCachedControl<bool> negB1(gSavedSettings, "S24CubeOrientNegB1", false);
        static LLCachedControl<bool> swap2(gSavedSettings, "S24CubeOrientSwap2", false);
        static LLCachedControl<bool> negA2(gSavedSettings, "S24CubeOrientNegA2", false);
        static LLCachedControl<bool> negB2(gSavedSettings, "S24CubeOrientNegB2", true);
        static LLCachedControl<bool> swap3(gSavedSettings, "S24CubeOrientSwap3", false);
        static LLCachedControl<bool> negA3(gSavedSettings, "S24CubeOrientNegA3", false);
        static LLCachedControl<bool> negB3(gSavedSettings, "S24CubeOrientNegB3", false);
        static LLCachedControl<bool> swap4(gSavedSettings, "S24CubeOrientSwap4", false);
        static LLCachedControl<bool> negA4(gSavedSettings, "S24CubeOrientNegA4", false);
        static LLCachedControl<bool> negB4(gSavedSettings, "S24CubeOrientNegB4", false);
        static LLCachedControl<bool> swap5(gSavedSettings, "S24CubeOrientSwap5", false);
        static LLCachedControl<bool> negA5(gSavedSettings, "S24CubeOrientNegA5", true);
        static LLCachedControl<bool> negB5(gSavedSettings, "S24CubeOrientNegB5", false);

        switch (face)
        {
        case 0:  return { swap0, negA0, negB0 };
        case 1:  return { swap1, negA1, negB1 };
        case 2:  return { swap2, negA2, negB2 };
        case 3:  return { swap3, negA3, negB3 };
        case 4:  return { swap4, negA4, negB4 };
        default: return { swap5, negA5, negB5 };
        }
    }
}
#endif

// For every 12 calls of this function, one complete reflection probe radiance map and irradiance map is generated
// First six passes render the scene with direct lighting only into a scratch space cube map at the end of the cube map array and generate
// a simple mip chain (not convolution filter).
// At the end of these passes, an irradiance map is generated for this probe and placed into the irradiance cube map array at the index for this probe
// The next six passes render the scene with both radiance and irradiance into the same scratch space cube map and generate a simple mip chain.
// At the end of these passes, a radiance map is generated for this probe and placed into the radiance cube map array at the index for this probe.
// In effect this simulates single-bounce lighting.
void LLReflectionMapManager::updateProbeFace(LLReflectionMap* probe, U32 face)
{
    LL_PROFILE_GPU_ZONE("probe update");
    // hacky hot-swap of camera specific render targets
    gPipeline.mRT = &gPipeline.mAuxillaryRT;

    mLightScale = 1.f;
    static LLCachedControl<F32> max_local_light_ambiance(gSavedSettings, "RenderReflectionProbeMaxLocalLightAmbiance", 8.f);
    if (!isRadiancePass() && probe->getAmbiance() > max_local_light_ambiance)
    {
        mLightScale = max_local_light_ambiance / probe->getAmbiance();
    }

    if (probe == mDefaultProbe)
    {
        touch_default_probe(probe);

        gPipeline.pushRenderTypeMask();

        //only render sky, water, terrain, and clouds
        gPipeline.andRenderTypeMask(LLPipeline::RENDER_TYPE_SKY, LLPipeline::RENDER_TYPE_WL_SKY,
            LLPipeline::RENDER_TYPE_WATER, LLPipeline::RENDER_TYPE_VOIDWATER, LLPipeline::RENDER_TYPE_CLOUDS, LLPipeline::RENDER_TYPE_TERRAIN, LLPipeline::END_RENDER_TYPES);

        probe->update(mRenderTarget.getWidth(), face);

        gPipeline.popRenderTypeMask();
    }
    else
    {
        llassert(mRenderReflectionProbeLevel > 0); // should never update a probe that's not the default probe if reflection coverage is none
        probe->update(mRenderTarget.getWidth(), face);
    }

    gPipeline.mRT = &gPipeline.mMainRT;

    S32 sourceIdx = mReflectionProbeCount;

    if (probe != mUpdatingProbe)
    { // this is the "realtime" probe that's updating every frame, use the secondary scratch space channel
        sourceIdx += 1;
    }

    gGL.setColorMask(true, true);
    LLGLDepthTest depth(GL_FALSE, GL_FALSE);
    LLGLDisable cull(GL_CULL_FACE);
    LLGLDisable blend(GL_BLEND);

    // downsample to placeholder map
    {
        gGL.matrixMode(gGL.MM_MODELVIEW);
        gGL.pushMatrix();
        gGL.loadIdentity();

        gGL.matrixMode(gGL.MM_PROJECTION);
        gGL.pushMatrix();
        gGL.loadIdentity();

        gGL.flush();
        U32 res = mProbeResolution * 2;

        static LLStaticHashedString resScale("resScale");
        static LLStaticHashedString direction("direction");
        static LLStaticHashedString znear("znear");
        static LLStaticHashedString zfar("zfar");

        LLRenderTarget* screen_rt = &gPipeline.mAuxillaryRT.screen;

        // S24 (task #194, 2026-08-14): a temporary face-ID color diagnostic
        // lived here (solid, distinct color per cube face instead of real
        // captured content). Answered its question - see
        // [[project_dxrender_stage8_status]]/memorygraph for the full
        // result - and was removed per the diagnostic-lifecycle convention.
        // Conclusion: face SELECTION is correct and stable for every
        // cardinal/vertical direction (matches the natural X=E/W, Y=N/S,
        // Z=up/down expectation) except an isolated west/south flicker -
        // the "wrong content"/upside-down symptoms chased most of this
        // session are therefore a WITHIN-FACE content-orientation problem
        // (the right face is chosen, its own captured image is rotated/
        // mirrored wrong), not a sample-direction/slot-selection problem -
        // don't re-attempt v.x/v.y/v.z sign flips in tapRefMap()/
        // tapIrradianceMap() without new evidence contradicting this.

        // S24 (2026-08-22): the A0 orientation-marker diagnostic that lived
        // here did its job - confirmed the capture->display pipeline is
        // live end-to-end (markers reached the screen on a nearby glass
        // cube), and separately surfaced that the glass FLOOR shows no
        // local-probe content at all (marker or otherwise) - a probe
        // selection/weighting issue independent of face orientation, not
        // yet resolved. Removed per diagnostic-lifecycle convention now
        // that this file's own look/up-vector math is being rederived
        // directly (see cubeSnapshot() and DXCubeMapFaces::sUpVecs).

        // perform a gaussian blur on the super sampled render before downsampling
        {
            gGaussianProgram.bind();
            gGaussianProgram.uniform1f(resScale, 1.f / (mProbeResolution * 2));
            S32 diffuseChannel = gGaussianProgram.enableTexture(LLShaderMgr::DEFERRED_DIFFUSE, LLTexUnit::TT_TEXTURE);

            // horizontal
            gGaussianProgram.uniform2f(direction, 1.f, 0.f);
            gGL.getTexUnit(diffuseChannel)->bind(screen_rt);
            mRenderTarget.bindTarget();
            gPipeline.mScreenTriangleVB->setBuffer();
            gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
            mRenderTarget.flush();

            // vertical
            gGaussianProgram.uniform2f(direction, 0.f, 1.f);
            gGL.getTexUnit(diffuseChannel)->bind(&mRenderTarget);
            screen_rt->bindTarget();
            gPipeline.mScreenTriangleVB->setBuffer();
            gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
            screen_rt->flush();
        }


        S32 mips = (S32)(log2((F32)mProbeResolution) + 0.5f);

        gReflectionMipProgram.bind();
        S32 diffuseChannel = gReflectionMipProgram.enableTexture(LLShaderMgr::DEFERRED_DIFFUSE, LLTexUnit::TT_TEXTURE);

        for (int i = 0; i < mMipChain.size(); ++i)
        {
            LL_PROFILE_GPU_ZONE("probe mip");
            mMipChain[i].bindTarget();
            if (i == 0)
            {
                gGL.getTexUnit(diffuseChannel)->bind(screen_rt);
            }
            else
            {
                gGL.getTexUnit(diffuseChannel)->bind(&(mMipChain[i - 1]));
            }


            gReflectionMipProgram.uniform1f(resScale, 1.f/(mProbeResolution*2));

            gPipeline.mScreenTriangleVB->setBuffer();
            gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

            res /= 2;

            GLint mip = i - (static_cast<GLint>(mMipChain.size()) - mips);

            if (mip >= 0)
            {
                LL_PROFILE_GPU_ZONE("probe mip copy");
#ifdef DX_RENDER
                // S24 (2026-08-09, task #147 step 5): glCopyTexSubImage3D
                // has no DX_RENDER translation (never existed anywhere in
                // this codebase before now) - replaced with a real
                // CopySubresourceRegion-based equivalent. Deliberately does
                // NOT bind mTexture as anything (no SRV, no texture-unit
                // bind) - mMipChain[i], bound as the render target just
                // above (bindTarget() at the top of this loop iteration),
                // is resolved directly via OMGetRenderTargets() inside
                // copySliceFromBoundRenderTarget(). This is what avoids the
                // "resource bound as both OM output and SRV input"
                // hazard class by construction - the exact crash class
                // that made this whole function unreachable under
                // DX_RENDER until this task. See DXCubeArrayTexture.h's
                // own class comment for the full reasoning. mMipChain[i] is
                // already a dedicated, correctly-sized-for-this-iteration
                // target (unlike the radiance/irradiance loops below), but
                // passing the explicit size anyway for consistency/safety
                // now that the API supports it - matches GL's own explicit
                // res,res args to glCopyTexSubImage3D() just below.
                mTexture->getDXTexture()->copySliceFromBoundRenderTarget(mip, sourceIdx * 6 + face, (UINT)mMipChain[i].getWidth(), (UINT)mMipChain[i].getHeight());
#else
                mTexture->bind(0);
                glCopyTexSubImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, mip, 0, 0, sourceIdx * 6 + face, 0, 0, res, res);
                mTexture->unbind();
#endif
            }
            mMipChain[i].flush();
        }

        gGL.popMatrix();
        gGL.matrixMode(gGL.MM_MODELVIEW);
        gGL.popMatrix();

        gGL.getTexUnit(diffuseChannel)->unbind(LLTexUnit::TT_TEXTURE);
        gReflectionMipProgram.unbind();
    }

    if (face == 5)
    {
        mMipChain[0].bindTarget();
        // S24 (2026-08-13, task #163 follow-up, REVERTED 2026-08-14): the
        // flipped-viewport override that used to be here (task #147/#184,
        // 2026-08-10) was removed on the theory that this whole Y-flip was
        // simply wrong. Confirmed wrong theory: reflections are still
        // reported upside down with it removed (task #194 room test, same
        // day as the other 3 matching sites' restoration). Re-added here,
        // matching DXContext::setViewport(...,true)'s exact negative-height
        // flip semantics for consistency with those other 3 sites.
#ifdef DX_RENDER
        {
            D3D11_VIEWPORT vp = {};
            vp.TopLeftX = 0.0f;
            vp.TopLeftY = (float)mMipChain[0].getHeight();
            vp.Width = (float)mMipChain[0].getWidth();
            vp.Height = -(float)mMipChain[0].getHeight();
            vp.MinDepth = 0.0f;
            vp.MaxDepth = 1.0f;
            gDXDevice.getContext()->RSSetViewports(1, &vp);
        }
#endif
        static LLStaticHashedString sSourceIdx("sourceIdx");

        if (isRadiancePass())
        {
            //generate radiance map (even if this is not the irradiance map, we need the mip chain for the irradiance map)
            gRadianceGenProgram.bind();
            mVertexBuffer->setBuffer();

            S32 channel = gRadianceGenProgram.enableTexture(LLShaderMgr::REFLECTION_PROBES, LLTexUnit::TT_CUBE_MAP_ARRAY);
            mTexture->bind(channel);
            gRadianceGenProgram.uniform1i(sSourceIdx, sourceIdx);
            gRadianceGenProgram.uniform1f(LLShaderMgr::REFLECTION_PROBE_MAX_LOD, mMaxProbeLOD);
            gRadianceGenProgram.uniform1f(LLShaderMgr::REFLECTION_PROBE_STRENGTH, 1.f);

            U32 res = mMipChain[0].getWidth();

            for (int i = 0; i < mMipChain.size(); ++i)
            {
                LL_PROFILE_GPU_ZONE("probe radiance gen");
                static LLStaticHashedString sMipLevel("mipLevel");
                static LLStaticHashedString sRoughness("roughness");
                static LLStaticHashedString sWidth("u_width");

                gRadianceGenProgram.uniform1f(sRoughness, (F32)i / (F32)(mMipChain.size() - 1));
                gRadianceGenProgram.uniform1f(sMipLevel, (GLfloat)i);
                gRadianceGenProgram.uniform1i(sWidth, mProbeResolution);

                for (int cf = 0; cf < 6; ++cf)
                { // for each cube face
#ifdef DX_RENDER
                    // S24 (2026-08-22, plan item A - CLOSED-FORM REWRITE):
                    // replaces the whole LLCoordFrame::lookAt() +
                    // getDirectXRotation() + per-face flipCol/fixHandedness/
                    // poleRotate patch history (task #194, 47+ "round N"
                    // comments; getDirectXRotation() itself fixed the
                    // original front/back symptom but the per-face patches
                    // on top of it never converged - 2-side, 4-side, and
                    // pole-only combinations were each tried and only
                    // partly right) with Direct3D's own documented per-face
                    // cubemap addressing formula, hardcoded directly in
                    // radianceGenV.hlsl and driven by nothing but this face
                    // index. See that shader's header comment for the full
                    // derivation of why a generic rotation-matrix approach
                    // could never be made to match hardware addressing by
                    // hand-tuning individual faces. GL path is completely
                    // untouched (still frame.lookAt()+getOpenGLRotation()
                    // below).
                    static LLStaticHashedString sCubeFace("cubeFace");
                    gRadianceGenProgram.uniform1i(sCubeFace, cf);

                    // S24 (2026-08-23, LIVE ORIENTATION TUNER): see the
                    // s24GetCubeOrientDebug() definition above this function.
                    static LLStaticHashedString sDbgSwap("dbgSwap");
                    static LLStaticHashedString sDbgSignA("dbgSignA");
                    static LLStaticHashedString sDbgSignB("dbgSignB");
                    S24CubeOrientDebug orientDbg = s24GetCubeOrientDebug(cf);
                    gRadianceGenProgram.uniform1i(sDbgSwap, orientDbg.swap ? 1 : 0);
                    gRadianceGenProgram.uniform1f(sDbgSignA, orientDbg.negA ? -1.f : 1.f);
                    gRadianceGenProgram.uniform1f(sDbgSignB, orientDbg.negB ? -1.f : 1.f);
#else
                    LLCoordFrame frame;
                    frame.lookAt(LLVector3(0, 0, 0), LLCubeMapArray::sClipToCubeLookVecs[cf], LLCubeMapArray::sClipToCubeUpVecs[cf]);
                    F32 mat[16];
                    frame.getOpenGLRotation(mat);
                    gGL.loadMatrix(mat);
#endif

                    mVertexBuffer->drawArrays(gGL.TRIANGLE_STRIP, 0, 4);

                    // S24 (2026-08-09, task #147 step 5): see the mip-copy
                    // block above for the full reasoning - mMipChain[0]
                    // (bound as render target once, before this whole
                    // face/mip loop, at mMipChain[0].bindTarget() a few
                    // lines up) is resolved via OMGetRenderTargets(), no
                    // SRV bind of mTexture needed for the copy itself.
                    //
                    // S24 (2026-08-10, task #147/#184 follow-up): mMipChain[0]
                    // stays bound as render target for EVERY iteration of
                    // this loop (never rebound per-i, unlike the reflection-
                    // mip loop above) - only a shrinking top-left `res x res`
                    // sub-region of it is actually rendered into via
                    // RSSetViewports each iteration (see the glViewport/
                    // RSSetViewports call a few lines below, and GL's own
                    // explicit res,res args to glCopyTexSubImage3D() right
                    // below this branch). Passing 0 (whole-subresource) here
                    // was silently copying mMipChain[0]'s FULL, unshrunk size
                    // every iteration regardless of i - a real, confirmed
                    // root cause of the "pSrcBox does not fit on the
                    // destination subresource" D3D11 debug-layer errors
                    // (context='Radiance Gen Shader', ~18000 occurrences/
                    // session). res already correctly tracks this loop's
                    // current iteration's shrinking size (see its own
                    // declaration/update above).
#ifdef DX_RENDER
                    mTexture->getDXTexture()->copySliceFromBoundRenderTarget(i, probe->mCubeIndex * 6 + cf, res, res);
#else
                    glCopyTexSubImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, i, 0, 0, probe->mCubeIndex * 6 + cf, 0, 0, res, res);
#endif
                }

                if (i != mMipChain.size() - 1)
                {
                    res /= 2;
                    // S24 (2026-08-09, task #147 v1): glViewport was
                    // unguarded raw GL - this whole function was
                    // unreachable under DX_RENDER before this task (the
                    // now-removed update() gate), so it was never audited.
                    // Real crash cause found the hard way: OpenGL is fully
                    // delinked from DX_RENDER=ON builds, so this resolved
                    // to a null function pointer at runtime (0xc0000005,
                    // offset 0x0 - same signature as the original login
                    // crash, task #95/#96).
#ifdef DX_RENDER
                    {
                        // S24 (2026-08-13, task #163 follow-up, REVERTED
                        // 2026-08-14): was unflipped here on the theory that
                        // the cube-face Y-flip was simply wrong - confirmed
                        // wrong theory (task #194 room test, still upside
                        // down with this unflipped). Restored to the
                        // standard D3D11 negative-height flip, matching
                        // DXContext::setViewport(...,true)'s exact semantics
                        // for consistency (llviewerwindow.cpp's
                        // setup3DViewport(), also restored).
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

            gRadianceGenProgram.unbind();
        }
        else
        {
            //generate irradiance map
            gIrradianceGenProgram.bind();
            S32 channel = gIrradianceGenProgram.enableTexture(LLShaderMgr::REFLECTION_PROBES, LLTexUnit::TT_CUBE_MAP_ARRAY);
            mTexture->bind(channel);

            gIrradianceGenProgram.uniform1i(sSourceIdx, sourceIdx);
            gIrradianceGenProgram.uniform1f(LLShaderMgr::REFLECTION_PROBE_MAX_LOD, mMaxProbeLOD);

            mVertexBuffer->setBuffer();
            int start_mip = 0;
            // find the mip target to start with based on irradiance map resolution
            for (start_mip = 0; start_mip < mMipChain.size(); ++start_mip)
            {
                if (mMipChain[start_mip].getWidth() == LL_IRRADIANCE_MAP_RESOLUTION)
                {
                    break;
                }
            }

            //for (int i = start_mip; i < mMipChain.size(); ++i)
            {
                int i = start_mip;
                LL_PROFILE_GPU_ZONE("probe irradiance gen");
                // S24 (2026-08-09, task #147 v1): see the other glViewport
                // fix just above in this function for the full reasoning
                // (unguarded raw GL, null fn ptr crash under DX_RENDER).
#ifdef DX_RENDER
                {
                    // S24 (2026-08-13, task #163 follow-up, REVERTED
                    // 2026-08-14): same reasoning as the radiance-gen loop
                    // above - restored to the standard D3D11 negative-height
                    // flip.
                    D3D11_VIEWPORT vp = {};
                    vp.TopLeftX = 0.0f;
                    vp.TopLeftY = (float)mMipChain[i].getHeight();
                    vp.Width = (float)mMipChain[i].getWidth();
                    vp.Height = -(float)mMipChain[i].getHeight();
                    vp.MinDepth = 0.0f;
                    vp.MaxDepth = 1.0f;
                    gDXDevice.getContext()->RSSetViewports(1, &vp);
                }
#else
                glViewport(0, 0, mMipChain[i].getWidth(), mMipChain[i].getHeight());
#endif
                for (int cf = 0; cf < 6; ++cf)
                { // for each cube face
#ifdef DX_RENDER
                    // S24 (2026-08-22, plan item A - CLOSED-FORM REWRITE):
                    // see the matching radiance-gen loop above for the full
                    // derivation - replaces lookAt()+getDirectXRotation()+
                    // flipCol/fixHandedness/poleRotate with a single face
                    // index, consumed by irradianceGenV.hlsl's hardcoded
                    // per-face formula. GL path untouched below.
                    static LLStaticHashedString sCubeFaceIrr("cubeFace");
                    gIrradianceGenProgram.uniform1i(sCubeFaceIrr, cf);

                    // S24 (2026-08-23, LIVE ORIENTATION TUNER): see the
                    // s24GetCubeOrientDebug() definition above updateProbeFace().
                    static LLStaticHashedString sDbgSwapIrr("dbgSwap");
                    static LLStaticHashedString sDbgSignAIrr("dbgSignA");
                    static LLStaticHashedString sDbgSignBIrr("dbgSignB");
                    S24CubeOrientDebug orientDbgIrr = s24GetCubeOrientDebug(cf);
                    gIrradianceGenProgram.uniform1i(sDbgSwapIrr, orientDbgIrr.swap ? 1 : 0);
                    gIrradianceGenProgram.uniform1f(sDbgSignAIrr, orientDbgIrr.negA ? -1.f : 1.f);
                    gIrradianceGenProgram.uniform1f(sDbgSignBIrr, orientDbgIrr.negB ? -1.f : 1.f);
#else
                    LLCoordFrame frame;
                    frame.lookAt(LLVector3(0, 0, 0), LLCubeMapArray::sClipToCubeLookVecs[cf], LLCubeMapArray::sClipToCubeUpVecs[cf]);
                    F32 mat[16];
                    frame.getOpenGLRotation(mat);
                    gGL.loadMatrix(mat);
#endif

                    mVertexBuffer->drawArrays(gGL.TRIANGLE_STRIP, 0, 4);

#ifdef DX_RENDER
                    // S24 (2026-08-09, task #147 step 5): see the mip-copy
                    // block above for the full reasoning. Unlike the GL
                    // path, no mIrradianceMaps->bind()/mTexture->bind()
                    // rebind dance is needed here at all - the copy method
                    // never touches texture-unit/SRV bind state (it only
                    // reads OMGetRenderTargets() and issues a resource-level
                    // CopySubresourceRegion), so mTexture's own SRV bind
                    // from before this loop (line ~1020, still needed for
                    // gIrradianceGenProgram's own sampling of REFLECTION_PROBES
                    // on the NEXT face's draw call) is never disturbed.
                    //
                    // S24 (2026-08-10, task #147/#184 follow-up): mMipChain[0]
                    // (bound once at the very top of this whole face==5
                    // block, shared by BOTH the radiance-gen loop above and
                    // this irradiance-gen section - never rebound to
                    // mMipChain[start_mip] specifically) stays the actual
                    // bound render target throughout - only the viewport
                    // (set a few lines above, mMipChain[i].getWidth()/
                    // getHeight()) narrows what's actually drawn into.
                    // Passing 0 (whole-subresource) here copied mMipChain[0]'s
                    // full, unshrunk size regardless of the much smaller
                    // irradiance-map resolution - a real, confirmed root
                    // cause of the "pSrcBox does not fit" D3D11 debug-layer
                    // errors (context='Irradiance Gen Shader').
                    mIrradianceMaps->getDXTexture()->copySliceFromBoundRenderTarget(i - start_mip, probe->mCubeIndex * 6 + cf, (UINT)mMipChain[i].getWidth(), (UINT)mMipChain[i].getHeight());
#else
                    S32 res = mMipChain[i].getWidth();
                    mIrradianceMaps->bind(channel);
                    glCopyTexSubImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, i - start_mip, 0, 0, probe->mCubeIndex * 6 + cf, 0, 0, res, res);
                    mTexture->bind(channel);
#endif
                }
            }

            gIrradianceGenProgram.unbind();
        }

        mMipChain[0].flush();
    }
}

void LLReflectionMapManager::reset()
{
    mReset = true;
}

void LLReflectionMapManager::pause(F32 duration)
{
    mPaused = true;
    mResumeTime = gFrameTimeSeconds + duration;
}

void LLReflectionMapManager::resume()
{
    mPaused = false;
}

void LLReflectionMapManager::shift(const LLVector4a& offset)
{
    for (auto& probe : mProbes)
    {
        probe->mOrigin.add(offset);
    }
}

void LLReflectionMapManager::updateNeighbors(LLReflectionMap* probe)
{
    if (mDefaultProbe == probe)
    {
        return;
    }

    //remove from existing neighbors
    {

        for (auto& other : probe->mNeighbors)
        {
            auto const & iter = std::find(other->mNeighbors.begin(), other->mNeighbors.end(), probe);
            llassert(iter != other->mNeighbors.end()); // <--- bug davep if this ever happens, something broke badly
            other->mNeighbors.erase(iter);
        }

        probe->mNeighbors.clear();
    }

    // search for new neighbors
    if (probe->isRelevant())
    {
        for (auto& other : mProbes)
        {
            if (other != mDefaultProbe && other != probe)
            {
                if (other->isRelevant() && probe->intersects(other))
                {
                    probe->mNeighbors.push_back(other);
                    other->mNeighbors.push_back(probe);
                }
            }
        }
    }
}

void LLReflectionMapManager::updateUniforms()
{
    if (!LLPipeline::sReflectionProbesEnabled)
    {
        return;
    }

    LL_PROFILE_GPU_ZONE("rmmu - uniforms");


    mReflectionMaps.resize(mReflectionProbeCount);
    getReflectionMaps(mReflectionMaps);

    F32 minDepth[256];

    for (int i = 0; i < 256; ++i)
    {
        mProbeData.refBucket[i][0] = mReflectionProbeCount;
        mProbeData.refBucket[i][1] = mReflectionProbeCount;
        mProbeData.refBucket[i][2] = mReflectionProbeCount;
        mProbeData.refBucket[i][3] = mReflectionProbeCount;
        minDepth[i] = FLT_MAX;
    }

    // load modelview matrix into matrix 4a
    LLMatrix4a modelview;
    modelview.loadu(gGLModelView);
    LLVector4a oa; // scratch space for transformed origin

    S32 count = 0;
    U32 nc = 0; // neighbor "cursor" - index into refNeighbor to start writing the next probe's list of neighbors

    LLEnvironment& environment = LLEnvironment::instance();
    LLSettingsSky::ptr_t psky = environment.getCurrentSky();

    static LLCachedControl<bool> should_auto_adjust(gSavedSettings, "RenderSkyAutoAdjustLegacy", false);
    F32 minimum_ambiance = psky->getReflectionProbeAmbiance(should_auto_adjust);

    bool is_ambiance_pass = gCubeSnapshot && !isRadiancePass();
    F32 ambscale = is_ambiance_pass ? 0.f : 1.f;
    ambscale *= mResetFade;
    ambscale = llmax(0, ambscale);
    F32 radscale = is_ambiance_pass ? 0.5f : 1.f;
    radscale *= mResetFade;
    radscale = llmax(0, radscale);

    for (auto* refmap : mReflectionMaps)
    {
        if (refmap == nullptr)
        {
            break;
        }

        if (refmap != mDefaultProbe)
        {
            // bucket search data
            // theory of operation:
            //      1. Determine minimum and maximum depth of each influence volume and store in mDepth (done in getReflectionMaps)
            //      2. Sort by minimum depth
            //      3. Prepare a bucket for each 1m of depth out to 256m
            //      4. For each bucket, store the index of the nearest probe that might influence pixels in that bucket
            //      5. In the shader, lookup the bucket for the pixel depth to get the index of the first probe that could possibly influence
            //          the current pixel.
            unsigned int depth_min = llclamp(llfloor(refmap->mMinDepth), 0, 255);
            unsigned int depth_max = llclamp(llfloor(refmap->mMaxDepth), 0, 255);
            for (U32 i = depth_min; i <= depth_max; ++i)
            {
                if (refmap->mMinDepth < minDepth[i])
                {
                    minDepth[i] = refmap->mMinDepth;
                    mProbeData.refBucket[i][0] = refmap->mProbeIndex;
                }
            }
        }

        llassert(refmap->mProbeIndex == count);
        llassert(mReflectionMaps[refmap->mProbeIndex] == refmap);

        llassert(refmap->mCubeIndex >= 0); // should always be  true, if not, getReflectionMaps is bugged

        {
            if (refmap->mViewerObject && refmap->mViewerObject->getVolume())
            { // have active manual probes live-track the object they're associated with
                LLVOVolume* vobj = (LLVOVolume*)refmap->mViewerObject.get();

                refmap->mOrigin.load3(vobj->getPositionAgent().mV);

                if (vobj->getReflectionProbeIsBox())
                {
                    LLVector3 s = vobj->getScale().scaledVec(LLVector3(0.5f, 0.5f, 0.5f));
                    refmap->mRadius = s.magVec();
                }
                else
                {
                    refmap->mRadius = refmap->mViewerObject->getScale().mV[0] * 0.5f;
                }
            }
            modelview.affineTransform(refmap->mOrigin, oa);
            mProbeData.refSphere[count].set(oa.getF32ptr());
            mProbeData.refSphere[count].mV[3] = refmap->mRadius;
        }

        mProbeData.refIndex[count][0] = refmap->mCubeIndex;
        llassert(nc % 4 == 0);
        mProbeData.refIndex[count][1] = nc / 4;
        mProbeData.refIndex[count][3] = refmap->mPriority;

        // for objects that are reflection probes, use the volume as the influence volume of the probe
        // only possibile influence volumes are boxes and spheres, so detect boxes and treat everything else as spheres
        if (refmap->getBox(mProbeData.refBox[count]))
        { // negate priority to indicate this probe has a box influence volume
            mProbeData.refIndex[count][3] = -mProbeData.refIndex[count][3];
        }

        mProbeData.refParams[count].set(
            llmax(minimum_ambiance, refmap->getAmbiance())*ambscale, // ambiance scale
            radscale, // radiance scale
            refmap->mFadeIn, // fade in weight
            oa.getF32ptr()[2] - refmap->mRadius); // z near

        S32 ni = nc; // neighbor ("index") - index into refNeighbor to write indices for current reflection probe's neighbors
        {
            //LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("rmmsu - refNeighbors");
            //pack neghbor list
            const U32 max_neighbors = 64;
            U32 neighbor_count = 0;

            for (auto& neighbor : refmap->mNeighbors)
            {
                if (ni >= 4096)
                { // out of space
                    break;
                }

                GLint idx = neighbor->mProbeIndex;
                if (idx == -1 || neighbor->mOccluded || neighbor->mCubeIndex == -1)
                {
                    continue;
                }

                // this neighbor may be sampled
                mProbeData.refNeighbor[ni++] = idx;

                neighbor_count++;
                if (neighbor_count >= max_neighbors)
                {
                    break;
                }
            }
        }

        if (nc == ni)
        {
            //no neighbors, tag as empty
            mProbeData.refIndex[count][1] = -1;
        }
        else
        {
            mProbeData.refIndex[count][2] = ni - nc;

            // move the cursor forward
            nc = ni;
            if (nc % 4 != 0)
            { // jump to next power of 4 for compatibility with ivec4
                nc += 4 - (nc % 4);
            }
        }


        count++;
    }

#if 0
    {
        // fill in gaps in refBucket
        S32 probe_idx = mReflectionProbeCount;

        for (int i = 0; i < 256; ++i)
        {
            if (i < count)
            { // for debugging, store depth of mReflectionsMaps[i]
                rpd.refBucket[i][1] = (S32) (mReflectionMaps[i]->mDepth * 10);
            }

            if (rpd.refBucket[i][0] == mReflectionProbeCount)
            {
                rpd.refBucket[i][0] = probe_idx;
            }
            else
            {
                probe_idx = rpd.refBucket[i][0];
            }
        }
    }
#endif

    mProbeData.refmapCount = count;

    // S24 (2026-08-22): a temporary diagnostic lived here during the task
    // #156 "reflections show only sky" investigation - confirmed refmapCount/
    // completeCount/occlusion/cbuffer state were all healthy, ruling out the
    // C++ capture pipeline. Root cause traced to RenderReflectionProbeBlurLODBias
    // sitting at a stale 1.5 in the user's settings.xml (default 0.0) rather
    // than any code gap. Removed per diagnostic-lifecycle convention.

    gPipeline.mHeroProbeManager.updateUniforms();

    // Get the hero data.

    mProbeData.heroBox = gPipeline.mHeroProbeManager.mHeroData.heroBox;
    mProbeData.heroSphere = gPipeline.mHeroProbeManager.mHeroData.heroSphere;
    mProbeData.heroShape  = gPipeline.mHeroProbeManager.mHeroData.heroShape;
    mProbeData.heroMipCount   = gPipeline.mHeroProbeManager.mHeroData.heroMipCount;
    mProbeData.heroProbeCount = gPipeline.mHeroProbeManager.mHeroData.heroProbeCount;

    //copy rpd into uniform buffer object
    // S24 (2026-08-09, task #147b): real D3D11 constant buffer - task #147
    // v1 deliberately skipped this (mProbeData/ReflectionProbeData is the
    // per-probe bucket/box/sphere/neighbor blend data v1's simplified
    // single-slice shader sample didn't read), guarded #ifndef DX_RENDER.
    // Now that reflectionProbeF.hlsl's real multi-probe blend reads this
    // data (cbuffer ReflectionProbes : register(b1)), it needs to actually
    // exist. mDXUBO mirrors mUBO's GL lifecycle: create once
    // (D3D11_USAGE_DYNAMIC, matching GL_STREAM_DRAW's "re-uploaded every
    // frame" usage pattern), then Map(WRITE_DISCARD)-based re-upload every
    // call after that - DXBuffer::upload() already does exactly this, no
    // new pattern needed. mUBO itself stays 0 under DX_RENDER (nothing
    // GL-side to allocate) - "created yet" is checked via
    // mDXUBO.getBuffer() (real resource pointer, correctly goes back to
    // null after cleanup()'s mDXUBO.destroy() call - e.g. on teleport -
    // triggering a real recreation next call, unlike a separate bool flag
    // which wouldn't reset itself the same way).
#ifdef DX_RENDER
    {
        if (!mDXUBO.getBuffer())
        {
            mDXUBO.createConstantBuffer(sizeof(ReflectionProbeData), &mProbeData);
        }
        else
        {
            mDXUBO.upload(&mProbeData, sizeof(ReflectionProbeData));
        }
    }
#else
    if (mUBO == 0)
    {
        glGenBuffers(1, &mUBO);
    }

    {
        glBindBuffer(GL_UNIFORM_BUFFER, mUBO);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(ReflectionProbeData), &mProbeData, GL_STREAM_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }
#endif

#if 0
    if (!gCubeSnapshot)
    {
        for (auto& probe : mProbes)
        {
            LLViewerObject* vobj = probe->mViewerObject;
            if (vobj)
            {
                F32 time = (F32)gFrameTimeSeconds - probe->mLastUpdateTime;
                vobj->setDebugText(llformat("%d/%d/%d/%.1f - %.1f/%.1f", probe->mCubeIndex, probe->mProbeIndex, (U32) probe->mNeighbors.size(), probe->mMinDepth, probe->mMaxDepth, time), time > 1.f ? LLColor4::white : LLColor4::green);
            }
        }
    }
#endif
}

void LLReflectionMapManager::setUniforms()
{
    // S24 (2026-08-15, task #194 offshoot): this used to return here with
    // no uniform written at all - confirmed real gap, verified against
    // pipeline.cpp:8881 (bindReflectionProbes(shader) is called
    // UNCONDITIONALLY, outside the use_legacy_env_map branch, so this
    // function still runs every frame regardless of the toggle) and
    // reflectionProbeF.hlsl's sampleProbes() (gated on the shader-side
    // "probes_enabled" uniform, a SEPARATE control from
    // sReflectionProbesEnabled - see RenderReflectionProbesEnabled vs
    // RenderReflectionsEnabled). With no write here, "probes_enabled"
    // kept whatever value was last uploaded (1, from this setting's own
    // true default) - the shader kept trying to sample the probe array
    // even when the C++ side had switched to the legacy binding path and
    // the capture pipeline had stopped updating that array. Now
    // explicitly tells the shader to stop sampling before returning,
    // instead of silently leaving it in whatever state it was last in.
    if (!LLPipeline::sReflectionProbesEnabled)
    {
        static LLStaticHashedString sProbesEnabledOff("probes_enabled");
        if (LLGLSLShader::sCurBoundShaderPtr)
        {
            LLGLSLShader::sCurBoundShaderPtr->uniform1i(sProbesEnabledOff, 0);
        }
        return;
    }

    if (mUBO == 0)
    {
        updateUniforms();
    }
    // S24 (2026-08-09, task #147b): real bind - register(b1) in
    // reflectionProbeF.hlsl's "cbuffer ReflectionProbes" declaration.
    // register(b0) is already taken by every shader's auto-generated
    // $Globals cbuffer (see LLRender's hardcoded VSSetConstantBuffers(0,...)/
    // PSSetConstantBuffers(0,...) call sites, llrender.cpp) - this is a
    // SEPARATE, explicit slot 1 bind, pixel-stage only (nothing in the
    // reflection-probe blend math runs in a vertex shader).
#ifdef DX_RENDER
    {
        ID3D11Buffer* cb = mDXUBO.getBuffer();
        if (cb)
        {
            gDXDevice.getContext()->PSSetConstantBuffers(1, 1, &cb);
        }
    }
#else
    glBindBufferBase(GL_UNIFORM_BUFFER, LLGLSLShader::UB_REFLECTION_PROBES, mUBO);
#endif

    // S24: Set probe control uniforms for shader tweaks (runtime adjustable)
    static LLCachedControl<bool> probes_enabled(gSavedSettings, "RenderReflectionProbesEnabled", true);
    static LLCachedControl<F32> probe_intensity(gSavedSettings, "RenderReflectionProbeIntensity", 1.0f);
    static LLCachedControl<F32> probe_saturation(gSavedSettings, "RenderReflectionProbeSaturation", 1.0f);
    static LLCachedControl<F32> probe_contrast(gSavedSettings, "RenderReflectionProbeContrast", 1.0f);
    static LLCachedControl<F32> probe_blur_lod_bias(gSavedSettings, "RenderReflectionProbeBlurLODBias", 0.0f);
    static LLCachedControl<F32> probe_ambient_mult(gSavedSettings, "RenderReflectionProbeAmbientMultiplier", 1.0f);

    static LLStaticHashedString sProbesEnabled("probes_enabled");
    static LLStaticHashedString sProbeIntensity("probe_intensity");
    static LLStaticHashedString sProbeSaturation("probe_saturation");
    static LLStaticHashedString sProbeContrast("probe_contrast");
    static LLStaticHashedString sProbeBlurLODBias("probe_blur_lod_bias");
    static LLStaticHashedString sProbeAmbientMultiplier("probe_ambient_multiplier");

    LLGLSLShader::sCurBoundShaderPtr->uniform1i(sProbesEnabled, probes_enabled ? 1 : 0);
    LLGLSLShader::sCurBoundShaderPtr->uniform1f(sProbeIntensity, probe_intensity);
    LLGLSLShader::sCurBoundShaderPtr->uniform1f(sProbeSaturation, probe_saturation);
    LLGLSLShader::sCurBoundShaderPtr->uniform1f(sProbeContrast, probe_contrast);
    LLGLSLShader::sCurBoundShaderPtr->uniform1f(sProbeBlurLODBias, probe_blur_lod_bias);
    LLGLSLShader::sCurBoundShaderPtr->uniform1f(sProbeAmbientMultiplier, probe_ambient_mult);
}


void renderReflectionProbe(LLReflectionMap* probe)
{
    if (probe->isRelevant())
    {
        F32* po = probe->mOrigin.getF32ptr();

        //draw orange line from probe to neighbors
        gGL.flush();
        gGL.diffuseColor4f(1, 0.5f, 0, 1);
        gGL.begin(gGL.LINES);
        for (auto& neighbor : probe->mNeighbors)
        {
            if (probe->mViewerObject && neighbor->mViewerObject)
            {
                continue;
            }

            gGL.vertex3fv(po);
            gGL.vertex3fv(neighbor->mOrigin.getF32ptr());
        }
        gGL.end();
        gGL.flush();

        gGL.diffuseColor4f(1, 1, 0, 1);
        gGL.begin(gGL.LINES);
        for (auto& neighbor : probe->mNeighbors)
        {
            if (probe->mViewerObject && neighbor->mViewerObject)
            {
                gGL.vertex3fv(po);
                gGL.vertex3fv(neighbor->mOrigin.getF32ptr());
            }
        }
        gGL.end();
        gGL.flush();
    }

#if 0
    LLSpatialGroup* group = probe->mGroup;
    if (group)
    { // draw lines from corners of object aabb to reflection probe

        const LLVector4a* bounds = group->getBounds();
        LLVector4a o = bounds[0];

        gGL.flush();
        gGL.diffuseColor4f(0, 0, 1, 1);
        F32* c = o.getF32ptr();

        const F32* bc = bounds[0].getF32ptr();
        const F32* bs = bounds[1].getF32ptr();

        // daaw blue lines from corners to center of node
        gGL.begin(gGL.LINES);
        gGL.vertex3fv(c);
        gGL.vertex3f(bc[0] + bs[0], bc[1] + bs[1], bc[2] + bs[2]);
        gGL.vertex3fv(c);
        gGL.vertex3f(bc[0] - bs[0], bc[1] + bs[1], bc[2] + bs[2]);
        gGL.vertex3fv(c);
        gGL.vertex3f(bc[0] + bs[0], bc[1] - bs[1], bc[2] + bs[2]);
        gGL.vertex3fv(c);
        gGL.vertex3f(bc[0] - bs[0], bc[1] - bs[1], bc[2] + bs[2]);

        gGL.vertex3fv(c);
        gGL.vertex3f(bc[0] + bs[0], bc[1] + bs[1], bc[2] - bs[2]);
        gGL.vertex3fv(c);
        gGL.vertex3f(bc[0] - bs[0], bc[1] + bs[1], bc[2] - bs[2]);
        gGL.vertex3fv(c);
        gGL.vertex3f(bc[0] + bs[0], bc[1] - bs[1], bc[2] - bs[2]);
        gGL.vertex3fv(c);
        gGL.vertex3f(bc[0] - bs[0], bc[1] - bs[1], bc[2] - bs[2]);
        gGL.end();

        //draw yellow line from center of node to reflection probe origin
        gGL.flush();
        gGL.diffuseColor4f(1, 1, 0, 1);
        gGL.begin(gGL.LINES);
        gGL.vertex3fv(c);
        gGL.vertex3fv(po);
        gGL.end();
        gGL.flush();
    }
#endif
}

void LLReflectionMapManager::renderDebug()
{
    gDebugProgram.bind();

    for (auto& probe : mProbes)
    {
        renderReflectionProbe(probe);
    }

    gDebugProgram.unbind();
}

void LLReflectionMapManager::initReflectionMaps()
{
    static LLCachedControl<U32> ref_probe_res(gSavedSettings, "RenderReflectionProbeResolution", 128U);
    U32 probe_resolution = nhpo2(llclamp(ref_probe_res(), (U32)64, (U32)512));
    if (mTexture.isNull() || mReflectionProbeCount != mDynamicProbeCount || mProbeResolution != probe_resolution || mReset)
    {
        if(mProbeResolution != probe_resolution)
        {
            mRenderTarget.release();
            mMipChain.clear();
        }

        gEXRImage = nullptr;
        mReset = false;
        mReflectionProbeCount = mDynamicProbeCount;
        mProbeResolution = probe_resolution;
        mMaxProbeLOD = log2f((F32)mProbeResolution) - 1.f; // number of mips - 1

        if (mTexture.isNull() ||
            mTexture->getWidth() != mProbeResolution ||
            mReflectionProbeCount + 2 != mTexture->getCount())
        {
            if (mTexture)
            {
                mTexture = new LLCubeMapArray(*mTexture, mProbeResolution, mReflectionProbeCount + 2);

                mIrradianceMaps = new LLCubeMapArray(*mIrradianceMaps, LL_IRRADIANCE_MAP_RESOLUTION, mReflectionProbeCount);
            }
            else
            {
            mTexture = new LLCubeMapArray();

            static LLCachedControl<bool> render_hdr(gSavedSettings, "RenderHDREnabled", true);

            // store mReflectionProbeCount+2 cube maps, final two cube maps are used for render target and radiance map generation source)
                // source)
            mTexture->allocate(mProbeResolution, 3, mReflectionProbeCount + 2, true, render_hdr);

            mIrradianceMaps = new LLCubeMapArray();
            mIrradianceMaps->allocate(LL_IRRADIANCE_MAP_RESOLUTION, 3, mReflectionProbeCount, false, render_hdr);
            }
        }

        // reset probe state
        mUpdatingFace = 0;
        mUpdatingProbe = nullptr;
        mRadiancePass = false;
        mRealtimeRadiancePass = false;

        // if default probe already exists, remember whether or not it's complete (SL-20498)
        bool default_complete = mDefaultProbe.isNull() ? false : mDefaultProbe->mComplete;

        for (auto& probe : mProbes)
        {
            probe->mLastUpdateTime = 0.f;
            probe->mComplete = false;
            probe->mProbeIndex = -1;
            probe->mCubeArray = nullptr;
            probe->mCubeIndex = -1;
            probe->mNeighbors.clear();
            probe->mFadeIn = 0;
        }

        mCubeFree.clear();
        initCubeFree();

        if (mDefaultProbe.isNull())
        {
            llassert(mProbes.empty()); // default probe MUST be the first probe created
            mDefaultProbe = new LLReflectionMap();
            mProbes.push_back(mDefaultProbe);
        }

        llassert(mProbes[0] == mDefaultProbe);

        mDefaultProbe->mCubeIndex = 0;
        mDefaultProbe->mCubeArray = mTexture;
        mDefaultProbe->mDistance = 64.f;
        mDefaultProbe->mRadius = 4096.f;
        mDefaultProbe->mProbeIndex = 0;
        mDefaultProbe->mComplete = default_complete;

        touch_default_probe(mDefaultProbe);
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

void LLReflectionMapManager::cleanup()
{
    mVertexBuffer = nullptr;
    mRenderTarget.release();

    mMipChain.clear();

    mTexture = nullptr;
    mIrradianceMaps = nullptr;

    mProbes.clear();
    mKillList.clear();
    mCreateList.clear();

    mReflectionMaps.clear();
    mUpdatingFace = 0;

    mDefaultProbe = nullptr;
    mUpdatingProbe = nullptr;

    // S24 (2026-08-09, task #147b): real destroy - mDXUBO now backs
    // mUBO's data under DX_RENDER (task #147 v1 never created it, so this
    // was previously just a guarded no-op here). Also called on every
    // teleport, not just shutdown - destroy()/getBuffer()==nullptr
    // afterward correctly triggers updateUniforms() to recreate it next
    // time, same as GL's mUBO==0 check does.
#ifdef DX_RENDER
    mDXUBO.destroy();
#else
    glDeleteBuffers(1, &mUBO);
#endif
    mUBO = 0;

    cleanupQueryPool();

    // note: also called on teleport (not just shutdown), so make sure we're in a good "starting" state
    initCubeFree();
}

void LLReflectionMapManager::cleanupQueryPool()
{
    if (!mQueryPool.empty())
    {
        std::vector<GLuint> queries(mQueryPool.begin(), mQueryPool.end());
        // S24 (2026-08-21, CTD fix): raw glDeleteQueries() is a null
        // extension-function-pointer crash under DX_RENDER - same bug class
        // as allocateQuery()'s glGenQueries() (task #182 CTD fix) and
        // LLReflectionMap::doOcclusion()'s glBeginQuery()/glEndQuery() (task
        // #249) - this is the release side of the same query pool, missed
        // by both of those earlier passes since it's only reached when
        // refreshSettings() runs (a settings-change listener, e.g. toggling
        // RenderScreenSpaceReflections/RenderReflectionProbeDetail) or on
        // teleport/shutdown (destroy()), not during normal per-frame
        // rendering - confirmed via a real minidump (RIP=0, called from
        // cleanupQueryPool -> refreshSettings -> handleReflectionProbeDetailChanged,
        // fired by clicking the SSR toggle).
#ifdef DX_RENDER
        DXOcclusionQuery::deleteQueries(static_cast<int>(queries.size()), queries.data());
#else
        glDeleteQueries(static_cast<GLsizei>(queries.size()), queries.data());
#endif
        mQueryPool.clear();
    }
}

void LLReflectionMapManager::doOcclusion()
{
    LLVector4a eye;
    eye.load3(LLViewerCamera::instance().getOrigin().mV);

    for (auto& probe : mProbes)
    {
        if (probe != nullptr && probe != mDefaultProbe)
        {
            probe->doOcclusion(eye);
        }
    }
}

void LLReflectionMapManager::forceDefaultProbeAndUpdateUniforms(bool force)
{
    static std::vector<bool> mProbeWasOccluded;

    if (force)
    {
        llassert(mProbeWasOccluded.empty());

        for (size_t i = 0; i < mProbes.size(); ++i)
        {
            auto& probe = mProbes[i];
            mProbeWasOccluded.push_back(probe->mOccluded);
            if (probe != nullptr && probe != mDefaultProbe)
            {
                probe->mOccluded = true;
            }
        }

        updateUniforms();
    }
    else
    {
        llassert(mProbes.size() == mProbeWasOccluded.size());

        const size_t n = llmin(mProbes.size(), mProbeWasOccluded.size());
        for (size_t i = 0; i < n; ++i)
        {
            auto& probe = mProbes[i];
            llassert(probe->mOccluded == (probe != mDefaultProbe));
            probe->mOccluded = mProbeWasOccluded[i];
        }
        mProbeWasOccluded.clear();
        mProbeWasOccluded.shrink_to_fit();
    }
}
