/** 
 * @file pipeline.h
 * @brief Rendering pipeline definitions
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#ifndef LL_PIPELINE_H
#define LL_PIPELINE_H

#include "llcamera.h"
#include "llerror.h"
#include "dxdrawpool.h"
#include "llspatialpartition.h"
#include "m4math.h"
#include "llpointer.h"
#include "dxdrawpoolalpha.h"
#include "dxdrawpoolmaterials.h"
#include "llgl.h"
#include "lldrawable.h"
#include "llrendertarget.h"

#include "DXTexture.h"
#include "llsphereprobes.h"
#include "llmirrorprobes.h"

#include <stack>
#include <functional>
#include <vector>

// Staging list for pipeline-mutating calls made from a DXPool worker thread during
// LLViewerObjectList::update()'s parallel idleUpdate() dispatch (llviewerobjectlist.cpp).
// DXPipeline::markMoved()/markRebuild()/etc push into shared, unlocked containers - not safe to
// call directly off the main thread. Set via DXPipeline::setDeferredMarksForThisThread() before
// dispatching work to a worker thread; each intercepted call defers itself into mActions for later
// serial replay on the main thread.
struct LLDeferredPipelineMarks
{
    std::vector<std::function<void()> > mActions;
};

class LLViewerTexture;
class LLViewerFetchedTexture; // KRLV_TOUCHPOINT-adjacent: @camtextures substitute texture param
class LLFace;
class LLViewerObject;
class LLTextureEntry;
class LLCullResult;
class LLVOAvatar;
class LLVOPartGroup;
class LLHLSLShader;
class DXAlphaDrawPool;
class LLSettingsSky;

typedef enum e_avatar_skinning_method
{
	SKIN_METHOD_SOFTWARE,
	SKIN_METHOD_VERTEX_PROGRAM
} EAvatarSkinningMethod;

bool compute_min_max(LLMatrix4& box, LLVector2& min, LLVector2& max); // Shouldn't be defined here!
// S24: LLRayAABB() fully moved to dxpipeline.h/.cpp (free function, not DXPipeline::) - no stub,
// every caller repointed directly.
bool setup_hud_matrices(); // use whole screen to render hud
bool setup_hud_matrices(const LLRect& screen_region); // specify portion of screen (in pixels) to render hud attachments from (for picking)


// S24: FTM_RENDER_GEOMETRY deleted outright - confirmed genuinely zero-caller tree-wide (same for
// FTM_RENDER_GEOMETRY_DEFERRED/POST_DEFERRED, which were never declared here to begin with).
extern LLTrace::BlockTimerStatHandle FTM_RENDER_INVISIBLE;
extern LLTrace::BlockTimerStatHandle FTM_RENDER_SHINY;
extern LLTrace::BlockTimerStatHandle FTM_RENDER_TERRAIN;
extern LLTrace::BlockTimerStatHandle FTM_RENDER_TREES;
extern LLTrace::BlockTimerStatHandle FTM_RENDER_UI;
extern LLTrace::BlockTimerStatHandle FTM_RENDER_WL_SKY;
extern LLTrace::BlockTimerStatHandle FTM_RENDER_CHARACTERS;
extern LLTrace::BlockTimerStatHandle FTM_RENDER_BUMP;
extern LLTrace::BlockTimerStatHandle FTM_RENDER_FULLBRIGHT;
extern LLTrace::BlockTimerStatHandle FTM_STATESORT;

extern LLTrace::BlockTimerStatHandle FTM_RENDER_UI_3D;
extern LLTrace::BlockTimerStatHandle FTM_RENDER_UI_2D;

class LLPipeline
{
public:
	LLPipeline();
	~LLPipeline();

	// S24: destroyGL()/restoreGL() fully moved to DXPipeline:: - no stub, every caller repointed
	// directly.

	// S24: requestResizeScreenTexture()/requestResizeShadowTexture()/resizeScreenTexture()/
	// resizeShadowTexture()/releaseGLBuffers()/releaseLUTBuffers()/releaseScreenBuffers()/
	// releaseShadowBuffers()/createGLBuffers()/createLUTBuffers()/bakeBrdfLut()/
	// allocateScreenBuffer()/doAllocateScreenBuffer()/allocateScreenBufferInternal()/
	// allocateShadowBuffer()/getBrdfLutAssetPath() fully moved to DXPipeline:: - no stub, every
	// caller repointed directly. The eFBOStatus enum itself stays here (a data type, not logic) -
	// DXPipeline::doAllocateScreenBuffer()'s header declaration widens it to a plain int so
	// dxpipeline.h never needs the complete LLPipeline type.
	typedef enum {
		FBO_SUCCESS_FULLRES = 0,
		FBO_SUCCESS_LOWRES,
		FBO_FAILURE
	} eFBOStatus;

    // S24: rebuildDrawInfo()/rebuildTerrain() fully moved to DXPipeline:: - no stub, every
    // caller repointed directly.

	// S24: profileAvatar()/generateImpostor() fully moved to DXPipeline:: - no stub, every
	// caller repointed directly. resetVertexBuffers() fully moved too; bindScreenToTexture()
	// deleted outright (empty body, zero callers).

	// S24: renderFinalize() fully moved to DXPipeline::presentDeferredScreen() - no stub, every
	// caller repointed directly.
	// S24: copyScreenSpaceReflections()/generateLuminance()/generateExposure()/gammaCorrect()/
	// generateGlow()/applyCAS()/applyFSR1Rcas()/applyFSR1Easu()/applyFSR1RcasToBoundTarget()/
	// applyFXAA()/generateSMAABuffers()/applySMAA()/copyRenderTarget()/applyKrlvCamDrawFog()/
	// combineGlow()/visualizeBuffers() fully moved to DXPipeline:: - no stub, every caller
	// repointed directly.
	// S24: renderDoF() fully moved to DXPipeline:: - no stub, every caller repointed directly.
	// Deliberately excluded from every earlier batch pending a separate DoF investigation
	// (project_dxrender_dof_alpha_mitigation_2026_08_31) - re-verified as of this move that the
	// investigation's own uncommitted experimental code had already been fully reverted back to
	// this exact baseline 46 days prior and nothing had touched it since.

	// S24: init()/cleanup() fully moved to DXPipeline:: - no stub, every caller repointed
	// directly.
	bool isInit() { return mInitialized; };
	// Public (not private, unlike assertInitializedDoError() it calls) - DXPipeline::renderObjects()
	// and its siblings need this same init-guard precondition check as every other LLPipeline caller.
	bool assertInitialized() { const bool is_init = isInit(); if (!is_init) assertInitializedDoError(); return is_init; };
	// S24: skipRenderingShadows() fully moved to DXPipeline:: - no stub, every caller
	// (generateSunShadow(), handleShadowDetailChanged()) repointed directly.

	// S24: findPool()/getPool()/getPoolFromTE()/addPool()/removePool()/getPoolTypeFromTE() fully
	// moved to DXPipeline:: - no stub, every caller repointed directly.

	// S24: allocDrawable()/unlinkDrawable()/markShift()/markTextured()/removeMutedAVsLights()
	// fully moved to DXPipeline:: - no stub, every caller repointed directly.

	// S24: markVisible()/markOccluder()/markNotCulled()/doOcclusion() fully moved to
	// DXPipeline:: - no stub, every caller repointed directly.

	// S24: markMoved()/markGLRebuild()/markRebuild()/markPartitionMove()/markMeshDirty()/
	// setDeferredMarksForThisThread() fully moved to DXPipeline:: - no stub, every caller
	// repointed directly. updateMoveDampedAsync()/updateMoveNormalAsync() (the other half of the
	// TLS-deferred threading contract documented by LLDeferredPipelineMarks above) fully moved
	// too.

	// S24: lineSegmentIntersectInWorld()/lineSegmentIntersectParticle()/lineSegmentIntersectInHUD()
	// fully moved to DXPipeline:: - no stub, every caller repointed directly.

	// S24: markAllGeometryDirty()/resetDrawOrders()/addObject() fully moved to DXPipeline:: -
	// no stub, every caller repointed directly.

	// S24: shadersLoaded()/canUseWindLightShaders()/canUseAntiAliasing()/resetFrameStats() fully
	// moved to DXPipeline:: - no stub, every caller repointed directly.

	// S24: getVisibleExtents() fully moved to DXPipeline:: - no stub, every caller repointed
	// directly.
	// S24: getVisiblePointCloud()/updateMovedList()/updateMove() fully moved to DXPipeline:: -
	// no stub, every caller repointed directly.

	// S24: updateCull() fully moved to DXPipeline:: - no stub, every caller repointed directly.
	// S24: createObjects()/createObject()/processPartitionQ()/updateGeom()/updateGL()/
	// rebuildPriorityGroups()/clearRebuildGroups()/clearRebuildDrawables() fully moved to
	// DXPipeline:: - no stub, every caller repointed directly.
	// S24: rebuildGroups() deleted outright - declared but never defined or called anywhere,
	// nothing to migrate.

	// S24: calcPixelArea() (both overloads) fully moved to DXPipeline:: - no stub, every
	// caller repointed directly.

	// S24: stateSort() (all 4 overloads)/postSort()/forAllVisibleDrawables() fully moved to
	// DXPipeline:: - no stub, every caller repointed directly.

	// S24: renderObjects()/renderGLTFObjects()/renderAlphaObjects()/renderMaskedObjects()/
	// renderFullbrightMaskedObjects()/grabReferences()/clearReferences() fully moved to
	// DXPipeline:: - no stub, every caller repointed directly.

	// S24: checkReferences() (4 overloads) fully moved to DXPipeline:: - no stub, every real
	// caller (LLDrawable/LLFace/LLSpatialGroup destructors) repointed directly. Bodies stay
	// `#if 0`'d out to a no-op, same inert behavior as before - a pure relocation, not a fix.
	
	// S24: renderGeomDeferred()/renderGeomPostDeferred()/renderGeomShadow() fully moved to
	// DXPipeline:: - no stub, every caller repointed directly.
	// S24: bindLightFunc()/bindShadowMaps()/bindDeferredShaderFast()/bindDeferredShader()/
	// setupSpotLight()/unbindDeferredShader()/setEnvMat()/bindReflectionProbes()/
	// unbindReflectionProbes()/bindSSRUniforms()/bindSSRBuffer()/renderSSRPass()/
	// renderSSRResolve() fully moved to DXPipeline:: - no stub, every caller repointed directly.

	// S24: renderDeferredLighting() fully moved to DXPipeline:: - no stub, every caller repointed
	// directly.

	// S24: doAtmospherics()/doWaterHaze()/doWaterExclusionMask() fully moved to DXPipeline:: -
	// no stub, every caller repointed directly.

	// S24: generateSunShadow()/renderShadow()/getSunShadowTarget()/getSpotShadowTarget() fully
	// moved to DXPipeline:: - no stub, every caller repointed directly.

	// S24: renderSelectedFaces()/renderHighlights()/renderDebug()/renderPhysicsDisplay() fully
	// moved to DXPipeline:: - no stub, every caller repointed directly.

	// S24: rebuildPools() fully moved to DXPipeline:: - no stub, every caller repointed directly.

	void findReferences(LLDrawable *drawablep);	// Find the lists which have references to this object

    S32  getLightCount() const { return static_cast<S32>(mLights.size()); }

	// S24: calcNearbyLights()/setupHWLights()/setupAvatarLights()/enableLights()/
	// enableLightsDynamic()/enableLightsAvatar()/enableLightsPreview()/enableLightsAvatarEdit()/
	// enableLightsFullbright()/disableLights()/setLight()/shiftObjects()/beginRenderMap()/
	// endRenderMap()/beginAlphaGroups()/endAlphaGroups()/beginRiggedAlphaGroups()/
	// endRiggedAlphaGroups()/addTrianglesDrawn()/recordTrianglesDrawn() fully moved to
	// DXPipeline:: - no stub, every caller repointed directly.

	bool hasRenderDebugFeatureMask(const U32 mask) const	{ return bool(mRenderDebugFeatureMask & mask); }
	bool hasRenderDebugMask(const U64 mask) const			{ return bool(mRenderDebugMask & mask); }
	void setAllRenderDebugFeatures() { mRenderDebugFeatureMask = 0xffffffff; }
	void clearAllRenderDebugFeatures() { mRenderDebugFeatureMask = 0x0; }
	void setAllRenderDebugDisplays() { mRenderDebugMask = 0xffffffffffffffff; }
	void clearAllRenderDebugDisplays() { mRenderDebugMask = 0x0; }

	// S24: hasRenderType()/hasAnyRenderType()/setRenderTypeMask()/andRenderTypeMask()/
	// clearRenderTypeMask()/setAllRenderTypes()/clearAllRenderTypes()/pushRenderTypeMask()/
	// popRenderTypeMask() fully moved to DXPipeline:: (dxpipeline.h) - no forwarding stub kept,
	// every caller repointed directly. C-variadic functions can't forward "..." through a stub
	// anyway, and the goal is killing pipeline.cpp outright, not indefinitely maintaining stubs.

	// S24: isWaterClip()/toggleRenderType()/toggleRenderTypeControl()/hasRenderTypeControl()/
	// toggleRenderTypeControlNegated()/toggleRenderDebug()/toggleRenderDebugControl()/
	// toggleRenderDebugFeature()/toggleRenderDebugFeatureControl()/setRenderDebugFeatureControl()/
	// pushRenderDebugFeatureMask()/popRenderDebugFeatureMask() and the whole beacon/highlight
	// get/set/toggle family, plus addDebugBlip(), fully moved to DXPipeline:: - no stub, every
	// caller repointed directly.

	// S24: updateRenderTransparentWater()/refreshCachedSettings() fully moved to DXPipeline:: -
	// no stub, every caller repointed directly. refreshCachedSettings() merged directly into the
	// pre-existing DXPipeline::refreshCachedSettings() (same name, single implementation now).

	// S24: hidePermanentObjects()/restorePermanentObjects()/skipRenderingOfTerrain()/hideObject()/
	// restoreHiddenObject() fully moved to DXPipeline:: - no stub, every caller repointed directly.
	// handleShadowDetailChanged() stays (see its own fate comment in pipeline.cpp - flagged dead,
	// not fixed, not moved).
    void handleShadowDetailChanged();

    LLSphereProbes mSphereProbes;
    LLMirrorProbes mMirrorProbes;

private:
	// S24: unloadShaders() fully moved to DXPipeline:: - no stub, every caller repointed directly.
	// S24: addToQuickLookup()/removeFromQuickLookup()/updateDrawableGeom() fully moved to
	// DXPipeline:: - no stub, every caller repointed directly.
	void assertInitializedDoError();
	// S24: connectRefreshCachedSettingsSafe() fully moved to DXPipeline:: - no stub, every
	// caller repointed directly.
	// S24: hideDrawable()/unhideDrawable() fully moved to DXPipeline:: - no stub, every caller
	// repointed directly.
public:
	enum {GPU_CLASS_MAX = 3 };

	enum LLRenderTypeMask
	{
		// Following are pool types (some are also object types)
		RENDER_TYPE_SKY							= LLDrawPool::POOL_SKY,
		RENDER_TYPE_WL_SKY						= LLDrawPool::POOL_WL_SKY,
		RENDER_TYPE_TERRAIN						= LLDrawPool::POOL_TERRAIN,
		RENDER_TYPE_SIMPLE						= LLDrawPool::POOL_SIMPLE,
		RENDER_TYPE_GRASS						= LLDrawPool::POOL_GRASS,
		RENDER_TYPE_ALPHA_MASK					= LLDrawPool::POOL_ALPHA_MASK,
		RENDER_TYPE_FULLBRIGHT_ALPHA_MASK		= LLDrawPool::POOL_FULLBRIGHT_ALPHA_MASK,
		RENDER_TYPE_FULLBRIGHT					= LLDrawPool::POOL_FULLBRIGHT,
		RENDER_TYPE_BUMP						= LLDrawPool::POOL_BUMP,
		RENDER_TYPE_MATERIALS					= LLDrawPool::POOL_MATERIALS,
		RENDER_TYPE_AVATAR						= LLDrawPool::POOL_AVATAR,
		RENDER_TYPE_CONTROL_AV					= LLDrawPool::POOL_CONTROL_AV, // Animesh
		RENDER_TYPE_TREE						= LLDrawPool::POOL_TREE,
        RENDER_TYPE_WATEREXCLUSION              = LLDrawPool::POOL_WATEREXCLUSION,
		RENDER_TYPE_VOIDWATER					= LLDrawPool::POOL_VOIDWATER,
		RENDER_TYPE_WATER						= LLDrawPool::POOL_WATER,
        RENDER_TYPE_GLTF_PBR                    = LLDrawPool::POOL_GLTF_PBR,
        RENDER_TYPE_GLTF_PBR_ALPHA_MASK         = LLDrawPool::POOL_GLTF_PBR_ALPHA_MASK,
 		RENDER_TYPE_ALPHA						= LLDrawPool::POOL_ALPHA,
        RENDER_TYPE_ALPHA_PRE_WATER             = LLDrawPool::POOL_ALPHA_PRE_WATER,
        RENDER_TYPE_ALPHA_POST_WATER            = LLDrawPool::POOL_ALPHA_POST_WATER,
		RENDER_TYPE_GLOW						= LLDrawPool::POOL_GLOW,
		RENDER_TYPE_PASS_SIMPLE 				= LLRenderPass::PASS_SIMPLE,
        RENDER_TYPE_PASS_SIMPLE_RIGGED = LLRenderPass::PASS_SIMPLE_RIGGED,
		RENDER_TYPE_PASS_GRASS					= LLRenderPass::PASS_GRASS,
		RENDER_TYPE_PASS_FULLBRIGHT				= LLRenderPass::PASS_FULLBRIGHT,
        RENDER_TYPE_PASS_FULLBRIGHT_RIGGED = LLRenderPass::PASS_FULLBRIGHT_RIGGED,
		RENDER_TYPE_PASS_INVISIBLE				= LLRenderPass::PASS_INVISIBLE,
        RENDER_TYPE_PASS_INVISIBLE_RIGGED = LLRenderPass::PASS_INVISIBLE_RIGGED,
		RENDER_TYPE_PASS_INVISI_SHINY			= LLRenderPass::PASS_INVISI_SHINY,
        RENDER_TYPE_PASS_INVISI_SHINY_RIGGED = LLRenderPass::PASS_INVISI_SHINY_RIGGED,
		RENDER_TYPE_PASS_FULLBRIGHT_SHINY		= LLRenderPass::PASS_FULLBRIGHT_SHINY,
        RENDER_TYPE_PASS_FULLBRIGHT_SHINY_RIGGED = LLRenderPass::PASS_FULLBRIGHT_SHINY_RIGGED,
		RENDER_TYPE_PASS_SHINY					= LLRenderPass::PASS_SHINY,
        RENDER_TYPE_PASS_SHINY_RIGGED = LLRenderPass::PASS_SHINY_RIGGED,
		RENDER_TYPE_PASS_BUMP					= LLRenderPass::PASS_BUMP,
        RENDER_TYPE_PASS_BUMP_RIGGED = LLRenderPass::PASS_BUMP_RIGGED,
		RENDER_TYPE_PASS_POST_BUMP				= LLRenderPass::PASS_POST_BUMP,
        RENDER_TYPE_PASS_POST_BUMP_RIGGED = LLRenderPass::PASS_POST_BUMP_RIGGED,
		RENDER_TYPE_PASS_GLOW					= LLRenderPass::PASS_GLOW,
        RENDER_TYPE_PASS_GLOW_RIGGED = LLRenderPass::PASS_GLOW_RIGGED,
        RENDER_TYPE_PASS_GLTF_GLOW = LLRenderPass::PASS_GLTF_GLOW,
        RENDER_TYPE_PASS_GLTF_GLOW_RIGGED = LLRenderPass::PASS_GLTF_GLOW_RIGGED,
		RENDER_TYPE_PASS_ALPHA					= LLRenderPass::PASS_ALPHA,
		RENDER_TYPE_PASS_ALPHA_MASK				= LLRenderPass::PASS_ALPHA_MASK,
        RENDER_TYPE_PASS_ALPHA_MASK_RIGGED = LLRenderPass::PASS_ALPHA_MASK_RIGGED,
		RENDER_TYPE_PASS_FULLBRIGHT_ALPHA_MASK	= LLRenderPass::PASS_FULLBRIGHT_ALPHA_MASK,
        RENDER_TYPE_PASS_FULLBRIGHT_ALPHA_MASK_RIGGED = LLRenderPass::PASS_FULLBRIGHT_ALPHA_MASK_RIGGED,
		RENDER_TYPE_PASS_MATERIAL				= LLRenderPass::PASS_MATERIAL,
        RENDER_TYPE_PASS_MATERIAL_RIGGED = LLRenderPass::PASS_MATERIAL_RIGGED,
		RENDER_TYPE_PASS_MATERIAL_ALPHA			= LLRenderPass::PASS_MATERIAL_ALPHA,
        RENDER_TYPE_PASS_MATERIAL_ALPHA_RIGGED = LLRenderPass::PASS_MATERIAL_ALPHA_RIGGED,
		RENDER_TYPE_PASS_MATERIAL_ALPHA_MASK	= LLRenderPass::PASS_MATERIAL_ALPHA_MASK,
        RENDER_TYPE_PASS_MATERIAL_ALPHA_MASK_RIGGED = LLRenderPass::PASS_MATERIAL_ALPHA_MASK_RIGGED,
		RENDER_TYPE_PASS_MATERIAL_ALPHA_EMISSIVE= LLRenderPass::PASS_MATERIAL_ALPHA_EMISSIVE,
        RENDER_TYPE_PASS_MATERIAL_ALPHA_EMISSIVE_RIGGED = LLRenderPass::PASS_MATERIAL_ALPHA_EMISSIVE_RIGGED,
		RENDER_TYPE_PASS_SPECMAP				= LLRenderPass::PASS_SPECMAP,
        RENDER_TYPE_PASS_SPECMAP_RIGGED = LLRenderPass::PASS_SPECMAP_RIGGED,
		RENDER_TYPE_PASS_SPECMAP_BLEND			= LLRenderPass::PASS_SPECMAP_BLEND,
        RENDER_TYPE_PASS_SPECMAP_BLEND_RIGGED = LLRenderPass::PASS_SPECMAP_BLEND_RIGGED,
		RENDER_TYPE_PASS_SPECMAP_MASK			= LLRenderPass::PASS_SPECMAP_MASK,
        RENDER_TYPE_PASS_SPECMAP_MASK_RIGGED = LLRenderPass::PASS_SPECMAP_MASK_RIGGED,
		RENDER_TYPE_PASS_SPECMAP_EMISSIVE		= LLRenderPass::PASS_SPECMAP_EMISSIVE,
        RENDER_TYPE_PASS_SPECMAP_EMISSIVE_RIGGED = LLRenderPass::PASS_SPECMAP_EMISSIVE_RIGGED,
		RENDER_TYPE_PASS_NORMMAP				= LLRenderPass::PASS_NORMMAP,
        RENDER_TYPE_PASS_NORMMAP_RIGGED = LLRenderPass::PASS_NORMMAP_RIGGED,
		RENDER_TYPE_PASS_NORMMAP_BLEND			= LLRenderPass::PASS_NORMMAP_BLEND,
        RENDER_TYPE_PASS_NORMMAP_BLEND_RIGGED = LLRenderPass::PASS_NORMMAP_BLEND_RIGGED,
		RENDER_TYPE_PASS_NORMMAP_MASK			= LLRenderPass::PASS_NORMMAP_MASK,
        RENDER_TYPE_PASS_NORMMAP_MASK_RIGGED = LLRenderPass::PASS_NORMMAP_MASK_RIGGED,
		RENDER_TYPE_PASS_NORMMAP_EMISSIVE		= LLRenderPass::PASS_NORMMAP_EMISSIVE,
        RENDER_TYPE_PASS_NORMMAP_EMISSIVE_RIGGED = LLRenderPass::PASS_NORMMAP_EMISSIVE_RIGGED,
		RENDER_TYPE_PASS_NORMSPEC				= LLRenderPass::PASS_NORMSPEC,
        RENDER_TYPE_PASS_NORMSPEC_RIGGED = LLRenderPass::PASS_NORMSPEC_RIGGED,
		RENDER_TYPE_PASS_NORMSPEC_BLEND			= LLRenderPass::PASS_NORMSPEC_BLEND,
        RENDER_TYPE_PASS_NORMSPEC_BLEND_RIGGED = LLRenderPass::PASS_NORMSPEC_BLEND_RIGGED,
		RENDER_TYPE_PASS_NORMSPEC_MASK			= LLRenderPass::PASS_NORMSPEC_MASK,
        RENDER_TYPE_PASS_NORMSPEC_MASK_RIGGED = LLRenderPass::PASS_NORMSPEC_MASK_RIGGED,
		RENDER_TYPE_PASS_NORMSPEC_EMISSIVE		= LLRenderPass::PASS_NORMSPEC_EMISSIVE,
        RENDER_TYPE_PASS_NORMSPEC_EMISSIVE_RIGGED = LLRenderPass::PASS_NORMSPEC_EMISSIVE_RIGGED,
        RENDER_TYPE_PASS_GLTF_PBR                 = LLRenderPass::PASS_GLTF_PBR,
        RENDER_TYPE_PASS_GLTF_PBR_RIGGED         = LLRenderPass::PASS_GLTF_PBR_RIGGED,
        RENDER_TYPE_PASS_GLTF_PBR_ALPHA_MASK        = LLRenderPass::PASS_GLTF_PBR_ALPHA_MASK,
        RENDER_TYPE_PASS_GLTF_PBR_ALPHA_MASK_RIGGED = LLRenderPass::PASS_GLTF_PBR_ALPHA_MASK_RIGGED,
		// Following are object types (only used in drawable mRenderType)
		RENDER_TYPE_HUD = LLRenderPass::NUM_RENDER_TYPES,
		RENDER_TYPE_VOLUME,
		RENDER_TYPE_PARTICLES,
		RENDER_TYPE_CLOUDS,
		RENDER_TYPE_HUD_PARTICLES,
		NUM_RENDER_TYPES,
		END_RENDER_TYPES = NUM_RENDER_TYPES
	};

	enum LLRenderDebugFeatureMask
	{
		RENDER_DEBUG_FEATURE_UI					= 0x0001,
		RENDER_DEBUG_FEATURE_SELECTED			= 0x0002,
		RENDER_DEBUG_FEATURE_DYNAMIC_TEXTURES	= 0x0008,
// 		RENDER_DEBUG_FEATURE_HW_LIGHTING		= 0x0010,
		RENDER_DEBUG_FEATURE_FLEXIBLE			= 0x0010,
		RENDER_DEBUG_FEATURE_FOG				= 0x0020,
	};

	enum LLRenderDebugMask: U64
	{
		RENDER_DEBUG_COMPOSITION		=  0x00000001,
		RENDER_DEBUG_VERIFY				=  0x00000002,
		RENDER_DEBUG_BBOXES				=  0x00000004,
		RENDER_DEBUG_OCTREE				=  0x00000008,
		RENDER_DEBUG_WIND_VECTORS		=  0x00000010,
		RENDER_DEBUG_OCCLUSION			=  0x00000020,
		RENDER_DEBUG_POINTS				=  0x00000040,
		RENDER_DEBUG_TEXTURE_PRIORITY	=  0x00000080,
		RENDER_DEBUG_TEXTURE_AREA		=  0x00000100,
		RENDER_DEBUG_FACE_AREA			=  0x00000200,
		RENDER_DEBUG_PARTICLES			=  0x00000400,
		RENDER_DEBUG_GLOW				=  0x00000800, // not used
		RENDER_DEBUG_TEXTURE_ANIM		=  0x00001000,
		RENDER_DEBUG_LIGHTS				=  0x00002000,
		RENDER_DEBUG_BATCH_SIZE			=  0x00004000,
		RENDER_DEBUG_ALPHA_BINS			=  0x00008000, // not used
		RENDER_DEBUG_RAYCAST            =  0x00010000,
		RENDER_DEBUG_AVATAR_DRAW_INFO	=  0x00020000,
		RENDER_DEBUG_SHADOW_FRUSTA		=  0x00040000,
		RENDER_DEBUG_SCULPTED           =  0x00080000,
		RENDER_DEBUG_AVATAR_VOLUME      =  0x00100000,
		RENDER_DEBUG_AVATAR_JOINTS      =  0x00200000,
		RENDER_DEBUG_AGENT_TARGET       =  0x00800000,
		RENDER_DEBUG_UPDATE_TYPE		=  0x01000000,
		RENDER_DEBUG_PHYSICS_SHAPES     =  0x02000000,
		RENDER_DEBUG_NORMALS	        =  0x04000000,
		RENDER_DEBUG_LOD_INFO	        =  0x08000000,
        RENDER_DEBUG_NODES              =  0x20000000,
		RENDER_DEBUG_TEXEL_DENSITY		=  0x40000000,
		RENDER_DEBUG_TRIANGLE_COUNT		=  0x80000000,
		RENDER_DEBUG_IMPOSTORS			= 0x100000000,
        RENDER_DEBUG_REFLECTION_PROBES  = 0x200000000,
        RENDER_DEBUG_PROBE_UPDATES      = 0x400000000,
	};

public:
	// S24: getSpatialPartition() fully moved to DXPipeline:: - no stub, every caller repointed
	// directly.

	// S24: updateCamera() deleted outright - declared but never defined or called anywhere,
	// nothing to migrate.

	LLVector3				mFlyCamPosition;
	LLQuaternion			mFlyCamRotation;

	bool					 mBackfaceCull;
	S32						 mMatrixOpCount;
	S32						 mTextureMatrixOps;
	S32						 mNumVisibleNodes;

	S32						 mDebugTextureUploadCost;
	S32						 mDebugSculptUploadCost;
	S32						 mDebugMeshUploadCost;

	S32						 mNumVisibleFaces;

	S32						mPoissonOffset;

	// S24: sCompiles through sStatBatchSize (the rest of this former block) moved to DXPipeline::
	// (dxpipeline.h) - no longer anything LLPipeline-specific about them, same as the cached
	// settings above. sDistortionRender/sDistortionWaterClipPlaneMargin, which used to be noted
	// here as deleted outright, stay deleted (genuinely zero-reader tree-wide).

    class RenderTargetPack
    {
    public:
        U32 					width = 0;
        U32 					height = 0;

        //screen texture
        LLRenderTarget			screen;
        LLRenderTarget			deferredScreen;
        LLRenderTarget			deferredLight;

        //sun shadow map
        LLRenderTarget			shadow[4];
    };

    // main full resoltuion render target
    RenderTargetPack mMainRT;

    // auxillary 512x512 render target pack
    // used by reflection probes and dynamic texture bakes
    RenderTargetPack mAuxillaryRT;

    // Auxillary render target pack scaled to the hero probe's per-face size.
    RenderTargetPack mHeroProbeRT;

    // currently used render target pack
    RenderTargetPack* mRT;

    LLRenderTarget          mSpotShadow[2];

    LLRenderTarget          mPbrBrdfLut;
    LLRenderTarget          mWaterExclusionMask;

    // copy of the color/depth buffer just before gamma correction
    // for use by SSR
    LLRenderTarget          mSceneMap;

    // screen-space reflection result (class3/deferred/ssrF.hlsl), read by deferred lighting
    LLRenderTarget          mSSRBuffer;
    // temporal resolve of the SSR buffer: ping-pong pair (renderSSRResolve)
    LLRenderTarget          mSSRResolved[2];
    S32                     mSSRResolveIndex = 0;   // target written this frame
    S32                     mSSRLastResolved = 0;   // target holding the last resolved result
    bool                    mSSRResolveReady = false;
    bool                    mSSRHistoryValid = false;

    // exposure map for getting average color in scene
    LLRenderTarget          mLuminanceMap;
    LLRenderTarget          mExposureMap;
    LLRenderTarget          mLastExposure;

    // tonemapped and gamma corrected render ready for post
    LLRenderTarget          mPostPingMap;
    LLRenderTarget          mPostPongMap;

    // Holds applyFSR1Easu()'s output at full (window/world-view) resolution,
    // so applyFSR1RcasToBoundTarget() can run as a second pass on it before
    // the real backbuffer write - see DXPipeline::presentFinal(). Unlike
    // mPostPingMap/mPostPongMap this is NOT sized off the (possibly
    // RenderScalePercent-shrunk) allocateScreenBufferInternal() path - it's
    // lazily allocated/resized in presentFinal() itself, directly off the
    // real destination size, since it only exists when upscaling is active
    // (RenderScalePercent < 100).
    LLRenderTarget          mFSR1EasuBuffer;

    // Anaglyph 3D: each eye's fully composited post-fx result is captured here (via
    // DXPipeline::presentFinal()'s optional destination parameter) instead of relying on left/right
    // tint surviving as ambient GPU colormask state across the frame. DXPipeline::presentStereoComposite()
    // reads both and writes the real back buffer once. Allocated unconditionally alongside
    // mPostPingMap/mPostPongMap, not gated on StereoMode, so toggling stereo at runtime never depends
    // on catching a settings-change path that would trigger reallocation.
    LLRenderTarget          mStereoEyeL;
    LLRenderTarget          mStereoEyeR;

    // Last frame's fully-resolved AO/shadow lightmap, reprojected and blended with this frame's raw
    // value in DXPipeline::renderDeferredLighting() to remove per-frame screen-locked-noise flicker.
    // Same history-buffer pattern as mLastExposure below (see generateExposure()).
    LLRenderTarget          mSSAOHistory;

    // FXAA helper target
    LLRenderTarget          mFXAAMap;
    LLRenderTarget          mSMAABlendBuffer;

    // render ui to buffer target
    LLRenderTarget          mUIScreen;

    // downres scratch space for GPU downscaling of textures
    LLRenderTarget          mDownResMap;

    // 2k bom scratch target
    LLRenderTarget          mBakeMap;

    LLCullResult            mSky;
    LLCullResult            mReflectedObjects;
    LLCullResult            mRefractedObjects;

	//utility buffers for rendering post effects
	LLPointer<LLVertexBuffer> mDeferredVB;

    // a single triangle that covers the whole screen
    LLPointer<LLVertexBuffer> mScreenTriangleVB;

	//utility buffer for rendering cubes, 8 vertices are corners of a cube [-1, 1]
	LLPointer<LLVertexBuffer> mCubeVB;

    //list of currently bound reflection maps
    std::vector<LLReflectionMap*> mReflectionMaps;

	std::vector<LLVector3>	mShadowFrustPoints[4];
	LLVector4				mShadowError;
	LLVector4				mShadowFOV;
	LLVector3				mShadowFrustOrigin[4];
	LLCamera				mShadowCamera[8];
	LLVector3				mShadowExtents[4][2];
    // TODO : separate Sun Shadow and Spot Shadow matrices
    glm::mat4               mSunShadowMatrix[6];
    glm::mat4               mShadowModelview[6];
    glm::mat4               mShadowProjection[6];
    glm::mat4               mReflectionModelView;

	LLPointer<LLDrawable>	mShadowSpotLight[2];
	F32						mSpotLightFade[2];
	LLPointer<LLDrawable>	mTargetShadowSpotLight[2];

	LLVector4				mSunClipPlanes;
	LLVector4				mSunOrthoClipPlanes;
	LLVector2				mScreenScale;

	//water distortion texture (refraction)
	LLRenderTarget				mWaterDis;

	//texture for making the glow
	LLRenderTarget				mGlow[3];

	//noise map
	U32					mNoiseMap;
	U32					mTrueNoiseMap;
	U32					mLightFunc;

    //smaa
    U32                 mSMAAAreaMap = 0;
    U32                 mSMAASearchMap = 0;
    U32                 mSMAASampleMap = 0;

    // DX-native backing for the 6 procedural textures above. Those are plain raw-GLuint fields (no
    // LLImageDX wrapper) created/bound via an ambient-GL-state idiom (bindManual() then
    // setManualImage()) that has no DX_RENDER translation - there's no "currently bound for upload"
    // concept in D3D11's explicit-resource model. Each raw field keeps its GL-only meaning; these
    // DXTexture fields are used instead, directly, at each call site's DX_RENDER branch.
    DXTexture mDXNoiseMap;
    DXTexture mDXTrueNoiseMap;
    DXTexture mDXLightFunc;
    DXTexture mDXSMAAAreaMap;
    DXTexture mDXSMAASearchMap;
    DXTexture mDXSMAASampleMap;

	LLColor4			mSunDiffuse;
    LLColor4			mMoonDiffuse;
	LLVector4			mSunDir;
    LLVector4			mMoonDir;
    bool                mNeedsShadowTargetClear;

	LLVector4			mTransformedSunDir;
    LLVector4			mTransformedMoonDir;

	bool					mInitialized;
	bool					mShadersLoaded;

	U32						mTransformFeedbackPrimitives; //number of primitives expected to be generated by transform feedback
protected:
	bool					mRenderTypeEnabled[NUM_RENDER_TYPES];
	std::stack<std::string> mRenderTypeEnableStack;

	U32						mRenderDebugFeatureMask;
	U64						mRenderDebugMask;
	U64						mOldRenderDebugMask;
	std::stack<U32>			mRenderDebugFeatureStack;

	/////////////////////////////////////////////
	//
	// Public (was protected) - DXPipeline::markMoved()/markShift()/shiftObjects()/
	// unlinkDrawable() read/write these drawable queues directly.
public:
	LLDrawable::drawable_vector_t	mMovedList;
	LLDrawable::drawable_vector_t mMovedBridge;
	LLDrawable::drawable_vector_t	mShiftList;

	/////////////////////////////////////////////
	//
	//
	// Public (was protected along with mLights/mNearbyLights below it) - DXPipeline::
	// calcNearbyLights()/setupHWLights() need to name this type directly (light_set_t::iterator,
	// Light(...) construction), unlike getPools()'s pool_set_t which only needs range-based-for/auto.
	struct Light
	{
		Light(LLDrawable* ptr, F32 d, F32 f = 0.0f)
			: drawable(ptr),
			  dist(d),
			  fade(f)
		{}
		LLPointer<LLDrawable> drawable;
		F32 dist;
		F32 fade;
		struct compare
		{
			bool operator()(const Light& a, const Light& b) const
			{
				if ( a.dist < b.dist )
					return true;
				else if ( a.dist > b.dist )
					return false;
				else
					return a.drawable < b.drawable;
			}
		};
	};
	typedef std::set< Light, Light::compare > light_set_t;
protected:
	LLDrawable::ordered_drawable_set_t	mLights;
	light_set_t						mNearbyLights; // lights near camera
	LLColor4						mHWLightColors[8];

	/////////////////////////////////////////////
	//
	// Different queues of drawables being processed.
	// Public (was protected) - DXPipeline::markRebuild()/markMeshDirty()/markPartitionMove()/
	// processPartitionQ()/addObject()/createObjects()/markTextured() read/write these directly.
public:
	LLDrawable::drawable_list_t 	mBuildQ1; // priority
	LLSpatialGroup::sg_vector_t		mGroupQ1; //priority

	LLSpatialGroup::sg_vector_t		mGroupSaveQ1; // a place to save mGroupQ1 until it is safe to unref

	LLSpatialGroup::sg_vector_t		mMeshDirtyGroup; //groups that need rebuildMesh called
	U32 mMeshDirtyQueryObject;

	LLDrawable::drawable_list_t		mPartitionQ; //drawables that need to update their spatial partition radius

	bool mGroupQ1Locked;

	bool mResetVertexBuffers; //if true, clear vertex buffers on next update

	LLViewerObject::vobj_list_t		mCreateQ;

	LLDrawable::drawable_set_t		mRetexturedList;
protected:

	class HighlightItem
	{
	public:
		const LLPointer<LLDrawable> mItem;
		mutable F32 mFade;

		HighlightItem(LLDrawable* item)
		: mItem(item), mFade(0)
		{
		}

		bool operator<(const HighlightItem& rhs) const
		{
			return mItem < rhs.mItem;
		}

		bool operator==(const HighlightItem& rhs) const
		{
			return mItem == rhs.mItem;
		}

		void incrFade(F32 val) const
		{
			mFade = llclamp(mFade+val, 0.f, 1.f);
		}
	};

	//////////////////////////////////////////////////
	//
	// Draw pools are responsible for storing all rendered data,
	// and performing the actual rendering of objects.
	//
	struct compare_pools
	{
		bool operator()(const LLDrawPool* a, const LLDrawPool* b) const
		{
			if (!a)
				return true;
			else if (!b)
				return false;
			else
			{
				S32 atype = a->getType();
				S32 btype = b->getType();
				if (atype < btype)
					return true;
				else if (atype > btype)
					return false;
				else
					return a->getId() < b->getId();
			}
		}
	};
 	typedef std::set<LLDrawPool*, compare_pools > pool_set_t;
public:
	// S24: public (was protected along with the whole pool-pointer block below) - DXPipeline::
	// addPool()/removePool()/rebuildPools()/addToQuickLookup()/removeFromQuickLookup() all
	// insert/erase mPools directly and assign these individual pool pointers directly, not just
	// read them (unlike getPools()'s read-only use case, which stayed a const-ref accessor).
	pool_set_t mPools;
	LLDrawPool*	mLastRebuildPool;

	// For quick-lookups into mPools (mapped by texture pointer)
	std::map<uintptr_t, LLDrawPool*>	mTerrainPools;
	std::map<uintptr_t, LLDrawPool*>	mTreePools;
	DXAlphaDrawPool*			mAlphaPoolPreWater = nullptr;
    DXAlphaDrawPool*            mAlphaPoolPostWater = nullptr;
	LLDrawPool*					mSkyPool = nullptr;
	LLDrawPool*					mTerrainPool = nullptr;
	LLDrawPool*					mWaterPool = nullptr;
	LLRenderPass*				mSimplePool = nullptr;
	LLRenderPass*				mGrassPool = nullptr;
	LLRenderPass*				mAlphaMaskPool = nullptr;
	LLRenderPass*				mFullbrightAlphaMaskPool = nullptr;
	LLRenderPass*				mFullbrightPool = nullptr;
	LLDrawPool*					mGlowPool = nullptr;
	LLDrawPool*					mBumpPool = nullptr;
	LLDrawPool*					mMaterialsPool = nullptr;
	LLDrawPool*					mWLSkyPool = nullptr;
	LLDrawPool*					mPBROpaquePool = nullptr;
    LLDrawPool*                 mPBRAlphaMaskPool = nullptr;
    LLDrawPool*                 mWaterExclusionPool      = nullptr;
protected:

	// Note: no need to keep an quick-lookup to avatar pools, since there's only one per avatar
	
public:
	std::vector<LLFace*>		mHighlightFaces;	// highlight faces on physical objects

	// DX_RENDER: read-only access to mPools for DXPipeline (newview/
	// dxpipeline.h) - kept as an ordinary accessor rather than a friend
	// declaration or ifdef, so it costs nothing on the GL build and doesn't
	// require DXPipeline to name the protected pool_set_t/compare_pools
	// types explicitly (range-based for + auto never need to spell them out).
	const pool_set_t& getPools() const { return mPools; }
	// Mutable counterpart to getPools() above - DXPipeline::cleanup() erases pools directly out of
	// mPools as it deletes them.
	pool_set_t& getPoolsMutable() { return mPools; }

	// DX_RENDER: same reasoning as getPools() above - DXPipeline::
	// renderDeferredLighting()'s local-lights pass needs read-only access
	// to mNearbyLights (protected), the already-culled/distance-sorted
	// "lights near camera" list GL's own local-lights loop iterates.
	const light_set_t& getNearbyLights() const { return mNearbyLights; }

	// Mutable counterpart to getNearbyLights() above - DXPipeline::calcNearbyLights()/
	// setupHWLights() both need to write mNearbyLights (rebuild it / mutate per-light fade), not
	// just read it.
	light_set_t& getNearbyLightsMutable() { return mNearbyLights; }

	// DX_RENDER: read-only access to mLights (protected) - DXPipeline::calcNearbyLights()
	// iterates the full attached-light drawable list to find new in-range lights.
	const LLDrawable::ordered_drawable_set_t& getLights() const { return mLights; }

	// Mutable counterpart to getLights() above - DXPipeline::setLight() inserts/erases
	// drawables from mLights directly.
	LLDrawable::ordered_drawable_set_t& getLightsMutable() { return mLights; }

	// DX_RENDER: mutable access to mHWLightColors (protected) - DXPipeline::setupAvatarLights()/
	// setupHWLights() write per-slot hardware light colors directly (mHWLightColors[i] = ...).
	LLColor4* getHWLightColorsMutable() { return mHWLightColors; }

	// DX_RENDER: mutable access to mLightMask/mLightMovingMask (protected, plain bitmasks) -
	// DXPipeline::setupHWLights()/enableLights() read and write these directly.
	U32& getLightMaskMutable() { return mLightMask; }
	U32& getLightMovingMaskMutable() { return mLightMovingMask; }

	// DX_RENDER: mutable access to mRenderTypeEnabled/mRenderTypeEnableStack (protected) -
	// DXPipeline::setRenderTypeMask()/hasAnyRenderType()/andRenderTypeMask()/
	// clearRenderTypeMask()/setAllRenderTypes()/clearAllRenderTypes()/pushRenderTypeMask()/
	// popRenderTypeMask() all read/write these directly, several via raw memcpy/memset on the
	// fixed-size array - returning a reference to the array itself (not a pointer) keeps
	// sizeof(getRenderTypeEnabledMutable()) working exactly like the original bare member access.
	bool (&getRenderTypeEnabledMutable())[NUM_RENDER_TYPES] { return mRenderTypeEnabled; }
	// Const read-only counterpart - DXPipeline::hasRenderType()/hasAnyRenderType() are themselves
	// const (mirroring the original LLPipeline:: member signatures), so they need a const-callable
	// accessor rather than the mutable one above.
	const bool (&getRenderTypeEnabled() const)[NUM_RENDER_TYPES] { return mRenderTypeEnabled; }
	std::stack<std::string>& getRenderTypeEnableStackMutable() { return mRenderTypeEnableStack; }

	// DX_RENDER: mutable access to mRenderDebugMask/mRenderDebugFeatureMask/
	// mRenderDebugFeatureStack (protected) - DXPipeline::toggleRenderDebug()/
	// toggleRenderDebugFeature()/setRenderDebugFeatureControl()/pushRenderDebugFeatureMask()/
	// popRenderDebugFeatureMask() all read/write these directly.
	U64& getRenderDebugMaskMutable() { return mRenderDebugMask; }
	U32& getRenderDebugFeatureMaskMutable() { return mRenderDebugFeatureMask; }
	std::stack<U32>& getRenderDebugFeatureStackMutable() { return mRenderDebugFeatureStack; }
	// DX_RENDER: mutable access to mOldRenderDebugMask (protected) - DXPipeline::resetFrameStats()
	// compares/assigns it against mRenderDebugMask directly.
	U64& getOldRenderDebugMaskMutable() { return mOldRenderDebugMask; }

	// DX_RENDER: same reasoning as getPools()/getNearbyLights() above -
	// DXPipeline::renderObjects()/renderGLTFObjects()/renderAlphaObjects()/
	// renderMaskedObjects()/renderFullbrightMaskedObjects() need these
	// (protected) pool pointers.
	LLRenderPass* getSimplePool() const { return mSimplePool; }
	LLRenderPass* getAlphaMaskPool() const { return mAlphaMaskPool; }
	LLRenderPass* getFullbrightAlphaMaskPool() const { return mFullbrightAlphaMaskPool; }

	// Public (was protected) - DXPipeline::addDebugBlip() constructs/pushes these directly.
	class DebugBlip
	{
	public:
		LLColor4 mColor;
		LLVector3 mPosition;
		F32 mAge;

		DebugBlip(const LLVector3& position, const LLColor4& color)
			: mColor(color), mPosition(position), mAge(0.f)
		{ }
	};

	std::list<DebugBlip> mDebugBlips;

	// Public (was protected) - DXPipeline::renderSelectedFaces() reads/writes these directly.
	std::vector<LLFace*>		mSelectedFaces;

	LLPointer<LLViewerFetchedTexture>	mFaceSelectImagep;
protected:
	U32						mLightMask;
	U32						mLightMovingMask;
public:
	// S24: sCurRenderPoolType deleted outright - confirmed genuinely zero-reader tree-wide.

	// S24: the ~76 cached-settings statics that used to live in this block (RenderGlow*, Preview*,
	// shadow/SSAO/SSR tuning, RenderFSAAType/RenderUIBuffer/RenderShadowDetail/Camera*/
	// RenderDeferredSunWash/RenderDelayCreation/RenderAnimateRes/RenderSpotLightsInNondeferred/
	// RenderDeferredAtmospheric/RenderFarClip/RenderAvatarCloth/etc.) are used only by DXPipeline::
	// now - relocated to DXPipeline:: static members (dxpipeline.h), populated by
	// DXPipeline::refreshCachedSettings() (called from this class's own refreshCachedSettings()).
	// pipeline.cpp, which used to read some of these directly, no longer exists.
};

// S24: render_bbox() deleted outright - declared but never defined or called anywhere, nothing
// to migrate.
// S24: render_hud_elements() fully moved to dxpipeline.h/.cpp (free function, not DXPipeline::) -
// no stub, every caller repointed directly.
// Not `static`: DXPipeline::presentDeferredScreen() (dxpipeline.cpp) calls this directly, since
// DX_RENDER's renderFinalize() early-returns before GL's own body (which normally calls this) ever
// runs. `static` would give every other TU an internally-linked, undefined copy.
void updateEffectMask();


extern LLPipeline gPipeline;
extern const LLMatrix4* gDXLastMatrix;

#endif
