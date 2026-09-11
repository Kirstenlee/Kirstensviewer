/**
 * @file llreflectionmap.cpp
 * @brief LLReflectionMap class implementation
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

#include "llreflectionmap.h"
#include "pipeline.h"
#include "llviewerwindow.h"
#include "llviewerregion.h"
#include "llworld.h"
#include "llshadermgr.h"

#ifdef DX_RENDER
#include "DXOcclusionQuery.h"
#endif

extern F32SecondsImplicit gFrameTimeSeconds;

extern U32 get_box_fan_indices(LLCamera* camera, const LLVector4a& center);

#ifdef DX_RENDER
// S24 (2026-08-19, task #250): see llvieweroctree.cpp's dx_get_occlusion_box_vb()
// comment - D3D11 has no TRIANGLE_FAN topology, so this probe's occlusion
// proxy-box draw needs the same triangle-list VB LLOcclusionCullingGroup
// already uses, not gPipeline.mCubeVB directly.
class LLVertexBuffer;
extern LLVertexBuffer* dx_get_occlusion_box_vb();
extern U32 get_box_triangle_offset(LLCamera* camera, const LLVector4a& center);
#endif

LLReflectionMap::LLReflectionMap()
{
}

LLReflectionMap::~LLReflectionMap()
{
    if (mOcclusionQuery)
    {
        gPipeline.mReflectionMapManager.recycleQuery(mOcclusionQuery);
        mOcclusionQuery = 0;
    }
}

void LLReflectionMap::update(U32 resolution, U32 face, bool force_dynamic, F32 near_clip, bool useClipPlane, LLPlane clipPlane)
{
    if (!mCubeArray.notNull())
        return;

    mLastUpdateTime = gFrameTimeSeconds;
    llassert(mCubeArray.notNull());
    llassert(mCubeIndex != -1);
    //llassert(LLPipeline::sRenderDeferred);

    // make sure we don't walk off the edge of the render target
    while (resolution > gPipeline.mRT->deferredScreen.getWidth() ||
        resolution > gPipeline.mRT->deferredScreen.getHeight())
    {
        resolution /= 2;
    }

    F32 clip = (near_clip > 0) ? near_clip : getNearClip();
    bool dynamic = force_dynamic || getIsDynamic();

    gViewerWindow->cubeSnapshot(LLVector3(mOrigin), mCubeArray, mCubeIndex, face, clip, dynamic, useClipPlane, clipPlane);
}

void LLReflectionMap::autoAdjustOrigin()
{


    if (mGroup && !mComplete && !mGroup->hasState(LLViewerOctreeGroup::DEAD))
    {
        const LLVector4a* bounds = mGroup->getBounds();
        auto* node = mGroup->getOctreeNode();
        LLSpatialPartition* part = mGroup->getSpatialPartition();

        if (part && part->mPartitionType == LLViewerRegion::PARTITION_VOLUME)
        {
            mPriority = 0;
            // cast a ray towards 8 corners of bounding box
            // nudge origin towards center of empty space

            if (!node)
            {
                return;
            }

            mOrigin = bounds[0];

            LLVector4a size = bounds[1];

            LLVector4a corners[] =
            {
                { 1, 1, 1 },
                { -1, 1, 1 },
                { 1, -1, 1 },
                { -1, -1, 1 },
                { 1, 1, -1 },
                { -1, 1, -1 },
                { 1, -1, -1 },
                { -1, -1, -1 }
            };

            for (int i = 0; i < 8; ++i)
            {
                corners[i].mul(size);
                corners[i].add(bounds[0]);
            }

            LLVector4a extents[2];
            extents[0].setAdd(bounds[0], bounds[1]);
            extents[1].setSub(bounds[0], bounds[1]);

            bool hit = false;
            for (int i = 0; i < 8; ++i)
            {
                int face = -1;
                LLVector4a intersection;
                // S24 (2026-09-06, task #271): ignore_visibility=true - see
                // LLOctreeIntersect::check(LLViewerOctreeEntry*)'s comment
                // (llspatialpartition.cpp) for why this ray-cast needs real
                // geometric presence, not "was this in the avatar's camera
                // frustum this exact frame" (isVisible() - a wall simply
                // outside the current view reads as empty space otherwise,
                // live-confirmed to place an automatic probe's origin
                // entirely outside its building).
                LLDrawable* drawable = mGroup->lineSegmentIntersect(bounds[0], corners[i], false, false, true, true, &face, &intersection, nullptr, nullptr, nullptr, true);
                if (drawable != nullptr)
                {
                    hit = true;
                    update_min_max(extents[0], extents[1], intersection);
                }
                else
                {
                    update_min_max(extents[0], extents[1], corners[i]);
                }
            }

            if (hit)
            {
                mOrigin.setAdd(extents[0], extents[1]);
                mOrigin.mul(0.5f);
            }

            // make sure origin isn't under ground
            F32* fp = mOrigin.getF32ptr();
            LLVector3 origin(fp);
            F32 height = LLWorld::instance().resolveLandHeightAgent(origin) + 2.f;
            fp[2] = llmax(fp[2], height);

            // make sure radius encompasses all objects
            LLSimdScalar r2 = 0.0;
            for (int i = 0; i < 8; ++i)
            {
                LLVector4a v;
                v.setSub(corners[i], mOrigin);

                LLSimdScalar d = v.dot3(v);

                if (d > r2)
                {
                    r2 = d;
                }
            }

            mRadius = llmax(sqrtf(r2.getF32()), 8.f);
            mBoxExtent.splat(mRadius); // S24 (task #271): automatic probe, isotropic - see mBoxExtent's own header comment.

            // make sure near clip doesn't poke through ground
            fp[2] = llmax(fp[2], height+mRadius*0.5f);

        }
    }
    else if (mViewerObject && !mViewerObject->isDead())
    {
        mPriority = 1;
        mOrigin.load3(mViewerObject->getPositionAgent().mV);

        if (mViewerObject->getVolume() && ((LLVOVolume*)mViewerObject.get())->getReflectionProbeIsBox())
        {
            LLVector3 s = mViewerObject->getScale().scaledVec(LLVector3(0.5f, 0.5f, 0.5f));
            mRadius = s.magVec();
            mBoxExtent.load3(s.mV); // S24 (task #271): real per-axis half-extent, see mBoxExtent's own header comment.
        }
        else
        {
            mRadius = mViewerObject->getScale().mV[0] * 0.5f;
            mBoxExtent.splat(mRadius); // S24 (task #271): sphere probe, isotropic is correct here.
        }
    }
}

bool LLReflectionMap::intersects(LLReflectionMap* other) const
{
    LLVector4a delta;
    delta.setSub(other->mOrigin, mOrigin);

    F32 dist = delta.dot3(delta).getF32();

    F32 r2 = mRadius + other->mRadius;

    r2 *= r2;

    return dist < r2;
}

extern LLControlGroup gSavedSettings;

F32 LLReflectionMap::getAmbiance() const
{
    F32 ret = 0.f;
    if (mViewerObject && mViewerObject->getVolumeConst())
    {
        ret = mViewerObject->getReflectionProbeAmbiance();
    }

    return ret;
}

F32 LLReflectionMap::getNearClip() const
{
    const F32 MINIMUM_NEAR_CLIP = 0.1f;

    F32 ret = 0.f;

    if (mViewerObject && mViewerObject->getVolumeConst())
    {
        ret = mViewerObject->getReflectionProbeNearClip();
    }
    else if (mGroup)
    {
        // S24 (2026-09-06, task #271 - real fix, not a guess: live-confirmed
        // via debug overlay that this exact probe's raw captured content is
        // empty specifically for a downward/floor-facing direction, while
        // the same probe's other directions - walls - capture fine and
        // weighting/selection are both independently confirmed correct).
        // mRadius here is the room's diagonal/corner-distance size
        // (autoAdjustOrigin()'s ray-cast-to-8-corners logic) - dominated by
        // the room's WIDEST (usually horizontal) dimension. Was mRadius*0.5
        // unconditionally - for a typical wide/long room with a modest
        // ceiling height, that can be several meters, easily exceeding the
        // real vertical distance from the probe's position to the floor -
        // near-plane-clipping the floor completely out of the probe's own
        // downward-facing capture pass while walls (much farther away
        // horizontally) remain safely beyond the near clip and capture
        // correctly. Capped at 1m (matching the terrain-probe branch's own
        // existing 1m default just below) - small enough to stay well
        // clear of a typical room's shortest real dimension. Independently
        // re-verified (2026-09-06): autoAdjustOrigin()'s group branch
        // floors mRadius at 8m unconditionally (`llmax(sqrtf(r2), 8.f)`),
        // and registerSpatialGroup() only registers group probes for
        // 15-17m octree nodes in the first place - so mRadius*0.5 was
        // ALWAYS >= 4m for every automatic room probe, not just wide/short
        // ones, and this cap always evaluates to the constant 1.0m in
        // practice (not a graduated scale-down for smaller probes, since
        // there aren't any this small).
        ret = llmin(mRadius * 0.5f, 1.f);
    }
    else
    {
        ret = 1.f; // default to 1m for automatic terrain probes
    }

    return llmax(ret, MINIMUM_NEAR_CLIP);
}

bool LLReflectionMap::getIsDynamic() const
{
    static LLCachedControl<S32> detail(gSavedSettings, "RenderReflectionProbeDetail", 1);
    if (detail() > (S32)LLReflectionMapManager::DetailLevel::STATIC_ONLY &&
        mViewerObject &&
        !mViewerObject->isDead() &&
        mViewerObject->getVolumeConst())
    {
        return mViewerObject->getReflectionProbeIsDynamic();
    }

    return false;
}

bool LLReflectionMap::getBox(LLMatrix4& box)
{
    if (mViewerObject)
    {
        LLVolume* volume = mViewerObject->getVolume();
        if (volume && mViewerObject->getReflectionProbeIsBox())
        {
            glm::mat4 mv(get_current_modelview());
            LLVector3 s = mViewerObject->getScale().scaledVec(LLVector3(0.5f, 0.5f, 0.5f));
            mRadius = s.magVec();
            mBoxExtent.load3(s.mV); // S24 (task #271): real per-axis half-extent, see mBoxExtent's own header comment.
            glm::mat4 scale = glm::scale(glm::vec3(s));
            if (mViewerObject->mDrawable != nullptr)
            {
                // object to agent space (no scale)
                glm::mat4 rm(glm::make_mat4((F32*)mViewerObject->mDrawable->getWorldMatrix().mMatrix));

                // construct object to camera space (with scale)
                mv = mv * rm * scale;

                // inverse is camera space to object unit cube
                mv = glm::inverse(mv);

                box = LLMatrix4(glm::value_ptr(mv));

                return true;
            }
        }
    }

    return false;
}

bool LLReflectionMap::isActive() const
{
    return mCubeIndex != -1;
}

bool LLReflectionMap::isRelevant() const
{
    static LLCachedControl<S32> RenderReflectionProbeLevel(gSavedSettings, "RenderReflectionProbeLevel", 3);

    if (mViewerObject && RenderReflectionProbeLevel > 0)
    { // not an automatic probe
        return true;
    }

    if (RenderReflectionProbeLevel == 3)
    { // all automatics are relevant
        return true;
    }

    if (RenderReflectionProbeLevel == 2)
    { // terrain and water only, ignore probes that have a group
        return !mGroup;
    }

    // no automatic probes, yes manual probes
    return mViewerObject != nullptr;
}

// S24 improved occlusion query handling for reflection probes.
// We want to avoid stalling the GPU by waiting for occlusion query 
// results

void LLReflectionMap::doOcclusion(const LLVector4a& eye)
{
    if (LLHLSLShader::sProfileEnabled)
    {
        return;
    }

    // EXACT Linden behaviour: bounding sphere of bounding cube + 1.f
    const F32 min_occlusion_dist = mRadius * F_SQRT3 + 1.f;

    LLVector4a eye_offset;
    eye_offset.setSub(mOrigin, eye);
    F32 eye_dist = eye_offset.getLength3().getF32();

    // Eye inside influence radius → never occlude
    if (eye_dist < min_occlusion_dist)
    {
        mOccluded = false;
        return;
    }

    bool should_query = false;

    // Allocate query if needed
    if (mOcclusionQuery == 0)
    {
        mOcclusionQuery = gPipeline.mReflectionMapManager.allocateQuery();

        if (mOcclusionQuery == 0)
        {
            // Fail open if allocation fails
            mOccluded = false;
            return;
        }

        should_query = true;
    }
    else
    {
        // Non-blocking check of previous query
        //
        // S24 (2026-08-19, task #182 CTD fix): glGetQueryObjectuiv/glBeginQuery/
        // glEndQuery are extension-loaded function pointers in this codebase
        // (llgl.cpp, populated only via GLH_EXT_GET_PROC_ADDRESS against a real
        // GL context) - unlike glPolygonOffset (a statically-linked core symbol
        // that safely no-ops with no context), these stay nullptr forever under
        // DX_RENDER, so calling them is an immediate null-function-pointer
        // crash, not a silent no-op. This whole doOcclusion() function was
        // unreachable under DX_RENDER until task #182 wired LLPipeline::
        // doOcclusion() into DXPipeline::renderGeomDeferred() - the raw calls
        // here were never exercised before that, then crashed the moment they
        // were. Routed through DXOcclusionQuery (task #245's real
        // D3D11_QUERY_OCCLUSION wrapper, same one LLOcclusionCullingGroup
        // already uses) instead.
#ifdef DX_RENDER
        bool available = DXOcclusionQuery::isResultAvailable(mOcclusionQuery);

        if (available)
        {
            GLuint samples_passed = (GLuint)llmin(DXOcclusionQuery::getResult(mOcclusionQuery), (unsigned long long)0xFFFFFFFFu);
#else
        GLuint available = 0;
        glGetQueryObjectuiv(mOcclusionQuery, GL_QUERY_RESULT_AVAILABLE, &available);

        if (available != 0)
        {
            GLuint samples_passed = 0;
            glGetQueryObjectuiv(mOcclusionQuery, GL_QUERY_RESULT, &samples_passed);
#endif

            mOccluded = (samples_passed == 0);
            mOcclusionPendingFrames = 0;
            should_query = true;
        }
        else
        {
            ++mOcclusionPendingFrames;
        }
    }

    if (!should_query)
    {
        return;
    }

#ifdef DX_RENDER
    DXOcclusionQuery::beginQuery(mOcclusionQuery);
#else
    glBeginQuery(GL_ANY_SAMPLES_PASSED, mOcclusionQuery);
#endif

    LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr;
    if (shader)
    {
        shader->uniform3fv(LLShaderMgr::BOX_CENTER, 1, mOrigin.getF32ptr());
        // S24 (2026-09-05, task #271 - real fix): was mRadius,mRadius,mRadius
        // - for a box-shaped manual probe, mRadius is the DIAGONAL half-
        // length (an isotropic collapse of the box's real, usually
        // anisotropic per-axis scale), correct for parallax-correction/
        // weight math elsewhere but wrong as an occlusion-query proxy size -
        // it draws a cube far larger than the real room in its shorter axis
        // (e.g. ceiling height), likely intersecting unrelated geometry
        // above/below and producing unreliable, frequently-false-occluded
        // query results. mBoxExtent holds the real per-axis half-extent for
        // box probes (mRadius,mRadius,mRadius for sphere/automatic probes,
        // where isotropic is already correct) - see its own header comment.
        // Matches the established, proven-correct convention
        // LLOcclusionCullingGroup already uses for spatial-partition
        // occlusion (llvieweroctree.cpp - real per-axis bounds, never a
        // collapsed radius).
        shader->uniform3f(LLShaderMgr::BOX_SIZE, mBoxExtent.getF32ptr()[0], mBoxExtent.getF32ptr()[1], mBoxExtent.getF32ptr()[2]);

#ifdef DX_RENDER
        dx_get_occlusion_box_vb()->setBuffer();
        dx_get_occlusion_box_vb()->drawArrays(
            LLRender::TRIANGLES,
            get_box_triangle_offset(LLViewerCamera::getInstance(), mOrigin),
            18);
#else
        gPipeline.mCubeVB->drawRange(
            LLRender::TRIANGLE_FAN,
            0,
            7,
            8,
            get_box_fan_indices(LLViewerCamera::getInstance(), mOrigin));
#endif
    }
    else
    {
        mOccluded = false;
    }

#ifdef DX_RENDER
    DXOcclusionQuery::endQuery(mOcclusionQuery);
#else
    glEndQuery(GL_ANY_SAMPLES_PASSED);
#endif
}
