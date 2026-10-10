/**
 * @file llsphereprobes.cpp
 * @brief LLSphereProbes class implementation
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

#include "llsphereprobes.h"

#include <vector>

#include "llagent.h"
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

static LLTrace::BlockTimerStatHandle FTM_REFLECTION_PROBE_UPDATE("Reflection Probes");
static LLTrace::BlockTimerStatHandle FTM_REFLECTION_PROBE_GEN("Probe Generation");

LLPointer<LLImageDX> gEXRImage;

void load_exr(const std::string& filename)
{
    // reset reflection maps when previewing a new HDRI
    gPipeline.mSphereProbes.reset();
    gPipeline.mSphereProbes.initReflectionMaps();

    float* out; // width * height * RGBA
    int width;
    int height;
    const char* err = NULL; // or nullptr in C++11

    int ret =  LoadEXRWithLayer(&out, &width, &height, filename.c_str(), /* layername */ nullptr, &err);
    if (ret == TINYEXR_SUCCESS)
    {
        U32 texName = 0;
        LLImageDX::generateTextures(1, &texName);

        gEXRImage = new LLImageDX(texName, 4, GL_TEXTURE_2D, GL_RGB16F, GL_RGB16F, GL_FLOAT, LLTexUnit::TAM_CLAMP);
        gEXRImage->setHasMipMaps(true);
        gEXRImage->setUseMipMaps(true);
        gEXRImage->setFilteringOption(LLTexUnit::TFO_TRILINEAR);

        gDX.getTexUnit(0)->bind(gEXRImage);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGBA, GL_FLOAT, out);

        LLImageDXMemory::alloc_tex_image(width, height, GL_RGB16F, 1);

        free(out); // release memory of image data

        glGenerateMipmap(GL_TEXTURE_2D);

        gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

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

// get the next highest power of two of v (or v if v is already a power of two)
//defined in llvertexbuffer.cpp
extern U32 nhpo2(U32 v);

static void touch_default_probe(LLReflectionMap* probe, bool force = false)
{
    // the origin is fixed for the whole capture unless this is the capture's start
    if (!force && gPipeline.mSphereProbes.isCapturing(probe))
    {
        return;
    }
    if (LLViewerCamera::getInstance())
    {
        LLVector3 origin = LLViewerCamera::getInstance()->getOrigin();
        origin.mV[2] += 64.f;

        probe->mOrigin.load3(origin.mV);
    }
}

LLSphereProbes::LLSphereProbes()
{
    mDynamicProbeCount = LL_MAX_REFLECTION_PROBE_COUNT;
    initCubeFree();
}

void LLSphereProbes::initCubeFree()
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
        // probes the current level excludes never sample, so they rank after every relevant probe and take no slots
        bool lhs_relevant = lhs->isRelevant();
        bool rhs_relevant = rhs->isRelevant();
        if (lhs_relevant != rhs_relevant)
        {
            return lhs_relevant;
        }

        // distance alone decides the order. Priority class does not, or a far manual probe could displace a nearer
        // complete automatic one, which the retention rules forbid.
        return lhs->mDistance < rhs->mDistance;
    }
};

// Update priority: staleness, with a distance term so nearer probes refresh sooner. An incomplete
// probe gets a fixed head start so new probes finish quickly. Because staleness keeps growing, a
// complete probe can't be starved by an incomplete one that never finishes (SL-20258).
static F32 probe_update_score(const LLReflectionMap* p)
{
    const F32 incomplete_head_start = 2.f; // seconds of staleness
    return (gFrameTimeSeconds - p->mLastUpdateTime) + (p->mComplete ? 0.f : incomplete_head_start) - p->mDistance * 0.1f;
}

// helper class to seed octree with probes
// One realtime cube centred on the avatar. Each frame it captures all six faces for both halves, so its radiance
// and irradiance are always from the same frame, then commits its geometry. Its far clip is the probe draw distance.
void LLSphereProbes::updateMasterCube()
{
    static LLCachedControl<bool> master(gSavedSettings, "RenderReflectionMasterCube", false);
    if (!master)
    {
        mMasterProbe = nullptr; // the entry is deleted once nothing else holds it
        return;
    }

    if (mMasterProbe.isNull())
    {
        mMasterProbe = new LLReflectionMap();
        mMasterProbe->mCell = true;
        mMasterProbe->mPriority = 0;
        mMasterProbe->mRadius = 64.f;
        mMasterProbe->mBoxExtent.splat(64.f);
        mCreateList.push_back(mMasterProbe);
        return; // gets its slot in the next update, captures from then on
    }

    if (mMasterProbe->mCubeIndex < 1)
    {
        return;
    }

    // Realtime captures every frame. Once and Static capture once after each reset (a level, detail or resolution
    // change). Static and dynamic also refreshes at the default probe period, so avatars and particles stay current.
    if (mRenderReflectionProbeDetail < (S32)DetailLevel::REALTIME && mMasterProbe->mComplete)
    {
        static LLCachedControl<F32> sUpdatePeriod(gSavedSettings, "RenderDefaultProbeUpdatePeriod", 2.f);
        if (mRenderReflectionProbeDetail < (S32)DetailLevel::STATIC_AND_DYNAMIC
            || (gFrameTimeSeconds - mMasterProbe->mLastUpdateTime) < sUpdatePeriod)
        {
            return;
        }
    }

    mMasterProbe->mOrigin.load3(gAgent.getPositionAgent().mV);
    mMasterProbe->captureGeometry();

    const bool radiance_pass = isRadiancePass();
    mRealtimeUpdate = true;
    for (U32 pass = 0; pass < 2; ++pass)
    {
        mRadiancePass = (pass == 1);
        for (U32 face = 0; face < 6; ++face)
        {
            updateProbeFace(mMasterProbe, face);
        }
    }
    mRealtimeUpdate = false;
    mRadiancePass = radiance_pass;

    mMasterProbe->commitCapture();
    mMasterProbe->mComplete = true;
    mMasterProbe->mLastUpdateTime = gFrameTimeSeconds;
}

// Keeps a 3x3x3 block of 16 m grid cells around the camera. Each cell is a probe with no owner: it captures the
// full scene at its centre, and surfaces sample it like any other automatic probe.
void LLSphereProbes::updateCellGrid(const LLVector4a& camera_pos)
{
    static LLCachedControl<bool> cell_grid(gSavedSettings, "RenderReflectionCellGrid", false);
    if (!cell_grid)
    {
        mCellProbes.clear(); // the entries are deleted once nothing else holds them
        return;
    }

    const F32 cell_size = 16.f;
    const S32 reach = 1;
    const F32* c = camera_pos.getF32ptr();
    const std::array<S32, 3> cam = {
        (S32)floorf(c[0] / cell_size), (S32)floorf(c[1] / cell_size), (S32)floorf(c[2] / cell_size) };

    // release cells the camera has moved away from
    for (auto it = mCellProbes.begin(); it != mCellProbes.end(); )
    {
        bool outside = false;
        for (S32 a = 0; a < 3; ++a)
        {
            outside |= llabs(it->first[a] - cam[a]) > reach + 1;
        }
        it = outside ? mCellProbes.erase(it) : std::next(it);
    }

    // create the cells around the camera that do not exist yet
    for (S32 x = -reach; x <= reach; ++x)
    {
        for (S32 y = -reach; y <= reach; ++y)
        {
            for (S32 z = -reach; z <= reach; ++z)
            {
                std::array<S32, 3> key = { cam[0] + x, cam[1] + y, cam[2] + z };
                if (mCellProbes.count(key))
                {
                    continue;
                }

                LLReflectionMap* probe = new LLReflectionMap();
                probe->mCell = true;
                probe->mPriority = 0;
                probe->mOrigin.set((key[0] + 0.5f) * cell_size, (key[1] + 0.5f) * cell_size, (key[2] + 0.5f) * cell_size, 1.f);
                probe->mRadius = cell_size * F_SQRT3 * 0.5f;
                probe->mBoxExtent.splat(probe->mRadius);
                mCellProbes[key] = probe;
                mCreateList.push_back(probe);
            }
        }
    }
}

void LLSphereProbes::update()
{
    LL_RECORD_BLOCK_TIME(FTM_REFLECTION_PROBE_UPDATE);

    // updateProbeFace() below uses CopySubresourceRegion-based copies (see DXCubeArrayTexture.h) instead
    // of glCopyTexSubImage3D - the destination is never bound as an SRV, avoiding the "resource bound
    // as both OM output and SRV input" hazard.
    if (!LLPipeline::sReflectionProbesEnabled || gTeleportDisplay || LLStartUp::getStartupState() < STATE_PRECACHE)
    {
        return;
    }

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
        // Slot count is the RenderReflectionProbeCount setting. Which probes use the slots is decided by
        // isRelevant(), so the level no longer changes the slot count.
        U32 probe_count_temp = mDynamicProbeCount;
        mDynamicProbeCount = llmin(mRenderReflectionProbeCount, (U32)LL_MAX_REFLECTION_PROBE_COUNT);

        if (mDynamicProbeCount != probe_count_temp)
            mResetFade = 0.f;
    }

    initReflectionMaps();

    static LLCachedControl<bool> render_hdr(gSavedSettings, "RenderHDREnabled", true);

    if (!mCapture.isAllocated())
    {
        // Mip count is read from the radiance array (see mMaxProbeLOD in initReflectionMaps()).
        mCapture.allocate(mProbeResolution, mCubes.radiance().getMipLevels(), render_hdr,
                          LLProbeCapture::SUPER_SAMPLE, render_hdr ? GL_R11F_G11F_B10F : GL_RGB8);
    }

    llassert(mProbes[0] == mDefaultProbe);

    LLVector4a camera_pos;
    camera_pos.load3(LLViewerCamera::instance().getOrigin().mV);

    // process create list
    for (auto& probe : mCreateList)
    {
        mProbes.push_back(probe);
    }

    mCreateList.clear();

    updateCellGrid(camera_pos);

    if (mProbes.empty())
    {
        return;
    }


    bool did_update = false;

    bool realtime = mRenderReflectionProbeDetail >= (S32)LLSphereProbes::DetailLevel::REALTIME;

    LLReflectionMap* closestDynamic = nullptr;

    LLReflectionMap* oldestProbe = nullptr;
    LLReflectionMap* oldestOccluded = nullptr;

    // The default probe is a normal candidate, but only once its update period has passed (or while it
    // is still incomplete, since everything else depends on it).
    static LLCachedControl<F32> sUpdatePeriod(gSavedSettings, "RenderDefaultProbeUpdatePeriod", 2.f);
    const bool default_due = !mDefaultProbe->mComplete || (gFrameTimeSeconds - mDefaultProbe->mLastUpdateTime) >= sUpdatePeriod;

    if (mUpdatingProbe != nullptr)
    {
        did_update = true;
        doProbeUpdate();
    }

    // rank this frame's distances before sorting, so a probe created since the last frame is not ranked on its unset default
    for (U32 i = 1; i < mProbes.size(); ++i)
    {
        LLVector4a d;
        d.setSub(camera_pos, mProbes[i]->mOrigin);
        mProbes[i]->mDistance = d.getLength3().getF32() - mProbes[i]->mRadius;
    }
    std::sort(mProbes.begin()+1, mProbes.end(), CompareProbeDistance());
    llassert(mProbes[0] == mDefaultProbe);
    llassert(mProbes[0]->mCubeIndex == 0);

    // make sure we're assigning cube slots to the closest probes

    // Probes the current level excludes give their cubes back at once. They never sample, and they must not hold slots.
    for (U32 i = 1; i < mProbes.size(); ++i)
    {
        LLReflectionMap* probe = mProbes[i];
        if (!probe->isRelevant() && probe->mCubeIndex != -1 && mUpdatingProbe != probe)
        {
            mCubeFree.push_back(probe->mCubeIndex);
            probe->mCubeIndex = -1;
            probe->mComplete = false;
            probe->mFadeIn = 0;
        }
    }

    // Closest probes still waiting for a cube. Only that many distant cubes are needed.
    U32 count = llmin(mReflectionProbeCount, (U32)mProbes.size());
    U32 waiting = 0;
    for (U32 i = 1; i < count; ++i)
    {
        if (mProbes[i]->mCubeIndex == -1 && mProbes[i]->isRelevant())
        {
            ++waiting;
        }
    }

    // Reclaim cubes from distant probes only while a closer probe needs one, furthest first. A distant probe
    // keeps its complete cube otherwise, so a reshuffle of the distance order cannot discard a finished capture.
    for (U32 i = (U32)mProbes.size(); i-- > count && (size_t)waiting > mCubeFree.size(); )
    {
        LLReflectionMap* probe = mProbes[i];
        llassert(probe != nullptr);

        if (probe && probe->mCubeIndex != -1 && mUpdatingProbe != probe)
        { // free this index
            mCubeFree.push_back(probe->mCubeIndex);
            probe->mCubeIndex = -1;
            probe->mComplete = false;
            probe->mFadeIn = 0;
        }
    }

    // next distribute the free indices

    for (U32 i = 1; i < count && !mCubeFree.empty(); ++i)
    {
        // find the closest probe that needs a cube index
        LLReflectionMap* probe = mProbes[i];

        if (probe->mCubeIndex == -1 && probe->isRelevant())
        {
            S32 idx = allocateCubeIndex();
            llassert(idx > 0); //if we're still in this loop, mCubeFree should not be empty and allocateCubeIndex should be returning good indices
            probe->mCubeIndex = idx;
        }
    }

    mResetFade = llmin((F32)(mResetFade + gFrameIntervalSeconds * 2.f), 1.f);

    for (unsigned int i = 0; i < mProbes.size(); ++i)
    {
        LLReflectionMap* probe = mProbes[i];
        if (probe->getNumRefs() == 1 && probe != mUpdatingProbe)
        { // no references held outside manager, delete this probe. A probe mid-capture is kept until it finishes.
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
                probe->setOriginFromViewerObject();
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
        if (probe->mOccluded && probe->mComplete && !probe->mStale)
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
        else if (!did_update &&
                 i < mReflectionProbeCount &&
                 probe->mCubeIndex != -1 &&
                 (probe != mDefaultProbe || default_due))
        {
            if (oldestProbe == nullptr || probe_update_score(probe) > probe_update_score(oldestProbe))
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

    }

    updateMasterCube();

    if (realtime && closestDynamic != nullptr && closestDynamic != mUpdatingProbe)
    {
        // update the closest dynamic probe realtime
        // should do a full irradiance pass on "odd" frames and a radiance pass on "even" frames
        closestDynamic->autoAdjustOrigin();

        // the realtime cube is rendered from the live origin; its geometry is committed once its faces are written
        closestDynamic->captureGeometry();

        // store and override the value of "isRadiancePass" -- parts of the render pipe rely on "isRadiancePass" to set
        // lighting values etc
        bool radiance_pass = isRadiancePass();
        mRadiancePass = mRealtimeRadiancePass;
        mRealtimeUpdate = true;
        for (U32 i = 0; i < 6; ++i)
        {
            updateProbeFace(closestDynamic, i);
        }
        mRealtimeUpdate = false;
        closestDynamic->commitCapture();
        mRealtimeRadiancePass = !mRealtimeRadiancePass;

        // restore "isRadiancePass"
        mRadiancePass = radiance_pass;
    }

    // update the chosen probe
    if (!did_update && oldestProbe != nullptr)
    {
        LLReflectionMap* probe = oldestProbe;
        llassert(probe->mCubeIndex != -1);

        probe->autoAdjustOrigin();

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

void LLSphereProbes::refreshSettings()
{
    mRenderReflectionProbeDetail = gSavedSettings.getS32("RenderReflectionProbeDetail");
    mRenderReflectionProbeLevel = gSavedSettings.getS32("RenderReflectionProbeLevel");
    mRenderReflectionProbeCount = gSavedSettings.getU32("RenderReflectionProbeCount");
    cleanupQueryPool();
}

LLReflectionMap* LLSphereProbes::addProbe(LLSpatialGroup* group)
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

U32 LLSphereProbes::probeCount()
{
    return mDynamicProbeCount;
}

U32 LLSphereProbes::probeMemory()
{
    // Matches the allocations in initReflectionMaps(): radiance has a full mip chain and two scratch
    // cubes, irradiance has no mips. HDR is R16G16B16A16_FLOAT (8 bytes/texel), otherwise 4.
    static LLCachedControl<bool> render_hdr(gSavedSettings, "RenderHDREnabled", true);
    U64 bytes_per_texel = render_hdr ? 8 : 4;
    U64 radiance_texels = U64(mReflectionProbeCount + 2) * 6 * mProbeResolution * mProbeResolution * 4 / 3;
    U64 irradiance_texels = U64(mReflectionProbeCount) * 6 * LL_IRRADIANCE_MAP_RESOLUTION * LL_IRRADIANCE_MAP_RESOLUTION;
    return (U32)((radiance_texels + irradiance_texels) * bytes_per_texel / (1024 * 1024));
}

GLuint LLSphereProbes::allocateQuery()
{
    if (mQueryPool.empty())
    {
        GLuint query = 0;
        DXOcclusionQuery::genQueries(1, &query);
        return query;
    }

    GLuint query = mQueryPool.front();
    mQueryPool.pop_front();
    return query;
}

void LLSphereProbes::recycleQuery(GLuint query)
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

void LLSphereProbes::getReflectionMaps(std::vector<LLReflectionMap*>& maps)
{

    LLMatrix4a modelview;
    modelview.loadu(gGLModelView);
    LLVector4a oa; // scratch space for transformed origin

    U32 count = 0;
    U32 lastIdx = 0;

    // While the master cube is on and complete it is the only probe sampled, apart from the sky. Otherwise a finished
    // local automatic probe wins every indoor pixel: the shader breaks full-coverage ties by smaller radius.
    const bool master_only = mMasterProbe.notNull() && mMasterProbe->mComplete && mMasterProbe->mCubeIndex > 0;

    for (U32 i = 0; count < maps.size() && i < mProbes.size(); ++i)
    {

        // only the closest mReflectionProbeCount probes are sampled, even if distant ones still hold a cube
        if (i < mReflectionProbeCount && mProbes[i]->mCubeIndex != -1
            && (!master_only || mProbes[i] == mDefaultProbe || mProbes[i] == mMasterProbe))
        {
            if (probeUsable(mProbes[i]) && mProbes[i]->mComplete)
            {
                maps[count++] = mProbes[i];
                modelview.affineTransform(mProbes[i]->sampleOrigin(), oa);
                mProbes[i]->mMinDepth = -oa.getF32ptr()[2] - mProbes[i]->sampleRadius();
                mProbes[i]->mMaxDepth = -oa.getF32ptr()[2] + mProbes[i]->sampleRadius();
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

    // preProbeSample() appends list index 0 as the default probe, so the default stays first and only
    // the remaining probes are depth-sorted.
    U32 first_sorted = 0;
    for (U32 i = 0; i < count; ++i)
    {
        if (maps[i] == mDefaultProbe.get())
        {
            std::swap(maps[0], maps[i]);
            first_sorted = 1;
            break;
        }
    }

    if (count - first_sorted > 1)
    {
        std::sort(maps.begin() + first_sorted, maps.begin() + count, CompareProbeDepth());
    }

    // Only sphere-volume probes get an index in the sphere uniform block. Box probes get -1 here and
    // are indexed by LLBoxProbes.
    U32 sphere_count = 0;
    for (U32 i = 0; i < count; ++i)
    {
        maps[i]->mProbeIndex = maps[i]->isBoxVolume() ? -1 : (S32)sphere_count++;
    }

    // null terminate list
    if (count < maps.size())
    {
        maps[count] = nullptr;
    }
}

LLReflectionMap* LLSphereProbes::registerSpatialGroup(LLSpatialGroup* group)
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

LLReflectionMap* LLSphereProbes::registerViewerObject(LLViewerObject* vobj)
{
    if (!LLPipeline::sReflectionProbesEnabled)
    {
        return nullptr;
    }

    llassert(vobj != nullptr);

    LLReflectionMap* probe = new LLReflectionMap();
    probe->mViewerObject = vobj;
    probe->setOriginFromViewerObject();

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

S32 LLSphereProbes::allocateCubeIndex()
{
    if (!mCubeFree.empty())
    {
        S32 ret = mCubeFree.front();
        mCubeFree.pop_front();
        return ret;
    }

    return -1;
}

void LLSphereProbes::deleteProbe(U32 i)
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
        mRadiancePass = false; // the next probe starts with its irradiance half
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


void LLSphereProbes::doProbeUpdate()
{
    LL_RECORD_BLOCK_TIME(FTM_REFLECTION_PROBE_GEN);
    llassert(mUpdatingProbe != nullptr);

    // The origin is decided once, at the start of the irradiance pass, and shared by both halves of the capture.
    if (mUpdatingFace == 0 && !isRadiancePass())
    {
        if (mUpdatingProbe == mDefaultProbe)
        {
            touch_default_probe(mUpdatingProbe, true);
        }
        mUpdatingProbe->captureGeometry();
    }

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
            mUpdatingProbe->commitCapture();
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
// For every 12 calls of this function, one complete reflection probe radiance map and irradiance map is generated
// First six passes render the scene with direct lighting only into a scratch space cube map at the end of the cube map array and generate
// a simple mip chain (not convolution filter).
// At the end of these passes, an irradiance map is generated for this probe and placed into the irradiance cube map array at the index for this probe
// The next six passes render the scene with both radiance and irradiance into the same scratch space cube map and generate a simple mip chain.
// At the end of these passes, a radiance map is generated for this probe and placed into the radiance cube map array at the index for this probe.
// In effect this simulates single-bounce lighting.
void LLSphereProbes::updateProbeFace(LLReflectionMap* probe, U32 face)
{
    // hacky hot-swap of camera specific render targets
    LLPipeline::RenderTargetPack* prev_rt = gPipeline.mRT;
    gPipeline.mRT = &gPipeline.mAuxillaryRT;

    mLightScale = 1.f;
    static LLCachedControl<F32> max_local_light_ambiance(gSavedSettings, "RenderReflectionProbeMaxLocalLightAmbiance", 8.f);
    if (!isRadiancePass() && probe->getAmbiance() > max_local_light_ambiance)
    {
        mLightScale = max_local_light_ambiance / probe->getAmbiance();
    }

    // Content tier (RenderReflectionProbeLevel), applied to every probe the same way:
    //   0: sky and clouds. 1: adds terrain. 2: adds water. 3: everything, no mask.
    // Water is only drawn in its own tier. Its surface samples the screen copy during a capture, so the
    // refraction branch is skipped there (probe_capture in waterF.hlsl).
    const bool tiered = mRenderReflectionProbeLevel < 3;
    if (probe == mDefaultProbe)
    {
        touch_default_probe(probe);
    }

    // Whether the master cube captures avatars and particles, separately switchable per Detail mode: a perf
    // choice, since avatars are the most expensive thing in the capture. Static and dynamic excludes them by
    // default; Realtime includes them by default.
    static LLCachedControl<bool> exclude_avatar_dynamic(gSavedSettings, "RenderReflectionExcludeAvatarDynamic", false);
    static LLCachedControl<bool> exclude_avatar_realtime(gSavedSettings, "RenderReflectionExcludeAvatarRealtime", true);
    const bool detail_is_dynamic = mRenderReflectionProbeDetail == (S32)DetailLevel::STATIC_AND_DYNAMIC;
    const bool detail_is_realtime = mRenderReflectionProbeDetail >= (S32)DetailLevel::REALTIME;
    const bool force_dynamic = (probe == mMasterProbe.get())
        && ((detail_is_dynamic && !exclude_avatar_dynamic) || (detail_is_realtime && !exclude_avatar_realtime));

    if (tiered)
    {
        gPipeline.pushRenderTypeMask();

        if (mRenderReflectionProbeLevel >= 2)
        {
            gPipeline.andRenderTypeMask(LLPipeline::RENDER_TYPE_SKY, LLPipeline::RENDER_TYPE_WL_SKY,
                LLPipeline::RENDER_TYPE_CLOUDS, LLPipeline::RENDER_TYPE_TERRAIN,
                LLPipeline::RENDER_TYPE_WATER, LLPipeline::RENDER_TYPE_VOIDWATER, LLPipeline::END_RENDER_TYPES);
        }
        else if (mRenderReflectionProbeLevel == 1)
        {
            gPipeline.andRenderTypeMask(LLPipeline::RENDER_TYPE_SKY, LLPipeline::RENDER_TYPE_WL_SKY,
                LLPipeline::RENDER_TYPE_CLOUDS, LLPipeline::RENDER_TYPE_TERRAIN, LLPipeline::END_RENDER_TYPES);
        }
        else
        {
            gPipeline.andRenderTypeMask(LLPipeline::RENDER_TYPE_SKY, LLPipeline::RENDER_TYPE_WL_SKY,
                LLPipeline::RENDER_TYPE_CLOUDS, LLPipeline::END_RENDER_TYPES);
        }

        probe->update(mCapture.getSuperSampleResolution(), face, force_dynamic);

        gPipeline.popRenderTypeMask();
    }
    else
    {
        probe->update(mCapture.getSuperSampleResolution(), face, force_dynamic);
    }

    gPipeline.mRT = prev_rt;

    S32 sourceIdx = mReflectionProbeCount;

    if (mRealtimeUpdate)
    { // the realtime probe updates every frame: use the secondary scratch space channel
        sourceIdx += 1;
    }

    gDX.setColorWriteMask(true, true);
    LLGLDepthTest depth(GL_FALSE, GL_FALSE);
    LLGLDisable cull(GL_CULL_FACE);
    LLGLDisable blend(GL_BLEND);

    // Blur, downsample and copy this face into the scratch cube (see LLProbeCapture).
    mCapture.resolveFace(gPipeline.mAuxillaryRT.screen, mCubes.radiance(), sourceIdx * 6 + face);

    if (face == 5)
    {
        mCapture.getMips()[0].bindTarget();
        // Negative-height viewport is deliberate - matches DXContext::setViewport(...,true)'s
        // semantics. Moving this flip into radianceGenV.hlsl as a shader-side y negation instead
        // breaks hero-probe mirror orientation.
#ifdef DX_RENDER
        {
            D3D11_VIEWPORT vp = {};
            vp.TopLeftX = 0.0f;
            vp.TopLeftY = (float)mCapture.getMips()[0].getHeight();
            vp.Width = (float)mCapture.getMips()[0].getWidth();
            vp.Height = -(float)mCapture.getMips()[0].getHeight();
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
            LLProbeCubeStore::setGeneratorConvention(gRadianceGenProgram);
            mVertexBuffer->setBuffer();

            S32 channel = gRadianceGenProgram.enableTexture(LLShaderMgr::REFLECTION_PROBES, LLTexUnit::TT_CUBE_MAP_ARRAY);
            bindRadiance(channel);
            gRadianceGenProgram.uniform1i(sSourceIdx, sourceIdx);
            gRadianceGenProgram.uniform1f(LLShaderMgr::REFLECTION_PROBE_MAX_LOD, mMaxProbeLOD);
            gRadianceGenProgram.uniform1f(LLShaderMgr::REFLECTION_PROBE_STRENGTH, 1.f);

            U32 res = mCapture.getMips()[0].getWidth();

            for (int i = 0; i < mCapture.getMips().size(); ++i)
            {
                static LLStaticHashedString sMipLevel("mipLevel");
                static LLStaticHashedString sWidth("u_width");

                gRadianceGenProgram.uniform1f(sMipLevel, (F32)i);
                gRadianceGenProgram.uniform1i(sWidth, mProbeResolution);

                for (int cf = 0; cf < 6; ++cf)
                { // for each cube face
#ifdef DX_RENDER
                    // Direct3D's documented per-face cubemap addressing formula, hardcoded in
                    // radianceGenV.hlsl and driven only by this face index - replaces the old
                    // LLCoordFrame::lookAt()+getDirectXRotation()+per-face rotation-matrix patching,
                    // which could never be hand-tuned to match hardware addressing exactly (see that
                    // shader's header comment). GL path untouched (still lookAt()+getOpenGLRotation()).
                    static LLStaticHashedString sCubeFace("cubeFace");
                    gRadianceGenProgram.uniform1i(sCubeFace, cf);
#endif // DX_RENDER - GL body removed (LLCubeMapArray deleted, DX_RENDER-only project)

                    mVertexBuffer->drawArrays(gDX.TRIANGLE_STRIP, 0, 4);

                    // mCapture.getMips()[0] stays bound as render target for every iteration of this loop
                    // (never rebound per-i); only the viewport (RSSetViewports below) narrows what's
                    // drawn into it. Must pass the real, currently-shrunk `res,res` size here - passing
                    // 0 (whole-subresource) copies mCapture.getMips()[0]'s full unshrunk size regardless of i,
                    // which D3D11's debug layer rejects as "pSrcBox does not fit on the destination
                    // subresource".
#ifdef DX_RENDER
                    mCubes.radiance().copySliceFromBoundRenderTarget(i, probe->mCubeIndex * 6 + cf, res, res);
#else
                    glCopyTexSubImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, i, 0, 0, probe->mCubeIndex * 6 + cf, 0, 0, res, res);
#endif
                }

                if (i != mCapture.getMips().size() - 1)
                {
                    res /= 2;
                    // Raw glViewport has no DX_RENDER translation (OpenGL is fully delinked from
                    // DX_RENDER=ON builds - a null function pointer, not a silent no-op).
#ifdef DX_RENDER
                    {
                        // Negative-height viewport is deliberate, matching mCapture.getMips()[0]'s viewport
                        // above (face==5 entry) - do not "fix" to a positive height.
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
            LLProbeCubeStore::setGeneratorConvention(gIrradianceGenProgram);
            S32 channel = gIrradianceGenProgram.enableTexture(LLShaderMgr::REFLECTION_PROBES, LLTexUnit::TT_CUBE_MAP_ARRAY);
            bindRadiance(channel);

            gIrradianceGenProgram.uniform1i(sSourceIdx, sourceIdx);
            gIrradianceGenProgram.uniform1f(LLShaderMgr::REFLECTION_PROBE_MAX_LOD, mMaxProbeLOD);
            // Source face width for the sample LOD. Must match the scratch cube, not a fixed size.
            static LLStaticHashedString sIrrWidth("u_width");
            gIrradianceGenProgram.uniform1i(sIrrWidth, mProbeResolution);

            mVertexBuffer->setBuffer();
            int start_mip = 0;
            // find the mip target to start with based on irradiance map resolution
            for (start_mip = 0; start_mip < mCapture.getMips().size(); ++start_mip)
            {
                if (mCapture.getMips()[start_mip].getWidth() == LL_IRRADIANCE_MAP_RESOLUTION)
                {
                    break;
                }
            }

            //for (int i = start_mip; i < mCapture.getMips().size(); ++i)
            {
                int i = start_mip;
#ifdef DX_RENDER
                {
                    // Negative-height viewport is deliberate, matching the radiance-gen loop's viewport
                    // above.
                    D3D11_VIEWPORT vp = {};
                    vp.TopLeftX = 0.0f;
                    vp.TopLeftY = (float)mCapture.getMips()[i].getHeight();
                    vp.Width = (float)mCapture.getMips()[i].getWidth();
                    vp.Height = -(float)mCapture.getMips()[i].getHeight();
                    vp.MinDepth = 0.0f;
                    vp.MaxDepth = 1.0f;
                    gDXDevice.getContext()->RSSetViewports(1, &vp);
                }
#else
                glViewport(0, 0, mCapture.getMips()[i].getWidth(), mCapture.getMips()[i].getHeight());
#endif
                for (int cf = 0; cf < 6; ++cf)
                { // for each cube face
#ifdef DX_RENDER
                    // Same per-face addressing formula as the radiance-gen loop above, hardcoded in
                    // irradianceGenV.hlsl.
                    static LLStaticHashedString sCubeFaceIrr("cubeFace");
                    gIrradianceGenProgram.uniform1i(sCubeFaceIrr, cf);
#endif // DX_RENDER - GL body removed (LLCubeMapArray deleted, DX_RENDER-only project)

                    mVertexBuffer->drawArrays(gDX.TRIANGLE_STRIP, 0, 4);

#ifdef DX_RENDER
                    // No irradiance/radiance bind needed - the copy method only
                    // reads OMGetRenderTargets() and issues a CopySubresourceRegion, never touching
                    // texture-unit/SRV state, so the radiance SRV bind (needed for the next face's draw)
                    // is undisturbed. mCapture.getMips()[0] stays bound as render target throughout (shared with
                    // the radiance-gen loop, never rebound to mCapture.getMips()[start_mip]) - must pass the
                    // real mCapture.getMips()[i] width/height here, not 0 (whole-subresource), since the
                    // irradiance-map resolution is smaller than mCapture.getMips()[0]'s full size.
                    mCubes.irradiance().copySliceFromBoundRenderTarget(i - start_mip, probe->mCubeIndex * 6 + cf, (UINT)mCapture.getMips()[i].getWidth(), (UINT)mCapture.getMips()[i].getHeight());
#else
                    S32 res = mCapture.getMips()[i].getWidth();
                    bindIrradiance(channel);
                    glCopyTexSubImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, i - start_mip, 0, 0, probe->mCubeIndex * 6 + cf, 0, 0, res, res);
                    bindRadiance(channel);
#endif
                }
            }

            gIrradianceGenProgram.unbind();
        }

        mCapture.getMips()[0].flush();
    }
}

void LLSphereProbes::reset()
{
    mReset = true;
}

void LLSphereProbes::pause(F32 duration)
{
    mPaused = true;
    mResumeTime = gFrameTimeSeconds + duration;
}

void LLSphereProbes::resume()
{
    mPaused = false;
}

void LLSphereProbes::shift(const LLVector4a& offset)
{
    mCellProbes.clear(); // cells are keyed in the old frame and are rebuilt around the camera

    for (auto& probe : mProbes)
    {
        probe->mOrigin.add(offset);

        // the frozen capture values live in the same region frame as the cube, so they shift too.
        // mCaptureWorld is column-major with translation in elements 12-14.
        probe->mCaptureOrigin.add(offset);
        F32* world = (F32*)probe->mCaptureWorld.mMatrix;
        world[12] += offset.getF32ptr()[0];
        world[13] += offset.getF32ptr()[1];
        world[14] += offset.getF32ptr()[2];

        // a capture in flight commits these, so they shift with the live values
        probe->mPendingOrigin.add(offset);
        F32* pending_world = (F32*)probe->mPendingWorld.mMatrix;
        pending_world[12] += offset.getF32ptr()[0];
        pending_world[13] += offset.getF32ptr()[1];
        pending_world[14] += offset.getF32ptr()[2];
    }
}

void LLSphereProbes::updateNeighbors(LLReflectionMap* probe)
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

void LLSphereProbes::updateUniforms()
{
    if (!LLPipeline::sReflectionProbesEnabled)
    {
        return;
    }



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
    F32 minimum_ambiance = psky ? psky->getReflectionProbeAmbiance(should_auto_adjust) : 0.f;

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

        // box probes are packed by LLBoxProbes
        if (refmap->isBoxVolume())
        {
            continue;
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

        llassert(refmap->mCubeIndex >= 0); // should always be  true, if not, getReflectionMaps is bugged

        {
            // have active manual probes live-track the object they're associated with
            refmap->trackViewerObject();
            // the cube was captured from mCaptureOrigin, so parallax must use the same centre
            modelview.affineTransform(refmap->mCaptureOrigin, oa);
            mProbeData.refSphere[count].set(oa.getF32ptr());
            mProbeData.refSphere[count].mV[3] = refmap->mCaptureRadius;
        }

        mProbeData.refIndex[count][0] = refmap->mCubeIndex;
        llassert(nc % 4 == 0);
        mProbeData.refIndex[count][1] = nc / 4;
        mProbeData.refIndex[count][3] = refmap->mPriority;

        mProbeData.refParams[count].set(
            llmax(minimum_ambiance, refmap->getAmbiance())*ambscale, // ambiance scale
            radscale, // radiance scale
            refmap->mFadeIn, // fade in weight
            oa.getF32ptr()[2] - refmap->mCaptureRadius); // z near

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
                if (idx == -1 || !probeUsable(neighbor) || neighbor->mCubeIndex == -1)
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


    mProbeData.refmapCount = count;

    gPipeline.mMirrorProbes.updateUniforms();

    // Get the hero data.

    mProbeData.heroBox = gPipeline.mMirrorProbes.mHeroData.heroBox;
    mProbeData.heroSphere = gPipeline.mMirrorProbes.mHeroData.heroSphere;
    mProbeData.heroShape  = gPipeline.mMirrorProbes.mHeroData.heroShape;
    mProbeData.heroMipCount   = gPipeline.mMirrorProbes.mHeroData.heroMipCount;
    mProbeData.heroProbeCount = gPipeline.mMirrorProbes.mHeroData.heroProbeCount;

    mBoxProbes.update(mReflectionMaps, LL_MAX_REFLECTION_PROBE_COUNT, radscale);

    //copy rpd into uniform buffer object
    // mDXUBO mirrors mUBO's GL lifecycle: create once (D3D11_USAGE_DYNAMIC, matching GL_STREAM_DRAW),
    // then Map(WRITE_DISCARD) re-upload via DXBuffer::upload() thereafter. mUBO itself stays 0 under
    // DX_RENDER; "created yet" is checked via mDXUBO.getBuffer() instead, which correctly goes back to
    // null after cleanup()'s destroy() (e.g. on teleport), triggering recreation next call.
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

}

void LLSphereProbes::setUniforms()
{
    // Must explicitly clear the shader-side "probes_enabled" uniform before returning - it's a
    // separate control from sReflectionProbesEnabled and otherwise keeps whatever value was last
    // uploaded, so the shader keeps sampling a probe array the capture pipeline has stopped updating.
    if (!LLPipeline::sReflectionProbesEnabled)
    {
        static LLStaticHashedString sProbesEnabledOff("probes_enabled");
        if (LLHLSLShader::sCurBoundShaderPtr)
        {
            LLHLSLShader::sCurBoundShaderPtr->uniform1i(sProbesEnabledOff, 0);
        }
        return;
    }

    // mUBO stays 0 forever under DX_RENDER, so this bootstrap-gate must check mDXUBO.getBuffer()
    // instead (the DX-side equivalent of mUBO's "created yet" check) - otherwise updateUniforms()'s
    // full probe-bucket rebuild + GPU re-upload would run on every setUniforms() call instead of only
    // before the first per-frame update.
#ifdef DX_RENDER
    if (!mDXUBO.getBuffer())
#else
    if (mUBO == 0)
#endif
    {
        updateUniforms();
    }
    // Binds register(b1) in reflectionProbeF.hlsl's "cbuffer ReflectionProbes" - register(b0) is
    // already taken by every shader's auto-generated $Globals cbuffer (llrender.cpp). Pixel-stage
    // only; nothing in the reflection-probe blend math runs in a vertex shader.
#ifdef DX_RENDER
    {
        ID3D11Buffer* cb = mDXUBO.getBuffer();
        if (cb)
        {
            gDXDevice.getContext()->PSSetConstantBuffers(1, 1, &cb);
        }
    }
#else
    glBindBufferBase(GL_UNIFORM_BUFFER, LLHLSLShader::UB_REFLECTION_PROBES, mUBO);
#endif
    mBoxProbes.bind();

    // Probe control uniforms for shader tweaks (runtime adjustable)
    static LLCachedControl<bool> probes_enabled(gSavedSettings, "RenderReflectionProbesEnabled", true);
    static LLCachedControl<F32> probe_intensity(gSavedSettings, "RenderReflectionProbeIntensity", 1.0f);
    static LLCachedControl<F32> probe_saturation(gSavedSettings, "RenderReflectionProbeSaturation", 1.0f);
    static LLCachedControl<F32> probe_contrast(gSavedSettings, "RenderReflectionProbeContrast", 1.0f);
    static LLCachedControl<F32> probe_blur_lod_bias(gSavedSettings, "RenderReflectionProbeBlurLODBias", 0.0f);
    static LLCachedControl<bool> probe_auto_parallax(gSavedSettings, "RenderReflectionAutoParallax", true);
    static LLCachedControl<F32> probe_ambient_mult(gSavedSettings, "RenderReflectionProbeAmbientMultiplier", 1.0f);
    // Blends this probe's detail-mip sample toward its own top mip (max_probe_lod, the fully
    // GGX-convolved whole-hemisphere average - see tapRefMap()) at a tunable 0-1 weight. Unlike
    // probe_saturation/probe_contrast, this equalizes per-face brightness/color toward the room's own
    // averaged tone rather than an arbitrary neutral; 0.0 (default) preserves full detail.
    static LLCachedControl<F32> probe_equalize(gSavedSettings, "RenderReflectionProbeEqualize", 0.0f);
    // Visibility blend: 1.0=fully visible, 0.0=fully transparent. See reflectionProbeF.hlsl.
    static LLCachedControl<F32> probe_opacity(gSavedSettings, "RenderReflectionProbeOpacity", 1.0f);
    // Material accept/reject gate for reflections. Both pairs default to 0/0 (no-op): see
    // materialReflectionGate() in reflectionProbeF.hlsl.
    static LLCachedControl<F32> gate_gloss_reject(gSavedSettings, "RenderReflectionGateGlossReject", 0.0f);
    static LLCachedControl<F32> gate_gloss_accept(gSavedSettings, "RenderReflectionGateGlossAccept", 0.0f);
    static LLCachedControl<F32> gate_metallic_reject(gSavedSettings, "RenderReflectionGateMetallicReject", 0.0f);
    static LLCachedControl<F32> gate_metallic_accept(gSavedSettings, "RenderReflectionGateMetallicAccept", 0.0f);

    static LLStaticHashedString sProbesEnabled("probes_enabled");
    static LLStaticHashedString sProbeIntensity("probe_intensity");
    static LLStaticHashedString sProbeSaturation("probe_saturation");
    static LLStaticHashedString sProbeContrast("probe_contrast");
    static LLStaticHashedString sProbeBlurLODBias("probe_blur_lod_bias");
    static LLStaticHashedString sProbeAutoParallax("probe_auto_parallax");
    static LLStaticHashedString sProbeAmbientMultiplier("probe_ambient_multiplier");
    static LLStaticHashedString sProbeEqualize("probe_equalize");
    static LLStaticHashedString sProbeOpacity("probe_opacity");
    static LLStaticHashedString sGateGlossReject("reflection_gate_gloss_reject");
    static LLStaticHashedString sGateGlossAccept("reflection_gate_gloss_accept");
    static LLStaticHashedString sGateMetallicReject("reflection_gate_metallic_reject");
    static LLStaticHashedString sGateMetallicAccept("reflection_gate_metallic_accept");

    LLHLSLShader::sCurBoundShaderPtr->uniform1i(sProbesEnabled, probes_enabled ? 1 : 0);
    LLHLSLShader::sCurBoundShaderPtr->uniform1f(sProbeIntensity, probe_intensity);
    LLHLSLShader::sCurBoundShaderPtr->uniform1f(sProbeSaturation, probe_saturation);
    LLHLSLShader::sCurBoundShaderPtr->uniform1f(sProbeContrast, probe_contrast);
    LLHLSLShader::sCurBoundShaderPtr->uniform1f(sProbeBlurLODBias, probe_blur_lod_bias);
    LLHLSLShader::sCurBoundShaderPtr->uniform1f(sProbeAutoParallax, probe_auto_parallax ? 1.f : 0.f);
    LLHLSLShader::sCurBoundShaderPtr->uniform1f(sProbeAmbientMultiplier, probe_ambient_mult);
    LLHLSLShader::sCurBoundShaderPtr->uniform1f(sProbeEqualize, probe_equalize);
    LLHLSLShader::sCurBoundShaderPtr->uniform1f(sProbeOpacity, probe_opacity);
    LLHLSLShader::sCurBoundShaderPtr->uniform1f(sGateGlossReject, gate_gloss_reject);
    LLHLSLShader::sCurBoundShaderPtr->uniform1f(sGateGlossAccept, gate_gloss_accept);
    LLHLSLShader::sCurBoundShaderPtr->uniform1f(sGateMetallicReject, gate_metallic_reject);
    LLHLSLShader::sCurBoundShaderPtr->uniform1f(sGateMetallicAccept, gate_metallic_accept);

    // Upload max_probe_lod here too, not only from LLPipeline::bindDeferredShader() - any shader that
    // reaches sampleProbes()/tapRefMap() via this function alone (e.g. gDeferredPBRAlphaProgram) would
    // otherwise read a never-explicitly-set value.
    static LLStaticHashedString sMaxProbeLOD("max_probe_lod");
    LLHLSLShader::sCurBoundShaderPtr->uniform1f(sMaxProbeLOD, mMaxProbeLOD);
}


void renderReflectionProbe(LLReflectionMap* probe)
{
    if (probe->isRelevant())
    {
        F32* po = probe->mOrigin.getF32ptr();

        //draw orange line from probe to neighbors
        gDX.flush();
        gDX.diffuseColor4f(1, 0.5f, 0, 1);
        gDX.begin(gDX.LINES);
        for (auto& neighbor : probe->mNeighbors)
        {
            if (probe->mViewerObject && neighbor->mViewerObject)
            {
                continue;
            }

            gDX.vertex3fv(po);
            gDX.vertex3fv(neighbor->mOrigin.getF32ptr());
        }
        gDX.end();
        gDX.flush();

        gDX.diffuseColor4f(1, 1, 0, 1);
        gDX.begin(gDX.LINES);
        for (auto& neighbor : probe->mNeighbors)
        {
            if (probe->mViewerObject && neighbor->mViewerObject)
            {
                gDX.vertex3fv(po);
                gDX.vertex3fv(neighbor->mOrigin.getF32ptr());
            }
        }
        gDX.end();
        gDX.flush();
    }

}

void LLSphereProbes::renderDebug()
{
    gDebugProgram.bind();

    for (auto& probe : mProbes)
    {
        renderReflectionProbe(probe);
    }

    gDebugProgram.unbind();
}

bool LLSphereProbes::setRadiancePass(bool radiance)
{
    bool previous = mRadiancePass;
    mRadiancePass = radiance;
    return previous;
}

void LLSphereProbes::bindRadiance(S32 stage)
{
    gDX.getTexUnit(stage)->bindCubeArraySRV(mCubes.radianceSRV());
}

void LLSphereProbes::bindIrradiance(S32 stage)
{
    gDX.getTexUnit(stage)->bindCubeArraySRV(mCubes.irradianceSRV());
}

void LLSphereProbes::unbindProbeCubes(S32 stage)
{
    gDX.getTexUnit(stage)->unbind(LLTexUnit::TT_CUBE_MAP_ARRAY);
}

void LLSphereProbes::initReflectionMaps()
{
    static LLCachedControl<U32> ref_probe_res(gSavedSettings, "RenderReflectionProbeResolution", 256U);
    U32 probe_resolution = nhpo2(llclamp(ref_probe_res(), (U32)64, (U32)512));
    static LLCachedControl<bool> render_hdr(gSavedSettings, "RenderHDREnabled", true);

    // the cube array survives this reset only if nothing about it changes. The default probe's cube (index 0) goes
    // blank with the array, so it may keep its complete state only when the array is kept.
    const bool cubes_kept = mCubes.isAllocated() && mCubes.getResolution() == probe_resolution && mCubes.getCount() == mDynamicProbeCount;

    if (!mCubes.isAllocated() || mReflectionProbeCount != mDynamicProbeCount || mRequestedResolution != probe_resolution || mReset)
    {
        if(mRequestedResolution != probe_resolution)
        {
            mCapture.release();
        }

        gEXRImage = nullptr;
        mReset = false;
        mResetFade = 0.f;
        mReflectionProbeCount = mDynamicProbeCount;
        mRequestedResolution = probe_resolution;
        mProbeResolution = probe_resolution;

        // Reallocates radiance (count + 2 scratch cubes) and irradiance (count cubes) together.
        if (!mCubes.isAllocated() ||
            mCubes.getResolution() != mProbeResolution ||
            mCubes.getCount() != mReflectionProbeCount)
        {
            // The GPU refuses some cube arrays (E_OUTOFMEMORY at 512 px with 256 slots). Step down until one fits,
            // so probes keep sampling at a smaller size instead of going black.
            while (!mCubes.allocate(mProbeResolution, mReflectionProbeCount, render_hdr) && mProbeResolution > 64)
            {
                LL_WARNS("Probes") << "reflection cube array " << mReflectionProbeCount << " slots at " << mProbeResolution
                                   << " px did not fit, trying " << mProbeResolution / 2 << " px" << LL_ENDL;
                mProbeResolution /= 2;
            }

            if (!mCubes.isAllocated())
            {
                LL_WARNS_ONCE("Probes") << "reflection cube array could not be allocated at any size" << LL_ENDL;
            }
        }

        // Read the texture's real allocated mip count rather than computing log2f(mProbeResolution) -
        // DXCubeArrayTexture::create()'s generate_mips=true (MipLevels=0) allocates one more level than
        // that formula assumes for a power-of-two resolution, which would shift the whole
        // roughness<->LOD curve (radianceGenF.hlsl/tapRefMap() via max_probe_lod) by one level and
        // leave the real highest mip permanently unwritten.
        mMaxProbeLOD = (F32)mCubes.radiance().getMipLevels() - 1.f; // number of mips - 1

        // reset probe state
        mUpdatingFace = 0;
        mUpdatingProbe = nullptr;
        mRadiancePass = false;
        mRealtimeRadiancePass = false;

        // if default probe already exists, remember whether or not it's complete (SL-20498)
        bool default_complete = cubes_kept && !mDefaultProbe.isNull() && mDefaultProbe->mComplete;

        for (auto& probe : mProbes)
        {
            probe->mLastUpdateTime = 0.f;
            probe->mComplete = false;
            probe->mProbeIndex = -1;
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

void LLSphereProbes::cleanup()
{
    mVertexBuffer = nullptr;
    mCapture.release();

    mCubes.release();

    mProbes.clear();
    mCreateList.clear();

    mReflectionMaps.clear();
    mUpdatingFace = 0;

    mDefaultProbe = nullptr;
    mUpdatingProbe = nullptr;

    // Also called on every teleport, not just shutdown - destroy() leaves getBuffer()==nullptr, which
    // correctly triggers updateUniforms() to recreate it next time, same as GL's mUBO==0 check.
#ifdef DX_RENDER
    mDXUBO.destroy();
    mBoxProbes.destroy();
#else
    glDeleteBuffers(1, &mUBO);
#endif
    mUBO = 0;

    cleanupQueryPool();

    // note: also called on teleport (not just shutdown), so make sure we're in a good "starting" state
    initCubeFree();
}

void LLSphereProbes::cleanupQueryPool()
{
    if (!mQueryPool.empty())
    {
        std::vector<GLuint> queries(mQueryPool.begin(), mQueryPool.end());
        DXOcclusionQuery::deleteQueries(static_cast<int>(queries.size()), queries.data());
        mQueryPool.clear();
    }
}

void LLSphereProbes::doOcclusion()
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

void LLSphereProbes::setDefaultProbeOnly(bool on)
{
    if (mDefaultProbeOnly != on)
    {
        mDefaultProbeOnly = on;
        updateUniforms();
    }
}

bool LLSphereProbes::probeUsable(const LLReflectionMap* probe) const
{
    if (probe == mDefaultProbe)
    {
        return true;
    }
    if (mDefaultProbeOnly)
    {
        return false;
    }
    // Occlusion does not hide a probe from sampling. A probe's volume can cover a visible surface while its centre
    // is behind walls, and sampling costs only a coverage test. Occlusion still steers which probe updates next.
    return true;
}
