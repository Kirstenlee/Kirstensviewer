/**
 * @file llviewershadermgr.cpp
 * @brief Viewer shader manager implementation.
 *
 * $LicenseInfo:firstyear=2005&license=viewerlgpl$
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


#include "llviewerprecompiledheaders.h"

#include <boost/lexical_cast.hpp>

#include "llfeaturemanager.h"
#include "llviewershadermgr.h"
#include "llviewercontrol.h"
#include "llversioninfo.h"

#include "llrender.h"
#include "llenvironment.h"
#include "llerrorcontrol.h"
#include "llworld.h"
#include "llsky.h"

#include "pipeline.h"

#include "lldir.h"
#include "llfile.h"
#include "llviewerwindow.h"
#include "llwindow.h"

#include "lljoint.h"
#include "llskinningutil.h"

#ifdef DX_RENDER
#include "DXShader.h"
#include "workqueue.h"
#include <condition_variable>
#include <mutex>
#endif

static LLStaticHashedString sTexture0("texture0");
static LLStaticHashedString sTexture1("texture1");
static LLStaticHashedString sTex0("tex0");
static LLStaticHashedString sTex1("tex1");
static LLStaticHashedString sDitherTex("dither_tex");
static LLStaticHashedString sGlowMap("glowMap");
static LLStaticHashedString sScreenMap("screenMap");

// Lots of STL stuff in here, using namespace std to keep things more readable
using std::vector;
using std::pair;
using std::make_pair;
using std::string;

bool                LLViewerShaderMgr::sInitialized = false;
bool                LLViewerShaderMgr::sSkipReload = false;

LLVector4           gShinyOrigin;

S32 clamp_terrain_mapping(S32 mapping)
{
    // 1 = "flat", 2 not implemented, 3 = triplanar mapping
    mapping = llclamp(mapping, 1, 3);
    if (mapping == 2) { mapping = 1; }
    return mapping;
}

//utility shaders
LLHLSLShader    gOcclusionProgram;
LLHLSLShader    gSkinnedOcclusionProgram;
LLHLSLShader    gOcclusionCubeProgram;
LLHLSLShader    gGlowCombineProgram;
LLHLSLShader    gReflectionMipProgram;
LLHLSLShader    gGaussianProgram;
LLHLSLShader    gRadianceGenProgram;
LLHLSLShader    gHeroRadianceGenProgram;
LLHLSLShader    gIrradianceGenProgram;
LLHLSLShader    gGlowCombineFXAAProgram;
LLHLSLShader    gTwoTextureCompareProgram;
LLHLSLShader    gOneTextureFilterProgram;
LLHLSLShader    gDebugProgram;
LLHLSLShader    gSkinnedDebugProgram;
LLHLSLShader    gNormalDebugProgram[NORMAL_DEBUG_SHADER_COUNT];
LLHLSLShader    gSkinnedNormalDebugProgram[NORMAL_DEBUG_SHADER_COUNT];
LLHLSLShader    gClipProgram;
LLHLSLShader    gAlphaMaskProgram;
LLHLSLShader    gBenchmarkProgram;
LLHLSLShader    gReflectionProbeDisplayProgram;
LLHLSLShader    gCopyProgram;
LLHLSLShader    gCopyDepthProgram;
LLHLSLShader    gPBRTerrainBakeProgram;
LLHLSLShader    gDrawColorProgram;

//object shaders
LLHLSLShader        gObjectPreviewProgram;
LLHLSLShader        gSkinnedObjectPreviewProgram;
LLHLSLShader        gPhysicsPreviewProgram;
LLHLSLShader        gObjectFullbrightAlphaMaskProgram;
LLHLSLShader        gObjectBumpProgram;
LLHLSLShader        gSkinnedObjectBumpProgram;
LLHLSLShader        gObjectAlphaMaskNoColorProgram;

//environment shaders
LLHLSLShader        gWaterProgram;
LLHLSLShader        gUnderWaterProgram;

//interface shaders
LLHLSLShader        gHighlightProgram;
LLHLSLShader        gSkinnedHighlightProgram;
LLHLSLShader        gHighlightNormalProgram;
LLHLSLShader        gHighlightSpecularProgram;

LLHLSLShader        gDeferredHighlightProgram;

LLHLSLShader        gPathfindingProgram;
LLHLSLShader        gPathfindingNoNormalsProgram;

//avatar shader handles
LLHLSLShader        gAvatarProgram;
LLHLSLShader        gAvatarEyeballProgram;
LLHLSLShader        gImpostorProgram;

// Effects Shaders
LLHLSLShader            gGlowProgram;
LLHLSLShader            gGlowExtractProgram;
LLHLSLShader            gPostScreenSpaceReflectionProgram;

// Deferred rendering shaders
LLHLSLShader            gDeferredImpostorProgram;
LLHLSLShader            gDeferredJellyGhostProgram;
LLHLSLShader            gDeferredDiffuseProgram;
LLHLSLShader            gDeferredDiffuseAlphaMaskProgram;
LLHLSLShader            gDeferredSkinnedDiffuseAlphaMaskProgram;
LLHLSLShader            gDeferredNonIndexedDiffuseAlphaMaskProgram;
LLHLSLShader            gDeferredNonIndexedDiffuseAlphaMaskNoColorProgram;
LLHLSLShader            gDeferredSkinnedDiffuseProgram;
LLHLSLShader            gDeferredSkinnedBumpProgram;
LLHLSLShader            gDeferredBumpProgram;
LLHLSLShader            gDeferredTerrainProgram;
LLHLSLShader            gDeferredTreeProgram;
LLHLSLShader            gDeferredTreeShadowProgram;
LLHLSLShader            gDeferredSkinnedTreeShadowProgram;
LLHLSLShader            gDeferredAvatarProgram;
LLHLSLShader            gDeferredAvatarAlphaProgram;
LLHLSLShader            gDeferredLightProgram;
LLHLSLShader            gDeferredMultiLightProgram[16];
LLHLSLShader            gDeferredSpotLightProgram;
LLHLSLShader            gDeferredMultiSpotLightProgram;
LLHLSLShader            gDeferredSunProgram;
LLHLSLShader            gDeferredSunProbeProgram;
LLHLSLShader            gHazeProgram;
LLHLSLShader            gHazeWaterProgram;
LLHLSLShader            gDeferredBlurLightProgram;
LLHLSLShader            gDeferredTemporalResolveSSAOProgram;
LLHLSLShader            gDeferredSoftenProgram;
LLHLSLShader            gDeferredShadowProgram;
LLHLSLShader            gDeferredSkinnedShadowProgram;
LLHLSLShader            gDeferredShadowCubeProgram;
LLHLSLShader            gDeferredShadowAlphaMaskProgram;
LLHLSLShader            gDeferredSkinnedShadowAlphaMaskProgram;
LLHLSLShader            gDeferredShadowGLTFAlphaMaskProgram;
LLHLSLShader            gDeferredSkinnedShadowGLTFAlphaMaskProgram;
LLHLSLShader            gDeferredShadowGLTFAlphaBlendProgram;
LLHLSLShader            gDeferredSkinnedShadowGLTFAlphaBlendProgram;
LLHLSLShader            gDeferredShadowFullbrightAlphaMaskProgram;
LLHLSLShader            gDeferredSkinnedShadowFullbrightAlphaMaskProgram;
LLHLSLShader            gDeferredAvatarShadowProgram;
LLHLSLShader            gDeferredAvatarAlphaShadowProgram;
LLHLSLShader            gDeferredAvatarAlphaMaskShadowProgram;
LLHLSLShader            gDeferredAlphaProgram;
LLHLSLShader            gHUDAlphaProgram;
LLHLSLShader            gDeferredSkinnedAlphaProgram;
LLHLSLShader            gDeferredAlphaImpostorProgram;
LLHLSLShader            gDeferredSkinnedAlphaImpostorProgram;
LLHLSLShader            gDeferredAvatarEyesProgram;
LLHLSLShader            gDeferredFullbrightProgram;
LLHLSLShader            gHUDFullbrightProgram;
LLHLSLShader            gDeferredFullbrightAlphaMaskProgram;
LLHLSLShader            gHUDFullbrightAlphaMaskProgram;
LLHLSLShader            gDeferredFullbrightAlphaMaskAlphaProgram;
LLHLSLShader            gHUDFullbrightAlphaMaskAlphaProgram;
LLHLSLShader            gDeferredEmissiveProgram;
LLHLSLShader            gDeferredSkinnedEmissiveProgram;
LLHLSLShader            gDeferredPostProgram;
LLHLSLShader            gDeferredCoFProgram;
LLHLSLShader            gDeferredDoFCombineProgram;
LLHLSLShader            gDeferredPostTonemapProgram;
LLHLSLShader            gNoPostTonemapProgram;
LLHLSLShader            gDeferredPostTonemapGammaCorrectProgram;
LLHLSLShader            gNoPostTonemapGammaCorrectProgram;
LLHLSLShader            gDeferredPostTonemapLegacyGammaCorrectProgram;
LLHLSLShader            gNoPostTonemapLegacyGammaCorrectProgram;
LLHLSLShader            gDeferredPostGammaCorrectProgram;
LLHLSLShader            gLegacyPostGammaCorrectProgram;
LLHLSLShader            gResizeBicubicProgram;
LLHLSLShader            gExposureProgram;
LLHLSLShader            gExposureProgramNoFade;
LLHLSLShader            gLuminanceProgram;
LLHLSLShader            gFXAAProgram[4];
LLHLSLShader            gSMAAEdgeDetectProgram[4];
LLHLSLShader            gSMAABlendWeightsProgram[4];
LLHLSLShader            gSMAANeighborhoodBlendProgram[4];
LLHLSLShader            gCASProgram;
LLHLSLShader            gCASLegacyGammaProgram;
LLHLSLShader            gDeferredPostNoDoFProgram;
LLHLSLShader            gDeferredPostNoDoFNoiseProgram;
LLHLSLShader            gDeferredWLSkyProgram;
LLHLSLShader            gEnvironmentMapProgram;
LLHLSLShader            gDeferredWLCloudProgram;
LLHLSLShader            gDeferredWLSunProgram;
LLHLSLShader            gDeferredWLMoonProgram;
LLHLSLShader            gDeferredStarProgram;
LLHLSLShader            gDeferredStarShootingProgram; // S24 task #279 stage 2
LLHLSLShader            gDeferredFullbrightShinyProgram;
LLHLSLShader            gHUDFullbrightShinyProgram;
LLHLSLShader            gDeferredSkinnedFullbrightShinyProgram;
LLHLSLShader            gDeferredSkinnedFullbrightProgram;
LLHLSLShader            gDeferredSkinnedFullbrightAlphaMaskProgram;
LLHLSLShader            gDeferredSkinnedFullbrightAlphaMaskAlphaProgram;
LLHLSLShader            gNormalMapGenProgram;
LLHLSLShader            gDeferredGenBrdfLutProgram;
LLHLSLShader            gDeferredBufferVisualProgram;

// Deferred materials shaders
LLHLSLShader            gDeferredMaterialProgram[LLMaterial::SHADER_COUNT*2];
LLHLSLShader            gHUDPBROpaqueProgram;
LLHLSLShader            gPBRGlowProgram;
LLHLSLShader            gPBRGlowSkinnedProgram;
LLHLSLShader            gDeferredPBROpaqueProgram;
LLHLSLShader            gDeferredSkinnedPBROpaqueProgram;
LLHLSLShader            gHUDPBRAlphaProgram;
LLHLSLShader            gDeferredPBRAlphaProgram;
LLHLSLShader            gDeferredSkinnedPBRAlphaProgram;
LLHLSLShader            gDeferredPBRTerrainProgram[TERRAIN_PAINT_TYPE_COUNT];

LLHLSLShader            gGLTFPBRMetallicRoughnessProgram;


//helper for making a rigged variant of a given shader
static bool make_rigged_variant(LLHLSLShader& shader, LLHLSLShader& riggedShader)
{
    riggedShader.mName = llformat("Skinned %s", shader.mName.c_str());
    riggedShader.mFeatures = shader.mFeatures;
    riggedShader.mFeatures.hasObjectSkinning = true;
    riggedShader.mDefines = shader.mDefines;    // NOTE: Must come before addPermutation

    riggedShader.addPermutation("HAS_SKIN", "1");
    riggedShader.mShaderFiles = shader.mShaderFiles;
    riggedShader.mShaderLevel = shader.mShaderLevel;
    riggedShader.mShaderGroup = shader.mShaderGroup;

    shader.mRiggedVariant = &riggedShader;
    return riggedShader.createShader();
}

static void add_common_permutations(LLHLSLShader* shader)
{
    static LLCachedControl<bool> emissive(gSavedSettings, "RenderEnableEmissiveBuffer", false);

    if (emissive)
    {
        shader->addPermutation("HAS_EMISSIVE", "1");
    }
}


static bool make_gltf_variant(LLHLSLShader& shader, LLHLSLShader& variant, bool alpha_blend, bool rigged, bool unlit, bool multi_uv, bool use_sun_shadow)
{
    variant.mName = shader.mName.c_str();
    variant.mFeatures = shader.mFeatures;
    variant.mShaderFiles = shader.mShaderFiles;
    variant.mShaderLevel = shader.mShaderLevel;
    variant.mShaderGroup = shader.mShaderGroup;

    variant.mDefines = shader.mDefines;    // NOTE: Must come before addPermutation

    U32 node_size = 16 * 3;
    U32 max_nodes = gGLManager.mMaxUniformBlockSize / node_size;
    variant.addPermutation("MAX_NODES_PER_GLTF_OBJECT", std::to_string(max_nodes));

    U32 material_size = 16 * 12;
    U32 max_materials = gGLManager.mMaxUniformBlockSize / material_size;
    LLHLSLShader::sMaxGLTFMaterials = max_materials;

    variant.addPermutation("MAX_MATERIALS_PER_GLTF_OBJECT", std::to_string(max_materials));

    U32 max_vec4s = gGLManager.mMaxUniformBlockSize / 16;
    variant.addPermutation("MAX_UBO_VEC4S", std::to_string(max_vec4s));

    if (rigged)
    {
        variant.addPermutation("HAS_SKIN", "1");
    }

    if (unlit)
    {
        variant.addPermutation("UNLIT", "1");
    }

    if (multi_uv)
    {
        variant.addPermutation("MULTI_UV", "1");
    }

    if (alpha_blend)
    {
        variant.addPermutation("ALPHA_BLEND", "1");

        variant.mFeatures.calculatesLighting = false;
        variant.mFeatures.hasLighting = false;
        variant.mFeatures.isAlphaLighting = true;
        variant.mFeatures.hasSrgb = true;
        variant.mFeatures.calculatesAtmospherics = true;
        variant.mFeatures.hasAtmospherics = true;
        variant.mFeatures.hasGamma = true;
        variant.mFeatures.hasShadows = use_sun_shadow;
        variant.mFeatures.isDeferred = true; // include deferredUtils
        variant.mFeatures.hasReflectionProbes = true;

        if (use_sun_shadow)
        {
            variant.addPermutation("HAS_SUN_SHADOW", "1");
        }

        bool success = variant.createShader();
        llassert(success);

        // Alpha Shader Hack
        // See: LLRender::syncMatrices()
        variant.mFeatures.calculatesLighting = true;
        variant.mFeatures.hasLighting = true;

        return success;
    }
    else
    {
        return variant.createShader();
    }
}

static bool make_gltf_variants(LLHLSLShader& shader, bool use_sun_shadow)
{
    shader.mFeatures.mGLTF = true;
    shader.mGLTFVariants.resize(LLHLSLShader::NUM_GLTF_VARIANTS);

    for (U32 i = 0; i < LLHLSLShader::NUM_GLTF_VARIANTS; ++i)
    {
        bool alpha_blend = i & LLHLSLShader::GLTFVariant::ALPHA_BLEND;
        bool rigged = i & LLHLSLShader::GLTFVariant::RIGGED;
        bool unlit = i & LLHLSLShader::GLTFVariant::UNLIT;
        bool multi_uv = i & LLHLSLShader::GLTFVariant::MULTI_UV;

        if (!make_gltf_variant(shader, shader.mGLTFVariants[i], alpha_blend, rigged, unlit, multi_uv, use_sun_shadow))
        {
            return false;
        }
    }

    return true;
}

#ifdef SHOW_ASSERT
// return true if there are no redundant shaders in the given vector
// also checks for redundant variants
static bool no_redundant_shaders(const std::vector<LLHLSLShader*>& shaders)
{
    std::set<std::string> names;
    for (LLHLSLShader* shader : shaders)
    {
        if (names.find(shader->mName) != names.end())
        {
            LL_WARNS("Shader") << "Redundant shader: " << shader->mName << LL_ENDL;
            return false;
        }
        names.insert(shader->mName);

        if (shader->mRiggedVariant)
        {
            if (names.find(shader->mRiggedVariant->mName) != names.end())
            {
                LL_WARNS("Shader") << "Redundant shader: " << shader->mRiggedVariant->mName << LL_ENDL;
                return false;
            }
            names.insert(shader->mRiggedVariant->mName);
        }
    }
    return true;
}
#endif


LLViewerShaderMgr::LLViewerShaderMgr() :
    mShaderLevel(SHADER_COUNT, 0),
    mMaxAvatarShaderLevel(0)
{
}

LLViewerShaderMgr::~LLViewerShaderMgr()
{
    mShaderLevel.clear();
    mShaderList.clear();
}

void LLViewerShaderMgr::finalizeShaderList()
{
    //ONLY shaders that need WL Param management should be added here
    mShaderList.push_back(&gAvatarProgram);
    mShaderList.push_back(&gWaterProgram);
    mShaderList.push_back(&gAvatarEyeballProgram);
    mShaderList.push_back(&gImpostorProgram);
    mShaderList.push_back(&gObjectBumpProgram);
    mShaderList.push_back(&gObjectFullbrightAlphaMaskProgram);
    mShaderList.push_back(&gObjectAlphaMaskNoColorProgram);
    mShaderList.push_back(&gUnderWaterProgram);
    mShaderList.push_back(&gDeferredSunProgram);
    mShaderList.push_back(&gDeferredSunProbeProgram);
    mShaderList.push_back(&gHazeProgram);
    mShaderList.push_back(&gHazeWaterProgram);
    mShaderList.push_back(&gDeferredSoftenProgram);
    mShaderList.push_back(&gDeferredAlphaProgram);
    mShaderList.push_back(&gHUDAlphaProgram);
    mShaderList.push_back(&gDeferredAlphaImpostorProgram);
    mShaderList.push_back(&gDeferredFullbrightProgram);
    mShaderList.push_back(&gHUDFullbrightProgram);
    mShaderList.push_back(&gDeferredFullbrightAlphaMaskProgram);
    mShaderList.push_back(&gHUDFullbrightAlphaMaskProgram);
    mShaderList.push_back(&gDeferredFullbrightAlphaMaskAlphaProgram);
    mShaderList.push_back(&gHUDFullbrightAlphaMaskAlphaProgram);
    mShaderList.push_back(&gDeferredFullbrightShinyProgram);
    mShaderList.push_back(&gHUDFullbrightShinyProgram);
    mShaderList.push_back(&gDeferredEmissiveProgram);
    mShaderList.push_back(&gDeferredAvatarEyesProgram);
    mShaderList.push_back(&gDeferredAvatarAlphaProgram);
    mShaderList.push_back(&gEnvironmentMapProgram);
    mShaderList.push_back(&gDeferredWLSkyProgram);
    mShaderList.push_back(&gDeferredWLCloudProgram);
    mShaderList.push_back(&gDeferredWLMoonProgram);
    mShaderList.push_back(&gDeferredWLSunProgram);
    mShaderList.push_back(&gDeferredPBRAlphaProgram);
    mShaderList.push_back(&gHUDPBRAlphaProgram);
    mShaderList.push_back(&gDeferredPostTonemapProgram);
    mShaderList.push_back(&gNoPostTonemapProgram);
    mShaderList.push_back(&gDeferredPostTonemapGammaCorrectProgram);
    mShaderList.push_back(&gNoPostTonemapGammaCorrectProgram);
    mShaderList.push_back(&gDeferredPostTonemapLegacyGammaCorrectProgram);
    mShaderList.push_back(&gNoPostTonemapLegacyGammaCorrectProgram);
    mShaderList.push_back(&gCASLegacyGammaProgram);
    mShaderList.push_back(&gDeferredPostGammaCorrectProgram); // for gamma
    mShaderList.push_back(&gLegacyPostGammaCorrectProgram);
    mShaderList.push_back(&gDeferredDiffuseProgram);
    mShaderList.push_back(&gDeferredBumpProgram);
    mShaderList.push_back(&gDeferredPBROpaqueProgram);
    // S24 (2026-08-26, task #262): gHUDPBROpaqueProgram was the one shader
    // in the whole Deferred/HUD pairing above missing its push_back - every
    // other "Deferred X" -> "HUD X" pair is registered together (Alpha,
    // Fullbright x3, PBRAlpha, etc.), this one wasn't. Confirmed via
    // llhlslshader.cpp's LLHLSLShader::bind(): a shader only gets
    // LLShaderMgr::updateShaderUniforms() called for it (WindLight/
    // environment param propagation - gamma, sun/ambient, atmospherics)
    // when mUniformsDirty is true, which is ONLY ever set by
    // LLEnvironment::update()'s per-frame loop over THIS list
    // (beginShaders()/endShaders()) - a shader missing from this list never
    // receives those uniforms at all, same "never uploaded, not a math bug"
    // class as the amblit/sunlit/atten bug already diagnosed for
    // softenLightF.hlsl (see this file's DX_RENDER branch of bind()).
    // Symptom this explains: PBR-opaque materials on HUD attachments
    // (routed to gHUDPBROpaqueProgram, dxrender/../pbropaqueF.hlsl's IS_HUD
    // branch) render solid black; switching the face to any alpha-blend
    // mode routes it through gHUDPBRAlphaProgram instead - which WAS
    // correctly registered here - masking the real gap as an "alpha fixes
    // it" workaround. User feedback (public alpha 0.1), not locally
    // reproducible - fix is a direct pattern match against every other
    // already-working Deferred/HUD pair above, not live-verified.
    mShaderList.push_back(&gHUDPBROpaqueProgram);

    if (gSavedSettings.getBOOL("GLTFEnabled"))
    {
        mShaderList.push_back(&gGLTFPBRMetallicRoughnessProgram);
    }

    mShaderList.push_back(&gDeferredAvatarProgram);
    mShaderList.push_back(&gDeferredTerrainProgram);

    for (U32 paint_type = 0; paint_type < TERRAIN_PAINT_TYPE_COUNT; ++paint_type)
    {
        mShaderList.push_back(&gDeferredPBRTerrainProgram[paint_type]);
    }

    mShaderList.push_back(&gDeferredDiffuseAlphaMaskProgram);
    mShaderList.push_back(&gDeferredNonIndexedDiffuseAlphaMaskProgram);
    mShaderList.push_back(&gDeferredTreeProgram);

    // make sure there are no redundancies
    llassert(no_redundant_shaders(mShaderList));
}

// static
LLViewerShaderMgr * LLViewerShaderMgr::instance()
{
    if(NULL == sInstance)
    {
        sInstance = new LLViewerShaderMgr();
    }

    return static_cast<LLViewerShaderMgr*>(sInstance);
}

// static
void LLViewerShaderMgr::releaseInstance()
{
    if (sInstance != NULL)
    {
        delete sInstance;
        sInstance = NULL;
    }
}

// static
void LLViewerShaderMgr::purgeShaderCache()
{
    std::string shader_cache_dir = gDirUtilp->getExpandedFilename(LL_PATH_CACHE, "shader_cache");

    if (gDirUtilp->fileExists(shader_cache_dir))
    {
        LL_INFOS("ShaderCache") << "S24: Purging shader cache directory: " << shader_cache_dir << LL_ENDL;

        // Delete all files in the shader_cache directory
        S32 deleted = gDirUtilp->deleteFilesInDir(shader_cache_dir, "*");

        LL_INFOS("ShaderCache") << "S24: Deleted " << deleted << " shader cache files. Shaders will rebuild on next launch." << LL_ENDL;
    }
    else
    {
        LL_INFOS("ShaderCache") << "S24: Shader cache directory not found: " << shader_cache_dir << LL_ENDL;
    }
}

void LLViewerShaderMgr::initAttribsAndUniforms(void)
{
    if (mReservedAttribs.empty())
    {
        LLShaderMgr::initAttribsAndUniforms();
    }
}


//============================================================================
// Set Levels

S32 LLViewerShaderMgr::getShaderLevel(S32 type)
{
    return mShaderLevel[type];
}

//============================================================================
// Shader Management

void LLViewerShaderMgr::requestDeferredShaderReload()
{
    mDeferredShaderReloadPending = true;
    LL_INFOS("ShaderLoading") << "Shader reload requested, will process on next frame" << LL_ENDL;
}

void LLViewerShaderMgr::processDeferredShaderReload()
{
    if (mDeferredShaderReloadPending)
    {
        mDeferredShaderReloadPending = false;
        LL_INFOS("ShaderLoading") << "Processing deferred shader reload" << LL_ENDL;
        setShaders();
    }
}

void LLViewerShaderMgr::setShaders()
{
    //setShaders might be called redundantly by gSavedSettings, so return on reentrance
    static bool reentrance = false;

    if (!gPipeline.mInitialized || !sInitialized || reentrance || sSkipReload)
    {
        return;
    }

    mShaderList.clear();

    if (!gGLManager.mHasRequirements)
    {
        // Viewer will show 'hardware requirements' warning later
        LL_INFOS("ShaderLoading") << "Not supported hardware/software" << LL_ENDL;
        return;
    }

    {
        static LLCachedControl<bool> shader_cache_enabled(gSavedSettings, "RenderShaderCacheEnabled", true);
        static LLUUID old_cache_version;
        static LLUUID current_cache_version;
        if (current_cache_version.isNull())
        {
            HBXXH128 hash_obj;
            hash_obj.update(LLVersionInfo::instance().getVersion());
            current_cache_version = hash_obj.digest();

            old_cache_version = LLUUID(gSavedSettings.getString("RenderShaderCacheVersion"));
            gSavedSettings.setString("RenderShaderCacheVersion", current_cache_version.asString());
        }

        initShaderCache(
            shader_cache_enabled,
            old_cache_version,
            current_cache_version,
            LLAppViewer::instance()->isSecondInstance());
    }

    static LLCachedControl<U32> max_texture_index(gSavedSettings, "RenderMaxTextureIndex", 16);

    // when using indexed texture rendering, leave some texture units available for shadow and reflection maps
    static LLCachedControl<S32> reserved_texture_units(gSavedSettings, "RenderReservedTextureIndices", 14);

    LLHLSLShader::sIndexedTextureChannels = 4;
        //llclamp<S32>(max_texture_index, 1, gGLManager.mNumTextureImageUnits-reserved_texture_units);

    reentrance = true;

    // Make sure the compiled shader map is cleared before we recompile shaders.
    mVertexShaderObjects.clear();
    mFragmentShaderObjects.clear();

    initAttribsAndUniforms();
    gPipeline.releaseGLBuffers();

    unloadShaders();

    LLPipeline::sRenderGlow = gSavedSettings.getBOOL("RenderGlow");
    LLPipeline::RenderAvatarCloth = gSavedSettings.getBOOL("RenderAvatarCloth");

    if (gViewerWindow)
    {
        gViewerWindow->setCursor(UI_CURSOR_WAIT);
    }

    // Shaders
    LL_INFOS("ShaderLoading") << "\n~~~~~~~~~~~~~~~~~~\n Loading Shaders:\n~~~~~~~~~~~~~~~~~~" << LL_ENDL;
    LL_INFOS("ShaderLoading") << llformat("Using GLSL %d.%d", gGLManager.mGLSLVersionMajor, gGLManager.mGLSLVersionMinor) << LL_ENDL;

    for (S32 i = 0; i < SHADER_COUNT; i++)
    {
        mShaderLevel[i] = 0;
    }
    mMaxAvatarShaderLevel = 0;

    LLVertexBuffer::unbind();

#ifndef DX_RENDER
    // gGLManager.mGLSLVersionMajor/Minor are never populated under
    // DX_RENDER (initGL() is never called - switchContext() replaces the
    // whole GL setup with initDX11Context()), so this and the other
    // gGLManager.mGLSLVersion* checks below would always evaluate false,
    // not because of any real hardware limitation.
    llassert((gGLManager.mGLSLVersionMajor > 1 || gGLManager.mGLSLVersionMinor >= 10));
#endif


    S32 light_class = 3;
    S32 interface_class = 2;
    S32 env_class = 2;
    S32 obj_class = 2;
    S32 effect_class = 2;
    S32 wl_class = 2;
    S32 water_class = 3;
    S32 deferred_class = 3;

    // Trigger a full rebuild of the fallback skybox / cubemap if we've toggled windlight shaders
    if (!wl_class || (mShaderLevel[SHADER_WINDLIGHT] != wl_class && gSky.mVOSkyp.notNull()))
    {
        gSky.mVOSkyp->forceSkyUpdate();
    }

    // Load lighting shaders
    mShaderLevel[SHADER_LIGHTING] = light_class;
    mShaderLevel[SHADER_INTERFACE] = interface_class;
    mShaderLevel[SHADER_ENVIRONMENT] = env_class;
    mShaderLevel[SHADER_WATER] = water_class;
    mShaderLevel[SHADER_OBJECT] = obj_class;
    mShaderLevel[SHADER_EFFECT] = effect_class;
    mShaderLevel[SHADER_WINDLIGHT] = wl_class;
    mShaderLevel[SHADER_DEFERRED] = deferred_class;

    std::string shader_name = loadBasicShaders();
    if (shader_name.empty())
    {
        LL_INFOS("Shader") << "Loaded basic shaders." << LL_ENDL;
    }
    else
    {
        // "ShaderLoading" and "Shader" need to be logged
        LL_WARNS("Shader") << "Failed loading basic shaders.  Retrying with increased log level..." << LL_ENDL;

        LLError::ELevel lvl = LLError::getDefaultLevel();
        LLError::setDefaultLevel(LLError::LEVEL_DEBUG);
        loadBasicShaders();
        LLError::setDefaultLevel(lvl);
        gGLManager.printGLInfoString();
        LL_ERRS() << "Unable to load basic shader " << shader_name << ", verify graphics driver installed and current." << LL_ENDL;
        reentrance = false; // For hygiene only, re-try probably helps nothing
        return;
    }

    gPipeline.mShadersLoaded = true;

    bool loaded = loadShadersWater();

    if (loaded)
    {
        LL_INFOS() << "Loaded water shaders." << LL_ENDL;
    }
    else
    {
        LL_WARNS() << "Failed to load water shaders." << LL_ENDL;
        llassert(loaded);
    }

    if (loaded)
    {
        loaded = loadShadersEffects();
        if (loaded)
        {
            LL_INFOS() << "Loaded effects shaders." << LL_ENDL;
        }
        else
        {
            LL_WARNS() << "Failed to load effects shaders." << LL_ENDL;
            llassert(loaded);
        }
    }

    if (loaded)
    {
        loaded = loadShadersInterface();
        if (loaded)
        {
            LL_INFOS() << "Loaded interface shaders." << LL_ENDL;
        }
        else
        {
            LL_WARNS() << "Failed to load interface shaders." << LL_ENDL;
            llassert(loaded);
        }
    }

    if (loaded)
    {
        // Load max avatar shaders to set the max level
        mShaderLevel[SHADER_AVATAR] = 3;
        mMaxAvatarShaderLevel = 3;

        if (loadShadersObject())
        { //hardware skinning is enabled and rigged attachment shaders loaded correctly
            // cloth is a class3 shader
            S32 avatar_class = 1;

            // Set the actual level
            mShaderLevel[SHADER_AVATAR] = avatar_class;

            loaded = loadShadersAvatar();
            llassert(loaded);
        }
        else
        { //hardware skinning not possible, neither is deferred rendering
            llassert(false); // SHOULD NOT BE POSSIBLE
        }
    }

    llassert(loaded);
    loaded = loaded && loadShadersDeferred();
    llassert(loaded);

    if (!LLAppViewer::instance()->isSecondInstance())
    {
    persistShaderCacheMetadata();
    }

    if (gViewerWindow)
    {
        gViewerWindow->setCursor(UI_CURSOR_ARROW);
    }
    gPipeline.createGLBuffers();

    finalizeShaderList();

    reentrance = false;
}

void LLViewerShaderMgr::unloadShaders()
{
    while (!LLHLSLShader::sInstances.empty())
    {
        LLHLSLShader* shader = *(LLHLSLShader::sInstances.begin());
        shader->unload();
    }

    mShaderLevel[SHADER_LIGHTING] = 0;
    mShaderLevel[SHADER_OBJECT] = 0;
    mShaderLevel[SHADER_AVATAR] = 0;
    mShaderLevel[SHADER_ENVIRONMENT] = 0;
    mShaderLevel[SHADER_WATER] = 0;
    mShaderLevel[SHADER_INTERFACE] = 0;
    mShaderLevel[SHADER_EFFECT] = 0;
    mShaderLevel[SHADER_WINDLIGHT] = 0;

    gPipeline.mShadersLoaded = false;
}

std::string LLViewerShaderMgr::loadBasicShaders()
{
    // Load basic dependency shaders first
    // All of these have to load for any shaders to function

    S32 sum_lights_class = 3;

#if LL_DARWIN
    // Work around driver crashes on older Macs when using deferred rendering
    // NORSPEC-59
    //
    if (gGLManager.mIsMobileGF)
        sum_lights_class = 3;
#endif

    // Use the feature table to mask out the max light level to use.  Also make sure it's at least 1.
    S32 max_light_class = gSavedSettings.getS32("RenderShaderLightingMaxLevel");
    sum_lights_class = llclamp(sum_lights_class, 1, max_light_class);

    // Load the Basic Vertex Shaders at the appropriate level.
    // (in order of shader function call depth for reference purposes, deepest level first)

    vector< pair<string, S32> > shaders;
    shaders.push_back( make_pair( "windlight/atmosphericsVarsV.glsl",       mShaderLevel[SHADER_WINDLIGHT] ) );
    shaders.push_back( make_pair( "windlight/atmosphericsHelpersV.glsl",    mShaderLevel[SHADER_WINDLIGHT] ) );
    shaders.push_back( make_pair( "lighting/lightFuncV.glsl",               mShaderLevel[SHADER_LIGHTING] ) );
    shaders.push_back( make_pair( "lighting/sumLightsV.glsl",               sum_lights_class ) );
    shaders.push_back( make_pair( "lighting/lightV.glsl",                   mShaderLevel[SHADER_LIGHTING] ) );
    shaders.push_back( make_pair( "lighting/lightFuncSpecularV.glsl",       mShaderLevel[SHADER_LIGHTING] ) );
    shaders.push_back( make_pair( "lighting/sumLightsSpecularV.glsl",       sum_lights_class ) );
    shaders.push_back( make_pair( "lighting/lightSpecularV.glsl",           mShaderLevel[SHADER_LIGHTING] ) );
    shaders.push_back( make_pair( "windlight/atmosphericsFuncs.glsl",       mShaderLevel[SHADER_WINDLIGHT] ) );
    shaders.push_back( make_pair( "windlight/atmosphericsV.glsl",           mShaderLevel[SHADER_WINDLIGHT] ) );
    shaders.push_back( make_pair( "environment/srgbF.glsl",                 1 ) );
    shaders.push_back( make_pair( "avatar/avatarSkinV.glsl",                1 ) );
    shaders.push_back( make_pair( "avatar/objectSkinV.glsl",                1 ) );
    shaders.push_back( make_pair( "deferred/textureUtilV.glsl",             1 ) );
#ifdef DX_RENDER
    // See the comment on the analogous check in loadShadersInterface() -
    // gGLManager.mGLSLVersionMajor/Minor are never populated under
    // DX_RENDER, so this hardware-capability check would always
    // (wrongly) evaluate false, leaving this file uncached even though
    // attachShaderFeatures() may still try to attach it based on a
    // shader's own mFeatures.mIndexedTextureChannels, unrelated to this
    // GL-version gate.
    shaders.push_back( make_pair( "objects/indexedTextureV.glsl",           1 ) );
#else
    if (gGLManager.mGLSLVersionMajor >= 2 || gGLManager.mGLSLVersionMinor >= 30)
    {
        shaders.push_back( make_pair( "objects/indexedTextureV.glsl",           1 ) );
    }
#endif
    shaders.push_back( make_pair( "objects/nonindexedTextureV.glsl",        1 ) );

    std::map<std::string, std::string> attribs;
    attribs["MAX_JOINTS_PER_MESH_OBJECT"] =
        std::to_string(LLSkinningUtil::getMaxJointCount());

    static LLCachedControl<bool> emissive(gSavedSettings, "RenderEnableEmissiveBuffer", false);

    if (emissive)
    {
        attribs["HAS_EMISSIVE"] = "1";
    }

    bool ssr = gSavedSettings.getBOOL("RenderScreenSpaceReflections");

    bool mirrors = gSavedSettings.getBOOL("RenderMirrors");

    bool has_reflection_probes = gSavedSettings.getBOOL("RenderReflectionsEnabled") && gGLManager.mGLVersion > 3.99f;

    S32 probe_level = llclamp(gSavedSettings.getS32("RenderReflectionProbeLevel"), 0, 3);

    S32 shadow_detail            = gSavedSettings.getS32("RenderShadowDetail");

    if (shadow_detail >= 1)
    {
        attribs["SUN_SHADOW"] = "1";

        if (shadow_detail >= 2)
        {
            attribs["SPOT_SHADOW"] = "1";
        }
    }

    if (ssr)
    {
        attribs["SSR"] = "1";
    }

    // S24 (2026-09-07, re-applied after an accidental svn revert wiped the
    // original uncommitted fix): REFMAP_LEVEL/REF_SAMPLE_COUNT must be
    // defined unconditionally, not just when has_reflection_probes is true -
    // reflectionProbeF.hlsl uses REF_SAMPLE_COUNT as a fixed HLSL array size
    // (`static int probeIndex[REF_SAMPLE_COUNT];`) regardless of that flag,
    // so leaving it undefined for any shader that attaches this file with
    // has_reflection_probes false is a real compile-time gap, not a cosmetic
    // one - this is the direct cause of "CTD disabling SSR" (see the
    // matching class-tier fix on reflectionProbeF.glsl/screenSpaceReflUtil.glsl
    // just below - same root bug class, same fix session).
    attribs["REFMAP_LEVEL"] = std::to_string(probe_level);
    attribs["REF_SAMPLE_COUNT"] = "32";

    if (mirrors)
    {
        attribs["HERO_PROBES"] = "1";
    }

    { // PBR terrain
        const S32 mapping = clamp_terrain_mapping(gSavedSettings.getS32("RenderTerrainPBRPlanarSampleCount"));
        attribs["TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT"] = llformat("%d", mapping);
        const F32 triplanar_factor = gSavedSettings.getF32("RenderTerrainPBRTriplanarBlendFactor");
        attribs["TERRAIN_TRIPLANAR_BLEND_FACTOR"] = llformat("%.2f", triplanar_factor);
        S32 detail = gSavedSettings.getS32("RenderTerrainPBRDetail");
        detail = llclamp(detail, TERRAIN_PBR_DETAIL_MIN, TERRAIN_PBR_DETAIL_MAX);
        attribs["TERRAIN_PBR_DETAIL"] = llformat("%d", detail);
    }

    LLHLSLShader::sGlobalDefines = attribs;

    // We no longer have to bind the shaders to global glhandles, they are automatically added to a map now.
    for (U32 i = 0; i < shaders.size(); i++)
    {
        // Note usage of GL_VERTEX_SHADER
        if (loadShaderFile(shaders[i].first, shaders[i].second, GL_VERTEX_SHADER, &attribs) == 0)
        {
            LL_WARNS("Shader") << "Failed to load basic vertex shader " << i << ": " << shaders[i].first << LL_ENDL;
            return shaders[i].first;
        }
    }

    // Load the Basic Fragment Shaders at the appropriate level.
    // (in order of shader function call depth for reference purposes, deepest level first)

    shaders.clear();
    S32 ch = 1;

#ifdef DX_RENDER
    // See the comment on the analogous vertex-shader-list check above -
    // gGLManager.mGLSLVersionMajor/Minor are never populated under
    // DX_RENDER, so this would always (wrongly) fall back to ch=1 instead
    // of the real indexed-texture-channel count.
    ch = llmax(LLHLSLShader::sIndexedTextureChannels, 1);
#else
    if (gGLManager.mGLSLVersionMajor > 1 || gGLManager.mGLSLVersionMinor >= 30)
    { //use indexed texture rendering for GLSL >= 1.30
        ch = llmax(LLHLSLShader::sIndexedTextureChannels, 1);
    }
#endif


    std::vector<S32> index_channels;
    index_channels.push_back(-1);    shaders.push_back( make_pair( "windlight/atmosphericsVarsF.glsl",      mShaderLevel[SHADER_WINDLIGHT] ) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "windlight/atmosphericsHelpersF.glsl",       mShaderLevel[SHADER_WINDLIGHT] ) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "windlight/gammaF.glsl",                 mShaderLevel[SHADER_WINDLIGHT]) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "windlight/atmosphericsFuncs.glsl",       mShaderLevel[SHADER_WINDLIGHT] ) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "windlight/atmosphericsF.glsl",          mShaderLevel[SHADER_WINDLIGHT] ) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "environment/waterFogF.glsl",                mShaderLevel[SHADER_WATER] ) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "environment/srgbF.glsl",                    mShaderLevel[SHADER_ENVIRONMENT] ) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "deferred/deferredUtil.glsl",                    1) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "deferred/gbufferUtil.glsl",                    1) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "deferred/globalF.glsl",                          1));
    index_channels.push_back(-1);    shaders.push_back( make_pair( "deferred/shadowUtil.glsl",                      1) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "deferred/aoUtil.glsl",                          1) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "deferred/pbrterrainUtilF.glsl",                 1) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "deferred/tonemapUtilF.glsl",                    1) );
    // S24 (2026-09-07, re-applied after an accidental svn revert wiped the
    // original uncommitted fix): both of these were previously cached at a
    // LOWER class tier whenever has_reflection_probes/ssr was false (e.g.
    // right after the user disables SSR) - LLShaderMgr::mFragmentShaderSourceText
    // caches this file's resolved source ONCE, keyed by bare filename, then
    // reuses that SAME cached text for every later shader that attaches it
    // regardless of THAT shader's own class level. A shader still needing
    // the higher-tier body (e.g. one compiled earlier this same session,
    // before the toggle) attaching the now-lower-tier cached text is the
    // real, confirmed cause of "CTD disabling SSR" - always request the
    // highest tier (3) unconditionally so the cached text is never
    // downgraded out from under a shader that needs it.
    index_channels.push_back(-1);    shaders.push_back( make_pair( "deferred/reflectionProbeF.glsl",                3) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "deferred/screenSpaceReflUtil.glsl",             3) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "lighting/lightNonIndexedF.glsl",                    mShaderLevel[SHADER_LIGHTING] ) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "lighting/lightAlphaMaskNonIndexedF.glsl",                   mShaderLevel[SHADER_LIGHTING] ) );
    index_channels.push_back(ch);    shaders.push_back( make_pair( "lighting/lightF.glsl",                  mShaderLevel[SHADER_LIGHTING] ) );
    index_channels.push_back(ch);    shaders.push_back( make_pair( "lighting/lightAlphaMaskF.glsl",                 mShaderLevel[SHADER_LIGHTING] ) );

    for (U32 i = 0; i < shaders.size(); i++)
    {
        // Note usage of GL_FRAGMENT_SHADER
        if (loadShaderFile(shaders[i].first, shaders[i].second, GL_FRAGMENT_SHADER, &attribs, index_channels[i]) == 0)
        {
            LL_WARNS("Shader") << "Failed to load fragment shader " << shaders[i].first << LL_ENDL;
            return shaders[i].first;
        }
    }

    return std::string();
}

bool LLViewerShaderMgr::loadShadersWater()
{
    bool success = true;
    bool terrainWaterSuccess = true;

    bool use_sun_shadow = mShaderLevel[SHADER_DEFERRED] > 1 &&
        gSavedSettings.getS32("RenderShadowDetail") > 0;

    if (mShaderLevel[SHADER_WATER] == 0)
    {
        gWaterProgram.unload();
        gUnderWaterProgram.unload();
        return true;
    }

    if (success)
    {
        // load water shader
        gWaterProgram.mName = "Water Shader";
        gWaterProgram.mFeatures.calculatesAtmospherics = true;
        gWaterProgram.mFeatures.hasAtmospherics = true;
        gWaterProgram.mFeatures.hasGamma = true;
        gWaterProgram.mFeatures.hasSrgb = true;
        gWaterProgram.mFeatures.hasReflectionProbes = true;
        gWaterProgram.mFeatures.hasTonemap = true;
        gWaterProgram.mFeatures.hasShadows = use_sun_shadow;
        gWaterProgram.mShaderFiles.clear();
        gWaterProgram.mShaderFiles.push_back(make_pair("environment/waterV.glsl", GL_VERTEX_SHADER));
        gWaterProgram.mShaderFiles.push_back(make_pair("environment/waterF.glsl", GL_FRAGMENT_SHADER));
        gWaterProgram.clearPermutations();
        if (LLPipeline::sRenderTransparentWater)
        {
            gWaterProgram.addPermutation("TRANSPARENT_WATER", "1");
        }

        if (use_sun_shadow)
        {
            gWaterProgram.addPermutation("HAS_SUN_SHADOW", "1");
        }

        gWaterProgram.mShaderGroup = LLHLSLShader::SG_WATER;
        gWaterProgram.mShaderLevel = mShaderLevel[SHADER_WATER];
        success = gWaterProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        //load under water vertex shader
        gUnderWaterProgram.mName = "Underwater Shader";
        gUnderWaterProgram.mFeatures.calculatesAtmospherics = true;
        gUnderWaterProgram.mFeatures.hasAtmospherics = true;
        gUnderWaterProgram.mShaderFiles.clear();
        gUnderWaterProgram.mShaderFiles.push_back(make_pair("environment/waterV.glsl", GL_VERTEX_SHADER));
        gUnderWaterProgram.mShaderFiles.push_back(make_pair("environment/underWaterF.glsl", GL_FRAGMENT_SHADER));
        gUnderWaterProgram.mShaderLevel = mShaderLevel[SHADER_WATER];
        gUnderWaterProgram.mShaderGroup = LLHLSLShader::SG_WATER;
        gUnderWaterProgram.clearPermutations();
        if (LLPipeline::sRenderTransparentWater)
        {
            gUnderWaterProgram.addPermutation("TRANSPARENT_WATER", "1");
        }
        success = gUnderWaterProgram.createShader();
        llassert(success);
    }

    /// Keep track of water shader levels
    if (gWaterProgram.mShaderLevel != mShaderLevel[SHADER_WATER]
        || gUnderWaterProgram.mShaderLevel != mShaderLevel[SHADER_WATER])
    {
        mShaderLevel[SHADER_WATER] = llmin(gWaterProgram.mShaderLevel, gUnderWaterProgram.mShaderLevel);
    }

    if (!success)
    {
        mShaderLevel[SHADER_WATER] = 0;
        return false;
    }

    // if we failed to load the terrain water shaders and we need them (using class2 water),
    // then drop down to class1 water.
    if (mShaderLevel[SHADER_WATER] > 1 && !terrainWaterSuccess)
    {
        mShaderLevel[SHADER_WATER]--;
        return loadShadersWater();
    }

    LLWorld::getInstance()->updateWaterObjects();

    return true;
}

bool LLViewerShaderMgr::loadShadersEffects()
{
    bool success = true;

    if (mShaderLevel[SHADER_EFFECT] == 0)
    {
        gGlowProgram.unload();
        gGlowExtractProgram.unload();
        return true;
    }

    if (success)
    {
        gGlowProgram.mName = "Glow Shader (Post)";
        gGlowProgram.mShaderFiles.clear();
        gGlowProgram.mShaderFiles.push_back(make_pair("effects/glowV.glsl", GL_VERTEX_SHADER));
        gGlowProgram.mShaderFiles.push_back(make_pair("effects/glowF.glsl", GL_FRAGMENT_SHADER));
        gGlowProgram.mShaderLevel = mShaderLevel[SHADER_EFFECT];
        success = gGlowProgram.createShader();
        if (!success)
        {
            LLPipeline::sRenderGlow = false;
        }
    }

    if (success)
    {
        const bool use_glow_noise = gSavedSettings.getBOOL("RenderGlowNoise");
        const std::string glow_noise_label = use_glow_noise ? " (+Noise)" : "";

        gGlowExtractProgram.mName = llformat("Glow Extract Shader (Post)%s", glow_noise_label.c_str());
        gGlowExtractProgram.mShaderFiles.clear();
        gGlowExtractProgram.mShaderFiles.push_back(make_pair("effects/glowExtractV.glsl", GL_VERTEX_SHADER));
        gGlowExtractProgram.mShaderFiles.push_back(make_pair("effects/glowExtractF.glsl", GL_FRAGMENT_SHADER));
        gGlowExtractProgram.mShaderLevel = mShaderLevel[SHADER_EFFECT];

        if (use_glow_noise)
        {
            gGlowExtractProgram.addPermutation("HAS_NOISE", "1");
        }

        success = gGlowExtractProgram.createShader();
        if (!success)
        {
            LLPipeline::sRenderGlow = false;
        }
    }

    return success;

}

bool LLViewerShaderMgr::loadShadersDeferred()
{
    bool use_sun_shadow = mShaderLevel[SHADER_DEFERRED] > 1 &&
        gSavedSettings.getS32("RenderShadowDetail") > 0;

    if (mShaderLevel[SHADER_DEFERRED] == 0)
    {
        gDeferredTreeProgram.unload();
        gDeferredTreeShadowProgram.unload();
        gDeferredSkinnedTreeShadowProgram.unload();
        gDeferredDiffuseProgram.unload();
        gDeferredSkinnedDiffuseProgram.unload();
        gDeferredDiffuseAlphaMaskProgram.unload();
        gDeferredSkinnedDiffuseAlphaMaskProgram.unload();
        gDeferredNonIndexedDiffuseAlphaMaskProgram.unload();
        gDeferredNonIndexedDiffuseAlphaMaskNoColorProgram.unload();
        gDeferredBumpProgram.unload();
        gDeferredSkinnedBumpProgram.unload();
        gDeferredImpostorProgram.unload();
        gDeferredJellyGhostProgram.unload();
        gDeferredTerrainProgram.unload();
        gDeferredLightProgram.unload();
        for (U32 i = 0; i < LL_DEFERRED_MULTI_LIGHT_COUNT; ++i)
        {
            gDeferredMultiLightProgram[i].unload();
        }
        gDeferredSpotLightProgram.unload();
        gDeferredMultiSpotLightProgram.unload();
        gDeferredSunProgram.unload();
        gDeferredBlurLightProgram.unload();
        gDeferredTemporalResolveSSAOProgram.unload();
        gDeferredSoftenProgram.unload();
        gDeferredShadowProgram.unload();
        gDeferredSkinnedShadowProgram.unload();
        gDeferredShadowCubeProgram.unload();
        gDeferredShadowAlphaMaskProgram.unload();
        gDeferredSkinnedShadowAlphaMaskProgram.unload();
        gDeferredShadowGLTFAlphaMaskProgram.unload();
        gDeferredSkinnedShadowGLTFAlphaMaskProgram.unload();
        gDeferredShadowFullbrightAlphaMaskProgram.unload();
        gDeferredSkinnedShadowFullbrightAlphaMaskProgram.unload();
        gDeferredAvatarShadowProgram.unload();
        gDeferredAvatarAlphaShadowProgram.unload();
        gDeferredAvatarAlphaMaskShadowProgram.unload();
        gDeferredAvatarProgram.unload();
        gDeferredAvatarAlphaProgram.unload();
        gDeferredAlphaProgram.unload();
        gHUDAlphaProgram.unload();
        gDeferredSkinnedAlphaProgram.unload();
        gDeferredFullbrightProgram.unload();
        gHUDFullbrightProgram.unload();
        gDeferredFullbrightAlphaMaskProgram.unload();
        gHUDFullbrightAlphaMaskProgram.unload();
        gDeferredFullbrightAlphaMaskAlphaProgram.unload();
        gHUDFullbrightAlphaMaskAlphaProgram.unload();
        gDeferredEmissiveProgram.unload();
        gDeferredSkinnedEmissiveProgram.unload();
        gDeferredAvatarEyesProgram.unload();
        gDeferredPostProgram.unload();
        gDeferredCoFProgram.unload();
        gDeferredDoFCombineProgram.unload();
        gExposureProgram.unload();
        gExposureProgramNoFade.unload();
        gLuminanceProgram.unload();
        gDeferredPostGammaCorrectProgram.unload();
        gLegacyPostGammaCorrectProgram.unload();
        gResizeBicubicProgram.unload();
        gDeferredPostTonemapProgram.unload();
        gNoPostTonemapProgram.unload();
        gDeferredPostTonemapGammaCorrectProgram.unload();
        gNoPostTonemapGammaCorrectProgram.unload();
        gDeferredPostTonemapLegacyGammaCorrectProgram.unload();
        gNoPostTonemapLegacyGammaCorrectProgram.unload();

        for (auto i = 0; i < 4; ++i)
        {
            gFXAAProgram[i].unload();
            gSMAAEdgeDetectProgram[i].unload();
            gSMAABlendWeightsProgram[i].unload();
            gSMAANeighborhoodBlendProgram[i].unload();
        }
        gCASProgram.unload();
        gCASLegacyGammaProgram.unload();
        gEnvironmentMapProgram.unload();
        gDeferredWLSkyProgram.unload();
        gDeferredWLCloudProgram.unload();
        gDeferredWLSunProgram.unload();
        gDeferredWLMoonProgram.unload();
        gDeferredStarProgram.unload();
        gDeferredStarShootingProgram.unload();
        gDeferredFullbrightShinyProgram.unload();
        gHUDFullbrightShinyProgram.unload();
        gDeferredSkinnedFullbrightShinyProgram.unload();
        gDeferredSkinnedFullbrightProgram.unload();
        gDeferredSkinnedFullbrightAlphaMaskProgram.unload();
        gDeferredSkinnedFullbrightAlphaMaskAlphaProgram.unload();

        gDeferredHighlightProgram.unload();

        gNormalMapGenProgram.unload();
        gDeferredGenBrdfLutProgram.unload();
        gDeferredBufferVisualProgram.unload();

        for (U32 i = 0; i < LLMaterial::SHADER_COUNT*2; ++i)
        {
            gDeferredMaterialProgram[i].unload();
        }

        gHUDPBROpaqueProgram.unload();
        gPBRGlowProgram.unload();
        gDeferredPBROpaqueProgram.unload();
        gGLTFPBRMetallicRoughnessProgram.unload();
        gDeferredSkinnedPBROpaqueProgram.unload();
        gDeferredPBRAlphaProgram.unload();
        gDeferredSkinnedPBRAlphaProgram.unload();
        for (U32 paint_type = 0; paint_type < TERRAIN_PAINT_TYPE_COUNT; ++paint_type)
        {
            gDeferredPBRTerrainProgram[paint_type].unload();
        }

        return true;
    }

    bool success = true;

    if (success)
    {
        gDeferredHighlightProgram.mName = "Deferred Highlight Shader";
        gDeferredHighlightProgram.mShaderFiles.clear();
        gDeferredHighlightProgram.mShaderFiles.push_back(make_pair("interface/highlightV.glsl", GL_VERTEX_SHADER));
        gDeferredHighlightProgram.mShaderFiles.push_back(make_pair("deferred/highlightF.glsl", GL_FRAGMENT_SHADER));
        gDeferredHighlightProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        add_common_permutations(&gDeferredHighlightProgram);
        success = gDeferredHighlightProgram.createShader();
    }

    if (success)
    {
        gDeferredDiffuseProgram.mName = "Deferred Diffuse Shader";
        gDeferredDiffuseProgram.mFeatures.hasSrgb = true;
        gDeferredDiffuseProgram.mShaderFiles.clear();
        gDeferredDiffuseProgram.mShaderFiles.push_back(make_pair("deferred/diffuseV.glsl", GL_VERTEX_SHADER));
        gDeferredDiffuseProgram.mShaderFiles.push_back(make_pair("deferred/diffuseIndexedF.glsl", GL_FRAGMENT_SHADER));
        gDeferredDiffuseProgram.mFeatures.mIndexedTextureChannels = LLHLSLShader::sIndexedTextureChannels;
        gDeferredDiffuseProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        add_common_permutations(&gDeferredDiffuseProgram);
        success = make_rigged_variant(gDeferredDiffuseProgram, gDeferredSkinnedDiffuseProgram);
        success = success && gDeferredDiffuseProgram.createShader();
    }

    if (success)
    {
        gDeferredDiffuseAlphaMaskProgram.mName = "Deferred Diffuse Alpha Mask Shader";
        gDeferredDiffuseAlphaMaskProgram.mShaderFiles.clear();
        gDeferredDiffuseAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/diffuseV.glsl", GL_VERTEX_SHADER));
        gDeferredDiffuseAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/diffuseAlphaMaskIndexedF.glsl", GL_FRAGMENT_SHADER));
        gDeferredDiffuseAlphaMaskProgram.mFeatures.mIndexedTextureChannels = LLHLSLShader::sIndexedTextureChannels;
        gDeferredDiffuseAlphaMaskProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        add_common_permutations(&gDeferredDiffuseAlphaMaskProgram);
        success = make_rigged_variant(gDeferredDiffuseAlphaMaskProgram, gDeferredSkinnedDiffuseAlphaMaskProgram);
        success = success && gDeferredDiffuseAlphaMaskProgram.createShader();
    }

    if (success)
    {
        gDeferredNonIndexedDiffuseAlphaMaskProgram.mName = "Deferred Diffuse Non-Indexed Alpha Mask Shader";
        gDeferredNonIndexedDiffuseAlphaMaskProgram.mShaderFiles.clear();
        gDeferredNonIndexedDiffuseAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/diffuseV.glsl", GL_VERTEX_SHADER));
        gDeferredNonIndexedDiffuseAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/diffuseAlphaMaskF.glsl", GL_FRAGMENT_SHADER));
        gDeferredNonIndexedDiffuseAlphaMaskProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        add_common_permutations(&gDeferredNonIndexedDiffuseAlphaMaskProgram);
        success = gDeferredNonIndexedDiffuseAlphaMaskProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredNonIndexedDiffuseAlphaMaskNoColorProgram.mName = "Deferred Diffuse Non-Indexed Alpha Mask No Color Shader";
        gDeferredNonIndexedDiffuseAlphaMaskNoColorProgram.mShaderFiles.clear();
        gDeferredNonIndexedDiffuseAlphaMaskNoColorProgram.mShaderFiles.push_back(make_pair("deferred/diffuseNoColorV.glsl", GL_VERTEX_SHADER));
        gDeferredNonIndexedDiffuseAlphaMaskNoColorProgram.mShaderFiles.push_back(make_pair("deferred/diffuseAlphaMaskNoColorF.glsl", GL_FRAGMENT_SHADER));
        gDeferredNonIndexedDiffuseAlphaMaskNoColorProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        add_common_permutations(&gDeferredNonIndexedDiffuseAlphaMaskNoColorProgram);
        success = gDeferredNonIndexedDiffuseAlphaMaskNoColorProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredBumpProgram.mName = "Deferred Bump Shader";
        gDeferredBumpProgram.mShaderFiles.clear();
        gDeferredBumpProgram.mShaderFiles.push_back(make_pair("deferred/bumpV.glsl", GL_VERTEX_SHADER));
        gDeferredBumpProgram.mShaderFiles.push_back(make_pair("deferred/bumpF.glsl", GL_FRAGMENT_SHADER));
        gDeferredBumpProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        add_common_permutations(&gDeferredBumpProgram);
        success = make_rigged_variant(gDeferredBumpProgram, gDeferredSkinnedBumpProgram);
        success = success && gDeferredBumpProgram.createShader();
        llassert(success);
    }

    gDeferredMaterialProgram[1].mFeatures.hasLighting = false;
    gDeferredMaterialProgram[5].mFeatures.hasLighting = false;
    gDeferredMaterialProgram[9].mFeatures.hasLighting = false;
    gDeferredMaterialProgram[13].mFeatures.hasLighting = false;
    gDeferredMaterialProgram[1+LLMaterial::SHADER_COUNT].mFeatures.hasLighting = false;
    gDeferredMaterialProgram[5+LLMaterial::SHADER_COUNT].mFeatures.hasLighting = false;
    gDeferredMaterialProgram[9+LLMaterial::SHADER_COUNT].mFeatures.hasLighting = false;
    gDeferredMaterialProgram[13+LLMaterial::SHADER_COUNT].mFeatures.hasLighting = false;

#ifdef DX_RENDER
    // S24 (2026-09-05, task #277): pending-count + mutex/cv used to wait for
    // this permutation array's background D3DCompile() prefetch (below) to
    // finish before the real, necessarily-sequential compile loop runs.
    // Guarded entirely by material_prefetch_mutex, per the standard
    // "mutate-and-check-predicate-under-the-same-lock" condition_variable
    // idiom - a plain int is enough, no separate atomic needed.
    int material_prefetch_pending = 0;
    std::mutex material_prefetch_mutex;
    std::condition_variable material_prefetch_cv;
    auto material_prefetch_queue = DXShader::sShaderCacheEnabled ? LL::WorkQueue::getInstance("DXPool") : nullptr;
#endif

    for (U32 i = 0; i < LLMaterial::SHADER_COUNT*2; ++i)
    {
        if (success)
        {
            bool has_skin = i & 0x10;

            if (!has_skin)
            {
                mShaderList.push_back(&gDeferredMaterialProgram[i]);
                gDeferredMaterialProgram[i].mName = llformat("Material Shader %d", i);
            }
            else
            {
                gDeferredMaterialProgram[i].mName = llformat("Skinned Material Shader %d", i);
            }

            U32 alpha_mode = i & 0x3;

            gDeferredMaterialProgram[i].mShaderFiles.clear();
            gDeferredMaterialProgram[i].mShaderFiles.push_back(make_pair("deferred/materialV.glsl", GL_VERTEX_SHADER));
            gDeferredMaterialProgram[i].mShaderFiles.push_back(make_pair("deferred/materialF.glsl", GL_FRAGMENT_SHADER));
            gDeferredMaterialProgram[i].mShaderLevel = mShaderLevel[SHADER_DEFERRED];

            gDeferredMaterialProgram[i].clearPermutations();

            bool has_normal_map   = (i & 0x8) > 0;
            bool has_specular_map = (i & 0x4) > 0;

            if (has_normal_map)
            {
                gDeferredMaterialProgram[i].addPermutation("HAS_NORMAL_MAP", "1");
            }

            if (has_specular_map)
            {
                gDeferredMaterialProgram[i].addPermutation("HAS_SPECULAR_MAP", "1");
            }

            gDeferredMaterialProgram[i].addPermutation("DIFFUSE_ALPHA_MODE", llformat("%d", alpha_mode));

            if (alpha_mode != 0)
            {
                gDeferredMaterialProgram[i].mFeatures.hasAlphaMask = true;
                gDeferredMaterialProgram[i].addPermutation("HAS_ALPHA_MASK", "1");
            }

            if (use_sun_shadow)
            {
                gDeferredMaterialProgram[i].addPermutation("HAS_SUN_SHADOW", "1");
            }

            add_common_permutations(&gDeferredMaterialProgram[i]);

            gDeferredMaterialProgram[i].mFeatures.hasSrgb = true;
            gDeferredMaterialProgram[i].mFeatures.calculatesAtmospherics = true;
            gDeferredMaterialProgram[i].mFeatures.hasAtmospherics = true;
            gDeferredMaterialProgram[i].mFeatures.hasGamma = true;
            gDeferredMaterialProgram[i].mFeatures.hasShadows = use_sun_shadow;
            gDeferredMaterialProgram[i].mFeatures.hasReflectionProbes = true;

            if (has_skin)
            {
                gDeferredMaterialProgram[i].addPermutation("HAS_SKIN", "1");
                gDeferredMaterialProgram[i].mFeatures.hasObjectSkinning = true;
            }
            else
            {
                gDeferredMaterialProgram[i].mRiggedVariant = &gDeferredMaterialProgram[i + 0x10];
            }

#ifdef DX_RENDER
            // S24 (2026-09-05, task #277): warm the D3DCompile() bytecode
            // disk cache for this permutation on a DXPool worker thread while
            // setup for later permutations continues on the main thread -
            // see DXShader::prefetchVertexShader()/prefetchPixelShader()'s
            // own comments for why only this step (not the device-touching
            // real compile below) is safe to parallelize. Purely a warm-up:
            // the real createShader() loop further down is completely
            // unchanged and correct either way, this only makes it faster
            // once the cache is warm. buildDXSource() itself stays on the
            // main thread (touches LLShaderMgr's shared source-text caches).
            if (material_prefetch_queue && gDeferredMaterialProgram[i].buildDXSource())
            {
                std::string debug_name = gDeferredMaterialProgram[i].mName;
                std::string vs_source = gDeferredMaterialProgram[i].mDXVertexSource;
                std::string ps_source = gDeferredMaterialProgram[i].mDXPixelSource;

                if (!vs_source.empty())
                {
                    {
                        std::lock_guard<std::mutex> lk(material_prefetch_mutex);
                        ++material_prefetch_pending;
                    }
                    bool posted = material_prefetch_queue->post(
                        [vs_source, debug_name, &material_prefetch_pending, &material_prefetch_mutex, &material_prefetch_cv]()
                        {
                            DXShader::prefetchVertexShader(vs_source, debug_name);
                            {
                                std::lock_guard<std::mutex> lk(material_prefetch_mutex);
                                --material_prefetch_pending;
                            }
                            material_prefetch_cv.notify_one();
                        });
                    if (!posted)
                    {
                        std::lock_guard<std::mutex> lk(material_prefetch_mutex);
                        --material_prefetch_pending;
                    }
                }

                if (!ps_source.empty())
                {
                    {
                        std::lock_guard<std::mutex> lk(material_prefetch_mutex);
                        ++material_prefetch_pending;
                    }
                    bool posted = material_prefetch_queue->post(
                        [ps_source, debug_name, &material_prefetch_pending, &material_prefetch_mutex, &material_prefetch_cv]()
                        {
                            DXShader::prefetchPixelShader(ps_source, debug_name);
                            {
                                std::lock_guard<std::mutex> lk(material_prefetch_mutex);
                                --material_prefetch_pending;
                            }
                            material_prefetch_cv.notify_one();
                        });
                    if (!posted)
                    {
                        std::lock_guard<std::mutex> lk(material_prefetch_mutex);
                        --material_prefetch_pending;
                    }
                }
            }
#endif
        }
    }

#ifdef DX_RENDER
    if (material_prefetch_queue)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_SHADER("materialProgramPrefetchWait");
        std::unique_lock<std::mutex> lk(material_prefetch_mutex);
        material_prefetch_cv.wait(lk, [&material_prefetch_pending] { return material_prefetch_pending == 0; });
    }
#endif

    for (U32 i = 0; i < LLMaterial::SHADER_COUNT*2; ++i)
    {
        if (success)
        {
            success = gDeferredMaterialProgram[i].createShader();
            llassert(success);
        }
    }

    gDeferredMaterialProgram[1].mFeatures.hasLighting = true;
    gDeferredMaterialProgram[5].mFeatures.hasLighting = true;
    gDeferredMaterialProgram[9].mFeatures.hasLighting = true;
    gDeferredMaterialProgram[13].mFeatures.hasLighting = true;
    gDeferredMaterialProgram[1+LLMaterial::SHADER_COUNT].mFeatures.hasLighting = true;
    gDeferredMaterialProgram[5+LLMaterial::SHADER_COUNT].mFeatures.hasLighting = true;
    gDeferredMaterialProgram[9+LLMaterial::SHADER_COUNT].mFeatures.hasLighting = true;
    gDeferredMaterialProgram[13+LLMaterial::SHADER_COUNT].mFeatures.hasLighting = true;

    if (success)
    {
        gDeferredPBROpaqueProgram.mName = "Deferred PBR Opaque Shader";
        gDeferredPBROpaqueProgram.mFeatures.hasSrgb = true;

        gDeferredPBROpaqueProgram.mShaderFiles.clear();
        gDeferredPBROpaqueProgram.mShaderFiles.push_back(make_pair("deferred/pbropaqueV.glsl", GL_VERTEX_SHADER));
        gDeferredPBROpaqueProgram.mShaderFiles.push_back(make_pair("deferred/pbropaqueF.glsl", GL_FRAGMENT_SHADER));
        gDeferredPBROpaqueProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredPBROpaqueProgram.clearPermutations();

        add_common_permutations(&gDeferredPBROpaqueProgram);

        success = make_rigged_variant(gDeferredPBROpaqueProgram, gDeferredSkinnedPBROpaqueProgram);
        if (success)
        {
            success = gDeferredPBROpaqueProgram.createShader();
        }
        llassert(success);
    }

    if (gSavedSettings.getBOOL("GLTFEnabled"))
    {
        if (success)
        {
            gGLTFPBRMetallicRoughnessProgram.mName = "GLTF PBR Metallic Roughness Shader";
            gGLTFPBRMetallicRoughnessProgram.mFeatures.hasSrgb = true;

            gGLTFPBRMetallicRoughnessProgram.mShaderFiles.clear();
            gGLTFPBRMetallicRoughnessProgram.mShaderFiles.push_back(make_pair("gltf/pbrmetallicroughnessV.glsl", GL_VERTEX_SHADER));
            gGLTFPBRMetallicRoughnessProgram.mShaderFiles.push_back(make_pair("gltf/pbrmetallicroughnessF.glsl", GL_FRAGMENT_SHADER));
            gGLTFPBRMetallicRoughnessProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
            gGLTFPBRMetallicRoughnessProgram.clearPermutations();

            add_common_permutations(&gGLTFPBRMetallicRoughnessProgram);

            success = make_gltf_variants(gGLTFPBRMetallicRoughnessProgram, use_sun_shadow);

            //llassert(success);
            if (!success)
            {
                LL_WARNS() << "Failed to create GLTF PBR Metallic Roughness Shader, disabling!" << LL_ENDL;
                gSavedSettings.setBOOL("RenderCanUseGLTFPBROpaqueShaders", false);
                // continue as if this shader never happened
                success = true;
            }
        }
    }

    if (success)
    {
        gPBRGlowProgram.mName = " PBR Glow Shader";
        gPBRGlowProgram.mFeatures.hasSrgb = true;
        gPBRGlowProgram.mShaderFiles.clear();
        gPBRGlowProgram.mShaderFiles.push_back(make_pair("deferred/pbrglowV.glsl", GL_VERTEX_SHADER));
        gPBRGlowProgram.mShaderFiles.push_back(make_pair("deferred/pbrglowF.glsl", GL_FRAGMENT_SHADER));
        gPBRGlowProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gPBRGlowProgram);

        success = make_rigged_variant(gPBRGlowProgram, gPBRGlowSkinnedProgram);
        if (success)
        {
            success = gPBRGlowProgram.createShader();
        }
        llassert(success);
    }

    if (success)
    {
        gHUDPBROpaqueProgram.mName = "HUD PBR Opaque Shader";
        gHUDPBROpaqueProgram.mFeatures.hasSrgb = true;
        gHUDPBROpaqueProgram.mShaderFiles.clear();
        gHUDPBROpaqueProgram.mShaderFiles.push_back(make_pair("deferred/pbropaqueV.glsl", GL_VERTEX_SHADER));
        gHUDPBROpaqueProgram.mShaderFiles.push_back(make_pair("deferred/pbropaqueF.glsl", GL_FRAGMENT_SHADER));
        gHUDPBROpaqueProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gHUDPBROpaqueProgram.clearPermutations();
        gHUDPBROpaqueProgram.addPermutation("IS_HUD", "1");

        add_common_permutations(&gHUDPBROpaqueProgram);

        success = gHUDPBROpaqueProgram.createShader();

        llassert(success);
    }

    if (success)
    {
        LLHLSLShader* shader = &gDeferredPBRAlphaProgram;
        shader->mName = "Deferred PBR Alpha Shader";

        shader->mFeatures.calculatesLighting = false;
        shader->mFeatures.hasLighting = false;
        shader->mFeatures.isAlphaLighting = true;
        shader->mFeatures.hasSrgb = true;
        shader->mFeatures.calculatesAtmospherics = true;
        shader->mFeatures.hasAtmospherics = true;
        shader->mFeatures.hasGamma = true;
        shader->mFeatures.hasShadows = use_sun_shadow;
        shader->mFeatures.isDeferred = true; // include deferredUtils
        shader->mFeatures.hasReflectionProbes = mShaderLevel[SHADER_DEFERRED];

        shader->mShaderFiles.clear();
        shader->mShaderFiles.push_back(make_pair("deferred/pbralphaV.glsl", GL_VERTEX_SHADER));
        shader->mShaderFiles.push_back(make_pair("deferred/pbralphaF.glsl", GL_FRAGMENT_SHADER));

        shader->clearPermutations();

        U32 alpha_mode = LLMaterial::DIFFUSE_ALPHA_MODE_BLEND;
        shader->addPermutation("DIFFUSE_ALPHA_MODE", llformat("%d", alpha_mode));
        shader->addPermutation("HAS_NORMAL_MAP", "1");
        shader->addPermutation("HAS_SPECULAR_MAP", "1"); // PBR: Packed: Occlusion, Metal, Roughness
        shader->addPermutation("HAS_EMISSIVE_MAP", "1");
        shader->addPermutation("USE_VERTEX_COLOR", "1");

        add_common_permutations(shader);

        if (use_sun_shadow)
        {
            shader->addPermutation("HAS_SUN_SHADOW", "1");
        }

        shader->mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = make_rigged_variant(*shader, gDeferredSkinnedPBRAlphaProgram);
        if (success)
        {
            success = shader->createShader();
        }
        llassert(success);

        // Alpha Shader Hack
        // See: LLRender::syncMatrices()
        shader->mFeatures.calculatesLighting = true;
        shader->mFeatures.hasLighting = true;

        shader->mRiggedVariant->mFeatures.calculatesLighting = true;
        shader->mRiggedVariant->mFeatures.hasLighting = true;
    }

    if (success)
    {
        LLHLSLShader* shader = &gHUDPBRAlphaProgram;
        shader->mName = "HUD PBR Alpha Shader";

        shader->mFeatures.hasSrgb = true;

        shader->mShaderFiles.clear();
        shader->mShaderFiles.push_back(make_pair("deferred/pbralphaV.glsl", GL_VERTEX_SHADER));
        shader->mShaderFiles.push_back(make_pair("deferred/pbralphaF.glsl", GL_FRAGMENT_SHADER));

        shader->clearPermutations();

        shader->addPermutation("IS_HUD", "1");

        add_common_permutations(shader);

        shader->mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = shader->createShader();
        llassert(success);
    }

    if (success)
    {
        S32 detail = gSavedSettings.getS32("RenderTerrainPBRDetail");
        detail = llclamp(detail, TERRAIN_PBR_DETAIL_MIN, TERRAIN_PBR_DETAIL_MAX);
        const S32 mapping = clamp_terrain_mapping(gSavedSettings.getS32("RenderTerrainPBRPlanarSampleCount"));
        for (U32 paint_type = 0; paint_type < TERRAIN_PAINT_TYPE_COUNT; ++paint_type)
        {
            LLHLSLShader* shader = &gDeferredPBRTerrainProgram[paint_type];
            shader->mName = llformat("Deferred PBR Terrain Shader %d %s %s",
                    detail,
                    (paint_type == TERRAIN_PAINT_TYPE_PBR_PAINTMAP ? "paintmap" : "heightmap-with-noise"),
                    (mapping == 1 ? "flat" : "triplanar"));
            shader->mFeatures.hasSrgb = true;
            shader->mFeatures.isAlphaLighting = true;
            shader->mFeatures.calculatesAtmospherics = true;
            shader->mFeatures.hasAtmospherics = true;
            shader->mFeatures.hasGamma = true;
            shader->mFeatures.hasTransport = true;
            shader->mFeatures.isPBRTerrain = true;

            shader->mShaderFiles.clear();
            shader->mShaderFiles.push_back(make_pair("deferred/pbrterrainV.glsl", GL_VERTEX_SHADER));
            shader->mShaderFiles.push_back(make_pair("deferred/pbrterrainF.glsl", GL_FRAGMENT_SHADER));
            shader->mShaderLevel = mShaderLevel[SHADER_DEFERRED];
            shader->addPermutation("TERRAIN_PBR_DETAIL", llformat("%d", detail));
            shader->addPermutation("TERRAIN_PAINT_TYPE", llformat("%d", paint_type));
            shader->addPermutation("TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT", llformat("%d", mapping));

            add_common_permutations(shader);

            success = success && shader->createShader();
            llassert(success);
        }
    }

    if (success)
    {
        gDeferredTreeProgram.mName = "Deferred Tree Shader";
        gDeferredTreeProgram.mShaderFiles.clear();
        gDeferredTreeProgram.mShaderFiles.push_back(make_pair("deferred/treeV.glsl", GL_VERTEX_SHADER));
        gDeferredTreeProgram.mShaderFiles.push_back(make_pair("deferred/treeF.glsl", GL_FRAGMENT_SHADER));
        gDeferredTreeProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredTreeProgram);

        success = gDeferredTreeProgram.createShader();
    }

    if (success)
    {
        gDeferredTreeShadowProgram.mName = "Deferred Tree Shadow Shader";
        gDeferredTreeShadowProgram.mShaderFiles.clear();
        gDeferredTreeShadowProgram.mShaderFiles.push_back(make_pair("deferred/treeShadowV.glsl", GL_VERTEX_SHADER));
        gDeferredTreeShadowProgram.mShaderFiles.push_back(make_pair("deferred/treeShadowF.glsl", GL_FRAGMENT_SHADER));
        gDeferredTreeShadowProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredTreeShadowProgram.mRiggedVariant = &gDeferredSkinnedTreeShadowProgram;
        success = gDeferredTreeShadowProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredSkinnedTreeShadowProgram.mName = "Deferred Skinned Tree Shadow Shader";
        gDeferredSkinnedTreeShadowProgram.mShaderFiles.clear();
        gDeferredSkinnedTreeShadowProgram.mFeatures.hasObjectSkinning = true;
        gDeferredSkinnedTreeShadowProgram.mShaderFiles.push_back(make_pair("deferred/treeShadowSkinnedV.glsl", GL_VERTEX_SHADER));
        gDeferredSkinnedTreeShadowProgram.mShaderFiles.push_back(make_pair("deferred/treeShadowF.glsl", GL_FRAGMENT_SHADER));
        gDeferredSkinnedTreeShadowProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredSkinnedTreeShadowProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredImpostorProgram.mName = "Deferred Impostor Shader";
        gDeferredImpostorProgram.mFeatures.hasSrgb = true;
        gDeferredImpostorProgram.mShaderFiles.clear();
        gDeferredImpostorProgram.mShaderFiles.push_back(make_pair("deferred/impostorV.glsl", GL_VERTEX_SHADER));
        gDeferredImpostorProgram.mShaderFiles.push_back(make_pair("deferred/impostorF.glsl", GL_FRAGMENT_SHADER));
        gDeferredImpostorProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredImpostorProgram);

        success = gDeferredImpostorProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        // S24 (2026-09-10): jelly-doll ghost - real alpha-blended,
        // post-deferred draw of the same cached impostor quad, see
        // LLDrawPoolAvatar::renderJellyDollGhosts().
        gDeferredJellyGhostProgram.mName = "Deferred Jelly Ghost Shader";
        gDeferredJellyGhostProgram.mFeatures.hasSrgb = true;
        gDeferredJellyGhostProgram.mShaderFiles.clear();
        gDeferredJellyGhostProgram.mShaderFiles.push_back(make_pair("deferred/jellyGhostV.glsl", GL_VERTEX_SHADER));
        gDeferredJellyGhostProgram.mShaderFiles.push_back(make_pair("deferred/jellyGhostF.glsl", GL_FRAGMENT_SHADER));
        gDeferredJellyGhostProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredJellyGhostProgram);

        success = gDeferredJellyGhostProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredLightProgram.mName = "Deferred Light Shader";
        gDeferredLightProgram.mFeatures.isDeferred = true;
        gDeferredLightProgram.mFeatures.hasFullGBuffer = true;
        gDeferredLightProgram.mFeatures.hasShadows = true;
        gDeferredLightProgram.mFeatures.hasSrgb = true;

        gDeferredLightProgram.mShaderFiles.clear();
        gDeferredLightProgram.mShaderFiles.push_back(make_pair("deferred/pointLightV.glsl", GL_VERTEX_SHADER));
        gDeferredLightProgram.mShaderFiles.push_back(make_pair("deferred/pointLightF.glsl", GL_FRAGMENT_SHADER));
        gDeferredLightProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        gDeferredLightProgram.clearPermutations();

        add_common_permutations(&gDeferredLightProgram);

        success = gDeferredLightProgram.createShader();
        llassert(success);
    }

    for (U32 i = 0; i < LL_DEFERRED_MULTI_LIGHT_COUNT; i++)
    {
        if (success)
        {
            gDeferredMultiLightProgram[i].mName = llformat("Deferred MultiLight Shader %d", i);
            gDeferredMultiLightProgram[i].mFeatures.isDeferred = true;
            gDeferredMultiLightProgram[i].mFeatures.hasFullGBuffer = true;
            gDeferredMultiLightProgram[i].mFeatures.hasShadows = true;
            gDeferredMultiLightProgram[i].mFeatures.hasSrgb = true;

            gDeferredMultiLightProgram[i].clearPermutations();
            gDeferredMultiLightProgram[i].mShaderFiles.clear();
            gDeferredMultiLightProgram[i].mShaderFiles.push_back(make_pair("deferred/multiPointLightV.glsl", GL_VERTEX_SHADER));
            gDeferredMultiLightProgram[i].mShaderFiles.push_back(make_pair("deferred/multiPointLightF.glsl", GL_FRAGMENT_SHADER));
            gDeferredMultiLightProgram[i].mShaderLevel = mShaderLevel[SHADER_DEFERRED];
            gDeferredMultiLightProgram[i].addPermutation("LIGHT_COUNT", llformat("%d", i+1));

            add_common_permutations(&gDeferredMultiLightProgram[i]);

            success = gDeferredMultiLightProgram[i].createShader();
            llassert(success);
        }
    }

    if (success)
    {
        gDeferredSpotLightProgram.mName = "Deferred SpotLight Shader";
        gDeferredSpotLightProgram.mShaderFiles.clear();
        gDeferredSpotLightProgram.mFeatures.hasSrgb = true;
        gDeferredSpotLightProgram.mFeatures.isDeferred = true;
        gDeferredSpotLightProgram.mFeatures.hasFullGBuffer = true;
        gDeferredSpotLightProgram.mFeatures.hasShadows = true;

        gDeferredSpotLightProgram.clearPermutations();
        gDeferredSpotLightProgram.mShaderFiles.push_back(make_pair("deferred/pointLightV.glsl", GL_VERTEX_SHADER));
        gDeferredSpotLightProgram.mShaderFiles.push_back(make_pair("deferred/spotLightF.glsl", GL_FRAGMENT_SHADER));
        gDeferredSpotLightProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredSpotLightProgram);

        success = gDeferredSpotLightProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredMultiSpotLightProgram.mName = "Deferred MultiSpotLight Shader";
        gDeferredMultiSpotLightProgram.mFeatures.hasSrgb = true;
        gDeferredMultiSpotLightProgram.mFeatures.isDeferred = true;
        gDeferredMultiSpotLightProgram.mFeatures.hasFullGBuffer = true;
        gDeferredMultiSpotLightProgram.mFeatures.hasShadows = true;

        gDeferredMultiSpotLightProgram.clearPermutations();
        gDeferredMultiSpotLightProgram.addPermutation("MULTI_SPOTLIGHT", "1");
        gDeferredMultiSpotLightProgram.mShaderFiles.clear();
        gDeferredMultiSpotLightProgram.mShaderFiles.push_back(make_pair("deferred/multiPointLightV.glsl", GL_VERTEX_SHADER));
        gDeferredMultiSpotLightProgram.mShaderFiles.push_back(make_pair("deferred/spotLightF.glsl", GL_FRAGMENT_SHADER));
        gDeferredMultiSpotLightProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredMultiSpotLightProgram);

        success = gDeferredMultiSpotLightProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        std::string fragment;
        bool use_ao = gSavedSettings.getBOOL("RenderDeferredSSAO");
        if (use_ao)
        {
            fragment = "deferred/sunLightSSAOF.glsl";
        }
        else
        {
            fragment = "deferred/sunLightF.glsl";
        }

        gDeferredSunProgram.mName = "Deferred Sun Shader";
        gDeferredSunProgram.mFeatures.isDeferred    = true;
        gDeferredSunProgram.mFeatures.hasShadows    = true;
        gDeferredSunProgram.mFeatures.hasAmbientOcclusion = use_ao;

        gDeferredSunProgram.mShaderFiles.clear();
        gDeferredSunProgram.mShaderFiles.push_back(make_pair("deferred/sunLightV.glsl", GL_VERTEX_SHADER));
        gDeferredSunProgram.mShaderFiles.push_back(make_pair(fragment, GL_FRAGMENT_SHADER));
        gDeferredSunProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredSunProgram);

        success = gDeferredSunProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredSunProbeProgram.mName = "Deferred Sun Probe Shader";
        gDeferredSunProbeProgram.mFeatures.isDeferred = true;
        gDeferredSunProbeProgram.mFeatures.hasShadows = true;

        gDeferredSunProbeProgram.mShaderFiles.clear();
        gDeferredSunProbeProgram.mShaderFiles.push_back(make_pair("deferred/sunLightV.glsl", GL_VERTEX_SHADER));
        gDeferredSunProbeProgram.mShaderFiles.push_back(make_pair("deferred/sunLightF.glsl", GL_FRAGMENT_SHADER));
        gDeferredSunProbeProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredSunProbeProgram);

        success = gDeferredSunProbeProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredBlurLightProgram.mName = "Deferred Blur Light Shader";
        gDeferredBlurLightProgram.mFeatures.isDeferred = true;

        gDeferredBlurLightProgram.mShaderFiles.clear();
        gDeferredBlurLightProgram.mShaderFiles.push_back(make_pair("deferred/blurLightV.glsl", GL_VERTEX_SHADER));
        gDeferredBlurLightProgram.mShaderFiles.push_back(make_pair("deferred/blurLightF.glsl", GL_FRAGMENT_SHADER));
        gDeferredBlurLightProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredBlurLightProgram);

        success = gDeferredBlurLightProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        // S24 (2026-08-23, task #190 temporal-SSAO follow-up): reprojects
        // and blends last frame's AO history with this frame's spatially-
        // blurred AO to remove screen-locked-noise flicker during camera
        // movement. Reuses blurLightV.hlsl unchanged - see dxpipeline.cpp's
        // renderDeferredLighting() for the insertion point/orchestration.
        gDeferredTemporalResolveSSAOProgram.mName = "Deferred Temporal Resolve SSAO Shader";
        gDeferredTemporalResolveSSAOProgram.mFeatures.isDeferred = true;

        gDeferredTemporalResolveSSAOProgram.mShaderFiles.clear();
        gDeferredTemporalResolveSSAOProgram.mShaderFiles.push_back(make_pair("deferred/blurLightV.glsl", GL_VERTEX_SHADER));
        gDeferredTemporalResolveSSAOProgram.mShaderFiles.push_back(make_pair("deferred/temporalResolveSSAOF.glsl", GL_FRAGMENT_SHADER));
        gDeferredTemporalResolveSSAOProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredTemporalResolveSSAOProgram);

        success = gDeferredTemporalResolveSSAOProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        for (int i = 0; i < 3 && success; ++i)
        {
            LLHLSLShader* shader = nullptr;
            bool rigged = (i == 1);
            bool hud = (i == 2);

            if (hud)
            {
                shader = &gHUDAlphaProgram;
                shader->mName = "HUD Alpha Shader";
            }
            else if (!rigged)
            {
                shader = &gDeferredAlphaProgram;
                shader->mName = "Deferred Alpha Shader";
                shader->mRiggedVariant = &gDeferredSkinnedAlphaProgram;
            }
            else
            {
                shader = &gDeferredSkinnedAlphaProgram;
                shader->mName = "Skinned Deferred Alpha Shader";
                shader->mFeatures.hasObjectSkinning = true;
            }

            shader->mFeatures.calculatesLighting = false;
            shader->mFeatures.hasLighting = false;
            shader->mFeatures.isAlphaLighting = true;
            shader->mFeatures.hasSrgb = true;
            shader->mFeatures.calculatesAtmospherics = true;
            shader->mFeatures.hasAtmospherics = true;
            shader->mFeatures.hasGamma = true;
            shader->mFeatures.hasShadows = use_sun_shadow;
            shader->mFeatures.hasReflectionProbes = true;
            shader->mFeatures.mIndexedTextureChannels = LLHLSLShader::sIndexedTextureChannels;

            shader->mShaderFiles.clear();
            shader->mShaderFiles.push_back(make_pair("deferred/alphaV.glsl", GL_VERTEX_SHADER));
            shader->mShaderFiles.push_back(make_pair("deferred/alphaF.glsl", GL_FRAGMENT_SHADER));

            shader->clearPermutations();
            shader->addPermutation("USE_VERTEX_COLOR", "1");
            shader->addPermutation("HAS_ALPHA_MASK", "1");
            shader->addPermutation("USE_INDEXED_TEX", "1");
            if (use_sun_shadow)
            {
                shader->addPermutation("HAS_SUN_SHADOW", "1");
            }

            add_common_permutations(shader);

            if (rigged)
            {
                shader->addPermutation("HAS_SKIN", "1");
            }

            if (hud)
            {
                shader->addPermutation("IS_HUD", "1");
            }

            shader->mShaderLevel = mShaderLevel[SHADER_DEFERRED];

            success = shader->createShader();
            llassert(success);

            // Hack
            shader->mFeatures.calculatesLighting = true;
            shader->mFeatures.hasLighting = true;
        }
    }

    if (success)
    {
        LLHLSLShader* shaders[] = {
            &gDeferredAlphaImpostorProgram,
            &gDeferredSkinnedAlphaImpostorProgram
        };

        for (int i = 0; i < 2 && success; ++i)
        {
            bool rigged = i == 1;
            LLHLSLShader* shader = shaders[i];

            shader->mName = rigged ? "Skinned Deferred Alpha Impostor Shader" : "Deferred Alpha Impostor Shader";

            // Begin Hack
            shader->mFeatures.calculatesLighting = false;
            shader->mFeatures.hasLighting = false;

            shader->mFeatures.hasSrgb = true;
            shader->mFeatures.isAlphaLighting = true;
            shader->mFeatures.hasShadows = use_sun_shadow;
            shader->mFeatures.hasReflectionProbes = true;
            shader->mFeatures.mIndexedTextureChannels = LLHLSLShader::sIndexedTextureChannels;

            shader->mShaderFiles.clear();
            shader->mShaderFiles.push_back(make_pair("deferred/alphaV.glsl", GL_VERTEX_SHADER));
            shader->mShaderFiles.push_back(make_pair("deferred/alphaF.glsl", GL_FRAGMENT_SHADER));

            shader->clearPermutations();
            shader->addPermutation("USE_INDEXED_TEX", "1");
            shader->addPermutation("FOR_IMPOSTOR", "1");
            shader->addPermutation("HAS_ALPHA_MASK", "1");
            shader->addPermutation("USE_VERTEX_COLOR", "1");
            if (rigged)
            {
                shader->mFeatures.hasObjectSkinning = true;
                shader->addPermutation("HAS_SKIN", "1");
            }

            if (use_sun_shadow)
            {
                shader->addPermutation("HAS_SUN_SHADOW", "1");
            }

            add_common_permutations(shader);

            shader->mRiggedVariant = &gDeferredSkinnedAlphaImpostorProgram;
            shader->mShaderLevel = mShaderLevel[SHADER_DEFERRED];
            if (!rigged)
            {
                shader->mRiggedVariant = shaders[1];
            }
            success = shader->createShader();
            llassert(success);

            // End Hack
            shader->mFeatures.calculatesLighting = true;
            shader->mFeatures.hasLighting = true;
        }
    }

    if (success)
    {
        gDeferredAvatarEyesProgram.mName = "Deferred Avatar Eyes Shader";
        gDeferredAvatarEyesProgram.mFeatures.calculatesAtmospherics = true;
        gDeferredAvatarEyesProgram.mFeatures.hasGamma = true;
        gDeferredAvatarEyesProgram.mFeatures.hasAtmospherics = true;
        gDeferredAvatarEyesProgram.mFeatures.hasSrgb = true;
        gDeferredAvatarEyesProgram.mFeatures.hasShadows = true;

        gDeferredAvatarEyesProgram.mShaderFiles.clear();
        gDeferredAvatarEyesProgram.mShaderFiles.push_back(make_pair("deferred/avatarEyesV.glsl", GL_VERTEX_SHADER));
        gDeferredAvatarEyesProgram.mShaderFiles.push_back(make_pair("deferred/diffuseF.glsl", GL_FRAGMENT_SHADER));
        gDeferredAvatarEyesProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredAvatarEyesProgram);

        success = gDeferredAvatarEyesProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredFullbrightProgram.mName = "Deferred Fullbright Shader";
        gDeferredFullbrightProgram.mFeatures.calculatesAtmospherics = true;
        gDeferredFullbrightProgram.mFeatures.hasGamma = true;
        gDeferredFullbrightProgram.mFeatures.hasAtmospherics = true;
        gDeferredFullbrightProgram.mFeatures.hasSrgb = true;
        gDeferredFullbrightProgram.mFeatures.mIndexedTextureChannels = LLHLSLShader::sIndexedTextureChannels;
        gDeferredFullbrightProgram.mShaderFiles.clear();
        gDeferredFullbrightProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightV.glsl", GL_VERTEX_SHADER));
        gDeferredFullbrightProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightF.glsl", GL_FRAGMENT_SHADER));
        gDeferredFullbrightProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredFullbrightProgram);

        success = make_rigged_variant(gDeferredFullbrightProgram, gDeferredSkinnedFullbrightProgram);
        success = gDeferredFullbrightProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gHUDFullbrightProgram.mName = "HUD Fullbright Shader";
        gHUDFullbrightProgram.mFeatures.calculatesAtmospherics = true;
        gHUDFullbrightProgram.mFeatures.hasGamma = true;
        gHUDFullbrightProgram.mFeatures.hasAtmospherics = true;
        gHUDFullbrightProgram.mFeatures.hasSrgb = true;
        gHUDFullbrightProgram.mFeatures.mIndexedTextureChannels = LLHLSLShader::sIndexedTextureChannels;
        gHUDFullbrightProgram.mShaderFiles.clear();
        gHUDFullbrightProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightV.glsl", GL_VERTEX_SHADER));
        gHUDFullbrightProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightF.glsl", GL_FRAGMENT_SHADER));
        gHUDFullbrightProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gHUDFullbrightProgram.clearPermutations();
        gHUDFullbrightProgram.addPermutation("IS_HUD", "1");

        add_common_permutations(&gHUDFullbrightProgram);

        success = gHUDFullbrightProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredFullbrightAlphaMaskProgram.mName = "Deferred Fullbright Alpha Masking Shader";
        gDeferredFullbrightAlphaMaskProgram.mFeatures.calculatesAtmospherics = true;
        gDeferredFullbrightAlphaMaskProgram.mFeatures.hasGamma = true;
        gDeferredFullbrightAlphaMaskProgram.mFeatures.hasAtmospherics = true;
        gDeferredFullbrightAlphaMaskProgram.mFeatures.hasSrgb = true;
        gDeferredFullbrightAlphaMaskProgram.mFeatures.mIndexedTextureChannels = LLHLSLShader::sIndexedTextureChannels;
        gDeferredFullbrightAlphaMaskProgram.mShaderFiles.clear();
        gDeferredFullbrightAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightV.glsl", GL_VERTEX_SHADER));
        gDeferredFullbrightAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightF.glsl", GL_FRAGMENT_SHADER));
        gDeferredFullbrightAlphaMaskProgram.clearPermutations();
        gDeferredFullbrightAlphaMaskProgram.addPermutation("HAS_ALPHA_MASK","1");
        gDeferredFullbrightAlphaMaskProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredFullbrightAlphaMaskProgram);

        success = make_rigged_variant(gDeferredFullbrightAlphaMaskProgram, gDeferredSkinnedFullbrightAlphaMaskProgram);
        success = success && gDeferredFullbrightAlphaMaskProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gHUDFullbrightAlphaMaskProgram.mName = "HUD Fullbright Alpha Masking Shader";
        gHUDFullbrightAlphaMaskProgram.mFeatures.calculatesAtmospherics = true;
        gHUDFullbrightAlphaMaskProgram.mFeatures.hasGamma = true;
        gHUDFullbrightAlphaMaskProgram.mFeatures.hasAtmospherics = true;
        gHUDFullbrightAlphaMaskProgram.mFeatures.hasSrgb = true;
        gHUDFullbrightAlphaMaskProgram.mFeatures.mIndexedTextureChannels = LLHLSLShader::sIndexedTextureChannels;
        gHUDFullbrightAlphaMaskProgram.mShaderFiles.clear();
        gHUDFullbrightAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightV.glsl", GL_VERTEX_SHADER));
        gHUDFullbrightAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightF.glsl", GL_FRAGMENT_SHADER));
        gHUDFullbrightAlphaMaskProgram.clearPermutations();
        gHUDFullbrightAlphaMaskProgram.addPermutation("HAS_ALPHA_MASK", "1");
        gHUDFullbrightAlphaMaskProgram.addPermutation("IS_HUD", "1");

        add_common_permutations(&gHUDFullbrightAlphaMaskProgram);

        gHUDFullbrightAlphaMaskProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gHUDFullbrightAlphaMaskProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredFullbrightAlphaMaskAlphaProgram.mName = "Deferred Fullbright Alpha Masking Alpha Shader";
        gDeferredFullbrightAlphaMaskAlphaProgram.mFeatures.calculatesAtmospherics = true;
        gDeferredFullbrightAlphaMaskAlphaProgram.mFeatures.hasGamma = true;
        gDeferredFullbrightAlphaMaskAlphaProgram.mFeatures.hasAtmospherics = true;
        gDeferredFullbrightAlphaMaskAlphaProgram.mFeatures.hasSrgb = true;
        gDeferredFullbrightAlphaMaskAlphaProgram.mFeatures.isDeferred = true;
        gDeferredFullbrightAlphaMaskAlphaProgram.mFeatures.mIndexedTextureChannels = LLHLSLShader::sIndexedTextureChannels;
        gDeferredFullbrightAlphaMaskAlphaProgram.mShaderFiles.clear();
        gDeferredFullbrightAlphaMaskAlphaProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightV.glsl", GL_VERTEX_SHADER));
        gDeferredFullbrightAlphaMaskAlphaProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightF.glsl", GL_FRAGMENT_SHADER));
        gDeferredFullbrightAlphaMaskAlphaProgram.clearPermutations();
        gDeferredFullbrightAlphaMaskAlphaProgram.addPermutation("HAS_ALPHA_MASK", "1");
        gDeferredFullbrightAlphaMaskAlphaProgram.addPermutation("IS_ALPHA", "1");

        add_common_permutations(&gDeferredFullbrightAlphaMaskAlphaProgram);

        gDeferredFullbrightAlphaMaskAlphaProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = make_rigged_variant(gDeferredFullbrightAlphaMaskAlphaProgram, gDeferredSkinnedFullbrightAlphaMaskAlphaProgram);
        success = success && gDeferredFullbrightAlphaMaskAlphaProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gHUDFullbrightAlphaMaskAlphaProgram.mName = "HUD Fullbright Alpha Masking Alpha Shader";
        gHUDFullbrightAlphaMaskAlphaProgram.mFeatures.calculatesAtmospherics = true;
        gHUDFullbrightAlphaMaskAlphaProgram.mFeatures.hasGamma = true;
        gHUDFullbrightAlphaMaskAlphaProgram.mFeatures.hasAtmospherics = true;
        gHUDFullbrightAlphaMaskAlphaProgram.mFeatures.hasSrgb = true;
        gHUDFullbrightAlphaMaskAlphaProgram.mFeatures.isDeferred = true;
        gHUDFullbrightAlphaMaskAlphaProgram.mFeatures.mIndexedTextureChannels = LLHLSLShader::sIndexedTextureChannels;
        gHUDFullbrightAlphaMaskAlphaProgram.mShaderFiles.clear();
        gHUDFullbrightAlphaMaskAlphaProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightV.glsl", GL_VERTEX_SHADER));
        gHUDFullbrightAlphaMaskAlphaProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightF.glsl", GL_FRAGMENT_SHADER));
        gHUDFullbrightAlphaMaskAlphaProgram.clearPermutations();
        gHUDFullbrightAlphaMaskAlphaProgram.addPermutation("HAS_ALPHA_MASK", "1");
        gHUDFullbrightAlphaMaskAlphaProgram.addPermutation("IS_ALPHA", "1");
        gHUDFullbrightAlphaMaskAlphaProgram.addPermutation("IS_HUD", "1");

        add_common_permutations(&gHUDFullbrightAlphaMaskAlphaProgram);

        gHUDFullbrightAlphaMaskAlphaProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = success && gHUDFullbrightAlphaMaskAlphaProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredFullbrightShinyProgram.mName = "Deferred FullbrightShiny Shader";
        gDeferredFullbrightShinyProgram.mFeatures.calculatesAtmospherics = true;
        gDeferredFullbrightShinyProgram.mFeatures.hasAtmospherics = true;
        gDeferredFullbrightShinyProgram.mFeatures.hasGamma = true;
        gDeferredFullbrightShinyProgram.mFeatures.hasSrgb = true;
        gDeferredFullbrightShinyProgram.mFeatures.mIndexedTextureChannels = LLHLSLShader::sIndexedTextureChannels;
        gDeferredFullbrightShinyProgram.mShaderFiles.clear();
        gDeferredFullbrightShinyProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightShinyV.glsl", GL_VERTEX_SHADER));
        gDeferredFullbrightShinyProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightShinyF.glsl", GL_FRAGMENT_SHADER));
        gDeferredFullbrightShinyProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredFullbrightShinyProgram.mFeatures.hasReflectionProbes = true;

        add_common_permutations(&gDeferredFullbrightShinyProgram);

        success = make_rigged_variant(gDeferredFullbrightShinyProgram, gDeferredSkinnedFullbrightShinyProgram);
        success = success && gDeferredFullbrightShinyProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gHUDFullbrightShinyProgram.mName = "HUD FullbrightShiny Shader";
        gHUDFullbrightShinyProgram.mFeatures.calculatesAtmospherics = true;
        gHUDFullbrightShinyProgram.mFeatures.hasAtmospherics = true;
        gHUDFullbrightShinyProgram.mFeatures.hasGamma = true;
        gHUDFullbrightShinyProgram.mFeatures.hasSrgb = true;
        gHUDFullbrightShinyProgram.mFeatures.mIndexedTextureChannels = LLHLSLShader::sIndexedTextureChannels;
        gHUDFullbrightShinyProgram.mShaderFiles.clear();
        gHUDFullbrightShinyProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightShinyV.glsl", GL_VERTEX_SHADER));
        gHUDFullbrightShinyProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightShinyF.glsl", GL_FRAGMENT_SHADER));
        gHUDFullbrightShinyProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gHUDFullbrightShinyProgram.mFeatures.hasReflectionProbes = true;
        gHUDFullbrightShinyProgram.clearPermutations();
        gHUDFullbrightShinyProgram.addPermutation("IS_HUD", "1");

        add_common_permutations(&gHUDFullbrightShinyProgram);

        success = gHUDFullbrightShinyProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredEmissiveProgram.mName = "Deferred Emissive Shader";
        gDeferredEmissiveProgram.mFeatures.calculatesAtmospherics = true;
        gDeferredEmissiveProgram.mFeatures.hasGamma = true;
        gDeferredEmissiveProgram.mFeatures.hasAtmospherics = true;
        gDeferredEmissiveProgram.mFeatures.mIndexedTextureChannels = LLHLSLShader::sIndexedTextureChannels;
        gDeferredEmissiveProgram.mShaderFiles.clear();
        gDeferredEmissiveProgram.mShaderFiles.push_back(make_pair("deferred/emissiveV.glsl", GL_VERTEX_SHADER));
        gDeferredEmissiveProgram.mShaderFiles.push_back(make_pair("deferred/emissiveF.glsl", GL_FRAGMENT_SHADER));
        gDeferredEmissiveProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredEmissiveProgram);

        success = make_rigged_variant(gDeferredEmissiveProgram, gDeferredSkinnedEmissiveProgram);
        success = success && gDeferredEmissiveProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredSoftenProgram.mName = "Deferred Soften Shader";
        gDeferredSoftenProgram.mShaderFiles.clear();
        gDeferredSoftenProgram.mFeatures.hasSrgb = true;
        gDeferredSoftenProgram.mFeatures.calculatesAtmospherics = true;
        gDeferredSoftenProgram.mFeatures.hasAtmospherics = true;
        gDeferredSoftenProgram.mFeatures.hasGamma = true;
        gDeferredSoftenProgram.mFeatures.isDeferred = true;
        gDeferredSoftenProgram.mFeatures.hasFullGBuffer = true;
        gDeferredSoftenProgram.mFeatures.hasShadows = use_sun_shadow;
        gDeferredSoftenProgram.mFeatures.hasReflectionProbes = mShaderLevel[SHADER_DEFERRED] > 2;

        gDeferredSoftenProgram.clearPermutations();
        add_common_permutations(&gDeferredSoftenProgram);
        gDeferredSoftenProgram.mShaderFiles.push_back(make_pair("deferred/softenLightV.glsl", GL_VERTEX_SHADER));
        gDeferredSoftenProgram.mShaderFiles.push_back(make_pair("deferred/softenLightF.glsl", GL_FRAGMENT_SHADER));

        gDeferredSoftenProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        if (use_sun_shadow)
        {
            gDeferredSoftenProgram.addPermutation("HAS_SUN_SHADOW", "1");
        }

        if (gSavedSettings.getBOOL("RenderDeferredSSAO"))
        { //if using SSAO, take screen space light map into account as if shadows are enabled
            gDeferredSoftenProgram.mShaderLevel = llmax(gDeferredSoftenProgram.mShaderLevel, 2);
            gDeferredSoftenProgram.addPermutation("HAS_SSAO", "1");
        }

        success = gDeferredSoftenProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gHazeProgram.mName = "Haze Shader";
        gHazeProgram.mShaderFiles.clear();
        gHazeProgram.mFeatures.hasSrgb                = true;
        gHazeProgram.mFeatures.calculatesAtmospherics = true;
        gHazeProgram.mFeatures.hasAtmospherics        = true;
        gHazeProgram.mFeatures.hasGamma               = true;
        gHazeProgram.mFeatures.isDeferred             = true;
        gHazeProgram.mFeatures.hasShadows             = use_sun_shadow;
        gHazeProgram.mFeatures.hasReflectionProbes    = mShaderLevel[SHADER_DEFERRED] > 2;

        gHazeProgram.clearPermutations();
        gHazeProgram.mShaderFiles.push_back(make_pair("deferred/softenLightV.glsl", GL_VERTEX_SHADER));
        gHazeProgram.mShaderFiles.push_back(make_pair("deferred/hazeF.glsl", GL_FRAGMENT_SHADER));

        add_common_permutations(&gHazeProgram);

        gHazeProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        success = gHazeProgram.createShader();
        llassert(success);
    }


    if (success)
    {
        gHazeWaterProgram.mName = "Water Haze Shader";
        gHazeWaterProgram.mShaderFiles.clear();
        gHazeWaterProgram.mShaderGroup           = LLHLSLShader::SG_WATER;
        gHazeWaterProgram.mFeatures.hasSrgb                = true;
        gHazeWaterProgram.mFeatures.calculatesAtmospherics = true;
        gHazeWaterProgram.mFeatures.hasAtmospherics        = true;
        gHazeWaterProgram.mFeatures.hasGamma               = true;
        gHazeWaterProgram.mFeatures.isDeferred             = true;
        gHazeWaterProgram.mFeatures.hasShadows             = use_sun_shadow;
        gHazeWaterProgram.mFeatures.hasReflectionProbes    = mShaderLevel[SHADER_DEFERRED] > 2;

        gHazeWaterProgram.clearPermutations();
        gHazeWaterProgram.mShaderFiles.push_back(make_pair("deferred/waterHazeV.glsl", GL_VERTEX_SHADER));
        gHazeWaterProgram.mShaderFiles.push_back(make_pair("deferred/waterHazeF.glsl", GL_FRAGMENT_SHADER));

        add_common_permutations(&gHazeWaterProgram);

        gHazeWaterProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        success = gHazeWaterProgram.createShader();
        llassert(success);
    }


    if (success)
    {
        gDeferredShadowProgram.mName = "Deferred Shadow Shader";
        gDeferredShadowProgram.mShaderFiles.clear();
        gDeferredShadowProgram.mShaderFiles.push_back(make_pair("deferred/shadowV.glsl", GL_VERTEX_SHADER));
        gDeferredShadowProgram.mShaderFiles.push_back(make_pair("deferred/shadowF.glsl", GL_FRAGMENT_SHADER));
        gDeferredShadowProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredShadowProgram.mRiggedVariant = &gDeferredSkinnedShadowProgram;
        success = gDeferredShadowProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredSkinnedShadowProgram.mName = "Deferred Skinned Shadow Shader";
        gDeferredSkinnedShadowProgram.mFeatures.isDeferred = true;
        gDeferredSkinnedShadowProgram.mFeatures.hasShadows = true;
        gDeferredSkinnedShadowProgram.mFeatures.hasObjectSkinning = true;
        gDeferredSkinnedShadowProgram.mShaderFiles.clear();
        gDeferredSkinnedShadowProgram.mShaderFiles.push_back(make_pair("deferred/shadowSkinnedV.glsl", GL_VERTEX_SHADER));
        gDeferredSkinnedShadowProgram.mShaderFiles.push_back(make_pair("deferred/shadowF.glsl", GL_FRAGMENT_SHADER));
        gDeferredSkinnedShadowProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredSkinnedShadowProgram);

        // gDeferredSkinnedShadowProgram.addPermutation("DEPTH_CLAMP", "1"); // disable depth clamp for now
        success = gDeferredSkinnedShadowProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredShadowCubeProgram.mName = "Deferred Shadow Cube Shader";
        gDeferredShadowCubeProgram.mFeatures.isDeferred = true;
        gDeferredShadowCubeProgram.mFeatures.hasShadows = true;
        gDeferredShadowCubeProgram.mShaderFiles.clear();
        gDeferredShadowCubeProgram.mShaderFiles.push_back(make_pair("deferred/shadowCubeV.glsl", GL_VERTEX_SHADER));
        gDeferredShadowCubeProgram.mShaderFiles.push_back(make_pair("deferred/shadowF.glsl", GL_FRAGMENT_SHADER));
        // gDeferredShadowCubeProgram.addPermutation("DEPTH_CLAMP", "1");
        gDeferredShadowCubeProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredShadowCubeProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredShadowFullbrightAlphaMaskProgram.mName = "Deferred Shadow Fullbright Alpha Mask Shader";
        gDeferredShadowFullbrightAlphaMaskProgram.mFeatures.mIndexedTextureChannels = LLHLSLShader::sIndexedTextureChannels;

        gDeferredShadowFullbrightAlphaMaskProgram.mShaderFiles.clear();
        gDeferredShadowFullbrightAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/shadowAlphaMaskV.glsl", GL_VERTEX_SHADER));
        gDeferredShadowFullbrightAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/shadowAlphaMaskF.glsl", GL_FRAGMENT_SHADER));

        gDeferredShadowFullbrightAlphaMaskProgram.clearPermutations();
        gDeferredShadowFullbrightAlphaMaskProgram.addPermutation("DEPTH_CLAMP", "1");
        gDeferredShadowFullbrightAlphaMaskProgram.addPermutation("IS_FULLBRIGHT", "1");

        add_common_permutations(&gDeferredShadowFullbrightAlphaMaskProgram);

        gDeferredShadowFullbrightAlphaMaskProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = make_rigged_variant(gDeferredShadowFullbrightAlphaMaskProgram, gDeferredSkinnedShadowFullbrightAlphaMaskProgram);
        success = success && gDeferredShadowFullbrightAlphaMaskProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredShadowAlphaMaskProgram.mName = "Deferred Shadow Alpha Mask Shader";
        gDeferredShadowAlphaMaskProgram.mFeatures.mIndexedTextureChannels = LLHLSLShader::sIndexedTextureChannels;

        gDeferredShadowAlphaMaskProgram.mShaderFiles.clear();
        gDeferredShadowAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/shadowAlphaMaskV.glsl", GL_VERTEX_SHADER));
        gDeferredShadowAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/shadowAlphaMaskF.glsl", GL_FRAGMENT_SHADER));
        gDeferredShadowAlphaMaskProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = make_rigged_variant(gDeferredShadowAlphaMaskProgram, gDeferredSkinnedShadowAlphaMaskProgram);
        success = success && gDeferredShadowAlphaMaskProgram.createShader();
        llassert(success);
    }


    if (success)
    {
        gDeferredShadowGLTFAlphaMaskProgram.mName = "Deferred GLTF Shadow Alpha Mask Shader";
        gDeferredShadowGLTFAlphaMaskProgram.mShaderFiles.clear();
        gDeferredShadowGLTFAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/pbrShadowAlphaMaskV.glsl", GL_VERTEX_SHADER));
        gDeferredShadowGLTFAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/pbrShadowAlphaMaskF.glsl", GL_FRAGMENT_SHADER));
        gDeferredShadowGLTFAlphaMaskProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredShadowGLTFAlphaMaskProgram.clearPermutations();

        add_common_permutations(&gDeferredShadowGLTFAlphaMaskProgram);

        success = make_rigged_variant(gDeferredShadowGLTFAlphaMaskProgram, gDeferredSkinnedShadowGLTFAlphaMaskProgram);
        success = success && gDeferredShadowGLTFAlphaMaskProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredShadowGLTFAlphaBlendProgram.mName = "Deferred GLTF Shadow Alpha Blend Shader";
        gDeferredShadowGLTFAlphaBlendProgram.mShaderFiles.clear();
        gDeferredShadowGLTFAlphaBlendProgram.mShaderFiles.push_back(make_pair("deferred/pbrShadowAlphaMaskV.glsl", GL_VERTEX_SHADER));
        gDeferredShadowGLTFAlphaBlendProgram.mShaderFiles.push_back(make_pair("deferred/pbrShadowAlphaBlendF.glsl", GL_FRAGMENT_SHADER));
        gDeferredShadowGLTFAlphaBlendProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredShadowGLTFAlphaBlendProgram.clearPermutations();

        add_common_permutations(&gDeferredShadowGLTFAlphaBlendProgram);

        success = make_rigged_variant(gDeferredShadowGLTFAlphaBlendProgram, gDeferredSkinnedShadowGLTFAlphaBlendProgram);
        success = success && gDeferredShadowGLTFAlphaBlendProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredAvatarShadowProgram.mName = "Deferred Avatar Shadow Shader";
        gDeferredAvatarShadowProgram.mFeatures.hasSkinning = true;

        gDeferredAvatarShadowProgram.mShaderFiles.clear();
        gDeferredAvatarShadowProgram.mShaderFiles.push_back(make_pair("deferred/avatarShadowV.glsl", GL_VERTEX_SHADER));
        gDeferredAvatarShadowProgram.mShaderFiles.push_back(make_pair("deferred/avatarShadowF.glsl", GL_FRAGMENT_SHADER));
        gDeferredAvatarShadowProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredAvatarShadowProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredAvatarAlphaShadowProgram.mName = "Deferred Avatar Alpha Shadow Shader";
        gDeferredAvatarAlphaShadowProgram.mFeatures.hasSkinning = true;
        gDeferredAvatarAlphaShadowProgram.mShaderFiles.clear();
        gDeferredAvatarAlphaShadowProgram.mShaderFiles.push_back(make_pair("deferred/avatarAlphaShadowV.glsl", GL_VERTEX_SHADER));
        gDeferredAvatarAlphaShadowProgram.mShaderFiles.push_back(make_pair("deferred/avatarAlphaShadowF.glsl", GL_FRAGMENT_SHADER));
        gDeferredAvatarAlphaShadowProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredAvatarAlphaShadowProgram.createShader();
        llassert(success);
    }
    if (success)
    {
        gDeferredAvatarAlphaMaskShadowProgram.mName = "Deferred Avatar Alpha Mask Shadow Shader";
        gDeferredAvatarAlphaMaskShadowProgram.mFeatures.hasSkinning  = true;
        gDeferredAvatarAlphaMaskShadowProgram.mShaderFiles.clear();
        gDeferredAvatarAlphaMaskShadowProgram.mShaderFiles.push_back(make_pair("deferred/avatarAlphaShadowV.glsl", GL_VERTEX_SHADER));
        gDeferredAvatarAlphaMaskShadowProgram.mShaderFiles.push_back(make_pair("deferred/avatarAlphaMaskShadowF.glsl", GL_FRAGMENT_SHADER));
        gDeferredAvatarAlphaMaskShadowProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredAvatarAlphaMaskShadowProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredTerrainProgram.mName = "Deferred Terrain Shader";
        gDeferredTerrainProgram.mFeatures.hasSrgb = true;
        gDeferredTerrainProgram.mFeatures.isAlphaLighting = true;
        gDeferredTerrainProgram.mFeatures.calculatesAtmospherics = true;
        gDeferredTerrainProgram.mFeatures.hasAtmospherics = true;
        gDeferredTerrainProgram.mFeatures.hasGamma = true;

        gDeferredTerrainProgram.mShaderFiles.clear();
        gDeferredTerrainProgram.mShaderFiles.push_back(make_pair("deferred/terrainV.glsl", GL_VERTEX_SHADER));
        gDeferredTerrainProgram.mShaderFiles.push_back(make_pair("deferred/terrainF.glsl", GL_FRAGMENT_SHADER));

        add_common_permutations(&gDeferredTerrainProgram);

        gDeferredTerrainProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredTerrainProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredAvatarProgram.mName = "Deferred Avatar Shader";
        gDeferredAvatarProgram.mFeatures.hasSkinning = true;
        gDeferredAvatarProgram.mShaderFiles.clear();
        gDeferredAvatarProgram.mShaderFiles.push_back(make_pair("deferred/avatarV.glsl", GL_VERTEX_SHADER));
        gDeferredAvatarProgram.mShaderFiles.push_back(make_pair("deferred/avatarF.glsl", GL_FRAGMENT_SHADER));
        gDeferredAvatarProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        gDeferredAvatarProgram.clearPermutations();
        add_common_permutations(&gDeferredAvatarProgram);
        gDeferredAvatarProgram.addPermutation("AVATAR_CLOTH", LLPipeline::RenderAvatarCloth ? "1" : "0");

        success = gDeferredAvatarProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredAvatarAlphaProgram.mName = "Deferred Avatar Alpha Shader";
        gDeferredAvatarAlphaProgram.mFeatures.hasSkinning = true;
        gDeferredAvatarAlphaProgram.mFeatures.calculatesLighting = false;
        gDeferredAvatarAlphaProgram.mFeatures.hasLighting = false;
        gDeferredAvatarAlphaProgram.mFeatures.isAlphaLighting = true;
        gDeferredAvatarAlphaProgram.mFeatures.hasSrgb = true;
        gDeferredAvatarAlphaProgram.mFeatures.calculatesAtmospherics = true;
        gDeferredAvatarAlphaProgram.mFeatures.hasAtmospherics = true;
        gDeferredAvatarAlphaProgram.mFeatures.hasGamma = true;
        gDeferredAvatarAlphaProgram.mFeatures.isDeferred = true;
        gDeferredAvatarAlphaProgram.mFeatures.hasShadows = true;
        gDeferredAvatarAlphaProgram.mFeatures.hasReflectionProbes = true;

        gDeferredAvatarAlphaProgram.mShaderFiles.clear();
        gDeferredAvatarAlphaProgram.mShaderFiles.push_back(make_pair("deferred/alphaV.glsl", GL_VERTEX_SHADER));
        gDeferredAvatarAlphaProgram.mShaderFiles.push_back(make_pair("deferred/alphaF.glsl", GL_FRAGMENT_SHADER));

        gDeferredAvatarAlphaProgram.clearPermutations();
        gDeferredAvatarAlphaProgram.addPermutation("USE_DIFFUSE_TEX", "1");
        gDeferredAvatarAlphaProgram.addPermutation("IS_AVATAR_SKIN", "1");
        if (use_sun_shadow)
        {
            gDeferredAvatarAlphaProgram.addPermutation("HAS_SUN_SHADOW", "1");
        }

        gDeferredAvatarAlphaProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredAvatarAlphaProgram);

        success = gDeferredAvatarAlphaProgram.createShader();
        llassert(success);

        gDeferredAvatarAlphaProgram.mFeatures.calculatesLighting = true;
        gDeferredAvatarAlphaProgram.mFeatures.hasLighting = true;
    }

    if (success)
    {
        gExposureProgram.mName = "Exposure";
        gExposureProgram.mFeatures.hasSrgb = true;
        gExposureProgram.mFeatures.isDeferred = true;
        gExposureProgram.mShaderFiles.clear();
        gExposureProgram.clearPermutations();
        gExposureProgram.addPermutation("USE_LAST_EXPOSURE", "1");
        gExposureProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gExposureProgram.mShaderFiles.push_back(make_pair("deferred/exposureF.glsl", GL_FRAGMENT_SHADER));
        gExposureProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gExposureProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gExposureProgramNoFade.mName = "Exposure (no fade)";
        gExposureProgramNoFade.mFeatures.hasSrgb = true;
        gExposureProgramNoFade.mFeatures.isDeferred = true;
        gExposureProgramNoFade.mShaderFiles.clear();
        gExposureProgramNoFade.clearPermutations();
        gExposureProgramNoFade.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gExposureProgramNoFade.mShaderFiles.push_back(make_pair("deferred/exposureF.glsl", GL_FRAGMENT_SHADER));
        gExposureProgramNoFade.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gExposureProgramNoFade.createShader();
        llassert(success);
    }

    if (success)
    {
        gLuminanceProgram.mName = "Luminance";
        gLuminanceProgram.mShaderFiles.clear();
        gLuminanceProgram.clearPermutations();
        gLuminanceProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gLuminanceProgram.mShaderFiles.push_back(make_pair("deferred/luminanceF.glsl", GL_FRAGMENT_SHADER));
        gLuminanceProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gLuminanceProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredPostGammaCorrectProgram.mName = "Deferred Gamma Correction Post Process";
        gDeferredPostGammaCorrectProgram.mFeatures.hasSrgb = true;
        gDeferredPostGammaCorrectProgram.mFeatures.isDeferred = true;
        gDeferredPostGammaCorrectProgram.mShaderFiles.clear();
        gDeferredPostGammaCorrectProgram.clearPermutations();
        gDeferredPostGammaCorrectProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gDeferredPostGammaCorrectProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredGammaCorrect.glsl", GL_FRAGMENT_SHADER));
        gDeferredPostGammaCorrectProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredPostGammaCorrectProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gLegacyPostGammaCorrectProgram.mName = "Legacy Gamma Correction Post Process";
        gLegacyPostGammaCorrectProgram.mFeatures.hasSrgb = true;
        gLegacyPostGammaCorrectProgram.mFeatures.isDeferred = true;
        gLegacyPostGammaCorrectProgram.mShaderFiles.clear();
        gLegacyPostGammaCorrectProgram.clearPermutations();
        gLegacyPostGammaCorrectProgram.addPermutation("LEGACY_GAMMA", "1");
        gLegacyPostGammaCorrectProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gLegacyPostGammaCorrectProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredGammaCorrect.glsl", GL_FRAGMENT_SHADER));
        gLegacyPostGammaCorrectProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gLegacyPostGammaCorrectProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        // S24 (2026-08-26, task #263): deliberately NOT mFeatures.isDeferred/
        // hasSrgb - unlike gDeferredPostGammaCorrectProgram above, this is a
        // standalone image resize with no deferred G-buffer dependency and
        // no color-space conversion of its own (operates on already-gamma-
        // corrected content) - same minimal registration shape gGlowProgram
        // uses for the same reason. Not added to mShaderList either (see
        // that list's own "ONLY shaders that need WL Param management"
        // comment) - this shader has no WindLight/environment uniforms.
        gResizeBicubicProgram.mName = "GPU Resize Bicubic";
        // S24 (2026-08-26, task #263 round 6): hasSrgb attaches srgbF.glsl's
        // srgb_to_linear()/linear_to_srgb() (same mechanism gCASProgram/
        // gDeferredPostGammaCorrectProgram use) so the shader can blend its
        // 4 taps in linear light instead of on the already gamma-encoded
        // source (the composited post target this reads is post-gamma-
        // correct) - blending gamma-encoded values directly is a real,
        // if subtle, error: it weights mid-tones wrong versus blending the
        // light they actually represent.
        gResizeBicubicProgram.mFeatures.hasSrgb = true;
        gResizeBicubicProgram.mShaderFiles.clear();
        gResizeBicubicProgram.clearPermutations();
        gResizeBicubicProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gResizeBicubicProgram.mShaderFiles.push_back(make_pair("deferred/resizeBicubic.glsl", GL_FRAGMENT_SHADER));
        gResizeBicubicProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gResizeBicubicProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredPostTonemapProgram.mName = "Deferred Tonemap Post Process";
        gDeferredPostTonemapProgram.mFeatures.hasSrgb = true;
        gDeferredPostTonemapProgram.mFeatures.isDeferred = true;
        gDeferredPostTonemapProgram.mFeatures.hasTonemap = true;
        gDeferredPostTonemapProgram.mShaderFiles.clear();
        gDeferredPostTonemapProgram.clearPermutations();
        gDeferredPostTonemapProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gDeferredPostTonemapProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredTonemap.glsl", GL_FRAGMENT_SHADER));
        gDeferredPostTonemapProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredPostTonemapProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gNoPostTonemapProgram.mName = "No Post Tonemap Post Process";
        gNoPostTonemapProgram.mFeatures.hasSrgb = true;
        gNoPostTonemapProgram.mFeatures.isDeferred = true;
        gNoPostTonemapProgram.mFeatures.hasTonemap = true;
        gNoPostTonemapProgram.mShaderFiles.clear();
        gNoPostTonemapProgram.clearPermutations();
        gNoPostTonemapProgram.addPermutation("NO_POST", "1");
        gNoPostTonemapProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gNoPostTonemapProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredTonemap.glsl", GL_FRAGMENT_SHADER));
        gNoPostTonemapProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gNoPostTonemapProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredPostTonemapGammaCorrectProgram.mName = "Deferred Tonemap Gamma Post Process";
        gDeferredPostTonemapGammaCorrectProgram.mFeatures.hasSrgb = true;
        gDeferredPostTonemapGammaCorrectProgram.mFeatures.isDeferred = true;
        gDeferredPostTonemapGammaCorrectProgram.mFeatures.hasTonemap = true;
        gDeferredPostTonemapGammaCorrectProgram.mShaderFiles.clear();
        gDeferredPostTonemapGammaCorrectProgram.clearPermutations();
        gDeferredPostTonemapGammaCorrectProgram.addPermutation("GAMMA_CORRECT", "1");
        gDeferredPostTonemapGammaCorrectProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gDeferredPostTonemapGammaCorrectProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredTonemap.glsl", GL_FRAGMENT_SHADER));
        gDeferredPostTonemapGammaCorrectProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredPostTonemapGammaCorrectProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gNoPostTonemapGammaCorrectProgram.mName = "No Post Tonemap Gamma Post Process";
        gNoPostTonemapGammaCorrectProgram.mFeatures.hasSrgb = true;
        gNoPostTonemapGammaCorrectProgram.mFeatures.isDeferred = true;
        gNoPostTonemapGammaCorrectProgram.mFeatures.hasTonemap = true;
        gNoPostTonemapGammaCorrectProgram.mShaderFiles.clear();
        gNoPostTonemapGammaCorrectProgram.clearPermutations();
        gNoPostTonemapGammaCorrectProgram.addPermutation("GAMMA_CORRECT", "1");
        gNoPostTonemapGammaCorrectProgram.addPermutation("NO_POST", "1");
        gNoPostTonemapGammaCorrectProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gNoPostTonemapGammaCorrectProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredTonemap.glsl", GL_FRAGMENT_SHADER));
        gNoPostTonemapGammaCorrectProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gNoPostTonemapGammaCorrectProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredPostTonemapLegacyGammaCorrectProgram.mName = "Deferred Tonemap Legacy Gamma Post Process";
        gDeferredPostTonemapLegacyGammaCorrectProgram.mFeatures.hasSrgb = true;
        gDeferredPostTonemapProgram.mFeatures.isDeferred = true;
        gDeferredPostTonemapLegacyGammaCorrectProgram.mFeatures.hasTonemap = true;
        gDeferredPostTonemapLegacyGammaCorrectProgram.mShaderFiles.clear();
        gDeferredPostTonemapLegacyGammaCorrectProgram.clearPermutations();
        gDeferredPostTonemapLegacyGammaCorrectProgram.addPermutation("GAMMA_CORRECT", "1");
        gDeferredPostTonemapLegacyGammaCorrectProgram.addPermutation("LEGACY_GAMMA", "1");
        gDeferredPostTonemapLegacyGammaCorrectProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gDeferredPostTonemapLegacyGammaCorrectProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredTonemap.glsl", GL_FRAGMENT_SHADER));
        gDeferredPostTonemapLegacyGammaCorrectProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredPostTonemapLegacyGammaCorrectProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gNoPostTonemapLegacyGammaCorrectProgram.mName = "No Post Tonemap Legacy Gamma Post Process";
        gNoPostTonemapLegacyGammaCorrectProgram.mFeatures.hasSrgb = true;
        gNoPostTonemapLegacyGammaCorrectProgram.mFeatures.isDeferred = true;
        gNoPostTonemapLegacyGammaCorrectProgram.mFeatures.hasTonemap = true;
        gNoPostTonemapLegacyGammaCorrectProgram.mShaderFiles.clear();
        gNoPostTonemapLegacyGammaCorrectProgram.clearPermutations();
        gNoPostTonemapLegacyGammaCorrectProgram.addPermutation("NO_POST", "1");
        gNoPostTonemapLegacyGammaCorrectProgram.addPermutation("GAMMA_CORRECT", "1");
        gNoPostTonemapLegacyGammaCorrectProgram.addPermutation("LEGACY_GAMMA", "1");
        gNoPostTonemapLegacyGammaCorrectProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gNoPostTonemapLegacyGammaCorrectProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredTonemap.glsl", GL_FRAGMENT_SHADER));
        gNoPostTonemapLegacyGammaCorrectProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gNoPostTonemapLegacyGammaCorrectProgram.createShader();
        llassert(success);
    }

    if (success && gGLManager.mGLVersion > 3.9f)
    {
        std::vector<std::pair<std::string, std::string>> quality_levels = { {"12", "Low"},
                                                                             {"23", "Medium"},
                                                                             {"28", "High"},
                                                                             {"39", "Ultra"} };
        int i = 0;
        bool failed = false;
        for (const auto& quality_pair : quality_levels)
        {
            if (success)
            {
                gFXAAProgram[i].mName = llformat("FXAA Shader (%s)", quality_pair.second.c_str());
                gFXAAProgram[i].mFeatures.isDeferred = true;
                gFXAAProgram[i].mShaderFiles.clear();
                gFXAAProgram[i].mShaderFiles.push_back(make_pair("deferred/postDeferredV.glsl", GL_VERTEX_SHADER));
                gFXAAProgram[i].mShaderFiles.push_back(make_pair("deferred/fxaaF.glsl", GL_FRAGMENT_SHADER));

                gFXAAProgram[i].clearPermutations();
                gFXAAProgram[i].addPermutation("FXAA_QUALITY__PRESET", quality_pair.first);
                if (gGLManager.mGLVersion > 3.9)
                {
                    gFXAAProgram[i].addPermutation("FXAA_GLSL_400", "1");
                }
                else
                {
                    gFXAAProgram[i].addPermutation("FXAA_GLSL_130", "1");
                }

                gFXAAProgram[i].mShaderLevel = mShaderLevel[SHADER_DEFERRED];
                success = gFXAAProgram[i].createShader();
                // llassert(success);
                if (!success)
                {
                    LL_WARNS() << "Failed to create shader '" << gFXAAProgram[i].mName << "', disabling!" << LL_ENDL;
                    // continue as if this shader never happened
                    failed = true;
                    success = true;
                    break;
                }
            }
            ++i;
        }

        if (failed)
        {
            for (auto i = 0; i < 4; ++i)
            {
                gFXAAProgram[i].unload();
            }
        }
    }

    if (gGLManager.mGLVersion > 3.15f && success)
    {
        std::vector<std::pair<std::string, std::string>> quality_levels = { {"SMAA_PRESET_LOW", "Low"},
                                                                             {"SMAA_PRESET_MEDIUM", "Medium"},
                                                                             {"SMAA_PRESET_HIGH", "High"},
                                                                          {"SMAA_PRESET_ULTRA", "Ultra"} };
        int i = 0;
        bool failed = false;
        for (const auto& smaa_pair : quality_levels)
        {
            std::map<std::string, std::string> defines;
            if (gGLManager.mGLVersion >= 4.f)
                defines.emplace("SMAA_GLSL_4", "1");
            else if (gGLManager.mGLVersion >= 3.1f)
                defines.emplace("SMAA_GLSL_3", "1");
            else
                defines.emplace("SMAA_GLSL_2", "1");
            defines.emplace("SMAA_PREDICATION", "0");
            defines.emplace("SMAA_REPROJECTION", "0");
            defines.emplace(smaa_pair.first, "1");

            if (success)
            {
                gSMAAEdgeDetectProgram[i].mName = llformat("SMAA Edge Detection (%s)", smaa_pair.second.c_str());
                gSMAAEdgeDetectProgram[i].mFeatures.isDeferred = true;

                gSMAAEdgeDetectProgram[i].clearPermutations();
                gSMAAEdgeDetectProgram[i].addPermutations(defines);

                gSMAAEdgeDetectProgram[i].mShaderFiles.clear();
                gSMAAEdgeDetectProgram[i].mShaderFiles.push_back(make_pair("deferred/SMAAEdgeDetectF.glsl", GL_FRAGMENT_SHADER_ARB));
                gSMAAEdgeDetectProgram[i].mShaderFiles.push_back(make_pair("deferred/SMAAEdgeDetectV.glsl", GL_VERTEX_SHADER_ARB));
                gSMAAEdgeDetectProgram[i].mShaderFiles.push_back(make_pair("deferred/SMAA.glsl", GL_FRAGMENT_SHADER_ARB));
                gSMAAEdgeDetectProgram[i].mShaderFiles.push_back(make_pair("deferred/SMAA.glsl", GL_VERTEX_SHADER_ARB));
                gSMAAEdgeDetectProgram[i].mShaderLevel = mShaderLevel[SHADER_DEFERRED];
                success = gSMAAEdgeDetectProgram[i].createShader();
                // llassert(success);
                if (!success)
                {
                    LL_WARNS() << "Failed to create shader '" << gSMAAEdgeDetectProgram[i].mName << "', disabling!" << LL_ENDL;
                    // continue as if this shader never happened
                    failed = true;
                    success = true;
                    break;
                }
            }

            if (success)
            {
                gSMAABlendWeightsProgram[i].mName = llformat("SMAA Blending Weights (%s)", smaa_pair.second.c_str());
                gSMAABlendWeightsProgram[i].mFeatures.isDeferred = true;

                gSMAABlendWeightsProgram[i].clearPermutations();
                gSMAABlendWeightsProgram[i].addPermutations(defines);

                gSMAABlendWeightsProgram[i].mShaderFiles.clear();
                gSMAABlendWeightsProgram[i].mShaderFiles.push_back(make_pair("deferred/SMAABlendWeightsF.glsl", GL_FRAGMENT_SHADER_ARB));
                gSMAABlendWeightsProgram[i].mShaderFiles.push_back(make_pair("deferred/SMAABlendWeightsV.glsl", GL_VERTEX_SHADER_ARB));
                gSMAABlendWeightsProgram[i].mShaderFiles.push_back(make_pair("deferred/SMAA.glsl", GL_FRAGMENT_SHADER_ARB));
                gSMAABlendWeightsProgram[i].mShaderFiles.push_back(make_pair("deferred/SMAA.glsl", GL_VERTEX_SHADER_ARB));
                gSMAABlendWeightsProgram[i].mShaderLevel = mShaderLevel[SHADER_DEFERRED];
                success = gSMAABlendWeightsProgram[i].createShader();
                // llassert(success);
                if (!success)
                {
                    LL_WARNS() << "Failed to create shader '" << gSMAABlendWeightsProgram[i].mName << "', disabling!" << LL_ENDL;
                    // continue as if this shader never happened
                    failed = true;
                    success = true;
                    break;
                }
            }

            if (success)
            {
                gSMAANeighborhoodBlendProgram[i].mName = llformat("SMAA Neighborhood Blending (%s)", smaa_pair.second.c_str());
                gSMAANeighborhoodBlendProgram[i].mFeatures.isDeferred = true;

                gSMAANeighborhoodBlendProgram[i].clearPermutations();
                gSMAANeighborhoodBlendProgram[i].addPermutations(defines);

                gSMAANeighborhoodBlendProgram[i].mShaderFiles.clear();
                gSMAANeighborhoodBlendProgram[i].mShaderFiles.push_back(make_pair("deferred/SMAANeighborhoodBlendF.glsl", GL_FRAGMENT_SHADER_ARB));
                gSMAANeighborhoodBlendProgram[i].mShaderFiles.push_back(make_pair("deferred/SMAANeighborhoodBlendV.glsl", GL_VERTEX_SHADER_ARB));
                gSMAANeighborhoodBlendProgram[i].mShaderFiles.push_back(make_pair("deferred/SMAA.glsl", GL_FRAGMENT_SHADER_ARB));
                gSMAANeighborhoodBlendProgram[i].mShaderFiles.push_back(make_pair("deferred/SMAA.glsl", GL_VERTEX_SHADER_ARB));
                gSMAANeighborhoodBlendProgram[i].mShaderLevel = mShaderLevel[SHADER_DEFERRED];
                success = gSMAANeighborhoodBlendProgram[i].createShader();
                // llassert(success);
                if (!success)
                {
                    LL_WARNS() << "Failed to create shader '" << gSMAANeighborhoodBlendProgram[i].mName << "', disabling!" << LL_ENDL;
                    // continue as if this shader never happened
                    failed = true;
                    success = true;
                    break;
                }
            }
            ++i;
        }

        if (failed)
        {
            for (auto i = 0; i < 4; ++i)
            {
                gSMAAEdgeDetectProgram[i].unload();
                gSMAABlendWeightsProgram[i].unload();
                gSMAANeighborhoodBlendProgram[i].unload();
            }
        }
    }

    if (success && gGLManager.mGLVersion > 4.05f)
    {
        gCASProgram.mName = "Contrast Adaptive Sharpening Shader";
        gCASProgram.mFeatures.hasSrgb = true;
        gCASProgram.mShaderFiles.clear();
        gCASProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gCASProgram.mShaderFiles.push_back(make_pair("deferred/CASF.glsl", GL_FRAGMENT_SHADER));
        gCASProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gCASProgram.createShader();
        // llassert(success);
        if (!success)
        {
            LL_WARNS() << "Failed to create shader '" << gCASProgram.mName << "', disabling!" << LL_ENDL;
            // continue as if this shader never happened
            success = true;
        }
    }

    if (success && gGLManager.mGLVersion > 4.05f)
    {
        gCASLegacyGammaProgram.mName = "Contrast Adaptive Sharpening Legacy Gamma Shader";
        gCASLegacyGammaProgram.mFeatures.hasSrgb = true;
        gCASLegacyGammaProgram.mShaderFiles.clear();
        gCASLegacyGammaProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gCASLegacyGammaProgram.mShaderFiles.push_back(make_pair("deferred/CASF.glsl", GL_FRAGMENT_SHADER));
        gCASLegacyGammaProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gCASLegacyGammaProgram.clearPermutations();
        gCASLegacyGammaProgram.addPermutation("GAMMA_CORRECT", "1");
        gCASLegacyGammaProgram.addPermutation("LEGACY_GAMMA", "1");
        success = gCASLegacyGammaProgram.createShader();
        // llassert(success);
        if (!success)
        {
            LL_WARNS() << "Failed to create shader '" << gCASProgram.mName << "', disabling!" << LL_ENDL;
            // continue as if this shader never happened
            success = true;
        }
    }

    if (success)
    {
        gDeferredPostProgram.mName = "Deferred Post Shader";
        gDeferredPostProgram.mFeatures.isDeferred = true;
        gDeferredPostProgram.mShaderFiles.clear();
        gDeferredPostProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gDeferredPostProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredF.glsl", GL_FRAGMENT_SHADER));
        gDeferredPostProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredPostProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredCoFProgram.mName = "Deferred CoF Shader";
        gDeferredCoFProgram.mShaderFiles.clear();
        gDeferredCoFProgram.mFeatures.isDeferred = true;
        gDeferredCoFProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gDeferredCoFProgram.mShaderFiles.push_back(make_pair("deferred/cofF.glsl", GL_FRAGMENT_SHADER));
        gDeferredCoFProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredCoFProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredDoFCombineProgram.mName = "Deferred DoFCombine Shader";
        gDeferredDoFCombineProgram.mFeatures.isDeferred = true;
        gDeferredDoFCombineProgram.mShaderFiles.clear();
        gDeferredDoFCombineProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gDeferredDoFCombineProgram.mShaderFiles.push_back(make_pair("deferred/dofCombineF.glsl", GL_FRAGMENT_SHADER));
        gDeferredDoFCombineProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredDoFCombineProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredPostNoDoFProgram.mName = "Deferred Post NoDoF Shader";
        gDeferredPostNoDoFProgram.mFeatures.isDeferred = true;
        gDeferredPostNoDoFProgram.mShaderFiles.clear();
        gDeferredPostNoDoFProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gDeferredPostNoDoFProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoDoFF.glsl", GL_FRAGMENT_SHADER));
        gDeferredPostNoDoFProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredPostNoDoFProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredPostNoDoFNoiseProgram.mName = "Deferred Post NoDoF Noise Shader";
        gDeferredPostNoDoFNoiseProgram.mFeatures.isDeferred = true;
        gDeferredPostNoDoFNoiseProgram.mShaderFiles.clear();
        gDeferredPostNoDoFNoiseProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gDeferredPostNoDoFNoiseProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoDoFF.glsl", GL_FRAGMENT_SHADER));

        gDeferredPostNoDoFNoiseProgram.clearPermutations();
        gDeferredPostNoDoFNoiseProgram.addPermutation("HAS_NOISE", "1");

        gDeferredPostNoDoFNoiseProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredPostNoDoFNoiseProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gEnvironmentMapProgram.mName = "Environment Map Program";
        gEnvironmentMapProgram.mShaderFiles.clear();
        gEnvironmentMapProgram.mFeatures.calculatesAtmospherics = true;
        gEnvironmentMapProgram.mFeatures.hasAtmospherics = true;
        gEnvironmentMapProgram.mFeatures.hasGamma = true;
        gEnvironmentMapProgram.mFeatures.hasSrgb = true;

        gEnvironmentMapProgram.clearPermutations();
        gEnvironmentMapProgram.addPermutation("HAS_HDRI", "1");
        add_common_permutations(&gEnvironmentMapProgram);
        gEnvironmentMapProgram.mShaderFiles.push_back(make_pair("deferred/skyV.glsl", GL_VERTEX_SHADER));
        gEnvironmentMapProgram.mShaderFiles.push_back(make_pair("deferred/skyF.glsl", GL_FRAGMENT_SHADER));
        gEnvironmentMapProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gEnvironmentMapProgram.mShaderGroup = LLHLSLShader::SG_SKY;

        success = gEnvironmentMapProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredWLSkyProgram.mName = "Deferred Windlight Sky Shader";
        gDeferredWLSkyProgram.mShaderFiles.clear();
        gDeferredWLSkyProgram.mFeatures.calculatesAtmospherics = true;
        gDeferredWLSkyProgram.mFeatures.hasAtmospherics = true;
        gDeferredWLSkyProgram.mFeatures.hasGamma = true;
        gDeferredWLSkyProgram.mFeatures.hasSrgb = true;

        gDeferredWLSkyProgram.mShaderFiles.push_back(make_pair("deferred/skyV.glsl", GL_VERTEX_SHADER));
        gDeferredWLSkyProgram.mShaderFiles.push_back(make_pair("deferred/skyF.glsl", GL_FRAGMENT_SHADER));
        gDeferredWLSkyProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredWLSkyProgram.mShaderGroup = LLHLSLShader::SG_SKY;

        add_common_permutations(&gDeferredWLSkyProgram);

        success = gDeferredWLSkyProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredWLCloudProgram.mName = "Deferred Windlight Cloud Program";
        gDeferredWLCloudProgram.mShaderFiles.clear();
        gDeferredWLCloudProgram.mFeatures.calculatesAtmospherics = true;
        gDeferredWLCloudProgram.mFeatures.hasAtmospherics = true;
        gDeferredWLCloudProgram.mFeatures.hasGamma = true;
        gDeferredWLCloudProgram.mFeatures.hasSrgb = true;

        gDeferredWLCloudProgram.mShaderFiles.push_back(make_pair("deferred/cloudsV.glsl", GL_VERTEX_SHADER));
        gDeferredWLCloudProgram.mShaderFiles.push_back(make_pair("deferred/cloudsF.glsl", GL_FRAGMENT_SHADER));
        gDeferredWLCloudProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredWLCloudProgram.mShaderGroup = LLHLSLShader::SG_SKY;
        gDeferredWLCloudProgram.addConstant( LLHLSLShader::SHADER_CONST_CLOUD_MOON_DEPTH ); // SL-14113

        add_common_permutations(&gDeferredWLCloudProgram);

        success = gDeferredWLCloudProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredWLSunProgram.mName = "Deferred Windlight Sun Program";
        gDeferredWLSunProgram.mFeatures.calculatesAtmospherics = true;
        gDeferredWLSunProgram.mFeatures.hasAtmospherics = true;
        gDeferredWLSunProgram.mFeatures.hasGamma = true;
        gDeferredWLSunProgram.mFeatures.hasAtmospherics = true;
        gDeferredWLSunProgram.mFeatures.hasSrgb = true;
        gDeferredWLSunProgram.mShaderFiles.clear();
        gDeferredWLSunProgram.mShaderFiles.push_back(make_pair("deferred/sunDiscV.glsl", GL_VERTEX_SHADER));
        gDeferredWLSunProgram.mShaderFiles.push_back(make_pair("deferred/sunDiscF.glsl", GL_FRAGMENT_SHADER));
        gDeferredWLSunProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredWLSunProgram.mShaderGroup = LLHLSLShader::SG_SKY;

        add_common_permutations(&gDeferredWLSunProgram);

        success = gDeferredWLSunProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredWLMoonProgram.mName = "Deferred Windlight Moon Program";
        gDeferredWLMoonProgram.mFeatures.calculatesAtmospherics = true;
        gDeferredWLMoonProgram.mFeatures.hasAtmospherics = true;
        gDeferredWLMoonProgram.mFeatures.hasGamma = true;
        gDeferredWLMoonProgram.mFeatures.hasAtmospherics = true;
        gDeferredWLMoonProgram.mFeatures.hasSrgb = true;

        gDeferredWLMoonProgram.mShaderFiles.clear();
        gDeferredWLMoonProgram.mShaderFiles.push_back(make_pair("deferred/moonV.glsl", GL_VERTEX_SHADER));
        gDeferredWLMoonProgram.mShaderFiles.push_back(make_pair("deferred/moonF.glsl", GL_FRAGMENT_SHADER));
        gDeferredWLMoonProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredWLMoonProgram.mShaderGroup = LLHLSLShader::SG_SKY;
        gDeferredWLMoonProgram.addConstant( LLHLSLShader::SHADER_CONST_CLOUD_MOON_DEPTH ); // SL-14113

        add_common_permutations(&gDeferredWLMoonProgram);

        success = gDeferredWLMoonProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredStarProgram.mName = "Deferred Star Program";
        gDeferredStarProgram.mShaderFiles.clear();
        gDeferredStarProgram.mShaderFiles.push_back(make_pair("deferred/starsV.glsl", GL_VERTEX_SHADER));
        gDeferredStarProgram.mShaderFiles.push_back(make_pair("deferred/starsF.glsl", GL_FRAGMENT_SHADER));
        gDeferredStarProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredStarProgram.mShaderGroup = LLHLSLShader::SG_SKY;
        gDeferredStarProgram.addConstant( LLHLSLShader::SHADER_CONST_STAR_DEPTH ); // SL-14113

        add_common_permutations(&gDeferredStarProgram);

        success = gDeferredStarProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        // S24 (task #279 stage 2, "RENDER WOW"): mirrors gDeferredStarProgram
        // above exactly - see starsShootingV/F.hlsl and
        // LLVOWLSky::drawShootingStars().
        gDeferredStarShootingProgram.mName = "Deferred Shooting Star Program";
        gDeferredStarShootingProgram.mShaderFiles.clear();
        gDeferredStarShootingProgram.mShaderFiles.push_back(make_pair("deferred/starsShootingV.glsl", GL_VERTEX_SHADER));
        gDeferredStarShootingProgram.mShaderFiles.push_back(make_pair("deferred/starsShootingF.glsl", GL_FRAGMENT_SHADER));
        gDeferredStarShootingProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredStarShootingProgram.mShaderGroup = LLHLSLShader::SG_SKY;

        add_common_permutations(&gDeferredStarShootingProgram);

        success = gDeferredStarShootingProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gNormalMapGenProgram.mName = "Normal Map Generation Program";
        gNormalMapGenProgram.mShaderFiles.clear();
        gNormalMapGenProgram.mShaderFiles.push_back(make_pair("deferred/normgenV.glsl", GL_VERTEX_SHADER));
        gNormalMapGenProgram.mShaderFiles.push_back(make_pair("deferred/normgenF.glsl", GL_FRAGMENT_SHADER));
        gNormalMapGenProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gNormalMapGenProgram.mShaderGroup = LLHLSLShader::SG_SKY;
        success = gNormalMapGenProgram.createShader();
    }

    if (success)
    {
        gDeferredGenBrdfLutProgram.mName = "Brdf Gen Shader";
        gDeferredGenBrdfLutProgram.mShaderFiles.clear();
        gDeferredGenBrdfLutProgram.mShaderFiles.push_back(make_pair("deferred/genbrdflutV.glsl", GL_VERTEX_SHADER));
        gDeferredGenBrdfLutProgram.mShaderFiles.push_back(make_pair("deferred/genbrdflutF.glsl", GL_FRAGMENT_SHADER));
        gDeferredGenBrdfLutProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredGenBrdfLutProgram.createShader();
    }

    // S24 (2026-09-04): removed from the eager startup chain entirely - a
    // live, reproducible startup hang ("we seem to be hanging on screen
    // space reflection post shader") landed directly on this program's
    // compile, same symptom/chokepoint as task #261's AMD buffer-
    // visualization lockup just above (LLHLSLShader::createShader()'s
    // driver-side CreatePixelShader() call). Unlike that shader, this one
    // has no lazy-compile-on-first-use option to fall back to - task #269's
    // audit (2026-08-27) and this file's own screenSpaceReflPostF.hlsl
    // comment both independently confirm gPostScreenSpaceReflectionProgram
    // is created at startup and NEVER bound/drawn anywhere on either
    // backend (re-confirmed via tree-wide grep just now: zero .bind()/
    // .isComplete() call sites at all, only its own creation). No legitimate
    // trigger point exists to defer compilation to, so simply never
    // compiling it is a strictly safe, unconditionally-correct fix
    // regardless of root cause - dead code has no business blocking
    // startup for everyone. mName/mShaderFiles left uncleared/unset
    // (default-constructed LLHLSLShader, isComplete() stays false, matching
    // its prior "never bound" reality exactly).

    // S24 (2026-08-24, task #261): gDeferredBufferVisualProgram's own
    // createShader() call moved OUT of this eager startup chain - see
    // loadShaderBufferVisualization() below for why.

    return success;
}

// S24 (2026-08-24, task #261): first real 0.1 alpha bug report - two
// independent AMD GPU users both saw the viewer lock up on the startup
// "Compiling shader: ..." splash dialog (LLHLSLShader::createShader()'s
// LLSplashScreen::update() call is the exact chokepoint named in the
// report), and static analysis narrowed it to specifically this shader:
// "Deferred Buffer Visualization Shader" (Develop > Rendering > Buffer
// Visualization, postDeferredVisualizeBuffers.hlsl) is genuinely a debug/
// diagnostic-only feature never used in normal gameplay, yet was being
// unconditionally compiled during loadShadersDeferred()'s mandatory startup
// chain like every real rendering shader - meaning a driver-side compile
// hang here (CreatePixelShader() invokes the GPU vendor's own DXBC->native-
// ISA backend compiler, a genuinely different code path per vendor, unlike
// the vendor-neutral CPU-side D3DCompile front-end step) blocked EVERY user
// from ever reaching the main viewer, not just the ones who'd actually open
// this debug view. No definite root cause was found via source inspection
// alone (the shader itself is trivial - one texture sample, no loops; no
// register collisions with unconditionally-attached shared files; no class3
// override exists to be silently substituted) - a genuine AMD driver-side
// compiler bug on some legal-but-unusual generated-bytecode pattern is
// plausible but unconfirmed without live AMD hardware to repro against.
// Rather than guess at the shader content, this is a real, unconditionally-
// correct mitigation regardless of root cause: a rarely-used debug shader
// has no business being compiled eagerly at startup at all. Moved to a
// lazy, on-first-use compile from LLPipeline::visualizeBuffers() instead -
// this cannot block startup for anyone who never opens the debug view, and
// if it does still hang for someone who does open it, that's now an
// isolated, diagnosable-in-the-moment problem instead of an unconditional
// block on the whole viewer for every user with this GPU class.
bool LLViewerShaderMgr::loadShaderBufferVisualization()
{
    gDeferredBufferVisualProgram.mName = "Deferred Buffer Visualization Shader";
    gDeferredBufferVisualProgram.mShaderFiles.clear();
    gDeferredBufferVisualProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
    gDeferredBufferVisualProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredVisualizeBuffers.glsl", GL_FRAGMENT_SHADER));
    gDeferredBufferVisualProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

    add_common_permutations(&gDeferredBufferVisualProgram);

    return gDeferredBufferVisualProgram.createShader();
}

bool LLViewerShaderMgr::loadShadersObject()
{
    bool success = true;

    if (success)
    {
        gObjectBumpProgram.mName = "Bump Shader";
        gObjectBumpProgram.mShaderFiles.clear();
        gObjectBumpProgram.mShaderFiles.push_back(make_pair("objects/bumpV.glsl", GL_VERTEX_SHADER));
        gObjectBumpProgram.mShaderFiles.push_back(make_pair("objects/bumpF.glsl", GL_FRAGMENT_SHADER));
        gObjectBumpProgram.mShaderLevel = mShaderLevel[SHADER_OBJECT];
        success = make_rigged_variant(gObjectBumpProgram, gSkinnedObjectBumpProgram);
        success = success && gObjectBumpProgram.createShader();
        if (success)
        { //lldrawpoolbump assumes "texture0" has channel 0 and "texture1" has channel 1
            LLHLSLShader* shader[] = { &gObjectBumpProgram, &gSkinnedObjectBumpProgram };
            for (int i = 0; i < 2; ++i)
            {
                shader[i]->bind();
                shader[i]->uniform1i(sTexture0, 0);
                shader[i]->uniform1i(sTexture1, 1);
                shader[i]->unbind();
            }
        }
    }

    if (success)
    {
        gObjectAlphaMaskNoColorProgram.mName = "No color alpha mask Shader";
        gObjectAlphaMaskNoColorProgram.mFeatures.calculatesLighting = true;
        gObjectAlphaMaskNoColorProgram.mFeatures.calculatesAtmospherics = true;
        gObjectAlphaMaskNoColorProgram.mFeatures.hasGamma = true;
        gObjectAlphaMaskNoColorProgram.mFeatures.hasAtmospherics = true;
        gObjectAlphaMaskNoColorProgram.mFeatures.hasLighting = true;
        gObjectAlphaMaskNoColorProgram.mFeatures.hasAlphaMask = true;
        gObjectAlphaMaskNoColorProgram.mShaderFiles.clear();
        gObjectAlphaMaskNoColorProgram.mShaderFiles.push_back(make_pair("objects/simpleNoColorV.glsl", GL_VERTEX_SHADER));
        gObjectAlphaMaskNoColorProgram.mShaderFiles.push_back(make_pair("objects/simpleF.glsl", GL_FRAGMENT_SHADER));
        gObjectAlphaMaskNoColorProgram.mShaderLevel = mShaderLevel[SHADER_OBJECT];
        success = gObjectAlphaMaskNoColorProgram.createShader();
    }

    if (success)
    {
        gImpostorProgram.mName = "Impostor Shader";
        gImpostorProgram.mFeatures.hasSrgb = true;
        gImpostorProgram.mShaderFiles.clear();
        gImpostorProgram.mShaderFiles.push_back(make_pair("objects/impostorV.glsl", GL_VERTEX_SHADER));
        gImpostorProgram.mShaderFiles.push_back(make_pair("objects/impostorF.glsl", GL_FRAGMENT_SHADER));
        gImpostorProgram.mShaderLevel = mShaderLevel[SHADER_OBJECT];
        success = gImpostorProgram.createShader();
    }

    if (success)
    {
        gObjectPreviewProgram.mName = "Object Preview Shader";
        gObjectPreviewProgram.mShaderFiles.clear();
        gObjectPreviewProgram.mShaderFiles.push_back(make_pair("objects/previewV.glsl", GL_VERTEX_SHADER));
        gObjectPreviewProgram.mShaderFiles.push_back(make_pair("objects/previewF.glsl", GL_FRAGMENT_SHADER));
        gObjectPreviewProgram.mShaderLevel = mShaderLevel[SHADER_OBJECT];
        success = make_rigged_variant(gObjectPreviewProgram, gSkinnedObjectPreviewProgram);
        success = gObjectPreviewProgram.createShader();
        gObjectPreviewProgram.mFeatures.hasLighting = true;
        gSkinnedObjectPreviewProgram.mFeatures.hasLighting = true;
    }

    if (success)
    {
        gPhysicsPreviewProgram.mName = "Preview Physics Shader";
        gPhysicsPreviewProgram.mFeatures.calculatesLighting = false;
        gPhysicsPreviewProgram.mFeatures.calculatesAtmospherics = false;
        gPhysicsPreviewProgram.mFeatures.hasGamma = false;
        gPhysicsPreviewProgram.mFeatures.hasAtmospherics = false;
        gPhysicsPreviewProgram.mFeatures.hasLighting = false;
        gPhysicsPreviewProgram.mShaderFiles.clear();
        gPhysicsPreviewProgram.mShaderFiles.push_back(make_pair("objects/previewPhysicsV.glsl", GL_VERTEX_SHADER));
        gPhysicsPreviewProgram.mShaderFiles.push_back(make_pair("objects/previewPhysicsF.glsl", GL_FRAGMENT_SHADER));
        gPhysicsPreviewProgram.mShaderLevel = mShaderLevel[SHADER_OBJECT];
        success = gPhysicsPreviewProgram.createShader();
        gPhysicsPreviewProgram.mFeatures.hasLighting = false;
    }

    if (!success)
    {
        mShaderLevel[SHADER_OBJECT] = 0;
        return false;
    }

    return true;
}

bool LLViewerShaderMgr::loadShadersAvatar()
{
#if 1 // DEPRECATED -- forward rendering is deprecated
    bool success = true;

    if (mShaderLevel[SHADER_AVATAR] == 0)
    {
        gAvatarProgram.unload();
        gAvatarEyeballProgram.unload();
        return true;
    }

    if (success)
    {
        gAvatarProgram.mName = "Avatar Shader";
        gAvatarProgram.mFeatures.hasSkinning = true;
        gAvatarProgram.mFeatures.calculatesAtmospherics = true;
        gAvatarProgram.mFeatures.calculatesLighting = true;
        gAvatarProgram.mFeatures.hasGamma = true;
        gAvatarProgram.mFeatures.hasAtmospherics = true;
        gAvatarProgram.mFeatures.hasLighting = true;
        gAvatarProgram.mFeatures.hasAlphaMask = true;
        gAvatarProgram.mShaderFiles.clear();
        gAvatarProgram.mShaderFiles.push_back(make_pair("avatar/avatarV.glsl", GL_VERTEX_SHADER));
        gAvatarProgram.mShaderFiles.push_back(make_pair("avatar/avatarF.glsl", GL_FRAGMENT_SHADER));
        gAvatarProgram.mShaderLevel = mShaderLevel[SHADER_AVATAR];
        success = gAvatarProgram.createShader();

        /// Keep track of avatar levels
        if (gAvatarProgram.mShaderLevel != mShaderLevel[SHADER_AVATAR])
        {
            mMaxAvatarShaderLevel = mShaderLevel[SHADER_AVATAR] = gAvatarProgram.mShaderLevel;
        }
    }

    if (success)
    {
        gAvatarEyeballProgram.mName = "Avatar Eyeball Program";
        gAvatarEyeballProgram.mFeatures.calculatesLighting = true;
        gAvatarEyeballProgram.mFeatures.isSpecular = true;
        gAvatarEyeballProgram.mFeatures.calculatesAtmospherics = true;
        gAvatarEyeballProgram.mFeatures.hasGamma = true;
        gAvatarEyeballProgram.mFeatures.hasAtmospherics = true;
        gAvatarEyeballProgram.mFeatures.hasLighting = true;
        gAvatarEyeballProgram.mFeatures.hasAlphaMask = true;
        gAvatarEyeballProgram.mShaderFiles.clear();
        gAvatarEyeballProgram.mShaderFiles.push_back(make_pair("avatar/eyeballV.glsl", GL_VERTEX_SHADER));
        gAvatarEyeballProgram.mShaderFiles.push_back(make_pair("avatar/eyeballF.glsl", GL_FRAGMENT_SHADER));
        gAvatarEyeballProgram.mShaderLevel = mShaderLevel[SHADER_AVATAR];
        success = gAvatarEyeballProgram.createShader();
    }

    if( !success )
    {
        mShaderLevel[SHADER_AVATAR] = 0;
        mMaxAvatarShaderLevel = 0;
        return false;
    }
#endif
    return true;
}

bool LLViewerShaderMgr::loadShadersInterface()
{
    bool success = true;

    if (success)
    {
        gHighlightProgram.mName = "Highlight Shader";
        gHighlightProgram.mShaderFiles.clear();
        gHighlightProgram.mShaderFiles.push_back(make_pair("interface/highlightV.glsl", GL_VERTEX_SHADER));
        gHighlightProgram.mShaderFiles.push_back(make_pair("interface/highlightF.glsl", GL_FRAGMENT_SHADER));
        gHighlightProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = make_rigged_variant(gHighlightProgram, gSkinnedHighlightProgram);
        success = success && gHighlightProgram.createShader();
    }

    if (success)
    {
        gHighlightNormalProgram.mName = "Highlight Normals Shader";
        gHighlightNormalProgram.mShaderFiles.clear();
        gHighlightNormalProgram.mShaderFiles.push_back(make_pair("interface/highlightNormV.glsl", GL_VERTEX_SHADER));
        gHighlightNormalProgram.mShaderFiles.push_back(make_pair("interface/highlightF.glsl", GL_FRAGMENT_SHADER));
        gHighlightNormalProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gHighlightNormalProgram.createShader();
    }

    if (success)
    {
        gHighlightSpecularProgram.mName = "Highlight Spec Shader";
        gHighlightSpecularProgram.mShaderFiles.clear();
        gHighlightSpecularProgram.mShaderFiles.push_back(make_pair("interface/highlightSpecV.glsl", GL_VERTEX_SHADER));
        gHighlightSpecularProgram.mShaderFiles.push_back(make_pair("interface/highlightF.glsl", GL_FRAGMENT_SHADER));
        gHighlightSpecularProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gHighlightSpecularProgram.createShader();
    }

    if (success)
    {
        gUIProgram.mName = "UI Shader";
        gUIProgram.mShaderFiles.clear();
        gUIProgram.mShaderFiles.push_back(make_pair("interface/uiV.glsl", GL_VERTEX_SHADER));
        gUIProgram.mShaderFiles.push_back(make_pair("interface/uiF.glsl", GL_FRAGMENT_SHADER));
        gUIProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gUIProgram.createShader();
    }

    if (success)
    {
        gPathfindingProgram.mName = "Pathfinding Shader";
        gPathfindingProgram.mShaderFiles.clear();
        gPathfindingProgram.mShaderFiles.push_back(make_pair("interface/pathfindingV.glsl", GL_VERTEX_SHADER));
        gPathfindingProgram.mShaderFiles.push_back(make_pair("interface/pathfindingF.glsl", GL_FRAGMENT_SHADER));
        gPathfindingProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gPathfindingProgram.createShader();
    }

    if (success)
    {
        gPathfindingNoNormalsProgram.mName = "PathfindingNoNormals Shader";
        gPathfindingNoNormalsProgram.mShaderFiles.clear();
        gPathfindingNoNormalsProgram.mShaderFiles.push_back(make_pair("interface/pathfindingNoNormalV.glsl", GL_VERTEX_SHADER));
        gPathfindingNoNormalsProgram.mShaderFiles.push_back(make_pair("interface/pathfindingF.glsl", GL_FRAGMENT_SHADER));
        gPathfindingNoNormalsProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gPathfindingNoNormalsProgram.createShader();
    }

    if (success)
    {
        gGlowCombineProgram.mName = "Glow Combine Shader";
        gGlowCombineProgram.mShaderFiles.clear();
        gGlowCombineProgram.mShaderFiles.push_back(make_pair("interface/glowcombineV.glsl", GL_VERTEX_SHADER));
        gGlowCombineProgram.mShaderFiles.push_back(make_pair("interface/glowcombineF.glsl", GL_FRAGMENT_SHADER));
        gGlowCombineProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gGlowCombineProgram.createShader();
        if (success)
        {
            gGlowCombineProgram.bind();
            gGlowCombineProgram.uniform1i(sGlowMap, 0);
            gGlowCombineProgram.uniform1i(sScreenMap, 1);
            gGlowCombineProgram.unbind();
        }
    }

    if (success)
    {
        gGlowCombineFXAAProgram.mName = "Glow CombineFXAA Shader";
        gGlowCombineFXAAProgram.mShaderFiles.clear();
        gGlowCombineFXAAProgram.mShaderFiles.push_back(make_pair("interface/glowcombineFXAAV.glsl", GL_VERTEX_SHADER));
        gGlowCombineFXAAProgram.mShaderFiles.push_back(make_pair("interface/glowcombineFXAAF.glsl", GL_FRAGMENT_SHADER));
        gGlowCombineFXAAProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gGlowCombineFXAAProgram.createShader();
        if (success)
        {
            gGlowCombineFXAAProgram.bind();
            gGlowCombineFXAAProgram.uniform1i(sGlowMap, 0);
            gGlowCombineFXAAProgram.uniform1i(sScreenMap, 1);
            gGlowCombineFXAAProgram.unbind();
        }
    }

#ifdef LL_WINDOWS
    if (success)
    {
        gTwoTextureCompareProgram.mName = "Two Texture Compare Shader";
        gTwoTextureCompareProgram.mShaderFiles.clear();
        gTwoTextureCompareProgram.mShaderFiles.push_back(make_pair("interface/twotexturecompareV.glsl", GL_VERTEX_SHADER));
        gTwoTextureCompareProgram.mShaderFiles.push_back(make_pair("interface/twotexturecompareF.glsl", GL_FRAGMENT_SHADER));
        gTwoTextureCompareProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gTwoTextureCompareProgram.createShader();
        if (success)
        {
            gTwoTextureCompareProgram.bind();
            gTwoTextureCompareProgram.uniform1i(sTex0, 0);
            gTwoTextureCompareProgram.uniform1i(sTex1, 1);
            gTwoTextureCompareProgram.uniform1i(sDitherTex, 2);
        }
    }

    if (success)
    {
        gOneTextureFilterProgram.mName = "One Texture Filter Shader";
        gOneTextureFilterProgram.mShaderFiles.clear();
        gOneTextureFilterProgram.mShaderFiles.push_back(make_pair("interface/onetexturefilterV.glsl", GL_VERTEX_SHADER));
        gOneTextureFilterProgram.mShaderFiles.push_back(make_pair("interface/onetexturefilterF.glsl", GL_FRAGMENT_SHADER));
        gOneTextureFilterProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gOneTextureFilterProgram.createShader();
        if (success)
        {
            gOneTextureFilterProgram.bind();
            gOneTextureFilterProgram.uniform1i(sTex0, 0);
        }
    }
#endif

    if (success)
    {
        gSolidColorProgram.mName = "Solid Color Shader";
        gSolidColorProgram.mShaderFiles.clear();
        gSolidColorProgram.mShaderFiles.push_back(make_pair("interface/solidcolorV.glsl", GL_VERTEX_SHADER));
        gSolidColorProgram.mShaderFiles.push_back(make_pair("interface/solidcolorF.glsl", GL_FRAGMENT_SHADER));
        gSolidColorProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gSolidColorProgram.createShader();
        if (success)
        {
            gSolidColorProgram.bind();
            gSolidColorProgram.uniform1i(sTex0, 0);
            gSolidColorProgram.unbind();
        }
    }

    if (success)
    {
        gOcclusionProgram.mName = "Occlusion Shader";
        gOcclusionProgram.mShaderFiles.clear();
        gOcclusionProgram.mShaderFiles.push_back(make_pair("interface/occlusionV.glsl", GL_VERTEX_SHADER));
        gOcclusionProgram.mShaderFiles.push_back(make_pair("interface/occlusionF.glsl", GL_FRAGMENT_SHADER));
        gOcclusionProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        gOcclusionProgram.mRiggedVariant = &gSkinnedOcclusionProgram;
        success = gOcclusionProgram.createShader();
    }

    if (success)
    {
        gSkinnedOcclusionProgram.mName = "Skinned Occlusion Shader";
        gSkinnedOcclusionProgram.mFeatures.hasObjectSkinning = true;
        gSkinnedOcclusionProgram.mShaderFiles.clear();
        gSkinnedOcclusionProgram.mShaderFiles.push_back(make_pair("interface/occlusionSkinnedV.glsl", GL_VERTEX_SHADER));
        gSkinnedOcclusionProgram.mShaderFiles.push_back(make_pair("interface/occlusionF.glsl", GL_FRAGMENT_SHADER));
        gSkinnedOcclusionProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gSkinnedOcclusionProgram.createShader();
    }

    if (success)
    {
        gOcclusionCubeProgram.mName = "Occlusion Cube Shader";
        gOcclusionCubeProgram.mShaderFiles.clear();
        gOcclusionCubeProgram.mShaderFiles.push_back(make_pair("interface/occlusionCubeV.glsl", GL_VERTEX_SHADER));
        gOcclusionCubeProgram.mShaderFiles.push_back(make_pair("interface/occlusionF.glsl", GL_FRAGMENT_SHADER));
        gOcclusionCubeProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gOcclusionCubeProgram.createShader();
    }

    if (success)
    {
        gDebugProgram.mName = "Debug Shader";
        gDebugProgram.mShaderFiles.clear();
        gDebugProgram.mShaderFiles.push_back(make_pair("interface/debugV.glsl", GL_VERTEX_SHADER));
        gDebugProgram.mShaderFiles.push_back(make_pair("interface/debugF.glsl", GL_FRAGMENT_SHADER));
        gDebugProgram.mRiggedVariant = &gSkinnedDebugProgram;
        gDebugProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = make_rigged_variant(gDebugProgram, gSkinnedDebugProgram);
        success = success && gDebugProgram.createShader();
    }

    if (success)
    {
        for (S32 variant = 0; variant < NORMAL_DEBUG_SHADER_COUNT; ++variant)
        {
            LLHLSLShader& shader = gNormalDebugProgram[variant];
            LLHLSLShader& skinned_shader = gSkinnedNormalDebugProgram[variant];
            shader.mName = "Normal Debug Shader";
            shader.mShaderFiles.clear();
            shader.mShaderFiles.push_back(make_pair("interface/normaldebugV.glsl", GL_VERTEX_SHADER));
            // *NOTE: Geometry shaders have a reputation for being slow.
            // Consider using compute shaders instead, which have a reputation
            // for being fast. This geometry shader in particular seems to run
            // fine on my machine, but I won't vouch for this in
            // performance-critical areas.  -Cosmic,2023-09-28
            shader.mShaderFiles.push_back(make_pair("interface/normaldebugG.glsl", GL_GEOMETRY_SHADER));
            shader.mShaderFiles.push_back(make_pair("interface/normaldebugF.glsl", GL_FRAGMENT_SHADER));
            shader.mRiggedVariant = &skinned_shader;
            shader.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
            if (variant == NORMAL_DEBUG_SHADER_WITH_TANGENTS)
            {
                shader.addPermutation("HAS_ATTRIBUTE_TANGENT", "1");
            }
            success = make_rigged_variant(shader, skinned_shader);
            success = success && shader.createShader();
        }
    }

    if (success)
    {
        gClipProgram.mName = "Clip Shader";
        gClipProgram.mShaderFiles.clear();
        gClipProgram.mShaderFiles.push_back(make_pair("interface/clipV.glsl", GL_VERTEX_SHADER));
        gClipProgram.mShaderFiles.push_back(make_pair("interface/clipF.glsl", GL_FRAGMENT_SHADER));
        gClipProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gClipProgram.createShader();
    }

    if (success)
    {
        gBenchmarkProgram.mName = "Benchmark Shader";
        gBenchmarkProgram.mShaderFiles.clear();
        gBenchmarkProgram.mShaderFiles.push_back(make_pair("interface/benchmarkV.glsl", GL_VERTEX_SHADER));
        gBenchmarkProgram.mShaderFiles.push_back(make_pair("interface/benchmarkF.glsl", GL_FRAGMENT_SHADER));
        gBenchmarkProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gBenchmarkProgram.createShader();
    }

    if (success)
    {
        gReflectionProbeDisplayProgram.mName = "Reflection Probe Display Shader";
        gReflectionProbeDisplayProgram.mFeatures.hasReflectionProbes = true;
        gReflectionProbeDisplayProgram.mFeatures.hasSrgb = true;
        gReflectionProbeDisplayProgram.mFeatures.calculatesAtmospherics = true;
        gReflectionProbeDisplayProgram.mFeatures.hasAtmospherics = true;
        gReflectionProbeDisplayProgram.mFeatures.hasGamma = true;
        gReflectionProbeDisplayProgram.mFeatures.isDeferred = true;
        gReflectionProbeDisplayProgram.mShaderFiles.clear();
        gReflectionProbeDisplayProgram.mShaderFiles.push_back(make_pair("interface/reflectionprobeV.glsl", GL_VERTEX_SHADER));
        gReflectionProbeDisplayProgram.mShaderFiles.push_back(make_pair("interface/reflectionprobeF.glsl", GL_FRAGMENT_SHADER));
        gReflectionProbeDisplayProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gReflectionProbeDisplayProgram.createShader();
    }

    if (success)
    {
        gCopyProgram.mName = "Copy Shader";
        gCopyProgram.mShaderFiles.clear();
        gCopyProgram.mShaderFiles.push_back(make_pair("interface/copyV.glsl", GL_VERTEX_SHADER));
        gCopyProgram.mShaderFiles.push_back(make_pair("interface/copyF.glsl", GL_FRAGMENT_SHADER));
        gCopyProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gCopyProgram.createShader();
    }

    if (success)
    {
        gCopyDepthProgram.mName = "Copy Depth Shader";
        gCopyDepthProgram.mShaderFiles.clear();
        gCopyDepthProgram.mShaderFiles.push_back(make_pair("interface/copyV.glsl", GL_VERTEX_SHADER));
        gCopyDepthProgram.mShaderFiles.push_back(make_pair("interface/copyF.glsl", GL_FRAGMENT_SHADER));
        gCopyDepthProgram.clearPermutations();
        gCopyDepthProgram.addPermutation("COPY_DEPTH", "1");
        gCopyDepthProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gCopyDepthProgram.createShader();
    }

    if (success)
    {
        gDrawColorProgram.mName = "Draw Color Shader";
        gDrawColorProgram.mShaderFiles.clear();
        gDrawColorProgram.mShaderFiles.push_back(make_pair("objects/simpleNoAtmosV.glsl", GL_VERTEX_SHADER));
        gDrawColorProgram.mShaderFiles.push_back(make_pair("objects/simpleColorF.glsl", GL_FRAGMENT_SHADER));
        gDrawColorProgram.clearPermutations();
        gDrawColorProgram.mShaderLevel = mShaderLevel[SHADER_OBJECT];
        success = gDrawColorProgram.createShader();
    }

    if (gSavedSettings.getBOOL("LocalTerrainPaintEnabled"))
    {
        if (success)
        {
            LLHLSLShader* shader = &gPBRTerrainBakeProgram;
            U32 bit_depth = gSavedSettings.getU32("TerrainPaintBitDepth");
            // LLTerrainPaintMap currently uses an RGB8 texture internally
            bit_depth = llclamp(bit_depth, 1, 8);
            shader->mName = llformat("Terrain Bake Shader RGB%o", bit_depth);
            shader->mFeatures.isPBRTerrain = true;

            shader->mShaderFiles.clear();
            shader->mShaderFiles.push_back(make_pair("interface/pbrTerrainBakeV.glsl", GL_VERTEX_SHADER));
            shader->mShaderFiles.push_back(make_pair("interface/pbrTerrainBakeF.glsl", GL_FRAGMENT_SHADER));
            shader->mShaderLevel = mShaderLevel[SHADER_INTERFACE];
            const U32 value_range = (1 << bit_depth) - 1;
            shader->addPermutation("TERRAIN_PAINT_PRECISION", llformat("%d", value_range));
            success = success && shader->createShader();
            //llassert(success);
            if (!success)
            {
                LL_WARNS() << "Failed to create shader '" << shader->mName << "', disabling!" << LL_ENDL;
                gSavedSettings.setBOOL("RenderCanUseTerrainBakeShaders", false);
                // continue as if this shader never happened
                success = true;
            }
        }
    }

    if (success)
    {
        gAlphaMaskProgram.mName = "Alpha Mask Shader";
        gAlphaMaskProgram.mShaderFiles.clear();
        gAlphaMaskProgram.mShaderFiles.push_back(make_pair("interface/alphamaskV.glsl", GL_VERTEX_SHADER));
        gAlphaMaskProgram.mShaderFiles.push_back(make_pair("interface/alphamaskF.glsl", GL_FRAGMENT_SHADER));
        gAlphaMaskProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gAlphaMaskProgram.createShader();
    }

    if (success)
    {
        gReflectionMipProgram.mName = "Reflection Mip Shader";
        gReflectionMipProgram.mFeatures.isDeferred = true;
        gReflectionMipProgram.mFeatures.hasGamma = true;
        gReflectionMipProgram.mFeatures.hasAtmospherics = true;
        gReflectionMipProgram.mFeatures.calculatesAtmospherics = true;
        gReflectionMipProgram.mShaderFiles.clear();
        gReflectionMipProgram.mShaderFiles.push_back(make_pair("interface/splattexturerectV.glsl", GL_VERTEX_SHADER));
        gReflectionMipProgram.mShaderFiles.push_back(make_pair("interface/reflectionmipF.glsl", GL_FRAGMENT_SHADER));
        gReflectionMipProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gReflectionMipProgram.createShader();
    }

    if (success)
    {
        // S24 (2026-08-09, task #147 step 1): was "Reflection Mip Shader" -
        // an exact copy-paste duplicate of gReflectionMipProgram.mName just
        // above, making crash-log shader names ambiguous (the one time
        // reflection-probe capture crashed, the D3D11 debug layer named
        // "Reflection Mip Shader" as the culprit with no way to tell which
        // of these two distinct programs actually caused it). Renamed to be
        // unique so future logs are unambiguous.
        gGaussianProgram.mName = "Gaussian Blur Shader";
        gGaussianProgram.mFeatures.isDeferred = true;
        gGaussianProgram.mFeatures.hasGamma = true;
        gGaussianProgram.mFeatures.hasAtmospherics = true;
        gGaussianProgram.mFeatures.calculatesAtmospherics = true;
        gGaussianProgram.mShaderFiles.clear();
        gGaussianProgram.mShaderFiles.push_back(make_pair("interface/splattexturerectV.glsl", GL_VERTEX_SHADER));
        gGaussianProgram.mShaderFiles.push_back(make_pair("interface/gaussianF.glsl", GL_FRAGMENT_SHADER));
        gGaussianProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gGaussianProgram.createShader();
    }

    if (success && gGLManager.mHasCubeMapArray)
    {
        gRadianceGenProgram.mName = "Radiance Gen Shader";
        gRadianceGenProgram.mShaderFiles.clear();
        gRadianceGenProgram.mShaderFiles.push_back(make_pair("interface/radianceGenV.glsl", GL_VERTEX_SHADER));
        gRadianceGenProgram.mShaderFiles.push_back(make_pair("interface/radianceGenF.glsl", GL_FRAGMENT_SHADER));
        gRadianceGenProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        gRadianceGenProgram.addPermutation("PROBE_FILTER_SAMPLES", "32");
        success = gRadianceGenProgram.createShader();
    }

    if (success && gGLManager.mHasCubeMapArray)
    {
        gHeroRadianceGenProgram.mName = "Hero Radiance Gen Shader";
        gHeroRadianceGenProgram.mShaderFiles.clear();
        gHeroRadianceGenProgram.mShaderFiles.push_back(make_pair("interface/radianceGenV.glsl", GL_VERTEX_SHADER));
        gHeroRadianceGenProgram.mShaderFiles.push_back(make_pair("interface/radianceGenF.glsl", GL_FRAGMENT_SHADER));
        gHeroRadianceGenProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        gHeroRadianceGenProgram.addPermutation("HERO_PROBES", "1");
        gHeroRadianceGenProgram.addPermutation("PROBE_FILTER_SAMPLES", "4");
        success                              = gHeroRadianceGenProgram.createShader();
    }

    if (success && gGLManager.mHasCubeMapArray)
    {
        gIrradianceGenProgram.mName = "Irradiance Gen Shader";
        gIrradianceGenProgram.mShaderFiles.clear();
        gIrradianceGenProgram.mShaderFiles.push_back(make_pair("interface/irradianceGenV.glsl", GL_VERTEX_SHADER));
        gIrradianceGenProgram.mShaderFiles.push_back(make_pair("interface/irradianceGenF.glsl", GL_FRAGMENT_SHADER));
        gIrradianceGenProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gIrradianceGenProgram.createShader();
    }

    if( !success )
    {
        mShaderLevel[SHADER_INTERFACE] = 0;
        return false;
    }

    return true;
}


std::string LLViewerShaderMgr::getShaderDirPrefix(void)
{
    return gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, "shaders", "class");
}

void LLViewerShaderMgr::updateShaderUniforms(LLHLSLShader * shader)
{
    LLEnvironment::instance().updateShaderUniforms(shader);
}

LLViewerShaderMgr::shader_iter LLViewerShaderMgr::beginShaders() const
{
    return mShaderList.begin();
}

LLViewerShaderMgr::shader_iter LLViewerShaderMgr::endShaders() const
{
    return mShaderList.end();
}

