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
// D3D11 has no TRIANGLE_FAN topology (see llvieweroctree.cpp's dx_get_occlusion_box_vb()), so this
// probe's occlusion proxy-box draw uses the same triangle-list VB LLOcclusionCullingGroup uses, not
// gPipeline.mCubeVB directly.
class LLVertexBuffer;
extern LLVertexBuffer* dx_get_occlusion_box_vb();
extern U32 get_box_triangle_offset(LLCamera* camera, const LLVector4a& center);
#endif

LLReflectionMap::LLReflectionMap()
{
    mCaptureOrigin.splat(0.f);
    mPendingOrigin.splat(0.f);
}

LLReflectionMap::~LLReflectionMap()
{
    if (mOcclusionQuery)
    {
        gPipeline.mSphereProbes.recycleQuery(mOcclusionQuery);
        mOcclusionQuery = 0;
    }
}

void LLReflectionMap::update(U32 resolution, U32 face, bool force_dynamic, F32 near_clip, bool useClipPlane, LLPlane clipPlane)
{
    if (mCubeIndex < 0)
        return;

    mLastUpdateTime = gFrameTimeSeconds;
    //llassert(LLPipeline::sRenderDeferred);


    F32 clip = (near_clip > 0) ? near_clip : getNearClip();
    bool dynamic = force_dynamic || getIsDynamic();

    // Exclude the whole linkset: markVisible compares root objects, and a child's root is the linkset root.
    LLPipeline::sCaptureExcludeObject = mViewerObject ? mViewerObject->getRootEdit() : nullptr;
    gViewerWindow->cubeSnapshot(LLVector3(mOrigin), resolution, face, clip, dynamic, useClipPlane, clipPlane);
    LLPipeline::sCaptureExcludeObject = nullptr;
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
                // ignore_visibility=true: this ray-cast needs real geometric presence, not isVisible()
                // (was this in the camera frustum this exact frame) - see
                // LLOctreeIntersect::check(LLViewerOctreeEntry*) in llspatialpartition.cpp. Without it,
                // a wall outside the current view reads as empty space, misplacing the probe origin.
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
            mBoxExtent.splat(mRadius); // automatic probe, isotropic - see mBoxExtent's own header comment.

            // make sure near clip doesn't poke through ground
            F32 z_before = fp[2];
            fp[2] = llmax(fp[2], height+mRadius*0.5f);

            // lifting the origin moves the sphere off the group's corners, so grow the radius by the same amount
            mRadius += fp[2] - z_before;
            mBoxExtent.splat(mRadius);
        }
    }
    else if (mViewerObject && !mViewerObject->isDead())
    {
        mPriority = 1;
        setOriginFromViewerObject();

        if (mViewerObject->getVolume() && ((LLVOVolume*)mViewerObject.get())->getReflectionProbeIsBox())
        {
            LLVector3 s = mViewerObject->getScale().scaledVec(LLVector3(0.5f, 0.5f, 0.5f));
            mRadius = s.magVec();
            mBoxExtent.load3(s.mV); // real per-axis half-extent, see mBoxExtent's own header comment.
        }
        else
        {
            mRadius = mViewerObject->getScale().mV[0] * 0.5f;
            mBoxExtent.splat(mRadius); // sphere probe, isotropic is correct here.
        }
    }
}

bool LLReflectionMap::intersects(LLReflectionMap* other) const
{
    LLVector4a delta;
    delta.setSub(other->sampleOrigin(), sampleOrigin());

    F32 dist = delta.dot3(delta).getF32();

    F32 r2 = sampleRadius() + other->sampleRadius();

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
        // mRadius here is the room's diagonal/corner-distance size (autoAdjustOrigin()'s
        // ray-cast-to-8-corners logic), dominated by the room's widest dimension - mRadius*0.5
        // unconditionally can exceed the real vertical distance to the floor for a wide/long room with
        // a modest ceiling, near-clipping the floor out of the downward-facing capture pass while
        // walls (farther away horizontally) still capture fine. Cap at 1m, matching the terrain-probe
        // branch's own default below.
        ret = llmin(mRadius * 0.5f, 1.f);
    }
    else if (mCell)
    {
        // the cell and master centre sits on the avatar. A 1 m clip removes the avatar's own body from its cube.
        ret = 0.f;
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
    if (detail() > (S32)LLSphereProbes::DetailLevel::STATIC_ONLY &&
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
            mBoxExtent.load3(s.mV); // real per-axis half-extent, see mBoxExtent's own header comment.
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

// Object to agent transform of a box volume, including its half-extent scale. Same transform getBox() uses.
bool LLReflectionMap::getBoxWorld(LLMatrix4& world) const
{
    if (!isBoxVolume() || !mViewerObject->mDrawable)
    {
        return false;
    }

    LLVector3 s = mViewerObject->getScale().scaledVec(LLVector3(0.5f, 0.5f, 0.5f));
    glm::mat4 rm(glm::make_mat4((F32*)mViewerObject->mDrawable->getWorldMatrix().mMatrix));
    glm::mat4 m = rm * glm::scale(glm::vec3(s));
    world = LLMatrix4(glm::value_ptr(m));
    return true;
}

void LLReflectionMap::captureGeometry()
{
    mPendingOrigin = mOrigin;
    mPendingRadius = mRadius;
    mPendingWorldValid = getBoxWorld(mPendingWorld);
}

void LLReflectionMap::commitCapture()
{
    mCaptureOrigin = mPendingOrigin;
    mCaptureRadius = mPendingRadius;
    mCaptureWorld = mPendingWorld;
    mCaptureWorldValid = mPendingWorldValid;
    mStale = false;
}

// True when a box's scale or rotation no longer matches the transform its cube was captured with.
bool LLReflectionMap::captureTransformChanged() const
{
    LLMatrix4 world;
    if (!mCaptureWorldValid || !getBoxWorld(world))
    {
        return false;
    }

    const F32* a = (const F32*)world.mMatrix;
    const F32* b = (const F32*)mCaptureWorld.mMatrix;
    for (S32 i = 0; i < 16; ++i)
    {
        F32 d = a[i] - b[i];
        if (d > 1e-4f || d < -1e-4f)
        {
            return true;
        }
    }
    return false;
}

// The box as the camera sees it, built from the transform the cube was captured with (not the live one),
// so the parallax box and the cube it samples describe the same box. Only valid after a capture.
bool LLReflectionMap::getCaptureBox(LLMatrix4& box)
{
    if (!mCaptureWorldValid)
    {
        return false;
    }

    glm::mat4 world(glm::make_mat4((F32*)mCaptureWorld.mMatrix));
    glm::mat4 mv = glm::inverse(glm::mat4(get_current_modelview()) * world);
    box = LLMatrix4(glm::value_ptr(mv));
    return true;
}

bool LLReflectionMap::isActive() const
{
    return mCubeIndex != -1;
}

bool LLReflectionMap::isBoxVolume() const
{
    return mViewerObject && mViewerObject->getVolume() && mViewerObject->getReflectionProbeIsBox();
}

void LLReflectionMap::setOriginFromViewerObject()
{
    if (!mViewerObject || gPipeline.mSphereProbes.isCapturing(this))
    {
        return; // the origin is frozen for the whole capture
    }

    computeOriginFromViewerObject();

    // a moved, rescaled or rotated manual probe has a stale cube: recapture it.
    // Compared with the capture origin, so slow drift cannot accumulate under the threshold.
    LLVector4a moved;
    moved.setSub(mOrigin, mCaptureOrigin);
    bool resized = mCaptureRadius - mRadius > 1e-4f || mRadius - mCaptureRadius > 1e-4f;
    if (mComplete && !mStale && (moved.getLength3().getF32() > 0.1f || resized || captureTransformChanged()))
    {
        // stale, not incomplete: the old cube and its captured geometry stay sampled until the recapture commits.
        // Zeroing the update time puts the probe first in line for that recapture.
        mStale = true;
        mLastUpdateTime = 0.f;
    }
}

void LLReflectionMap::computeOriginFromViewerObject()
{
    if (!mViewerObject)
    {
        return;
    }

    mOrigin.load3(mViewerObject->getPositionAgent().mV);

    // Box probes keep the position: their box transform is built around the object's pivot.
    if (isBoxVolume())
    {
        return;
    }

    LLDrawable* drawable = mViewerObject->mDrawable;
    if (!drawable)
    {
        return;
    }

    const LLVector4a* exts = drawable->getSpatialExtents();
    LLVector4a size;
    size.setSub(exts[1], exts[0]);
    if (size.getLength3().getF32() <= 0.f)
    {
        return; // extents not computed yet
    }

    // Inactive drawables' face extents are already agent-space (LLFace::genVolumeBBoxes applies the region
    // offset), so no offset is added here. Active drawables' extents are object-local and are not corrected yet.
    LLVector4a lo = exts[0];
    LLVector4a hi = exts[1];

    LLVector4a centre;
    centre.setAdd(lo, hi);
    centre.mul(0.5f);
    if (drawable->isActive())
    {
        // Active extents are in the root object's local frame (the face boxes use the child's relative transform,
        // which already contains its offset within the root). Take the centre through the root's transform,
        // not the child's pivot, so the child position is not counted twice.
        LLViewerObject* root = mViewerObject->getRootEdit();
        LLVector3 local(centre.getF32ptr());
        local = local * root->getRenderRotation();
        LLVector4a base;
        base.load3(root->getPositionAgent().mV);
        LLVector4a offset;
        offset.load3(local.mV);
        mOrigin.setAdd(base, offset);
    }
    else
    {
        mOrigin.load3(centre.getF32ptr());
    }
}

void LLReflectionMap::trackViewerObject()
{
    if (!mViewerObject || !mViewerObject->getVolume())
    {
        return;
    }

    LLVOVolume* vobj = (LLVOVolume*)mViewerObject.get();

    setOriginFromViewerObject();

    if (vobj->getReflectionProbeIsBox())
    {
        LLVector3 s = vobj->getScale().scaledVec(LLVector3(0.5f, 0.5f, 0.5f));
        mRadius = s.magVec();
        mBoxExtent.load3(s.mV); // real per-axis half-extent, see mBoxExtent's own header comment.
    }
    else
    {
        mRadius = mViewerObject->getScale().mV[0] * 0.5f;
        mBoxExtent.splat(mRadius); // sphere probe, isotropic is correct here.
    }

    // mPriority is the only source of the manual-vs-automatic classification. Set it here so a
    // manual probe is classified correctly from the moment it exists, not only after its first capture.
    mPriority = 1;
}

bool LLReflectionMap::isRelevant() const
{
    static LLCachedControl<S32> RenderReflectionProbeLevel(gSavedSettings, "RenderReflectionProbeLevel", 0);
    static LLCachedControl<bool> RenderReflectionBoxProbesEnabled(gSavedSettings, "RenderReflectionBoxProbesEnabled", true);

    // grid cells and the master cube are always relevant: they have no object to switch them off with
    if (mCell)
    {
        return true;
    }

    if (!RenderReflectionBoxProbesEnabled && isBoxVolume())
    {
        return false;
    }

    // Level 0: default probe only. Level 1: manual probes. Level 2: manual plus environment probes
    // (automatic probes outside any object group). Level 3: everything.
    if (mViewerObject)
    {
        return RenderReflectionProbeLevel > 0;
    }

    switch (RenderReflectionProbeLevel)
    {
        case 3: return true;
        case 2: return !mGroup;
        default: return false;
    }
}

// Non-blocking occlusion query handling - avoids stalling the GPU by waiting for results.

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
        mOcclusionQuery = gPipeline.mSphereProbes.allocateQuery();

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
        // Non-blocking: read the previous query only when its result is ready, never stall on the GPU.
        if (DXOcclusionQuery::isResultAvailable(mOcclusionQuery))
        {
            GLuint samples_passed = (GLuint)llmin(DXOcclusionQuery::getResult(mOcclusionQuery), (unsigned long long)0xFFFFFFFFu);
            mOccluded = (samples_passed == 0);
            should_query = true;
        }
    }

    if (!should_query)
    {
        return;
    }

    DXOcclusionQuery::beginQuery(mOcclusionQuery);

    LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr;
    if (shader)
    {
        shader->uniform3fv(LLShaderMgr::BOX_CENTER, 1, mOrigin.getF32ptr());
        // mRadius is an isotropic diagonal half-length - correct for parallax-correction/weight math
        // but wrong as an occlusion-query proxy size for a box probe (draws a cube far larger than the
        // room in its shorter axis). mBoxExtent holds the real per-axis half-extent instead (see its
        // header comment); still mRadius,mRadius,mRadius for sphere/automatic probes.
        // Matches the established, proven-correct convention
        // LLOcclusionCullingGroup already uses for spatial-partition
        // occlusion (llvieweroctree.cpp - real per-axis bounds, never a
        // collapsed radius).
        shader->uniform3f(LLShaderMgr::BOX_SIZE, mBoxExtent.getF32ptr()[0], mBoxExtent.getF32ptr()[1], mBoxExtent.getF32ptr()[2]);

        dx_get_occlusion_box_vb()->setBuffer();
        dx_get_occlusion_box_vb()->drawArrays(
            LLRender::TRIANGLES,
            get_box_triangle_offset(LLViewerCamera::getInstance(), mOrigin),
            18);
    }
    else
    {
        mOccluded = false;
    }

    DXOcclusionQuery::endQuery(mOcclusionQuery);
}
