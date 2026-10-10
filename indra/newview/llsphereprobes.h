/**
 * @file llsphereprobes.h
 * @brief LLSphereProbes class declaration
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

#include "llreflectionmap.h"
#include "llrendertarget.h"
#include "llprobecapture.h"
#include "llprobecube.h"
#include "llboxprobes.h"

#include <array>
#include <map>

#ifdef DX_RENDER
#include "DXBuffer.h"
#endif

class LLSpatialGroup;
class LLViewerObject;

// reflection probe resolution
#define LL_IRRADIANCE_MAP_RESOLUTION 16

void renderReflectionProbe(LLReflectionMap* probe);

class alignas(16) LLSphereProbes
{
    LL_ALIGN_NEW
public:
    // mLightScale/updateUniforms() are private (LLPipeline-only friend) - DXPipeline needs both
    // (to darken local lights during probe capture, and to trigger the once-per-frame rebuild
    // LLPipeline::renderGeomDeferred() does under GL) but can't reach the GL call site under
    // DX_RENDER. Scoped getter/forwarder is a smaller surface than a second full friend class.
    F32 getLightScale() const { return mLightScale; }


    void updateUniformsPerFrame() { updateUniforms(); }

    enum class DetailLevel
    {
        STATIC_ONLY = 0,
        STATIC_AND_DYNAMIC,
        REALTIME = 2
    };

    // General guidance for UBOs is to statically allocate all of these fields to make your life ever so slightly easier.
    // Then set a "max" value for the number of probes you'll ever have, and use that to index into the arrays.
    // We do this with refmapCount.  The shaders will just pick up on it there.
    // This data structure should _always_ match what's in class3/deferred/reflectionProbeF.glsl.
    // The shader can and will break otherwise.
    // -Geenz 2025-03-10
    struct ReflectionProbeData
    {
        LLMatrix4 heroBox;

        // for sphere probes, origin (xyz) and radius (w) of refmaps in clip space
        LLVector4 refSphere[LL_MAX_REFLECTION_PROBE_COUNT];

        // extra parameters
        //  x - irradiance scale
        //  y - radiance scale
        //  z - fade in
        //  w - znear
        LLVector4 refParams[LL_MAX_REFLECTION_PROBE_COUNT];

        LLVector4 heroSphere;

        // indices used by probe:
        //  [i][0] - cubemap array index for this probe
        //  [i][1] - index into "refNeighbor" for probes that intersect this probe
        //  [i][2] - number of probes  that intersect this probe, or -1 for no neighbors
        //  [i][3] - priority (probe type stored in sign bit - positive for spheres, negative for boxes)
        GLint refIndex[LL_MAX_REFLECTION_PROBE_COUNT][4];

        // list of neighbor indices
        GLint refNeighbor[4096];

        GLint refBucket[256][4]; // lookup table for which index to start with for the given Z depth
        // numbrer of active refmaps
        GLint refmapCount;

        GLint heroShape;
        GLint heroMipCount;
        GLint heroProbeCount;
    };

    // allocate an environment map of the given resolution
    LLSphereProbes();

    // release any GL state
    void cleanup();
    void cleanupQueryPool();

    // maintain reflection probes
    void update();

    void refreshSettings();

    // add a probe for the given spatial group
    LLReflectionMap* addProbe(LLSpatialGroup* group = nullptr);

    // Populate "maps" with the N most relevant Reflection Maps where N is no more than maps.size()
    // If less than maps.size() ReflectionMaps are available, will assign trailing elements to nullptr.
    //  maps -- presized array of Reflection Map pointers
    void getReflectionMaps(std::vector<LLReflectionMap*>& maps);

    // called by LLSpatialGroup constructor
    // If spatial group should receive a Reflection Probe, will create one for the specified spatial group
    LLReflectionMap* registerSpatialGroup(LLSpatialGroup* group);

    // presently hacked into LLViewerObject::setTE
    // Used by LLViewerObjects that are Reflection Probes
    // vobj must not be null
    // Guaranteed to not return null
    LLReflectionMap* registerViewerObject(LLViewerObject* vobj);

    // reset all state on the next update
    void reset();

    // pause all updates other than the default probe
    // duration - number of seconds to pause (default 10)
    void pause(F32 duration = 10.f);

    // unpause (see pause)
    void resume();

    // called on region crossing to "shift" probes into new coordinate frame
    void shift(const LLVector4a& offset);

    // debug display, called from llspatialpartition if reflection
    // probe debug display is active
    void renderDebug();

    // Sets whether the next capture is a radiance pass (lights at full strength) or an irradiance
    // pass. Returns the previous value, so an external capture (mirrors) can restore it afterwards.
    bool setRadiancePass(bool radiance);

    // Probe cube storage. Bind helpers bind the radiance and irradiance arrays to a texture stage.
    bool hasProbeCubes() const { return mCubes.isAllocated(); }
    void bindRadiance(S32 stage);
    void bindIrradiance(S32 stage);
    void unbindProbeCubes(S32 stage);

    // call once at startup to allocate cubemap arrays
    void initReflectionMaps();

    // True if currently updating a radiance map, false if currently updating an irradiance map
    bool isRadiancePass() { return mRadiancePass; }

    // perform occlusion culling on all active reflection probes
    void doOcclusion();

    // True while this probe's faces are being rendered. Its origin is frozen for the whole capture.
    bool isCapturing(const LLReflectionMap* probe) const { return mUpdatingProbe == probe; }

    // Limits the uniforms to the default probe (used by material previews). Off by default.
    void setDefaultProbeOnly(bool on);

    // True if a probe may be sampled: the default probe always, others unless default-only is set
    // or an occlusion query has found them hidden.
    bool probeUsable(const LLReflectionMap* probe) const;

    U32 probeCount();
    U32 probeMemory();

    // glDeleteQueries is expensive, so we maintain a pool of queries
    GLuint allocateQuery();
    void recycleQuery(GLuint query);

private:
    friend class LLPipeline;
    friend class LLMirrorProbes; // renamed to LLMirrorProbes in the mirror port

    // initialize mCubeFree array to default values
    void initCubeFree();

    // Just does a bulk clear of all of the cubemaps.
    void clearCubeMaps();

    // delete the probe with the given index in mProbes
    void deleteProbe(U32 i);

    // get a free cube index
    // returns -1 if allocation failed
    S32 allocateCubeIndex();

    // update the neighbors of the given probe
    void updateNeighbors(LLReflectionMap* probe);

    // update UBO used for rendering (call only once per render pipe flush)
    void updateUniforms();

    // bind UBO used for rendering
    void setUniforms();

    std::deque<GLuint>                                    mQueryPool;

    // face capture: super-sample blur, mip chain and copy into the scratch cube
    LLProbeCapture mCapture;

    // box-volume probes: own packing and uniform block, indexed separately from sphere probes
    LLBoxProbes mBoxProbes;



    // vertex buffer for pushing verts to filter shaders
    LLPointer<LLVertexBuffer> mVertexBuffer;

    // radiance and irradiance cube storage
    LLProbeCubeStore mCubes;

    // list of free cubemap indices
    std::list<S32> mCubeFree;

    // perform an update on the currently updating Probe
    void doProbeUpdate();

    // update the specified face of the specified probe
    void updateProbeFace(LLReflectionMap* probe, U32 face);

    // list of active reflection maps
    std::vector<LLPointer<LLReflectionMap> > mProbes;

    // list of reflection maps to kill

    // list of reflection maps to create
    std::vector<LLPointer<LLReflectionMap> > mCreateList;

    // handle to UBO
    U32 mUBO = 0;
#ifdef DX_RENDER
    // Real D3D11 constant buffer backing mUBO's data (mProbeData/ReflectionProbeData) - see
    // updateUniforms()/setUniforms() in llsphereprobes.cpp. mUBO itself stays 0 under
    // DX_RENDER.
    DXBuffer mDXUBO;
#endif

    // list of maps being used for rendering
    std::vector<LLReflectionMap*> mReflectionMaps;

    LLReflectionMap* mUpdatingProbe = nullptr;
    // True only while the realtime probe is updated. Selects the secondary scratch slot explicitly,
    // instead of comparing against mUpdatingProbe, which can still hold last frame's probe.
    bool mRealtimeUpdate = false;
    U32 mUpdatingFace = 0;

    // if true, we're generating the radiance map for the current probe, otherwise we're generating the irradiance map.
    // Update sequence should be to generate the irradiance map from render of the world that has no irradiance,
    // then generate the radiance map from a render of the world that includes irradiance.
    // This should avoid feedback loops and ensure that the colors in the radiance maps match the colors in the environment.
    bool mRadiancePass = false;

    // same as above, but for the realtime probe.
    // Realtime probes should update all six sides of the irradiance map on "odd" frames and all six sides of the
    // radiance map on "even" frames.
    bool mRealtimeRadiancePass = false;

    LLPointer<LLReflectionMap> mDefaultProbe;  // default reflection probe to fall back to for pixels with no probe influences (should always be at cube index 0)

    // number of reflection probes to use for rendering
    U32 mReflectionProbeCount;

    U32 mDynamicProbeCount;
    bool mDefaultProbeOnly = false;

    // cached settings from gSavedSettings
    S32 mRenderReflectionProbeDetail = -1;
    S32 mRenderReflectionProbeLevel = 0;
    U32 mRenderReflectionProbeCount = 256U;

    // resolution of reflection probes
    U32 mProbeResolution = 128;

    // resolution the setting asks for; mProbeResolution can be lower when the GPU cannot hold the requested array
    U32 mRequestedResolution = 0;

    // grid cells around the camera (RenderReflectionCellGrid). Keys are integer cell coordinates.
    std::map<std::array<S32, 3>, LLPointer<LLReflectionMap>> mCellProbes;
    void updateCellGrid(const LLVector4a& camera_pos);

    // one realtime cube centred on the avatar (RenderReflectionMasterCube), recaptured every frame
    LLPointer<LLReflectionMap> mMasterProbe;
    void updateMasterCube();

    // maximum LoD of reflection probes (mip levels - 1)
    F32 mMaxProbeLOD = 6.f;

    // amount to scale local lights during an irradiance map update (set during updateProbeFace and used by LLPipeline)
    F32 mLightScale = 1.f;

    // if true, reset all probe render state on the next update (for teleports and sky changes)
    bool mReset = false;

    float mResetFade = 1.f;

    // if true, only update the default probe
    bool mPaused = false;
    F32 mResumeTime = 0.f;

    ReflectionProbeData mProbeData;
};

