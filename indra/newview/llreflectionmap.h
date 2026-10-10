/**
 * @file llreflectionmap.h
 * @brief LLReflectionMap class declaration
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

#pragma once

#include "llmemory.h"

// maximum number of probes in each uniform block (sphere and box)
#define LL_MAX_REFLECTION_PROBE_COUNT 256

class LLSpatialGroup;
class LLViewerObject;

class alignas(16) LLReflectionMap : public LLRefCount
{
    LL_ALIGN_NEW
public:

    enum class ProbeType
    {
        ALL = 0,
        RADIANCE,
        IRRADIANCE,
        REFLECTION
    };

    // allocate an environment map of the given resolution
    LLReflectionMap();

    ~LLReflectionMap();

    // update this environment map
    // resolution - size of cube map to generate
    void update(U32 resolution, U32 face, bool force_dynamic = false, F32 near_clip = -1.f, bool useClipPlane = false, LLPlane clipPlane = LLPlane(LLVector3(0, 0, 0), LLVector3(0, 0, 1)));

    // for volume partition probes, try to place this probe in the best spot
    void autoAdjustOrigin();

    // return true if given Reflection Map's influence volume intersect's with this one's
    bool intersects(LLReflectionMap* other) const;

    // Get the ambiance value to use for this probe
    F32 getAmbiance() const;

    // Get the near clip plane distance to use for this probe
    F32 getNearClip() const;

    // Return true if this probe should include avatars in its reflection map
    bool getIsDynamic() const;

    // get the encoded bounding box of this probe's influence volume
    // will only return a box if this probe is associated with a VOVolume
    // with its reflection probe influence volume to to VOLUME_TYPE_BOX
    // return false if no bounding box (treat as sphere influence volume)
    bool getBox(LLMatrix4& box);

    // return true if this probe is active for rendering
    bool isActive() const;

    // true if this probe is a manual probe whose object volume is a box (see LLBoxProbes)
    bool isBoxVolume() const;

    // copies the viewer object's position, radius and box extent into this probe
    // (manual probes only; no-op otherwise)
    void trackViewerObject();

    // sphere probes centre on the object's visible bounding box, not its pivot (position).
    // Falls back to the position when no extents exist yet, and is not used for box volumes.
    void setOriginFromViewerObject();
    void computeOriginFromViewerObject();

    // perform occlusion query/readback
    void doOcclusion(const LLVector4a& eye);

    // return false if this probe isn't currently relevant (for example, disabled due to graphics preferences)
    bool isRelevant() const;

    // point at which environment map was last generated from (in agent space)
    LLVector4a mOrigin;

    // distance from main viewer camera
    F32 mDistance = -1.f;

    // Minimum and maximum depth in current render camera
    F32 mMinDepth = -1.f;
    F32 mMaxDepth = -1.f;

    // radius of this probe's affected area
    F32 mRadius = 16.f;

    // mRadius is a single isotropic scalar (diagonal half-length) - correct for sphere/automatic
    // probes' occlusion proxy, but wrong for box probes (a non-cubic room needs its real per-axis
    // half-extent, not an isotropic cube, or occlusion queries against the shorter axis are
    // unreliable). mBoxExtent stores that per-axis extent for box probes only; stays 0 for
    // sphere/automatic probes where mRadius,mRadius,mRadius is already correct.
    LLVector4a mBoxExtent;

    // last time this probe was updated (or when its update timer got reset)
    F32 mLastUpdateTime = 0.f;

    // last time this probe was bound for rendering

    // slot in the probe cube store, or -1 if not stored there
    S32 mCubeIndex = -1; // index into cube map array or -1 if not currently stored in cube map array

    // probe has had at least one full update and is ready to render
    bool mComplete = false;

    // Origin the current cube was captured from. The shader uploads this, so parallax matches the capture.
    LLVector4a mCaptureOrigin;

    // Object-to-agent transform (with the box scale) the current box cube was captured with.
    LLMatrix4 mCaptureWorld;
    bool mCaptureWorldValid = false;
    // Records origin, radius and box transform at the start of a capture into pending fields. They become the
    // sampled values only when the new cube completes (commitCapture), so the old cube keeps its own geometry.
    void captureGeometry();
    void commitCapture();
    F32 mCaptureRadius = 0.f;
    LLVector4a mPendingOrigin;
    F32 mPendingRadius = 0.f;
    LLMatrix4 mPendingWorld;
    bool mPendingWorldValid = false;

    // moved, resized or re-shaped since the live cube was captured. Still sampled until the recapture completes.
    bool mStale = false;

    // Origin and radius the cube was captured with. Live values only while no complete capture exists.
    const LLVector4a& sampleOrigin() const { return mComplete ? mCaptureOrigin : mOrigin; }
    F32 sampleRadius() const { return mComplete ? mCaptureRadius : mRadius; }

    bool getCaptureBox(LLMatrix4& box);
    bool getBoxWorld(LLMatrix4& world) const;
    bool captureTransformChanged() const;

    // fade in parameter for this probe
    F32 mFadeIn = 0.f;

    // index into array packed by LLSphereProbes::getReflectionMaps
    // WARNING -- only valid immediately after call to getReflectionMaps
    S32 mProbeIndex = -1;

    // set of any LLReflectionMaps that intersect this map (maintained by LLSphereProbes
    std::vector<LLReflectionMap*> mNeighbors;

    // spatial group this probe is tracking (if any)
    LLSpatialGroup* mGroup = nullptr;

    // viewer object this probe is tracking (if any)
    LLPointer<LLViewerObject> mViewerObject;

    // grid cell probe (RenderReflectionCellGrid): no owning object or group, full-scene capture
    bool mCell = false;

    // what priority should this probe have (higher is higher priority)
    // currently only 0 or 1
    // 0 - automatic probe
    // 1 - manual probe
    U32 mPriority = 0;

    // occlusion culling state
    GLuint mOcclusionQuery = 0;
    bool mOccluded = false;

    ProbeType mType;
};

