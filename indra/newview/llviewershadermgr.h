/**
 * @file llviewershadermgr.h
 * @brief Viewer Shader Manager
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

#ifndef LL_VIEWER_SHADER_MGR_H
#define LL_VIEWER_SHADER_MGR_H

#include "llshadermgr.h"
#include "llmaterial.h"

#define LL_DEFERRED_MULTI_LIGHT_COUNT 16

class LLViewerShaderMgr : public LLShaderMgr
{
public:
	static bool sInitialized;
	static bool sSkipReload;

	LLViewerShaderMgr();
	/* virtual */ ~LLViewerShaderMgr();

	// Add shaders to mShaderList for later uniform propagation
	// Will assert on redundant shader entries in debug builds
	void finalizeShaderList();

	// singleton pattern implementation
	static LLViewerShaderMgr* instance();
	static void releaseInstance();

	void initAttribsAndUniforms(void);
	void setShaders();
	void unloadShaders();
	S32  getShaderLevel(S32 type);

	// Request deferred shader reload on next frame (to avoid blocking main thread)
	void requestDeferredShaderReload();
	// Check and process any pending shader reload requests
	void processDeferredShaderReload();
	// True if a deferred reload is queued and hasn't run yet - lets a caller that's about to do
	// its own immediate setShaders() skip it instead, since the pending one will cover the same work.
	bool isDeferredShaderReloadPending() const { return mDeferredShaderReloadPending; }

	// loadBasicShaders in case of a failure returns
	// name of a file error happened at, otherwise
	// returns an empty string
	std::string loadBasicShaders();
	bool loadShadersEffects();
	bool loadShadersDeferred();
	// Not called from loadShadersDeferred()'s eager startup chain; called lazily instead from
	// LLPipeline::visualizeBuffers() on first use of the debug view (see llviewershadermgr.cpp).
	bool loadShaderBufferVisualization();
	bool loadShadersObject();
	bool loadShadersAvatar();
	bool loadShadersWater();
	bool loadShadersInterface();

	std::vector<S32> mShaderLevel;
	S32 mMaxAvatarShaderLevel;

	enum EShaderClass
	{
		SHADER_LIGHTING,
		SHADER_OBJECT,
		SHADER_AVATAR,
		SHADER_ENVIRONMENT,
		SHADER_INTERFACE,
		SHADER_EFFECT,
		SHADER_WINDLIGHT,
		SHADER_WATER,
		SHADER_DEFERRED,
		SHADER_COUNT
	};

	// simple model of forward iterator
	// http://www.sgi.com/tech/stl/ForwardIterator.html
	class shader_iter
	{
	private:
		friend bool operator == (shader_iter const& a, shader_iter const& b);
		friend bool operator != (shader_iter const& a, shader_iter const& b);

		typedef std::vector<LLHLSLShader*>::const_iterator base_iter_t;
	public:
		shader_iter()
		{
		}

		shader_iter(base_iter_t iter) : mIter(iter)
		{
		}

		LLHLSLShader& operator * () const
		{
			return **mIter;
		}

		LLHLSLShader* operator -> () const
		{
			return *mIter;
		}

		shader_iter& operator++ ()
		{
			++mIter;
			return *this;
		}

		shader_iter operator++ (int)
		{
			return mIter++;
		}

	private:
		base_iter_t mIter;
	};

	shader_iter beginShaders() const;
	shader_iter endShaders() const;

	/* virtual */ std::string getShaderDirPrefix(void);

	/* virtual */ void updateShaderUniforms(LLHLSLShader* shader);

	static void purgeShaderCache();

private:
	// the list of shaders we need to propagate parameters to.
	std::vector<LLHLSLShader*> mShaderList;

	// Flag to indicate shader reload is pending
	bool mDeferredShaderReloadPending = false;
}; //LLViewerShaderMgr

inline bool operator == (LLViewerShaderMgr::shader_iter const& a, LLViewerShaderMgr::shader_iter const& b)
{
	return a.mIter == b.mIter;
}

inline bool operator != (LLViewerShaderMgr::shader_iter const& a, LLViewerShaderMgr::shader_iter const& b)
{
	return a.mIter != b.mIter;
}

extern LLVector4            gShinyOrigin;

//utility shaders
extern LLHLSLShader         gOcclusionProgram;
extern LLHLSLShader         gOcclusionCubeProgram;
extern LLHLSLShader         gGlowCombineProgram;
extern LLHLSLShader         gReflectionMipProgram;
extern LLHLSLShader         gGaussianProgram;
extern LLHLSLShader         gRadianceGenProgram;
extern LLHLSLShader         gHeroRadianceGenProgram;
extern LLHLSLShader         gIrradianceGenProgram;
extern LLHLSLShader         gGlowCombineFXAAProgram;
extern LLHLSLShader         gDebugProgram;
enum NormalDebugShaderVariant : S32
{
	NORMAL_DEBUG_SHADER_DEFAULT,
	NORMAL_DEBUG_SHADER_WITH_TANGENTS,
	NORMAL_DEBUG_SHADER_COUNT
};
extern LLHLSLShader         gNormalDebugProgram[NORMAL_DEBUG_SHADER_COUNT];
extern LLHLSLShader         gSkinnedNormalDebugProgram[NORMAL_DEBUG_SHADER_COUNT];
extern LLHLSLShader         gClipProgram;
extern LLHLSLShader         gBenchmarkProgram;
extern LLHLSLShader         gReflectionProbeDisplayProgram;
extern LLHLSLShader         gCopyProgram;
extern LLHLSLShader         gCopyDepthProgram;
extern LLHLSLShader         gPBRTerrainBakeProgram;
extern LLHLSLShader         gDrawColorProgram;

//output tex0[tc0] - tex1[tc1]
extern LLHLSLShader         gTwoTextureCompareProgram;
//discard some fragments based on user-set color tolerance
extern LLHLSLShader         gOneTextureFilterProgram;

//object shaders
extern LLHLSLShader     gObjectPreviewProgram;
extern LLHLSLShader        gPhysicsPreviewProgram;
extern LLHLSLShader     gObjectBumpProgram;
extern LLHLSLShader        gSkinnedObjectBumpProgram;
extern LLHLSLShader     gObjectAlphaMaskNoColorProgram;

//environment shaders
extern LLHLSLShader         gWaterProgram;
extern LLHLSLShader         gUnderWaterProgram;
extern LLHLSLShader         gGlowProgram;
extern LLHLSLShader         gGlowExtractProgram;

//interface shaders
extern LLHLSLShader         gHighlightProgram;
extern LLHLSLShader         gHighlightNormalProgram;
extern LLHLSLShader         gHighlightSpecularProgram;

extern LLHLSLShader         gDeferredHighlightProgram;

extern LLHLSLShader         gPathfindingProgram;
extern LLHLSLShader         gPathfindingNoNormalsProgram;

// avatar shader handles
extern LLHLSLShader         gAvatarProgram;
extern LLHLSLShader         gAvatarEyeballProgram;
extern LLHLSLShader         gImpostorProgram;

// Post Process Shaders
extern LLHLSLShader         gPostScreenSpaceReflectionProgram;

// Deferred rendering shaders
extern LLHLSLShader         gDeferredImpostorProgram;
// Jelly-doll "ghost" impostor: alpha-blended, rim-glowing silhouette drawn post-deferred
// instead of into the opaque G-buffer. See LLDrawPoolAvatar::renderJellyDollGhosts().
extern LLHLSLShader         gDeferredJellyGhostProgram;
extern LLHLSLShader         gDeferredDiffuseProgram;
extern LLHLSLShader         gDeferredDiffuseAlphaMaskProgram;
extern LLHLSLShader         gDeferredNonIndexedDiffuseAlphaMaskProgram;
extern LLHLSLShader         gDeferredNonIndexedDiffuseAlphaMaskNoColorProgram;
extern LLHLSLShader         gDeferredNonIndexedDiffuseProgram;
extern LLHLSLShader         gDeferredBumpProgram;
extern LLHLSLShader         gDeferredTerrainProgram;
extern LLHLSLShader         gDeferredTreeProgram;
extern LLHLSLShader         gDeferredTreeShadowProgram;
extern LLHLSLShader         gDeferredLightProgram;
extern LLHLSLShader         gDeferredMultiLightProgram[LL_DEFERRED_MULTI_LIGHT_COUNT];
extern LLHLSLShader         gDeferredSpotLightProgram;
extern LLHLSLShader         gDeferredMultiSpotLightProgram;
extern LLHLSLShader         gDeferredSunProgram;
extern LLHLSLShader         gDeferredSunProbeProgram;
extern LLHLSLShader         gHazeProgram;
extern LLHLSLShader         gHazeWaterProgram;
extern LLHLSLShader         gDeferredBlurLightProgram;
extern LLHLSLShader         gDeferredTemporalResolveSSAOProgram;
extern LLHLSLShader         gDeferredAvatarProgram;
extern LLHLSLShader         gDeferredSoftenProgram;
extern LLHLSLShader         gDeferredShadowProgram;
extern LLHLSLShader         gDeferredShadowCubeProgram;
extern LLHLSLShader         gDeferredShadowAlphaMaskProgram;
extern LLHLSLShader         gDeferredShadowGLTFAlphaMaskProgram;
extern LLHLSLShader         gDeferredShadowGLTFAlphaBlendProgram;
extern LLHLSLShader         gDeferredShadowFullbrightAlphaMaskProgram;
extern LLHLSLShader         gDeferredPostProgram;
extern LLHLSLShader         gDeferredCoFProgram;
extern LLHLSLShader         gDeferredDoFCombineProgram;
extern LLHLSLShader         gFXAAProgram[4];
extern LLHLSLShader         gSMAAEdgeDetectProgram[4];
extern LLHLSLShader         gSMAABlendWeightsProgram[4];
extern LLHLSLShader         gSMAANeighborhoodBlendProgram[4];
extern LLHLSLShader         gCASProgram;
extern LLHLSLShader         gCASLegacyGammaProgram;
extern LLHLSLShader         gDeferredPostNoDoFProgram;
extern LLHLSLShader         gDeferredPostNoDoFNoiseProgram;

// Final stereo composite; see stereoAnaglyphF.hlsl.
extern LLHLSLShader         gStereoAnaglyphProgram;
extern LLHLSLShader         gDeferredPostGammaCorrectProgram;
// Separable Catmull-Rom bicubic resize: one program, bound twice (horizontal then vertical)
// via LLGPUResize::resize() (newview/llgpuresize.h); see resizeBicubic.hlsl.
extern LLHLSLShader         gResizeBicubicProgram;
extern LLHLSLShader         gLegacyPostGammaCorrectProgram;
extern LLHLSLShader         gDeferredPostTonemapProgram;
extern LLHLSLShader         gNoPostTonemapProgram;
extern LLHLSLShader         gDeferredPostTonemapGammaCorrectProgram;
extern LLHLSLShader         gNoPostTonemapGammaCorrectProgram;
extern LLHLSLShader         gDeferredPostTonemapLegacyGammaCorrectProgram;
extern LLHLSLShader         gNoPostTonemapLegacyGammaCorrectProgram;
extern LLHLSLShader         gExposureProgram;
extern LLHLSLShader         gExposureProgramNoFade;
extern LLHLSLShader         gLuminanceProgram;
extern LLHLSLShader         gDeferredAvatarShadowProgram;
extern LLHLSLShader         gDeferredAvatarAlphaShadowProgram;
extern LLHLSLShader         gDeferredAvatarAlphaMaskShadowProgram;
extern LLHLSLShader         gDeferredAlphaProgram;
extern LLHLSLShader         gHUDAlphaProgram;
extern LLHLSLShader         gDeferredAlphaImpostorProgram;
extern LLHLSLShader         gDeferredFullbrightProgram;
extern LLHLSLShader         gHUDFullbrightProgram;
extern LLHLSLShader         gDeferredFullbrightAlphaMaskProgram;
extern LLHLSLShader         gHUDFullbrightAlphaMaskProgram;
extern LLHLSLShader         gDeferredFullbrightAlphaMaskAlphaProgram;
extern LLHLSLShader         gHUDFullbrightAlphaMaskAlphaProgram;
extern LLHLSLShader         gDeferredEmissiveProgram;
extern LLHLSLShader         gDeferredAvatarEyesProgram;
extern LLHLSLShader         gDeferredAvatarAlphaProgram;
extern LLHLSLShader         gEnvironmentMapProgram;
extern LLHLSLShader         gDeferredWLSkyProgram;
extern LLHLSLShader         gDeferredWLCloudProgram;
extern LLHLSLShader         gDeferredWLSunProgram;
extern LLHLSLShader         gDeferredWLMoonProgram;
extern LLHLSLShader         gDeferredStarProgram;
extern LLHLSLShader         gDeferredStarShootingProgram;
// Minimal flat-vertex-color line shader for sky-dome-distance immediate-mode geometry
// (constellation connector lines, dxLineWidth()-based) - see skyLineV.hlsl's comment for why this
// can't just reuse gUIProgram (no far-clip pin, silently clips sky-dome-distance geometry away).
extern LLHLSLShader         gDeferredSkyLineProgram;
// Procedural galactic-dust band (RenderStarDustIntensity) - a real noise-based cloudy lane rendered
// on the existing sky-dome mesh, replacing the earlier photo-overlay attempt (pole/horizon warping,
// see git history) and the original sprite-scatter dust patches (read as dots/streaks, not a haze) -
// see galacticBandV.hlsl's comment.
extern LLHLSLShader         gDeferredGalacticBandProgram;
extern LLHLSLShader         gDeferredFullbrightShinyProgram;
extern LLHLSLShader         gHUDFullbrightShinyProgram;
extern LLHLSLShader         gNormalMapGenProgram;
extern LLHLSLShader         gDeferredGenBrdfLutProgram;
extern LLHLSLShader         gDeferredBufferVisualProgram;

// Deferred materials shaders
extern LLHLSLShader         gDeferredMaterialProgram[LLMaterial::SHADER_COUNT * 2];

extern LLHLSLShader         gHUDPBROpaqueProgram;
extern LLHLSLShader         gPBRGlowProgram;
extern LLHLSLShader         gDeferredPBROpaqueProgram;
extern LLHLSLShader         gDeferredPBRAlphaProgram;
extern LLHLSLShader         gHUDPBRAlphaProgram;

// GLTF shaders
extern LLHLSLShader         gGLTFPBRMetallicRoughnessProgram;

// Encodes detail level for dropping textures, in accordance with the GLTF spec where possible
// 0 is highest detail, -1 drops emissive, etc
// Dropping metallic roughness is off-spec - Reserve for potato machines as needed
// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#additional-textures
enum TerrainPBRDetail : S32
{
	TERRAIN_PBR_DETAIL_MAX = 0,
	TERRAIN_PBR_DETAIL_EMISSIVE = 0,
	TERRAIN_PBR_DETAIL_OCCLUSION = -1,
	TERRAIN_PBR_DETAIL_NORMAL = -2,
	TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS = -3,
	TERRAIN_PBR_DETAIL_BASE_COLOR = -4,
	TERRAIN_PBR_DETAIL_MIN = -4,
};
enum TerrainPaintType : U32
{
	// Use LLVLComposition::mDatap (heightmap) generated by generateHeights, plus noise from TERRAIN_ALPHARAMP
	TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE = 0,
	// Use paint map if PBR terrain, otherwise fall back to TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE
	TERRAIN_PAINT_TYPE_PBR_PAINTMAP = 1,
	TERRAIN_PAINT_TYPE_COUNT = 2,
};
extern LLHLSLShader         gDeferredPBRTerrainProgram[TERRAIN_PAINT_TYPE_COUNT];
#endif
