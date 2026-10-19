/**
 * @file dxpipeline.cpp
 * @brief Fresh DX11-native implementation of LLPipeline's deferred
 * render-loop drawing logic.
 *
 * Copyright (c) 2025 Kirstenlee Cinquetti (Lee Quick)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

// S24: must stay first - precompiled-header convention.
#include "llviewerprecompiledheaders.h"

#include "dxpipeline.h"

// DX_RENDER pipeline / drawpool family
#include "dxdrawpool.h"
#include "dxdrawpoolalpha.h"
#include "dxdrawpoolavatar.h"
#include "dxdrawpoolbump.h" // gBumpImageList - createGLBuffers()/releaseGLBuffers()
#include "dxdrawpoolpbropaque.h"
#include "pipeline.h"

// D3D11 wrapper layer
#include "DXContext.h" // gDXContext - renderDoF()
#include "DXCubeMap.h" // full DXCubeMap type (llvosky.h only forward-declares it)
#include "DXOcclusionQuery.h" // DXOcclusionQuery::deleteQueries() - destroyGL()

// SMAA sample-map data
#include "SMAAAreaTex.h" // areaTexBytes/AREATEX_WIDTH/AREATEX_HEIGHT - createGLBuffers()
#include "SMAASearchTex.h" // searchTexBytes/SEARCHTEX_WIDTH/SEARCHTEX_HEIGHT - createGLBuffers()

// Core llrender types
#include "lldrawable.h"
#include "llhlslshader.h"
#include "llrendertarget.h"
#include "llspatialpartition.h"
#include "llvertexbuffer.h"
#include "llviewershadermgr.h"

// Viewer/application headers
#include "gltfscenemanager.h" // LL::GLTFSceneManager - renderGLTFObjects()
#include "krlvcamera.h" // KRLV_TOUCHPOINT: @camdrawmin/@camdrawmax/@camdrawalphamin/@camdrawalphamax/@camdrawcolor
#include "kveffects.h"
#include "lllightconstants.h" // LIGHT_FADE_TIME - calcNearbyLights()/setupHWLights()
#include "llagentcamera.h" // gAgentCamera - generateSunShadow()
#include "llappviewer.h"
#include "llaudioengine.h" // gAudiop/LLAudioSource - postSort()'s sound-beacon walk
#include "llenvironment.h"
#include "llfeaturemanager.h" // LLFeatureManager - refreshCachedSettings()
#include "llfloaterpathfindingcharacters.h" // LLFloaterPathfindingCharacters - renderDebug()
#include "llfloaterpathfindingconsole.h" // LLFloaterPathfindingConsole - renderDebug()
#include "llfloaterreg.h" // LLFloaterReg::instanceVisible("beacons") - postSort()
#include "llfloatertelehub.h" // LLFloaterTelehub - postSort()'s telehub beacon draw
#include "llfloatertools.h"
#include "llfontvertexbuffer.h" // LLFontVertexBuffer/LLFontWidthBuffer - refreshCachedSettings()/init()
#include "llhudnametag.h" // LLHUDNameTag::shiftAll() - shiftObjects()
#include "llhudtext.h" // LLHUDText::shiftAll() - shiftObjects()
#include "llimagepng.h" // LLImagePNG - createGLBuffers()'s SMAA sample-map load
#include "llmeshrepository.h" // gMeshRepo - rebuildPriorityGroups()
#include "llpanelface.h" // gFloaterTools->getPanelFace()->getTextureChannelToEdit() - postSort()
#include "llpathfindingpathtool.h" // LLPathfindingPathTool - renderDebug()
#include "llpathinglib.h" // LLPathingLib - renderDebug()'s navmesh/pathfinding visualization
#include "llpipelinelistener.h" // LLPipelineListener - the EventHost API listener object
#include "llprimitive.h" // LIGHT_MAX_RADIUS - calcNearbyLights()
#include "llselectmgr.h" // LLSelectMgr/LLSelectedTEFunctor - stateSort()/postSort() selection checks
#include "llsky.h" // gSky - legacy env-map fallback in bindDeferredShader()/unbindDeferredShader()
#include "lltoolmgr.h"
#include "lltracker.h" // LLTracker::render3D() - render_hud_elements()
#include "llui.h" // LLUIImage::enableDisplayListsCollection() - refreshCachedSettings()/init()
#include "llviewercamera.h"
#include "llviewercontrol.h"
#include "llviewerdisplay.h" // gDepthDirty - shiftObjects()
#include "llviewerjoystick.h" // LLViewerJoystick - renderDoF()
#include "llviewermediafocus.h" // LLViewerMediaFocus - renderDoF()
#include "llviewerobjectlist.h" // gObjectList - hidePermanentObjects()/hideObject()/restoreHiddenObject()
#include "llviewerparcelmgr.h" // LLViewerParcelMgr - render_hud_elements()
#include "llviewerregion.h" // LLViewerRegion - rebuildDrawInfo()/rebuildTerrain()
#include "llviewertexture.h" // KRLV_TOUCHPOINT-adjacent: LLViewerTextureManager::getFetchedTexture(), @camtextures
#include "llviewerwindow.h"
#include "llvoavatarself.h" // isAgentAvatarValid()/gAgentAvatarp - generateSunShadow()
#include "llvocache.h" // LLVOCachePartition - updateMove()/doOcclusion()
#include "llvosky.h" // LLVOSky::getCubeMap()
#include "llvovolume.h"
#include "llvowlsky.h" // LLVOWLSky's mDrawable member - only forward-declared via llsky.h before - updateCull()
#include "llworld.h" // LLWorld - rebuildDrawInfo()/rebuildTerrain()

// System
#include <unordered_map>
#include <unordered_set>

// A_CPU 1 selects CASF.glsl's CPU-side (not HLSL-side) CasSetup()/varAU4() macros, used by
// applyCAS() below. Wrapped in an anonymous namespace: CASF.glsl's C++ functions (CasSetup(),
// CasSupportScaling(), etc.) have plain external linkage, so including this header unwrapped
// risks an ODR violation if anything else in the link ever includes it too - the anonymous
// namespace gives every symbol in it internal linkage to this translation unit, a cheap
// insurance policy that costs nothing (unqualified calls below still resolve normally).
namespace
{
#define A_CPU 1
#include "app_settings/shaders/class1/deferred/CASF.glsl" // This is also C++
}

LLTrace::BlockTimerStatHandle FTM_DEFERRED_POOLRENDER("RenderPool (Deferred)");
LLTrace::BlockTimerStatHandle FTM_POST_DEFERRED_POOLRENDER("RenderPool (Post)");
LLTrace::BlockTimerStatHandle FTM_RENDER_DEFERRED("Deferred Shading");
LLTrace::BlockTimerStatHandle FTM_GEO_UPDATE("Geo Update");
LLTrace::BlockTimerStatHandle FTM_UPDATE_MOVE("Update Move", "Pipeline");
LLTrace::BlockTimerStatHandle FTM_STATESORT("Sort Draw State");
LLTrace::BlockTimerStatHandle FTM_POST_SORT("Post Sort", "Pipeline");
LLTrace::BlockTimerStatHandle FTM_STATESORT_DRAWABLE("Sort Drawables");
LLTrace::BlockTimerStatHandle FTM_CULL("Object Culling");

// Defined in llviewerwindow.cpp - picks the no-post-processing shader variant during a snapshot
// (presentDeferredScreen()'s inline tonemap selection).
extern bool gSnapshotNoPost;

// Set during reflection-probe cube-face capture - renderDeferredLighting()'s sun-shadow/SSAO
// lightmap pass picks gDeferredSunProbeProgram while this is true.
extern bool gCubeSnapshot;

// Set by LLEquirectCapture for its whole capture loop - see llviewerdisplay.cpp's definition
// comment.
extern bool gEquirectCapture;

// Defined in llviewerdisplay.cpp, set right before both presentFinal() call sites - rawSnapshot()
// (llviewerwindow.cpp) reads the true final composited render target directly through this.
extern LLRenderTarget* gLastCompositedPostTarget;

int effectsMask = 0; // consumed by presentDeferredScreen(), populated by updateEffectMask()
// llviewerdisplay.cpp's own global - render_hud_elements()'s parcel-snapshot-only land-fence
// override needs this too.
extern bool gForceRenderLandFence;

void updateEffectMask()
{
    // Uses LLCachedControl<bool> rather than raw gSavedSettings.getBOOL() hash-map lookups every call -
    // matches the pattern elsewhere in this file (presentDeferredScreen()/renderDoF()/applyFXAA()),
    // which only re-query on an actual settings-change notification.
    static LLCachedControl<bool> UseDesaturation(gSavedSettings, "UseDesaturation", false);
    static LLCachedControl<bool> UseInvert(gSavedSettings, "UseInvert", false);
    static LLCachedControl<bool> UseCelShading(gSavedSettings, "UseCelShade", false);
    static LLCachedControl<bool> applyVignette(gSavedSettings, "UseVignette", false);
    static LLCachedControl<bool> applyNightVision(gSavedSettings, "UseNightVision", false);
    static LLCachedControl<bool> applyEdgeGlow(gSavedSettings, "UseEdgeGlow", false);
    static LLCachedControl<bool> applyRGBControl(gSavedSettings, "UseRGBControl", false);
    static LLCachedControl<bool> applyMotionBlur(gSavedSettings, "UseMotionBlur", false);

    effectsMask = 0;
    effectsMask |= UseDesaturation() ? DESATURATION : 0;
    effectsMask |= UseInvert() ? INVERT : 0;
    effectsMask |= UseCelShading() ? CEL_SHADING : 0;
    effectsMask |= applyVignette() ? VIGNETTE : 0;
    effectsMask |= applyNightVision() ? NIGHT_VISION : 0;
    effectsMask |= applyEdgeGlow() ? EDGE_GLOW : 0;
    effectsMask |= applyRGBControl() ? RGB_CONTROL : 0;
    effectsMask |= applyMotionBlur() ? MOTION_BLUR : 0;
}

void render_hud_elements()
{
    LL_RECORD_BLOCK_TIME(FTM_RENDER_UI);
    DXPipeline::disableLights(gPipeline);

    DXUIDefault gls_ui;

    gUIProgram.bind();
    gDX.color4f(1, 1, 1, 1);
    DXDepthTest depth(true, false);

    if (!DXPipeline::sReflectionRender && gPipeline.hasRenderDebugFeatureMask(LLPipeline::RENDER_DEBUG_FEATURE_UI))
    {
        gViewerWindow->renderSelections(false, false, false); // For HUD version in render_ui_3d()

        // Draw the tracking overlays
        LLTracker::render3D();

        if (LLWorld::instanceExists())
        {
            // Show the property lines
            LLWorld::getInstance()->renderPropertyLines();
        }
        LLViewerParcelMgr::getInstance()->render();
        LLViewerParcelMgr::getInstance()->renderParcelCollision();
    }
    else if (gForceRenderLandFence)
    {
        // This is only set when not rendering the UI, for parcel snapshots
        LLViewerParcelMgr::getInstance()->render();
    }
    else if (DXPipeline::hasRenderType(gPipeline, LLPipeline::RENDER_TYPE_HUD))
    {
        LLHUDText::renderAllHUD();
    }

    gUIProgram.unbind();
}

// setupAvatarLights() needs both of these.
const F32 BACKLIGHT_DAY_MAGNITUDE_OBJECT = 0.1f;
const F32 BACKLIGHT_NIGHT_MAGNITUDE_OBJECT = 0.08f;
// *** DO NOT TOUCH - DO NOT MERGE THESE TWO BACK INTO ONE CONSTANT ***
// General/world alpha-blend shadow-casting hard cutoff (shadowAlphaMaskF.hlsl/
// pbrShadowAlphaBlendF.hlsl's `minimum_alpha` uniform, gating BEFORE their own stacked
// bayerDitherDiscard(alpha, 0.4, ...)). Genuinely translucent world content (glass, windows,
// tinted materials) needs this HIGH - real alpha gradients in that 0.1-0.4 range are common
// there, and dropping below 0.4 makes them always survive the dither gate too, i.e. cast a
// full solid shadow regardless of how see-through they actually look. A single shared constant
// low enough to fix wispy rigged hair shadow breaks shadow casting for every piece of
// translucent world geometry instead. Tune ALPHA_BLEND_CUTOFF_RIGGED below instead for anything
// hair/attachment-related.
const F32 ALPHA_BLEND_CUTOFF = 0.598f;
// Rigged-attachment-only alpha-blend cutoff, deliberately lower than the general
// ALPHA_BLEND_CUTOFF above - only used for renderAlphaObjects()'s rigged=true branches
// (hair/attachments) - never for general world geometry. See the warning above.
const F32 ALPHA_BLEND_CUTOFF_RIGGED = 0.1f;
const F32 DEFERRED_LIGHT_FALLOFF = 0.5f;
const U32 DEFERRED_VB_MASK = LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_TEXCOORD0 | LLVertexBuffer::MAP_TEXCOORD1;
bool gAvatarBacklight = false;
bool gShiftFrame = false;
const LLMatrix4* gDXLastMatrix = NULL;
// This TLS slot is shared by the drawable/object lifecycle Batch 2 functions (markMoved()/
// markGLRebuild()/markPartitionMove()/markMeshDirty()/markRebuild()/
// setDeferredMarksForThisThread()) and updateMoveDampedAsync()/updateMoveNormalAsync() - all
// already live in this file.
thread_local LLDeferredPipelineMarks* sTLSDeferredPipelineMarks = nullptr;
// This per-frame cull result is shared by DXPipeline::beginRenderMap()/endRenderMap()/
// beginAlphaGroups()/endAlphaGroups()/beginRiggedAlphaGroups()/endRiggedAlphaGroups()/
// grabReferences()/clearReferences() (already live in this file), and sIndicesDrawnCount by
// addTrianglesDrawn()/recordTrianglesDrawn() (same).
LLCullResult* sCull = NULL;
U32 sIndicesDrawnCount = 0;
extern U32 nhpo2(U32 v);
// Same convention - DXPipeline::doOcclusion() needs this for its occlusion-query cube VB.
LLVertexBuffer* ll_create_cube_vb(U32 type_mask);
// Same convention - DXPipeline::renderDebug() needs these for its occlusion/shadow-frusta
// debug-visualization boxes.
void drawBox(const LLVector4a& c, const LLVector4a& r);
void drawBoxOutline(const LLVector3& pos, const LLVector3& size);
// Defined in llviewerdisplay.cpp, no header declaration elsewhere - shiftObjects() needs this.
void display_update_camera();
// Defined in llspatialpartition.cpp, no header declaration elsewhere - renderDebug() needs this.
extern std::set<LLSpatialGroup*> visible_selected_groups;

// Adds color attachments for deferred rendering - used by generateImpostor()/
// allocateScreenBufferInternal().
bool addDeferredAttachments(LLRenderTarget& target, bool for_impostor = false)
{
    U32 orm = GL_RGBA;
    U32 norm = GL_RGBA16;
    U32 emissive = GL_RGB16F;

    static LLCachedControl<bool> has_emissive(gSavedSettings, "RenderEnableEmissiveBuffer", false);
    static LLCachedControl<bool> has_hdr(gSavedSettings, "RenderHDREnabled", true);
    bool hdr = has_hdr();

    if (!hdr)
    {
        norm = GL_RGB10_A2;
        emissive = GL_RGB;
    }

    bool valid = true;
    valid = valid && target.addColorAttachment(orm);    // frag-data[1] specular OR PBR ORM
    valid = valid && target.addColorAttachment(norm);
    if (has_emissive)                                // Third attachment (Emissive)
    {
        valid = valid && target.addColorAttachment(emissive);
    }

    return valid;
}

#define IR(x)   ((U32&)x)
bool LLRayAABB(const LLVector3& center, const LLVector3& size, const LLVector3& origin, const LLVector3& dir, LLVector3& coord, F32 epsilon)
{
    bool Inside = true;
    LLVector3 MinB = center - size;
    LLVector3 MaxB = center + size;
    LLVector3 MaxT;
    MaxT.mV[VX] = MaxT.mV[VY] = MaxT.mV[VZ] = -1.0f;

    // Find candidate planes.
    for (U32 i = 0; i < 3; i++)
    {
        if (origin.mV[i] < MinB.mV[i])
        {
            coord.mV[i] = MinB.mV[i];
            Inside = false;

            // Calculate T distances to candidate planes
            if (IR(dir.mV[i]))   MaxT.mV[i] = (MinB.mV[i] - origin.mV[i]) / dir.mV[i];
        }
        else if (origin.mV[i] > MaxB.mV[i])
        {
            coord.mV[i] = MaxB.mV[i];
            Inside = false;

            // Calculate T distances to candidate planes
            if (IR(dir.mV[i]))   MaxT.mV[i] = (MaxB.mV[i] - origin.mV[i]) / dir.mV[i];
        }
    }

    // Ray origin inside bounding box
    if (Inside)
    {
        coord = origin;
        return true;
    }

    // Get largest of the maxT's for final choice of intersection
    U32 WhichPlane = 0;
    if (MaxT.mV[1] > MaxT.mV[WhichPlane])    WhichPlane = 1;
    if (MaxT.mV[2] > MaxT.mV[WhichPlane])    WhichPlane = 2;

    // Check final candidate actually inside box
    if (IR(MaxT.mV[WhichPlane]) & 0x80000000) return false;

    for (U32 i = 0; i < 3; i++)
    {
        if (i != WhichPlane)
        {
            coord.mV[i] = origin.mV[i] + MaxT.mV[WhichPlane] * dir.mV[i];
            if (epsilon > 0)
            {
                if (coord.mV[i] < MinB.mV[i] - epsilon || coord.mV[i] > MaxB.mV[i] + epsilon)    return false;
            }
            else
            {
                if (coord.mV[i] < MinB.mV[i] || coord.mV[i] > MaxB.mV[i])    return false;
            }
        }
    }
    return true;    // ray hits box
}

// Cached-settings storage - populated by DXPipeline::refreshCachedSettings(), declared in
// dxpipeline.h.
S32     DXPipeline::sUseOcclusion = 0;
bool    DXPipeline::sRenderingHUDs;
bool    DXPipeline::sUnderWaterRender = false;
bool DXPipeline::RenderScreenSpaceReflections;
bool DXPipeline::RenderDepthOfField;
U32 DXPipeline::RenderFSAAType;
bool DXPipeline::RenderDepthOfFieldInEditMode;
S32 DXPipeline::RenderBufferVisualization;
bool DXPipeline::RenderDeferredAtmospheric;
bool DXPipeline::RenderDeferredSSAO;
S32 DXPipeline::RenderShadowDetail;
LLVector3 DXPipeline::RenderShadowGaussian;
F32 DXPipeline::RenderShadowBlurSize;
F32 DXPipeline::RenderShadowBlurDistFactor;
bool    DXPipeline::sReflectionProbesEnabled = false;
bool    DXPipeline::sRenderAttachedLights = true;
S32     DXPipeline::sVisibleLightCount = 0;
bool    DXPipeline::sRenderGlow = false;
F32 DXPipeline::RenderGlowMaxExtractAlpha;
F32 DXPipeline::RenderGlowWarmthAmount;
LLVector3 DXPipeline::RenderGlowLumWeights;
LLVector3 DXPipeline::RenderGlowWarmthWeights;
bool DXPipeline::RenderGlowNoise;
S32 DXPipeline::RenderGlowResolutionPow;
S32 DXPipeline::RenderGlowIterations;
F32 DXPipeline::RenderGlowWidth;
F32 DXPipeline::RenderGlowStrength;
F32 DXPipeline::RenderDeferredSunWash;
F32 DXPipeline::RenderShadowNoise;
F32 DXPipeline::RenderSSAOScale;
U32 DXPipeline::RenderSSAOMaxScale;
F32 DXPipeline::RenderSSAOFactor;
LLVector3 DXPipeline::RenderSSAOEffect;
F32 DXPipeline::RenderShadowBiasError;
F32 DXPipeline::RenderShadowBias;
F32 DXPipeline::RenderShadowOffset;
F32 DXPipeline::RenderSpotShadowOffset;
F32 DXPipeline::RenderSpotShadowBias;
F32 DXPipeline::RenderEdgeDepthCutoff;
F32 DXPipeline::RenderEdgeNormCutoff;
bool DXPipeline::RenderMirrors;
S32 DXPipeline::RenderScreenSpaceReflectionIterations;
S32 DXPipeline::RenderWaterSSRIterations;
F32 DXPipeline::RenderScreenSpaceReflectionRayStep;
F32 DXPipeline::RenderWaterSSRRayStep;
F32 DXPipeline::RenderScreenSpaceReflectionDistanceBias;
S32 DXPipeline::RenderScreenSpaceReflectionGlossySamples;
F32 DXPipeline::RenderScreenSpaceReflectionDepthRejectBias;
F32 DXPipeline::RenderScreenSpaceReflectionAdaptiveStepMultiplier;
F32 DXPipeline::RenderScreenSpaceReflectionGlossThreshold;
bool    DXPipeline::sShadowRender = false;
bool    DXPipeline::sRenderDeferred = false;
bool DXPipeline::CameraOffset;
F32 DXPipeline::RenderFarClip;
LLVector3 DXPipeline::RenderShadowSplitExponent;
S32 DXPipeline::RenderShadowSplits;
F32 DXPipeline::RenderShadowErrorCutoff;
F32 DXPipeline::RenderShadowFOVCutoff;
LLDrawable* DXPipeline::RenderSpotLight = nullptr;
bool    DXPipeline::sReflectionRender = false;
bool DXPipeline::RenderSpotLightsInNondeferred;
LLColor4 DXPipeline::PreviewAmbientColor;
LLColor4 DXPipeline::PreviewDiffuse0;
LLColor4 DXPipeline::PreviewSpecular0;
LLColor4 DXPipeline::PreviewDiffuse1;
LLColor4 DXPipeline::PreviewSpecular1;
LLColor4 DXPipeline::PreviewDiffuse2;
LLColor4 DXPipeline::PreviewSpecular2;
LLVector3 DXPipeline::PreviewDirection0;
LLVector3 DXPipeline::PreviewDirection1;
LLVector3 DXPipeline::PreviewDirection2;
bool    DXPipeline::sImpostorRender = false;
bool    DXPipeline::sImpostorRenderAlphaDepthPass = false;
bool    DXPipeline::sRenderScriptedBeacons = false;
bool    DXPipeline::sRenderScriptedTouchBeacons = true;
bool    DXPipeline::sRenderMOAPBeacons = false;
bool    DXPipeline::sRenderPhysicalBeacons = true;
bool    DXPipeline::sRenderParticleBeacons = false;
bool    DXPipeline::sRenderSoundBeacons = false;
bool    DXPipeline::sRenderBeacons = false;
bool    DXPipeline::sRenderHighlight = true;
LLRender::eTexIndex DXPipeline::sRenderHighlightTextureChannel = LLRender::DIFFUSE_MAP;
bool DXPipeline::RenderDelayCreation;
bool DXPipeline::RenderAnimateRes;
bool    DXPipeline::sAutoMaskAlphaDeferred = true;
bool    DXPipeline::sAutoMaskAlphaNonDeferred = false;
bool    DXPipeline::sUseFarClip = true;
bool DXPipeline::RenderUIBuffer;
F32 DXPipeline::RenderShadowResolutionScale;
bool DXPipeline::FreezeTime;
S32 DXPipeline::DebugBeaconLineWidth;
F32 DXPipeline::CameraFocusTransitionTime;
F32 DXPipeline::CameraFNumber;
F32 DXPipeline::CameraFocalLength;
F32 DXPipeline::CameraFieldOfView;
F32 DXPipeline::CameraMaxCoF;
F32 DXPipeline::CameraDoFResScale;
F32 DXPipeline::CameraAspectRatio;
F32 DXPipeline::RenderAutoHideSurfaceAreaLimit;
bool DXPipeline::RenderAvatarCloth;
bool    DXPipeline::sRenderTransparentWater = true;
LLViewerObject* DXPipeline::sCaptureExcludeObject = nullptr;
bool    DXPipeline::sPickAvatar = true;
S32     DXPipeline::sCompiles = 0;
bool    DXPipeline::sDynamicLOD = true;
bool    DXPipeline::sRenderAttachedParticles = true;
LLTrace::EventStatHandle<S64> DXPipeline::sStatBatchSize("renderbatchsize");
const U32 DXPipeline::MAX_PREVIEW_WIDTH = 512;
bool    DXPipeline::sShowHUDAttachments = true;
bool    DXPipeline::sForceOldBakedUpload = false;
bool    DXPipeline::sBakeSunlight = false;
bool    DXPipeline::sNoAlpha = false;
bool    DXPipeline::sTextureBindTest = false;

// The 11 FTM_* handles pipeline.h already extern-declares (unlike the 9 above, which needed
// their own local extern in this file first).
LLTrace::BlockTimerStatHandle FTM_RENDER_INVISIBLE("Invisible");
LLTrace::BlockTimerStatHandle FTM_RENDER_SHINY("Shiny");
LLTrace::BlockTimerStatHandle FTM_RENDER_TERRAIN("Terrain");
LLTrace::BlockTimerStatHandle FTM_RENDER_TREES("Trees");
LLTrace::BlockTimerStatHandle FTM_RENDER_UI("UI");
LLTrace::BlockTimerStatHandle FTM_RENDER_WL_SKY("Windlight Sky");
LLTrace::BlockTimerStatHandle FTM_RENDER_CHARACTERS("Avatars");
LLTrace::BlockTimerStatHandle FTM_RENDER_BUMP("Bump");
LLTrace::BlockTimerStatHandle FTM_RENDER_FULLBRIGHT("Fullbright");
LLTrace::BlockTimerStatHandle FTM_RENDER_UI_3D("3D");
LLTrace::BlockTimerStatHandle FTM_RENDER_UI_2D("2D");

// The global LLPipeline singleton, and the EventHost API listener that self-registers in its own
// constructor (LLEventAPI pattern).
LLPipeline gPipeline;
static LLPipelineListener sPipelineListener;

LLPipeline::LLPipeline() :
    mBackfaceCull(false),
    mMatrixOpCount(0),
    mTextureMatrixOps(0),
    mNumVisibleNodes(0),
    mNumVisibleFaces(0),
    mPoissonOffset(0),

    mInitialized(false),
    mShadersLoaded(false),
    mTransformFeedbackPrimitives(0),
    mRenderDebugFeatureMask(0),
    mRenderDebugMask(0),
    mOldRenderDebugMask(0),
    mMeshDirtyQueryObject(0),
    mGroupQ1Locked(false),
    mResetVertexBuffers(false),
    mLastRebuildPool(NULL),
    mLightMask(0),
    mLightMovingMask(0)
{
    mNoiseMap = 0;
    mTrueNoiseMap = 0;
    mLightFunc = 0;

    for (U32 i = 0; i < 8; i++)
    {
        mHWLightColors[i] = LLColor4::black;
    }
}

LLPipeline::~LLPipeline()
{}

void LLPipeline::assertInitializedDoError()
{
    LL_ERRS() << "LLPipeline used when uninitialized." << LL_ENDL;
}

#include "DXDevice.h"
#include "nvapi/nvapi.h"
#include "DXReadback.h"
#include "DXRenderTarget.h"
#include "DXStateCache.h"
#include "DXShader.h"
#include "DXSwapChain.h"
#include "DXSampler.h"

namespace
{
    // Tracks whether renderDeferredLighting() actually completed and wrote
    // real content into mRT->screen THIS frame. mRT->screen is allocated
    // unconditionally so its SRV is always non-null even when
    // renderDeferredLighting() early-returned without writing anything -
    // presentDeferredScreen() needs this flag, not an SRV-null check, to
    // avoid blitting stale content.
    bool sScreenLitThisFrame = false;

    // D3D11 has no TRIANGLE_FAN topology (llvertexbuffer.cpp's sDXMode[]
    // maps it to D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED; drawRange()/drawArrays()
    // llassert() against that). GL's local-lights box draw relies on
    // llvieweroctree.cpp's private sOcclusionIndices fan table (8
    // camera-relative "cypher" variants, each an 8-vertex fan). Expanded
    // below into the triangle-list equivalent (6 triangles/18 vertices per
    // cypher, standard fan-to-list expansion), same vertex order per cypher
    // as GL's fan so winding/culling matches. Built as a flat, non-indexed
    // vertex list, owned separately from GL's mCubeVB.
    LLPointer<LLVertexBuffer> sDXBoxLightVB;

    const U8 sBoxLightTriangleIndices[8][18] =
    {
        { 7,6,2, 7,2,3, 7,3,1, 7,1,5, 7,5,4, 7,4,6 }, // 000
        { 3,2,0, 3,0,1, 3,1,5, 3,5,7, 3,7,6, 3,6,2 }, // 001
        { 5,4,6, 5,6,7, 5,7,3, 5,3,1, 5,1,0, 5,0,4 }, // 010
        { 1,0,4, 1,4,5, 1,5,7, 1,7,3, 1,3,2, 1,2,0 }, // 011
        { 6,0,2, 6,2,3, 6,3,7, 6,7,5, 6,5,4, 6,4,0 }, // 100
        { 2,4,0, 2,0,1, 2,1,3, 2,3,7, 2,7,6, 2,6,4 }, // 101
        { 4,2,6, 4,6,7, 4,7,5, 4,5,1, 4,1,0, 4,0,2 }, // 110
        { 0,6,4, 0,4,5, 0,5,1, 0,1,3, 0,3,2, 0,2,6 }, // 111
    };

    U32 getBoxLightCypher(LLCamera* camera, const LLVector4a& center)
    {
        LLVector4a origin(0.f);
        origin.load3(camera->getOrigin().mV);
        return center.greaterThan(origin).getGatheredBits() & 0x7;
    }

    // Only pools in this list are drawn under DX_RENDER - see DXPipeline.h's
    // class comment. Everything else is silently skipped, not a bug: those
    // pools haven't had their own DX_RENDER branch written yet.
    //
    // POOL_GRASS/POOL_ALPHA_MASK (LLDrawPoolGrass/DXAlphaDrawPoolMask) got
    // a DX_RENDER branch (see dxdrawpoolsimple.cpp) and hook
    // renderDeferred() same as POOL_SIMPLE, so they fit this existing loop
    // as-is. The other 3 pools (POOL_FULLBRIGHT_ALPHA_MASK/POOL_FULLBRIGHT/
    // POOL_GLOW) hook renderPostDeferred() instead - deliberately NOT
    // whitelisted here, since renderGeomPostDeferred()'s loop (with its
    // atmospherics/water-haze/exclusion interleaving) has no DXPipeline
    // equivalent yet.
    //
    // POOL_TREE (LLDrawPoolTree) needed zero code changes - its
    // renderDeferred()/beginDeferredPass()/endDeferredPass() already route
    // entirely through already-DX-safe primitives. POOL_MATERIALS
    // (LLDrawPoolMaterials) got a real DX_RENDER branch too (see
    // dxdrawpoolmaterials.cpp) - both hook renderDeferred(), so both fit
    // this loop as-is.
    //
    // POOL_WATEREXCLUSION (LLDrawPoolWaterExclusion) also needed zero code
    // changes, but is deliberately NOT whitelisted here: its render() isn't
    // called from this loop at all - it's invoked from
    // DXPipeline::doWaterExclusionMask(), itself called from
    // renderGeomPostDeferred()'s loop, which has no DXPipeline equivalent
    // yet (same situation as the 3 post-deferred pools above).
    //
    // POOL_GLTF_PBR/POOL_GLTF_PBR_ALPHA_MASK (LLDrawPoolGLTFPBR) has a real
    // DX_RENDER branch (see dxdrawpoolpbropaque.cpp) -
    // renderDeferred()'s DX_RENDER branch calls pushGLTFBatches() (ordinary
    // PBR-materialed prims/mesh - routes through LLFetchedGLTFMaterial::
    // bind()'s shader->uniform*()/bindTexture() calls and the same
    // LLVertexBuffer::drawRange() chokepoint every other pool uses, already
    // DX-safe) but explicitly skips LL::GLTFSceneManager::instance().
    // render()/renderOpaque() - that's a SEPARATE mechanism, only used for
    // imported .glb/.gltf SCENE-FILE assets specifically, which calls
    // glBindBufferBase() (a GLEW-style fn ptr, null under DX_RENDER - calling
    // it would crash). Rigged variant skipped, same limitation as every
    // other pool (DXVertexLayout has no skinned attributes yet).
    // renderPostDeferred() (the glow pass) is NOT whitelisted here - same
    // situation as the other post-deferred pools noted above.
    //
    // POOL_BUMP (LLDrawPoolBump) got a real DX_RENDER branch (see
    // dxdrawpoolbump.cpp, a staged full duplicate like dxdrawpoolalpha) -
    // its renderDeferred() hooks the deferred pass, so it fits this loop
    // as-is (beginDeferredPass()/endDeferredPass() are unoverridden no-ops
    // from the LLDrawPool base). DXAlphaDrawPool and LLDrawPoolBump's own
    // renderPostDeferred() are NOT whitelisted - same situation as the
    // other post-deferred pools above.
    //
    // POOL_WL_SKY (LLDrawPoolWLSky) got a real DX_RENDER branch (see
    // dxdrawpoolwlsky.cpp, same staged-full-duplicate pattern) - it DOES
    // override beginDeferredPass()/endDeferredPass() (unlike POOL_BUMP),
    // both of which also got their own thin redirect, so it fits this loop
    // as-is too. POOL_SKY (LLDrawPoolSky) needed no work at all - every
    // method on that class is an empty, `// DEPRECATED` stub already, GL or
    // DX - so it's not listed here (nothing to whitelist, it draws nothing
    // regardless of backend).
    //
    // POOL_TERRAIN (LLDrawPoolTerrain) got a real DX_RENDER branch (see
    // dxdrawpoolterrain.cpp) - same shape as POOL_WL_SKY,
    // beginDeferredPass()/endDeferredPass()/renderDeferred() all redirected
    // together (endDeferredPass() unbinds a file-static shader pointer set
    // by renderDeferred(), so all three had to move together - see
    // dxdrawpoolterrain.h class comment). Fits this loop as-is.
    //
    // POOL_AVATAR (LLDrawPoolAvatar) got surgical in-place guards, not a
    // dedicated dx*.cpp full duplicate like the pools above - unlike
    // Terrain/WLSky/Alpha/Bump (which synthesize their own geometry and own
    // their render logic directly), LLDrawPoolAvatar is a thin dispatcher
    // into a *different* class (LLVOAvatar's renderSkinned()/
    // renderRigid()/renderImpostor()) whose actual GL calls are safe by
    // composition once the shared primitives (LLTexUnit::bind(),
    // LLVertexBuffer, uniform4fv's AVATAR_MATRIX handling) are fixed -
    // nothing pool-side needed a fresh DX-native reimplementation. Also,
    // its shader-hand-off statics (sRenderingSkinned etc.) are file-static,
    // not class members, so a separate dxdrawpoolavatar.cpp couldn't reach
    // them anyway. Scoped to pass 2 ("skinned", the avatar body mesh) only -
    // passes 0 (impostor) and 1 (rigid/eyeballs) are deliberately deferred,
    // same umbrella as rigged mesh-attachment skinning. Fits this loop
    // as-is since begin/endDeferredPass() now no-op for the deferred passes.
    // POOL_CONTROL_AV (Animesh) constructs the exact same LLDrawPoolAvatar
    // class (see LLDrawPool::createPool()'s shared
    // `case POOL_AVATAR: case POOL_CONTROL_AV:` branch) - the pass-keyed
    // guards above apply identically, so it's whitelisted alongside
    // POOL_AVATAR with no extra work.
    bool isConvertedPool(U32 type)
    {
        return type == LLDrawPool::POOL_SIMPLE
            || type == LLDrawPool::POOL_GRASS
            || type == LLDrawPool::POOL_ALPHA_MASK
            || type == LLDrawPool::POOL_TREE
            || type == LLDrawPool::POOL_MATERIALS
            || type == LLDrawPool::POOL_BUMP
            || type == LLDrawPool::POOL_WL_SKY
            || type == LLDrawPool::POOL_TERRAIN
            || type == LLDrawPool::POOL_AVATAR
            || type == LLDrawPool::POOL_CONTROL_AV
            || type == LLDrawPool::POOL_GLTF_PBR
            || type == LLDrawPool::POOL_GLTF_PBR_ALPHA_MASK;
    }

    // Whitelist for renderGeomPostDeferred()'s pools (the "forward"/
    // post-deferred pass: translucent/alpha content, water, glow).
    // POOL_ALPHA_PRE_WATER/POOL_ALPHA_POST_WATER -> dxdrawpoolalpha.cpp/.h.
    // POOL_FULLBRIGHT/POOL_FULLBRIGHT_ALPHA_MASK/POOL_GLOW ->
    // dxdrawpoolsimple.cpp. POOL_WATER -> dxdrawpoolwater.cpp/.h (relies on
    // LLHLSLShader::bindTexture(S32, LLRenderTarget*, ...) for
    // WATER_SCREENTEX/WATER_EXCLUSIONTEX). POOL_GLTF_PBR/
    // POOL_GLTF_PBR_ALPHA_MASK's own renderPostDeferred() (the glow
    // sub-pass) stays GL-only, not reachable here.
    bool isConvertedPostDeferredPool(U32 type)
    {
        return type == LLDrawPool::POOL_ALPHA_PRE_WATER
            || type == LLDrawPool::POOL_ALPHA_POST_WATER
            || type == LLDrawPool::POOL_WATER
            || type == LLDrawPool::POOL_FULLBRIGHT
            || type == LLDrawPool::POOL_FULLBRIGHT_ALPHA_MASK
            || type == LLDrawPool::POOL_GLOW
            // LLDrawPoolAvatar::beginPostDeferredPass()/renderPostDeferred()/
            // endPostDeferredPass() (dxdrawpoolavatar.cpp) is a complete,
            // already-safe implementation; without this entry alpha-blended
            // avatar content (transparent clothing, hair, alpha-masked
            // attachments) never renders.
            || type == LLDrawPool::POOL_AVATAR
            // DXDrawPoolBump::renderPostDeferred() (dxdrawpoolbump.cpp,
            // shiny-fullbright + emboss-bump) is a complete implementation
            // that simply wasn't whitelisted.
            || type == LLDrawPool::POOL_BUMP
            // POOL_GLTF_PBR/POOL_GLTF_PBR_ALPHA_MASK's own
            // renderPostDeferred() (dxdrawpoolpbropaque.cpp) is the PBR
            // glow/emissive sub-pass (gPBRGlowProgram, static + rigged via
            // pushRiggedGLTFBatches()) plus HUD-attached PBR content -
            // complete implementation, just wasn't whitelisted.
            || type == LLDrawPool::POOL_GLTF_PBR
            || type == LLDrawPool::POOL_GLTF_PBR_ALPHA_MASK;
    }
}

// ============================================================================
// Geometry rendering (deferred/post-deferred passes) and the final present chain
// ============================================================================

// static
void DXPipeline::renderGeomDeferred(LLPipeline& pipeline, LLCamera& camera, bool do_occlusion)
{

    // Rigged-mesh invisibility root cause: LLRender::syncMatrices()'s
    // DX_RENDER branch (llrender.cpp) was missing the projection_matrix
    // uniform upload; see also objectSkinV.hlsl for two related
    // matrix-layout fixes.

    // DXContext::beginFrame() resets rasterizer state to cull_enabled=false
    // at the start of every frame (matching GL's default). Must explicitly
    // re-enable it here - GL's own renderGeomDeferred() wraps its entire
    // deferred-pools block in the same LLGLEnable cull(GL_CULL_FACE); -
    // without it, all deferred pools render with backface culling off,
    // which double-renders thin double-sided-style geometry (e.g. foliage)
    // as visible z-fighting.
    LLGLEnable cull(GL_CULL_FACE);

    // S24: real DX11 wireframe mode (Develop > Rendering > Wireframe, gUseWireframe) - GL's own
    // renderGeomDeferred() brackets this exact block the same way (glPolygonMode(GL_LINE) at the
    // top, GL_FILL at the bottom). D3D11 has no per-draw fill-mode call like glPolygonMode() - fill
    // mode is a rasterizer-state CREATION-time field. Setting DXStateCache::sWireframeScopeActive
    // (rather than binding a one-off wireframe rasterizer state directly here) is deliberate: cull/
    // scissor/depth-clamp toggle constantly during normal rendering (every LLGLEnable/LLGLDisable),
    // each one forcing LLRender::applyDXRasterizerState() to rebuild the WHOLE bundled rasterizer
    // state object - a direct RSSetState() call here gets silently clobbered back to solid fill by
    // the very first such toggle (the first drawpool), before anything actually draws wireframed.
    // The explicit applyDXRasterizerState() call forces the new scope to take effect immediately
    // rather than waiting on the next incidental toggle. No D3D11 line-width equivalent for
    // wireframe edges (always 1px) - a real, smaller residual gap vs GL.
    if (gUseWireframe)
    {
        DXStateCache::sWireframeScopeActive = true;
        gDX.applyDXRasterizerState();
    }

    // KRLV_TOUCHPOINT: @camtextures/@setcam_textures - checked ONCE per
    // frame here (not per-batch) so LLRenderPass::pushBatch()
    // (dxdrawpool.cpp) and DXDrawPoolAlpha::renderAlpha()
    // (dxdrawpoolalpha.cpp) can cheaply skip the whole stencil-tagging
    // mechanism entirely on every frame the restriction isn't active - see
    // DXStateCache::sTagAttachmentStencilActive's own comment and
    // krlv/README.md's Camera section. outSubstituteTextureId isn't needed
    // here - only presentDeferredScreen()'s post-process pass, later this
    // same frame, needs it.
    {
        LLUUID unusedSubstituteId;
        DXStateCache::sTagAttachmentStencilActive = KRlv::isCamTexturesActive(unusedSubstituteId);
    }

    // bindDeferredShader() uploads MODELVIEW_DELTA_MATRIX/INVERSE_MODELVIEW_DELTA_MATRIX AFTER
    // its call to bindReflectionProbes(), so gDXDeltaModelView/gDXInverseDeltaModelView must be
    // computed here, at the top of this function. This delta must be a real per-frame value, not
    // identity - screenSpaceReflUtil.hlsl's traceScreenRay() depends on it for correct
    // reprojection.
    if (&camera == LLViewerCamera::getInstance())
    {
        glm::mat4 last_modelview = get_last_modelview();
        glm::mat4 cur_modelview = get_current_modelview();

        glm::mat4 m = glm::inverse(last_modelview);  // last camera space to world space
        m = cur_modelview * m; // world space to camera space

        glm::mat4 n = glm::inverse(m);

        gDXDeltaModelView = m;
        gDXInverseDeltaModelView = n;
    }

    // GL calls mSphereProbes.updateUniformsPerFrame()/
    // mMirrorProbes.updateUniformsPerFrame() once per frame here.
    // Mirrored explicitly - nothing else refreshes probe data (positions,
    // bucket assignments, hero-probe box/sphere/mip data) per frame under
    // DX_RENDER, so without this call reflections freeze after the first
    // shader bind. Main camera only: origins are built from gDXModelView, so a secondary
    // camera's pass (e.g. impostor generation) would overwrite them with its own view space.
    if (&camera == LLViewerCamera::getInstance() && LLViewerShaderMgr::instance()->mShaderLevel[LLViewerShaderMgr::SHADER_DEFERRED] > 1)
    {
        pipeline.mSphereProbes.updateUniformsPerFrame();
        pipeline.mMirrorProbes.updateUniformsPerFrame();
    }

    // Mirrors GL's doOcclusion(camera) call site/gating (sUseOcclusion/
    // do_occlusion/sProfileEnabled/!gCubeSnapshot), triggered once when the
    // loop reaches the first pool with getType() >= POOL_GRASS. The
    // gDXLastMatrix reset + reload mirrors GL's pre-doOcclusion() matrix
    // refresh - the occlusion proxy-box shader needs the current camera's
    // modelview bound correctly.
    bool occlude = DXPipeline::sUseOcclusion > 1 && do_occlusion && !LLHLSLShader::sProfileEnabled && !gCubeSnapshot;

    // S24: GL calls setupHWLights() 3x/frame - here, in renderGeomPostDeferred()
    // (calcNearbyLights()+setupHWLights() below), and in renderDeferredLighting()'s
    // local-light block. This call was missing, so every pool drawn here (WLSky,
    // materials, avatar) read the PREVIOUS frame's sun/moon state while later
    // passes in the SAME frame already had the current one - a within-frame
    // mismatch, not a threshold issue. That's what caused the sunrise/sunset
    // flicker: two halves of one frame disagreeing on whether the sun is up.
    DXPipeline::setupHWLights(pipeline);

    LL_RECORD_BLOCK_TIME(FTM_DEFERRED_POOLRENDER);
    // S24: grouped by pool-type run (mirrors DXPipeline::renderGeomShadow()'s
    // identical pattern, below in this file) instead of begin/render/end per pool
    // INSTANCE. For almost every pool type there's only ever one instance,
    // so the two are equivalent - but LLDrawPoolAvatar gets a brand-new
    // instance per avatar (LLPipeline::findPool()'s POOL_AVATAR/
    // POOL_CONTROL_AV case is a deliberate no-op), and beginDeferredPass()/
    // endDeferredPass() bind/unbind the WHOLE deferred shader (reflection
    // probes, shadow matrices, SSAO, ~10 texture units) via shared static
    // state, not anything instance-specific - so per-instance begin/end was
    // rebinding all of that once per avatar instead of once for the whole
    // avatar group. getPools() (a std::set ordered by type, see
    // compare_pools) guarantees same-type pools are always contiguous, so
    // grouping is always valid here, not avatar-specific special-casing.
    // getSkipRenderFlag() stays a per-INSTANCE check inside the inner draw
    // loop (it's real per-instance state for POOL_TERRAIN, see
    // LLPipeline::skipRenderingOfTerrain()) - only the begin/end bracket and
    // the type-level gates move to run once per type-run.
    const auto& pools = pipeline.getPools();
    for (auto iter1 = pools.begin(); iter1 != pools.end(); )
    {
        LLDrawPool* poolp = *iter1;
        U32 cur_type = poolp->getType();

        auto iter2 = iter1;
        while (iter2 != pools.end() && (*iter2)->getType() == cur_type)
        {
            ++iter2;
        }

        if (occlude && cur_type >= LLDrawPool::POOL_GRASS)
        {
            occlude = false;
            gDXLastMatrix = nullptr;
            gDX.loadMatrix(gDXModelView);
            DXPipeline::doOcclusion(pipeline, camera);
        }

        if (!isConvertedPool(cur_type) || !DXPipeline::hasRenderType(pipeline, cur_type))
        {
            iter1 = iter2;
            continue;
        }

        S32 pass_count = poolp->getNumDeferredPasses();
        for (S32 i = 0; i < pass_count; ++i)
        {
            LLVertexBuffer::unbind();
            poolp->beginDeferredPass(i);
            for (auto it = iter1; it != iter2; ++it)
            {
                if ((*it)->getSkipRenderFlag())
                {
                    continue;
                }
                (*it)->renderDeferred(i);
            }
            poolp->endDeferredPass(i);
        }

        iter1 = iter2;
    }

    LLVertexBuffer::unbind();

    if (gUseWireframe)
    {
        DXStateCache::sWireframeScopeActive = false;
        gDX.applyDXRasterizerState();
    }
}

// static
void DXPipeline::renderGeomPostDeferred(LLPipeline& pipeline, LLCamera& camera)
{
    // Draws only isConvertedPostDeferredPool()'s whitelist; GL's grouped-by-
    // type begin/render/end structure is not ported (this loop does
    // begin/render/end once per pool instance instead - harmless for every
    // pool type EXCEPT avatars, which get a fresh LLDrawPoolAvatar instance
    // per avatar (LLPipeline::findPool()'s POOL_AVATAR/POOL_CONTROL_AV case
    // is a deliberate no-op) - see LLDrawPoolAvatar::
    // sJellyGhostsRenderedThisPass's own comment for the one confirmed
    // consequence of that (jelly-doll ghosts redrawn once per avatar instead
    // of once per pass), fixed via this per-pass reset.
    LLDrawPoolAvatar::sJellyGhostsRenderedThisPass = false;
    // renderDebug() is called at the tail below (Develop > Render Metadata's
    // 22 checkboxes - see LLPipeline::renderDebug()'s own comment); it has
    // zero callers anywhere in the engine, so every one of those checkboxes
    // toggles state that nothing reads.
    // renderHighlights() (build/edit-mode selection outline) is a separate,
    // still-unwired gap - it isn't driven by any Render Metadata checkbox.
    // Wireframe mode (gUseWireframe) is wired - see the matching bracket in
    // renderGeomDeferred() above and the one at the tail of this function.
    //
    // doAtmospherics()/doWaterHaze() contain no raw GL calls, so mirroring
    // GL's ordering was enough: atmospherics fires before POOL_WATER when
    // underwater, else before POOL_ALPHA_POST_WATER; water haze always
    // fires before POOL_ALPHA_PRE_WATER; both are skipped for HUD
    // attachments (sRenderingHUDs) and low-detail reflection-probe capture
    // (gCubeSnapshot + RenderReflectionProbeLevel==0).
    (void)camera;

    if (gUseWireframe)
    {
        DXStateCache::sWireframeScopeActive = true;
        gDX.applyDXRasterizerState();
    }

    // LLDrawPoolWaterExclusion::render() has no raw GL calls, so it needed
    // no porting, just to actually be called - mirrored here as an
    // unconditional call before this loop, matching GL's earliest-pool
    // placement. See DXPipeline::doWaterExclusionMask()'s DX_RENDER branch
    // (below in this file): its glClearColor(1,1,1,1) is GL-only and skipped
    // under DX_RENDER, so it uses LLRenderTarget::clearColor() explicitly -
    // LLRenderTarget::clear() clears to transparent black, which would
    // invert the mask's meaning (white=included) and exclude the whole
    // screen by default.
    DXPipeline::doWaterExclusionMask(pipeline);

    bool done_atmospherics = DXPipeline::sRenderingHUDs;
    bool done_water_haze = done_atmospherics;

    U32 atmospherics_pass = DXPipeline::sUnderWaterRender ? (U32)LLDrawPool::POOL_WATER : (U32)LLDrawPool::POOL_ALPHA_POST_WATER;
    U32 water_haze_pass = LLDrawPool::POOL_ALPHA_PRE_WATER;

    // S24 : !gEquirectCapture - a full 360 capture wants the same real
    // atmospherics/water-haze a normal frame gets, not the reduced-detail
    // shortcut reflection/sphere probes intentionally take.
    static LLCachedControl<S32> atmospherics_probe_level(gSavedSettings, "RenderReflectionProbeLevel", 0);
    bool low_detail_probe = atmospherics_probe_level == 0 && gCubeSnapshot && !gEquirectCapture;
    done_atmospherics = done_atmospherics || low_detail_probe;
    done_water_haze = done_water_haze || low_detail_probe;

    // Called for the main scene from DXPipeline::renderDeferredLighting()
    // (right before its own final screen_target->flush()), and for HUD
    // attachments from render_hud_attachments().

    // GL's renderGeomPostDeferred() also wraps its whole pool loop in
    // LLGLEnable cull(GL_CULL_FACE); - individual post-deferred pools (e.g.
    // dxdrawpoolalpha.cpp) only ever explicitly DISABLE cull for confirmed
    // double-sided materials, assuming it's already on from this outer
    // scope, matching GL.
    LLGLEnable cull(GL_CULL_FACE);

    DXPipeline::calcNearbyLights(pipeline, camera);
    DXPipeline::setupHWLights(pipeline);

    gDX.setSceneBlendType(LLRender::BT_ALPHA);
    gDX.setColorWriteMask(true, false);

    LL_RECORD_BLOCK_TIME(FTM_POST_DEFERRED_POOLRENDER);
    // S24: grouped by pool-type run - see renderGeomDeferred()'s identical
    // restructuring and its comment for why (LLDrawPoolAvatar gets a fresh
    // instance per avatar, and beginPostDeferredPass()/endPostDeferredPass()
    // bind/unbind the whole deferred-alpha shader via shared static state,
    // not anything instance-specific, so per-instance begin/end was
    // rebinding it once per avatar instead of once for the whole avatar
    // group). getSkipRenderFlag() stays a per-instance check in the inner
    // draw loop.
    const auto& post_pools = pipeline.getPools();
    for (auto iter1 = post_pools.begin(); iter1 != post_pools.end(); )
    {
        LLDrawPool* poolp = *iter1;
        U32 cur_type = poolp->getType();

        auto iter2 = iter1;
        while (iter2 != post_pools.end() && (*iter2)->getType() == cur_type)
        {
            ++iter2;
        }

        if (!isConvertedPostDeferredPool(cur_type) || !DXPipeline::hasRenderType(pipeline, cur_type))
        {
            iter1 = iter2;
            continue;
        }

        if (cur_type >= atmospherics_pass && !done_atmospherics)
        { // do atmospherics against depth buffer before rendering alpha
            DXPipeline::doAtmospherics(pipeline);
            done_atmospherics = true;
        }

        if (cur_type >= water_haze_pass && !done_water_haze)
        { // do water haze against depth buffer before rendering alpha
            DXPipeline::doWaterHaze(pipeline);
            done_water_haze = true;
        }

        // gDXLastMatrix reset + reload mirrors the per-pool-type-group reset done elsewhere.
        // Pools that call LLRenderPass::applyModelMatrix() per draw item (dxdrawpool.cpp) self-correct
        // regardless, but LLDrawPoolWater does NOT call it (its vertices
        // are pre-baked in world/agent space by LLVOWater::updateGeometry(),
        // expecting only the camera view matrix) - without this reset,
        // water inherits whatever model matrix the last alpha-blended
        // object drawn by POOL_ALPHA_PRE_WATER happened to leave bound.
        gDXLastMatrix = nullptr;
        gDX.matrixMode(LLRender::MM_MODELVIEW);
        gDX.loadMatrix(gDXModelView);

        S32 pass_count = poolp->getNumPostDeferredPasses();
        for (S32 i = 0; i < pass_count; ++i)
        {
            LLVertexBuffer::unbind();
            poolp->beginPostDeferredPass(i);
            for (auto it = iter1; it != iter2; ++it)
            {
                if ((*it)->getSkipRenderFlag())
                {
                    continue;
                }
                (*it)->renderPostDeferred(i);
            }
            poolp->endPostDeferredPass(i);
        }

        iter1 = iter2;
    }

    LLVertexBuffer::unbind();

    // Restores colorWriteMask to (true,true) after it was set to
    // (true,false) at the top of this function for the alpha/fullbright/
    // glow pool loop - real, sticky D3D11 blend state (OMSetBlendState) that
    // nothing else resets before the UI draw pass. This function runs both
    // for the main scene and for HUD attachments (render_hud_attachments()).
    gDX.setColorWriteMask(true, true);

    // Mirrors GL's own placement exactly (right after the post-deferred pool
    // loop, before restoring wireframe fill mode) - see the GL reference
    // baseline's LLPipeline::renderGeomPostDeferred(). gCubeSnapshot-gated
    // like GL: reflection-probe/snapshot captures shouldn't show debug
    // overlays baked into the probe's result.
    if (!gCubeSnapshot)
    {
        DXPipeline::renderDebug(pipeline);
    }

    if (gUseWireframe)
    {
        DXStateCache::sWireframeScopeActive = false;
        gDX.applyDXRasterizerState();
    }

    // gDXLastModelView/gDXLastProjection's "advance to current, for next
    // frame's reprojection" bookkeeping lives in
    // DXPipeline::renderDeferredLighting()'s tail instead of here - this
    // function runs from multiple contexts (world camera early and late,
    // and the HUD camera via render_hud_attachments()), and letting the
    // HUD-camera call clobber these each frame broke SSAO's temporal
    // resolve and SSR's ray march, which must reproject off the previous
    // frame's WORLD camera, not the HUD's.
}

namespace
{
    // Self-contained placeholder shader pair for presentDeferredScreen()
    // below - deliberately NOT registered with LLViewerShaderMgr (this
    // isn't part of the real ~150-shader set, just a one-off blit used
    // only by this stopgap). Draws a fullscreen triangle purely from
    // SV_VertexID (no vertex buffer needed).
    const char* const kPresentVS =
        "struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };\n"
        "VSOut main(uint vid : SV_VertexID)\n"
        "{\n"
        "    VSOut o;\n"
        "    o.uv = float2((vid << 1) & 2, vid & 2);\n"
        "    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
        "    return o;\n"
        "}\n";

    const char* const kPresentPS =
        "Texture2D srcTex : register(t0);\n"
        "SamplerState srcSampler : register(s0);\n"
        "float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET\n"
        "{\n"
        "    return srcTex.Sample(srcSampler, uv);\n"
        "}\n";

    // Lazily compiled on first use, then cached for the process lifetime -
    // mirrors DXStateCache's "compile/create once" approach for other
    // one-off D3D11 objects.
    bool getPresentShader(DXShader** out_shader, ID3D11SamplerState** out_sampler)
    {
        static DXShader s_shader;
        static ID3D11SamplerState* s_sampler = nullptr;
        static bool s_attempted = false;
        static bool s_ready = false;

        if (!s_attempted)
        {
            s_attempted = true;

            bool compiled = s_shader.compileVertexShader(kPresentVS, "DXPipeline present VS")
                && s_shader.compilePixelShader(kPresentPS, "DXPipeline present PS");

            D3D11_SAMPLER_DESC sampler_desc = {};
            sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
            sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
            HRESULT hr = gDXDevice.getDevice()->CreateSamplerState(&sampler_desc, &s_sampler);

            s_ready = compiled && SUCCEEDED(hr) && s_sampler != nullptr;
            if (!s_ready)
            {
                LL_WARNS("DXPipeline") << "presentDeferredScreen: failed to build placeholder present shader/sampler" << LL_ENDL;
            }
        }

        *out_shader = &s_shader;
        *out_sampler = s_sampler;
        return s_ready;
    }

    // DXRenderTarget::bindSwapChainBackBuffer() (called by both
    // presentDeferredScreen() draw paths below) always sets a viewport
    // covering the FULL swap-chain/back-buffer size - correct for its other
    // caller (LLRenderTarget::flush()'s generic restore case), but wrong for
    // the final present-to-screen step, which must confine 3D content to
    // LLViewerWindow::mWorldViewRectRaw (the area left over after menu/
    // location bar chrome, matching GL's glViewport()). Overridden here
    // instead of changing bindSwapChainBackBuffer()'s own default.
    //
    // Coordinate conversion: LLRect (mWorldViewRectRaw) uses GL's bottom-up
    // convention (Y grows upward, origin bottom-left, mTop/mBottom measured
    // from the bottom) - D3D11_VIEWPORT uses top-down screen-space (Y grows
    // downward, origin top-left). TopLeftX matches directly (both left-
    // origin); TopLeftY = full window height - mTop (the gap between the
    // window's top edge and the world-view rect's top edge, in D3D11's
    // convention).
    void setPresentViewport()
    {
        LLRect world_rect = gViewerWindow ? gViewerWindow->getWorldViewRectRaw() : LLRect();
        if (world_rect.getWidth() <= 0 || world_rect.getHeight() <= 0)
        {
            return; // not ready yet (e.g. very early startup) - leave the full-window viewport bindSwapChainBackBuffer() already set
        }

        D3D11_VIEWPORT vp = {};
        vp.TopLeftX = (float)world_rect.mLeft;
        vp.TopLeftY = (float)(gDXSwapChain.getHeight() - world_rect.mTop);
        vp.Width = (float)world_rect.getWidth();
        vp.Height = (float)world_rect.getHeight();
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        gDXDevice.getContext()->RSSetViewports(1, &vp);
    }

    // Single, shared "put this render target on screen" chokepoint for both
    // the fallback (unlit/no-gamma-shader) case and the real gamma-corrected/
    // post-fx'd case. Primary path uses gDeferredPostNoDoFNoiseProgram
    // (depth-aware noise dithering, same permutation GL's renderFinalize()
    // tail uses); the placeholder VS/PS pair is kept only as a fallback if
    // that shader failed to compile.
    //
    // Selects which offscreen eye target (if any) this frame's world-scene
    // composite should land in, via LLViewerWindow::mMaskMode/getMaskMode()
    // (same enum LLViewerCamera's stereo frustum math reads).
    LLRenderTarget* getCurrentStereoEyeTarget(LLPipeline& pipeline)
    {
        S32 mode = gViewerWindow->getMaskMode();
        if (mode == MASK_MODE_LEFT) { return &pipeline.mStereoEyeL; }
        if (mode == MASK_MODE_RIGHT) { return &pipeline.mStereoEyeR; }
        return nullptr;
    }

    void presentFinal(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dest = nullptr)
    {
        if (!src || !src->getColorSRV(0))
        {
            return;
        }

        // When dest is given (stereo mode, one eye's pass), capture this
        // eye's final composited frame into its own offscreen target
        // instead of the swap chain - see stereoAnaglyphF.hlsl's comment.
        // dest is exactly world_rect-sized (LLPipeline::mStereoEyeL/R, the
        // WORLD VIEW resolution not the full chrome-included window), so no
        // world_rect viewport offset is needed here, unlike the swap-chain
        // case below. A real LLRenderTarget::bindTarget() call already
        // manages LLRenderTarget::sBoundTarget bookkeeping on its own.
        if (dest)
        {
            dest->bindTarget(false);

            if (gDeferredPostNoDoFNoiseProgram.isComplete())
            {
                gDeferredPostNoDoFNoiseProgram.bind();
                gDeferredPostNoDoFNoiseProgram.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, src);
                gDeferredPostNoDoFNoiseProgram.bindTexture(LLShaderMgr::DEFERRED_DEPTH, &pipeline.mRT->deferredScreen, true);
                gDeferredPostNoDoFNoiseProgram.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES,
                    (F32)src->getWidth(), (F32)src->getHeight());

                {
                    DXDepthTest depth_test(true, true, GL_ALWAYS);
                    pipeline.mScreenTriangleVB->setBuffer();
                    pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
                }

                gDeferredPostNoDoFNoiseProgram.unbind();
            }
            // Deliberately no raw-placeholder fallback here (unlike the
            // swap-chain path below) - this exact shader already backs the
            // ordinary non-stereo present path too, so if it ever failed to
            // compile the whole viewer would already be in far worse shape
            // than a stereo-only gap.
            dest->flush();
            return;
        }

        DXRenderTarget::bindSwapChainBackBuffer();
        // This call updates the real D3D11 render target directly via
        // DXRenderTarget, bypassing LLRenderTarget::bindTarget()/flush()'s
        // sBoundTarget/mPreviousRT bookkeeping. Whatever LLRenderTarget was
        // last left bound-but-not-flushed (e.g. mPostPongMap, left bound by
        // combineGlow()) stays "remembered" as sBoundTarget otherwise - a
        // silent bug when a HUD is attached and this function runs a second
        // time that frame: doWaterExclusionMask()'s bindTarget()/flush()
        // cycle would capture/restore that stale target instead of the real
        // back buffer, and nothing downstream ever rebinds it, so the whole
        // UI silently lands in a stale offscreen texture. Reset the
        // bookkeeping here (same pattern as LLHLSLShader::sCurBoundShaderPtr
        // below).
        LLRenderTarget::sBoundTarget = nullptr;
        setPresentViewport();

        // FSR1 EASU upscale - takes over this final blit entirely when
        // active. AMD's own FSR1 pipeline is EASU followed by RCAS, not EASU
        // alone (EASU's spatial resample alone is visibly softer) - so EASU
        // writes into mFSR1EasuBuffer (full dest resolution) first, then
        // RCAS reads that and writes the real backbuffer. No stereo
        // (dest != nullptr) support yet - returned above before reaching here.
        // S24: EASU upscale auto-engages purely off RenderScalePercent < 100,
        // independent of the CAS/RenderFSR1Sharpness same-resolution sliders
        // below. Also drops out of EASU (and
        // the mFSR1EasuBuffer bind/flush/rebind sequence it uniquely does,
        // including LLRenderTarget::flush()'s rarely-exercised no-previous-
        // target fallback) for the last few frames before shutdown, once the
        // user has actually confirmed quit (LLAppViewer::requestQuit(), which
        // this reflects, runs right after the Ares shutdown console in the
        // normal quit flow - exactly the window a real device-lost crash was
        // once tied to here, though that crash's actual root cause turned
        // out to be unrelated - see LLViewerWindow::rawSnapshot()'s oversized-
        // snapshot guard). RenderScalePercent itself is untouched, so this is
        // purely a runtime skip - EASU comes back automatically next launch.
        // Falls through to the same safe path EASU's own shader-incomplete
        // case already uses.
        static LLCachedControl<F32> render_scale_pct(gSavedSettings, "RenderScalePercent", 100.0f);
        bool upscaling_active = render_scale_pct() < 100.0f;
        bool quitting = LLAppViewer::instance() && LLAppViewer::instance()->quitRequested();
        if (upscaling_active && gViewerWindow && !quitting)
        {
            LLRect world_rect = gViewerWindow->getWorldViewRectRaw();
            U32 dest_w = (U32)world_rect.getWidth();
            U32 dest_h = (U32)world_rect.getHeight();
            if (dest_w > 0 && dest_h > 0)
            {
                if (!pipeline.mFSR1EasuBuffer.isComplete() ||
                    pipeline.mFSR1EasuBuffer.getWidth() != dest_w ||
                    pipeline.mFSR1EasuBuffer.getHeight() != dest_h)
                {
                    pipeline.mFSR1EasuBuffer.allocate(dest_w, dest_h, GL_RGBA);
                }

                // S24: allocate() can fail (e.g. the device is already in
                // trouble from a resize/teardown race at quit time, or
                // genuinely out of memory) - LLRenderTarget::bindTarget() has
                // no completeness check of its own and will unconditionally
                // drive a bind call for whatever's there, invalid texture or
                // not. Check isComplete() before touching it at all, same as
                // "EASU itself failed" below falls through to the normal path
                // rather than using a broken target.
                bool easu_ok = false;
                if (pipeline.mFSR1EasuBuffer.isComplete())
                {
                    pipeline.mFSR1EasuBuffer.bindTarget();
                    easu_ok = DXPipeline::applyFSR1Easu(pipeline, src, dest_w, dest_h);
                    pipeline.mFSR1EasuBuffer.flush();
                }

                if (easu_ok)
                {
                    // Re-bind the real destination for the final write - EASU
                    // wrote into mFSR1EasuBuffer above, not here.
                    DXRenderTarget::bindSwapChainBackBuffer();
                    LLRenderTarget::sBoundTarget = nullptr;
                    setPresentViewport();

                    if (!DXPipeline::applyFSR1RcasToBoundTarget(pipeline, &pipeline.mFSR1EasuBuffer))
                    {
                        // RCAS off (RenderFSR1Sharpness==0) or unavailable -
                        // EASU's result still needs to reach the screen.
                        // Reuse the existing noise-dither blit, sourced from
                        // mFSR1EasuBuffer instead of the original low-res src.
                        if (gDeferredPostNoDoFNoiseProgram.isComplete())
                        {
                            gDeferredPostNoDoFNoiseProgram.bind();
                            gDeferredPostNoDoFNoiseProgram.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, &pipeline.mFSR1EasuBuffer);
                            gDeferredPostNoDoFNoiseProgram.bindTexture(LLShaderMgr::DEFERRED_DEPTH, &pipeline.mRT->deferredScreen, true);
                            gDeferredPostNoDoFNoiseProgram.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES,
                                (F32)dest_w, (F32)dest_h);

                            DXDepthTest depth_test(true, true, GL_ALWAYS);
                            pipeline.mScreenTriangleVB->setBuffer();
                            pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

                            gDeferredPostNoDoFNoiseProgram.unbind();
                        }
                    }
                    return;
                }
                // EASU itself failed (shader incomplete) - fall through to
                // the normal path below.
            }
        }

        if (gDeferredPostNoDoFNoiseProgram.isComplete())
        {
            gDeferredPostNoDoFNoiseProgram.bind();
            gDeferredPostNoDoFNoiseProgram.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, src);
            gDeferredPostNoDoFNoiseProgram.bindTexture(LLShaderMgr::DEFERRED_DEPTH, &pipeline.mRT->deferredScreen, true);
            gDeferredPostNoDoFNoiseProgram.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES,
                (F32)src->getWidth(), (F32)src->getHeight());

            {
                DXDepthTest depth_test(true, true, GL_ALWAYS);
                pipeline.mScreenTriangleVB->setBuffer();
                pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
            }

            gDeferredPostNoDoFNoiseProgram.unbind();
            return;
        }

        // Fallback: real shader failed to compile - original placeholder blit.
        DXShader* shader = nullptr;
        ID3D11SamplerState* sampler = nullptr;
        if (!getPresentShader(&shader, &sampler))
        {
            return;
        }

        ID3D11DeviceContext* ctx = gDXDevice.getContext();
        ID3D11ShaderResourceView* srv = src->getColorSRV(0);

        ctx->VSSetShader(shader->getVS(), nullptr, 0);
        ctx->PSSetShader(shader->getPS(), nullptr, 0);
        ctx->IASetInputLayout(nullptr);
        DXStateCache::setPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        ID3D11Buffer* null_vb = nullptr;
        UINT stride = 0;
        UINT offset = 0;
        ctx->IASetVertexBuffers(0, 1, &null_vb, &stride, &offset);

        // Wrapped like every other PSSetShaderResources call site (dangling-SRV safety net) -
        // src's SRV is only guaranteed valid on the bound (non-fallback) path
        // above; a device-lost/resize race hitting src at the exact moment
        // this placeholder fallback fires is the scenario the net exists for.
        // Still call unbind() below even on a caught exception - the raw
        // VSSetShader/PSSetShader calls above already ran, so sCurBoundShaderPtr
        // bookkeeping needs resetting regardless of whether the SRV bind itself
        // landed.
        bool srv_bind_ok = safeBindSRV(ctx, 0, srv);
        if (srv_bind_ok)
        {
            ctx->PSSetSamplers(0, 1, &sampler);
            ctx->Draw(3, 0);

            ID3D11ShaderResourceView* null_srv = nullptr;
            ctx->PSSetShaderResources(0, 1, &null_srv);
        }

        // This fallback binds its placeholder VS/PS via raw
        // ctx->VSSetShader()/PSSetShader() calls, bypassing
        // LLHLSLShader::bind() - so LLHLSLShader::sCurBoundShaderPtr (which
        // bind() uses to skip redundant VSSetShader/PSSetShader calls) never
        // learns the real bound shader changed, and the next real bind()
        // call of that same shader object would wrongly skip the rebind.
        // Calling unbind() here nulls sCurBoundShaderPtr, forcing a genuine
        // rebind next time.
        LLHLSLShader::unbind();
    }
}

// static
void DXPipeline::presentDeferredScreen(LLPipeline& pipeline)
{
    // Prefers the real lit result (mRT->screen, written by
    // renderDeferredLighting()) over deferredScreen's raw, unlit diffuse
    // attachment - but ONLY when renderDeferredLighting() actually ran and
    // completed THIS frame (sScreenLitThisFrame). A getColorSRV(0)!=nullptr
    // check alone is not sufficient: mRT->screen is allocated
    // unconditionally every frame, so its SRV is always non-null even when
    // renderDeferredLighting() early-returned without writing anything.
    bool use_lit = sScreenLitThisFrame && pipeline.mRT->screen.getColorSRV(0) != nullptr;

    LLRenderTarget* diffuse_rt = use_lit ? &pipeline.mRT->screen : &pipeline.mRT->deferredScreen;
    if (!diffuse_rt->getColorSRV(0))
    {
        // Nothing has allocated the deferred screen's diffuse attachment
        // yet (e.g. very first frame(s) during startup) - nothing to
        // present, leave the back buffer as DXContext::beginFrame() left it.
        return;
    }

    // Only the genuinely lit result needs gamma correction - the unlit
    // fallback (deferredScreen's raw diffuse attachment, shown when
    // renderDeferredLighting() didn't run this frame) is a debug/startup
    // view, so it keeps going through the raw placeholder blit unchanged.
    //
    // LLPipeline::tonemap() (GL, long since removed) was the real per-frame present
    // pass when RenderHDREnabled is on - postDeferredTonemap.hlsl, which
    // tonemaps THEN gamma-corrects. When RenderHDREnabled is off, GL falls
    // back to LLPipeline::gammaCorrect() (postDeferredGammaCorrect.hlsl -
    // linear_to_srgb() + a hard clamp(0,1), no tonemap curve). This mirrors
    // LLPipeline::tonemap()'s own no_post/legacy_gamma shader selection and
    // uniform uploads, gated the same way GL gates it (RenderHDREnabled).
    // Without the real tonemap curve, bright HDR values (e.g. an
    // unattenuated sky) hard-clip to white instead of compressing
    // gracefully.
    //
    // Known limitation: generateExposure()/generateLuminance() (the
    // temporal auto-exposure chain feeding mExposureMap when HDR is on) are
    // still DX_RENDER no-ops - mExposureMap stays at its static neutral
    // (1,1,1,0) clear value rather than adapting to scene luminance. The
    // tonemap curve itself is correct either way; only auto-exposure
    // adaptation is a placeholder.
    //
    // copyScreenSpaceReflections() is already fully DX-safe as written
    // (gDX.getTexUnit()->bind()/DXDepthTest/mScreenTriangleVB/
    // gCopyDepthProgram); it just needed a DX_RENDER call site here, since
    // GL's own call to it lives inside renderFinalize(), whose GL body this
    // function replaces. Mirrors GL's order (copy happens before tonemap) -
    // mSceneMap gets this frame's just-finished lit
    // scene, consumed by the next frame's SSR ray march (standard
    // temporal-reuse, not a bug).
    if (use_lit && DXPipeline::RenderScreenSpaceReflections)
    {
        DXPipeline::copyScreenSpaceReflections(pipeline, &pipeline.mRT->screen, &pipeline.mSceneMap);
    }

    if (use_lit)
    {
        static LLCachedControl<bool> should_auto_adjust(gSavedSettings, "RenderSkyAutoAdjustLegacy", false);
        static LLCachedControl<bool> buildNoPost(gSavedSettings, "RenderDisablePostProcessing", false);
        static LLCachedControl<F32> exposure_setting(gSavedSettings, "RenderExposure", 1.f);
        static LLCachedControl<U32> tonemap_type_setting(gSavedSettings, "RenderTonemapType", 0U);
        static LLCachedControl<bool> has_hdr_setting(gSavedSettings, "RenderHDREnabled", true);
        LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();

        bool use_tonemap = has_hdr_setting;
        bool legacy_gamma = (!psky || psky->getReflectionProbeAmbiance(should_auto_adjust) == 0.f);
        bool no_post = gSnapshotNoPost || legacy_gamma || (buildNoPost && gFloaterTools && gFloaterTools->isAvailable());

        LLHLSLShader& gamma_shader = use_tonemap
            ? (legacy_gamma
                ? (no_post ? gNoPostTonemapLegacyGammaCorrectProgram : gDeferredPostTonemapLegacyGammaCorrectProgram)
                : (no_post ? gNoPostTonemapGammaCorrectProgram : gDeferredPostTonemapGammaCorrectProgram))
            : (legacy_gamma ? gLegacyPostGammaCorrectProgram : gDeferredPostGammaCorrectProgram);

        if (gamma_shader.isComplete())
        {
            // Writes to mPostPingMap (the same GL_RGBA scratch buffer GL's
            // post-fx chain ping-pongs through), not the back buffer
            // directly, so later stages (glow, DoF, FXAA/SMAA) have a real
            // intermediate to read/write before anything reaches the
            // screen - matches GL's gammaCorrect(&mRT->screen, &mPostPingMap)
            // shape. presentFinal() below is the single "put this on
            // screen" chokepoint every stage eventually feeds.
            // false: mPostPingMap has no depth attachment of its own.
            pipeline.mPostPingMap.bindTarget(false);

            gamma_shader.bind();
            gamma_shader.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, &pipeline.mRT->screen, false, LLTexUnit::TFO_POINT);
            gamma_shader.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (F32)pipeline.mRT->screen.getWidth(), (F32)pipeline.mRT->screen.getHeight());

            // Only the tonemap family (postDeferredTonemap.hlsl) declares/
            // uses exposureMap/exposure/tonemap_mix/tonemap_type;
            // postDeferredGammaCorrect.hlsl does not.
            if (use_tonemap)
            {
                gamma_shader.bindTexture(LLShaderMgr::EXPOSURE_MAP, &pipeline.mExposureMap);

                static LLStaticHashedString s_exposure("exposure");
                static LLStaticHashedString s_tonemap_mix("tonemap_mix");
                static LLStaticHashedString s_tonemap_type("tonemap_type");
                F32 e = llclamp((F32)exposure_setting(), 0.5f, 4.f);
                gamma_shader.uniform1f(s_exposure, e);
                gamma_shader.uniform1i(s_tonemap_type, tonemap_type_setting);
                gamma_shader.uniform1f(s_tonemap_mix, psky ? psky->getTonemapMix(should_auto_adjust()) : 1.f);
            }

            pipeline.mScreenTriangleVB->setBuffer();
            pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

            gamma_shader.unbind();
            pipeline.mPostPingMap.flush();

            // generateGlow()/combineGlow(): uniform2f(U32,...)/uniform3f(U32,...) (GLOW_DELTA,
            // GLOW_LUM_WEIGHTS, GLOW_WARMTH_WEIGHTS, DEFERRED_SCREEN_RES) used to be a silent
            // no-op under DX_RENDER - fixed at the source (llhlslshader.cpp) so every caller
            // benefits, not just glow.
            DXPipeline::generateGlow(pipeline, &pipeline.mPostPingMap);

            // NOTE: glowExtractV.hlsl must stay position-only input with
            // texcoord derived procedurally (see postDeferredV.hlsl) - it
            // draws with mScreenTriangleVB, which supplies no TEXCOORD0
            // attribute; declaring one there fails CreateInputLayout()
            // (E_INVALIDARG) and silently skips the extract draw entirely.

            LLRenderTarget* sourceBuffer = &pipeline.mPostPingMap;
            LLRenderTarget* targetBuffer = &pipeline.mPostPongMap;

            DXPipeline::combineGlow(pipeline, sourceBuffer, targetBuffer);
            std::swap(sourceBuffer, targetBuffer);

            // DoF and FXAA/SMAA are called directly, unmodified - the only real DX_RENDER gap was
            // 3 raw glViewport() calls (renderDoF() x2, applyFXAA() x1) assuming an implicit GL
            // default-framebuffer viewport, fixed at the source (DXContext::setViewport()).
            // Mirrors GL's renderFinalize() order: DoF is optional (RenderDepthOfField),
            // then exactly one of FXAA or SMAA runs per RenderFSAAType.
            if ((DXPipeline::RenderDepthOfFieldInEditMode || !LLToolMgr::getInstance()->inBuildMode()) &&
                DXPipeline::RenderDepthOfField &&
                !gCubeSnapshot)
            {
                DXPipeline::renderDoF(pipeline, sourceBuffer, targetBuffer);
                std::swap(sourceBuffer, targetBuffer);
            }

            if (DXPipeline::RenderFSAAType == 1)
            {
                DXPipeline::applyFXAA(pipeline, sourceBuffer, targetBuffer);
                std::swap(sourceBuffer, targetBuffer);
            }
            else if (DXPipeline::RenderFSAAType == 2)
            {
                DXPipeline::generateSMAABuffers(pipeline, sourceBuffer);
                DXPipeline::applySMAA(pipeline, sourceBuffer, targetBuffer);
                std::swap(sourceBuffer, targetBuffer);
            }

            // Develop > Rendering > Buffer Visualization. Mirrors GL's ordering: after
            // FXAA/SMAA, before the OpenCL effects block.
            if (DXPipeline::RenderBufferVisualization > -1)
            {
                switch (DXPipeline::RenderBufferVisualization)
                {
                case 0:
                case 1:
                case 2:
                case 3:
                    DXPipeline::visualizeBuffers(pipeline, &pipeline.mRT->deferredScreen, sourceBuffer, DXPipeline::RenderBufferVisualization);
                    break;
                case 4:
                    DXPipeline::visualizeBuffers(pipeline, &pipeline.mLuminanceMap, sourceBuffer, 0);
                    break;
                case 5:
                    if (DXPipeline::RenderFSAAType > 0)
                    {
                        DXPipeline::visualizeBuffers(pipeline, &pipeline.mFXAAMap, sourceBuffer, 0);
                    }
                    break;
                case 6:
                    if (DXPipeline::RenderFSAAType == 2)
                    {
                        DXPipeline::visualizeBuffers(pipeline, &pipeline.mSMAABlendBuffer, sourceBuffer, 0);
                    }
                    break;
                case 7:
                    if (DXPipeline::RenderFSAAType == 2)
                    {
                        DXPipeline::visualizeBuffers(pipeline, &pipeline.mSMAABlendBuffer, sourceBuffer, 0, true);
                    }
                    break;
                default:
                    break;
                }
            }

            // OpenCL post-fx effects (kveffects.cpp): mirrors GL's
            // renderFinalize() "apply effects before final draw" block.
            // updateEffectMask()/effectsMask are normally refreshed by GL's
            // renderFinalize() body, which never runs under DX_RENDER, so
            // both must be called/read from here instead. No
            // glFinish()-equivalent needed before the read:
            // DXReadback::readPixels()'s Map(D3D11_MAP_READ) is already a
            // synchronous GPU/CPU coherence point.
            //
            // S24: !gCubeSnapshot mirrors DoF's existing gate just above -
            // motionBlurGPU() (kveffects.cpp) keeps a persistent cross-frame
            // OpenCL history buffer, which would otherwise blend unrelated,
            // discontinuous-orientation cube faces together during a
            // multi-face capture (360 capture, reflection probes).
            updateEffectMask();
            if (effectsMask != 0 && !gCubeSnapshot)
            {
                ID3D11Texture2D* effects_tex = sourceBuffer->getDXColorTexture(0);
                if (effects_tex)
                {
                    int width = sourceBuffer->getWidth();
                    int height = sourceBuffer->getHeight();

                    if (effectsMask & DESATURATION) ImageProcessor::desaturateImageGPU(effects_tex, width, height);
                    if (effectsMask & INVERT) ImageProcessor::invertImageGPU(effects_tex, width, height);
                    if (effectsMask & RGB_CONTROL) ImageProcessor::RGBControlGPU(effects_tex, width, height);
                    if (effectsMask & CEL_SHADING) ImageProcessor::celShadeImageGPU(effects_tex, width, height);
                    if (effectsMask & VIGNETTE) ImageProcessor::vignetteGPU(effects_tex, width, height);
                    if (effectsMask & EDGE_GLOW) ImageProcessor::edgeGlowGPU(effects_tex, width, height);
                    if (effectsMask & NIGHT_VISION) ImageProcessor::nightVisionGPU(effects_tex, width, height);
                    if (effectsMask & MOTION_BLUR) ImageProcessor::motionBlurGPU(effects_tex, width, height);
                }
            }

            // KRLV_TOUCHPOINT: @camdrawmin/@camdrawmax/@camdrawalphamin/
            // @camdrawalphamax/@camdrawcolor - a real HLSL shader pass
            // (unlike the OpenCL block above, this needs the scene depth
            // buffer, which kveffects' color-only pipeline never touches
            // - see krlv/README.md's Camera section). Runs after the
            // OpenCL effects and before presentFinal(), same insertion
            // point, same src/dst ping-pong idiom every other pass in
            // this block already uses.
            {
                // Zero-initialized: getCamDrawParams() leaves these
                // untouched (not zeroed) when it returns false - now that
                // this call can also fire for @camtextures ALONE (see
                // below), these need a safe "no fog" default (0 dist/alpha
                // makes the shader's own fogAlpha lerp collapse to 0, a
                // pure no-op) rather than whatever was on the stack.
                F32 krlvMinDist = 0.f, krlvMaxDist = 0.f, krlvMinAlpha = 0.f, krlvMaxAlpha = 0.f, krlvColorR = 0.f, krlvColorG = 0.f, krlvColorB = 0.f;
                const bool krlvFogActive = KRlv::getCamDrawParams(krlvMinDist, krlvMaxDist, krlvMinAlpha, krlvMaxAlpha, krlvColorR, krlvColorG, krlvColorB);

                // KRLV_TOUCHPOINT: @camtextures/@setcam_textures - shares
                // this SAME pass rather than a separate one, so it must
                // also run on a frame where ONLY this restriction (and not
                // camdrawfog) is active - see krlv/README.md's Camera
                // section and LLPipeline::applyKrlvCamDrawFog()'s comment.
                LLUUID krlvCamTexturesSubstituteId;
                const bool krlvCamTexturesActive = KRlv::isCamTexturesActive(krlvCamTexturesSubstituteId);

                if (krlvFogActive || krlvCamTexturesActive)
                {
                    LLViewerFetchedTexture* krlvSubstitute = krlvCamTexturesSubstituteId.notNull()
                        ? LLViewerTextureManager::getFetchedTexture(krlvCamTexturesSubstituteId)
                        : nullptr;
                    DXPipeline::applyKrlvCamDrawFog(pipeline, sourceBuffer, targetBuffer, krlvMinDist, krlvMaxDist, krlvMinAlpha, krlvMaxAlpha, krlvColorR, krlvColorG, krlvColorB,
                        krlvCamTexturesActive, krlvSubstitute);
                    std::swap(sourceBuffer, targetBuffer);
                }
            }

            // Sharpening. Last pass before present; its input is the
            // display-encoded output of the gamma step, so neither CASF.hlsl
            // nor RCASF.hlsl re-encode. CAS (standard Advanced Graphics
            // Preferences) and FSR1 RCAS (S24 Engine Tweaks > AMD FSR card)
            // are two independent sliders now, not a shared method selector -
            // FSR takes priority when both are non-zero (same guardrail
            // intent as before: running both back-to-back would just
            // double-sharpen). Skipped entirely whenever upscaling is active
            // (RenderScalePercent < 100), since presentFinal()'s EASU+RCAS
            // chain is already the complete final-polish stage for that case -
            // letting this ALSO run first would sharpen the low-res source,
            // then EASU would upscale that already-sharpened result, then
            // RCAS would sharpen it again, compounding into an over-sharpened,
            // not just redundant, result.
            static LLCachedControl<F32> rcas_sharpness(gSavedSettings, "RenderFSR1Sharpness", 0.4f);
            static LLCachedControl<F32> cas_sharpness(gSavedSettings, "RenderCASSharpness", 0.4f);
            static LLCachedControl<F32> sharpen_render_scale_pct(gSavedSettings, "RenderScalePercent", 100.0f);
            if (sharpen_render_scale_pct() >= 100.0f)
            {
                if (rcas_sharpness() > 0.f)
                {
                    DXPipeline::applyFSR1Rcas(pipeline, sourceBuffer, targetBuffer);
                    std::swap(sourceBuffer, targetBuffer);
                }
                else if (cas_sharpness() > 0.f)
                {
                    DXPipeline::applyCAS(pipeline, sourceBuffer, targetBuffer);
                    std::swap(sourceBuffer, targetBuffer);
                }
            }

            gLastCompositedPostTarget = sourceBuffer;
            presentFinal(pipeline, sourceBuffer, getCurrentStereoEyeTarget(pipeline));
            return;
        }
        // gamma_shader failed to compile - fall through to the raw
        // placeholder blit below rather than presenting nothing.
    }

    // Startup/fallback case (not lit yet this frame, or gamma shader failed
    // to compile) - present deferredScreen's raw diffuse straight through,
    // same as always. No post-fx chain to run in this case (nothing has
    // been gamma-corrected yet, so glow/DoF/FXAA would have nothing
    // meaningful to operate on either) - keep this path exactly as simple
    // as it always was.
    gLastCompositedPostTarget = diffuse_rt;
    presentFinal(pipeline, diffuse_rt, getCurrentStereoEyeTarget(pipeline));
}

// static
void DXPipeline::presentStereoComposite(LLPipeline& pipeline)
{
    if (!pipeline.mStereoEyeL.getColorSRV(0) || !pipeline.mStereoEyeR.getColorSRV(0))
    {
        // One or both eyes never actually captured anything this frame
        // (e.g. very first frame(s) during startup) - nothing to compose,
        // leave the back buffer as DXContext::beginFrame() left it, same
        // convention as presentDeferredScreen()'s own early-out.
        return;
    }

    DXRenderTarget::bindSwapChainBackBuffer();
    // See presentFinal()'s matching comment - a raw DXRenderTarget bind
    // bypasses LLRenderTarget::bindTarget()'s own sBoundTarget bookkeeping.
    LLRenderTarget::sBoundTarget = nullptr;
    setPresentViewport();

    if (!gStereoAnaglyphProgram.isComplete())
    {
        // Shader failed to compile - nothing sane to present in this case
        // (unlike presentFinal()'s raw-placeholder fallback, there's no
        // single-eye image to fall back to that wouldn't just look like a
        // half-broken mono frame) - leave the back buffer as-is rather than
        // guess.
        return;
    }

    gStereoAnaglyphProgram.bind();
    // Bound via LLShaderMgr's existing DIFFUSE_MAP/ALTERNATE_DIFFUSE_MAP
    // reserved-uniform slots (LLHLSLShader::bindTexture(S32,...)) rather
    // than a custom texture name - see stereoAnaglyphF.hlsl's comment for
    // why the string-keyed overload doesn't work under DX_RENDER.
    gStereoAnaglyphProgram.bindTexture(LLShaderMgr::DIFFUSE_MAP, &pipeline.mStereoEyeL);
    gStereoAnaglyphProgram.bindTexture(LLShaderMgr::ALTERNATE_DIFFUSE_MAP, &pipeline.mStereoEyeR);

    // True-color red/cyan default - see stereoAnaglyphF.hlsl's comment.
    // Both diagonal, so row-vs-column-major layout is a non-issue (a
    // diagonal matrix is its own transpose) - verify this before ever
    // shipping a non-diagonal preset. 4x4, not 3x3: uniformMatrix4fv's
    // name-based overload is the only one with a real DX_RENDER upload path.
    static const F32 s_left_eye_matrix[16] = {
        1.f, 0.f, 0.f, 0.f,
        0.f, 0.f, 0.f, 0.f,
        0.f, 0.f, 0.f, 0.f,
        0.f, 0.f, 0.f, 0.f,
    };
    static const F32 s_right_eye_matrix[16] = {
        0.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 0.f,
    };
    static LLStaticHashedString s_left_matrix_name("left_eye_matrix");
    static LLStaticHashedString s_right_matrix_name("right_eye_matrix");

    gStereoAnaglyphProgram.uniformMatrix4fv(s_left_matrix_name, 1, false, s_left_eye_matrix);
    gStereoAnaglyphProgram.uniformMatrix4fv(s_right_matrix_name, 1, false, s_right_eye_matrix);

    {
        // No depth test/write - this target has no meaningful depth of its
        // own (each eye's real depth stayed in its own offscreen pass, not
        // carried into the composite). Known limitation: 3D-in-UI-space
        // content (manipulator gizmos etc.) won't depth-test correctly
        // against the composited result yet in stereo mode - see task
        // #220's memory for the full list of deferred follow-up work.
        DXDepthTest depth_test(false, false);
        pipeline.mScreenTriangleVB->setBuffer();
        pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
    }

    gStereoAnaglyphProgram.unbind();
}

// S24 : Depth Bounds Test helpers for the local-light loop below (AMD AGS /
// NVAPI - both vendors expose this, no portable D3D11 equivalent exists).
// Computes the light's hardware-depth range by replaying its view-space Z
// extent through the SAME projection matrix the GPU uses this frame, rather
// than deriving the reversed-Z/standard-Z formula by hand - depth bounds
// depend only on Z in a standard perspective matrix (the x/y terms in rows
// 2/3 are zero for both symmetric and off-center frustums), so zeroing x/y
// here is the standard technique, not an approximation. Returns false (skip
// depth bounds for this light) on any degenerate result rather than risk
// clipping real pixels.
static bool computeLightDepthBounds(const F32* world_center, F32 radius, const glm::mat4& modelview, const glm::mat4& proj, F32& out_min, F32& out_max)
{
    glm::vec4 view_center = modelview * glm::vec4(world_center[0], world_center[1], world_center[2], 1.0f);

    glm::vec4 clip0 = proj * glm::vec4(0.f, 0.f, view_center.z + radius, 1.0f);
    glm::vec4 clip1 = proj * glm::vec4(0.f, 0.f, view_center.z - radius, 1.0f);
    if (clip0.w == 0.f || clip1.w == 0.f)
    {
        return false;
    }

    F32 z0 = clip0.z / clip0.w;
    F32 z1 = clip1.z / clip1.w;
    out_min = llclamp(llmin(z0, z1), 0.f, 1.f);
    out_max = llclamp(llmax(z0, z1), 0.f, 1.f);

    return (out_max - out_min) > 0.0001f;
}

static void enableLightDepthBounds(F32 min_depth, F32 max_depth)
{
    // Dispatch on what THIS device actually is, not on whether each
    // vendor's driver merely happens to be installed somewhere on the
    // system (see wasCreatedViaAgs()'s comment - matters on hybrid
    // AMD+NVIDIA systems).
    if (gDXDevice.wasCreatedViaAgs())
    {
        agsDriverExtensionsDX11_SetDepthBounds(DXDevice::sAgsContext, gDXDevice.getContext(), true, min_depth, max_depth);
    }
    else if (DXDevice::sNvApiAvailable)
    {
        NvAPI_D3D11_SetDepthBoundsTest(gDXDevice.getContext(), 1, min_depth, max_depth);
    }
}

static void disableLightDepthBounds()
{
    if (gDXDevice.wasCreatedViaAgs())
    {
        agsDriverExtensionsDX11_SetDepthBounds(DXDevice::sAgsContext, gDXDevice.getContext(), false, 0.f, 1.f);
    }
    else if (DXDevice::sNvApiAvailable)
    {
        NvAPI_D3D11_SetDepthBoundsTest(gDXDevice.getContext(), 0, 0.f, 1.f);
    }
}

// static
void DXPipeline::renderDeferredLighting(LLPipeline& pipeline)
{
    if (!sCull)
    {
        return;
    }

    LL_RECORD_BLOCK_TIME(FTM_RENDER_DEFERRED);

    // Ambient+sun/atmospherics (softenLightF/V.hlsl) mirrors the
    // "RenderDeferredAtmospheric" block of LLPipeline::renderDeferredLighting()'s
    // GL body; the sun-shadow/SSAO lightmap pass and local lights/spotlights
    // are further down in this same function.
    //
    // Must reset every frame BEFORE any early-return below, so
    // presentDeferredScreen() never mistakes a skipped frame for a lit one
    // - see sScreenLitThisFrame's own comment.
    sScreenLitThisFrame = false;

    if (!gDeferredSoftenProgram.isComplete())
    {
        // Shader didn't compile (or isn't loaded) - leave mRT->screen
        // untouched. presentDeferredScreen() falls back to its existing
        // raw deferredScreen blit, so this is a graceful no-op, not a
        // missing frame.
        return;
    }

    // The RenderDeferredAtmospheric gate wraps ONLY the soften/ambient shader draw call itself -
    // not the sun/moon-dir
    // transform, the shadow/SSAO lightmap pass, or the local-light/
    // spotlight loops after it. Must NOT early-return the whole function
    // on this gate, or all local/projected lights vanish whenever
    // RenderDeferredAtmospheric is off.

    // NOTE: mTransformedSunDir/mTransformedMoonDir (feed shadowUtil.hlsl's
    // sun_dir/moon_dir uniforms) must be computed every call, even though
    // setupHWLights() is also called elsewhere under DX_RENDER. If left at
    // their zero-initialized default, normalize((0,0,0)) is NaN in HLSL
    // (not a zero vector), and any comparison against that NaN - including
    // sampleDirectionalShadow()'s cascade-select guard - is unconditionally
    // false per IEEE 754, silently disabling all shadow sampling.
    DXPipeline::setupHWLights(pipeline);
    {
        glm::mat4 mat = get_current_modelview();

        glm::vec4 tc(pipeline.mSunDir);
        tc = mat * tc;
        pipeline.mTransformedSunDir.set(tc);

        glm::vec4 tc_moon(pipeline.mMoonDir);
        tc_moon = mat * tc_moon;
        pipeline.mTransformedMoonDir.set(tc_moon);
    }

    // Real sun-shadow/SSAO lightmap pass, replacing an earlier neutral-white
    // stopgap (still used as the "shadows off" fallback below). Mirrors
    // GL's `if ((RenderDeferredSSAO && !gCubeSnapshot) || RenderShadowDetail
    // > 0)` block: gDeferredSunProgram/gDeferredSunProbeProgram reads
    // shadowMap0-5 (bound by bindDeferredShader()'s bindShadowMaps()) and
    // writes per-pixel shadow/spot-shadow occlusion into mRT->deferredLight.
    LLRenderTarget& deferred_light_target = pipeline.mRT->deferredLight;

    // Snapshots last frame's fully-resolved lightmap into mSSAOHistory
    // BEFORE either branch below overwrites deferred_light_target - same
    // pattern as LLPipeline::generateExposure()'s mLastExposure refresh. No
    // first-frame guard needed: mSSAOHistory is pre-seeded to (1,1,1,1) at
    // allocation time.
    if (DXPipeline::RenderDeferredSSAO && !gCubeSnapshot)
    {
        pipeline.mSSAOHistory.bindTarget();
        gCopyProgram.bind();
        gDX.getTexUnit(0)->bind(&deferred_light_target);
        pipeline.mScreenTriangleVB->setBuffer();
        pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
        pipeline.mSSAOHistory.flush();
    }

    if ((DXPipeline::RenderDeferredSSAO && !gCubeSnapshot) || DXPipeline::RenderShadowDetail > 0)
    {
        deferred_light_target.bindTarget();
        deferred_light_target.clearColor(1.0f, 1.0f, 1.0f, 1.0f);

        LLHLSLShader& sun_shader = gCubeSnapshot ? gDeferredSunProbeProgram : gDeferredSunProgram;
        if (sun_shader.isComplete())
        {
            DXPipeline::bindDeferredShader(pipeline, sun_shader, &deferred_light_target);

            sun_shader.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES,
                (F32)deferred_light_target.getWidth(),
                (F32)deferred_light_target.getHeight());

            {
                LLGLDisable   blend(GL_BLEND);
                DXDepthTest depth(true, false, GL_ALWAYS);
                LLGLDisable   cullface(GL_CULL_FACE);
                pipeline.mScreenTriangleVB->setBuffer();
                pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
            }

            DXPipeline::unbindDeferredShader(pipeline, sun_shader);
        }
        deferred_light_target.flush();
    }
    else
    {
        // Shadows AND SSAO both off: skip the real lightmap pass, same as
        // GL's early-out. Neutral "fully lit" fill - softenLightF.hlsl's
        // scol_ambocc read must never come back (0,0) ("fully shadowed/
        // occluded") when there's no real lightmap data behind it.
        deferred_light_target.bindTarget();
        deferred_light_target.clearColor(1.0f, 1.0f, 1.0f, 1.0f);
        deferred_light_target.flush();
    }

    LLRenderTarget* screen_target = &pipeline.mRT->screen;

    // Gaussian blur/soften pass for the sun-shadow/SSAO lightmap - without
    // it, calcAmbientOcclusion()'s raw per-pixel term (aoUtil.hlsl) reaches
    // softenLightF.hlsl unsmoothed; since the noise sample is keyed to
    // SCREEN position, a moving camera reads as flicker that settles once
    // it stops. Mirrors GL's gDeferredBlurLightProgram/blurLightV+F.hlsl
    // block - a depth/normal-aware bilateral blur that rejects samples
    // across depth discontinuities, so it won't smear AO across real edges.
    // Purely additive; the underlying SSAO computation is untouched.
    if (DXPipeline::RenderDeferredSSAO && !gCubeSnapshot)
    {
        screen_target->bindTarget();
        screen_target->clearColor(1.0f, 1.0f, 1.0f, 1.0f);

        DXPipeline::bindDeferredShader(pipeline, gDeferredBlurLightProgram);

        LLVector3 go = DXPipeline::RenderShadowGaussian;
        const U32 kern_length = 4;
        F32 blur_size = DXPipeline::RenderShadowBlurSize;
        F32 dist_factor = DXPipeline::RenderShadowBlurDistFactor;

        // sample symmetrically with the middle sample falling exactly on 0.0
        F32 x = 0.f;
        LLVector3 gauss[32]; // xweight, yweight, offset
        for (U32 i = 0; i < kern_length; i++)
        {
            gauss[i].mV[0] = llgaussian(x, go.mV[0]);
            gauss[i].mV[1] = llgaussian(x, go.mV[1]);
            gauss[i].mV[2] = x;
            x += 1.f;
        }

        // sDelta/sDistFactor/sKern/sKernScale match blurLightF.hlsl's uniform names exactly.
        static LLStaticHashedString sBlurDelta("delta");
        static LLStaticHashedString sBlurDistFactor("dist_factor");
        static LLStaticHashedString sBlurKern("kern");
        static LLStaticHashedString sBlurKernScale("kern_scale");

        gDeferredBlurLightProgram.uniform2f(sBlurDelta, 1.f, 0.f);
        gDeferredBlurLightProgram.uniform1f(sBlurDistFactor, dist_factor);
        gDeferredBlurLightProgram.uniform3fv(sBlurKern, kern_length, gauss[0].mV);
        gDeferredBlurLightProgram.uniform1f(sBlurKernScale, blur_size * (kern_length / 2.f - 0.5f));

        {
            LLGLDisable   blend(GL_BLEND);
            DXDepthTest depth(true, false, GL_ALWAYS);
            pipeline.mScreenTriangleVB->setBuffer();
            pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
        }

        screen_target->flush();
        DXPipeline::unbindDeferredShader(pipeline, gDeferredBlurLightProgram);

        // Second (vertical) pass: read the horizontally-blurred result back
        // from screen_target, write the final blurred lightmap into
        // deferred_light_target - matches GL's own ping-pong exactly.
        DXPipeline::bindDeferredShader(pipeline, gDeferredBlurLightProgram, screen_target);

        deferred_light_target.bindTarget();

        gDeferredBlurLightProgram.uniform2f(sBlurDelta, 0.f, 1.f);

        {
            LLGLDisable   blend(GL_BLEND);
            DXDepthTest depth(true, false, GL_ALWAYS);
            pipeline.mScreenTriangleVB->setBuffer();
            pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
        }
        deferred_light_target.flush();
        DXPipeline::unbindDeferredShader(pipeline, gDeferredBlurLightProgram);
    }

    // Temporal-resolve pass: reprojects mSSAOHistory (last frame's value)
    // into this frame's screen space and blends it with the just-blurred
    // deferred_light_target, then blits the result back so the soften pass
    // below sees the final value. Reads depth as an SRV (getPosition() in
    // temporalResolveSSAOF.hlsl), so - like the soften pass below -
    // screen_target must be bound WITHOUT its shared depth attached
    // (bindTarget(false)); binding it with depth hits the D3D11 OM/SRV
    // hazard described in that pass's comment.
    if (DXPipeline::RenderDeferredSSAO && !gCubeSnapshot)
    {
        screen_target->bindTarget(false);
        screen_target->clear(GL_COLOR_BUFFER_BIT);

        DXPipeline::bindDeferredShader(pipeline, gDeferredTemporalResolveSSAOProgram, &deferred_light_target);

        S32 history_channel = gDeferredTemporalResolveSSAOProgram.enableTexture(LLShaderMgr::DEFERRED_SSAO_HISTORY_MAP);
        if (history_channel > -1)
        {
            gDX.getTexUnit(history_channel)->bind(&pipeline.mSSAOHistory);
        }

        // Uploaded manually rather than relying on bindDeferredShader()'s
        // bindReflectionProbes() call for inv_modelview_delta -
        // bindReflectionProbes() early-returns entirely when
        // sReflectionProbesEnabled is false, which would leave this
        // shader's inv_modelview_delta uninitialized whenever reflection
        // probes are off. Same formula, computed independently so this
        // pass never depends on that unrelated toggle.
        {
            glm::mat4 cur_modelview = get_current_modelview();
            glm::mat4 last_modelview = get_last_modelview();
            glm::mat4 inv_modelview_delta = glm::inverse(cur_modelview * glm::inverse(last_modelview));
            glm::mat4 last_projection = get_last_projection();
            gDeferredTemporalResolveSSAOProgram.uniformMatrix4fv(LLShaderMgr::INVERSE_MODELVIEW_DELTA_MATRIX, 1, false, glm::value_ptr(inv_modelview_delta));
            gDeferredTemporalResolveSSAOProgram.uniformMatrix4fv(LLShaderMgr::LAST_PROJECTION_MATRIX, 1, false, glm::value_ptr(last_projection));
        }

        {
            LLGLDisable   blend(GL_BLEND);
            DXDepthTest depth(true, false, GL_ALWAYS);
            LLGLDisable   cullface(GL_CULL_FACE);
            pipeline.mScreenTriangleVB->setBuffer();
            pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
        }

        screen_target->flush();
        DXPipeline::unbindDeferredShader(pipeline, gDeferredTemporalResolveSSAOProgram);

        // Blit the resolved result back into deferred_light_target so the
        // soften pass below needs no changes.
        deferred_light_target.bindTarget();
        gCopyProgram.bind();
        gDX.getTexUnit(0)->bind(screen_target);
        pipeline.mScreenTriangleVB->setBuffer();
        pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
        deferred_light_target.flush();
    }

    // screen_target shares its depth buffer with deferredScreen, and this
    // pass reads that SAME depth buffer as an SRV via bindDeferredShader()'s
    // DEFERRED_DEPTH bind just below. D3D11 forbids a resource being bound
    // as an OM output and a shader-stage SRV input at the same time -
    // binding with the DSV attached here silently forces the depth SRV to
    // NULL, reading back as a flat 0 in softenLightF.hlsl's getDepth().
    // This pass doesn't depth-test/write anyway, so leaving the DSV unbound
    // is correct, not a workaround.
    screen_target->bindTarget(false);
    // Matches GL's screen_target->clear(GL_COLOR_BUFFER_BIT) - color only. A
    // default-mask clear() here would also clear the shared depth buffer
    // (mUseDepth is true via shareDepthBuffer()) back to its far-clear
    // value, destroying the real depth just written by the opaque pass.
    screen_target->clear(GL_COLOR_BUFFER_BIT);

    // Opaque screen-space reflections, traced before lighting so softenLight reads them.
    DXPipeline::renderSSRPass(pipeline);

    LLHLSLShader& soften_shader = gDeferredSoftenProgram;

    // Only the soften/ambient draw itself is atmospheric-gated (matches
    // GL's real scope). When off, screen_target stays at the plain clear()
    // above - local lights then additively blend onto a black base with no
    // ambient/sun contribution, not a fully blank frame.
    if (DXPipeline::RenderDeferredAtmospheric)
    {
        // NOTE: if ground ambient looks frozen regardless of the
        // Reflection Probe Ambiance slider, check llsettingsvo.cpp's
        // applySpecial() - it substitutes a fixed fallback constant for any
        // nonzero ambiance whenever DXPipeline::sReflectionProbesEnabled is
        // false. This C++/shader uniform-upload chain is not the culprit.
        DXPipeline::bindDeferredShader(pipeline, soften_shader);
        DXPipeline::bindSSRBuffer(pipeline, soften_shader);

        static LLCachedControl<F32> ssao_scale(gSavedSettings, "RenderSSAOIrradianceScale", 0.5f);
        static LLCachedControl<F32> ssao_max(gSavedSettings, "RenderSSAOIrradianceMax", 0.25f);
        static LLStaticHashedString ssao_scale_str("ssao_irradiance_scale");
        static LLStaticHashedString ssao_max_str("ssao_irradiance_max");

        soften_shader.uniform1f(ssao_scale_str, ssao_scale);
        soften_shader.uniform1f(ssao_max_str, ssao_max);

        LLEnvironment& environment = LLEnvironment::instance();

        soften_shader.uniform1i(LLShaderMgr::SUN_UP_FACTOR, environment.getIsSunUp() ? 1 : 0);
        soften_shader.uniform3fv(LLShaderMgr::LIGHTNORM, 1, environment.getClampedLightNorm().mV);

        soften_shader.uniform4fv(LLShaderMgr::WATER_WATERPLANE, 1, DXDrawPoolAlpha::sWaterPlane.mV);

        {
            DXDepthTest depth(false);
            LLGLDisable   blend(GL_BLEND);
            // Fullscreen post-process triangles shouldn't rely on ambient
            // rasterizer state (this is a raw mScreenTriangleVB->drawArrays()
            // call, not wrapped by any LLDrawPool's own state setup).
            LLGLDisable   cullface(GL_CULL_FACE);

            // full screen blit
            pipeline.mScreenTriangleVB->setBuffer();
            pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
        }

        DXPipeline::unbindDeferredShader(pipeline, soften_shader);
    }

    sScreenLitThisFrame = true;

    // NOTE: screen_target was left with no depth-stencil view attached
    // (bindTarget(false) above, for the soften pass's depth-as-SRV-while-DSV
    // hazard). The local-light loops below need DXDepthTest(true,
    // false) (test on, write off) to actually occlude against opaque
    // geometry, so it must be re-attached here - but read-only (mReadOnlyDSV
    // via rebindWithDepth(true, true)), not the normal read/write mDSV: the
    // light shaders (pointLightF.hlsl/spotLightF.hlsl) also sample this same
    // depth buffer as an SRV via bindDeferredShader()'s DEFERRED_DEPTH bind,
    // and a read/write DSV would re-trigger the OM-output-vs-SRV-input
    // hazard. Covers both the point-light and spotlight loops below (nothing
    // rebinds screen_target in between); superseded later in this function
    // by a non-read-only rebindWithDepth(true) for real 3D geometry.
    screen_target->rebindWithDepth(true, true);

    // Local point lights: mirrors GL's RenderLocalLightCount-gated block,
    // non-spotlight lights drawn as additively-blended cube-volume geometry
    // (sDXBoxLightVB, see its own comment for why this couldn't just reuse
    // GL's mCubeVB directly) via gDeferredLightProgram/pointLightV/F.hlsl.
    // Spotlights/projectors (gDeferredSpotLightProgram, see the spot_lights
    // block below) reuse setupSpotLight() directly - pure math + already-DX-safe
    // uniform/texture calls.
    {
        static LLCachedControl<S32> local_light_count(gSavedSettings, "RenderLocalLightCount", 256);
        static LLCachedControl<S32> probe_level(gSavedSettings, "RenderReflectionProbeLevel", 0);

        // GL's matching gate is `local_light_count > 0 && (!gCubeSnapshot ||
        // probe_level > 0)` - local lights must be suppressed during
        // reflection-probe capture unless probe ambiance is above 0.
        // light_scale below must also mirror GL's
        // mSphereProbes.mLightScale multiply during capture, or
        // probe captures come out double-lit/washed-out relative to GL.
        //
        // S24 : gEquirectCapture opts out of both the stale light_scale
        // read and the local-light suppression below.
        F32 light_scale = 1.f;
        if (gCubeSnapshot && !gEquirectCapture)
        {
            light_scale = pipeline.mSphereProbes.getLightScale();
        }

        if (local_light_count > 0 && (!gCubeSnapshot || probe_level > 0 || gEquirectCapture))
        {
            // GL resets both spot-shadow target slots every frame BEFORE
            // the priority competition in setupSpotLight() runs. Without
            // this, that competition (called from both the box-path and
            // fullscreen-path spotlight blocks below) compares each frame's
            // candidate against whatever won a slot on some previous frame
            // instead of a clean slate - generateSunShadow()'s fade ramp
            // drives PROJECTOR_SHADOW_FADE from this same state, so a stale
            // competition causes visible brightness swings as a projected
            // light's priority changes (e.g. on zoom-in).
            if (!gCubeSnapshot)
            {
                for (U32 i = 0; i < 2; i++)
                {
                    pipeline.mTargetShadowSpotLight[i] = nullptr;
                }
            }

            // Duplicates DEFERRED_LIGHT_FALLOFF's value locally - a fixed tuning constant that
            // never varies.
            const F32 deferred_light_falloff = 0.5f;

            LLViewerCamera*      camera = LLViewerCamera::getInstance();
            LLHLSLShader&        light_shader = gDeferredLightProgram;
            LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();

            gDX.setSceneBlendType(LLRender::BT_ADD);
            DXPipeline::bindDeferredShader(pipeline, light_shader);

            if (sDXBoxLightVB.isNull())
            {
                sDXBoxLightVB = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX);
                sDXBoxLightVB->allocateBuffer(8 * 18, 0);
                LLStrider<LLVector3> pos;
                sDXBoxLightVB->getVertexStrider(pos);
                static const LLVector3 corners[8] =
                {
                    LLVector3(-1,-1,-1), LLVector3(-1,-1, 1), LLVector3(-1, 1,-1), LLVector3(-1, 1, 1),
                    LLVector3( 1,-1,-1), LLVector3( 1,-1, 1), LLVector3( 1, 1,-1), LLVector3( 1, 1, 1),
                };
                for (int cypher = 0; cypher < 8; ++cypher)
                {
                    for (int i = 0; i < 18; ++i)
                    {
                        pos[cypher * 18 + i] = corners[sBoxLightTriangleIndices[cypher][i]];
                    }
                }
                sDXBoxLightVB->unmapBuffer();
            }
            sDXBoxLightVB->setBuffer();

            DXDepthTest depth(true, false);

            // setSceneBlendType(BT_ADD) above only sets the blend FACTORS
            // (blendFunc(ONE,ONE)) - it does not enable blending itself.
            // LLRender::applyDXBlendState() reads DXState::isEnabled(GL_BLEND)
            // to decide whether to build an enabled D3D11 blend state at
            // all; if blend was left disabled by an earlier pass (e.g. the
            // ambient pass disables it for its own fullscreen draw), every
            // light-volume draw here would REPLACE the pixel instead of
            // adding to it - only the last light drawn at a pixel would
            // show. Enable explicitly rather than trust inherited state.
            LLGLEnable blend(GL_BLEND);

            // Same reasoning as the blend fix above, for GL_CULL_FACE: this
            // 3D box-volume geometry has no explicit cull-state management
            // of its own, so it silently inherits whatever an earlier pass
            // (e.g. the opaque G-buffer pass, which legitimately enables
            // cull for solid meshes) left behind. The octant/cypher
            // technique already selects the correct visible triangles via
            // DATA (which cypher/winding to use, per camera-relative
            // octant) - it needs no runtime backface elimination, so
            // disabling cull entirely here is correct, not just a
            // convention match.
            LLGLDisable cullface(GL_CULL_FACE);

            // Collected during this same pass (mirrors GL's own spot_lights
            // vector) and rendered afterward in its own block, once
            // light_shader (gDeferredLightProgram) is unbound - see below.
            // Static to keep capacity across frames; renderDeferredLighting() runs on the main thread only.
            static std::vector<LLDrawable*> spot_lights;
            spot_lights.clear();

            // Camera-inside-light-box buckets - mirrors GL's
            // fullscreen_lights/fullscreen_spot_lights and the modelview
            // matrix used to transform their centers into view space for
            // the fullscreen multi-light/multi-spotlight shaders.
            glm::mat4 mat = get_current_modelview();
            // S24 : for Depth Bounds Test below (AMD AGS / NVAPI) - computed
            // once per frame, not per light.
            glm::mat4 proj = get_current_projection();
            static std::vector<LLVector4> fullscreen_lights;
            static std::vector<LLVector4> fullscreen_light_colors;
            static std::vector<LLDrawable*> fullscreen_spot_lights;
            fullscreen_lights.clear();
            fullscreen_light_colors.clear();
            fullscreen_spot_lights.clear();

            S32 count = 0;
            for (const auto& light : pipeline.getNearbyLights())
            {
                ++count;
                if (count > local_light_count)
                {
                    break;
                }

                LLDrawable* drawablep = light.drawable;
                LLVOVolume* volume = drawablep->getVOVolume();
                if (!volume)
                {
                    continue;
                }

                if (volume->isAttachment() && !DXPipeline::sRenderAttachedLights)
                {
                    continue;
                }

                LLVector4a center(0.f);
                center.load3(drawablep->getPositionAgent().mV);
                const F32* c = center.getF32ptr();
                F32        raw_radius = volume->getLightRadius() * 1.5f;

                LLColor3 col = volume->getLightLinearColor() * light_scale;
                if (col.magVecSquared() < 0.001f || raw_radius <= 0.001f)
                {
                    continue;
                }

                // Small decorative local lights (e.g. walkway lanterns) are
                // commonly authored with a tiny real radius - `s` feeds
                // BOTH the dist<=1.0 shader gate (multiPointLightF.hlsl/
                // pointLightF.hlsl) AND this loop's own AABB/
                // camera_outside_box tests, so a small authored radius means
                // the camera has to be almost standing inside the light
                // before it contributes any color. Floors the EFFECTIVE
                // radius used for downstream distance/falloff math to at
                // least RenderLocalLightMinRadius, while the light-is-on
                // check above still uses the TRUE raw radius (a genuinely
                // radius=0 light stays off). 0 = original behavior (no
                // floor).
                static LLCachedControl<F32> min_light_radius(gSavedSettings, "RenderLocalLightMinRadius", 15.f);
                F32 s = llmax(raw_radius, (F32)min_light_radius);

                // RenderLocalLightFrustumCulling gates both this render-loop's
                // AABB check and calcNearbyLights()'s candidate-list prefilter;
                // RenderLocalLightFrustumMargin adds a spatial margin to it.
                // Both are real, working knobs.
                static LLCachedControl<bool> frustum_culling_enabled(gSavedSettings, "RenderLocalLightFrustumCulling", true);
                static LLCachedControl<F32> frustum_margin(gSavedSettings, "RenderLocalLightFrustumMargin", 32.f);
                S32 aabb_result = 1;
                if (frustum_culling_enabled)
                {
                    LLVector4a sa(s + frustum_margin);
                    aabb_result = camera->AABBInFrustumNoFarClip(center, sa);
                }

                // Whole-light counterpart to multiPointLightF.hlsl's
                // per-pixel N.L smoothstep: aabb_result is a hard, zero-
                // margin binary test recomputed every frame from camera-
                // relative geometry, which pops under continuous camera
                // micro-motion. It's a single decision for the entire light
                // (unlike a per-pixel shader value), so a real per-light
                // temporal hold is meaningful here. RenderLocalLightFrustumCulling
                // defaults OFF, so aabb_result is 1 unconditionally for most
                // users and this hold is inert by default - it only matters
                // once frustum culling is turned on (paired with it for
                // lower-end systems: cull aggressively, hold briefly to
                // avoid flicker).
                static LLCachedControl<F32> hold_time_setting(gSavedSettings, "RenderLocalLightHoldTime", 0.30f);
                bool light_eligible = (aabb_result != 0);
                // S24: gated on frustum_culling_enabled too, not just
                // hold_time_setting - aabb_result is hardcoded to 1 (always
                // eligible) above when frustum culling is off, so this
                // block's whole purpose (rescuing a FALSE light_eligible
                // during its hold window) can never trigger in that case.
                // Previously ran this per-light sHoldStates hashmap lookup/
                // insert unconditionally every frame regardless - genuinely
                // inert bookkeeping for the ~everyone-by-default case where
                // RenderLocalLightFrustumCulling is off (see this block's own
                // comment above, which already said as much).
                if (frustum_culling_enabled && hold_time_setting > 0.f)
                {
                    struct LightHoldState
                    {
                        bool held = false;
                        F32 timeSinceIneligible = 0.f;
                        U64 lastFrameSeen = 0;
                    };
                    static std::unordered_map<const LLDrawable*, LightHoldState> sHoldStates;
                    static U64 sHoldFrame = 0;
                    ++sHoldFrame;

                    // Periodic prune (every 512 frames, drop anything not
                    // touched in the last 256) - keeps this map bounded and
                    // safe against a drawable pointer being freed and later
                    // reused by an unrelated light, without needing to hook
                    // any destruction callback.
                    if ((sHoldFrame & 0x1FF) == 0)
                    {
                        for (auto it = sHoldStates.begin(); it != sHoldStates.end(); )
                        {
                            if (sHoldFrame - it->second.lastFrameSeen > 256)
                            {
                                it = sHoldStates.erase(it);
                            }
                            else
                            {
                                ++it;
                            }
                        }
                    }

                    LightHoldState& hold = sHoldStates[drawablep];
                    hold.lastFrameSeen = sHoldFrame;
                    if (light_eligible)
                    {
                        hold.held = true;
                        hold.timeSinceIneligible = 0.f;
                    }
                    else if (hold.held)
                    {
                        hold.timeSinceIneligible += gFrameIntervalSeconds;
                        if (hold.timeSinceIneligible < (F32)hold_time_setting)
                        {
                            light_eligible = true; // still within the hold window
                        }
                        else
                        {
                            hold.held = false;
                        }
                    }
                }

                if (!light_eligible)
                {
                    continue;
                }

                DXPipeline::sVisibleLightCount++;

                // v1 drew every light through the box-mesh path
                // unconditionally, which silently drops a light whenever
                // the camera is inside its box - the box's near-camera-
                // facing triangles (selected by getBoxLightCypher()'s
                // octant test) end up behind the camera/near-plane and get
                // clipped away. Mirrors GL's camera-inside-vs-outside split:
                // outside keeps the box-mesh draw below; inside collects
                // into fullscreen_lights/fullscreen_spot_lights, drawn via
                // the fullscreen multi-light/multi-spotlight passes further
                // down. Also applies to projected/spot lights.
                LLVector3 cam_origin = camera->getOrigin();

                // NOTE: this is real Euclidean clearance, not GL's flat
                // per-axis 0.2m margin. A per-axis OR test only guarantees
                // the camera clears the box in AT LEAST ONE world axis - the
                // camera can sit deep inside 2 of 3 axes, barely past the
                // margin in the third, and still pass as "outside," at which
                // point half the box's corners land on the wrong side of the
                // camera's eye-plane and produce corrupted, screen-spanning
                // clip geometry. Scaling the margin to the light's own size
                // keeps close-in lights on the fullscreen multi-light path
                // (immune to this clip corruption) instead of ever reaching
                // the box-mesh path in this degenerate zone.
                //
                // Radius is the box's own circumscribing sphere (half-extent
                // `s` per axis -> corner distance s*sqrt(3)) plus the same
                // 0.2m margin GL uses - not an arbitrary 2x. That circumsphere
                // is the minimum radius that still fully contains the box
                // from every direction (the previous 2x overshot it,
                // unnecessarily forcing extra lights the box-mesh path could
                // safely have handled onto the fullscreen path instead -
                // itself confirmed correct this session, but this keeps
                // DX's classification as close to GL's as the degenerate-zone
                // fix allows, rather than diverging further than necessary).
                F32 dist_to_center = (cam_origin - LLVector3(c[0], c[1], c[2])).length();
                bool camera_outside_box = dist_to_center > (s * 1.7320508f + 0.2f);

                if (camera_outside_box)
                {
                    if (volume->isLightSpotlight())
                    {
                        volume->updateSpotLightPriority();
                        spot_lights.push_back(drawablep);
                        continue;
                    }

                    U32 cypher = getBoxLightCypher(camera, center);

                    light_shader.uniform3fv(LLShaderMgr::LIGHT_CENTER, 1, c);
                    light_shader.uniform1f(LLShaderMgr::LIGHT_SIZE, s);
                    light_shader.uniform3fv(LLShaderMgr::DIFFUSE_COLOR, 1, col.mV);
                    light_shader.uniform1f(LLShaderMgr::LIGHT_FALLOFF, volume->getLightFalloff(deferred_light_falloff));
                    light_shader.uniform1i(LLShaderMgr::CLASSIC_MODE, (psky && psky->canAutoAdjust()) ? 1 : 0);

                    gDX.syncMatrices();

                    F32 depth_min = 0.f, depth_max = 0.f;
                    bool bounds_set = DXDevice::sDepthBoundsTestEnabled
                        && computeLightDepthBounds(c, s, mat, proj, depth_min, depth_max);
                    if (bounds_set)
                    {
                        enableLightDepthBounds(depth_min, depth_max);
                    }

                    sDXBoxLightVB->drawArrays(LLRender::TRIANGLES, cypher * 18, 18);

                    if (bounds_set)
                    {
                        disableLightDepthBounds();
                    }
                }
                else
                {
                    if (volume->isLightSpotlight())
                    {
                        volume->updateSpotLightPriority();
                        fullscreen_spot_lights.push_back(drawablep);
                        continue;
                    }

                    glm::vec3 tc(c[0], c[1], c[2]);
                    tc = mul_mat4_vec3(mat, tc);
                    fullscreen_lights.push_back(LLVector4(tc.x, tc.y, tc.z, s));
                    fullscreen_light_colors.push_back(LLVector4(col.mV[0], col.mV[1], col.mV[2], volume->getLightFalloff(deferred_light_falloff)));
                }
            }

            DXPipeline::unbindDeferredShader(pipeline, light_shader);

            // setupSpotLight() is pure math + already-DX-safe uniform/enableTexture/bind()
            // calls, reused as-is by composition. Reuses the same box-volume draw path as the
            // point lights above (sDXBoxLightVB/getBoxLightCypher()) rather
            // than GL's separate mCubeVB TRIANGLE_FAN. Only ever holds
            // camera-OUTSIDE-box spotlights - camera-inside ones are
            // diverted to fullscreen_spot_lights above and drawn via the
            // gDeferredMultiSpotLightProgram fullscreen pass below.
            // S24: bind() safely no-ops on a shader that never compiled (logs once,
            // returns without updating sCurBoundShaderPtr or rebinding VS/PS - see
            // its own comment) - but nothing downstream of bindDeferredShader() here
            // checks for that, so a broken gDeferredSpotLightProgram would still run
            // enableTexture()/setupSpotLight()/uniform uploads and draw, all landing
            // on whatever shader was left bound from the previous pass. Guard at the
            // block level instead, matching gDeferredSoftenProgram's isComplete()
            // check at the top of this function - skip the whole block cleanly
            // rather than draw with a silently wrong shader bound.
            if (!spot_lights.empty() && gDeferredSpotLightProgram.isComplete())
            {
                DXDepthTest spot_depth(true, false);
                LLGLEnable spot_blend(GL_BLEND);

                DXPipeline::bindDeferredShader(pipeline, gDeferredSpotLightProgram);
                sDXBoxLightVB->setBuffer();
                gDeferredSpotLightProgram.enableTexture(LLShaderMgr::DEFERRED_PROJECTION);

                for (LLDrawable* drawablep : spot_lights)
                {
                    LLVOVolume* volume = drawablep->getVOVolume();

                    LLVector4a center(0.f);
                    center.load3(drawablep->getPositionAgent().mV);
                    const F32* c = center.getF32ptr();
                    F32        s = volume->getLightRadius() * 1.5f;

                    DXPipeline::sVisibleLightCount++;

                    DXPipeline::setupSpotLight(pipeline, gDeferredSpotLightProgram, drawablep);

                    LLColor3 col = volume->getLightLinearColor() * light_scale;

                    gDeferredSpotLightProgram.uniform3fv(LLShaderMgr::LIGHT_CENTER, 1, c);
                    gDeferredSpotLightProgram.uniform1f(LLShaderMgr::LIGHT_SIZE, s);
                    gDeferredSpotLightProgram.uniform3fv(LLShaderMgr::DIFFUSE_COLOR, 1, col.mV);
                    gDeferredSpotLightProgram.uniform1f(LLShaderMgr::LIGHT_FALLOFF, volume->getLightFalloff(deferred_light_falloff));
                    gDeferredSpotLightProgram.uniform1i(LLShaderMgr::CLASSIC_MODE, (psky && psky->canAutoAdjust()) ? 1 : 0);

                    gDX.syncMatrices();

                    U32 cypher = getBoxLightCypher(camera, center);

                    F32 depth_min = 0.f, depth_max = 0.f;
                    bool bounds_set = DXDevice::sDepthBoundsTestEnabled
                        && computeLightDepthBounds(c, s, mat, proj, depth_min, depth_max);
                    if (bounds_set)
                    {
                        enableLightDepthBounds(depth_min, depth_max);
                    }

                    sDXBoxLightVB->drawArrays(LLRender::TRIANGLES, cypher * 18, 18);

                    if (bounds_set)
                    {
                        disableLightDepthBounds();
                    }
                }

                gDeferredSpotLightProgram.disableTexture(LLShaderMgr::DEFERRED_PROJECTION);
                DXPipeline::unbindDeferredShader(pipeline, gDeferredSpotLightProgram);
            }

            // Fullscreen multi-light pass for lights the camera is inside
            // the box of. Mirrors GL: batch up to
            // LL_DEFERRED_MULTI_LIGHT_COUNT lights per draw, picking the
            // shader permutation compiled for exactly that many
            // (gDeferredMultiLightProgram[idx], idx = count-1). Depth test
            // off - this is a fullscreen triangle, not per-object geometry;
            // the shader reconstructs per-pixel position from the real
            // depth buffer via bindDeferredShader()'s DEFERRED_DEPTH channel.
            if (!fullscreen_lights.empty())
            {
                DXDepthTest depth(false);

                U32 fs_count = 0;
                const U32 max_count = LL_DEFERRED_MULTI_LIGHT_COUNT;
                LLVector4 light_arr[LL_DEFERRED_MULTI_LIGHT_COUNT];
                LLVector4 col_arr[LL_DEFERRED_MULTI_LIGHT_COUNT];
                F32 far_z = 0.f;

                for (size_t li = 0; li < fullscreen_lights.size(); ++li)
                {
                    light_arr[fs_count] = fullscreen_lights[li];
                    col_arr[fs_count] = fullscreen_light_colors[li];
                    far_z = llmin(light_arr[fs_count].mV[2] - light_arr[fs_count].mV[3], far_z);
                    ++fs_count;
                    if (fs_count == max_count || li + 1 == fullscreen_lights.size())
                    {
                        U32 idx = fs_count - 1;
                        DXPipeline::bindDeferredShader(pipeline, gDeferredMultiLightProgram[idx]);
                        gDeferredMultiLightProgram[idx].uniform1i(LLShaderMgr::MULTI_LIGHT_COUNT, fs_count);
                        gDeferredMultiLightProgram[idx].uniform4fv(LLShaderMgr::MULTI_LIGHT, fs_count, (F32*)light_arr);
                        gDeferredMultiLightProgram[idx].uniform4fv(LLShaderMgr::MULTI_LIGHT_COL, fs_count, (F32*)col_arr);
                        gDeferredMultiLightProgram[idx].uniform1f(LLShaderMgr::MULTI_LIGHT_FAR_Z, far_z);
                        gDeferredMultiLightProgram[idx].uniform1i(LLShaderMgr::CLASSIC_MODE, (psky && psky->canAutoAdjust()) ? 1 : 0);
                        far_z = 0.f;
                        fs_count = 0;
                        pipeline.mScreenTriangleVB->setBuffer();
                        pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
                        DXPipeline::unbindDeferredShader(pipeline, gDeferredMultiLightProgram[idx]);
                    }
                }
            }

            // Fullscreen multi-spotlight pass for spotlights the camera is
            // inside the box of. Mirrors GL: one draw per light (not
            // batched, unlike the point-light case above), via
            // gDeferredMultiSpotLightProgram.
            // S24: same isComplete() guard as the box-mesh spot block above - see its comment.
            if (!fullscreen_spot_lights.empty() && gDeferredMultiSpotLightProgram.isComplete())
            {
                DXDepthTest depth(false);

                DXPipeline::bindDeferredShader(pipeline, gDeferredMultiSpotLightProgram);
                gDeferredMultiSpotLightProgram.enableTexture(LLShaderMgr::DEFERRED_PROJECTION);
                pipeline.mScreenTriangleVB->setBuffer();

                for (LLDrawable* drawablep : fullscreen_spot_lights)
                {
                    LLVOVolume* volume = drawablep->getVOVolume();
                    LLVector3   sl_center = drawablep->getPositionAgent();
                    F32         light_size_final = volume->getLightRadius() * 1.5f;
                    F32         light_falloff_final = volume->getLightFalloff(deferred_light_falloff);

                    DXPipeline::sVisibleLightCount++;

                    glm::vec3 tc(sl_center.mV[0], sl_center.mV[1], sl_center.mV[2]);
                    tc = mul_mat4_vec3(mat, tc);
                    F32 tc_arr[3] = { tc.x, tc.y, tc.z };

                    DXPipeline::setupSpotLight(pipeline, gDeferredMultiSpotLightProgram, drawablep);

                    LLColor3 col = volume->getLightLinearColor() * light_scale;

                    gDeferredMultiSpotLightProgram.uniform3fv(LLShaderMgr::LIGHT_CENTER, 1, tc_arr);
                    gDeferredMultiSpotLightProgram.uniform1f(LLShaderMgr::LIGHT_SIZE, light_size_final);
                    gDeferredMultiSpotLightProgram.uniform3fv(LLShaderMgr::DIFFUSE_COLOR, 1, col.mV);
                    gDeferredMultiSpotLightProgram.uniform1f(LLShaderMgr::LIGHT_FALLOFF, light_falloff_final);
                    gDeferredMultiSpotLightProgram.uniform1i(LLShaderMgr::CLASSIC_MODE, (psky && psky->canAutoAdjust()) ? 1 : 0);

                    pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
                }

                gDeferredMultiSpotLightProgram.disableTexture(LLShaderMgr::DEFERRED_PROJECTION);
                DXPipeline::unbindDeferredShader(pipeline, gDeferredMultiSpotLightProgram);
            }
        }
    }

    // Calls renderGeomPostDeferred() here, before this function's own
    // final flush() below, mirroring GL's structure exactly: GL calls it
    // from inside renderDeferredLighting() with screen_target still bound,
    // then flushes once. This draws into the still-bound mRT->screen (the
    // lit result), so alpha/water/fullbright/glow content reaches the
    // world scene without separate re-bind/flush plumbing in display().
    //
    // screen_target was bound with bind_depth=false above (see that
    // bindTarget() call's comment) to avoid the depth-as-SRV-while-DSV
    // hazard for the ambient pass's fullscreen draw - but that leaves no
    // depth-stencil view attached, so the real 3D alpha/fullbright/glow
    // geometry drawn here needs depth re-attached via rebindWithDepth()
    // (not a second bindTarget() call, which would trip its "not already
    // bound" assert) to depth-test correctly against opaque geometry.
    screen_target->rebindWithDepth(true);
    DXPipeline::renderGeomPostDeferred(pipeline, *LLViewerCamera::getInstance());

    // LLRenderTarget::flush() restores the previously-bound render target,
    // so it must run exactly once, after BOTH the ambient pass and the
    // local-lights loop (plus the post-deferred pass above) are done
    // writing to screen_target - unconditionally, since the ambient pass
    // always writes to it regardless of whether local_light_count gates
    // the local-lights block off.
    screen_target->flush();

    // Advances gDXLastModelView/gDXLastProjection to this frame's real
    // world camera, here at the single true end of the world-camera scene
    // render - NOT inside renderGeomPostDeferred(), which also runs from
    // an early world-camera pass and from render_hud_attachments() (HUD
    // camera). Doing it in either of those places clobbers the value SSAO's
    // temporal resolve and SSR depend on for reprojecting off the previous
    // frame's real world camera.
    if (!gCubeSnapshot)
    {
        for (U32 i = 0; i < 16; i++)
        {
            gDXLastModelView[i] = gDXModelView[i];
            gDXLastProjection[i] = gDXProjection[i];
        }
    }
}

void DXPipeline::bindSMAAStaticSamplers()
{
    DXSampler::bindStatic(13, 2, 1);
    DXSampler::bindStatic(14, 2, 0);
}

// ============================================================================
// Post-processing chain (buffer visualization, color grading, tonemap,
// sharpening, AA, glow)
// ============================================================================

// static
void DXPipeline::visualizeBuffers(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst, U32 bufferIndex, bool blend_vertical)
{
    // Lazily compiled on first actual use instead of eagerly at startup - see
    // LLViewerShaderMgr::loadShaderBufferVisualization()'s comment for why. isComplete() stays false if
    // compilation fails, so this retries on every call while the debug view is left open; LL_WARNS_ONCE
    // keeps that from spamming the log.
    if (!gDeferredBufferVisualProgram.isComplete())
    {
        if (!LLViewerShaderMgr::instance()->loadShaderBufferVisualization())
        {
            LL_WARNS_ONCE("Shader") << "Failed to compile Deferred Buffer Visualization Shader - Develop > Rendering > Buffer Visualization will show nothing" << LL_ENDL;
            return;
        }
    }

    dst->bindTarget();
    gDeferredBufferVisualProgram.bind();
    gDeferredBufferVisualProgram.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, src, false, LLTexUnit::TFO_BILINEAR, bufferIndex);

    static LLStaticHashedString mipLevel("mipLevel");
    if (DXPipeline::RenderBufferVisualization != 4)
        gDeferredBufferVisualProgram.uniform1f(mipLevel, 0);
    else
        gDeferredBufferVisualProgram.uniform1f(mipLevel, 8);

    static LLStaticHashedString showBlendVertical("showBlendVertical");
    gDeferredBufferVisualProgram.uniform1f(showBlendVertical, blend_vertical ? 1.f : 0.f);

    pipeline.mScreenTriangleVB->setBuffer();
    pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
    gDeferredBufferVisualProgram.unbind();
    dst->flush();
}

// static
void DXPipeline::applyKrlvCamDrawFog(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst,
    F32 minDist, F32 maxDist, F32 minAlpha, F32 maxAlpha,
    F32 fogColorR, F32 fogColorG, F32 fogColorB,
    bool camTexturesActive, LLViewerFetchedTexture* camTexturesSubstitute)
{
    dst->bindTarget();

    gKrlvCamDrawFogProgram.bind();

    gKrlvCamDrawFogProgram.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, src);
    gKrlvCamDrawFogProgram.bindTexture(LLShaderMgr::DEFERRED_DEPTH, &pipeline.mRT->deferredScreen, true);

    static LLStaticHashedString sKrlvFogMinDist("krlv_fog_min_dist");
    static LLStaticHashedString sKrlvFogMaxDist("krlv_fog_max_dist");
    static LLStaticHashedString sKrlvFogMinAlpha("krlv_fog_min_alpha");
    static LLStaticHashedString sKrlvFogMaxAlpha("krlv_fog_max_alpha");
    static LLStaticHashedString sKrlvFogColor("krlv_fog_color");
    static LLStaticHashedString sKrlvCamTexturesActive("krlv_camtextures_active");

    gKrlvCamDrawFogProgram.uniform1f(sKrlvFogMinDist, minDist);
    gKrlvCamDrawFogProgram.uniform1f(sKrlvFogMaxDist, maxDist);
    gKrlvCamDrawFogProgram.uniform1f(sKrlvFogMinAlpha, minAlpha);
    gKrlvCamDrawFogProgram.uniform1f(sKrlvFogMaxAlpha, maxAlpha);
    gKrlvCamDrawFogProgram.uniform3f(sKrlvFogColor, fogColorR, fogColorG, fogColorB);

    // KRLV_TOUCHPOINT: @camtextures/@setcam_textures - see this function's
    // header comment (dxpipeline.h) and krlv/README.md's Camera section.
    gKrlvCamDrawFogProgram.uniform1i(sKrlvCamTexturesActive, camTexturesActive ? 1 : 0);
    if (camTexturesActive)
    {
        gKrlvCamDrawFogProgram.bindTexture(LLShaderMgr::KRLV_CAMTEXTURES_STENCIL, &pipeline.mRT->deferredScreen, false, LLTexUnit::TFO_BILINEAR, 0, true);
        LLViewerFetchedTexture* substitute = camTexturesSubstitute ? camTexturesSubstitute : LLViewerFetchedTexture::sDefaultImagep.get();
        gKrlvCamDrawFogProgram.bindTexture(LLShaderMgr::KRLV_CAMTEXTURES_SUBSTITUTE, substitute);
    }

    pipeline.mScreenTriangleVB->setBuffer();
    pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

    gKrlvCamDrawFogProgram.unbind();

    dst->flush();
}

// static
void DXPipeline::gammaCorrect(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst)
{
    dst->bindTarget();
    {
        DXDepthTest depth(false, false);

        // S24: buildNoPost is declared but never read anywhere in this
        // function (likely a tonemap()-sibling copy-paste leftover, see its
        // own no_post logic) - kept as-is, pure move, not a fix. Candidate
        // for a separate cleanup step.
        static LLCachedControl<bool> buildNoPost(gSavedSettings, "RenderDisablePostProcessing", false);
        static LLCachedControl<bool> should_auto_adjust(gSavedSettings, "RenderSkyAutoAdjustLegacy", false);

        LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();
        LLHLSLShader& shader = psky->getReflectionProbeAmbiance(should_auto_adjust) == 0.f ? gLegacyPostGammaCorrectProgram :
            gDeferredPostGammaCorrectProgram;

        shader.bind();
        shader.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, src, false, LLTexUnit::TFO_POINT);
        shader.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (F32)src->getWidth(), (F32)src->getHeight());

        pipeline.mScreenTriangleVB->setBuffer();
        pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

        shader.unbind();
    }
    dst->flush();
}

// static
void DXPipeline::applyCAS(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst)
{
    static LLCachedControl<F32> cas_sharpness(gSavedSettings, "RenderCASSharpness", 0.4f);
    if (cas_sharpness == 0.0f || !gCASProgram.isComplete() || !gCASLegacyGammaProgram.isComplete())
    {
        DXPipeline::copyRenderTarget(pipeline, src, dst);
        return;
    }

    LLHLSLShader* sharpen_shader = &gCASProgram;
    static LLCachedControl<bool> should_auto_adjust(gSavedSettings, "RenderSkyAutoAdjustLegacy", false);

    LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();
    bool legacy_gamma = psky->getReflectionProbeAmbiance(should_auto_adjust) == 0.f;
    if (legacy_gamma)
    {
        sharpen_shader = &gCASLegacyGammaProgram;
    }

    // Bind setup:
    dst->bindTarget();

    sharpen_shader->bind();

    {
        static LLStaticHashedString cas_param_1("cas_param_1");
        static LLStaticHashedString out_screen_res("out_screen_res");

        // const0 is only CasSetup's out-parameter; CASF.hlsl never reads cas_param_0.
        varAU4(const0);
        varAU4(const1);
        CasSetup(const0, const1,
            cas_sharpness(),             // Sharpness tuning knob (0.0 to 1.0).
            (AF1)src->getWidth(), (AF1)src->getHeight(),  // Input size.
            (AF1)dst->getWidth(), (AF1)dst->getHeight()); // Output size.

        sharpen_shader->uniform4uiv(cas_param_1, 1, const1);

        sharpen_shader->uniform2f(out_screen_res, (AF1)dst->getWidth(), (AF1)dst->getHeight());
    }

    sharpen_shader->bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, src, false, LLTexUnit::TFO_POINT);

    // Draw
    pipeline.mScreenTriangleVB->setBuffer();
    pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

    sharpen_shader->unbind();

    dst->flush();
}

// static
void DXPipeline::applyFXAA(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst)
{
    llassert(!gCubeSnapshot);
    bool multisample = DXPipeline::RenderFSAAType == 1 && gFXAAProgram[0].isComplete() && pipeline.mFXAAMap.isComplete();

    // Present everything.
    if (multisample)
    {
        S32 width = dst->getWidth();
        S32 height = dst->getHeight();

        // bake out texture2D with RGBL for FXAA shader
        pipeline.mFXAAMap.bindTarget();
        pipeline.mFXAAMap.clear(GL_COLOR_BUFFER_BIT);

        LLHLSLShader* shader = &gGlowCombineFXAAProgram;
        shader->bind();

        S32 channel = shader->enableTexture(LLShaderMgr::DEFERRED_DIFFUSE, src->getUsage());
        if (channel > -1)
        {
            src->bindTexture(0, channel, LLTexUnit::TFO_BILINEAR);
        }

        {
            DXDepthTest depth_test(true, true, GL_ALWAYS);
            pipeline.mScreenTriangleVB->setBuffer();
            pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
        }

        shader->disableTexture(LLShaderMgr::DEFERRED_DIFFUSE, src->getUsage());
        shader->unbind();

        pipeline.mFXAAMap.flush();

        dst->bindTarget();

        static LLCachedControl<U32> aa_quality(gSavedSettings, "RenderFSAASamples", 0U);
        U32 fsaa_quality = std::clamp(aa_quality(), 0U, 3U);

        shader = &gFXAAProgram[fsaa_quality];
        shader->bind();

        channel = shader->enableTexture(LLShaderMgr::DIFFUSE_MAP, pipeline.mFXAAMap.getUsage());
        if (channel > -1)
        {
            pipeline.mFXAAMap.bindTexture(0, channel, LLTexUnit::TFO_BILINEAR);
        }

        gDXViewport[0] = gViewerWindow->getWorldViewRectRaw().mLeft;
        gDXViewport[1] = gViewerWindow->getWorldViewRectRaw().mBottom;
        gDXViewport[2] = gViewerWindow->getWorldViewRectRaw().getWidth();
        gDXViewport[3] = gViewerWindow->getWorldViewRectRaw().getHeight();

        // dst->bindTarget() above already set a D3D11 viewport of (0,0,dst width,dst height). dst is
        // always one of mPostPingMap/mPostPongMap, both allocated at exactly
        // getWorldViewWidthRaw()/HeightRaw() with implicit (0,0) origin - the same dimensions
        // worldViewRectRaw describes, so this GL viewport
        // override (which additionally offsets by mLeft/mBottom, relevant
        // only when binding the real window framebuffer, not an offscreen
        // RT) is redundant under DX_RENDER. Same reasoning as renderDoF()'s dst-viewport call.
        F32 scale_x = (F32)width / pipeline.mFXAAMap.getWidth();
        F32 scale_y = (F32)height / pipeline.mFXAAMap.getHeight();
        shader->uniform2f(LLShaderMgr::FXAA_TC_SCALE, scale_x, scale_y);
        shader->uniform2f(LLShaderMgr::FXAA_RCP_SCREEN_RES, 1.f / width * scale_x, 1.f / height * scale_y);
        shader->uniform4f(LLShaderMgr::FXAA_RCP_FRAME_OPT, -0.5f / width * scale_x, -0.5f / height * scale_y,
            0.5f / width * scale_x, 0.5f / height * scale_y);
        shader->uniform4f(LLShaderMgr::FXAA_RCP_FRAME_OPT2, -2.f / width * scale_x, -2.f / height * scale_y,
            2.f / width * scale_x, 2.f / height * scale_y);

        {
            DXDepthTest depth_test(true, true, GL_ALWAYS);
            S32 depth_channel = shader->getTextureChannel(LLShaderMgr::DEFERRED_DEPTH);
            gDX.getTexUnit(depth_channel)->bind(&pipeline.mRT->deferredScreen, true);

            pipeline.mScreenTriangleVB->setBuffer();
            pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
        }

        shader->unbind();
        dst->flush();
    }
    else
    {
        DXPipeline::copyRenderTarget(pipeline, src, dst);
    }
}

// static
void DXPipeline::copyScreenSpaceReflections(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst)
{
    if (DXPipeline::RenderScreenSpaceReflections && !gCubeSnapshot)
    {
        DXDepthTest depth(true, true, GL_ALWAYS);

        LLRenderTarget& depth_src = pipeline.mRT->deferredScreen;

        dst->bindTarget();
        dst->clear();
        gCopyDepthProgram.bind();

        S32 diff_map = gCopyDepthProgram.getTextureChannel(LLShaderMgr::DIFFUSE_MAP);
        S32 depth_map = gCopyDepthProgram.getTextureChannel(LLShaderMgr::DEFERRED_DEPTH);

        gDX.getTexUnit(diff_map)->bind(src);
        gDX.getTexUnit(depth_map)->bind(&depth_src, true);

        pipeline.mScreenTriangleVB->setBuffer();
        pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

        dst->flush();
    }
}

// static
void DXPipeline::generateLuminance(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst)
{
    // luminance sample and mipmap generation
    {
        dst->bindTarget();
        DXDepthTest depth(false, false);
        gLuminanceProgram.bind();
        static LLCachedControl<F32> diffuse_luminance_scale(gSavedSettings, "RenderDiffuseLuminanceScale", 1.0f);

        S32 channel = 0;
        channel = gLuminanceProgram.enableTexture(LLShaderMgr::DEFERRED_DIFFUSE);
        if (channel > -1) { src->bindTexture(0, channel, LLTexUnit::TFO_POINT); }

        channel = gLuminanceProgram.enableTexture(LLShaderMgr::DEFERRED_EMISSIVE);
        if (channel > -1) { pipeline.mGlow[1].bindTexture(0, channel); }

        channel = gLuminanceProgram.enableTexture(LLShaderMgr::NORMAL_MAP);
        if (channel > -1) { pipeline.mRT->deferredScreen.bindTexture(2, channel, LLTexUnit::TFO_POINT); }

        static LLStaticHashedString diffuse_luminance_scale_s("diffuse_luminance_scale");
        gLuminanceProgram.uniform1f(diffuse_luminance_scale_s, diffuse_luminance_scale);

        pipeline.mScreenTriangleVB->setBuffer();
        pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

        dst->flush();
        gLuminanceProgram.unbind();
    }
}

// static
void DXPipeline::generateExposure(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst, bool use_history)
{
    // exposure sample
    {
        if (use_history)
        {
            pipeline.mLastExposure.bindTarget();
            gCopyProgram.bind();
            gDX.getTexUnit(0)->bind(dst);
            pipeline.mScreenTriangleVB->setBuffer();
            pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
            pipeline.mLastExposure.flush();
        }

        dst->bindTarget();
        DXDepthTest depth(false, false);

        LLHLSLShader* shader = nullptr;
        if (use_history) { shader = &gExposureProgram; }
        else { shader = &gExposureProgramNoFade; }

        shader->bind();

        S32 channel = shader->enableTexture(LLShaderMgr::DEFERRED_EMISSIVE);
        if (channel > -1) { src->bindTexture(0, channel, LLTexUnit::TFO_TRILINEAR); }

        if (use_history)
        {
            channel = shader->enableTexture(LLShaderMgr::EXPOSURE_MAP);
            if (channel > -1) { pipeline.mLastExposure.bindTexture(0, channel); }
        }

        static LLStaticHashedString dt("dt");
        static LLStaticHashedString noiseVec("noiseVec");
        static LLStaticHashedString dynamic_exposure_params("dynamic_exposure_params");
        static LLStaticHashedString dynamic_exposure_params2("dynamic_exposure_params2");
        static LLStaticHashedString dynamic_exposure_e("dynamic_exposure_enabled");

        static LLCachedControl<bool> should_auto_adjust(gSavedSettings, "RenderSkyAutoAdjustLegacy", false);
        static LLCachedControl<bool> dynamic_exposure_enabled(gSavedSettings, "RenderDynamicExposureEnabled", true);
        static LLCachedControl<F32> dynamic_exposure_coefficient(gSavedSettings, "RenderDynamicExposureCoefficient", 0.175f);
        static LLCachedControl<F32> dynamic_exposure_speed_error(gSavedSettings, "RenderDynamicExposureSpeedError", 0.1f);
        static LLCachedControl<F32> dynamic_exposure_speed_target(gSavedSettings, "RenderDynamicExposureSpeedTarget", 2.f);

        LLSettingsSky::ptr_t sky = LLEnvironment::instance().getCurrentSky();
        F32 probe_ambiance = LLEnvironment::instance().getCurrentSky()->getReflectionProbeAmbiance(should_auto_adjust());

        F32 exp_min = 1.f;
        F32 exp_max = 1.f;

        static LLCachedControl<bool> use_exposure_sky_settings(gSavedSettings, "RenderUseExposureSkySettings", false);
        if (use_exposure_sky_settings)
        {
            if (dynamic_exposure_enabled)
            {
                exp_min = sky->getHDROffset(should_auto_adjust()) - sky->getHDRMin(should_auto_adjust());
                exp_max = sky->getHDROffset(should_auto_adjust()) + sky->getHDRMax(should_auto_adjust());
            }
            else
            {
                exp_min = sky->getHDROffset(should_auto_adjust());
                exp_max = sky->getHDROffset(should_auto_adjust());
            }
        }
        else if (dynamic_exposure_enabled)
        {
            if (probe_ambiance > 0.f)
            {
                F32 hdr_scale = sqrtf(LLEnvironment::instance().getCurrentSky()->getGamma()) * 2.f;

                if (hdr_scale > 1.f)
                {
                    exp_min = 1.f / hdr_scale;
                    exp_max = hdr_scale;
                }
            }
        }

        shader->uniform1f(dt, gFrameIntervalSeconds);
        shader->uniform2f(noiseVec, ll_frand() * 2.0f - 1.0f, ll_frand() * 2.0f - 1.0f);
        shader->uniform4f(dynamic_exposure_params, dynamic_exposure_coefficient, exp_min, exp_max, dynamic_exposure_speed_error);
        shader->uniform4f(dynamic_exposure_params2, sky->getHDROffset(should_auto_adjust()), exp_min, exp_max, dynamic_exposure_speed_target);

        pipeline.mScreenTriangleVB->setBuffer();
        pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

        if (use_history)
        {
            gDX.getTexUnit(channel)->unbind(pipeline.mLastExposure.getUsage());
        }
        shader->unbind();
        dst->flush();
    }
}

// static
void DXPipeline::copyRenderTarget(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst)
{
    dst->bindTarget();

    gDeferredPostNoDoFProgram.bind();

    gDeferredPostNoDoFProgram.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, src);
    gDeferredPostNoDoFProgram.bindTexture(LLShaderMgr::DEFERRED_DEPTH, &pipeline.mRT->deferredScreen, true);

    pipeline.mScreenTriangleVB->setBuffer();
    pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

    gDeferredPostNoDoFProgram.unbind();

    dst->flush();
}

// static
bool DXPipeline::applyFSR1Easu(LLPipeline& pipeline, LLRenderTarget* src, U32 dest_width, U32 dest_height)
{
    if (!gFSR1EasuProgram.isComplete())
    {
        return false;
    }

    gFSR1EasuProgram.bind();

    {
        // Hand-rolled FsrEasuCon() (ffx_fsr1.h) - same "not worth vendoring a
        // second AMD portability file" call as applyFSR1Rcas()'s FsrRcasCon().
        // Viewport-in-pixels and size-in-pixels are the same here (this
        // project doesn't support an input texture larger than the viewport
        // being upscaled, unlike AMD's dynamic-resolution-scaling case).
        F32 input_w = (F32)src->getWidth();
        F32 input_h = (F32)src->getHeight();
        F32 output_w = (F32)dest_width;
        F32 output_h = (F32)dest_height;

        U32 con0[4] = {}, con1[4] = {}, con2[4] = {}, con3[4] = {};

        auto bits = [](F32 f) -> U32 {
            U32 u;
            static_assert(sizeof(U32) == sizeof(F32), "FSR1 EASU bit-reinterpret assumes 32-bit float/uint");
            memcpy(&u, &f, sizeof(U32));
            return u;
        };

        con0[0] = bits(input_w / output_w);
        con0[1] = bits(input_h / output_h);
        con0[2] = bits(0.5f * input_w / output_w - 0.5f);
        con0[3] = bits(0.5f * input_h / output_h - 0.5f);

        con1[0] = bits(1.0f / input_w);
        con1[1] = bits(1.0f / input_h);
        con1[2] = bits(1.0f / input_w);
        con1[3] = bits(-1.0f / input_h);

        con2[0] = bits(-1.0f / input_w);
        con2[1] = bits(2.0f / input_h);
        con2[2] = bits(1.0f / input_w);
        con2[3] = bits(2.0f / input_h);

        con3[0] = bits(0.0f);
        con3[1] = bits(4.0f / input_h);
        con3[2] = 0;
        con3[3] = 0;

        static LLStaticHashedString easu_const0("easu_const0");
        static LLStaticHashedString easu_const1("easu_const1");
        static LLStaticHashedString easu_const2("easu_const2");
        static LLStaticHashedString easu_const3("easu_const3");
        static LLStaticHashedString out_screen_res("out_screen_res");
        gFSR1EasuProgram.uniform4uiv(easu_const0, 1, con0);
        gFSR1EasuProgram.uniform4uiv(easu_const1, 1, con1);
        gFSR1EasuProgram.uniform4uiv(easu_const2, 1, con2);
        gFSR1EasuProgram.uniform4uiv(easu_const3, 1, con3);
        // Lets the shader recover a viewport-relative output pixel position
        // from vary_fragcoord (0-1) instead of SV_Position, which is in
        // absolute backbuffer coordinates and would be offset by however much
        // window chrome setPresentViewport() excluded - see EASUF.hlsl's
        // main() comment.
        gFSR1EasuProgram.uniform2f(out_screen_res, output_w, output_h);
    }

    gFSR1EasuProgram.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, src, false, LLTexUnit::TFO_BILINEAR);

    pipeline.mScreenTriangleVB->setBuffer();
    pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

    gFSR1EasuProgram.unbind();

    return true;
}

// static
void DXPipeline::applyFSR1Rcas(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst)
{
    static LLCachedControl<F32> rcas_sharpness(gSavedSettings, "RenderFSR1Sharpness", 0.4f);
    if (rcas_sharpness == 0.0f || !gFSR1RcasProgram.isComplete() || !gFSR1RcasLegacyGammaProgram.isComplete())
    {
        DXPipeline::copyRenderTarget(pipeline, src, dst);
        return;
    }

    LLHLSLShader* sharpen_shader = &gFSR1RcasProgram;
    static LLCachedControl<bool> should_auto_adjust(gSavedSettings, "RenderSkyAutoAdjustLegacy", false);

    LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();
    bool legacy_gamma = psky->getReflectionProbeAmbiance(should_auto_adjust) == 0.f;
    if (legacy_gamma)
    {
        sharpen_shader = &gFSR1RcasLegacyGammaProgram;
    }

    // Bind setup:
    dst->bindTarget();

    sharpen_shader->bind();

    {
        static LLStaticHashedString rcas_param("rcas_param");

        // Hand-rolled FsrRcasCon() (ffx_fsr1.h) - not worth vendoring a whole
        // second AMD portability file for two lines of real math. AMD's
        // convention is "stops of reduction from max sharpness" (0.0 =
        // sharpest, higher = less sharp); RenderFSR1Sharpness instead follows
        // RenderCASSharpness's own 0(off)-1(max) convention for UI
        // consistency, mapped onto a 0-2 stops range here so the slider's max
        // reaches FsrRcasCon's sharpest setting.
        F32 stops = (1.0f - rcas_sharpness()) * 2.0f;
        F32 linear_sharpness = exp2f(-stops);
        U32 const0 = 0;
        static_assert(sizeof(U32) == sizeof(F32), "FSR1 RCAS bit-reinterpret assumes 32-bit float/uint");
        memcpy(&const0, &linear_sharpness, sizeof(U32));

        U32 const1[4] = { const0, 0, 0, 0 };
        sharpen_shader->uniform4uiv(rcas_param, 1, const1);

        static LLStaticHashedString out_screen_res("out_screen_res");
        sharpen_shader->uniform2f(out_screen_res, (F32)src->getWidth(), (F32)src->getHeight());
    }

    sharpen_shader->bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, src, false, LLTexUnit::TFO_POINT);

    // Draw
    pipeline.mScreenTriangleVB->setBuffer();
    pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

    sharpen_shader->unbind();

    dst->flush();
}

// static
bool DXPipeline::applyFSR1RcasToBoundTarget(LLPipeline& pipeline, LLRenderTarget* src)
{
    static LLCachedControl<F32> rcas_sharpness(gSavedSettings, "RenderFSR1Sharpness", 0.4f);
    if (rcas_sharpness() == 0.0f || !gFSR1RcasProgram.isComplete() || !gFSR1RcasLegacyGammaProgram.isComplete())
    {
        return false;
    }

    LLHLSLShader* sharpen_shader = &gFSR1RcasProgram;
    static LLCachedControl<bool> should_auto_adjust(gSavedSettings, "RenderSkyAutoAdjustLegacy", false);

    LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();
    bool legacy_gamma = psky->getReflectionProbeAmbiance(should_auto_adjust) == 0.f;
    if (legacy_gamma)
    {
        sharpen_shader = &gFSR1RcasLegacyGammaProgram;
    }

    sharpen_shader->bind();

    {
        static LLStaticHashedString rcas_param("rcas_param");

        // Same hand-rolled FsrRcasCon() as applyFSR1Rcas() above.
        F32 stops = (1.0f - rcas_sharpness()) * 2.0f;
        F32 linear_sharpness = exp2f(-stops);
        U32 const0 = 0;
        memcpy(&const0, &linear_sharpness, sizeof(U32));

        U32 const1[4] = { const0, 0, 0, 0 };
        sharpen_shader->uniform4uiv(rcas_param, 1, const1);

        static LLStaticHashedString out_screen_res("out_screen_res");
        sharpen_shader->uniform2f(out_screen_res, (F32)src->getWidth(), (F32)src->getHeight());
    }

    sharpen_shader->bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, src, false, LLTexUnit::TFO_POINT);

    pipeline.mScreenTriangleVB->setBuffer();
    pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

    sharpen_shader->unbind();

    return true;
}

// static
void DXPipeline::generateGlow(LLPipeline& pipeline, LLRenderTarget* src)
{
    if (DXPipeline::sRenderGlow)
    {
        pipeline.mGlow[2].bindTarget();
        pipeline.mGlow[2].clear();

        gGlowExtractProgram.bind();
        F32 maxAlpha = DXPipeline::RenderGlowMaxExtractAlpha;
        F32 warmthAmount = DXPipeline::RenderGlowWarmthAmount;
        LLVector3 lumWeights = DXPipeline::RenderGlowLumWeights;
        LLVector3 warmthWeights = DXPipeline::RenderGlowWarmthWeights;

        gGlowExtractProgram.uniform1f(LLShaderMgr::GLOW_MIN_LUMINANCE, 9999);
        gGlowExtractProgram.uniform1f(LLShaderMgr::GLOW_MAX_EXTRACT_ALPHA, maxAlpha);
        gGlowExtractProgram.uniform3f(LLShaderMgr::GLOW_LUM_WEIGHTS, lumWeights.mV[0], lumWeights.mV[1],
            lumWeights.mV[2]);
        gGlowExtractProgram.uniform3f(LLShaderMgr::GLOW_WARMTH_WEIGHTS, warmthWeights.mV[0], warmthWeights.mV[1],
            warmthWeights.mV[2]);
        gGlowExtractProgram.uniform1f(LLShaderMgr::GLOW_WARMTH_AMOUNT, warmthAmount);

        if (DXPipeline::RenderGlowNoise)
        {
            S32 channel = gGlowExtractProgram.enableTexture(LLShaderMgr::GLOW_NOISE_MAP);
            if (channel > -1)
            {
                gDX.getTexUnit(channel)->bind(pipeline.mDXTrueNoiseMap, LLTexUnit::TAM_WRAP, LLTexUnit::TFO_POINT);
            }
            gGlowExtractProgram.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES,
                (F32)pipeline.mGlow[2].getWidth(),
                (F32)pipeline.mGlow[2].getHeight());
        }

        {
            LLGLEnable blend_on(GL_BLEND);

            gDX.setSceneBlendType(LLRender::BT_ADD_WITH_ALPHA);

            gGlowExtractProgram.bindTexture(LLShaderMgr::DIFFUSE_MAP, src);

            gDX.color4f(1, 1, 1, 1);
            DXPipeline::enableLightsFullbright(pipeline);

            pipeline.mScreenTriangleVB->setBuffer();
            pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

            pipeline.mGlow[2].flush();
        }

        gGlowExtractProgram.unbind();

        // power of two between 1 and 1024
        U32 glowResPow = DXPipeline::RenderGlowResolutionPow;
        const U32 glow_res = llmax(1, llmin(1024, 1 << glowResPow));

        S32 kernel = DXPipeline::RenderGlowIterations * 2;
        F32 delta = DXPipeline::RenderGlowWidth / glow_res;
        // Use half the glow width if we have the res set to less than 9 so that it looks
        // almost the same in either case.
        if (glowResPow < 9)
        {
            delta *= 0.5f;
        }
        F32 strength = DXPipeline::RenderGlowStrength;
        // S24 Version 1 - more effcient
        gGlowProgram.bind();
        gGlowProgram.uniform1f(LLShaderMgr::GLOW_STRENGTH, strength);

        for (S32 i = 0; i < kernel; ++i)
        {
            const S32 pass = i & 1;          // 0 or 1
            const S32 prev_pass = (i - 1) & 1;    // 1 or 0

            // Bind target ping or pong
            pipeline.mGlow[pass].bindTarget();
            pipeline.mGlow[pass].clear();

            // Bind input texture
            LLRenderTarget* input =
                (i == 0) ? &pipeline.mGlow[2] : &pipeline.mGlow[prev_pass];

            gGlowProgram.bindTexture(LLShaderMgr::DIFFUSE_MAP, input);

            // Horizontal on even, vertical on odd
            const bool horizontal = (pass == 0);
            gGlowProgram.uniform2f(
                LLShaderMgr::GLOW_DELTA,
                horizontal ? delta : 0.f,
                horizontal ? 0.f : delta
            );

            pipeline.mScreenTriangleVB->setBuffer();
            pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

            pipeline.mGlow[pass].flush();
        }

        gGlowProgram.unbind();
    }
    else // !sRenderGlow, skip the glow ping-pong and just clear the result target
    {
        pipeline.mGlow[1].bindTarget();
        pipeline.mGlow[1].clear();
        pipeline.mGlow[1].flush();
    }
}

// static
void DXPipeline::combineGlow(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst)
{
    // Go ahead and do our glow combine here in our destination.  We blit this later into the front buffer.
    dst->bindTarget();

    gGlowCombineProgram.bind();

    gGlowCombineProgram.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, src);
    gGlowCombineProgram.bindTexture(LLShaderMgr::DEFERRED_EMISSIVE, &pipeline.mGlow[1]);

    pipeline.mScreenTriangleVB->setBuffer();
    pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

    dst->flush();
}

// static
void DXPipeline::generateSMAABuffers(LLPipeline& pipeline, LLRenderTarget* src)
{
    static LLStaticHashedString sSmaaRTMetrics("SMAA_RT_METRICS");

    llassert(!gCubeSnapshot);
    bool multisample = DXPipeline::RenderFSAAType == 2 && gSMAAEdgeDetectProgram[0].isComplete() && pipeline.mFXAAMap.isComplete() && pipeline.mSMAABlendBuffer.isComplete();

    // Present everything.
    if (multisample)
    {
        static LLCachedControl<U32> aa_quality(gSavedSettings, "RenderFSAASamples", 0U);
        U32 fsaa_quality = std::clamp(aa_quality(), 0U, 3U);

        S32 width = src->getWidth();
        S32 height = src->getHeight();

        float rt_metrics[] = { 1.f / width, 1.f / height, (float)width, (float)height };

        DXDepthTest    depth(false, false);

        static LLCachedControl<bool> use_sample(gSavedSettings, "RenderSMAAUseSample", false);
        {
            // Bind setup:
            LLRenderTarget& dest = pipeline.mFXAAMap;
            LLHLSLShader& edge_shader = gSMAAEdgeDetectProgram[fsaa_quality];

            dest.bindTarget();
            dest.clear(GL_COLOR_BUFFER_BIT);

            edge_shader.bind();
            edge_shader.uniform4fv(sSmaaRTMetrics, 1, rt_metrics);
            DXPipeline::bindSMAAStaticSamplers();

            S32 channel = edge_shader.enableTexture(LLShaderMgr::DEFERRED_DIFFUSE, src->getUsage());
            if (channel > -1)
            {
                if (!use_sample)
                {
                    src->bindTexture(0, channel, LLTexUnit::TFO_BILINEAR);
                }
                else
                {
                    // bindManual() is a documented no-op under DX_RENDER - mSMAASampleMap here is just a
                    // sentinel, never a real GL name. mDXSMAASampleMap is the real DXTexture already uploaded
                    // at that same load site - just never wired into this bind
                    // call. RenderSMAAUseSample defaults false, so this branch
                    // is off by default, but was silently broken whenever a
                    // user turned it on. setTextureFilteringOption()/
                    // setTextureAddressMode() below are already self-guarded
                    // DX_RENDER no-ops (llrender.cpp) - safe to call
                    // unconditionally, only the bind itself needs branching.
                    gDX.getTexUnit(channel)->bind(pipeline.mDXSMAASampleMap, LLTexUnit::TAM_CLAMP, LLTexUnit::TFO_BILINEAR);
                    gDX.getTexUnit(channel)->setTextureFilteringOption(LLTexUnit::TFO_BILINEAR);
                }
                gDX.getTexUnit(channel)->setTextureAddressMode(LLTexUnit::TAM_CLAMP);
            }

            pipeline.mScreenTriangleVB->setBuffer();
            pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

            edge_shader.unbind();
            dest.flush();

            if (channel > -1)
            {
                gDX.getTexUnit(channel)->unbindFast(LLTexUnit::TT_TEXTURE);
            }
        }

        {
            // Bind setup:
            LLRenderTarget& dest = pipeline.mSMAABlendBuffer;
            LLHLSLShader& blend_weights_shader = gSMAABlendWeightsProgram[fsaa_quality];

            dest.bindTarget();
            dest.clear(GL_COLOR_BUFFER_BIT);

            blend_weights_shader.bind();
            blend_weights_shader.uniform4fv(sSmaaRTMetrics, 1, rt_metrics);
            DXPipeline::bindSMAAStaticSamplers();

            S32 edge_tex_channel = blend_weights_shader.enableTexture(LLShaderMgr::SMAA_EDGE_TEX, pipeline.mFXAAMap.getUsage());
            if (edge_tex_channel > -1)
            {
                pipeline.mFXAAMap.bindTexture(0, edge_tex_channel, LLTexUnit::TFO_BILINEAR);
                gDX.getTexUnit(edge_tex_channel)->setTextureAddressMode(LLTexUnit::TAM_CLAMP);
            }
            S32 area_tex_channel = blend_weights_shader.enableTexture(LLShaderMgr::SMAA_AREA_TEX, LLTexUnit::TT_TEXTURE);
            if (area_tex_channel > -1)
            {
                // Same bindManual() no-op gap as mSMAASampleMap above, but hit unconditionally whenever
                // RenderFSAAType==2 (not gated behind a settings toggle) - without this, SMAA's
                // blend-weight pass silently read whatever texture happened to already be bound here.
                gDX.getTexUnit(area_tex_channel)->bind(pipeline.mDXSMAAAreaMap, LLTexUnit::TAM_CLAMP, LLTexUnit::TFO_BILINEAR);
                gDX.getTexUnit(area_tex_channel)->setTextureFilteringOption(LLTexUnit::TFO_BILINEAR);
                gDX.getTexUnit(area_tex_channel)->setTextureAddressMode(LLTexUnit::TAM_CLAMP);
            }
            S32 search_tex_channel = blend_weights_shader.enableTexture(LLShaderMgr::SMAA_SEARCH_TEX, LLTexUnit::TT_TEXTURE);
            if (search_tex_channel > -1)
            {
                // Same bindManual() gap as mSMAAAreaMap just above.
                gDX.getTexUnit(search_tex_channel)->bind(pipeline.mDXSMAASearchMap, LLTexUnit::TAM_CLAMP, LLTexUnit::TFO_BILINEAR);
                gDX.getTexUnit(search_tex_channel)->setTextureFilteringOption(LLTexUnit::TFO_BILINEAR);
                gDX.getTexUnit(search_tex_channel)->setTextureAddressMode(LLTexUnit::TAM_CLAMP);
            }

            pipeline.mScreenTriangleVB->setBuffer();
            pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
            blend_weights_shader.unbind();
            dest.flush();
            if (edge_tex_channel > -1)
            {
                gDX.getTexUnit(edge_tex_channel)->unbindFast(LLTexUnit::TT_TEXTURE);
            }
            if (area_tex_channel > -1)
            {
                gDX.getTexUnit(area_tex_channel)->unbindFast(LLTexUnit::TT_TEXTURE);
            }
            if (search_tex_channel > -1)
            {
                gDX.getTexUnit(search_tex_channel)->unbindFast(LLTexUnit::TT_TEXTURE);
            }
        }
    }
}

// static
void DXPipeline::applySMAA(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst)
{
    static LLStaticHashedString sSmaaRTMetrics("SMAA_RT_METRICS");

    llassert(!gCubeSnapshot);
    bool multisample = DXPipeline::RenderFSAAType == 2 && gSMAAEdgeDetectProgram[0].isComplete() && pipeline.mFXAAMap.isComplete() && pipeline.mSMAABlendBuffer.isComplete();

    // Present everything.
    if (multisample)
    {
        static LLCachedControl<U32> aa_quality(gSavedSettings, "RenderFSAASamples", 0U);
        U32 fsaa_quality = std::clamp(aa_quality(), 0U, 3U);

        S32 width = src->getWidth();
        S32 height = src->getHeight();

        float rt_metrics[] = { 1.f / width, 1.f / height, (float)width, (float)height };

        DXDepthTest    depth(false, false);

        {
            // Bind setup:
            LLRenderTarget* bound_target = dst;
            LLHLSLShader& blend_shader = gSMAANeighborhoodBlendProgram[fsaa_quality];

            bound_target->bindTarget();
            bound_target->clear(GL_COLOR_BUFFER_BIT);

            blend_shader.bind();
            blend_shader.uniform4fv(sSmaaRTMetrics, 1, rt_metrics);
            DXPipeline::bindSMAAStaticSamplers();

            S32 diffuse_channel = blend_shader.enableTexture(LLShaderMgr::DEFERRED_DIFFUSE);
            if (diffuse_channel > -1)
            {
                src->bindTexture(0, diffuse_channel, LLTexUnit::TFO_BILINEAR);
                gDX.getTexUnit(diffuse_channel)->setTextureAddressMode(LLTexUnit::TAM_CLAMP);
            }

            S32 blend_channel = blend_shader.enableTexture(LLShaderMgr::SMAA_BLEND_TEX);
            if (blend_channel > -1)
            {
                pipeline.mSMAABlendBuffer.bindTexture(0, blend_channel, LLTexUnit::TFO_BILINEAR);
            }

            pipeline.mScreenTriangleVB->setBuffer();
            pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

            bound_target->flush();
            blend_shader.unbind();
            if (diffuse_channel > -1)
            {
                gDX.getTexUnit(diffuse_channel)->unbindFast(LLTexUnit::TT_TEXTURE);
            }
            if (blend_channel > -1)
            {
                gDX.getTexUnit(blend_channel)->unbindFast(LLTexUnit::TT_TEXTURE);
            }
        }
    }
    else
    {
        DXPipeline::copyRenderTarget(pipeline, src, dst);
    }
}

// ============================================================================
// Shader binding, reflection probes, and screen-space reflections
// ============================================================================

// static
void DXPipeline::bindLightFunc(LLPipeline& pipeline, LLHLSLShader& shader)
{
    S32 channel = shader.enableTexture(LLShaderMgr::DEFERRED_LIGHTFUNC);
    if (channel > -1)
    {
        gDX.getTexUnit(channel)->bind(pipeline.mDXLightFunc, LLTexUnit::TAM_CLAMP, LLTexUnit::TFO_TRILINEAR);
    }

    channel = shader.enableTexture(LLShaderMgr::DEFERRED_BRDF_LUT, LLTexUnit::TT_TEXTURE);
    if (channel > -1)
    {
        pipeline.mPbrBrdfLut.bindTexture(0, channel);
    }
}

// static
void DXPipeline::bindShadowMaps(LLPipeline& pipeline, LLHLSLShader& shader)
{
    // DX_RENDER-only 3rd arg (see LLTexUnit::bind()): shadowUtil.hlsl declares shadowMap0-5Sampler as
    // SamplerComparisonState (hardware PCF via .SampleCmp()), but this bind call always created a
    // regular filtering sampler otherwise - undefined behavior under D3D11. Ignored under GL, whose
    // equivalent (GL_TEXTURE_COMPARE_MODE) is set at shadow-target allocation time instead.
    for (U32 i = 0; i < 4; i++)
    {
        LLRenderTarget* shadow_target = DXPipeline::getSunShadowTarget(pipeline, i);
        if (shadow_target)
        {
            S32 channel = shader.enableTexture(LLShaderMgr::DEFERRED_SHADOW0 + i, LLTexUnit::TT_TEXTURE);
            if (channel > -1)
            {
                gDX.getTexUnit(channel)->bind(DXPipeline::getSunShadowTarget(pipeline, i), true, true);
            }
        }
    }

    for (U32 i = 4; i < 6; i++)
    {
        S32 channel = shader.enableTexture(LLShaderMgr::DEFERRED_SHADOW0 + i);
        if (channel > -1)
        {
            LLRenderTarget* shadow_target = DXPipeline::getSpotShadowTarget(pipeline, i - 4);
            if (shadow_target)
            {
                gDX.getTexUnit(channel)->bind(shadow_target, true, true);
            }
        }
    }
}

// static
void DXPipeline::bindDeferredShaderFast(LLPipeline& pipeline, LLHLSLShader& shader)
{
    if (shader.mCanBindFast)
    { // was previously fully bound, use fast path
        shader.bind();
        DXPipeline::bindLightFunc(pipeline, shader);
        DXPipeline::bindShadowMaps(pipeline, shader);
        DXPipeline::bindReflectionProbes(pipeline, shader);
    }
    else
    { //wasn't previously bound, use slow path
        DXPipeline::bindDeferredShader(pipeline, shader);
        shader.mCanBindFast = true;
    }
}

// static
void DXPipeline::bindDeferredShader(LLPipeline& pipeline, LLHLSLShader& shader, LLRenderTarget* light_target, LLRenderTarget* depth_target)
{
    LLRenderTarget* deferred_target = &pipeline.mRT->deferredScreen;
    LLRenderTarget* deferred_light_target = &pipeline.mRT->deferredLight;

    shader.bind();
    S32 channel = 0;
    channel = shader.enableTexture(LLShaderMgr::DEFERRED_DIFFUSE, deferred_target->getUsage());
    if (channel > -1)
    {
        deferred_target->bindTexture(0, channel, LLTexUnit::TFO_POINT); // frag_data[0]
        gDX.getTexUnit(channel)->setTextureAddressMode(LLTexUnit::TAM_CLAMP);
    }

    channel = shader.enableTexture(LLShaderMgr::DEFERRED_SPECULAR, deferred_target->getUsage());
    if (channel > -1)
    {
        deferred_target->bindTexture(1, channel, LLTexUnit::TFO_POINT); // frag_data[1]
        gDX.getTexUnit(channel)->setTextureAddressMode(LLTexUnit::TAM_CLAMP);
    }

    channel = shader.enableTexture(LLShaderMgr::NORMAL_MAP, deferred_target->getUsage());
    if (channel > -1)
    {
        deferred_target->bindTexture(2, channel, LLTexUnit::TFO_POINT); // frag_data[2]
        gDX.getTexUnit(channel)->setTextureAddressMode(LLTexUnit::TAM_CLAMP);
    }

    channel = shader.enableTexture(LLShaderMgr::DEFERRED_EMISSIVE, deferred_target->getUsage());
    if (channel > -1)
    {
        deferred_target->bindTexture(3, channel, LLTexUnit::TFO_POINT); // frag_data[3]
        gDX.getTexUnit(channel)->setTextureAddressMode(LLTexUnit::TAM_CLAMP);
    }

    channel = shader.enableTexture(LLShaderMgr::DEFERRED_DEPTH, deferred_target->getUsage());
    if (channel > -1)
    {
        if (depth_target)
        {
            gDX.getTexUnit(channel)->bind(depth_target, true);
        }
        else
        {
            gDX.getTexUnit(channel)->bind(deferred_target, true);
        }
    }

    channel = shader.enableTexture(LLShaderMgr::EXPOSURE_MAP);
    if (channel > -1)
    {
        gDX.getTexUnit(channel)->bind(&pipeline.mExposureMap);
    }

    // S24: both of these were gated on getUniformLocation(), which was a
    // permanent -1 (or, for the '!' check below, always-truthy-so-always-
    // false) stub under DX_RENDER - no GL program object concept ever
    // existed to reflect a real location from. Neither upload has actually
    // run in this build; removed alongside the rest of that dead reflection
    // API rather than kept as a conditional with no real check left to gate
    // on. Revisit only if VIEWPORT/MODELVIEW_MATRIX are found missing from a
    // shader that needs them - a real reflection or per-shader-declares
    // check would need to be built from scratch, not restored.

    channel = shader.enableTexture(LLShaderMgr::DEFERRED_NOISE);
    if (channel > -1)
    {
        gDX.getTexUnit(channel)->bind(pipeline.mDXNoiseMap, LLTexUnit::TAM_WRAP, LLTexUnit::TFO_POINT);
    }

    DXPipeline::bindLightFunc(pipeline, shader);


    light_target = light_target ? light_target : deferred_light_target;
    channel = shader.enableTexture(LLShaderMgr::DEFERRED_LIGHT, light_target->getUsage());
    if (channel > -1)
    {
        if (light_target->isComplete())
        {
            light_target->bindTexture(0, channel, LLTexUnit::TFO_POINT);
        }
        else
        {
            gDX.getTexUnit(channel)->bindFast(LLViewerFetchedTexture::sWhiteImagep);
        }
    }


    DXPipeline::bindShadowMaps(pipeline, shader);


    F32 mat[16 * 6] = {};
    for (U32 i = 0; i < 16; i++)
    {
        mat[i] = glm::value_ptr(pipeline.mSunShadowMatrix[0])[i];
        mat[i + 16] = glm::value_ptr(pipeline.mSunShadowMatrix[1])[i];
        mat[i + 32] = glm::value_ptr(pipeline.mSunShadowMatrix[2])[i];
        mat[i + 48] = glm::value_ptr(pipeline.mSunShadowMatrix[3])[i];
        mat[i + 64] = glm::value_ptr(pipeline.mSunShadowMatrix[4])[i];
        mat[i + 80] = glm::value_ptr(pipeline.mSunShadowMatrix[5])[i];
    }

    shader.uniformMatrix4fv(LLShaderMgr::DEFERRED_SHADOW_MATRIX, 6, false, mat);


    // Must use DXPipeline::shouldUseLegacyEnvMap() here, not sReflectionProbesEnabled alone (see its
    // declaration comment in pipeline.h) - sReflectionProbesEnabled only reflects
    // RenderReflectionsEnabled, while reflectionProbeF.hlsl's own fallback condition also reacts to
    // RenderReflectionProbesEnabled being off independently. LLVOSky::updateSky() (the legacy cubemap's
    // producer) also skips its update work whenever sReflectionProbesEnabled is true, so this texture
    // must not be consumed under that condition either or it reads stale/uninitialized data.
    bool use_legacy_env_map = DXPipeline::shouldUseLegacyEnvMap();
    if (use_legacy_env_map)
    {
        channel = shader.enableTexture(LLShaderMgr::ENVIRONMENT_MAP, LLTexUnit::TT_CUBE_MAP);
        if (channel > -1)
        {
            DXCubeMap* cube_map = gSky.mVOSkyp ? gSky.mVOSkyp->getCubeMap() : NULL;
            if (cube_map)
            {
                cube_map->enable(channel);
                cube_map->bind();
            }

            F32* m = gDXModelView;

            F32 mat[] = { m[0], m[1], m[2],
                          m[4], m[5], m[6],
                          m[8], m[9], m[10] };

            shader.uniformMatrix3fv(LLShaderMgr::DEFERRED_ENV_MAT, 1, true, mat);
        }
    }

    DXPipeline::bindReflectionProbes(pipeline, shader);

    /*if (gCubeSnapshot)
    { // we only really care about the first two values, but the shader needs increasing separation between clip planes
        shader.uniform4f(LLShaderMgr::DEFERRED_SHADOW_CLIP, 1.f, 64.f, 128.f, 256.f);
    }
    else*/
    {
        shader.uniform4fv(LLShaderMgr::DEFERRED_SHADOW_CLIP, 1, pipeline.mSunClipPlanes.mV);
    }
    shader.uniform1f(LLShaderMgr::DEFERRED_SUN_WASH, DXPipeline::RenderDeferredSunWash);
    shader.uniform1f(LLShaderMgr::DEFERRED_SHADOW_NOISE, DXPipeline::RenderShadowNoise);
    shader.uniform1f(LLShaderMgr::DEFERRED_BLUR_SIZE, DXPipeline::RenderShadowBlurSize);

    shader.uniform1f(LLShaderMgr::DEFERRED_SSAO_RADIUS, DXPipeline::RenderSSAOScale);
    shader.uniform1f(LLShaderMgr::DEFERRED_SSAO_MAX_RADIUS, (F32)DXPipeline::RenderSSAOMaxScale);

    F32 ssao_factor = DXPipeline::RenderSSAOFactor;
    shader.uniform1f(LLShaderMgr::DEFERRED_SSAO_FACTOR, ssao_factor);
    shader.uniform1f(LLShaderMgr::DEFERRED_SSAO_FACTOR_INV, 1.0f / ssao_factor);

    LLVector3 ssao_effect = DXPipeline::RenderSSAOEffect;
    F32 matrix_diag = (ssao_effect[0] + 2.0f * ssao_effect[1]) / 3.0f;
    F32 matrix_nondiag = (ssao_effect[0] - ssao_effect[1]) / 3.0f;
    // This matrix scales (proj of color onto <1/rt(3),1/rt(3),1/rt(3)>) by
    // value factor, and scales remainder by saturation factor
    F32 ssao_effect_mat[] = { matrix_diag, matrix_nondiag, matrix_nondiag,
                                matrix_nondiag, matrix_diag, matrix_nondiag,
                                matrix_nondiag, matrix_nondiag, matrix_diag };
    shader.uniformMatrix3fv(LLShaderMgr::DEFERRED_SSAO_EFFECT_MAT, 1, GL_FALSE, ssao_effect_mat);

    //F32 shadow_offset_error = 1.f + RenderShadowOffsetError * fabsf(LLViewerCamera::getInstance()->getOrigin().mV[2]);
    F32 shadow_bias_error = DXPipeline::RenderShadowBiasError * fabsf(LLViewerCamera::getInstance()->getOrigin().mV[2]) / 3000.f;
    F32 shadow_bias = DXPipeline::RenderShadowBias + shadow_bias_error;

    shader.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (F32)deferred_target->getWidth(), (F32)deferred_target->getHeight());
    shader.uniform1f(LLShaderMgr::DEFERRED_NEAR_CLIP, LLViewerCamera::getInstance()->getNear() * 2.f);
    shader.uniform1f(LLShaderMgr::DEFERRED_SHADOW_OFFSET, DXPipeline::RenderShadowOffset); //*shadow_offset_error);
    shader.uniform1f(LLShaderMgr::DEFERRED_SHADOW_BIAS, shadow_bias);
    shader.uniform1f(LLShaderMgr::DEFERRED_SPOT_SHADOW_OFFSET, DXPipeline::RenderSpotShadowOffset);
    shader.uniform1f(LLShaderMgr::DEFERRED_SPOT_SHADOW_BIAS, DXPipeline::RenderSpotShadowBias);

    shader.uniform3fv(LLShaderMgr::DEFERRED_SUN_DIR, 1, pipeline.mTransformedSunDir.mV);
    shader.uniform3fv(LLShaderMgr::DEFERRED_MOON_DIR, 1, pipeline.mTransformedMoonDir.mV);
    shader.uniform2f(LLShaderMgr::DEFERRED_SHADOW_RES, (F32)pipeline.mRT->shadow[0].getWidth(), (F32)pipeline.mRT->shadow[0].getHeight());
    shader.uniform2f(LLShaderMgr::DEFERRED_PROJ_SHADOW_RES, (F32)pipeline.mSpotShadow[0].getWidth(), (F32)pipeline.mSpotShadow[0].getHeight());
    shader.uniform1f(LLShaderMgr::DEFERRED_DEPTH_CUTOFF, DXPipeline::RenderEdgeDepthCutoff);
    shader.uniform1f(LLShaderMgr::DEFERRED_NORM_CUTOFF, DXPipeline::RenderEdgeNormCutoff);

    // MODELVIEW_DELTA_MATRIX/INVERSE_MODELVIEW_DELTA_MATRIX are uploaded once, in bindReflectionProbes(shader)
    // (called earlier in this function) - do not re-upload them here too.

    shader.uniform1i(LLShaderMgr::CUBE_SNAPSHOT, gCubeSnapshot ? 1 : 0);

    // S24: see the getUniformLocation() removal comment above - same
    // permanently-false gate, this upload has never actually run.

    // auto adjust legacy sun color if needed
    static LLCachedControl<bool> should_auto_adjust(gSavedSettings, "RenderSkyAutoAdjustLegacy", false);
    static LLCachedControl<F32> auto_adjust_sun_color_scale(gSavedSettings, "RenderSkyAutoAdjustSunColorScale", 1.f);
    LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();
    LLColor3 sun_diffuse(pipeline.mSunDiffuse.mV);
    if (should_auto_adjust && psky->canAutoAdjust())
    {
        sun_diffuse *= auto_adjust_sun_color_scale;
    }

    shader.uniform3fv(LLShaderMgr::SUNLIGHT_COLOR, 1, sun_diffuse.mV);
    shader.uniform3fv(LLShaderMgr::MOONLIGHT_COLOR, 1, pipeline.mMoonDiffuse.mV);

    shader.uniform1f(LLShaderMgr::REFLECTION_PROBE_MAX_LOD, pipeline.mSphereProbes.getMaxProbeLOD());
}

// static
void DXPipeline::unbindDeferredShader(LLPipeline& pipeline, LLHLSLShader& shader)
{
    LLRenderTarget* deferred_target = &pipeline.mRT->deferredScreen;
    LLRenderTarget* deferred_light_target = &pipeline.mRT->deferredLight;

    shader.disableTexture(LLShaderMgr::NORMAL_MAP, deferred_target->getUsage());
    shader.disableTexture(LLShaderMgr::DEFERRED_DIFFUSE, deferred_target->getUsage());
    shader.disableTexture(LLShaderMgr::DEFERRED_SPECULAR, deferred_target->getUsage());
    shader.disableTexture(LLShaderMgr::DEFERRED_EMISSIVE, deferred_target->getUsage());
    shader.disableTexture(LLShaderMgr::DEFERRED_BRDF_LUT);
    //shader.disableTexture(LLShaderMgr::DEFERRED_DEPTH, deferred_depth_target->getUsage());
    shader.disableTexture(LLShaderMgr::DEFERRED_DEPTH, deferred_target->getUsage());
    shader.disableTexture(LLShaderMgr::DEFERRED_LIGHT, deferred_light_target->getUsage());
    shader.disableTexture(LLShaderMgr::DIFFUSE_MAP);
    shader.disableTexture(LLShaderMgr::DEFERRED_BLOOM);

    for (U32 i = 0; i < 4; i++)
    {
        shader.disableTexture(LLShaderMgr::DEFERRED_SHADOW0 + i);
    }

    for (U32 i = 4; i < 6; i++)
    {
        shader.disableTexture(LLShaderMgr::DEFERRED_SHADOW0 + i);
    }

    shader.disableTexture(LLShaderMgr::DEFERRED_NOISE);
    shader.disableTexture(LLShaderMgr::DEFERRED_LIGHTFUNC);

    if (!DXPipeline::sReflectionProbesEnabled)
    {
        S32 channel = shader.disableTexture(LLShaderMgr::ENVIRONMENT_MAP, LLTexUnit::TT_CUBE_MAP);
        if (channel > -1)
        {
            DXCubeMap* cube_map = gSky.mVOSkyp ? gSky.mVOSkyp->getCubeMap() : NULL;
            if (cube_map)
            {
                cube_map->disable();
            }
        }
    }

    DXPipeline::unbindReflectionProbes(pipeline, shader);

    gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
    gDX.getTexUnit(0)->activate();
    shader.unbind();
}

// static
void DXPipeline::setEnvMat(LLPipeline& pipeline, LLHLSLShader& shader)
{
    // Must read gDXModelView here, not gDX's own internal matrix stack (getModelviewMatrix()): that
    // stack is touched by far more systems within a single frame (shadow cascades, water reflection,
    // ~10 other set_current_modelview() call sites) than gDXModelView, so reading it at an arbitrary
    // point mid-frame picks up transient poisoning from whichever of those last ran.
    F32* m = gDXModelView;

    F32 mat[] = { m[0], m[1], m[2],
                    m[4], m[5], m[6],
                    m[8], m[9], m[10] };

    shader.uniformMatrix3fv(LLShaderMgr::DEFERRED_ENV_MAT, 1, true, mat);
}

// static
void DXPipeline::bindReflectionProbes(LLPipeline& pipeline, LLHLSLShader& shader)
{
    //static LLCachedControl <bool> stereo(gSavedSettings,"StereoMode",false); // might be redundant now placemarker
    if (!DXPipeline::sReflectionProbesEnabled)
    {
        return;
    }

    S32 channel = shader.enableTexture(LLShaderMgr::REFLECTION_PROBES, LLTexUnit::TT_CUBE_MAP_ARRAY);
    bool bound = false;
    if (channel > -1 && pipeline.mSphereProbes.hasProbeCubes())
    {
        pipeline.mSphereProbes.bindRadiance(channel);
        bound = true;
    }

    channel = shader.enableTexture(LLShaderMgr::IRRADIANCE_PROBES, LLTexUnit::TT_CUBE_MAP_ARRAY);
    if (channel > -1 && pipeline.mSphereProbes.hasProbeCubes())
    {
        pipeline.mSphereProbes.bindIrradiance(channel);
        bound = true;
    }

    if (DXPipeline::RenderMirrors)
    {
        channel = shader.enableTexture(LLShaderMgr::HERO_PROBE, LLTexUnit::TT_CUBE_MAP_ARRAY);
        if (channel > -1 && pipeline.mMirrorProbes.hasProbeCubes())
        {
            pipeline.mMirrorProbes.bindRadiance(channel);
            bound = true;
        }
    }

    if (bound)
    {
        pipeline.mSphereProbes.bindUniforms();

        DXPipeline::setEnvMat(pipeline, shader);
    }

    // scene map, scene depth and SSR uniforms (see bindSSRUniforms)
    DXPipeline::bindSSRUniforms(pipeline, shader);

}

// static
void DXPipeline::bindSSRUniforms(LLPipeline& pipeline, LLHLSLShader& shader)
{
    S32 channel = -1;

    channel = shader.enableTexture(LLShaderMgr::SCENE_MAP);
    if (channel > -1)
    {
        gDX.getTexUnit(channel)->bind(&pipeline.mSceneMap);
    }

    // Check if we're rendering water to use water-specific SSR settings
    bool isWaterShader = (&shader == &gWaterProgram || &shader == &gUnderWaterProgram);

    S32 ssrIterations = isWaterShader ? DXPipeline::RenderWaterSSRIterations : DXPipeline::RenderScreenSpaceReflectionIterations;
    F32 ssrRayStep = isWaterShader ? DXPipeline::RenderWaterSSRRayStep : DXPipeline::RenderScreenSpaceReflectionRayStep;

    shader.uniform1f(LLShaderMgr::DEFERRED_SSR_ITR_COUNT, (F32)ssrIterations);
    shader.uniform1f(LLShaderMgr::DEFERRED_SSR_DIST_BIAS, DXPipeline::RenderScreenSpaceReflectionDistanceBias);
    shader.uniform1f(LLShaderMgr::DEFERRED_SSR_RAY_STEP, ssrRayStep);
    shader.uniform1f(LLShaderMgr::DEFERRED_SSR_GLOSSY_SAMPLES, (F32)DXPipeline::RenderScreenSpaceReflectionGlossySamples);
    shader.uniform1f(LLShaderMgr::DEFERRED_SSR_REJECT_BIAS, DXPipeline::RenderScreenSpaceReflectionDepthRejectBias);
    pipeline.mPoissonOffset++;

    if (pipeline.mPoissonOffset > 128 - DXPipeline::RenderScreenSpaceReflectionGlossySamples)
        pipeline.mPoissonOffset = 0;

    shader.uniform1f(LLShaderMgr::DEFERRED_SSR_NOISE_SINE, (F32)pipeline.mPoissonOffset);
    shader.uniform1f(LLShaderMgr::DEFERRED_SSR_ADAPTIVE_STEP_MULT, DXPipeline::RenderScreenSpaceReflectionAdaptiveStepMultiplier);
    shader.uniform1f(LLShaderMgr::DEFERRED_SSR_GLOSS_THRESHOLD, DXPipeline::RenderScreenSpaceReflectionGlossThreshold);
    static LLCachedControl<F32> ssr_miss_fill_setting(gSavedSettings, "RenderSSRMissFill", 0.25f);
    static LLStaticHashedString sSSRMissFill("ssr_miss_fill");
    shader.uniform1f(sSSRMissFill, ssr_miss_fill_setting);

    // doProbeSample()'s SSR gate is `cube_snapshot != 1 && glossiness >= ssrGlossThreshold`
    // (reflectionProbeF.hlsl). Must upload CUBE_SNAPSHOT here too, not just from bindDeferredShader() -
    // callers of this function that skip that upload leave the shader's copy holding whatever a real
    // reflection-probe capture pass last left in that constant buffer slot (reads as 1), permanently
    // defeating cube_snapshot != 1 and zeroing SSR regardless of angle/distance/glossiness.
    shader.uniform1i(LLShaderMgr::CUBE_SNAPSHOT, gCubeSnapshot ? 1 : 0);

    // modelview_delta/inv_modelview_delta (screenSpaceReflUtil.glsl's "transform from last camera space
    // to current camera space") have real LLShaderMgr entries but were never uploaded from anywhere in
    // the engine. get_current_modelview()/get_last_modelview() (llrender.cpp) are the same
    // backend-agnostic accessors used elsewhere for this delta pattern; gDXLastModelView is captured
    // every frame by DXPipeline::renderGeomPostDeferred(), mirroring GL's own body (which never runs
    // under DX_RENDER).
    {
        glm::mat4 cur_modelview = get_current_modelview();
        glm::mat4 last_modelview = get_last_modelview();
        glm::mat4 modelview_delta = cur_modelview * glm::inverse(last_modelview);
        glm::mat4 inv_modelview_delta = glm::inverse(modelview_delta);
        shader.uniformMatrix4fv(LLShaderMgr::MODELVIEW_DELTA_MATRIX, 1, false, glm::value_ptr(modelview_delta));
        shader.uniformMatrix4fv(LLShaderMgr::INVERSE_MODELVIEW_DELTA_MATRIX, 1, false, glm::value_ptr(inv_modelview_delta));

        // screenSpaceReflUtil.hlsl's ray march reprojects into last-frame view space via
        // inv_modelview_delta above, then needs LAST frame's projection matrix (not this frame's) to
        // turn that into a screen UV - get_last_projection(), same accessor family as
        // get_last_modelview() above.
        glm::mat4 last_projection = get_last_projection();
        shader.uniformMatrix4fv(LLShaderMgr::LAST_PROJECTION_MATRIX, 1, false, glm::value_ptr(last_projection));
    }

    channel = shader.enableTexture(LLShaderMgr::SCENE_DEPTH);
    if (channel > -1)
    {
        gDX.getTexUnit(channel)->bind(&pipeline.mSceneMap, true);
    }
}

// static
void DXPipeline::bindSSRBuffer(LLPipeline& pipeline, LLHLSLShader& shader)
{
    S32 channel = shader.enableTexture(LLShaderMgr::SSR_BUFFER);
    LLRenderTarget* source = pipeline.mSSRResolveReady ? &pipeline.mSSRResolved[pipeline.mSSRLastResolved] : &pipeline.mSSRBuffer;
    if (channel > -1 && source->isComplete())
    {
        gDX.getTexUnit(channel)->bind(source);
    }

    // The lighting shader reads the SSR buffer only when the pass ran this frame.
    static LLStaticHashedString sSSRFromBuffer("ssr_from_buffer");
    static LLCachedControl<bool> ssr_pass_enabled(gSavedSettings, "RenderSSRPassEnabled", true);
    bool buffer_ready = DXPipeline::RenderScreenSpaceReflections && ssr_pass_enabled && gSSRProgram.isComplete() && pipeline.mSSRBuffer.isComplete();
    shader.uniform1i(sSSRFromBuffer, buffer_ready ? 1 : 0);
}

// static
void DXPipeline::renderSSRPass(LLPipeline& pipeline)
{
    // Not during a reflection-probe capture: the buffer would be stale and cube_snapshot gates it anyway.
    static LLCachedControl<bool> ssr_pass_enabled(gSavedSettings, "RenderSSRPassEnabled", true);
    if (gCubeSnapshot)
    {
        // probe captures must not reset temporal history
        return;
    }
    if (!DXPipeline::RenderScreenSpaceReflections || !ssr_pass_enabled || !gSSRProgram.isComplete() || !pipeline.mSSRBuffer.isComplete())
    {
        pipeline.mSSRResolveReady = false;
        pipeline.mSSRHistoryValid = false;
        return;
    }

    pipeline.mSSRBuffer.bindTarget(false);
    pipeline.mSSRBuffer.clear(GL_COLOR_BUFFER_BIT);

    gSSRProgram.bind();
    DXPipeline::bindDeferredShader(pipeline, gSSRProgram);
    DXPipeline::bindSSRUniforms(pipeline, gSSRProgram);

    {
        DXDepthTest depth(false);
        LLGLDisable   blend(GL_BLEND);
        LLGLDisable   cullface(GL_CULL_FACE);

        pipeline.mScreenTriangleVB->setBuffer();
        pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
    }

    pipeline.mSSRBuffer.flush();
    gSSRProgram.unbind();

    DXPipeline::renderSSRResolve(pipeline);
}

// static
void DXPipeline::renderSSRResolve(LLPipeline& pipeline)
{
    static LLCachedControl<bool> temporal(gSavedSettings, "RenderSSRTemporal", true);
    pipeline.mSSRResolveReady = false;
    if (!temporal || !gSSRResolveProgram.isComplete() || !pipeline.mSSRResolved[0].isComplete() || !pipeline.mSSRResolved[1].isComplete())
    {
        return;
    }

    S32 cur = pipeline.mSSRResolveIndex;
    S32 prev = 1 - cur;

    pipeline.mSSRResolved[cur].bindTarget(false);

    gSSRResolveProgram.bind();
    DXPipeline::bindDeferredShader(pipeline, gSSRResolveProgram);

    S32 raw_channel = gSSRResolveProgram.enableTexture(LLShaderMgr::SSR_BUFFER);
    if (raw_channel > -1)
    {
        gDX.getTexUnit(raw_channel)->bind(&pipeline.mSSRBuffer);
    }
    S32 history_channel = gSSRResolveProgram.enableTexture(LLShaderMgr::SSR_HISTORY);
    if (history_channel > -1)
    {
        gDX.getTexUnit(history_channel)->bind(&pipeline.mSSRResolved[prev]);
    }

    // Same reprojection inputs as bindSSRUniforms(): world camera now and one real frame ago.
    glm::mat4 cur_modelview = get_current_modelview();
    glm::mat4 last_modelview = get_last_modelview();
    glm::mat4 modelview_delta = cur_modelview * glm::inverse(last_modelview);
    glm::mat4 inv_modelview_delta = glm::inverse(modelview_delta);
    gSSRResolveProgram.uniformMatrix4fv(LLShaderMgr::INVERSE_MODELVIEW_DELTA_MATRIX, 1, false, glm::value_ptr(inv_modelview_delta));
    gSSRResolveProgram.uniformMatrix4fv(LLShaderMgr::LAST_PROJECTION_MATRIX, 1, false, glm::value_ptr(get_last_projection()));

    static LLStaticHashedString sHistoryValid("ssr_history_valid");
    gSSRResolveProgram.uniform1i(sHistoryValid, pipeline.mSSRHistoryValid ? 1 : 0);

    {
        DXDepthTest depth(false);
        LLGLDisable   blend(GL_BLEND);
        LLGLDisable   cullface(GL_CULL_FACE);

        pipeline.mScreenTriangleVB->setBuffer();
        pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
    }

    pipeline.mSSRResolved[cur].flush();
    gSSRResolveProgram.unbind();

    pipeline.mSSRLastResolved = cur;
    pipeline.mSSRResolveIndex = prev;
    pipeline.mSSRHistoryValid = true;
    pipeline.mSSRResolveReady = true;
}

// static
void DXPipeline::unbindReflectionProbes(LLPipeline& pipeline, LLHLSLShader& shader)
{
    S32 channel = shader.disableTexture(LLShaderMgr::REFLECTION_PROBES, LLTexUnit::TT_CUBE_MAP);
    if (channel > -1 && pipeline.mSphereProbes.hasProbeCubes())
    {
        pipeline.mSphereProbes.unbindProbeCubes(channel);
        if (channel == 0)
        {
            gDX.getTexUnit(channel)->enable(LLTexUnit::TT_TEXTURE);
        }
    }
}

// ============================================================================
// Render-pass dispatch (per-pool draw-call issuing)
// ============================================================================

// static
void DXPipeline::renderObjects(LLPipeline& pipeline, U32 type, bool texture, bool batch_texture, bool rigged)
{
    pipeline.assertInitialized();

    gDX.loadMatrix(gDXModelView);
    gDXLastMatrix = nullptr;

    // Precompute the actual batch type once.
    const U32 batch_type = rigged ? (type + 1) : type;

    // Single dispatch point.
    if (rigged)
    {
        pipeline.getSimplePool()->pushRiggedBatches(batch_type, texture, batch_texture);
    }
    else
    {
        pipeline.getSimplePool()->pushBatches(batch_type, texture, batch_texture);
    }

    // Only reload the matrix if the pool modified it.
    gDX.loadMatrix(gDXModelView);
    gDXLastMatrix = nullptr;
}

// static
void DXPipeline::renderGLTFObjects(LLPipeline& pipeline, U32 type, bool texture, bool rigged)
{
    pipeline.assertInitialized();
    gDX.loadMatrix(gDXModelView);
    gDXLastMatrix = NULL;

    if (rigged)
    {
        pipeline.getSimplePool()->pushRiggedGLTFBatches(type + 1, texture);
    }
    else
    {
        pipeline.getSimplePool()->pushGLTFBatches(type, texture);
    }

    gDX.loadMatrix(gDXModelView);
    gDXLastMatrix = NULL;

    if (!rigged)
    {
        LL::GLTFSceneManager::instance().renderOpaque();
    }
    else
    {
        LL::GLTFSceneManager::instance().render(true, true);
    }
}

// Currently only used for shadows -Cosmic,2023-04-19
// static
void DXPipeline::renderAlphaObjects(LLPipeline& pipeline, bool rigged)
{
    pipeline.assertInitialized();
    gDX.loadMatrix(gDXModelView);
    gDXLastMatrix = NULL;
    S32 sun_up = LLEnvironment::instance().getIsSunUp() ? 1 : 0;
    U32 target_width = LLRenderTarget::sCurResX;
    U32 type = LLRenderPass::PASS_ALPHA;
    // for gDeferredShadowAlphaMaskProgram
    const LLVOAvatar* lastAvatar = nullptr;
    U64 lastMeshId = 0;
    bool skipLastSkin = false;
    // for gDeferredShadowGLTFAlphaBlendProgram
    const LLVOAvatar* lastAvatarGLTF = nullptr;
    U64 lastMeshIdGLTF = 0;
    bool skipLastSkinGLTF = false;
    auto* begin = DXPipeline::beginRenderMap(pipeline, type);
    auto* end = DXPipeline::endRenderMap(pipeline, type);

    for (LLCullResult::drawinfo_iterator i = begin; i != end; )
    {
        LLDrawInfo* pparams = *i;
        LLCullResult::increment_iterator(i, end);

        if (rigged != (pparams->mAvatar != nullptr))
        {
            // Pool contains both rigged and non-rigged DrawInfos. Only draw
            // the objects we're interested in in this pass.
            continue;
        }

        if (rigged)
        {
            if (pparams->mGLTFMaterial)
            {
                // Color pass respects mDoubleSided per-draw (dxdrawpoolalpha.cpp)
                // so both faces of thin hair-card geometry render; this pass
                // inherited a blanket LLGLEnable cull(GL_CULL_FACE) from
                // renderShadow() with no override, silently discarding
                // whichever face pointed away from the sun.
                LLGLDisable cull_face(pparams->mGLTFMaterial->mDoubleSided ? GL_CULL_FACE : 0);
                gDeferredShadowGLTFAlphaBlendProgram.bind(rigged);
                LLHLSLShader::sCurBoundShaderPtr->uniform1i(LLShaderMgr::SUN_UP_FACTOR, sun_up);
                LLHLSLShader::sCurBoundShaderPtr->uniform1f(LLShaderMgr::DEFERRED_SHADOW_TARGET_WIDTH, (float)target_width);
                LLHLSLShader::sCurBoundShaderPtr->setMinimumAlpha(ALPHA_BLEND_CUTOFF_RIGGED);
                if (LLRenderPass::uploadMatrixPalette(pparams->mAvatar, pparams->mSkinInfo, lastAvatarGLTF, lastMeshIdGLTF, skipLastSkinGLTF))
                {
                    LLRenderPass::pushGLTFBatch(*pparams);
                }
            }
            else
            {
                gDeferredShadowAlphaMaskProgram.bind(rigged);
                LLHLSLShader::sCurBoundShaderPtr->uniform1i(LLShaderMgr::SUN_UP_FACTOR, sun_up);
                LLHLSLShader::sCurBoundShaderPtr->uniform1f(LLShaderMgr::DEFERRED_SHADOW_TARGET_WIDTH, (float)target_width);
                LLHLSLShader::sCurBoundShaderPtr->setMinimumAlpha(ALPHA_BLEND_CUTOFF_RIGGED);
                if (pipeline.getSimplePool()->uploadMatrixPalette(pparams->mAvatar, pparams->mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
                {
                    pipeline.getSimplePool()->pushBatch(*pparams, true, true);
                }
            }
        }
        else
        {
            if (pparams->mGLTFMaterial)
            {
                LLGLDisable cull_face(pparams->mGLTFMaterial->mDoubleSided ? GL_CULL_FACE : 0);
                gDeferredShadowGLTFAlphaBlendProgram.bind(rigged);
                LLHLSLShader::sCurBoundShaderPtr->uniform1i(LLShaderMgr::SUN_UP_FACTOR, sun_up);
                LLHLSLShader::sCurBoundShaderPtr->uniform1f(LLShaderMgr::DEFERRED_SHADOW_TARGET_WIDTH, (float)target_width);
                LLHLSLShader::sCurBoundShaderPtr->setMinimumAlpha(ALPHA_BLEND_CUTOFF);
                LLRenderPass::pushGLTFBatch(*pparams);
            }
            else
            {
                gDeferredShadowAlphaMaskProgram.bind(rigged);
                LLHLSLShader::sCurBoundShaderPtr->uniform1i(LLShaderMgr::SUN_UP_FACTOR, sun_up);
                LLHLSLShader::sCurBoundShaderPtr->uniform1f(LLShaderMgr::DEFERRED_SHADOW_TARGET_WIDTH, (float)target_width);
                LLHLSLShader::sCurBoundShaderPtr->setMinimumAlpha(ALPHA_BLEND_CUTOFF);
                pipeline.getSimplePool()->pushBatch(*pparams, true, true);
            }
        }
    }

    gDX.loadMatrix(gDXModelView);
    gDXLastMatrix = NULL;
}

// Currently only used for shadows -Cosmic,2023-04-19
// static
void DXPipeline::renderMaskedObjects(LLPipeline& pipeline, U32 type, bool texture, bool batch_texture, bool rigged)
{
    pipeline.assertInitialized();
    gDX.loadMatrix(gDXModelView);
    gDXLastMatrix = NULL;
    if (rigged)
    {
        pipeline.getAlphaMaskPool()->pushRiggedMaskBatches(type + 1, texture, batch_texture);
    }
    else
    {
        pipeline.getAlphaMaskPool()->pushMaskBatches(type, texture, batch_texture);
    }
    gDX.loadMatrix(gDXModelView);
    gDXLastMatrix = NULL;
}

// Currently only used for shadows -Cosmic,2023-04-19
// static
void DXPipeline::renderFullbrightMaskedObjects(LLPipeline& pipeline, U32 type, bool texture, bool batch_texture, bool rigged)
{
    pipeline.assertInitialized();
    gDX.loadMatrix(gDXModelView);
    gDXLastMatrix = NULL;
    if (rigged)
    {
        pipeline.getFullbrightAlphaMaskPool()->pushRiggedMaskBatches(type + 1, texture, batch_texture);
    }
    else
    {
        pipeline.getFullbrightAlphaMaskPool()->pushMaskBatches(type, texture, batch_texture);
    }
    gDX.loadMatrix(gDXModelView);
    gDXLastMatrix = NULL;
}

// Used only by renderShadow() below.
static LLTrace::BlockTimerStatHandle FTM_SHADOW_RENDER("Render Shadows");
static LLTrace::BlockTimerStatHandle FTM_SHADOW_ALPHA("Alpha Shadow");
static LLTrace::BlockTimerStatHandle FTM_SHADOW_SIMPLE("Simple Shadow");
static LLTrace::BlockTimerStatHandle FTM_SHADOW_GEOM("Shadow Geom");
static LLTrace::BlockTimerStatHandle FTM_SHADOW_ALPHA_MASKED("Alpha Masked");
static LLTrace::BlockTimerStatHandle FTM_SHADOW_ALPHA_BLEND("Alpha Blend");
static LLTrace::BlockTimerStatHandle FTM_SHADOW_ALPHA_GRASS("Alpha Grass");
static LLTrace::BlockTimerStatHandle FTM_SHADOW_FULLBRIGHT_ALPHA_MASKED("Fullbright Alpha Masked");

// ============================================================================
// Shadow generation
// ============================================================================

// static
void DXPipeline::renderShadow(LLPipeline& pipeline, const glm::mat4& view, const glm::mat4& proj, LLCamera& shadow_cam, LLCullResult& result, bool depth_clamp)
{
    LL_RECORD_BLOCK_TIME(FTM_SHADOW_RENDER);

    DXPipeline::sShadowRender = true;

    // disable occlusion culling during shadow render
    U32 saved_occlusion = DXPipeline::sUseOcclusion;
    DXPipeline::sUseOcclusion = 0;

    // List of render pass types that use the prim volume as the shadow,
    // ignoring textures.
    static const U32 types[] = {
        LLRenderPass::PASS_SIMPLE,
        LLRenderPass::PASS_FULLBRIGHT,
        LLRenderPass::PASS_SHINY,
        LLRenderPass::PASS_BUMP,
        LLRenderPass::PASS_FULLBRIGHT_SHINY,
        LLRenderPass::PASS_MATERIAL,
        LLRenderPass::PASS_MATERIAL_ALPHA_EMISSIVE,
        LLRenderPass::PASS_SPECMAP,
        LLRenderPass::PASS_SPECMAP_EMISSIVE,
        LLRenderPass::PASS_NORMMAP,
        LLRenderPass::PASS_NORMMAP_EMISSIVE,
        LLRenderPass::PASS_NORMSPEC,
        LLRenderPass::PASS_NORMSPEC_EMISSIVE
    };

    LLGLEnable cull(GL_CULL_FACE);

    //enable depth clamping if available
    LLGLEnable clamp_depth(depth_clamp ? GL_DEPTH_CLAMP : 0);

    DXDepthTest depth_test(true, true, GL_LESS);

    DXPipeline::updateCull(pipeline, shadow_cam, result);

    DXPipeline::stateSort(pipeline, shadow_cam, result);

    //generate shadow map
    gDX.matrixMode(LLRender::MM_PROJECTION);
    gDX.pushMatrix();
    gDX.loadMatrix(glm::value_ptr(proj));
    gDX.matrixMode(LLRender::MM_MODELVIEW);
    gDX.pushMatrix();
    gDX.loadMatrix(glm::value_ptr(view));

    gDXLastMatrix = NULL;

    gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);


    struct CompareVertexBuffer
    {
        bool operator()(const LLDrawInfo* const& lhs, const LLDrawInfo* const& rhs)
        {
            return lhs->mVertexBuffer > rhs->mVertexBuffer;
        }
    };


    LLVertexBuffer::unbind();
    for (int j = 0; j < 2; ++j) // 0 -- static, 1 -- rigged
    {
        bool rigged = j == 1;
        gDeferredShadowProgram.bind(rigged);

        gDX.diffuseColor4f(1, 1, 1, 1);

        S32 shadow_detail = DXPipeline::RenderShadowDetail;

        // if not using VSM, disable color writes
        if (shadow_detail <= 2)
        {
            gDX.setColorWriteMask(false, false);
        }

        LL_RECORD_BLOCK_TIME(FTM_SHADOW_SIMPLE);
        gDX.getTexUnit(0)->disable();

        for (U32 type : types)
        {
            DXPipeline::renderObjects(pipeline, type, false, false, rigged);
        }

        DXPipeline::renderGLTFObjects(pipeline, LLRenderPass::PASS_GLTF_PBR, false, rigged);

        gDX.getTexUnit(0)->enable(LLTexUnit::TT_TEXTURE);
    }

    if (DXPipeline::sUseOcclusion > 1)
    { // do occlusion culling against non-masked only to take advantage of hierarchical Z
        DXPipeline::doOcclusion(pipeline, shadow_cam);
    }


    {
        LL_RECORD_BLOCK_TIME(FTM_SHADOW_GEOM);
        DXPipeline::renderGeomShadow(pipeline, shadow_cam);
    }

    {
        LL_RECORD_BLOCK_TIME(FTM_SHADOW_ALPHA);
        const S32 sun_up = LLEnvironment::instance().getIsSunUp() ? 1 : 0;
        U32 target_width = LLRenderTarget::sCurResX;

        for (int i = 0; i < 2; ++i)
        {
            bool rigged = i == 1;

            {
                LL_RECORD_BLOCK_TIME(FTM_SHADOW_ALPHA_MASKED);
                gDeferredShadowAlphaMaskProgram.bind(rigged);
                LLHLSLShader::sCurBoundShaderPtr->uniform1i(LLShaderMgr::SUN_UP_FACTOR, sun_up);
                LLHLSLShader::sCurBoundShaderPtr->uniform1f(LLShaderMgr::DEFERRED_SHADOW_TARGET_WIDTH, (float)target_width);
                DXPipeline::renderMaskedObjects(pipeline, LLRenderPass::PASS_ALPHA_MASK, true, true, rigged);
            }

            {
                LL_RECORD_BLOCK_TIME(FTM_SHADOW_ALPHA_BLEND);
                DXPipeline::renderAlphaObjects(pipeline, rigged);
            }

            {
                LL_RECORD_BLOCK_TIME(FTM_SHADOW_FULLBRIGHT_ALPHA_MASKED);
                gDeferredShadowFullbrightAlphaMaskProgram.bind(rigged);
                LLHLSLShader::sCurBoundShaderPtr->uniform1i(LLShaderMgr::SUN_UP_FACTOR, sun_up);
                LLHLSLShader::sCurBoundShaderPtr->uniform1f(LLShaderMgr::DEFERRED_SHADOW_TARGET_WIDTH, (float)target_width);
                DXPipeline::renderFullbrightMaskedObjects(pipeline, LLRenderPass::PASS_FULLBRIGHT_ALPHA_MASK, true, true, rigged);
            }

            {
                LL_RECORD_BLOCK_TIME(FTM_SHADOW_ALPHA_GRASS);
                gDeferredTreeShadowProgram.bind(rigged);
                LLHLSLShader::sCurBoundShaderPtr->setMinimumAlpha(ALPHA_BLEND_CUTOFF);

                if (i == 0)
                {
                    DXPipeline::renderObjects(pipeline, LLRenderPass::PASS_GRASS, true);
                }

                {
                    DXPipeline::renderMaskedObjects(pipeline, LLRenderPass::PASS_NORMSPEC_MASK, true, false, rigged);
                    DXPipeline::renderMaskedObjects(pipeline, LLRenderPass::PASS_MATERIAL_ALPHA_MASK, true, false, rigged);
                    DXPipeline::renderMaskedObjects(pipeline, LLRenderPass::PASS_SPECMAP_MASK, true, false, rigged);
                    DXPipeline::renderMaskedObjects(pipeline, LLRenderPass::PASS_NORMMAP_MASK, true, false, rigged);
                }
            }
        }

        for (int i = 0; i < 2; ++i)
        {
            bool rigged = i == 1;
            gDeferredShadowGLTFAlphaMaskProgram.bind(rigged);
            LLHLSLShader::sCurBoundShaderPtr->uniform1i(LLShaderMgr::SUN_UP_FACTOR, sun_up);
            LLHLSLShader::sCurBoundShaderPtr->uniform1f(LLShaderMgr::DEFERRED_SHADOW_TARGET_WIDTH, (float)target_width);

            gDX.loadMatrix(gDXModelView);
            gDXLastMatrix = NULL;

            U32 type = LLRenderPass::PASS_GLTF_PBR_ALPHA_MASK;

            if (rigged)
            {
                pipeline.getAlphaMaskPool()->pushRiggedGLTFBatches(type + 1);
            }
            else
            {
                pipeline.getAlphaMaskPool()->pushGLTFBatches(type);
            }

            gDX.loadMatrix(gDXModelView);
            gDXLastMatrix = NULL;
        }
    }

    gDeferredShadowCubeProgram.bind();
    gDXLastMatrix = NULL;
    gDX.loadMatrix(gDXModelView);

    gDX.setColorWriteMask(true, true);

    gDX.matrixMode(LLRender::MM_PROJECTION);
    gDX.popMatrix();
    gDX.matrixMode(LLRender::MM_MODELVIEW);
    gDX.popMatrix();
    gDXLastMatrix = NULL;

    // reset occlusion culling flag
    DXPipeline::sUseOcclusion = saved_occlusion;
    DXPipeline::sShadowRender = false;
}

// static
bool DXPipeline::getVisiblePointCloud(LLPipeline& pipeline, LLCamera& camera, LLVector3& min, LLVector3& max, std::vector<LLVector3>& fp, LLVector3 light_dir)
{
    //get point cloud of intersection of frust and min, max

    if (DXPipeline::getVisibleExtents(pipeline, camera, min, max))
    {
        return false;
    }

    // Clip shadow cascades at water plane when camera is above water - avoids wasting shadow texels on
    // underwater geometry and eliminates waterline flicker.
    F32 water_height = LLEnvironment::instance().getWaterHeight();
    if (camera.getOrigin().mV[2] > water_height)
    {
        // Camera is above water - clip shadow volume at water plane
        min.mV[2] = llmax(min.mV[2], water_height);
    }

    //get set of planes on bounding box
    LLPlane bp[] = {
        LLPlane(min, LLVector3(-1,0,0)),
        LLPlane(min, LLVector3(0,-1,0)),
        LLPlane(min, LLVector3(0,0,-1)),
        LLPlane(max, LLVector3(1,0,0)),
        LLPlane(max, LLVector3(0,1,0)),
        LLPlane(max, LLVector3(0,0,1)) };

    //potential points
    std::vector<LLVector3> pp;

    //add corners of AABB
    pp.push_back(LLVector3(min.mV[0], min.mV[1], min.mV[2]));
    pp.push_back(LLVector3(max.mV[0], min.mV[1], min.mV[2]));
    pp.push_back(LLVector3(min.mV[0], max.mV[1], min.mV[2]));
    pp.push_back(LLVector3(max.mV[0], max.mV[1], min.mV[2]));
    pp.push_back(LLVector3(min.mV[0], min.mV[1], max.mV[2]));
    pp.push_back(LLVector3(max.mV[0], min.mV[1], max.mV[2]));
    pp.push_back(LLVector3(min.mV[0], max.mV[1], max.mV[2]));
    pp.push_back(LLVector3(max.mV[0], max.mV[1], max.mV[2]));

    //add corners of camera frustum
    for (U32 i = 0; i < LLCamera::AGENT_FRUSTRUM_NUM; i++)
    {
        pp.push_back(camera.mAgentFrustum[i]);
    }

    // The two 12-edge x 6-plane clipping loops below can push at most one point per iteration each (72
    // worst case) - reserve that up front so neither loop causes a reallocation/copy partway through.
    pp.reserve(pp.size() + 72 + 72);

    //bounding box line segments
    U32 bs[] =
    {
0,1,
1,3,
3,2,
2,0,

4,5,
5,7,
7,6,
6,4,

0,4,
1,5,
3,7,
2,6
    };

    // getAgentPlane(j)'s normal only depends on j, not the outer edge index i - extracted once here
    // instead of being recomputed 12x redundantly per plane inside the loop below.
    LLVector3 frustumPlaneNormals[LLCamera::AGENT_PLANE_NO_USER_CLIP_NUM];
    for (U32 j = 0; j < LLCamera::AGENT_PLANE_NO_USER_CLIP_NUM; ++j)
    {
        camera.getAgentPlane(j).getVector3(frustumPlaneNormals[j]);
    }

    for (size_t i = 0; i < 12; i++)
    { //for each line segment in bounding box
        for (U32 j = 0; j < LLCamera::AGENT_PLANE_NO_USER_CLIP_NUM; j++)
        { //for each plane in camera frustum
            const LLPlane& cp = camera.getAgentPlane(j);
            const LLVector3& v1 = pp[bs[i * 2 + 0]];
            const LLVector3& v2 = pp[bs[i * 2 + 1]];
            const LLVector3& n = frustumPlaneNormals[j];

            LLVector3 line = v1 - v2;

            F32 d1 = line * n;
            F32 d2 = -cp.dist(v2);

            F32 t = d2 / d1;

            if (t > 0.f && t < 1.f)
            {
                LLVector3 intersect = v2 + line * t;
                pp.push_back(intersect);
            }
        }
    }

    //camera frustum line segments
    const U32 fs[] =
    {
        0,1,
        1,2,
        2,3,
        3,0,

        4,5,
        5,6,
        6,7,
        7,4,

        0,4,
        1,5,
        2,6,
        3,7
    };

    // Same redundant-recompute pattern as the bs[] loop above - bp[j]'s normal only depends on j,
    // extracted once instead of 12x per plane.
    LLVector3 boxPlaneNormals[6];
    for (U32 j = 0; j < 6; ++j)
    {
        bp[j].getVector3(boxPlaneNormals[j]);
    }

    for (size_t i = 0; i < 12; i++)
    {
        for (U32 j = 0; j < 6; ++j)
        {
            const LLVector3& v1 = pp[fs[i * 2 + 0] + 8];
            const LLVector3& v2 = pp[fs[i * 2 + 1] + 8];
            const LLPlane& cp = bp[j];
            const LLVector3& n = boxPlaneNormals[j];

            LLVector3 line = v1 - v2;

            F32 d1 = line * n;
            F32 d2 = -cp.dist(v2);

            F32 t = d2 / d1;

            if (t > 0.f && t < 1.f)
            {
                LLVector3 intersect = v2 + line * t;
                pp.push_back(intersect);
            }
        }
    }

    LLVector3 ext[] = { min - LLVector3(0.05f,0.05f,0.05f),
        max + LLVector3(0.05f,0.05f,0.05f) };

    for (U32 i = 0; i < pp.size(); ++i)
    {
        bool found = true;

        const F32* p = pp[i].mV;

        for (U32 j = 0; j < 3; ++j)
        {
            if (p[j] < ext[0].mV[j] ||
                p[j] > ext[1].mV[j])
            {
                found = false;
                break;
            }
        }

        for (U32 j = 0; j < LLCamera::AGENT_PLANE_NO_USER_CLIP_NUM; ++j)
        {
            const LLPlane& cp = camera.getAgentPlane(j);
            F32 dist = cp.dist(pp[i]);
            if (dist > 0.05f) //point is above some plane, not contained
            {
                found = false;
                break;
            }
        }

        if (found)
        {
            fp.push_back(pp[i]);
        }
    }

    if (fp.empty())
    {
        return false;
    }

    return true;
}

// Used only by generateSunShadow() below.
static glm::mat4 look(const LLVector3 pos, const LLVector3 dir, const LLVector3 up)
{
    LLVector3 dirN;
    LLVector3 upN;
    LLVector3 lftN;

    lftN = dir % up;
    lftN.normVec();

    upN = lftN % dir;
    upN.normVec();

    dirN = dir;
    dirN.normVec();

    F32 ret[16] = {};
    ret[0] = lftN[0];
    ret[1] = upN[0];
    ret[2] = -dirN[0];
    ret[3] = 0.f;

    ret[4] = lftN[1];
    ret[5] = upN[1];
    ret[6] = -dirN[1];
    ret[7] = 0.f;

    ret[8] = lftN[2];
    ret[9] = upN[2];
    ret[10] = -dirN[2];
    ret[11] = 0.f;

    ret[12] = -(lftN * pos);
    ret[13] = -(upN * pos);
    ret[14] = dirN * pos;
    ret[15] = 1.f;

    return glm::make_mat4(ret);
}

// Used only by generateSunShadow() below.
static LLTrace::BlockTimerStatHandle FTM_GEN_SUN_SHADOW("Gen Sun Shadow");

// helper class for disabling occlusion culling for the current stack frame
class LLDisableOcclusionCulling
{
public:
    S32 mUseOcclusion;

    LLDisableOcclusionCulling()
    {
        mUseOcclusion = DXPipeline::sUseOcclusion;
        DXPipeline::sUseOcclusion = 0;
    }

    ~LLDisableOcclusionCulling()
    {
        DXPipeline::sUseOcclusion = mUseOcclusion;
    }
};

// static
void DXPipeline::generateSunShadow(LLPipeline& pipeline, LLCamera& camera)
{
    // This function's body (cascade fitting, mRT->shadow[j] bind/clear/renderShadow()/flush per
    // cascade, spot-light shadows) is backend-agnostic and reused as-is under DX_RENDER, same pattern
    // as renderGeomDeferred()/renderGeomPostDeferred(). See this file's renderDeferredLighting()
    // for the sun-shadow/SSAO lightmap combine pass consuming this function's output.
    if (!DXPipeline::sRenderDeferred || DXPipeline::RenderShadowDetail <= 0)
    {
        return;
    }

    LL_RECORD_BLOCK_TIME(FTM_GEN_SUN_SHADOW);

    // S24: defensive backstop - DXState::sCullFace is a genuinely global, un-scoped rasterizer
    // flag (see LLGLCullFace's own comment in llgl.h), so an unrelated leftover from a previous
    // frame/pass could otherwise silently corrupt cull direction for every object this shadow
    // pass draws (sun + spot shadows, alpha/complex geometry included). Reset to GL's own
    // default here so this pass always starts from a known-good state regardless.
    DXState::setCullFace(GL_BACK);

    LLDisableOcclusionCulling no_occlusion;

    bool skip_avatar_update = false;
    if (!isAgentAvatarValid() || gAgentCamera.getCameraAnimating() || gAgentCamera.getCameraMode() != CAMERA_MODE_MOUSELOOK || !LLVOAvatar::sVisibleInFirstPerson)
    {
        skip_avatar_update = true;
    }

    if (!skip_avatar_update)
    {
        gAgentAvatarp->updateAttachmentVisibility(CAMERA_MODE_THIRD_PERSON);
    }

    glm::mat4 last_modelview = get_last_modelview();
    glm::mat4 last_projection = get_last_projection();

    DXPipeline::pushRenderTypeMask(pipeline);
    DXPipeline::andRenderTypeMask(pipeline, LLPipeline::RENDER_TYPE_SIMPLE,
        LLPipeline::RENDER_TYPE_ALPHA,
        LLPipeline::RENDER_TYPE_ALPHA_PRE_WATER,
        LLPipeline::RENDER_TYPE_ALPHA_POST_WATER,
        LLPipeline::RENDER_TYPE_GRASS,
        LLPipeline::RENDER_TYPE_GLTF_PBR,
        LLPipeline::RENDER_TYPE_FULLBRIGHT,
        LLPipeline::RENDER_TYPE_BUMP,
        LLPipeline::RENDER_TYPE_VOLUME,
        LLPipeline::RENDER_TYPE_AVATAR,
        LLPipeline::RENDER_TYPE_CONTROL_AV,
        LLPipeline::RENDER_TYPE_TREE,
        LLPipeline::RENDER_TYPE_TERRAIN,
        LLPipeline::RENDER_TYPE_WATER,
        LLPipeline::RENDER_TYPE_VOIDWATER,
        LLPipeline::RENDER_TYPE_PASS_ALPHA,
        LLPipeline::RENDER_TYPE_PASS_ALPHA_MASK,
        LLPipeline::RENDER_TYPE_PASS_FULLBRIGHT_ALPHA_MASK,
        LLPipeline::RENDER_TYPE_PASS_GRASS,
        LLPipeline::RENDER_TYPE_PASS_SIMPLE,
        LLPipeline::RENDER_TYPE_PASS_BUMP,
        LLPipeline::RENDER_TYPE_PASS_FULLBRIGHT,
        LLPipeline::RENDER_TYPE_PASS_SHINY,
        LLPipeline::RENDER_TYPE_PASS_FULLBRIGHT_SHINY,
        LLPipeline::RENDER_TYPE_PASS_MATERIAL,
        LLPipeline::RENDER_TYPE_PASS_MATERIAL_ALPHA,
        LLPipeline::RENDER_TYPE_PASS_MATERIAL_ALPHA_MASK,
        LLPipeline::RENDER_TYPE_PASS_MATERIAL_ALPHA_EMISSIVE,
        LLPipeline::RENDER_TYPE_PASS_SPECMAP,
        LLPipeline::RENDER_TYPE_PASS_SPECMAP_BLEND,
        LLPipeline::RENDER_TYPE_PASS_SPECMAP_MASK,
        LLPipeline::RENDER_TYPE_PASS_SPECMAP_EMISSIVE,
        LLPipeline::RENDER_TYPE_PASS_NORMMAP,
        LLPipeline::RENDER_TYPE_PASS_NORMMAP_BLEND,
        LLPipeline::RENDER_TYPE_PASS_NORMMAP_MASK,
        LLPipeline::RENDER_TYPE_PASS_NORMMAP_EMISSIVE,
        LLPipeline::RENDER_TYPE_PASS_NORMSPEC,
        LLPipeline::RENDER_TYPE_PASS_NORMSPEC_BLEND,
        LLPipeline::RENDER_TYPE_PASS_NORMSPEC_MASK,
        LLPipeline::RENDER_TYPE_PASS_NORMSPEC_EMISSIVE,
        LLPipeline::RENDER_TYPE_PASS_ALPHA_MASK_RIGGED,
        LLPipeline::RENDER_TYPE_PASS_FULLBRIGHT_ALPHA_MASK_RIGGED,
        LLPipeline::RENDER_TYPE_PASS_SIMPLE_RIGGED,
        LLPipeline::RENDER_TYPE_PASS_BUMP_RIGGED,
        LLPipeline::RENDER_TYPE_PASS_FULLBRIGHT_RIGGED,
        LLPipeline::RENDER_TYPE_PASS_SHINY_RIGGED,
        LLPipeline::RENDER_TYPE_PASS_FULLBRIGHT_SHINY_RIGGED,
        LLPipeline::RENDER_TYPE_PASS_MATERIAL_RIGGED,
        LLPipeline::RENDER_TYPE_PASS_MATERIAL_ALPHA_RIGGED,
        LLPipeline::RENDER_TYPE_PASS_MATERIAL_ALPHA_MASK_RIGGED,
        LLPipeline::RENDER_TYPE_PASS_MATERIAL_ALPHA_EMISSIVE_RIGGED,
        LLPipeline::RENDER_TYPE_PASS_SPECMAP_RIGGED,
        LLPipeline::RENDER_TYPE_PASS_SPECMAP_BLEND_RIGGED,
        LLPipeline::RENDER_TYPE_PASS_SPECMAP_MASK_RIGGED,
        LLPipeline::RENDER_TYPE_PASS_SPECMAP_EMISSIVE_RIGGED,
        LLPipeline::RENDER_TYPE_PASS_NORMMAP_RIGGED,
        LLPipeline::RENDER_TYPE_PASS_NORMMAP_BLEND_RIGGED,
        LLPipeline::RENDER_TYPE_PASS_NORMMAP_MASK_RIGGED,
        LLPipeline::RENDER_TYPE_PASS_NORMMAP_EMISSIVE_RIGGED,
        LLPipeline::RENDER_TYPE_PASS_NORMSPEC_RIGGED,
        LLPipeline::RENDER_TYPE_PASS_NORMSPEC_BLEND_RIGGED,
        LLPipeline::RENDER_TYPE_PASS_NORMSPEC_MASK_RIGGED,
        LLPipeline::RENDER_TYPE_PASS_NORMSPEC_EMISSIVE_RIGGED,
        LLPipeline::RENDER_TYPE_PASS_GLTF_PBR,
        LLPipeline::RENDER_TYPE_PASS_GLTF_PBR_RIGGED,
        LLPipeline::RENDER_TYPE_PASS_GLTF_PBR_ALPHA_MASK,
        LLPipeline::RENDER_TYPE_PASS_GLTF_PBR_ALPHA_MASK_RIGGED,
        LLPipeline::END_RENDER_TYPES);

    gDX.setColorWriteMask(false, false);

    LLEnvironment& environment = LLEnvironment::instance();

    //get sun view matrix

    //store current projection/modelview matrix
    glm::mat4 saved_proj = get_current_projection();
    glm::mat4 saved_view = get_current_modelview();
    glm::mat4 inv_view = glm::inverse(saved_view);

    // glm::mat4's default ctor is a no-op in this build (GLM_FORCE_CTOR_INIT is never defined) - without
    // zero-init, any cascade/spotlight slot skipped this frame (e.g. view[1]/proj[1] when cascade 1 has
    // no visible receivers) is raw stack garbage, later read unconditionally by the CameraOffset branch
    // below.
    glm::mat4 view[6] = {};
    glm::mat4 proj[6] = {};

    LLVector3 caster_dir(environment.getIsSunUp() ? pipeline.mSunDir : pipeline.mMoonDir);

    //put together a universal "near clip" plane for shadow frusta
    LLVector3 shadow_near_clip_point = camera.getOrigin(); // gAgent.getPositionAgent();
    shadow_near_clip_point += caster_dir * DXPipeline::RenderFarClip * 2.f;
    LLPlane shadow_near_clip(shadow_near_clip_point, caster_dir);

    LLVector3 lightDir = -caster_dir;
    lightDir.normVec();

    //create light space camera matrix
    LLVector3 at = lightDir;

    LLVector3 up = camera.getAtAxis();

    if (fabsf(up * lightDir) > 0.75f)
    {
        up = camera.getUpAxis();
    }

    up.normVec();
    at.normVec();

    LLCamera main_camera = camera;

    F32 near_clip = 0.f;
    {
        //get visible point cloud
        std::vector<LLVector3> fp;

        main_camera.calcAgentFrustumPlanes(main_camera.mAgentFrustum);

        LLVector3 min, max;
        DXPipeline::getVisiblePointCloud(pipeline, main_camera, min, max, fp);

        if (fp.empty())
        {
            if (!pipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_SHADOW_FRUSTA) && !gCubeSnapshot)
            {
                pipeline.mShadowCamera[0] = main_camera;
                pipeline.mShadowExtents[0][0] = min;
                pipeline.mShadowExtents[0][1] = max;

                pipeline.mShadowFrustPoints[0].clear();
                pipeline.mShadowFrustPoints[1].clear();
                pipeline.mShadowFrustPoints[2].clear();
                pipeline.mShadowFrustPoints[3].clear();
            }
            DXPipeline::popRenderTypeMask(pipeline);

            if (!skip_avatar_update)
            {
                gAgentAvatarp->updateAttachmentVisibility(gAgentCamera.getCameraMode());
            }

            return;
        }

        //get good split distances for frustum
        for (U32 i = 0; i < fp.size(); ++i)
        {
            glm::vec3 v(fp[i]);
            v = mul_mat4_vec3(saved_view, v);
            fp[i] = LLVector3(v);
        }

        min = fp[0];
        max = fp[0];

        //get camera space bounding box
        for (U32 i = 1; i < fp.size(); ++i)
        {
            update_min_max(min, max, fp[i]);
        }

        near_clip = llclamp(-max.mV[2], 0.01f, 4.0f);
        F32 far_clip = llclamp(-min.mV[2] * 2.f, 16.0f, 512.0f);

        //far_clip = llmin(far_clip, 128.f);
        far_clip = llmin(far_clip, camera.getFar());

        // Cap shadow distance based on draw distance to reduce wasted computation
        // At high draw distances (>200m), don't render shadows for the full far clip
        // This provides substantial performance improvement with minimal visual impact
        F32 shadow_distance_cap = llmin(camera.getFar() * 0.5f, 256.0f);
        far_clip = llmin(far_clip, shadow_distance_cap);

        F32 range = far_clip - near_clip;

        LLVector3 split_exp = DXPipeline::RenderShadowSplitExponent;

        F32 da = 1.f - llmax(fabsf(lightDir * up), fabsf(lightDir * camera.getLeftAxis()));

        da = powf(da, split_exp.mV[2]);

        F32 sxp = split_exp.mV[1] + (split_exp.mV[0] - split_exp.mV[1]) * da;

        for (U32 i = 0; i < 4; ++i)
        {
            F32 x = (F32)(i + 1) / 4.f;
            x = powf(x, sxp);
            pipeline.mSunClipPlanes.mV[i] = near_clip + range * x;
        }

        pipeline.mSunClipPlanes.mV[0] *= 1.25f; //bump back first split for transition padding
    }

    if (gCubeSnapshot)
    { // stretch clip planes for reflection probe renders to reduce number of shadow passes
        pipeline.mSunClipPlanes.mV[1] = pipeline.mSunClipPlanes.mV[2];
        pipeline.mSunClipPlanes.mV[2] = pipeline.mSunClipPlanes.mV[3];
        pipeline.mSunClipPlanes.mV[3] *= 1.5f;
    }

    // convenience array of 4 near clip plane distances
    F32 dist[] = { near_clip, pipeline.mSunClipPlanes.mV[0], pipeline.mSunClipPlanes.mV[1], pipeline.mSunClipPlanes.mV[2], pipeline.mSunClipPlanes.mV[3] };

    if (pipeline.mSunDiffuse == LLColor4::black)
    { //sun diffuse is totally black shadows don't matter
        DXPipeline::skipRenderingShadows(pipeline);
    }
    else
    {
        // Camera-movement-aware cascade throttle: every cascade renders every frame while the camera
        // moves. After it stops, a short settle period of full updates runs before throttling
        // re-engages, avoiding the transition flicker a fixed-modulo throttle would produce.
        bool shadow_throttle_active = false;
        static LLCachedControl<bool> throttle_enabled(gSavedSettings, "RenderShadowThrottleEnabled", true);
        if (throttle_enabled && !gCubeSnapshot && !pipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_SHADOW_FRUSTA))
        {
            static LLVector3 s_last_pos;
            static LLVector3 s_last_at;
            static U32       s_settle = 0;
            static U32       s_wake = 0;              // consecutive "moved" frames seen while throttled
            static bool      s_throttle_active = false;
            static LLCachedControl<U32> settle_frames(gSavedSettings, "RenderShadowThrottleSettleFrames", 6U);

            const LLVector3 cur_pos = camera.getOrigin();
            const LLVector3 cur_at = camera.getAtAxis();

            // pos_delta: metres moved this frame; rot_delta: 0 = identical, >0 = rotated
            const F32 pos_delta = (cur_pos - s_last_pos).magVec();
            const F32 rot_delta = 1.0f - llabs(cur_at * s_last_at);
            const bool moved = (pos_delta > 0.01f || rot_delta > 0.0001f);

            // S24: exit debounce, symmetric to the settle-frame entry debounce above. A slow
            // camera-lag settle tail (soft CameraPositionSmoothing/DynamicCameraStrength) decays
            // exponentially - pos_delta can hover right at the 0.01f threshold for many
            // consecutive frames, crossing it on ordinary frame-time noise alone. Without this,
            // a single borderline frame flips shadow_throttle_active off then straight back on,
            // popping cascades 2/3 between their stale and freshly-computed frustum/matrix each
            // time - visible as flicker. Requiring 2 consecutive moved frames before dropping
            // throttle absorbs that noise; entry into throttle (the existing settle_frames count)
            // is unchanged.
            if (moved)
            {
                s_settle = 0;                                    // camera moved - reset settle counter
                if (s_throttle_active && ++s_wake >= 2)
                {
                    s_throttle_active = false;
                    s_wake = 0;
                }
            }
            else
            {
                s_wake = 0;
                if (s_settle < (U32)settle_frames)
                    ++s_settle;                                  // camera still - count up to threshold
                if (!s_throttle_active && s_settle >= (U32)settle_frames)
                    s_throttle_active = true;
            }

            s_last_pos = cur_pos;
            s_last_at = cur_at;

            shadow_throttle_active = s_throttle_active;
        }

        static LLCachedControl<U32> mid_cascade_throttle(gSavedSettings, "RenderShadowMidCascadeThrottle", 2U);
        static LLCachedControl<U32> far_cascade_throttle(gSavedSettings, "RenderShadowFarCascadeThrottle", 4U);

        for (S32 j = 0; j < (gCubeSnapshot ? 2 : 4); j++)
        {
            // Cascade throttling (only when camera is stationary): 0+1 (near) always update; 2 (mid)
            // and 3 (far) throttle per RenderShadowMidCascadeThrottle/RenderShadowFarCascadeThrottle.
            if (shadow_throttle_active)
            {
                if (j == 2 && mid_cascade_throttle > 1 && (gFrameCount % (U32)mid_cascade_throttle) != 0) continue;
                if (j == 3 && far_cascade_throttle > 1 && (gFrameCount % (U32)far_cascade_throttle) != 0) continue;
            }

            if (!pipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_SHADOW_FRUSTA) && !gCubeSnapshot)
            {
                pipeline.mShadowFrustPoints[j].clear();
            }

            LLViewerCamera::sCurCameraID = (LLViewerCamera::eCameraID)(LLViewerCamera::CAMERA_SUN_SHADOW0 + j);

            //restore render matrices
            set_current_modelview(saved_view);
            set_current_projection(saved_proj);

            LLVector3 eye = camera.getOrigin();
            llassert(eye.isFinite());

            //camera used for shadow cull/render
            LLCamera shadow_cam;

            //create world space camera frustum for this split
            shadow_cam = camera;
            shadow_cam.setFar(16.f);

            LLViewerCamera::updateFrustumPlanes(shadow_cam, false, false, true);

            LLVector3* frust = shadow_cam.mAgentFrustum;

            LLVector3 pn = shadow_cam.getAtAxis();

            LLVector3 min, max;

            //construct 8 corners of split frustum section
            for (U32 i = 0; i < 4; i++)
            {
                LLVector3 delta = frust[i + 4] - eye;
                delta += (frust[i + 4] - frust[(i + 2) % 4 + 4]) * 0.05f;
                delta.normVec();
                F32 dp = delta * pn;
                frust[i] = eye + (delta * dist[j] * 0.75f) / dp;
                frust[i + 4] = eye + (delta * dist[j + 1] * 1.25f) / dp;
            }

            shadow_cam.calcAgentFrustumPlanes(frust);
            shadow_cam.mFrustumCornerDist = 0.f;

            if (!pipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_SHADOW_FRUSTA) && !gCubeSnapshot)
            {
                pipeline.mShadowCamera[j] = shadow_cam;
            }

            std::vector<LLVector3> fp;

            if (!DXPipeline::getVisiblePointCloud(pipeline, shadow_cam, min, max, fp, lightDir)
                || j > DXPipeline::RenderShadowSplits)
            {
                //no possible shadow receivers
                if (!pipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_SHADOW_FRUSTA) && !gCubeSnapshot)
                {
                    pipeline.mShadowExtents[j][0] = LLVector3();
                    pipeline.mShadowExtents[j][1] = LLVector3();
                    pipeline.mShadowCamera[j + 4] = shadow_cam;
                }

                pipeline.mRT->shadow[j].bindTarget();
                {
                    DXDepthTest depth(true);
                    pipeline.mRT->shadow[j].clear();
                }
                pipeline.mRT->shadow[j].flush();

                pipeline.mShadowError.mV[j] = 0.f;
                pipeline.mShadowFOV.mV[j] = 0.f;

                continue;
            }

            if (!pipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_SHADOW_FRUSTA) && !gCubeSnapshot)
            {
                pipeline.mShadowExtents[j][0] = min;
                pipeline.mShadowExtents[j][1] = max;
                pipeline.mShadowFrustPoints[j] = fp;
            }

            //find a good origin for shadow projection
            LLVector3 origin;

            //get a temporary view projection
            view[j] = look(camera.getOrigin(), lightDir, -up);

            std::vector<LLVector3> wpf;

            for (U32 i = 0; i < fp.size(); i++)
            {
                glm::vec3 p(fp[i]);
                p = mul_mat4_vec3(view[j], p);
                wpf.push_back(LLVector3(p));
            }

            min = wpf[0];
            max = wpf[0];

            for (U32 i = 0; i < fp.size(); ++i)
            { //get AABB in camera space
                update_min_max(min, max, wpf[i]);
            }

            // Construct a perspective transform with perspective along y-axis that contains
            // points in wpf
            //Known:
            // - far clip plane
            // - near clip plane
            // - points in frustum
            //Find:
            // - origin

            //get some "interesting" points of reference
            LLVector3 center = (min + max) * 0.5f;
            LLVector3 size = (max - min) * 0.5f;
            LLVector3 near_center = center;
            near_center.mV[1] += size.mV[1] * 2.f;

            //put all points in wpf in quadrant 0, reletive to center of min/max
            //get the best fit line using least squares
            F32 bfm = 0.f;
            F32 bfb = 0.f;

            for (U32 i = 0; i < wpf.size(); ++i)
            {
                wpf[i] -= center;
                wpf[i].mV[0] = fabsf(wpf[i].mV[0]);
                wpf[i].mV[2] = fabsf(wpf[i].mV[2]);
            }

            if (!wpf.empty())
            {
                F32 sx = 0.f;
                F32 sx2 = 0.f;
                F32 sy = 0.f;
                F32 sxy = 0.f;

                for (U32 i = 0; i < wpf.size(); ++i)
                {
                    sx += wpf[i].mV[0];
                    sx2 += wpf[i].mV[0] * wpf[i].mV[0];
                    sy += wpf[i].mV[1];
                    sxy += wpf[i].mV[0] * wpf[i].mV[1];
                }

                bfm = (sy * sx - wpf.size() * sxy) / (sx * sx - wpf.size() * sx2);
                bfb = (sx * sxy - sy * sx2) / (sx * sx - bfm * sx2);
            }

            {
                // best fit line is y=bfm*x+bfb

                //find point that is furthest to the right of line
                F32 off_x = -1.f;
                LLVector3 lp;

                for (U32 i = 0; i < wpf.size(); ++i)
                {
                    //y = bfm*x+bfb
                    //x = (y-bfb)/bfm
                    F32 lx = (wpf[i].mV[1] - bfb) / bfm;

                    lx = wpf[i].mV[0] - lx;

                    if (off_x < lx)
                    {
                        off_x = lx;
                        lp = wpf[i];
                    }
                }

                //get line with slope bfm through lp
                // bfb = y-bfm*x
                bfb = lp.mV[1] - bfm * lp.mV[0];

                //calculate error
                pipeline.mShadowError.mV[j] = 0.f;

                for (U32 i = 0; i < wpf.size(); ++i)
                {
                    F32 lx = (wpf[i].mV[1] - bfb) / bfm;
                    pipeline.mShadowError.mV[j] += fabsf(wpf[i].mV[0] - lx);
                }

                pipeline.mShadowError.mV[j] /= wpf.size();
                pipeline.mShadowError.mV[j] /= size.mV[0];

                if (pipeline.mShadowError.mV[j] > DXPipeline::RenderShadowErrorCutoff)
                { //just use ortho projection
                    pipeline.mShadowFOV.mV[j] = -1.f;
                    origin.clearVec();
                    proj[j] = glm::ortho(min.mV[0], max.mV[0],
                        min.mV[1], max.mV[1],
                        -max.mV[2], -min.mV[2]);
                }
                else
                {
                    //origin is where line x = 0;
                    origin.setVec(0, bfb, 0);

                    F32 fovz = 1.f;
                    F32 fovx = 1.f;

                    LLVector3 zp;
                    LLVector3 xp;

                    for (U32 i = 0; i < wpf.size(); ++i)
                    {
                        LLVector3 atz = wpf[i] - origin;
                        atz.mV[0] = 0.f;
                        atz.normVec();
                        if (fovz > -atz.mV[1])
                        {
                            zp = wpf[i];
                            fovz = -atz.mV[1];
                        }

                        LLVector3 atx = wpf[i] - origin;
                        atx.mV[2] = 0.f;
                        atx.normVec();
                        if (fovx > -atx.mV[1])
                        {
                            fovx = -atx.mV[1];
                            xp = wpf[i];
                        }
                    }

                    fovx = acos(fovx);
                    fovz = acos(fovz);

                    F32 cutoff = llmin((F32)DXPipeline::RenderShadowFOVCutoff, 1.4f);

                    pipeline.mShadowFOV.mV[j] = fovx;

                    if (fovx < cutoff && fovz > cutoff)
                    {
                        //x is a good fit, but z is too big, move away from zp enough so that fovz matches cutoff
                        F32 d = zp.mV[2] / tan(cutoff);
                        F32 ny = zp.mV[1] + fabsf(d);

                        origin.mV[1] = ny;

                        fovz = 1.f;
                        fovx = 1.f;

                        for (U32 i = 0; i < wpf.size(); ++i)
                        {
                            LLVector3 atz = wpf[i] - origin;
                            atz.mV[0] = 0.f;
                            atz.normVec();
                            fovz = llmin(fovz, -atz.mV[1]);

                            LLVector3 atx = wpf[i] - origin;
                            atx.mV[2] = 0.f;
                            atx.normVec();
                            fovx = llmin(fovx, -atx.mV[1]);
                        }

                        fovx = acos(fovx);
                        fovz = acos(fovz);

                        pipeline.mShadowFOV.mV[j] = cutoff;
                    }

                    origin += center;

                    F32 ynear = -(max.mV[1] - origin.mV[1]);
                    F32 yfar = -(min.mV[1] - origin.mV[1]);

                    if (ynear < 0.1f) //keep a sensible near clip plane
                    {
                        F32 diff = 0.1f - ynear;
                        origin.mV[1] += diff;
                        ynear += diff;
                        yfar += diff;
                    }

                    if (fovx > cutoff)
                    { //just use ortho projection
                        origin.clearVec();
                        pipeline.mShadowError.mV[j] = -1.f;
                        proj[j] = glm::ortho(min.mV[0], max.mV[0],
                            min.mV[1], max.mV[1],
                            -max.mV[2], -min.mV[2]);
                    }
                    else
                    {
                        //get perspective projection
                        view[j] = glm::inverse(view[j]);
                        //llassert(origin.isFinite());

                        glm::vec3 origin_agent(origin);

                        //translate view to origin
                        origin_agent = mul_mat4_vec3(view[j], origin_agent);

                        // Snap the shadow camera origin to shadow-map texels to prevent sub-pixel movement
                        // of the projection matrix - without this, cascades shimmer and flicker at distance.

                        // Shadow map resolution for this cascade
                        float shadowRes = (float)pipeline.mRT->shadow[j].getWidth();

                        // Compute the orthographic radius of this cascade.
                        // 'size' was computed earlier as (max - min) * 0.5f in light-space.
                        float orthoRadius = llmax(size.mV[0], size.mV[2]);

                        // World-space size of one shadow-map texel
                        float texelSize = (2.0f * orthoRadius) / shadowRes;

                        // Snap origin to texel grid
                        origin_agent.x = floorf(origin_agent.x / texelSize) * texelSize;
                        origin_agent.y = floorf(origin_agent.y / texelSize) * texelSize;
                        origin_agent.z = floorf(origin_agent.z / texelSize) * texelSize;

                        eye = LLVector3(origin_agent);
                        //llassert(eye.isFinite());
                        if (!pipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_SHADOW_FRUSTA) && !gCubeSnapshot)
                        {
                            pipeline.mShadowFrustOrigin[j] = eye;
                        }

                        view[j] = look(LLVector3(origin_agent), lightDir, -up);

                        F32 fx = 1.f / tanf(fovx);
                        F32 fz = 1.f / tanf(fovz);

                        proj[j] = glm::mat4(-fx, 0, 0, 0,
                            0, (yfar + ynear) / (ynear - yfar), 0, -1.0f,
                            0, 0, -fz, 0,
                            0, (2.f * yfar * ynear) / (ynear - yfar), 0, 0);
                    }
                }
            }

            //shadow_cam.setFar(128.f);
            shadow_cam.setOriginAndLookAt(eye, up, center);

            shadow_cam.setOrigin(0, 0, 0);

            set_current_modelview(view[j]);
            set_current_projection(proj[j]);

            LLViewerCamera::updateFrustumPlanes(shadow_cam, false, false, true);

            //shadow_cam.ignoreAgentFrustumPlane(LLCamera::AGENT_PLANE_NEAR);
            shadow_cam.getAgentPlane(LLCamera::AGENT_PLANE_NEAR).set(shadow_near_clip);

            //translate and scale to from [-1, 1] to [0, 1]
            // z-column flipped to -0.5f (was 0.5f): this matrix is a separate, independent copy of the
            // same GL-NDC-to-stored-depth mapping kGLtoDXDepthRemap (llrender.cpp) performs for the main
            // scene, hand-built here for mSunShadowMatrix. It must match the reversed-Z convention
            // (near=1.0/far=0.0) the shadow map texture itself is written with via proj[j]/syncMatrices(),
            // or the shader compares against a reference computed in the wrong convention. x/y rows are
            // texture U/V, unrelated to depth, left unchanged.
            glm::mat4 trans(0.5f, 0.0f, 0.0f, 0.0f,
                0.0f, 0.5f, 0.0f, 0.0f,
                0.0f, 0.0f, -0.5f, 0.0f,
                0.5f, 0.5f, 0.5f, 1.0f);

            set_current_modelview(view[j]);
            set_current_projection(proj[j]);

            set_last_modelview(pipeline.mShadowModelview[j]);
            set_last_projection(pipeline.mShadowProjection[j]);

            pipeline.mShadowModelview[j] = view[j];
            pipeline.mShadowProjection[j] = proj[j];
            pipeline.mSunShadowMatrix[j] = trans * proj[j] * view[j] * inv_view;


            pipeline.mRT->shadow[j].bindTarget();
            pipeline.mRT->shadow[j].getViewport(gDXViewport);
            pipeline.mRT->shadow[j].clear();

            {
                static LLCullResult result[4];
                DXPipeline::renderShadow(pipeline, view[j], proj[j], shadow_cam, result[j], true);
            }

            pipeline.mRT->shadow[j].flush();

            if (!pipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_SHADOW_FRUSTA) && !gCubeSnapshot)
            {
                pipeline.mShadowCamera[j + 4] = shadow_cam;
            }
        }
    }

    //hack to disable projector shadows
    bool gen_shadow = DXPipeline::RenderShadowDetail > 1;

    if (gen_shadow)
    {
        if (!gCubeSnapshot) //skip updating spot shadow maps during cubemap updates
        {
            LLTrace::CountStatHandle<>* velocity_stat = LLViewerCamera::getVelocityStat();
            F32 fade_amt = gFrameIntervalSeconds.value()
                * (F32)llmax(LLTrace::get_frame_recording().getLastRecording().getSum(*velocity_stat) / LLTrace::get_frame_recording().getLastRecording().getDuration().value(), 1.0);

            // should never happen
            llassert(pipeline.mTargetShadowSpotLight[0] != pipeline.mTargetShadowSpotLight[1] || pipeline.mTargetShadowSpotLight[0].isNull());

            //update shadow targets
            for (U32 i = 0; i < 2; i++)
            { //for each current shadow
                LLViewerCamera::sCurCameraID = (LLViewerCamera::eCameraID)(LLViewerCamera::CAMERA_SPOT_SHADOW0 + i);

                if (pipeline.mShadowSpotLight[i].notNull() &&
                    (pipeline.mShadowSpotLight[i] == pipeline.mTargetShadowSpotLight[0] ||
                        pipeline.mShadowSpotLight[i] == pipeline.mTargetShadowSpotLight[1]))
                { //keep this spotlight
                    pipeline.mSpotLightFade[i] = llmin(pipeline.mSpotLightFade[i] + fade_amt, 1.f);
                }
                else
                { //fade out this light
                    pipeline.mSpotLightFade[i] = llmax(pipeline.mSpotLightFade[i] - fade_amt, 0.f);

                    if (pipeline.mSpotLightFade[i] == 0.f || pipeline.mShadowSpotLight[i].isNull())
                    { //faded out, grab one of the pending spots (whichever one isn't already taken)
                        if (pipeline.mTargetShadowSpotLight[0] != pipeline.mShadowSpotLight[(i + 1) % 2])
                        {
                            pipeline.mShadowSpotLight[i] = pipeline.mTargetShadowSpotLight[0];
                        }
                        else
                        {
                            pipeline.mShadowSpotLight[i] = pipeline.mTargetShadowSpotLight[1];
                        }
                    }
                }
            }
        }

        // this should never happen
        llassert(pipeline.mShadowSpotLight[0] != pipeline.mShadowSpotLight[1] || pipeline.mShadowSpotLight[0].isNull());

        // Spot shadows can be updated less frequently than sun shadows to save performance.
        static LLCachedControl<U32> spot_shadow_throttle(gSavedSettings, "RenderSpotShadowThrottle", 1U);
        static U32 s_last_spot_frame = 0;
        bool update_spot_shadows = (spot_shadow_throttle <= 1) || ((gFrameCount - s_last_spot_frame) >= (U32)spot_shadow_throttle);
        if (update_spot_shadows)
        {
            s_last_spot_frame = gFrameCount;
        }

        for (S32 i = 0; i < 2; i++)
        {
            set_current_modelview(saved_view);
            set_current_projection(saved_proj);

            if (pipeline.mShadowSpotLight[i].isNull())
            {
                continue;
            }

            // mSunShadowMatrix[i+4]/mShadowModelview[i+4]/mShadowProjection[i+4]/mSpotShadow[i] are
            // single, un-duplicated LLPipeline members, not part of the swappable RenderTargetPack (mRT)
            // the sun-cascade shadows use. This matrix computation must be skipped during a
            // reflection-probe capture (`!gCubeSnapshot`, matching the actual shadow texture render a
            // few lines below) or it consumes a set_last_modelview()/set_last_projection() "current ->
            // last" transition slot that should only advance once per real main-scene frame, corrupting
            // temporal continuity for the next real frame's spot shadow reprojection, and leaves this probe's own
            // capture reading a matrix/texture pair computed from two
            // different points in time. Skip the whole per-spotlight update
            // during a probe capture, matching the gating already used for
            // the mShadowSpotLight[] assignment above and the texture render
            // below - the probe simply reads whatever the last real
            // main-scene update left in place, self-consistent by
            // construction.
            if (gCubeSnapshot)
            {
                continue;
            }

            LLVOVolume* volume = pipeline.mShadowSpotLight[i]->getVOVolume();

            if (!volume)
            {
                // S24: keep mSpotLightFade[i] in lockstep with this abrupt null - see the matching
                // comment at unlinkDrawable()'s mShadowSpotLight[i]=NULL above.
                pipeline.mShadowSpotLight[i] = NULL;
                pipeline.mSpotLightFade[i] = 0.f;
                continue;
            }

            LLDrawable* drawable = pipeline.mShadowSpotLight[i];

            LLVector3 params = volume->getSpotLightParams();
            F32 fov = params.mV[0];

            //get agent->light space matrix (modelview)
            LLVector3 center = drawable->getPositionAgent();
            LLQuaternion quat = volume->getRenderRotation();

            //get near clip plane
            LLVector3 scale = volume->getScale();
            LLVector3 at_axis(0, 0, -scale.mV[2] * 0.5f);
            at_axis *= quat;

            LLVector3 np = center + at_axis;
            at_axis.normVec();

            //get origin that has given fov for plane np, at_axis, and given scale
            F32 dist = (scale.mV[1] * 0.5f) / tanf(fov * 0.5f);

            LLVector3 origin = np - at_axis * dist;

            LLMatrix4 mat(quat, LLVector4(origin, 1.f));

            view[i + 4] = glm::make_mat4((F32*)mat.mMatrix);

            view[i + 4] = glm::inverse(view[i + 4]);

            // Calculate near and far clip distances.
            F32 near_clip = dist + 0.01f;
            F32 width = scale.mV[VX];
            F32 height = scale.mV[VY];
            F32 far_clip = dist + volume->getLightRadius() * 1.5f;

            // Compute fovy and aspect ratio.
            F32 fovy = fov; // fovy is in radians
            F32 aspect = width / height;

            // Always symmetric for shadows
            float t = near_clip * tanf(fovy * 0.5f);
            float b = -t;
            float r = t * aspect;
            float l = -r;

            proj[i + 4] = glm::frustum(l, r, b, t, near_clip, far_clip);

            // Define a transformation matrix to map NDC coordinates from [-1, 1] to [0, 1].
            // z-column flipped to -0.5f (was 0.5f) - same reversed-Z fix as the sun-shadow "trans" matrix
            // above in this function.
            const glm::mat4 ndcToTexture(0.5f, 0.0f, 0.0f, 0.0f,
                0.0f, 0.5f, 0.0f, 0.0f,
                0.0f, 0.0f, -0.5f, 0.0f,
                0.5f, 0.5f, 0.5f, 1.0f);

            // S24: do NOT pre-multiply proj[i+4] by ndcToTexture before using it to render -
            // proj[i+4] is what gets fed to set_current_projection()/
            // renderShadow() below, and LLRender::syncMatrices()'s DX branch (llrender.cpp)
            // unconditionally applies its own kGLtoDXDepthRemap to whatever the current projection
            // is when the shadow geometry actually draws. Baking ndcToTexture in here meant the
            // rasterized depth values in the spot shadow map were computed through BOTH transforms
            // stacked - a real, self-inflicted double remap, corrupting every depth value actually
            // written to shadowMap4/5. The sibling sun-shadow code directly above in this same
            // function (proj[j]/mSunShadowMatrix[j]) already gets this right: render with the raw
            // perspective projection, apply the NDC-to-texture remap exactly once, only in the
            // CPU-side sampling matrix below. Mirror that here instead of double-transforming.
            set_current_modelview(view[i + 4]);
            set_current_projection(proj[i + 4]);

            pipeline.mSunShadowMatrix[i + 4] = ndcToTexture * proj[i + 4] * view[i + 4] * inv_view;

            set_last_modelview(pipeline.mShadowModelview[i + 4]);
            set_last_projection(pipeline.mShadowProjection[i + 4]);

            pipeline.mShadowModelview[i + 4] = view[i + 4];
            pipeline.mShadowProjection[i + 4] = proj[i + 4];

            if (!gCubeSnapshot && update_spot_shadows) //skip updating spot shadow maps during cubemap updates or when throttled
            {
                LLCamera shadow_cam = camera;
                shadow_cam.setFar(far_clip);
                shadow_cam.setOrigin(origin);

                LLViewerCamera::updateFrustumPlanes(shadow_cam, false, false, true);

                //

                pipeline.mSpotShadow[i].bindTarget();
                pipeline.mSpotShadow[i].getViewport(gDXViewport);
                pipeline.mSpotShadow[i].clear();

                static LLCullResult result[2];

                LLViewerCamera::sCurCameraID = (LLViewerCamera::eCameraID)(LLViewerCamera::CAMERA_SPOT_SHADOW0 + i);

                DXPipeline::RenderSpotLight = drawable;

                DXPipeline::renderShadow(pipeline, view[i + 4], proj[i + 4], shadow_cam, result[i], false);

                DXPipeline::RenderSpotLight = nullptr;

                pipeline.mSpotShadow[i].flush();
            }
        }
    }
    else
    { //no spotlight shadows
        // S24: keep mSpotLightFade[] in lockstep with this abrupt null - see the matching
        // comment at unlinkDrawable()'s mShadowSpotLight[i]=NULL above.
        pipeline.mShadowSpotLight[0] = pipeline.mShadowSpotLight[1] = NULL;
        pipeline.mSpotLightFade[0] = pipeline.mSpotLightFade[1] = 0.f;
    }

    if (!DXPipeline::CameraOffset)
    {
        set_current_modelview(saved_view);
        set_current_projection(saved_proj);
    }
    else
    {
        set_current_modelview(view[1]);
        set_current_projection(proj[1]);
        gDX.loadMatrix(glm::value_ptr(view[1]));
        gDX.matrixMode(LLRender::MM_PROJECTION);
        gDX.loadMatrix(glm::value_ptr(proj[1]));
        gDX.matrixMode(LLRender::MM_MODELVIEW);
    }
    gDX.setColorWriteMask(true, true);

    set_last_modelview(last_modelview);
    set_last_projection(last_projection);

    DXPipeline::popRenderTypeMask(pipeline);

    if (!skip_avatar_update)
    {
        gAgentAvatarp->updateAttachmentVisibility(gAgentCamera.getCameraMode());
    }
}

// ============================================================================
// Lighting setup
// ============================================================================

// static
void DXPipeline::setupAvatarLights(LLPipeline& pipeline, bool for_edit)
{
    pipeline.assertInitialized();

    LLEnvironment& environment = LLEnvironment::instance();
    LLSettingsSky::ptr_t psky = environment.getCurrentSky();

    bool sun_up = environment.getIsSunUp();

    if (for_edit)
    {
        LLColor4 diffuse(1.f, 1.f, 1.f, 0.f);
        LLVector4 light_pos_cam(-8.f, 0.25f, 10.f, 0.f);  // w==0 => directional light
        LLMatrix4 camera_mat = LLViewerCamera::getInstance()->getModelview();
        LLMatrix4 camera_rot(camera_mat.getMat3());
        camera_rot.invert();
        LLVector4 light_pos = light_pos_cam * camera_rot;

        light_pos.normalize();

        LLLightState* light = gDX.getLight(1);

        pipeline.getHWLightColorsMutable()[1] = diffuse;

        light->setDiffuse(diffuse);
        light->setAmbient(LLColor4::black);
        light->setSpecular(LLColor4::black);
        light->setPosition(light_pos);
        light->setConstantAttenuation(1.f);
        light->setLinearAttenuation(0.f);
        light->setQuadraticAttenuation(0.f);
        light->setSpotExponent(0.f);
        light->setSpotCutoff(180.f);
    }
    else if (gAvatarBacklight)
    {
        LLVector3 light_dir = sun_up ? LLVector3(pipeline.mSunDir) : LLVector3(pipeline.mMoonDir);
        LLVector3 opposite_pos = -light_dir;
        LLVector3 orthog_light_pos = light_dir % LLVector3::z_axis;
        LLVector4 backlight_pos = LLVector4(lerp(opposite_pos, orthog_light_pos, 0.3f), 0.0f);
        backlight_pos.normalize();

        LLColor4 light_diffuse = sun_up ? pipeline.mSunDiffuse : pipeline.mMoonDiffuse;

        LLColor4 backlight_diffuse(1.f - light_diffuse.mV[VRED], 1.f - light_diffuse.mV[VGREEN], 1.f - light_diffuse.mV[VBLUE], 1.f);
        F32 max_component = 0.001f;
        for (S32 i = 0; i < 3; i++)
        {
            if (backlight_diffuse.mV[i] > max_component)
            {
                max_component = backlight_diffuse.mV[i];
            }
        }
        F32 backlight_mag = 0.f;
        if (LLEnvironment::instance().getIsSunUp())
        {
            backlight_mag = BACKLIGHT_DAY_MAGNITUDE_OBJECT;
        }
        else
        {
            backlight_mag = BACKLIGHT_NIGHT_MAGNITUDE_OBJECT;
        }
        backlight_diffuse *= backlight_mag / max_component;

        pipeline.getHWLightColorsMutable()[1] = backlight_diffuse;

        LLLightState* light = gDX.getLight(1);

        light->setPosition(backlight_pos);
        light->setDiffuse(backlight_diffuse);
        light->setAmbient(LLColor4::black);
        light->setSpecular(LLColor4::black);
        light->setConstantAttenuation(1.f);
        light->setLinearAttenuation(0.f);
        light->setQuadraticAttenuation(0.f);
        light->setSpotExponent(0.f);
        light->setSpotCutoff(180.f);
    }
    else
    {
        LLLightState* light = gDX.getLight(1);

        pipeline.getHWLightColorsMutable()[1] = LLColor4::black;

        light->setDiffuse(LLColor4::black);
        light->setAmbient(LLColor4::black);
        light->setSpecular(LLColor4::black);
    }
}

// Camera-centric local light prioritization: only 6 hardware slots exist for local lights. Unlike
// upstream (which let selected lights bypass distance sorting, letting distant selected objects steal
// slots from lights actually visible to the observer), the 6 slots always go to the 6 closest lights
// in view - selected lights still get priority (see below), but plain distance otherwise decides.
static F32 calc_light_dist(LLVOVolume* light, const LLVector3& cam_pos, F32 max_dist)
{
    F32 inten = light->getLightIntensity();
    if (inten < .001f)
    {
        return max_dist; // Light has no intensity, exclude from consideration
    }

    bool selected = light->isSelected();
    if (selected)
    {
        return 0.f; // selected lights get highest priority
    }
    LLVector3 light_pos = light->getRenderPosition();
    F32 dist = dist_vec(light_pos, cam_pos);

    // Small boost for agent avatar lights, keeps self-lighting competitive without guaranteeing slots
    LLVOAvatar* avatar = light->getAvatar();
    if (avatar && isAgentAvatarValid() && avatar == gAgentAvatarp)
    {
        dist = llmax(dist - 5.0f, 0.0f); // 5m virtual closeness bonus
    }

    // Subtract light radius from distance (lights illuminate beyond their center point)
    F32 radius = light->getLightRadius();
    dist = llmax(dist - radius, 0.f);

    return dist;
}

// static
void DXPipeline::calcNearbyLights(LLPipeline& pipeline, LLCamera& camera)
{
    pipeline.assertInitialized();

    if (DXPipeline::sReflectionRender || gCubeSnapshot || DXPipeline::sRenderingHUDs || LLApp::isExiting())
    {
        return;
    }

    static LLCachedControl<bool> enable_frustum_culling(gSavedSettings, "RenderLocalLightFrustumCulling", true);
    static bool last_frustum_culling_state = enable_frustum_culling;

    // If frustum culling was just toggled off, reset all fade states to prevent
    // lights from staying dark due to lingering negative fade values
    if (last_frustum_culling_state && !enable_frustum_culling)
    {
        for (LLPipeline::light_set_t::iterator iter = pipeline.getNearbyLightsMutable().begin(); iter != pipeline.getNearbyLightsMutable().end(); ++iter)
        {
            // Force all lights to fade in immediately when frustum culling is disabled
            if (iter->fade < 0.f)
            {
                ((LLPipeline::Light*)(&(*iter)))->fade = 0.f;
            }
        }
    }
    last_frustum_culling_state = enable_frustum_culling;

    static LLCachedControl<S32> local_light_count(gSavedSettings, "RenderLocalLightCount", 256);

    if (local_light_count >= 1)
    {
        // mNearbyLight (and all light_set_t's) are sorted such that
        // begin() == the closest light and rbegin() == the farthest light
        const S32 MAX_LOCAL_LIGHTS = 6;
        LLVector3 cam_pos = camera.getOrigin();

        F32 max_dist = 0.f;
        if (DXPipeline::sRenderDeferred)
        {
            max_dist = DXPipeline::RenderFarClip;
        }
        else
        {
            max_dist = llmin(DXPipeline::RenderFarClip, LIGHT_MAX_RADIUS * 4.f);
        }

        // UPDATE THE EXISTING NEARBY LIGHTS
        LLPipeline::light_set_t cur_nearby_lights;
        for (LLPipeline::light_set_t::iterator iter = pipeline.getNearbyLightsMutable().begin();
            iter != pipeline.getNearbyLightsMutable().end(); iter++)
        {
            const LLPipeline::Light* light = &(*iter);
            LLDrawable* drawable = light->drawable;
            const LLViewerObject* vobj = light->drawable->getVObj();
            if (vobj && vobj->getAvatar()
                && (vobj->getAvatar()->isTooComplex() || vobj->getAvatar()->isInMuteList() || vobj->getAvatar()->isTooSlow())
                )
            {
                drawable->clearState(LLDrawable::NEARBY_LIGHT);
                continue;
            }

            LLVOVolume* volight = drawable->getVOVolume();
            if (!volight || !drawable->isState(LLDrawable::LIGHT))
            {
                drawable->clearState(LLDrawable::NEARBY_LIGHT);
                continue;
            }
            if (light->fade <= -LIGHT_FADE_TIME)
            {
                drawable->clearState(LLDrawable::NEARBY_LIGHT);
                continue;
            }
            if (!DXPipeline::sRenderAttachedLights && volight && volight->isAttachment())
            {
                drawable->clearState(LLDrawable::NEARBY_LIGHT);
                continue;
            }

            F32 dist = calc_light_dist(volight, cam_pos, max_dist);
            F32 fade = light->fade;
            // actual fade gets decreased/increased by setupHWLights
            // light->fade value is 'time'.
            // >=0 and light will become visible as value increases
            // <0 and light will fade out
            if (dist < max_dist)
            {
                if (fade < 0)
                {
                    // mark light to fade in
                    // if fade was -LIGHT_FADE_TIME - it was fully invisible
                    // if fade -0 - it was fully visible
                    // visibility goes up from 0 to LIGHT_FADE_TIME.
                    fade += LIGHT_FADE_TIME;
                }
            }
            else
            {
                // mark light to fade out
                // visibility goes down from -0 to -LIGHT_FADE_TIME.
                if (fade >= LIGHT_FADE_TIME)
                {
                    fade = -0.0001f; // was fully visible
                }
                else if (fade >= 0)
                {
                    // 0.75 visible light should stay 0.75 visible, but should reverse direction
                    fade -= LIGHT_FADE_TIME;
                }
            }
            cur_nearby_lights.insert(LLPipeline::Light(drawable, dist, fade));
        }
        pipeline.getNearbyLightsMutable() = cur_nearby_lights;

        // FIND NEW LIGHTS THAT ARE IN RANGE
        LLPipeline::light_set_t new_nearby_lights;

        for (LLDrawable::ordered_drawable_set_t::iterator iter = pipeline.getLights().begin();
            iter != pipeline.getLights().end(); ++iter)
        {
            LLDrawable* drawable = *iter;
            LLVOVolume* light = drawable->getVOVolume();
            if (!light || drawable->isState(LLDrawable::NEARBY_LIGHT))
            {
                continue;
            }
            if (light->isHUDAttachment())
            {
                continue; // no lighting from HUD objects
            }
            if (!DXPipeline::sRenderAttachedLights && light && light->isAttachment())
            {
                continue;
            }
            LLVOAvatar* av = light->getAvatar();
            if (av && (av->isTooComplex() || av->isInMuteList() || av->isTooSlow()))
            {
                // avatars that are already in the list will be removed by removeMutedAVsLights
                continue;
            }

            F32 dist = calc_light_dist(light, cam_pos, max_dist);
            if (dist >= max_dist)
            {
                continue; // Beyond maximum distance threshold
            }

            // With only 6 hardware slots, exclude off-screen lights so visible scene elements get
            // priority. Toggle: RenderLocalLightFrustumCulling (default: true)

            if (enable_frustum_culling)
            {
                LLVector3 light_pos = light->getRenderPosition();
                F32 light_radius = light->getLightRadius();

                // Strict frustum test: if not in view, exclude immediately
                // This prevents slot theft by off-screen lights and improves frame coherence
                if (camera.sphereInFrustum(light_pos, light_radius) == 0)
                {
                    continue; // Light outside frustum, excluded from slot assignment
                }
            }

            new_nearby_lights.insert(LLPipeline::Light(drawable, dist, 0.f));
            if (!DXPipeline::sRenderDeferred && new_nearby_lights.size() > (U32)MAX_LOCAL_LIGHTS)
            {
                new_nearby_lights.erase(--new_nearby_lights.end());
                const LLPipeline::Light& last = *new_nearby_lights.rbegin();
                max_dist = last.dist;
            }
        }

        // INSERT ANY NEW LIGHTS
        for (LLPipeline::light_set_t::iterator iter = new_nearby_lights.begin();
            iter != new_nearby_lights.end(); iter++)
        {
            const LLPipeline::Light* light = &(*iter);
            if (DXPipeline::sRenderDeferred || pipeline.getNearbyLightsMutable().size() < (U32)MAX_LOCAL_LIGHTS)
            {
                pipeline.getNearbyLightsMutable().insert(*light);
                ((LLDrawable*)light->drawable)->setState(LLDrawable::NEARBY_LIGHT);
            }
            else
            {
                // crazy cast so that we can overwrite the fade value
                // even though gcc enforces sets as const
                // (fade value doesn't affect sort so this is safe)
                LLPipeline::Light* farthest_light = (const_cast<LLPipeline::Light*>(&(*(pipeline.getNearbyLightsMutable().rbegin()))));
                if (light->dist < farthest_light->dist)
                {
                    // mark light to fade out
                    // visibility goes down from -0 to -LIGHT_FADE_TIME.
                    //
                    // This is a mess, but for now it needs to be in sync
                    // with fade code above. Ex: code above detects distance < max,
                    // sets fade time to positive, this code then detects closer
                    // lights and sets fade time negative, fully compensating
                    // for the code above
                    if (farthest_light->fade >= LIGHT_FADE_TIME)
                    {
                        farthest_light->fade = -0.0001f; // was fully visible
                    }
                    else if (farthest_light->fade >= 0)
                    {
                        farthest_light->fade -= LIGHT_FADE_TIME;
                    }
                }
                else
                {
                    break; // none of the other lights are closer
                }
            }
        }

        //mark nearby lights not-removable.
        for (LLPipeline::light_set_t::iterator iter = pipeline.getNearbyLightsMutable().begin();
            iter != pipeline.getNearbyLightsMutable().end(); iter++)
        {
            const LLPipeline::Light* light = &(*iter);
            ((LLViewerOctreeEntryData*)light->drawable)->setVisible();
        }
    }
}

// static
void DXPipeline::setupHWLights(LLPipeline& pipeline)
{
    pipeline.assertInitialized();

    if (DXPipeline::sRenderingHUDs)
    {
        return;
    }

    F32 light_scale = 1.f;

    // S24 : !gEquirectCapture - mLightScale is only refreshed per sphere-
    // probe update cycle, stale for a 360 capture; keep normal brightness.
    if (gCubeSnapshot && !gEquirectCapture)
    { //darken local lights when probe ambiance is above 1
        light_scale = pipeline.mSphereProbes.getLightScale();
    }


    LLEnvironment& environment = LLEnvironment::instance();
    LLSettingsSky::ptr_t psky = environment.getCurrentSky();

    // Ambient
    LLColor4 ambient = psky->getTotalAmbient();

    gDX.setAmbientLightColor(ambient);

    bool sun_up = environment.getIsSunUp();
    bool moon_up = environment.getIsMoonUp();

    // Light 0 = Sun or Moon (All objects)
    {
        LLVector4 sun_dir(environment.getSunDirection(), 0.0f);
        LLVector4 moon_dir(environment.getMoonDirection(), 0.0f);

        pipeline.mSunDir.setVec(sun_dir);
        pipeline.mMoonDir.setVec(moon_dir);

        pipeline.mSunDiffuse.setVec(psky->getSunlightColor());
        pipeline.mMoonDiffuse.setVec(psky->getMoonlightColor());

        F32 max_color = llmax(pipeline.mSunDiffuse.mV[0], pipeline.mSunDiffuse.mV[1], pipeline.mSunDiffuse.mV[2]);
        if (max_color > 1.f)
        {
            pipeline.mSunDiffuse *= 1.f / max_color;
        }
        pipeline.mSunDiffuse.clamp();

        max_color = llmax(pipeline.mMoonDiffuse.mV[0], pipeline.mMoonDiffuse.mV[1], pipeline.mMoonDiffuse.mV[2]);
        if (max_color > 1.f)
        {
            pipeline.mMoonDiffuse *= 1.f / max_color;
        }
        pipeline.mMoonDiffuse.clamp();

        // prevent underlighting from having neither lightsource facing us
        if (!sun_up && !moon_up)
        {
            pipeline.mSunDiffuse.setVec(LLColor4(0.0, 0.0, 0.0, 1.0));
            pipeline.mMoonDiffuse.setVec(LLColor4(0.0, 0.0, 0.0, 1.0));
            pipeline.mSunDir.setVec(LLVector4(0.0, 1.0, 0.0, 0.0));
            pipeline.mMoonDir.setVec(LLVector4(0.0, 1.0, 0.0, 0.0));
        }

        LLVector4 light_dir = sun_up ? pipeline.mSunDir : pipeline.mMoonDir;

        pipeline.getHWLightColorsMutable()[0] = sun_up ? pipeline.mSunDiffuse : pipeline.mMoonDiffuse;

        LLLightState* light = gDX.getLight(0);
        light->setPosition(light_dir);

        light->setSunPrimary(sun_up);
        light->setDiffuse(pipeline.getHWLightColorsMutable()[0]);
        light->setDiffuseB(pipeline.mMoonDiffuse);
        light->setAmbient(psky->getTotalAmbient());
        light->setSpecular(LLColor4::black);
        light->setConstantAttenuation(1.f);
        light->setLinearAttenuation(0.f);
        light->setQuadraticAttenuation(0.f);
        light->setSpotExponent(0.f);
        light->setSpotCutoff(180.f);
    }

    // Light 1 = Backlight (for avatars)
    // (set by enableLightsAvatar)

    S32 cur_light = 2;

    // Nearby lights = LIGHT 2-7

    pipeline.getLightMovingMaskMutable() = 0;

    static LLCachedControl<S32> local_light_count(gSavedSettings, "RenderLocalLightCount", 256);

    if (local_light_count >= 1)
    {
        for (LLPipeline::light_set_t::iterator iter = pipeline.getNearbyLightsMutable().begin();
            iter != pipeline.getNearbyLightsMutable().end(); ++iter)
        {
            LLDrawable* drawable = iter->drawable;
            LLVOVolume* light = drawable->getVOVolume();
            if (!light)
            {
                continue;
            }

            // CRITICAL: Agent avatar lights bypass the RenderAttachedLights setting
            // The observer always needs to see their own avatar lit properly
            bool is_agent_light = false;
            if (light->isAttachment())
            {
                LLVOAvatar* avatar = light->getAvatar();
                if (avatar && isAgentAvatarValid() && avatar == gAgentAvatarp)
                {
                    is_agent_light = true; // Agent's own lights always render
                }
                else if (!DXPipeline::sRenderAttachedLights)
                {
                    continue; // Other avatars' attachment lights can be disabled
                }
            }

            if (drawable->isState(LLDrawable::ACTIVE))
            {
                pipeline.getLightMovingMaskMutable() |= (1 << cur_light);
            }

            //send linear light color to shader
            LLColor4  light_color = light->getLightLinearColor() * light_scale;
            light_color.mV[3] = 0.0f;

            F32 fade = iter->fade;
            if (fade < LIGHT_FADE_TIME)
            {
                // fade in/out light
                if (fade >= 0.f)
                {
                    fade = fade / LIGHT_FADE_TIME;
                    ((LLPipeline::Light*)(&(*iter)))->fade += gFrameIntervalSeconds.value();
                }
                else
                {
                    fade = 1.f + fade / LIGHT_FADE_TIME;
                    ((LLPipeline::Light*)(&(*iter)))->fade -= gFrameIntervalSeconds.value();
                }
                fade = llclamp(fade, 0.f, 1.f);
                light_color *= fade;
            }

            if (light_color.magVecSquared() < 0.001f)
            {
                continue;
            }

            LLVector3 light_pos(light->getRenderPosition());
            LLVector4 light_pos_gl(light_pos, 1.0f);

            F32 adjusted_radius = light->getLightRadius() * (DXPipeline::sRenderDeferred ? 1.5f : 1.0f);
            if (adjusted_radius <= 0.001f)
            {
                continue;
            }

            F32 x = (3.f * (1.f + (light->getLightFalloff() * 2.0f)));  // why this magic?  probably trying to match a historic behavior.
            F32 linatten = x / adjusted_radius;                         // % of brightness at radius

            pipeline.getHWLightColorsMutable()[cur_light] = light_color;
            LLLightState* light_state = gDX.getLight(cur_light);

            light_state->setPosition(light_pos_gl);
            light_state->setDiffuse(light_color);
            light_state->setAmbient(LLColor4::black);
            light_state->setConstantAttenuation(0.f);
            light_state->setSize(light->getLightRadius() * 1.5f);
            light_state->setFalloff(light->getLightFalloff(DEFERRED_LIGHT_FALLOFF));

            if (DXPipeline::sRenderDeferred)
            {
                light_state->setLinearAttenuation(linatten);
                light_state->setQuadraticAttenuation(light->getLightFalloff(DEFERRED_LIGHT_FALLOFF) + 1.f); // get falloff to match for forward deferred rendering lights
            }
            else
            {
                light_state->setLinearAttenuation(linatten);
                light_state->setQuadraticAttenuation(0.f);
            }

            if (light->isLightSpotlight() // directional (spot-)light
                && (DXPipeline::sRenderDeferred || DXPipeline::RenderSpotLightsInNondeferred)) // these are only rendered as GL spotlights if we're in deferred rendering mode *or* the setting forces them on
            {
                LLQuaternion quat = light->getRenderRotation();
                LLVector3 at_axis(0, 0, -1); // this matches deferred rendering's object light direction
                at_axis *= quat;

                light_state->setSpotDirection(at_axis);
                light_state->setSpotCutoff(90.f);
                light_state->setSpotExponent(2.f);

                LLVector3 spotParams = light->getSpotLightParams();

                const LLColor4 specular(0.f, 0.f, 0.f, spotParams[2]);
                light_state->setSpecular(specular);
            }
            else // omnidirectional (point) light
            {
                light_state->setSpotExponent(0.f);
                light_state->setSpotCutoff(180.f);

                // we use specular.z = 1.0 as a cheap hack for the shaders to know that this is omnidirectional rather than a spotlight
                const LLColor4 specular(0.f, 0.f, 1.f, 0.f);
                light_state->setSpecular(specular);
            }
            cur_light++;
            if (cur_light >= 8)
            {
                break; // safety
            }
        }
    }
    for (; cur_light < 8; cur_light++)
    {
        pipeline.getHWLightColorsMutable()[cur_light] = LLColor4::black;
        LLLightState* light = gDX.getLight(cur_light);
        light->setSunPrimary(true);
        light->setDiffuse(LLColor4::black);
        light->setAmbient(LLColor4::black);
        light->setSpecular(LLColor4::black);
    }

    // Bookmark comment to allow searching for mSpecialRenderMode == 3 (avatar edit mode),
    // prev site of forward (non-deferred) character light injection, removed by SL-13522 09/20

    // Init GL state
    for (S32 i = 0; i < 8; ++i)
    {
        gDX.getLight(i)->disable();
    }
    pipeline.getLightMaskMutable() = 0;
}

// static
void DXPipeline::enableLights(LLPipeline& pipeline, U32 mask)
{
    pipeline.assertInitialized();

    if (pipeline.getLightMaskMutable() == mask)
        return;

    pipeline.getLightMaskMutable() = mask;

    // No lights at all - disable all and bail
    if (mask == 0)
    {
        for (S32 i = 0; i < 8; ++i)
        {
            LLLightState* light = gDX.getLight(i);
            light->disable();
            light->setDiffuse(LLColor4::black);
        }
        return;
    }

    // Update only the 8 fixed-function lights
    for (S32 i = 0; i < 8; ++i)
    {
        LLLightState* light = gDX.getLight(i);
        const bool enabled = (mask >> i) & 1;

        if (enabled)
        {
            light->enable();
            light->setDiffuse(pipeline.getHWLightColorsMutable()[i]);
        }
        else
        {
            light->disable();
            light->setDiffuse(LLColor4::black);
        }
    }
}

// S24 V1
// static
void DXPipeline::enableLightsDynamic(LLPipeline& pipeline)
{
    pipeline.assertInitialized();

    // Local lights: all 8 fixed-function lights except index 1
    constexpr U32 LOCAL_LIGHT_MASK = 0xFF & ~0x02;
    DXPipeline::enableLights(pipeline, LOCAL_LIGHT_MASK);

    if (!isAgentAvatarValid())
        return;

    const S32 mode = gAgentAvatarp->mSpecialRenderMode;

    switch (mode)
    {
    case 0: // normal
        DXPipeline::enableLightsAvatar(pipeline);
        break;

    case 2: // animation preview
        DXPipeline::enableLightsAvatarEdit(pipeline, LLColor4(0.7f, 0.6f, 0.3f, 1.f));
        break;

    default:
        // No special lighting
        break;
    }
}

// static
void DXPipeline::enableLightsAvatar(LLPipeline& pipeline)
{
    U32 mask = 0xff; // All lights
    DXPipeline::setupAvatarLights(pipeline, false);
    DXPipeline::enableLights(pipeline, mask);
}

// static
void DXPipeline::enableLightsPreview(LLPipeline& pipeline)
{
    DXPipeline::disableLights(pipeline);

    LLColor4 ambient = DXPipeline::PreviewAmbientColor;
    gDX.setAmbientLightColor(ambient);

    LLColor4 diffuse0 = DXPipeline::PreviewDiffuse0;
    LLColor4 specular0 = DXPipeline::PreviewSpecular0;
    LLColor4 diffuse1 = DXPipeline::PreviewDiffuse1;
    LLColor4 specular1 = DXPipeline::PreviewSpecular1;
    LLColor4 diffuse2 = DXPipeline::PreviewDiffuse2;
    LLColor4 specular2 = DXPipeline::PreviewSpecular2;

    LLVector3 dir0 = DXPipeline::PreviewDirection0;
    LLVector3 dir1 = DXPipeline::PreviewDirection1;
    LLVector3 dir2 = DXPipeline::PreviewDirection2;

    dir0.normVec();
    dir1.normVec();
    dir2.normVec();

    LLVector4 light_pos(dir0, 0.0f);

    LLLightState* light = gDX.getLight(1);

    light->enable();
    light->setPosition(light_pos);
    light->setDiffuse(diffuse0);
    light->setAmbient(ambient);
    light->setSpecular(specular0);
    light->setSpotExponent(0.f);
    light->setSpotCutoff(180.f);

    light_pos = LLVector4(dir1, 0.f);

    light = gDX.getLight(2);
    light->enable();
    light->setPosition(light_pos);
    light->setDiffuse(diffuse1);
    light->setAmbient(ambient);
    light->setSpecular(specular1);
    light->setSpotExponent(0.f);
    light->setSpotCutoff(180.f);

    light_pos = LLVector4(dir2, 0.f);
    light = gDX.getLight(3);
    light->enable();
    light->setPosition(light_pos);
    light->setDiffuse(diffuse2);
    light->setAmbient(ambient);
    light->setSpecular(specular2);
    light->setSpotExponent(0.f);
    light->setSpotCutoff(180.f);
}

// static
void DXPipeline::enableLightsAvatarEdit(LLPipeline& pipeline, const LLColor4& color)
{
    U32 mask = 0x2002; // Avatar backlight only, set ambient
    DXPipeline::setupAvatarLights(pipeline, true);
    DXPipeline::enableLights(pipeline, mask);

    gDX.setAmbientLightColor(color);
}

// static
void DXPipeline::enableLightsFullbright(LLPipeline& pipeline)
{
    pipeline.assertInitialized();
    U32 mask = 0x1000; // Non-0 mask, set ambient
    DXPipeline::enableLights(pipeline, mask);
}

// static
void DXPipeline::disableLights(LLPipeline& pipeline)
{
    DXPipeline::enableLights(pipeline, 0); // no lighting (full bright)
}

// static
void DXPipeline::setLight(LLPipeline& pipeline, LLDrawable* drawablep, bool is_light)
{
    if (drawablep && pipeline.assertInitialized())
    {
        if (is_light)
        {
            pipeline.getLightsMutable().insert(drawablep);
            drawablep->setState(LLDrawable::LIGHT);
        }
        else
        {
            drawablep->clearState(LLDrawable::LIGHT);
            pipeline.getLightsMutable().erase(drawablep);
        }
    }
}

// ============================================================================
// Avatar impostor baking
// ============================================================================

// static
void DXPipeline::profileAvatar(LLPipeline& pipeline, LLVOAvatar* avatar, bool profile_attachments)
{
    // don't continue to profile an avatar that is known to be too slow
    llassert(!avatar->isTooSlow());

    LLHLSLShader* cur_shader = LLHLSLShader::sCurBoundShaderPtr;

    pipeline.mRT->deferredScreen.bindTarget();
    pipeline.mRT->deferredScreen.clear();

    if (!profile_attachments)
    {
        // profile entire avatar all at once and readback asynchronously
        avatar->placeProfileQuery();

        LLTimer cpu_timer;

        DXPipeline::generateImpostor(pipeline, avatar, false, true);

        avatar->setCPURenderTime((F32)cpu_timer.getElapsedTimeF32() * 1000.f);

        avatar->readProfileQuery(5); // allow up to 5 frames of latency
    }
    else
    {
        // profile attachments one at a time
        LLVOAvatar::attachment_map_t::iterator iter;
        LLVOAvatar::attachment_map_t::iterator begin = avatar->mAttachmentPoints.begin();
        LLVOAvatar::attachment_map_t::iterator end = avatar->mAttachmentPoints.end();

        for (iter = begin;
            iter != end;
            ++iter)
        {
            LLViewerJointAttachment* attachment = iter->second;
            for (LLViewerJointAttachment::attachedobjs_vec_t::iterator attachment_iter = attachment->mAttachedObjects.begin();
                attachment_iter != attachment->mAttachedObjects.end();
                ++attachment_iter)
            {
                LLViewerObject* attached_object = attachment_iter->get();
                if (attached_object)
                {
                    // use gDebugProgram to do the GPU queries
                    gDebugProgram.clearStats();
                    gDebugProgram.placeProfileQuery(true);

                    DXPipeline::generateImpostor(pipeline, avatar, false, true, attached_object);
                    gDebugProgram.readProfileQuery(true, true);

                    attached_object->mGPURenderTime = gDebugProgram.mTimeElapsed / 1000000.f;
                }
            }
        }
    }

    pipeline.mRT->deferredScreen.flush();

    if (cur_shader)
    {
        cur_shader->bind();
    }
}

// static
void DXPipeline::generateImpostor(LLPipeline& pipeline, LLVOAvatar* avatar, bool preview_avatar, bool for_profile, LLViewerObject* specific_attachment)
{
    DXState::checkStates();

    static LLCullResult result;
    result.clear();
    DXPipeline::grabReferences(pipeline, result);

    if (!avatar || avatar->isDead() || !avatar->mDrawable)
    {
        LL_WARNS_ONCE("AvatarRenderPipeline") << "Avatar is " << (avatar ? "not drawable" : "null") << LL_ENDL;
        return;
    }
    LL_DEBUGS_ONCE("AvatarRenderPipeline") << "Avatar " << avatar->getID() << " is drawable" << LL_ENDL;

    pipeline.assertInitialized();

    // previews can't be muted or impostered
    bool visually_muted = !for_profile && !preview_avatar && avatar->isVisuallyMuted();
    LL_DEBUGS_ONCE("AvatarRenderPipeline") << "Avatar " << avatar->getID()
        << " is " << (visually_muted ? "" : "not ") << "visually muted"
        << LL_ENDL;
    bool too_complex = !for_profile && !preview_avatar && avatar->isTooComplex();
    LL_DEBUGS_ONCE("AvatarRenderPipeline") << "Avatar " << avatar->getID()
        << " is " << (too_complex ? "" : "not ") << "too complex"
        << LL_ENDL;

    DXPipeline::pushRenderTypeMask(pipeline);

    if (visually_muted || too_complex)
    {
        // only show jelly doll geometry
        DXPipeline::andRenderTypeMask(pipeline, LLPipeline::RENDER_TYPE_AVATAR,
            LLPipeline::RENDER_TYPE_CONTROL_AV,
            LLPipeline::END_RENDER_TYPES);
    }
    else
    {
        //hide world geometry
        DXPipeline::clearRenderTypeMask(pipeline, 
            LLPipeline::RENDER_TYPE_SKY,
            LLPipeline::RENDER_TYPE_WL_SKY,
            LLPipeline::RENDER_TYPE_TERRAIN,
            LLPipeline::RENDER_TYPE_GRASS,
            LLPipeline::RENDER_TYPE_CONTROL_AV, // Animesh
            LLPipeline::RENDER_TYPE_TREE,
            LLPipeline::RENDER_TYPE_VOIDWATER,
            LLPipeline::RENDER_TYPE_WATER,
            LLPipeline::RENDER_TYPE_ALPHA_PRE_WATER,
            LLPipeline::RENDER_TYPE_PASS_GRASS,
            LLPipeline::RENDER_TYPE_HUD,
            LLPipeline::RENDER_TYPE_PARTICLES,
            LLPipeline::RENDER_TYPE_CLOUDS,
            LLPipeline::RENDER_TYPE_HUD_PARTICLES,
            LLPipeline::END_RENDER_TYPES
        );
    }

    if (specific_attachment && specific_attachment->isHUDAttachment())
    { //enable HUD rendering
        DXPipeline::setRenderTypeMask(pipeline, LLPipeline::RENDER_TYPE_HUD, LLPipeline::END_RENDER_TYPES);
    }

    S32 occlusion = DXPipeline::sUseOcclusion;
    DXPipeline::sUseOcclusion = 0;

    DXPipeline::sReflectionRender = !DXPipeline::sRenderDeferred;

    DXPipeline::sShadowRender = true;
    DXPipeline::sImpostorRender = true;

    LLViewerCamera* viewer_camera = LLViewerCamera::getInstance();

    {
        DXPipeline::markVisible(pipeline, avatar->mDrawable, *viewer_camera);

        if (preview_avatar)
        {
            // Only show rigged attachments for preview
            // For the sake of performance and so that static
            // objects won't obstruct previewing changes
            LLVOAvatar::attachment_map_t::iterator iter;
            for (iter = avatar->mAttachmentPoints.begin();
                iter != avatar->mAttachmentPoints.end();
                ++iter)
            {
                LLViewerJointAttachment* attachment = iter->second;
                for (LLViewerJointAttachment::attachedobjs_vec_t::iterator attachment_iter = attachment->mAttachedObjects.begin();
                    attachment_iter != attachment->mAttachedObjects.end();
                    ++attachment_iter)
                {
                    LLViewerObject* attached_object = attachment_iter->get();
                    if (attached_object)
                    {
                        if (attached_object->isRiggedMesh())
                        {
                            DXPipeline::markVisible(pipeline, attached_object->mDrawable->getSpatialBridge(), *viewer_camera);
                        }
                        else
                        {
                            // sometimes object is a linkset and rigged mesh is a child
                            LLViewerObject::const_child_list_t& child_list = attached_object->getChildren();
                            for (LLViewerObject::child_list_t::const_iterator iter = child_list.begin();
                                iter != child_list.end(); iter++)
                            {
                                LLViewerObject* child = *iter;
                                if (child->isRiggedMesh())
                                {
                                    DXPipeline::markVisible(pipeline, attached_object->mDrawable->getSpatialBridge(), *viewer_camera);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        else
        {
            if (specific_attachment)
            {
                DXPipeline::markVisible(pipeline, specific_attachment->mDrawable->getSpatialBridge(), *viewer_camera);
            }
            else
            {
                LLVOAvatar::attachment_map_t::iterator iter;
                LLVOAvatar::attachment_map_t::iterator begin = avatar->mAttachmentPoints.begin();
                LLVOAvatar::attachment_map_t::iterator end = avatar->mAttachmentPoints.end();

                for (iter = begin;
                    iter != end;
                    ++iter)
                {
                    LLViewerJointAttachment* attachment = iter->second;
                    for (LLViewerJointAttachment::attachedobjs_vec_t::iterator attachment_iter = attachment->mAttachedObjects.begin();
                        attachment_iter != attachment->mAttachedObjects.end();
                        ++attachment_iter)
                    {
                        LLViewerObject* attached_object = attachment_iter->get();
                        if (attached_object)
                        {
                            DXPipeline::markVisible(pipeline, attached_object->mDrawable->getSpatialBridge(), *viewer_camera);
                        }
                    }
                }
            }
        }
    }

    DXPipeline::stateSort(pipeline, *LLViewerCamera::getInstance(), result);

    LLCamera camera = *viewer_camera;
    LLVector2 tdim;
    U32 resY = 0;
    U32 resX = 0;

    if (!preview_avatar)
    {
        const LLVector4a* ext = avatar->mDrawable->getSpatialExtents();
        LLVector3 pos(avatar->getRenderPosition() + avatar->getImpostorOffset());

        camera.lookAt(viewer_camera->getOrigin(), pos, viewer_camera->getUpAxis());

        LLVector4a half_height(0.f);
        half_height.setSub(ext[1], ext[0]);
        half_height.mul(0.5f);

        LLVector4a left(0.f);
        left.load3(camera.getLeftAxis().mV);
        left.mul(left);
        llassert(left.dot3(left).getF32() > F_APPROXIMATELY_ZERO);
        left.normalize3fast();

        LLVector4a up(0.f);
        up.load3(camera.getUpAxis().mV);
        up.mul(up);
        llassert(up.dot3(up).getF32() > F_APPROXIMATELY_ZERO);
        up.normalize3fast();

        tdim.mV[0] = fabsf(half_height.dot3(left).getF32());
        tdim.mV[1] = fabsf(half_height.dot3(up).getF32());

        gDX.matrixMode(LLRender::MM_PROJECTION);
        gDX.pushMatrix();

        F32 distance = (pos - camera.getOrigin()).length();
        F32 fov = atanf(tdim.mV[1] / distance) * 2.f * RAD_TO_DEG;
        F32 aspect = tdim.mV[0] / tdim.mV[1];
        // S42 3D Define near/far clip planes
        F32 near_clip = 1.f;
        F32 far_clip = 256.f;
        // Convert FOV to radians
        F32 fovRadians = glm::radians(fov);
        // Build symmetric frustum (equivalent to glm::perspective)
        float t = near_clip * tanf(fovRadians * 0.5f);
        float b = -t;
        float r = t * aspect;
        float l = -r;

        glm::mat4 persp = glm::frustum(l, r, b, t, near_clip, far_clip);
        set_current_projection(persp);
        gDX.loadMatrix(glm::value_ptr(persp));

        gDX.matrixMode(LLRender::MM_MODELVIEW);
        gDX.pushMatrix();

        F32 dx_matrix[16] = {};
        camera.getDirectXTransform(dx_matrix);
        glm::mat4 mat = glm::make_mat4((F32*)OGL_TO_CFR_ROTATION) * glm::make_mat4(dx_matrix);

        gDX.loadMatrix(glm::value_ptr(mat));
        set_current_modelview(mat);

        gDX.setColorWriteMask(true, true);

        // get the number of pixels per angle
        F32 pa = gViewerWindow->getWindowHeightRaw() / (RAD_TO_DEG * viewer_camera->getView());

        //get resolution based on angle width and height of impostor (double desired resolution to prevent aliasing)
        resY = llmin(nhpo2((U32)(fov * pa)), (U32)512);
        resX = llmin(nhpo2((U32)(atanf(tdim.mV[0] / distance) * 2.f * RAD_TO_DEG * pa)), (U32)512);

        if (!for_profile)
        {
            if (!avatar->mImpostor.isComplete())
            {
                avatar->mImpostor.allocate(resX, resY, GL_RGBA, true);

                if (DXPipeline::sRenderDeferred)
                {
                    addDeferredAttachments(avatar->mImpostor, true);
                }

                gDX.getTexUnit(0)->bind(&avatar->mImpostor);
                gDX.getTexUnit(0)->setTextureFilteringOption(LLTexUnit::TFO_POINT);
                gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
            }
            else if (resX != avatar->mImpostor.getWidth() || resY != avatar->mImpostor.getHeight())
            {
                avatar->mImpostor.resize(resX, resY);
            }

            avatar->mImpostor.bindTarget();
        }
    }

    F32 old_alpha = LLDrawPoolAvatar::sMinimumAlpha;

    if (visually_muted || too_complex)
    { //disable alpha masking for muted avatars (get whole skin silhouette)
        LLDrawPoolAvatar::sMinimumAlpha = 0.f;
    }

    if (preview_avatar || for_profile)
    {
        // previews and profiles don't care about imposters
        DXPipeline::renderGeomDeferred(pipeline, camera, false);
        DXPipeline::renderGeomPostDeferred(pipeline, camera);
    }
    else
    {
        avatar->mImpostor.clear();
        DXPipeline::renderGeomDeferred(pipeline, camera, false);

        DXPipeline::renderGeomPostDeferred(pipeline, camera);

        // Shameless hack time: render it all again,
        // this time writing the depth
        // values we need to generate the alpha mask below
        // while preserving the alpha-sorted color rendering
        // from the previous pass
        //
        DXPipeline::sImpostorRenderAlphaDepthPass = true;
        // depth-only here...
        //
        gDX.setColorWriteMask(false, false);
        DXPipeline::renderGeomPostDeferred(pipeline, camera);

        DXPipeline::sImpostorRenderAlphaDepthPass = false;
    }

    LLDrawPoolAvatar::sMinimumAlpha = old_alpha;

    if (!for_profile)
    { //create alpha mask based on depth buffer (grey out if muted)
        // GL narrows the impostor FBO's draw buffers to just COLOR_ATTACHMENT0 here; no DX-side
        // equivalent needed - DXRenderTarget::bindTarget() binds all of the impostor's attachment RTVs,
        // but this quad draws with gDebugProgram (debugF.hlsl), which declares only a single
        // `SV_Target` output, and a D3D11 pixel shader that doesn't write a bound RTV slot leaves that
        // slot's contents untouched.
        LLGLDisable blend(GL_BLEND);

        if (visually_muted || too_complex)
        {
            gDX.setColorWriteMask(true, true);
        }
        else
        {
            gDX.setColorWriteMask(false, true);
        }

        gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

        DXDepthTest depth(true, false, GL_GREATER);

        gDX.flush();

        gDX.pushMatrix();
        gDX.loadIdentity();
        gDX.matrixMode(LLRender::MM_PROJECTION);
        gDX.pushMatrix();
        gDX.loadIdentity();

        static const F32 clip_plane = 0.99999f;

        gDebugProgram.bind();

        if (visually_muted)
        {   // Visually muted avatar
            LLColor4 muted_color(avatar->getMutedAVColor());
            LL_DEBUGS_ONCE("AvatarRenderPipeline") << "Avatar " << avatar->getID() << " MUTED set solid color " << muted_color << LL_ENDL;
            gDX.diffuseColor4fv(muted_color.mV);
        }
        else if (!preview_avatar)
        { //grey muted avatar
            LL_DEBUGS_ONCE("AvatarRenderPipeline") << "Avatar " << avatar->getID() << " MUTED set grey" << LL_ENDL;
            gDX.diffuseColor4fv(LLColor4::pink.mV);
        }

        gDX.begin(LLRender::TRIANGLES);
        {
            gDX.vertex3f(-1.f, -1.f, clip_plane);
            gDX.vertex3f(1.f, -1.f, clip_plane);
            gDX.vertex3f(1.f, 1.f, clip_plane);

            gDX.vertex3f(-1.f, -1.f, clip_plane);
            gDX.vertex3f(1.f, 1.f, clip_plane);
            gDX.vertex3f(-1.f, 1.f, clip_plane);
        }
        gDX.end();
        gDX.flush();

        gDebugProgram.unbind();

        gDX.popMatrix();
        gDX.matrixMode(LLRender::MM_MODELVIEW);
        gDX.popMatrix();
    }

    if (!preview_avatar && !for_profile)
    {
        avatar->mImpostor.flush();
        avatar->setImpostorDim(tdim);
    }

    DXPipeline::sUseOcclusion = occlusion;
    DXPipeline::sReflectionRender = false;
    DXPipeline::sImpostorRender = false;
    DXPipeline::sShadowRender = false;
    DXPipeline::popRenderTypeMask(pipeline);

    if (!preview_avatar)
    {
        gDX.matrixMode(LLRender::MM_PROJECTION);
        gDX.popMatrix();
        gDX.matrixMode(LLRender::MM_MODELVIEW);
        gDX.popMatrix();
    }

    if (!preview_avatar && !for_profile)
    {
        avatar->mNeedsImpostorUpdate = false;
        avatar->cacheImpostorValues();
        avatar->mLastImpostorUpdateFrameTime = gFrameTimeSeconds;
    }

    LLVertexBuffer::unbind();
    DXState::checkStates();
}

// static
void DXPipeline::setupSpotLight(LLPipeline& pipeline, LLHLSLShader& shader, LLDrawable* drawablep)
{
    //construct frustum
    LLVOVolume* volume = drawablep->getVOVolume();
    LLVector3 params = volume->getSpotLightParams();

    F32 fov = params.mV[0];
    F32 focus = params.mV[1];

    LLVector3 pos = drawablep->getPositionAgent();
    LLQuaternion quat = volume->getRenderRotation();
    LLVector3 scale = volume->getScale();

    //get near clip plane
    LLVector3 at_axis(0, 0, -scale.mV[2] * 0.5f);
    at_axis *= quat;

    LLVector3 np = pos + at_axis;
    at_axis.normVec();

    //get origin that has given fov for plane np, at_axis, and given scale
    F32 dist = (scale.mV[1] * 0.5f) / tanf(fov * 0.5f);

    LLVector3 origin = np - at_axis * dist;

    //matrix from volume space to agent space
    LLMatrix4 light_mat(quat, LLVector4(origin, 1.f));

    glm::mat4 light_to_agent(glm::make_mat4((F32*)light_mat.mMatrix));
    glm::mat4 light_to_screen = get_current_modelview() * light_to_agent;

    glm::mat4 screen_to_light = glm::inverse(light_to_screen);

    F32 s = volume->getLightRadius() * 1.5f;
    F32 near_clip = dist;
    F32 width = scale.mV[VX];
    F32 height = scale.mV[VY];
    F32 far_clip = s + dist - scale.mV[VZ];

    F32 fovy = fov; // radians
    F32 aspect = width / height;

    glm::mat4 trans(0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f,
        0.5f, 0.5f, 0.5f, 1.0f);

    glm::vec3 p1(0, 0, -(near_clip + 0.01f));
    glm::vec3 p2(0, 0, -(near_clip + 1.f));

    glm::vec3 screen_origin(0, 0, 0);

    p1 = mul_mat4_vec3(light_to_screen, p1);
    p2 = mul_mat4_vec3(light_to_screen, p2);
    screen_origin = mul_mat4_vec3(light_to_screen, screen_origin);

    glm::vec3 n = p2 - p1;
    n = glm::normalize(n);

    // Compute projection range
    F32 proj_range = far_clip - near_clip;

    // Compute the projection matrix using the helper function
    //glm::mat4 light_proj;
    // LLViewerCamera::ComputeSpotLightProjectionMatrix(light_proj, fovy, aspect, near_clip, far_clip);
    glm::mat4 light_proj = glm::perspective(fovy, aspect, near_clip, far_clip);
    // Apply transformation to screen_to_light matrix
    screen_to_light = trans * light_proj * screen_to_light;

    // Pass values to shader
    shader.uniformMatrix4fv(LLShaderMgr::PROJECTOR_MATRIX, 1, false, glm::value_ptr(screen_to_light));
    shader.uniform1f(LLShaderMgr::PROJECTOR_NEAR, near_clip);
    shader.uniform3fv(LLShaderMgr::PROJECTOR_P, 1, glm::value_ptr(p1));
    shader.uniform3fv(LLShaderMgr::PROJECTOR_N, 1, glm::value_ptr(n));
    shader.uniform3fv(LLShaderMgr::PROJECTOR_ORIGIN, 1, glm::value_ptr(screen_origin));
    shader.uniform1f(LLShaderMgr::PROJECTOR_RANGE, proj_range);
    shader.uniform1f(LLShaderMgr::PROJECTOR_AMBIANCE, params.mV[2]);

    // Projector-only specular controls. Off (0) leaves the LL lobe and mip selection unchanged.
    static LLCachedControl<bool> proj_spec_reliable(gSavedSettings, "RenderProjectorSpecularReliable", false);
    static LLCachedControl<F32> proj_spec_exponent(gSavedSettings, "RenderProjectorSpecularExponent", 24.f);
    static LLCachedControl<F32> proj_spec_lod(gSavedSettings, "RenderProjectorSpecularLODCap", 1.f);
    static LLCachedControl<F32> spec_exponent_setting(gSavedSettings, "RenderSpecularExponent", 368.f);
    static LLStaticHashedString sProjSpecGlossCap("proj_spec_gloss_cap");
    static LLStaticHashedString sProjSpecLodCap("proj_spec_lod_cap");
    F32 proj_gloss_cap = 0.f;
    F32 proj_lod_cap = 0.f;
    if (proj_spec_reliable)
    {
        // Blinn-Phong exponent is gloss^2 * RenderSpecularExponent (createLUTBuffers), so gloss = sqrt(exponent / specExp).
        proj_gloss_cap = llclamp((F32)sqrt(proj_spec_exponent / llmax((F32)spec_exponent_setting, 1.f)), 0.f, 1.f);
        proj_lod_cap = proj_spec_lod;
    }
    shader.uniform1f(sProjSpecGlossCap, proj_gloss_cap);
    shader.uniform1f(sProjSpecLodCap, proj_lod_cap);
    static LLCachedControl<bool> proj_pbr_legacy(gSavedSettings, "RenderProjectorPBRLegacySpecular", false);
    static LLStaticHashedString sProjPbrLegacySpec("proj_pbr_legacy_spec");
    shader.uniform1f(sProjPbrLegacySpec, proj_pbr_legacy ? 1.f : 0.f);

    S32 s_idx = -1;

    for (U32 i = 0; i < 2; i++)
    {
        if (pipeline.mShadowSpotLight[i] == drawablep)
        {
            s_idx = i;
        }
    }

    shader.uniform1i(LLShaderMgr::PROJECTOR_SHADOW_INDEX, s_idx);

    if (s_idx >= 0)
    {
        shader.uniform1f(LLShaderMgr::PROJECTOR_SHADOW_FADE, 1.f - pipeline.mSpotLightFade[s_idx]);
    }
    else
    {
        shader.uniform1f(LLShaderMgr::PROJECTOR_SHADOW_FADE, 1.f);
    }

    // make sure we're not already targeting the same spot light with both shadow maps
    llassert(pipeline.mTargetShadowSpotLight[0] != pipeline.mTargetShadowSpotLight[1] || pipeline.mTargetShadowSpotLight[0].isNull());

    if (!gCubeSnapshot)
    {
        LLDrawable* potential = drawablep;
        //determine if this light is higher priority than one of the existing spot shadows
        F32 m_pri = volume->getSpotLightPriority();

        // S24: incumbency hysteresis against single-frame near-tie flips. The competition
        // above (dxpipeline.cpp) resets mTargetShadowSpotLight[] every frame, so this
        // tournament has no cross-frame memory of its own - with 3+ spot/projector lights of
        // close priority in range, ordinary camera-position noise (e.g. soft camera-lag settle
        // tail) can flip which two win on a near-arbitrary per-frame basis, wobbling
        // PROJECTOR_SHADOW_FADE every time a light flips in/out of the winning pair. A light
        // already holding a real-time shadow slot (mShadowSpotLight[], the downstream state
        // that actually has memory) needs to be clearly beaten, not just marginally, before a
        // challenger takes its slot.
        static constexpr F32 kSpotShadowHysteresis = 1.05f;
        if (potential == pipeline.mShadowSpotLight[0] || potential == pipeline.mShadowSpotLight[1])
        {
            m_pri *= kSpotShadowHysteresis;
        }

        for (U32 i = 0; i < 2; i++)
        {
            F32 pri = 0.f;

            if (pipeline.mTargetShadowSpotLight[i].notNull())
            {
                // S24: getVOVolume() can return null (drawable dead / not LL_PCODE_VOLUME) if this
                // slot's drawable was torn down out of lock-step - guard matches the sibling site
                // walking the same array at mShadowSpotLight[i]->getVOVolume() below.
                LLVOVolume* target_volume = pipeline.mTargetShadowSpotLight[i]->getVOVolume();
                if (target_volume)
                {
                    pri = target_volume->getSpotLightPriority();
                    if (pipeline.mTargetShadowSpotLight[i] == pipeline.mShadowSpotLight[0] || pipeline.mTargetShadowSpotLight[i] == pipeline.mShadowSpotLight[1])
                    {
                        pri *= kSpotShadowHysteresis;
                    }
                }
                else
                {
                    pipeline.mTargetShadowSpotLight[i] = NULL;
                }
            }

            if (m_pri > pri)
            {
                LLDrawable* temp = pipeline.mTargetShadowSpotLight[i];
                pipeline.mTargetShadowSpotLight[i] = potential;
                potential = temp;
                m_pri = pri;
            }
        }
    }

    // make sure we didn't end up targeting the same spot light with both shadow maps
    llassert(pipeline.mTargetShadowSpotLight[0] != pipeline.mTargetShadowSpotLight[1] || pipeline.mTargetShadowSpotLight[0].isNull());

    LLViewerTexture* img = volume->getLightTexture();

    if (img == NULL)
    {
        img = LLViewerFetchedTexture::sWhiteImagep;
    }

    S32 channel = shader.enableTexture(LLShaderMgr::DEFERRED_PROJECTION);

    if (channel > -1)
    {
        if (img)
        {
            gDX.getTexUnit(channel)->bind(img);

            F32 lod_range = logf((F32)img->getWidth()) / logf(2.f);

            shader.uniform1f(LLShaderMgr::PROJECTOR_FOCUS, focus);
            shader.uniform1f(LLShaderMgr::PROJECTOR_LOD, lod_range);
            shader.uniform1f(LLShaderMgr::PROJECTOR_AMBIENT_LOD, llclamp((proj_range - focus) / proj_range * lod_range, 0.f, 1.f));
        }
    }
}

// ============================================================================
// Render-type/render-debug bitmask plumbing, visibility toggles, beacons
// ============================================================================

// static
bool DXPipeline::hasRenderType(const LLPipeline& pipeline, const U32 type)
{
    // STORM-365 : LLViewerJointAttachment::setAttachmentVisibility() is setting type to 0 to actually mean "do not render"
    // We then need to test that value here and return false to prevent attachment to render (in mouselook for instance)
    // TODO: reintroduce RENDER_TYPE_NONE in LLRenderTypeMask and initialize its mRenderTypeEnabled[RENDER_TYPE_NONE] to false explicitely
    return (type == 0 ? false : pipeline.getRenderTypeEnabled()[type]);
}

// static
void DXPipeline::setRenderTypeMask(LLPipeline& pipeline, U32 type, ...)
{
    pipeline.assertInitialized();

    va_list args;
    va_start(args, type);
    while (type < LLPipeline::END_RENDER_TYPES)
    {
        pipeline.getRenderTypeEnabledMutable()[type] = true;
        type = va_arg(args, U32);
    }
    va_end(args);
}

// static
bool DXPipeline::hasAnyRenderType(const LLPipeline& pipeline, U32 type, ...)
{
    va_list args;
    va_start(args, type);

    while (type < LLPipeline::END_RENDER_TYPES)
    {
        if (pipeline.getRenderTypeEnabled()[type])
        {
            va_end(args);
            return true;
        }
        type = va_arg(args, U32);
    }
    va_end(args);

    return false;
}

// static
void DXPipeline::pushRenderTypeMask(LLPipeline& pipeline)
{
    std::string cur_mask;
    cur_mask.assign((const char*)pipeline.getRenderTypeEnabledMutable(), sizeof(pipeline.getRenderTypeEnabledMutable()));
    pipeline.getRenderTypeEnableStackMutable().push(cur_mask);
}

// static
void DXPipeline::popRenderTypeMask(LLPipeline& pipeline)
{
    if (pipeline.getRenderTypeEnableStackMutable().empty())
        return;

    memcpy(pipeline.getRenderTypeEnabledMutable(),
        pipeline.getRenderTypeEnableStackMutable().top().data(),
        sizeof(pipeline.getRenderTypeEnabledMutable()));

    pipeline.getRenderTypeEnableStackMutable().pop();
}

// static
void DXPipeline::andRenderTypeMask(LLPipeline& pipeline, U32 type, ...)
{
    // Build a mask of allowed types
    bool allowed[LLPipeline::NUM_RENDER_TYPES] = { false };

    va_list args;
    va_start(args, type);

    while (type < LLPipeline::END_RENDER_TYPES)
    {
        allowed[type] = true;
        type = va_arg(args, U32);
    }

    va_end(args);

    // AND the current mask with the allowed mask
    for (U32 i = 0; i < LLPipeline::NUM_RENDER_TYPES; ++i)
    {
        pipeline.getRenderTypeEnabledMutable()[i] = pipeline.getRenderTypeEnabledMutable()[i] && allowed[i];
    }
}

// static
void DXPipeline::clearRenderTypeMask(LLPipeline& pipeline, U32 type, ...)
{
    va_list args;
    va_start(args, type);
    while (type < LLPipeline::END_RENDER_TYPES)
    {
        pipeline.getRenderTypeEnabledMutable()[type] = false;
        type = va_arg(args, U32);
    }
    va_end(args);
}

// static
void DXPipeline::setAllRenderTypes(LLPipeline& pipeline)
{
    memset(pipeline.getRenderTypeEnabledMutable(), 1, sizeof(pipeline.getRenderTypeEnabledMutable()));
}

// static
void DXPipeline::clearAllRenderTypes(LLPipeline& pipeline)
{
    memset(pipeline.getRenderTypeEnabledMutable(), 0, sizeof(pipeline.getRenderTypeEnabledMutable()));
}

// static
void DXPipeline::hidePermanentObjects(LLPipeline& pipeline, std::vector<U32>& restoreList)
{
    const U32 count = gObjectList.getNumObjects();
    restoreList.reserve(restoreList.size() + count); // avoid reallocs

    for (U32 i = 0; i < count; ++i)
    {
        LLViewerObject* obj = gObjectList.getObject(i);
        if (!obj || !obj->flagObjectPermanent())
            continue;

        LLDrawable* drawable = obj->mDrawable;
        if (!drawable)
            continue;

        restoreList.push_back(i);
        DXPipeline::hideDrawable(pipeline, drawable);
    }

    DXPipeline::skipRenderingOfTerrain(pipeline, true);
}

// static
void DXPipeline::restorePermanentObjects(LLPipeline& pipeline, const std::vector<U32>& restoreList)
{
    const U32 count = gObjectList.getNumObjects();

    for (U32 index : restoreList)
    {
        if (index >= count)
            continue;

        LLViewerObject* obj = gObjectList.getObject(index);
        if (!obj)
            continue;

        LLDrawable* drawable = obj->mDrawable;
        if (!drawable)
            continue;

        drawable->clearState(LLDrawable::FORCE_INVISIBLE);
        DXPipeline::unhideDrawable(pipeline, drawable);
    }

    DXPipeline::skipRenderingOfTerrain(pipeline, false);
}

// static
void DXPipeline::skipRenderingOfTerrain(LLPipeline& pipeline, bool flag)
{
    for (LLDrawPool* pool : pipeline.getPools())
    {
        const U32 type = pool->getType();

        // Cheap check first, expensive check second
        if (type != LLDrawPool::POOL_TERRAIN)
            continue;

        if (!DXPipeline::hasRenderType(pipeline, type))
            continue;

        pool->setSkipRenderFlag(flag);
    }
}

// static
void DXPipeline::hideObject(LLPipeline& pipeline, const LLUUID& id)
{
    LLViewerObject* obj = gObjectList.findObject(id);
    if (!obj)
        return;

    LLDrawable* drawable = obj->mDrawable;
    if (!drawable)
        return;

    DXPipeline::hideDrawable(pipeline, drawable);
}

// static
void DXPipeline::hideDrawable(LLPipeline& pipeline, LLDrawable* drawable)
{
    drawable->setState(LLDrawable::FORCE_INVISIBLE);
    DXPipeline::markRebuild(pipeline, drawable, LLDrawable::REBUILD_ALL);

    const auto& children = drawable->getVObj()->getChildren();
    for (LLViewerObject* child : children)
    {
        LLDrawable* cd = child->mDrawable;
        if (!cd)
            continue;

        cd->setState(LLDrawable::FORCE_INVISIBLE);
        DXPipeline::markRebuild(pipeline, cd, LLDrawable::REBUILD_ALL);
    }
}

// static
void DXPipeline::unhideDrawable(LLPipeline& pipeline, LLDrawable* drawable)
{
    drawable->clearState(LLDrawable::FORCE_INVISIBLE);
    DXPipeline::markRebuild(pipeline, drawable, LLDrawable::REBUILD_ALL);

    const auto& children = drawable->getVObj()->getChildren();
    for (LLViewerObject* child : children)
    {
        LLDrawable* cd = child->mDrawable;
        if (!cd)
            continue;

        cd->clearState(LLDrawable::FORCE_INVISIBLE);
        DXPipeline::markRebuild(pipeline, cd, LLDrawable::REBUILD_ALL);
    }
}

// static
void DXPipeline::restoreHiddenObject(LLPipeline& pipeline, const LLUUID& id)
{
    LLViewerObject* obj = gObjectList.findObject(id);
    if (!obj)
        return;

    LLDrawable* drawable = obj->mDrawable;
    if (!drawable)
        return;

    DXPipeline::unhideDrawable(pipeline, drawable);
}

// static
void DXPipeline::skipRenderingShadows(LLPipeline& pipeline)
{
    DXDepthTest depth(true);

    for (auto& shadowRT : pipeline.mRT->shadow)
    {
        shadowRT.bindTarget();
        shadowRT.clear();
        shadowRT.flush();
    }
}

// Used only by rebuildDrawInfo() below.
class LLOctreeDirty : public OctreeTraveler
{
public:
    void visit(const OctreeNode* node) override
    {
        auto* group = static_cast<LLSpatialGroup*>(node->getListener(0));
        if (!group)
            return;

        if (group->getSpatialPartition()->mRenderByGroup)
        {
            group->setState(LLSpatialGroup::GEOM_DIRTY);
            DXPipeline::markRebuild(gPipeline, group);
        }

        for (LLSpatialBridge* bridge : group->mBridgeList)
        {
            if (bridge && bridge->mOctree)
                traverse(bridge->mOctree);
        }
    }
};

// static
void DXPipeline::rebuildDrawInfo(LLPipeline& pipeline)
{
    static constexpr U32 types_to_traverse[] =
    {
        LLViewerRegion::PARTITION_VOLUME,
        LLViewerRegion::PARTITION_BRIDGE,
        LLViewerRegion::PARTITION_AVATAR
    };

    LLOctreeDirty dirty;

    for (LLViewerRegion* region : LLWorld::getInstance()->getRegionList())
    {
        for (U32 type : types_to_traverse)
        {
            LLSpatialPartition* part = region->getSpatialPartition(type);
            if (!part || !part->mOctree)
                continue;

            dirty.traverse(part->mOctree);
        }
    }
}

// static
void DXPipeline::rebuildTerrain(LLPipeline& pipeline)
{
    for (LLViewerRegion* region : LLWorld::getInstance()->getRegionList())
    {
        region->dirtyAllPatches();
    }
}

// static
void DXPipeline::toggleRenderType(LLPipeline& pipeline, U32 type, bool mark_dirty)
{
    pipeline.getRenderTypeEnabledMutable()[type] = !pipeline.getRenderTypeEnabledMutable()[type];
    bool is_enabled = pipeline.getRenderTypeEnabledMutable()[type];

    if (type == LLPipeline::RENDER_TYPE_WATER)
    {
        pipeline.getRenderTypeEnabledMutable()[LLPipeline::RENDER_TYPE_VOIDWATER] = !pipeline.getRenderTypeEnabledMutable()[LLPipeline::RENDER_TYPE_VOIDWATER];
    }
    else if (type == LLPipeline::RENDER_TYPE_GLTF_PBR)
    {
        // Toggle all PBR-related render types together
        pipeline.getRenderTypeEnabledMutable()[LLPipeline::RENDER_TYPE_GLTF_PBR_ALPHA_MASK] = is_enabled;
        pipeline.getRenderTypeEnabledMutable()[LLPipeline::RENDER_TYPE_PASS_GLTF_PBR] = is_enabled;
        pipeline.getRenderTypeEnabledMutable()[LLPipeline::RENDER_TYPE_PASS_GLTF_PBR_RIGGED] = is_enabled;
        pipeline.getRenderTypeEnabledMutable()[LLPipeline::RENDER_TYPE_PASS_GLTF_PBR_ALPHA_MASK] = is_enabled;
        pipeline.getRenderTypeEnabledMutable()[LLPipeline::RENDER_TYPE_PASS_GLTF_PBR_ALPHA_MASK_RIGGED] = is_enabled;
        pipeline.getRenderTypeEnabledMutable()[LLPipeline::RENDER_TYPE_PASS_GLTF_GLOW] = is_enabled;
        pipeline.getRenderTypeEnabledMutable()[LLPipeline::RENDER_TYPE_PASS_GLTF_GLOW_RIGGED] = is_enabled;

        if (mark_dirty)
        {
            // Mark all geometry dirty so faces are re-sorted into correct render pools
            DXPipeline::markAllGeometryDirty(pipeline);
        }
    }
}

// static
void DXPipeline::toggleRenderTypeControl(LLPipeline& pipeline, U32 type)
{
    DXPipeline::toggleRenderType(pipeline, type);
}

// static
bool DXPipeline::hasRenderTypeControl(LLPipeline& pipeline, U32 type)
{
    return DXPipeline::hasRenderType(pipeline, type);
}

// Allows UI items labeled "Hide foo" instead of "Show foo"
// static
bool DXPipeline::toggleRenderTypeControlNegated(LLPipeline& pipeline, S32 type)
{
    return !DXPipeline::hasRenderType(pipeline, type);
}

// static
void DXPipeline::toggleRenderDebug(LLPipeline& pipeline, U64 bit)
{
    if (pipeline.hasRenderDebugMask(bit))
    {
        LL_INFOS() << "Toggling render debug mask " << std::hex << bit << " off" << std::dec << LL_ENDL;
    }
    else
    {
        LL_INFOS() << "Toggling render debug mask " << std::hex << bit << " on" << std::dec << LL_ENDL;
    }
    pipeline.getRenderDebugMaskMutable() ^= bit;
}

// static
bool DXPipeline::toggleRenderDebugControl(LLPipeline& pipeline, U64 bit)
{
    return pipeline.hasRenderDebugMask(bit);
}

// static
void DXPipeline::toggleRenderDebugFeature(LLPipeline& pipeline, U32 bit)
{
    pipeline.getRenderDebugFeatureMaskMutable() ^= bit;
}

// static
bool DXPipeline::toggleRenderDebugFeatureControl(LLPipeline& pipeline, U32 bit)
{
    return pipeline.hasRenderDebugFeatureMask(bit);
}

// static
void DXPipeline::setRenderDebugFeatureControl(LLPipeline& pipeline, U32 bit, bool value)
{
    if (value)
    {
        pipeline.getRenderDebugFeatureMaskMutable() |= bit;
    }
    else
    {
        pipeline.getRenderDebugFeatureMaskMutable() &= ~bit;
    }
}

// static
void DXPipeline::pushRenderDebugFeatureMask(LLPipeline& pipeline)
{
    pipeline.getRenderDebugFeatureStackMutable().push(pipeline.getRenderDebugFeatureMaskMutable());
}

// static
void DXPipeline::popRenderDebugFeatureMask(LLPipeline& pipeline)
{
    if (pipeline.getRenderDebugFeatureStackMutable().empty())
    {
        LL_ERRS() << "Depleted render feature stack." << LL_ENDL;
    }

    pipeline.getRenderDebugFeatureMaskMutable() = pipeline.getRenderDebugFeatureStackMutable().top();
    pipeline.getRenderDebugFeatureStackMutable().pop();
}

// static
void DXPipeline::setRenderScriptedBeacons(bool val)
{
    DXPipeline::sRenderScriptedBeacons = val;
}

// static
void DXPipeline::toggleRenderScriptedBeacons()
{
    DXPipeline::sRenderScriptedBeacons = !DXPipeline::sRenderScriptedBeacons;
}

// static
bool DXPipeline::getRenderScriptedBeacons()
{
    return DXPipeline::sRenderScriptedBeacons;
}

// static
void DXPipeline::setRenderScriptedTouchBeacons(bool val)
{
    DXPipeline::sRenderScriptedTouchBeacons = val;
}

// static
void DXPipeline::toggleRenderScriptedTouchBeacons()
{
    DXPipeline::sRenderScriptedTouchBeacons = !DXPipeline::sRenderScriptedTouchBeacons;
}

// static
bool DXPipeline::getRenderScriptedTouchBeacons()
{
    return DXPipeline::sRenderScriptedTouchBeacons;
}

// static
void DXPipeline::setRenderMOAPBeacons(bool val)
{
    DXPipeline::sRenderMOAPBeacons = val;
}

// static
void DXPipeline::toggleRenderMOAPBeacons()
{
    DXPipeline::sRenderMOAPBeacons = !DXPipeline::sRenderMOAPBeacons;
}

// static
bool DXPipeline::getRenderMOAPBeacons()
{
    return DXPipeline::sRenderMOAPBeacons;
}

// static
void DXPipeline::setRenderPhysicalBeacons(bool val)
{
    DXPipeline::sRenderPhysicalBeacons = val;
}

// static
void DXPipeline::toggleRenderPhysicalBeacons()
{
    DXPipeline::sRenderPhysicalBeacons = !DXPipeline::sRenderPhysicalBeacons;
}

// static
bool DXPipeline::getRenderPhysicalBeacons()
{
    return DXPipeline::sRenderPhysicalBeacons;
}

// static
void DXPipeline::setRenderParticleBeacons(bool val)
{
    DXPipeline::sRenderParticleBeacons = val;
}

// static
void DXPipeline::toggleRenderParticleBeacons()
{
    DXPipeline::sRenderParticleBeacons = !DXPipeline::sRenderParticleBeacons;
}

// static
bool DXPipeline::getRenderParticleBeacons()
{
    return DXPipeline::sRenderParticleBeacons;
}

// static
void DXPipeline::setRenderSoundBeacons(bool val)
{
    DXPipeline::sRenderSoundBeacons = val;
}

// static
void DXPipeline::toggleRenderSoundBeacons()
{
    DXPipeline::sRenderSoundBeacons = !DXPipeline::sRenderSoundBeacons;
}

// static
bool DXPipeline::getRenderSoundBeacons()
{
    return DXPipeline::sRenderSoundBeacons;
}

// static
void DXPipeline::setRenderBeacons(bool val)
{
    DXPipeline::sRenderBeacons = val;
}

// static
void DXPipeline::toggleRenderBeacons()
{
    DXPipeline::sRenderBeacons = !DXPipeline::sRenderBeacons;
}

// static
bool DXPipeline::getRenderBeacons()
{
    return DXPipeline::sRenderBeacons;
}

// static
void DXPipeline::setRenderHighlights(bool val)
{
    DXPipeline::sRenderHighlight = val;
}

// static
void DXPipeline::toggleRenderHighlights()
{
    DXPipeline::sRenderHighlight = !DXPipeline::sRenderHighlight;
}

// static
bool DXPipeline::getRenderHighlights()
{
    return DXPipeline::sRenderHighlight;
}

// static
void DXPipeline::setRenderHighlightTextureChannel(LLRender::eTexIndex channel)
{
    if (channel != DXPipeline::sRenderHighlightTextureChannel)
    {
        DXPipeline::sRenderHighlightTextureChannel = channel;
    }
}

// ============================================================================
// Pool management
// ============================================================================

// static
LLDrawPool* DXPipeline::findPool(LLPipeline& pipeline, const U32 type, LLViewerTexture* tex0)
{
    pipeline.assertInitialized();

    LLDrawPool* poolp = NULL;
    switch (type)
    {
    case LLDrawPool::POOL_SIMPLE:
        poolp = pipeline.mSimplePool;
        break;

    case LLDrawPool::POOL_GRASS:
        poolp = pipeline.mGrassPool;
        break;

    case LLDrawPool::POOL_ALPHA_MASK:
        poolp = pipeline.mAlphaMaskPool;
        break;

    case LLDrawPool::POOL_FULLBRIGHT_ALPHA_MASK:
        poolp = pipeline.mFullbrightAlphaMaskPool;
        break;

    case LLDrawPool::POOL_FULLBRIGHT:
        poolp = pipeline.mFullbrightPool;
        break;

    case LLDrawPool::POOL_GLOW:
        poolp = pipeline.mGlowPool;
        break;

    case LLDrawPool::POOL_TREE:
        poolp = get_if_there(pipeline.mTreePools, (uintptr_t)tex0, (LLDrawPool*)0);
        break;

    case LLDrawPool::POOL_TERRAIN:
        poolp = get_if_there(pipeline.mTerrainPools, (uintptr_t)tex0, (LLDrawPool*)0);
        break;

    case LLDrawPool::POOL_BUMP:
        poolp = pipeline.mBumpPool;
        break;
    case LLDrawPool::POOL_MATERIALS:
        poolp = pipeline.mMaterialsPool;
        break;
    case LLDrawPool::POOL_ALPHA_PRE_WATER:
        poolp = pipeline.mAlphaPoolPreWater;
        break;
    case LLDrawPool::POOL_ALPHA_POST_WATER:
        poolp = pipeline.mAlphaPoolPostWater;
        break;

    case LLDrawPool::POOL_AVATAR:
    case LLDrawPool::POOL_CONTROL_AV:
        break; // Do nothing

    case LLDrawPool::POOL_SKY:
        poolp = pipeline.mSkyPool;
        break;

    case LLDrawPool::POOL_WATER:
        poolp = pipeline.mWaterPool;
        break;

    case LLDrawPool::POOL_WL_SKY:
        poolp = pipeline.mWLSkyPool;
        break;

    case LLDrawPool::POOL_GLTF_PBR:
        poolp = pipeline.mPBROpaquePool;
        break;
    case LLDrawPool::POOL_GLTF_PBR_ALPHA_MASK:
        poolp = pipeline.mPBRAlphaMaskPool;
        break;

    case LLDrawPool::POOL_WATEREXCLUSION:
        poolp = pipeline.mWaterExclusionPool;
        break;

    default:
        llassert(0);
        LL_ERRS() << "Invalid Pool Type in  LLPipeline::findPool() type=" << type << LL_ENDL;
        break;
    }

    return poolp;
}

// static
LLDrawPool* DXPipeline::getPool(LLPipeline& pipeline, const U32 type, LLViewerTexture* tex0)
{
    LLDrawPool* poolp = DXPipeline::findPool(pipeline, type, tex0);
    if (poolp)
    {
        return poolp;
    }

    LLDrawPool* new_poolp = LLDrawPool::createPool(type, tex0);
    DXPipeline::addPool(pipeline, new_poolp);

    return new_poolp;
}

// static
LLDrawPool* DXPipeline::getPoolFromTE(LLPipeline& pipeline, const LLTextureEntry* te, LLViewerTexture* imagep)
{
    U32 type = DXPipeline::getPoolTypeFromTE(te, imagep);
    return DXPipeline::getPool(pipeline, type, imagep);
}

// static
void DXPipeline::addPool(LLPipeline& pipeline, LLDrawPool* new_poolp)
{
    pipeline.assertInitialized();
    pipeline.mPools.insert(new_poolp);
    DXPipeline::addToQuickLookup(pipeline, new_poolp);
}

// static
void DXPipeline::rebuildPools(LLPipeline& pipeline)
{
    pipeline.assertInitialized();

    auto max_count = pipeline.mPools.size();
    auto iter1 = pipeline.mPools.upper_bound(pipeline.mLastRebuildPool);
    while (max_count > 0 && pipeline.mPools.size() > 0) // && num_rebuilds < MAX_REBUILDS)
    {
        if (iter1 == pipeline.mPools.end())
        {
            iter1 = pipeline.mPools.begin();
        }
        LLDrawPool* poolp = *iter1;

        if (poolp->isDead())
        {
            pipeline.mPools.erase(iter1++);
            DXPipeline::removeFromQuickLookup(pipeline, poolp);
            if (poolp == pipeline.mLastRebuildPool)
            {
                pipeline.mLastRebuildPool = NULL;
            }
            delete poolp;
        }
        else
        {
            pipeline.mLastRebuildPool = poolp;
            iter1++;
        }
        max_count--;
    }
}

// static
void DXPipeline::addToQuickLookup(LLPipeline& pipeline, LLDrawPool* new_poolp)
{
    pipeline.assertInitialized();

    switch (new_poolp->getType())
    {
    case LLDrawPool::POOL_SIMPLE:
        if (pipeline.mSimplePool)
        {
            llassert(0);
            LL_WARNS() << "Ignoring duplicate simple pool." << LL_ENDL;
        }
        else
        {
            pipeline.mSimplePool = (LLRenderPass*)new_poolp;
        }
        break;

    case LLDrawPool::POOL_ALPHA_MASK:
        if (pipeline.mAlphaMaskPool)
        {
            llassert(0);
            LL_WARNS() << "Ignoring duplicate alpha mask pool." << LL_ENDL;
            break;
        }
        else
        {
            pipeline.mAlphaMaskPool = (LLRenderPass*)new_poolp;
        }
        break;

    case LLDrawPool::POOL_FULLBRIGHT_ALPHA_MASK:
        if (pipeline.mFullbrightAlphaMaskPool)
        {
            llassert(0);
            LL_WARNS() << "Ignoring duplicate alpha mask pool." << LL_ENDL;
            break;
        }
        else
        {
            pipeline.mFullbrightAlphaMaskPool = (LLRenderPass*)new_poolp;
        }
        break;

    case LLDrawPool::POOL_GRASS:
        if (pipeline.mGrassPool)
        {
            llassert(0);
            LL_WARNS() << "Ignoring duplicate grass pool." << LL_ENDL;
        }
        else
        {
            pipeline.mGrassPool = (LLRenderPass*)new_poolp;
        }
        break;

    case LLDrawPool::POOL_FULLBRIGHT:
        if (pipeline.mFullbrightPool)
        {
            llassert(0);
            LL_WARNS() << "Ignoring duplicate simple pool." << LL_ENDL;
        }
        else
        {
            pipeline.mFullbrightPool = (LLRenderPass*)new_poolp;
        }
        break;

    case LLDrawPool::POOL_GLOW:
        if (pipeline.mGlowPool)
        {
            llassert(0);
            LL_WARNS() << "Ignoring duplicate glow pool." << LL_ENDL;
        }
        else
        {
            pipeline.mGlowPool = (LLRenderPass*)new_poolp;
        }
        break;

    case LLDrawPool::POOL_TREE:
        pipeline.mTreePools[uintptr_t(new_poolp->getTexture())] = new_poolp;
        break;

    case LLDrawPool::POOL_TERRAIN:
        pipeline.mTerrainPools[uintptr_t(new_poolp->getTexture())] = new_poolp;
        break;

    case LLDrawPool::POOL_BUMP:
        if (pipeline.mBumpPool)
        {
            llassert(0);
            LL_WARNS() << "Ignoring duplicate bump pool." << LL_ENDL;
        }
        else
        {
            pipeline.mBumpPool = new_poolp;
        }
        break;
    case LLDrawPool::POOL_MATERIALS:
        if (pipeline.mMaterialsPool)
        {
            llassert(0);
            LL_WARNS() << "Ignorning duplicate materials pool." << LL_ENDL;
        }
        else
        {
            pipeline.mMaterialsPool = new_poolp;
        }
        break;
    case LLDrawPool::POOL_ALPHA_PRE_WATER:
        if (pipeline.mAlphaPoolPreWater)
        {
            llassert(0);
            LL_WARNS() << "LLPipeline::addPool(): Ignoring duplicate Alpha pre-water pool" << LL_ENDL;
        }
        else
        {
            pipeline.mAlphaPoolPreWater = (DXAlphaDrawPool*)new_poolp;
        }
        break;
    case LLDrawPool::POOL_ALPHA_POST_WATER:
        if (pipeline.mAlphaPoolPostWater)
        {
            llassert(0);
            LL_WARNS() << "LLPipeline::addPool(): Ignoring duplicate Alpha post-water pool" << LL_ENDL;
        }
        else
        {
            pipeline.mAlphaPoolPostWater = (DXAlphaDrawPool*)new_poolp;
        }
        break;

    case LLDrawPool::POOL_AVATAR:
    case LLDrawPool::POOL_CONTROL_AV:
        break; // Do nothing

    case LLDrawPool::POOL_SKY:
        if (pipeline.mSkyPool)
        {
            llassert(0);
            LL_WARNS() << "LLPipeline::addPool(): Ignoring duplicate Sky pool" << LL_ENDL;
        }
        else
        {
            pipeline.mSkyPool = new_poolp;
        }
        break;

    case LLDrawPool::POOL_WATER:
        if (pipeline.mWaterPool)
        {
            llassert(0);
            LL_WARNS() << "LLPipeline::addPool(): Ignoring duplicate Water pool" << LL_ENDL;
        }
        else
        {
            pipeline.mWaterPool = new_poolp;
        }
        break;

    case LLDrawPool::POOL_WL_SKY:
        if (pipeline.mWLSkyPool)
        {
            llassert(0);
            LL_WARNS() << "LLPipeline::addPool(): Ignoring duplicate WLSky Pool" << LL_ENDL;
        }
        else
        {
            pipeline.mWLSkyPool = new_poolp;
        }
        break;

    case LLDrawPool::POOL_GLTF_PBR:
        if (pipeline.mPBROpaquePool)
        {
            llassert(0);
            LL_WARNS() << "LLPipeline::addPool(): Ignoring duplicate PBR Opaque Pool" << LL_ENDL;
        }
        else
        {
            pipeline.mPBROpaquePool = new_poolp;
        }
        break;

    case LLDrawPool::POOL_GLTF_PBR_ALPHA_MASK:
        if (pipeline.mPBRAlphaMaskPool)
        {
            llassert(0);
            LL_WARNS() << "LLPipeline::addPool(): Ignoring duplicate PBR Alpha Mask Pool" << LL_ENDL;
        }
        else
        {
            pipeline.mPBRAlphaMaskPool = new_poolp;
        }
        break;

    case LLDrawPool::POOL_WATEREXCLUSION:
        if (pipeline.mWaterExclusionPool)
        {
            llassert(0);
            LL_WARNS() << "LLPipeline::addPool(): Ignoring duplicate Water Exclusion Pool" << LL_ENDL;
        }
        else
        {
            pipeline.mWaterExclusionPool = new_poolp;
        }
        break;

    default:
        llassert(0);
        LL_WARNS() << "Invalid Pool Type in  LLPipeline::addPool()" << LL_ENDL;
        break;
    }
}

// static
void DXPipeline::removePool(LLPipeline& pipeline, LLDrawPool* poolp)
{
    pipeline.assertInitialized();
    DXPipeline::removeFromQuickLookup(pipeline, poolp);
    pipeline.mPools.erase(poolp);
    delete poolp;
}

// static
void DXPipeline::removeFromQuickLookup(LLPipeline& pipeline, LLDrawPool* poolp)
{
    pipeline.assertInitialized();
    switch (poolp->getType())
    {
    case LLDrawPool::POOL_SIMPLE:
        llassert(pipeline.mSimplePool == poolp);
        pipeline.mSimplePool = NULL;
        break;

    case LLDrawPool::POOL_ALPHA_MASK:
        llassert(pipeline.mAlphaMaskPool == poolp);
        pipeline.mAlphaMaskPool = NULL;
        break;

    case LLDrawPool::POOL_FULLBRIGHT_ALPHA_MASK:
        llassert(pipeline.mFullbrightAlphaMaskPool == poolp);
        pipeline.mFullbrightAlphaMaskPool = NULL;
        break;

    case LLDrawPool::POOL_GRASS:
        llassert(pipeline.mGrassPool == poolp);
        pipeline.mGrassPool = NULL;
        break;

    case LLDrawPool::POOL_FULLBRIGHT:
        llassert(pipeline.mFullbrightPool == poolp);
        pipeline.mFullbrightPool = NULL;
        break;

    case LLDrawPool::POOL_WL_SKY:
        llassert(pipeline.mWLSkyPool == poolp);
        pipeline.mWLSkyPool = NULL;
        break;

    case LLDrawPool::POOL_GLOW:
        llassert(pipeline.mGlowPool == poolp);
        pipeline.mGlowPool = NULL;
        break;

    case LLDrawPool::POOL_TREE:
#ifdef _DEBUG
    {
        bool found = pipeline.mTreePools.erase((uintptr_t)poolp->getTexture());
        llassert(found);
    }
#else
        pipeline.mTreePools.erase((uintptr_t)poolp->getTexture());
#endif
        break;

    case LLDrawPool::POOL_TERRAIN:
#ifdef _DEBUG
    {
        bool found = pipeline.mTerrainPools.erase((uintptr_t)poolp->getTexture());
        llassert(found);
    }
#else
        pipeline.mTerrainPools.erase((uintptr_t)poolp->getTexture());
#endif
        break;

    case LLDrawPool::POOL_BUMP:
        llassert(poolp == pipeline.mBumpPool);
        pipeline.mBumpPool = NULL;
        break;

    case LLDrawPool::POOL_MATERIALS:
        llassert(poolp == pipeline.mMaterialsPool);
        pipeline.mMaterialsPool = NULL;
        break;

    case LLDrawPool::POOL_ALPHA_PRE_WATER:
        llassert(poolp == pipeline.mAlphaPoolPreWater);
        pipeline.mAlphaPoolPreWater = nullptr;
        break;

    case LLDrawPool::POOL_ALPHA_POST_WATER:
        llassert(poolp == pipeline.mAlphaPoolPostWater);
        pipeline.mAlphaPoolPostWater = nullptr;
        break;

    case LLDrawPool::POOL_AVATAR:
    case LLDrawPool::POOL_CONTROL_AV:
        break; // Do nothing

    case LLDrawPool::POOL_SKY:
        llassert(poolp == pipeline.mSkyPool);
        pipeline.mSkyPool = NULL;
        break;

    case LLDrawPool::POOL_WATER:
        llassert(poolp == pipeline.mWaterPool);
        pipeline.mWaterPool = NULL;
        break;

    case LLDrawPool::POOL_GLTF_PBR:
        llassert(poolp == pipeline.mPBROpaquePool);
        pipeline.mPBROpaquePool = NULL;
        break;

    case LLDrawPool::POOL_GLTF_PBR_ALPHA_MASK:
        llassert(poolp == pipeline.mPBRAlphaMaskPool);
        pipeline.mPBRAlphaMaskPool = NULL;
        break;

    case LLDrawPool::POOL_WATEREXCLUSION:
        llassert(poolp == pipeline.mWaterExclusionPool);
        pipeline.mWaterExclusionPool = nullptr;
        break;

    default:
        llassert(0);
        LL_WARNS() << "Invalid Pool Type in  LLPipeline::removeFromQuickLookup() type=" << poolp->getType() << LL_ENDL;
        break;
    }
}

// static
void DXPipeline::resetDrawOrders(LLPipeline& pipeline)
{
    pipeline.assertInitialized();
    // Iterate through all of the draw pools and rebuild them.
    for (auto iter = pipeline.mPools.begin(); iter != pipeline.mPools.end(); ++iter)
    {
        LLDrawPool* poolp = *iter;
        poolp->resetDrawOrders();
    }
}

// ============================================================================
// Drawable/object lifecycle
// ============================================================================

// static
void DXPipeline::allocDrawable(LLPipeline& pipeline, LLViewerObject* vobj)
{
    LLDrawable* drawable = new LLDrawable(vobj);
    vobj->mDrawable = drawable;

    //encompass completely sheared objects by taking
    //the most extreme point possible (<1,1,0.5>)
    drawable->setRadius(LLVector3(1, 1, 0.5f).scaleVec(vobj->getScale()).length());
    if (vobj->isOrphaned())
    {
        drawable->setState(LLDrawable::FORCE_INVISIBLE);
    }
    drawable->updateXform(true);
}

// static
void DXPipeline::unlinkDrawable(LLPipeline& pipeline, LLDrawable* drawable)
{
    pipeline.assertInitialized();

    LLPointer<LLDrawable> drawablep = drawable; // make sure this doesn't get deleted before we are done

    // Based on flags, remove the drawable from the queues that it's on.
    if (drawablep->isState(LLDrawable::ON_MOVE_LIST))
    {
        LLDrawable::drawable_vector_t::iterator iter = std::find(pipeline.mMovedList.begin(), pipeline.mMovedList.end(), drawablep);
        if (iter != pipeline.mMovedList.end())
        {
            pipeline.mMovedList.erase(iter);
        }
    }

    if (drawablep->getSpatialGroup())
    {
        if (!drawablep->getSpatialGroup()->getSpatialPartition()->remove(drawablep, drawablep->getSpatialGroup()))
        {
#ifdef LL_RELEASE_FOR_DOWNLOAD
            LL_WARNS() << "Couldn't remove object from spatial group!" << LL_ENDL;
#else
            LL_ERRS() << "Couldn't remove object from spatial group!" << LL_ENDL;
#endif
        }
    }

    pipeline.getLightsMutable().erase(drawablep);

    for (LLPipeline::light_set_t::iterator iter = pipeline.getNearbyLightsMutable().begin();
        iter != pipeline.getNearbyLightsMutable().end(); iter++)
    {
        if (iter->drawable == drawablep)
        {
            pipeline.getNearbyLightsMutable().erase(iter);
            break;
        }
    }

    for (U32 i = 0; i < 2; ++i)
    {
        if (pipeline.mShadowSpotLight[i] == drawablep)
        {
            pipeline.mShadowSpotLight[i] = NULL;
            // S24: mSpotLightFade[i] must go to 0 in lockstep with mShadowSpotLight[i] going
            // NULL - the reassignment check further down (generateShadows) treats an empty slot
            // and a fully-decayed fade as interchangeable (`fade==0.f || isNull()`), but this is
            // an abrupt null outside that decay path. Left stale (e.g. near 1.0 from a
            // previously fully-visible light), a slot reassigned here to a DIFFERENT light before
            // its own natural fade-out would inherit the old light's near-1.0 value, so
            // PROJECTOR_SHADOW_FADE (1-mSpotLightFade) comes out near 0 instead of 1 - the
            // masking grace period that's supposed to hide a freshly (re)assigned slot's
            // transient state never runs, and the real (possibly still-wrong) shadow sample goes
            // straight to screen with no fade-in.
            pipeline.mSpotLightFade[i] = 0.f;
        }

        if (pipeline.mTargetShadowSpotLight[i] == drawablep)
        {
            pipeline.mTargetShadowSpotLight[i] = NULL;
        }
    }
}

// static
U32 DXPipeline::addObject(LLPipeline& pipeline, LLViewerObject* vobj)
{
    if (DXPipeline::RenderDelayCreation)
    {
        pipeline.mCreateQ.push_back(vobj);
    }
    else
    {
        DXPipeline::createObject(pipeline, vobj);
    }

    return 1;
}

// static
void DXPipeline::createObjects(LLPipeline& pipeline, F32 max_dtime)
{
    LLTimer update_timer;

    while (!pipeline.mCreateQ.empty() && update_timer.getElapsedTimeF32() < max_dtime)
    {
        LLViewerObject* vobj = pipeline.mCreateQ.front();
        if (!vobj->isDead())
        {
            DXPipeline::createObject(pipeline, vobj);
        }
        pipeline.mCreateQ.pop_front();
    }
}

// static
void DXPipeline::createObject(LLPipeline& pipeline, LLViewerObject* vobj)
{
    LLDrawable* drawablep = vobj->mDrawable;

    if (!drawablep)
    {
        drawablep = vobj->createDrawable(&pipeline);
    }
    else
    {
        LL_ERRS() << "Redundant drawable creation!" << LL_ENDL;
    }

    llassert(drawablep);

    if (vobj->getParent())
    {
        vobj->setDrawableParent(((LLViewerObject*)vobj->getParent())->mDrawable); // LLPipeline::addObject 1
    }
    else
    {
        vobj->setDrawableParent(NULL); // LLPipeline::addObject 2
    }

    DXPipeline::markRebuild(pipeline, drawablep, LLDrawable::REBUILD_ALL);

    if (drawablep->getVOVolume() && DXPipeline::RenderAnimateRes)
    {
        // fun animated res
        drawablep->updateXform(true);
        drawablep->clearState(LLDrawable::MOVE_UNDAMPED);
        drawablep->setScale(LLVector3(0, 0, 0));
        drawablep->makeActive();
    }
}

// static
void DXPipeline::markShift(LLPipeline& pipeline, LLDrawable* drawablep)
{
    if (!drawablep || drawablep->isDead() || !drawablep->getVObj())
    {
        return;
    }

    pipeline.assertInitialized();

    if (!drawablep->isState(LLDrawable::ON_SHIFT_LIST))
    {
        drawablep->getVObj()->setChanged(LLXform::SHIFTED | LLXform::SILHOUETTE);
        if (drawablep->getParent())
        {
            DXPipeline::markShift(pipeline, drawablep->getParent());
        }
        pipeline.mShiftList.push_back(drawablep);
        drawablep->setState(LLDrawable::ON_SHIFT_LIST);
    }
}

// static
void DXPipeline::shiftObjects(LLPipeline& pipeline, const LLVector3& offset)
{
    pipeline.assertInitialized();

    // depthDirty is how DX_RENDER signals "don't trust the depth buffer" elsewhere too (see
    // llviewerdisplay.cpp) - the flag below already covers DX, so the GL clear
    // itself is GL-only, matching that established convention.
    gDepthDirty = true;

    LLVector4a offseta(0.f);
    offseta.load3(offset.mV);

    for (LLDrawable::drawable_vector_t::iterator iter = pipeline.mShiftList.begin();
        iter != pipeline.mShiftList.end(); iter++)
    {
        LLDrawable* drawablep = *iter;
        if (drawablep->isDead() || !drawablep->getVObj())
        {
            continue;
        }
        drawablep->shiftPos(offseta);
        drawablep->clearState(LLDrawable::ON_SHIFT_LIST);
    }
    pipeline.mShiftList.resize(0);

    for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
        iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
    {
        LLViewerRegion* region = *iter;
        for (U32 i = 0; i < LLViewerRegion::NUM_PARTITIONS; i++)
        {
            LLSpatialPartition* part = region->getSpatialPartition(i);
            if (part)
            {
                part->shift(offseta);
            }
        }
    }

    pipeline.mSphereProbes.shift(offseta);

    LLHUDText::shiftAll(offset);
    LLHUDNameTag::shiftAll(offset);

    display_update_camera();
}

// static
void DXPipeline::markTextured(LLPipeline& pipeline, LLDrawable* drawablep)
{
    if (drawablep && !drawablep->isDead() && pipeline.assertInitialized())
    {
        pipeline.mRetexturedList.insert(drawablep);
    }
}

// static
void DXPipeline::processPartitionQ(LLPipeline& pipeline)
{
    for (LLDrawable::drawable_list_t::iterator iter = pipeline.mPartitionQ.begin(); iter != pipeline.mPartitionQ.end(); ++iter)
    {
        LLDrawable* drawable = *iter;
        if (!drawable->isDead())
        {
            drawable->updateBinRadius();
            drawable->movePartition();
        }
        drawable->clearState(LLDrawable::PARTITION_MOVE);
    }

    pipeline.mPartitionQ.clear();
}

// static
void DXPipeline::setDeferredMarksForThisThread(LLDeferredPipelineMarks* marks)
{
    sTLSDeferredPipelineMarks = marks;
}

// static
void DXPipeline::markMoved(LLPipeline& pipeline, LLDrawable* drawablep, bool damped_motion)
{
    if (sTLSDeferredPipelineMarks)
    {
        LLPipeline* pPipeline = &pipeline;
        sTLSDeferredPipelineMarks->mActions.push_back([pPipeline, drawablep, damped_motion]{ DXPipeline::markMoved(*pPipeline, drawablep, damped_motion); });
        return;
    }

    if (!drawablep)
    {
        //LL_ERRS() << "Sending null drawable to moved list!" << LL_ENDL;
        return;
    }

    if (drawablep->isDead())
    {
        LL_WARNS() << "Marking NULL or dead drawable moved!" << LL_ENDL;
        return;
    }

    if (drawablep->getParent())
    {
        //ensure that parent drawables are moved first
        DXPipeline::markMoved(pipeline, drawablep->getParent(), damped_motion);
    }

    pipeline.assertInitialized();

    if (!drawablep->isState(LLDrawable::ON_MOVE_LIST))
    {
        if (drawablep->isSpatialBridge())
        {
            pipeline.mMovedBridge.push_back(drawablep);
        }
        else
        {
            pipeline.mMovedList.push_back(drawablep);
        }
        drawablep->setState(LLDrawable::ON_MOVE_LIST);
    }
    if (!damped_motion)
    {
        drawablep->setState(LLDrawable::MOVE_UNDAMPED); // UNDAMPED trumps DAMPED
    }
    else if (drawablep->isState(LLDrawable::MOVE_UNDAMPED))
    {
        drawablep->clearState(LLDrawable::MOVE_UNDAMPED);
    }
}

// static
void DXPipeline::markGLRebuild(LLPipeline& pipeline, LLGLUpdate* glu)
{
    if (sTLSDeferredPipelineMarks)
    {
        LLPipeline* pPipeline = &pipeline;
        sTLSDeferredPipelineMarks->mActions.push_back([pPipeline, glu]{ DXPipeline::markGLRebuild(*pPipeline, glu); });
        return;
    }

    if (glu && !glu->mInQ)
    {
        LLGLUpdate::sGLQ.push_back(glu);
        glu->mInQ = true;
    }
}

// static
void DXPipeline::markPartitionMove(LLPipeline& pipeline, LLDrawable* drawable)
{
    if (sTLSDeferredPipelineMarks)
    {
        LLPipeline* pPipeline = &pipeline;
        sTLSDeferredPipelineMarks->mActions.push_back([pPipeline, drawable]{ DXPipeline::markPartitionMove(*pPipeline, drawable); });
        return;
    }

    if (!drawable->isState(LLDrawable::PARTITION_MOVE) &&
        !drawable->getPositionGroup().equals3(LLVector4a::getZero()))
    {
        drawable->setState(LLDrawable::PARTITION_MOVE);
        pipeline.mPartitionQ.push_back(drawable);
    }
}

// static
void DXPipeline::markMeshDirty(LLPipeline& pipeline, LLSpatialGroup* group)
{
    if (sTLSDeferredPipelineMarks)
    {
        LLPipeline* pPipeline = &pipeline;
        sTLSDeferredPipelineMarks->mActions.push_back([pPipeline, group]{ DXPipeline::markMeshDirty(*pPipeline, group); });
        return;
    }

    pipeline.mMeshDirtyGroup.push_back(group);
}

// static
void DXPipeline::markRebuild(LLPipeline& pipeline, LLSpatialGroup* group)
{
    if (sTLSDeferredPipelineMarks)
    {
        LLPipeline* pPipeline = &pipeline;
        sTLSDeferredPipelineMarks->mActions.push_back([pPipeline, group]{ DXPipeline::markRebuild(*pPipeline, group); });
        return;
    }

    if (group && !group->isDead() && group->getSpatialPartition())
    {
        if (!group->hasState(LLSpatialGroup::IN_BUILD_Q1))
        {
            llassert_always(!pipeline.mGroupQ1Locked);

            pipeline.mGroupQ1.push_back(group);
            group->setState(LLSpatialGroup::IN_BUILD_Q1);
        }
    }
}

// static
void DXPipeline::markRebuild(LLPipeline& pipeline, LLDrawable* drawablep, LLDrawable::EDrawableFlags flag)
{
    if (sTLSDeferredPipelineMarks)
    {
        LLPipeline* pPipeline = &pipeline;
        sTLSDeferredPipelineMarks->mActions.push_back([pPipeline, drawablep, flag]{ DXPipeline::markRebuild(*pPipeline, drawablep, flag); });
        return;
    }

    if (drawablep && !drawablep->isDead() && pipeline.assertInitialized())
    {
        if (!drawablep->isState(LLDrawable::IN_REBUILD_Q))
        {
            pipeline.mBuildQ1.push_back(drawablep);
            drawablep->setState(LLDrawable::IN_REBUILD_Q); // mark drawable as being in priority queue
        }

        if (flag & (LLDrawable::REBUILD_VOLUME | LLDrawable::REBUILD_POSITION))
        {
            drawablep->getVObj()->setChanged(LLXform::SILHOUETTE);
        }
        drawablep->setState(flag);
    }
}

// ============================================================================
// Atmospherics / water
// ============================================================================

// static
void DXPipeline::doAtmospherics(LLPipeline& pipeline)
{
    if (DXPipeline::sImpostorRender)
    { // do not attempt atmospherics on impostors
        return;
    }

    if (DXPipeline::RenderDeferredAtmospheric)
    {
        {
            // copy depth buffer for use in haze shader (use water displacement map as temp storage)
            DXDepthTest depth(true, true, GL_ALWAYS);

            LLRenderTarget& src = pipeline.mRT->screen;
            LLRenderTarget& depth_src = pipeline.mRT->deferredScreen;
            LLRenderTarget& dst = pipeline.mWaterDis;

            pipeline.mRT->screen.flush();
            dst.bindTarget();
            gCopyDepthProgram.bind();

            S32 diff_map = gCopyDepthProgram.getTextureChannel(LLShaderMgr::DIFFUSE_MAP);
            S32 depth_map = gCopyDepthProgram.getTextureChannel(LLShaderMgr::DEFERRED_DEPTH);

            gDX.getTexUnit(diff_map)->bind(&src);
            gDX.getTexUnit(depth_map)->bind(&depth_src, true);

            gDX.setColorWriteMask(false, false);
            pipeline.mScreenTriangleVB->setBuffer();
            pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

            dst.flush();
            pipeline.mRT->screen.bindTarget();
        }

        LLGLEnable blend(GL_BLEND);
        gDX.blendFunc(LLRender::BF_ONE, LLRender::BF_SOURCE_ALPHA, LLRender::BF_ZERO, LLRender::BF_SOURCE_ALPHA);
        gDX.setColorWriteMask(true, true);

        // apply haze
        LLHLSLShader& haze_shader = gHazeProgram;

        DXPipeline::bindDeferredShader(pipeline, haze_shader, nullptr, &pipeline.mWaterDis);

        LLEnvironment& environment = LLEnvironment::instance();
        haze_shader.uniform1i(LLShaderMgr::SUN_UP_FACTOR, environment.getIsSunUp() ? 1 : 0);
        haze_shader.uniform3fv(LLShaderMgr::LIGHTNORM, 1, environment.getClampedLightNorm().mV);

        haze_shader.uniform4fv(LLShaderMgr::WATER_WATERPLANE, 1, DXDrawPoolAlpha::sWaterPlane.mV);

        DXDepthTest depth(false);

        // full screen blit
        pipeline.mScreenTriangleVB->setBuffer();
        pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

        DXPipeline::unbindDeferredShader(pipeline, haze_shader);

        gDX.setSceneBlendType(LLRender::BT_ALPHA);
    }
}

// static
void DXPipeline::doWaterHaze(LLPipeline& pipeline)
{
    if (DXPipeline::sImpostorRender)
    { // do not attempt water haze on impostors
        return;
    }

    if (DXPipeline::RenderDeferredAtmospheric)
    {
        // copy depth buffer for use in haze shader (use water displacement map as temp storage)
        {
            DXDepthTest depth(true, true, GL_ALWAYS);

            LLRenderTarget& src = pipeline.mRT->screen;
            LLRenderTarget& depth_src = pipeline.mRT->deferredScreen;
            LLRenderTarget& dst = pipeline.mWaterDis;

            pipeline.mRT->screen.flush();
            dst.bindTarget();
            gCopyDepthProgram.bind();

            S32 diff_map = gCopyDepthProgram.getTextureChannel(LLShaderMgr::DIFFUSE_MAP);
            S32 depth_map = gCopyDepthProgram.getTextureChannel(LLShaderMgr::DEFERRED_DEPTH);

            gDX.getTexUnit(diff_map)->bind(&src);
            gDX.getTexUnit(depth_map)->bind(&depth_src, true);

            gDX.setColorWriteMask(false, false);
            pipeline.mScreenTriangleVB->setBuffer();
            pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

            dst.flush();
            pipeline.mRT->screen.bindTarget();
        }

        LLGLEnable blend(GL_BLEND);
        gDX.blendFunc(LLRender::BF_ONE, LLRender::BF_SOURCE_ALPHA, LLRender::BF_ZERO, LLRender::BF_SOURCE_ALPHA);

        gDX.setColorWriteMask(true, true);

        // apply haze
        LLHLSLShader& haze_shader = gHazeWaterProgram;

        DXPipeline::bindDeferredShader(pipeline, haze_shader, nullptr, &pipeline.mWaterDis);

        haze_shader.uniform4fv(LLShaderMgr::WATER_WATERPLANE, 1, DXDrawPoolAlpha::sWaterPlane.mV);

        static LLStaticHashedString above_water_str("above_water");
        haze_shader.uniform1i(above_water_str, DXPipeline::sUnderWaterRender ? -1 : 1);

        haze_shader.bindTexture(LLShaderMgr::WATER_EXCLUSIONTEX, &pipeline.mWaterExclusionMask);

        if (DXPipeline::sUnderWaterRender)
        {
            DXDepthTest depth(false);

            // full screen blit
            pipeline.mScreenTriangleVB->setBuffer();
            pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
        }
        else
        {
            //render water patches like LLDrawPoolWater does
            DXDepthTest depth(true, false);
            LLGLDisable   cull(GL_CULL_FACE);

            gDXLastMatrix = NULL;
            gDX.loadMatrix(gDXModelView);

            if (pipeline.mWaterPool)
            {
                pipeline.mWaterPool->pushFaceGeometry();
            }
        }

        // Must run for both branches, not just the above-water `else`: the underwater branch used to
        // return with haze_shader's textures left bound and colorMask still at (true,true) instead of
        // the (true,false) the caller set up. Since the alpha pool draws immediately after this
        // function returns, a left-enabled alpha-write channel lets alpha-blended content corrupt
        // screen_target's alpha channel, which this pipeline uses to carry glow, not just coverage.
        DXPipeline::unbindDeferredShader(pipeline, haze_shader);

        gDX.setSceneBlendType(LLRender::BT_ALPHA);
        gDX.setColorWriteMask(true, false);
    }
}

// static
void DXPipeline::doWaterExclusionMask(LLPipeline& pipeline)
{
    pipeline.mWaterExclusionMask.bindTarget();
    // LLRenderTarget::clear() under DX_RENDER always clears to transparent black regardless of the
    // GL-only glClearColor() call below, but this mask's meaning is inverted: white (1,1,1,1) = "not
    // excluded, water visible here". Use clearColor() (an explicit ClearRenderTargetView()) instead, or
    // every pixel not covered by mWaterExclusionPool->render() defaults to "excluded".
    pipeline.mWaterExclusionMask.clearColor(1, 1, 1, 1);
    pipeline.mWaterExclusionPool->render();

    pipeline.mWaterExclusionMask.flush();
}

// ============================================================================
// Cached settings
// ============================================================================

// static
void DXPipeline::refreshCachedSettings()
{
    DXPipeline::sAutoMaskAlphaDeferred = gSavedSettings.getBOOL("RenderAutoMaskAlphaDeferred");
    DXPipeline::sAutoMaskAlphaNonDeferred = gSavedSettings.getBOOL("RenderAutoMaskAlphaNonDeferred");
    DXPipeline::sUseFarClip = gSavedSettings.getBOOL("RenderUseFarClip");
    LLVOAvatar::sMaxNonImpostors = gSavedSettings.getU32("RenderAvatarMaxNonImpostors");
    LLVOAvatar::updateImpostorRendering(LLVOAvatar::sMaxNonImpostors);

    DXPipeline::sUseOcclusion =
        (!gUseWireframe
            && LLFeatureManager::getInstance()->isFeatureAvailable("UseOcclusion")
            && gSavedSettings.getBOOL("UseOcclusion")) ? 2 : 0;

    DXPipeline::RenderFSAAType = gSavedSettings.getU32("RenderFSAAType");
    DXPipeline::RenderUIBuffer = gSavedSettings.getBOOL("RenderUIBuffer");
    DXPipeline::RenderShadowDetail = gSavedSettings.getS32("RenderShadowDetail");
    DXPipeline::RenderDeferredSSAO = gSavedSettings.getBOOL("RenderDeferredSSAO");
    DXPipeline::RenderShadowResolutionScale = gSavedSettings.getF32("RenderShadowResolutionScale");
    DXPipeline::FreezeTime = gSavedSettings.getBOOL("FreezeTime");
    DXPipeline::DebugBeaconLineWidth = gSavedSettings.getS32("DebugBeaconLineWidth");
    DXPipeline::RenderDepthOfField = gSavedSettings.getBOOL("RenderDepthOfField");
    DXPipeline::RenderDepthOfFieldInEditMode = gSavedSettings.getBOOL("RenderDepthOfFieldInEditMode");
    DXPipeline::CameraFocusTransitionTime = gSavedSettings.getF32("CameraFocusTransitionTime");
    DXPipeline::CameraFNumber = gSavedSettings.getF32("CameraFNumber");
    DXPipeline::CameraFocalLength = gSavedSettings.getF32("CameraFocalLength");
    DXPipeline::CameraFieldOfView = gSavedSettings.getF32("CameraFieldOfView");
    DXPipeline::RenderEdgeDepthCutoff = gSavedSettings.getF32("RenderEdgeDepthCutoff");
    DXPipeline::RenderEdgeNormCutoff = gSavedSettings.getF32("RenderEdgeNormCutoff");
    DXPipeline::CameraMaxCoF = gSavedSettings.getF32("CameraMaxCoF");
    DXPipeline::CameraDoFResScale = gSavedSettings.getF32("CameraDoFResScale");
    DXPipeline::CameraAspectRatio = gSavedSettings.getF32("CameraAspectRatio");
    DXPipeline::RenderAutoHideSurfaceAreaLimit = gSavedSettings.getF32("RenderAutoHideSurfaceAreaLimit");
    DXPipeline::RenderScreenSpaceReflections = gSavedSettings.getBOOL("RenderScreenSpaceReflections");
    DXPipeline::RenderMirrors = gSavedSettings.getBOOL("RenderMirrors");

    DXPipeline::sReflectionProbesEnabled = LLFeatureManager::getInstance()->isFeatureAvailable("RenderReflectionsEnabled") && gSavedSettings.getBOOL("RenderReflectionsEnabled");
    DXPipeline::RenderSpotLight = nullptr;

    if (gNonInteractive)
    {
        LLVOAvatar::sMaxNonImpostors = 1;
        LLVOAvatar::updateImpostorRendering(LLVOAvatar::sMaxNonImpostors);
    }

    {
        bool enable_buffers = gSavedSettings.getBOOL("CollectFontVertexBuffers");
        LLFontVertexBuffer::enableBufferCollection(enable_buffers);
        LLFontWidthBuffer::enableBufferCollection(enable_buffers);
        enable_buffers = gSavedSettings.getBOOL("CollectUIImageVertexBuffers");
        LLUIImage::enableDisplayListsCollection(enable_buffers);
    }

    RenderDeferredSunWash = gSavedSettings.getF32("RenderDeferredSunWash");
    RenderShadowSplits = gSavedSettings.getS32("RenderShadowSplits");
    RenderDelayCreation = gSavedSettings.getBOOL("RenderDelayCreation");
    RenderAnimateRes = gSavedSettings.getBOOL("RenderAnimateRes");
    RenderSpotLightsInNondeferred = gSavedSettings.getBOOL("RenderSpotLightsInNondeferred");
    PreviewAmbientColor = gSavedSettings.getColor4("PreviewAmbientColor");
    PreviewDiffuse0 = gSavedSettings.getColor4("PreviewDiffuse0");
    PreviewSpecular0 = gSavedSettings.getColor4("PreviewSpecular0");
    PreviewDiffuse1 = gSavedSettings.getColor4("PreviewDiffuse1");
    PreviewSpecular1 = gSavedSettings.getColor4("PreviewSpecular1");
    PreviewDiffuse2 = gSavedSettings.getColor4("PreviewDiffuse2");
    PreviewSpecular2 = gSavedSettings.getColor4("PreviewSpecular2");
    PreviewDirection0 = gSavedSettings.getVector3("PreviewDirection0");
    PreviewDirection1 = gSavedSettings.getVector3("PreviewDirection1");
    PreviewDirection2 = gSavedSettings.getVector3("PreviewDirection2");
    RenderGlowMaxExtractAlpha = gSavedSettings.getF32("RenderGlowMaxExtractAlpha");
    RenderGlowWarmthAmount = gSavedSettings.getF32("RenderGlowWarmthAmount");
    RenderGlowLumWeights = gSavedSettings.getVector3("RenderGlowLumWeights");
    RenderGlowWarmthWeights = gSavedSettings.getVector3("RenderGlowWarmthWeights");
    RenderGlowResolutionPow = gSavedSettings.getS32("RenderGlowResolutionPow");
    RenderGlowIterations = gSavedSettings.getS32("RenderGlowIterations");
    RenderGlowWidth = gSavedSettings.getF32("RenderGlowWidth");
    RenderGlowStrength = gSavedSettings.getF32("RenderGlowStrength");
    RenderGlowNoise = gSavedSettings.getBOOL("RenderGlowNoise");
    RenderShadowNoise = gSavedSettings.getF32("RenderShadowNoise");
    RenderShadowBlurSize = gSavedSettings.getF32("RenderShadowBlurSize");
    RenderSSAOScale = gSavedSettings.getF32("RenderSSAOScale");
    RenderSSAOMaxScale = gSavedSettings.getU32("RenderSSAOMaxScale");
    RenderSSAOFactor = gSavedSettings.getF32("RenderSSAOFactor");
    RenderSSAOEffect = gSavedSettings.getVector3("RenderSSAOEffect");
    RenderShadowBiasError = gSavedSettings.getF32("RenderShadowBiasError");
    RenderShadowOffset = gSavedSettings.getF32("RenderShadowOffset");
    RenderShadowBias = gSavedSettings.getF32("RenderShadowBias");
    RenderSpotShadowOffset = gSavedSettings.getF32("RenderSpotShadowOffset");
    RenderSpotShadowBias = gSavedSettings.getF32("RenderSpotShadowBias");
    RenderShadowGaussian = gSavedSettings.getVector3("RenderShadowGaussian");
    RenderShadowBlurDistFactor = gSavedSettings.getF32("RenderShadowBlurDistFactor");
    RenderDeferredAtmospheric = gSavedSettings.getBOOL("RenderDeferredAtmospheric");
    RenderFarClip = gSavedSettings.getF32("RenderFarClip");
    RenderShadowSplitExponent = gSavedSettings.getVector3("RenderShadowSplitExponent");
    RenderShadowErrorCutoff = gSavedSettings.getF32("RenderShadowErrorCutoff");
    RenderShadowFOVCutoff = gSavedSettings.getF32("RenderShadowFOVCutoff");
    CameraOffset = gSavedSettings.getBOOL("CameraOffset");
    RenderScreenSpaceReflectionIterations = gSavedSettings.getS32("RenderScreenSpaceReflectionIterations");
    RenderScreenSpaceReflectionRayStep = gSavedSettings.getF32("RenderScreenSpaceReflectionRayStep");
    RenderScreenSpaceReflectionDistanceBias = gSavedSettings.getF32("RenderScreenSpaceReflectionDistanceBias");
    RenderScreenSpaceReflectionDepthRejectBias = gSavedSettings.getF32("RenderScreenSpaceReflectionDepthRejectBias");
    RenderScreenSpaceReflectionAdaptiveStepMultiplier = gSavedSettings.getF32("RenderScreenSpaceReflectionAdaptiveStepMultiplier");
    RenderScreenSpaceReflectionGlossySamples = gSavedSettings.getS32("RenderScreenSpaceReflectionGlossySamples");
    RenderScreenSpaceReflectionGlossThreshold = gSavedSettings.getF32("RenderScreenSpaceReflectionGlossThreshold");
    RenderWaterSSRIterations = gSavedSettings.getS32("RenderWaterSSRIterations");
    RenderWaterSSRRayStep = gSavedSettings.getF32("RenderWaterSSRRayStep");
    RenderBufferVisualization = gSavedSettings.getS32("RenderBufferVisualization");
    RenderAvatarCloth = gSavedSettings.getBOOL("RenderAvatarCloth");
}

// See pipeline.h's declaration comment - single source of truth for whether the shader takes the
// legacy/environmentMap fallback branch, so LLVOSky::updateSky() and bindDeferredShader() agree with
// the shader's own fallback logic instead of only checking sReflectionProbesEnabled. Live read (not
// cached) - called at most a few times per frame, not worth a settings-listener.

// ============================================================================
// Per-frame cull-result utilities and small helpers
// ============================================================================

// static
bool DXPipeline::shouldUseLegacyEnvMap()
{
    static LLCachedControl<bool> probes_enabled(gSavedSettings, "RenderReflectionProbesEnabled", true);
    return !DXPipeline::sReflectionProbesEnabled || !probes_enabled;
}

// static
bool DXPipeline::isWaterClip()
{
    // We always pretend that we're not clipping water when rendering mirrors.
    // Probe captures are not clipped at the waterline, so the seabed is captured and terrain joins up.
    return (gPipeline.mMirrorProbes.isMirrorPass() || gCubeSnapshot) ? false : (!DXPipeline::sRenderTransparentWater) && !DXPipeline::sRenderingHUDs;
}

// S24 double precision version of calcPixelArea, to avoid overflow for large distances
// static
F32 DXPipeline::calcPixelArea(LLVector3 center, LLVector3 size, LLCamera& camera)
{
    llassert(!gCubeSnapshot); // shouldn't be doing ANY of this during cube snap shots
    LLVector3 lookAt = center - camera.getOrigin();
    F32 dist = lookAt.length();

    //ramp down distance for nearby objects
    //shrink dist by dist/16.
    if (dist < 16.f)
    {
        dist /= 16.f;
        dist *= dist;
        dist *= 16.f;
    }

    //get area of circle around node
    F32 app_angle = atanf(size.length() / dist);
    F32 radius = app_angle * LLDrawable::sCurPixelAngle;
    return radius * radius * F_PI;
}

// S24 double precision version of calcPixelArea, to avoid overflow for large distances
// static
F32 DXPipeline::calcPixelArea(const LLVector4a& center, const LLVector4a& size, LLCamera& camera)
{
    LLVector4a origin(0.f);
    origin.load3(camera.getOrigin().mV);

    LLVector4a lookAt(0.f);
    lookAt.setSub(center, origin);
    F32 dist = lookAt.getLength3().getF32();

    //ramp down distance for nearby objects
    //shrink dist by dist/16.
    if (dist < 16.f)
    {
        dist /= 16.f;
        dist *= dist;
        dist *= 16.f;
    }

    //get area of circle around node
    F32 app_angle = atanf(size.getLength3().getF32() / dist);
    F32 radius = app_angle * LLDrawable::sCurPixelAngle;
    return radius * radius * F_PI;
}

// static
LLSpatialPartition* DXPipeline::getSpatialPartition(LLPipeline& pipeline, LLViewerObject* vobj)
{
    if (vobj)
    {
        LLViewerRegion* region = vobj->getRegion();
        if (region)
        {
            return region->getSpatialPartition(vobj->getPartitionType());
        }
    }
    return NULL;
}

// static
void DXPipeline::resetVertexBuffers(LLPipeline& pipeline, LLDrawable* drawable)
{
    if (!drawable)
    {
        return;
    }

    for (S32 i = 0; i < drawable->getNumFaces(); i++)
    {
        LLFace* facep = drawable->getFace(i);
        if (facep)
        {
            facep->clearVertexBuffer();
        }
    }
}

// static
void DXPipeline::addDebugBlip(LLPipeline& pipeline, const LLVector3& position, const LLColor4& color)
{
    LLPipeline::DebugBlip blip(position, color);
    pipeline.mDebugBlips.push_back(blip);
}

// static
void DXPipeline::addTrianglesDrawn(LLPipeline& pipeline, S32 index_count)
{
    sIndicesDrawnCount += index_count;
}

// static
void DXPipeline::recordTrianglesDrawn(LLPipeline& pipeline)
{
    pipeline.assertInitialized();
    U32 count = sIndicesDrawnCount / 3;
    sIndicesDrawnCount = 0;
    add(LLStatViewer::TRIANGLES_DRAWN, LLUnits::Triangles::fromValue(count));
}

// static
LLRenderTarget* DXPipeline::getSunShadowTarget(LLPipeline& pipeline, U32 i)
{
    llassert(i < 4);
    return &pipeline.mRT->shadow[i];
}

// static
LLRenderTarget* DXPipeline::getSpotShadowTarget(LLPipeline& pipeline, U32 i)
{
    llassert(i < 2);
    return &pipeline.mSpotShadow[i];
}

// static
void DXPipeline::grabReferences(LLPipeline& pipeline, LLCullResult& result)
{
    sCull = &result;
}

// static
void DXPipeline::clearReferences(LLPipeline& pipeline)
{
    sCull = NULL;
    pipeline.mGroupSaveQ1.clear();
}

// static
LLCullResult::drawinfo_iterator DXPipeline::beginRenderMap(LLPipeline& pipeline, U32 type)
{
    return sCull->beginRenderMap(type);
}

// static
LLCullResult::drawinfo_iterator DXPipeline::endRenderMap(LLPipeline& pipeline, U32 type)
{
    return sCull->endRenderMap(type);
}

// static
LLCullResult::sg_iterator DXPipeline::beginAlphaGroups(LLPipeline& pipeline)
{
    return sCull->beginAlphaGroups();
}

// static
LLCullResult::sg_iterator DXPipeline::endAlphaGroups(LLPipeline& pipeline)
{
    return sCull->endAlphaGroups();
}

// static
LLCullResult::sg_iterator DXPipeline::beginRiggedAlphaGroups(LLPipeline& pipeline)
{
    return sCull->beginRiggedAlphaGroups();
}

// static
LLCullResult::sg_iterator DXPipeline::endRiggedAlphaGroups(LLPipeline& pipeline)
{
    return sCull->endRiggedAlphaGroups();
}

// Used only by markAllGeometryDirty() below.
class LLOctreeDirtyGeometry : public OctreeTraveler
{
public:
    void visit(const OctreeNode* node) override
    {
        LLSpatialGroup* group = (LLSpatialGroup*)node->getListener(0);
        if (!group->isEmpty())
        {
            // Avoid marking HUD groups dirty here. HUD geometry is rebuilt
            // through dedicated code paths and marking it dirty here can
            // cause HUD textures to be re-sorted/rebuilt unexpectedly
            if (!group->isHUDGroup())
            {
                group->setState(LLSpatialGroup::GEOM_DIRTY);
            }
        }

        for (LLSpatialGroup::bridge_list_t::iterator i = group->mBridgeList.begin(); i != group->mBridgeList.end(); ++i)
        {
            LLSpatialBridge* bridge = *i;
            traverse(bridge->mOctree);
        }
    }
};

// static
void DXPipeline::markAllGeometryDirty(LLPipeline& pipeline)
{
    pipeline.assertInitialized();

    LLOctreeDirtyGeometry dirty;
    for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
        iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
    {
        LLViewerRegion* region = *iter;
        for (U32 i = 0; i < LLViewerRegion::NUM_PARTITIONS; i++)
        {
            LLSpatialPartition* part = region->getSpatialPartition(i);
            if (part)
            {
                dirty.traverse(part->mOctree);
            }
        }
    }
}

// static
void DXPipeline::markOccluder(LLPipeline& pipeline, LLSpatialGroup* group)
{
    if (DXPipeline::sUseOcclusion > 1 && group && !group->isOcclusionState(LLSpatialGroup::ACTIVE_OCCLUSION))
    {
        LLSpatialGroup* parent = group->getParent();

        if (!parent || !parent->isOcclusionState(LLSpatialGroup::OCCLUDED))
        { //only mark top most occluders as active occlusion
            sCull->pushOcclusionGroup(group);
            group->setOcclusionState(LLSpatialGroup::ACTIVE_OCCLUSION);

            if (parent &&
                !parent->isOcclusionState(LLSpatialGroup::ACTIVE_OCCLUSION) &&
                parent->getElementCount() == 0 &&
                parent->needsUpdate())
            {
                sCull->pushOcclusionGroup(group);
                parent->setOcclusionState(LLSpatialGroup::ACTIVE_OCCLUSION);
            }
        }
    }
}

// static
void DXPipeline::markNotCulled(LLPipeline& pipeline, LLSpatialGroup* group, LLCamera& camera)
{
    if (group->isEmpty())
    {
        return;
    }

    group->setVisible();

    if (LLViewerCamera::sCurCameraID == LLViewerCamera::CAMERA_WORLD && !gCubeSnapshot)
    {
        group->updateDistance(camera);
    }

    pipeline.assertInitialized();

    if (!group->getSpatialPartition()->mRenderByGroup)
    { //render by drawable
        sCull->pushDrawableGroup(group);
    }
    else
    {   //render by group
        sCull->pushVisibleGroup(group);
    }

    if (group->needsUpdate() ||
        group->getVisible(LLViewerCamera::sCurCameraID) < LLDrawable::getCurrentFrame() - 1)
    {
        // include this group in occlusion groups, not because it is an occluder, but because we want to run
        // an occlusion query to find out if it's an occluder
        DXPipeline::markOccluder(pipeline, group);
    }
    pipeline.mNumVisibleNodes++;
}

// ============================================================================
// Culling, occlusion, and the update/rebuild queues
// ============================================================================

// static
void DXPipeline::markVisible(LLPipeline& pipeline, LLDrawable* drawablep, LLCamera& camera)
{
    if (drawablep && !drawablep->isDead())
    {
        // A probe's own linked set is not drawn into that probe's capture.
        LLViewerObject* vobj = drawablep->getVObj().get();
        if (DXPipeline::sCaptureExcludeObject && vobj && vobj->getRootEdit() == DXPipeline::sCaptureExcludeObject)
        {
            return;
        }

        if (drawablep->isSpatialBridge())
        {
            const LLDrawable* root = ((LLSpatialBridge*)drawablep)->mDrawable;
            llassert(root); // trying to catch a bad assumption

            if (root && //  // this test may not be needed, see above
                root->getVObj()->isAttachment())
            {
                LLDrawable* rootparent = root->getParent();
                if (rootparent) // this IS sometimes NULL
                {
                    LLViewerObject* vobj = rootparent->getVObj();
                    llassert(vobj); // trying to catch a bad assumption
                    if (vobj) // this test may not be needed, see above
                    {
                        LLVOAvatar* av = vobj->asAvatar();
                        if (av &&
                            ((!DXPipeline::sImpostorRender && av->isImpostor()) //ignore impostor flag during impostor pass
                                || av->isInMuteList()
                                || (LLVOAvatar::AOA_JELLYDOLL == av->getOverallAppearance() && !av->needsImpostorUpdate())))
                        {
                            return;
                        }
                    }
                }
            }
            sCull->pushBridge((LLSpatialBridge*)drawablep);
        }
        else
        {
            sCull->pushDrawable(drawablep);
        }

        drawablep->setVisible(camera);
    }
}

// static
void DXPipeline::renderGeomShadow(LLPipeline& pipeline, LLCamera& camera)
{
    LLGLEnable cull(GL_CULL_FACE);
    LLVertexBuffer::unbind();

    U32 cur_type = 0;

    for (auto iter1 = pipeline.mPools.begin(); iter1 != pipeline.mPools.end(); )
    {
        LLDrawPool* poolp = *iter1;
        cur_type = poolp->getType();

        // Find the end of this pool-type run
        auto iter2 = iter1;
        while (iter2 != pipeline.mPools.end() && (*iter2)->getType() == cur_type)
        {
            ++iter2;
        }

        const bool do_render =
            DXPipeline::hasRenderType(pipeline, cur_type) &&
            poolp->getNumShadowPasses() > 0;

        if (do_render)
        {
            poolp->prerender();

            gDXLastMatrix = nullptr;
            gDX.loadMatrix(gDXModelView);

            const S32 passes = poolp->getNumShadowPasses();
            for (S32 pass = 0; pass < passes; ++pass)
            {
                LLVertexBuffer::unbind();
                poolp->beginShadowPass(pass);

                for (auto it = iter1; it != iter2; ++it)
                {
                    (*it)->renderShadow(pass);
                }

                poolp->endShadowPass(pass);
                LLVertexBuffer::unbind();
            }
        }

        iter1 = iter2;
    }

    gDXLastMatrix = nullptr;
    gDX.loadMatrix(gDXModelView);
}

// static
void DXPipeline::doOcclusion(LLPipeline& pipeline, LLCamera& camera)
{
    // S24: Do not use 3D here!
    llassert(!gCubeSnapshot);

    // Unified probe occlusion pass: reflection + hero probes
    if (DXPipeline::sReflectionProbesEnabled && DXPipeline::sUseOcclusion > 1 && !DXPipeline::sShadowRender && !gCubeSnapshot)
    {
        gDX.setColorWriteMask(false, false);
        DXDepthTest depth(true, false);
        LLGLDisable cull(GL_CULL_FACE);

        gOcclusionCubeProgram.bind();

        if (pipeline.mCubeVB.isNull())
        { //cube VB will be used for issuing occlusion queries
            pipeline.mCubeVB = ll_create_cube_vb(LLVertexBuffer::MAP_VERTEX);
        }
        pipeline.mCubeVB->setBuffer();

        pipeline.mSphereProbes.doOcclusion();
        pipeline.mMirrorProbes.doOcclusion(); // Single invocation

        gOcclusionCubeProgram.unbind();

        // Colormask no longer tracks which stereo eye is rendering - see
        // DXPipeline::presentStereoComposite()'s comment for the real design.
        gDX.setColorWriteMask(true, true);
    }

    if (DXPipeline::sUseOcclusion > 1 &&
        (sCull->hasOcclusionGroups() || LLVOCachePartition::sNeedsOcclusionCheck))
    {
        LLVertexBuffer::unbind();

        gDX.setColorWriteMask(false, false);

        LLGLDisable blend(GL_BLEND);
        gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
        DXDepthTest depth(true, false);

        LLGLDisable cull(GL_CULL_FACE);

        gOcclusionCubeProgram.bind();

        if (pipeline.mCubeVB.isNull())
        {
            pipeline.mCubeVB = ll_create_cube_vb(LLVertexBuffer::MAP_VERTEX);
        }
        pipeline.mCubeVB->setBuffer();

        for (LLCullResult::sg_iterator iter = sCull->beginOcclusionGroups(); iter != sCull->endOcclusionGroups(); ++iter)
        {
            LLSpatialGroup* group = *iter;
            if (!group->isDead())
            {
                group->doOcclusion(&camera);
                group->clearOcclusionState(LLSpatialGroup::ACTIVE_OCCLUSION);
            }
        }

        // Apply occlusion culling to object cache tree
        for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
            iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
        {
            LLVOCachePartition* vo_part = (*iter)->getVOCachePartition();
            if (vo_part)
            {
                vo_part->processOccluders(&camera);
            }
        }

        // See the matching comment above.
        gDX.setColorWriteMask(true, true);
    }

    // Avatar nametag occlusion-fade piggybacks on this function's existing gOcclusionCubeProgram/mCubeVB
    // GPU occlusion-query setup rather than a real depth-buffer test - wiring a depth test through the
    // UI/HUD draw path made the nametag panel vanish entirely, so this is deliberately a softer signal
    // that fades nametags instead of gating their draw. Gated on the real-camera pass only (not
    // shadow-camera/cubemap-snapshot), unrelated to the spatial-group block's hasOcclusionGroups().
    if (DXPipeline::sUseOcclusion > 1 && !DXPipeline::sShadowRender && !gCubeSnapshot)
    {
        gDX.setColorWriteMask(false, false);
        DXDepthTest depth(true, false);
        LLGLDisable cull(GL_CULL_FACE);

        gOcclusionCubeProgram.bind();

        if (pipeline.mCubeVB.isNull())
        { //cube VB will be used for issuing occlusion queries
            pipeline.mCubeVB = ll_create_cube_vb(LLVertexBuffer::MAP_VERTEX);
        }
        pipeline.mCubeVB->setBuffer();

        LLHUDNameTag::issueOcclusionQueries();
        // Same mechanism, generic LLHUDObject subtypes (currently just LLVoiceVisualizer's voice-speaking
        // indicator) - see llhudobject.h's issueOcclusionQueries().
        LLHUDObject::issueOcclusionQueries();

        gOcclusionCubeProgram.unbind();

        // See the matching comment above.
        gDX.setColorWriteMask(true, true);
    }
}

// static
void DXPipeline::updateMovedList(LLPipeline& pipeline, LLDrawable::drawable_vector_t& moved_list)
{
    for (LLDrawable::drawable_vector_t::iterator iter = moved_list.begin();
        iter != moved_list.end(); )
    {
        LLDrawable::drawable_vector_t::iterator curiter = iter++;
        LLDrawable* drawablep = *curiter;
        if (!drawablep)
        {
            iter = moved_list.erase(curiter);
            continue;
        }
        bool done = true;
        if (!drawablep->isDead() && (!drawablep->isState(LLDrawable::EARLY_MOVE)))
        {
            done = drawablep->updateMove();
        }
        drawablep->clearState(LLDrawable::EARLY_MOVE | LLDrawable::MOVE_UNDAMPED);
        if (done)
        {
            if (drawablep->isRoot() && !drawablep->isState(LLDrawable::ACTIVE))
            {
                drawablep->makeStatic();
            }
            drawablep->clearState(LLDrawable::ON_MOVE_LIST);
            if (drawablep->isState(LLDrawable::ANIMATED_CHILD))
            { //will likely not receive any future world matrix updates
                // -- this keeps attachments from getting stuck in space and falling off your avatar
                drawablep->clearState(LLDrawable::ANIMATED_CHILD);
                DXPipeline::markRebuild(pipeline, drawablep, LLDrawable::REBUILD_VOLUME);
                if (drawablep->getVObj())
                {
                    drawablep->getVObj()->dirtySpatialGroup();
                }
            }
            iter = moved_list.erase(curiter);
        }
    }
}

// static
void DXPipeline::updateMove(LLPipeline& pipeline)
{
    LL_RECORD_BLOCK_TIME(FTM_UPDATE_MOVE); // S24: Critical - called every frame for moving objects

    if (DXPipeline::FreezeTime)
    {
        return;
    }

    pipeline.assertInitialized();

    for (LLDrawable::drawable_set_t::iterator iter = pipeline.mRetexturedList.begin();
        iter != pipeline.mRetexturedList.end(); ++iter)
    {
        LLDrawable* drawablep = *iter;
        if (drawablep && !drawablep->isDead())
        {
            drawablep->updateTexture();
        }
    }
    pipeline.mRetexturedList.clear();

    DXPipeline::updateMovedList(pipeline, pipeline.mMovedList);

    //balance octrees
    for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
        iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
    {
        LLViewerRegion* region = *iter;
        for (U32 i = 0; i < LLViewerRegion::NUM_PARTITIONS; i++)
        {
            LLSpatialPartition* part = region->getSpatialPartition(i);
            if (part)
            {
                part->mOctree->balance();
            }
        }

        //balance the VO Cache tree
        LLVOCachePartition* vo_part = region->getVOCachePartition();
        if (vo_part)
        {
            vo_part->mOctree->balance();
        }
    }
}

// static
bool DXPipeline::updateDrawableGeom(LLPipeline& pipeline, LLDrawable* drawablep)
{
    bool update_complete = drawablep->updateGeometry();
    if (update_complete && pipeline.assertInitialized())
    {
        drawablep->setState(LLDrawable::BUILT);
    }
    return update_complete;
}

// static
void DXPipeline::updateGL(LLPipeline& pipeline)
{
    while (!LLGLUpdate::sGLQ.empty())
    {
        LLGLUpdate* glu = LLGLUpdate::sGLQ.front();
        glu->updateGL();
        glu->mInQ = false;
        LLGLUpdate::sGLQ.pop_front();
    }
}

// static
void DXPipeline::clearRebuildGroups(LLPipeline& pipeline)
{
    LLSpatialGroup::sg_vector_t hudGroups;

    pipeline.mGroupQ1Locked = true;
    // Iterate through all drawables on the priority build queue,
    for (LLSpatialGroup::sg_vector_t::iterator iter = pipeline.mGroupQ1.begin();
        iter != pipeline.mGroupQ1.end(); ++iter)
    {
        LLSpatialGroup* group = *iter;

        if (!group || group->isDead())
        {
            continue;
        }
        // If the group contains HUD objects, save the group
        if (group->isHUDGroup())
        {
            hudGroups.push_back(group);
        }
        // Else, no HUD objects so clear the build state
        else
        {
            group->clearState(LLSpatialGroup::IN_BUILD_Q1);
        }
    }

    // Clear the group
    pipeline.mGroupQ1.clear();

    // Copy the saved HUD groups back in
    pipeline.mGroupQ1.assign(hudGroups.begin(), hudGroups.end());
    pipeline.mGroupQ1Locked = false;
}

// static
void DXPipeline::clearRebuildDrawables(LLPipeline& pipeline)
{
    // Clear all drawables on the priority build queue,
    for (LLDrawable::drawable_list_t::iterator iter = pipeline.mBuildQ1.begin();
        iter != pipeline.mBuildQ1.end(); ++iter)
    {
        LLDrawable* drawablep = *iter;
        if (drawablep && !drawablep->isDead())
        {
            drawablep->clearState(LLDrawable::IN_REBUILD_Q);
        }
    }
    pipeline.mBuildQ1.clear();

    //clear all moving bridges
    for (LLDrawable::drawable_vector_t::iterator iter = pipeline.mMovedBridge.begin();
        iter != pipeline.mMovedBridge.end(); ++iter)
    {
        LLDrawable* drawablep = *iter;
        drawablep->clearState(LLDrawable::EARLY_MOVE | LLDrawable::MOVE_UNDAMPED | LLDrawable::ON_MOVE_LIST | LLDrawable::ANIMATED_CHILD);
    }
    pipeline.mMovedBridge.clear();

    //clear all moving drawables
    for (LLDrawable::drawable_vector_t::iterator iter = pipeline.mMovedList.begin();
        iter != pipeline.mMovedList.end(); ++iter)
    {
        LLDrawable* drawablep = *iter;
        drawablep->clearState(LLDrawable::EARLY_MOVE | LLDrawable::MOVE_UNDAMPED | LLDrawable::ON_MOVE_LIST | LLDrawable::ANIMATED_CHILD);
    }
    pipeline.mMovedList.clear();

    for (LLDrawable::drawable_vector_t::iterator iter = pipeline.mShiftList.begin();
        iter != pipeline.mShiftList.end(); ++iter)
    {
        LLDrawable* drawablep = *iter;
        drawablep->clearState(LLDrawable::EARLY_MOVE | LLDrawable::MOVE_UNDAMPED | LLDrawable::ON_MOVE_LIST | LLDrawable::ANIMATED_CHILD | LLDrawable::ON_SHIFT_LIST);
    }
    pipeline.mShiftList.clear();
}

// static
void DXPipeline::rebuildPriorityGroups(LLPipeline& pipeline, F32 max_dtime)
{
    LLTimer update_timer;
    pipeline.assertInitialized();

    gMeshRepo.notifyLoadedMeshes();

    pipeline.mGroupQ1Locked = true;

    // S24: erases each group from the queue only once it has actually been rebuilt, rather than
    // looping then clearing the whole container unconditionally afterward. A group left queued
    // when the time budget runs out stays flagged (markRebuild() refuses to re-queue anything
    // already flagged) and is picked up next postSort(); clearing the container regardless of how
    // far the loop got would drop unprocessed groups and leave them flagged forever, never rebuilt.
    pipeline.mGroupSaveQ1 = pipeline.mGroupQ1; // preserve the original "save until safe to unref" snapshot semantics

    for (LLSpatialGroup::sg_vector_t::iterator iter = pipeline.mGroupQ1.begin(); iter != pipeline.mGroupQ1.end();)
    {
        LLSpatialGroup* group = *iter;
        group->rebuildGeom();
        group->clearState(LLSpatialGroup::IN_BUILD_Q1);
        iter = pipeline.mGroupQ1.erase(iter);

        if (update_timer.getElapsedTimeF32() >= max_dtime)
        {
            break;
        }
    }

    pipeline.mGroupQ1Locked = false;
}

// static
void DXPipeline::updateGeom(LLPipeline& pipeline, F32 max_dtime)
{
    LL_RECORD_BLOCK_TIME(FTM_GEO_UPDATE);
    if (gCubeSnapshot)
    {
        return;
    }

    pipeline.assertInitialized();

    // Reset per-frame metrics (currently only LLVOVolume uses this).
    LLVOVolume::preUpdateGeom();

    // S24: budgets this queue by max_dtime, the same way its sibling createObjects() (above) does,
    // rather than draining the entire priority rebuild queue every frame regardless of size.
    // Turning a corner and revealing a room's worth of previously-occluded geometry can queue many
    // drawables for rebuild in the same frame; forcing all of them through in one frame produces a
    // deep single-frame time spike. A drawable not reached this call stays in mBuildQ1 (still
    // IN_REBUILD_Q) and is picked up next frame - safe, since anything visible-and-dirty this frame
    // still gets force-built by postSort()'s own cull-time rebuild pass regardless.
    LLTimer update_timer;

    for (auto iter = pipeline.mBuildQ1.begin(); iter != pipeline.mBuildQ1.end();)
    {
        LLDrawable* drawablep = *iter;

        // Dead or null -> drop immediately.
        if (!drawablep || drawablep->isDead())
        {
            iter = pipeline.mBuildQ1.erase(iter);
            continue;
        }

        // Handle pending unload.
        if (drawablep->isUnload())
        {
            drawablep->unload();
            drawablep->clearState(LLDrawable::FOR_UNLOAD);
        }

        // If geometry update completes, remove from queue.
        if (DXPipeline::updateDrawableGeom(pipeline, drawablep))
        {
            drawablep->clearState(LLDrawable::IN_REBUILD_Q);
            iter = pipeline.mBuildQ1.erase(iter);
        }
        else
        {
            ++iter;
        }

        if (update_timer.getElapsedTimeF32() >= max_dtime)
        {
            break;
        }
    }

    DXPipeline::updateMovedList(pipeline, pipeline.mMovedBridge);
}

namespace
{
    // Used only by renderHighlights() below.
    inline void bindHighlightProgram(LLHLSLShader& program)
    {
        if ((LLViewerShaderMgr::instance()->getShaderLevel(LLViewerShaderMgr::SHADER_INTERFACE) > 0))
        {
            program.bind();
            gDX.diffuseColor4f(1, 1, 1, 0.5f);
        }
    }

    inline void unbindHighlightProgram(LLHLSLShader& program)
    {
        if (LLViewerShaderMgr::instance()->getShaderLevel(LLViewerShaderMgr::SHADER_INTERFACE) > 0)
        {
            program.unbind();
        }
    }
}

// ============================================================================
// Debug-visualization rendering and picking/intersection
// ============================================================================

// static
void DXPipeline::renderSelectedFaces(LLPipeline& pipeline, const LLColor4& color)
{
    if (!pipeline.mFaceSelectImagep)
    {
        pipeline.mFaceSelectImagep = LLViewerTextureManager::getFetchedTexture(IMG_FACE_SELECT);
    }

    if (pipeline.mFaceSelectImagep)
    {
        // Make sure the selection image gets downloaded and decoded
        pipeline.mFaceSelectImagep->addTextureStats((F32)MAX_IMAGE_AREA);

        for (auto facep : pipeline.mSelectedFaces)
        {
            if (!facep || !facep->getViewerObject())
            {
                LLSelectMgr::getInstance()->clearSelections();
                return;
            }
            if (!facep->getDrawable() || facep->getDrawable()->isDead())
            {
                LL_ERRS() << "Bad face on selection" << LL_ENDL;
                return;
            }

            facep->renderSelected(pipeline.mFaceSelectImagep, color);
        }
    }
}

// static
void DXPipeline::renderHighlights(LLPipeline& pipeline)
{
    pipeline.assertInitialized();

    // Draw 3D UI elements here (before we clear the Z buffer in POOL_HUD)
    // Render highlighted faces.
    LLGLSPipelineAlpha gls_pipeline_alpha;
    DXPipeline::disableLights(pipeline);

    if (pipeline.hasRenderDebugFeatureMask(LLPipeline::RENDER_DEBUG_FEATURE_SELECTED))
    {
        bindHighlightProgram(gHighlightProgram);

        if (DXPipeline::sRenderHighlightTextureChannel == LLRender::DIFFUSE_MAP ||
            DXPipeline::sRenderHighlightTextureChannel == LLRender::BASECOLOR_MAP ||
            DXPipeline::sRenderHighlightTextureChannel == LLRender::METALLIC_ROUGHNESS_MAP ||
            DXPipeline::sRenderHighlightTextureChannel == LLRender::GLTF_NORMAL_MAP ||
            DXPipeline::sRenderHighlightTextureChannel == LLRender::EMISSIVE_MAP ||
            DXPipeline::sRenderHighlightTextureChannel == LLRender::NUM_TEXTURE_CHANNELS)
        {
            static const LLColor4 highlight_selected_color(1.f, 1.f, 1.f, 0.5f);
            DXPipeline::renderSelectedFaces(pipeline, highlight_selected_color);
        }

        // Paint 'em red!
        static const LLColor4 highlight_face_color(1.f, 0.f, 0.f, 0.5f);
        for (auto facep : pipeline.mHighlightFaces)
        {
            facep->renderSelected(LLViewerTexture::sNullImagep, highlight_face_color);
        }

        unbindHighlightProgram(gHighlightProgram);
    }

    // Contains a list of the faces of objects that are physical or
    // have touch-handlers.
    pipeline.mHighlightFaces.clear();

    if (pipeline.hasRenderDebugFeatureMask(LLPipeline::RENDER_DEBUG_FEATURE_SELECTED))
    {
        if (DXPipeline::sRenderHighlightTextureChannel == LLRender::NORMAL_MAP)
        {
            static const LLColor4 highlight_normal_color(1.0f, 0.5f, 0.5f, 0.5f);
            bindHighlightProgram(gHighlightNormalProgram);
            DXPipeline::renderSelectedFaces(pipeline, highlight_normal_color);
            unbindHighlightProgram(gHighlightNormalProgram);
        }
        else if (DXPipeline::sRenderHighlightTextureChannel == LLRender::SPECULAR_MAP)
        {
            static const LLColor4 highlight_specular_color(0.0f, 0.3f, 1.0f, 0.8f);
            bindHighlightProgram(gHighlightSpecularProgram);
            DXPipeline::renderSelectedFaces(pipeline, highlight_specular_color);
            unbindHighlightProgram(gHighlightSpecularProgram);
        }
    }
}

// static
void DXPipeline::renderPhysicsDisplay(LLPipeline& pipeline)
{
    if (!pipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_PHYSICS_SHAPES))
    {
        return;
    }

    gDX.flush();
    gDebugProgram.bind();

    LLGLEnable polygon_offset_line(GL_POLYGON_OFFSET_LINE);
    gDX.setPolygonOffset(3.f, 3.f);
    // S24: glLineWidth() has no DX11 equivalent; LLUI::setLineWidth() is the no-op stand-in under
    // DX_RENDER. glPolygonMode(GL_LINE/GL_FILL) below is removed outright: no D3D11 per-draw
    // wireframe equivalent exists.
    LLUI::setLineWidth(3.f);
    LLGLEnable blend(GL_BLEND);
    gDX.setSceneBlendType(LLRender::BT_ALPHA);

    for (int pass = 0; pass < 3; ++pass)
    {
        // pass 0 - depth write enabled, color write disabled, fill
        // pass 1 - depth write disabled, color write enabled, fill
        // pass 2 - depth write disabled, color write enabled, wireframe
        gDX.setColorWriteMask(pass >= 1, false);
        DXDepthTest depth(true, pass == 0);

        bool wireframe = (pass == 2);

        for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
            iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
        {
            LLViewerRegion* region = *iter;
            for (U32 i = 0; i < LLViewerRegion::NUM_PARTITIONS; i++)
            {
                LLSpatialPartition* part = region->getSpatialPartition(i);
                if (part)
                {
                    if (DXPipeline::hasRenderType(pipeline, part->mDrawableType))
                    {
                        part->renderPhysicsShapes(wireframe);
                    }
                }
            }
        }
        gDX.flush();
    }
    LLUI::setLineWidth(1.f);
    gDebugProgram.unbind();
}

// static
void DXPipeline::renderDebug(LLPipeline& pipeline)
{
    static LLStaticHashedString sTint("tint");
    static LLStaticHashedString sAmbiance("ambiance");
    static LLStaticHashedString sAlphaScale("alpha_scale");

    pipeline.assertInitialized();

    bool hud_only = DXPipeline::hasRenderType(pipeline, LLPipeline::RENDER_TYPE_HUD);

    if (!hud_only)
    {
        //Render any navmesh geometry
        LLPathingLib* llPathingLibInstance = LLPathingLib::getInstance();
        if (llPathingLibInstance != NULL)
        {
            //character floater renderables

            LLHandle<LLFloaterPathfindingCharacters> pathfindingCharacterHandle = LLFloaterPathfindingCharacters::getInstanceHandle();
            if (!pathfindingCharacterHandle.isDead())
            {
                LLFloaterPathfindingCharacters* pathfindingCharacter = pathfindingCharacterHandle.get();

                if (pathfindingCharacter->getVisible() || gAgentCamera.cameraMouselook())
                {
                    gPathfindingProgram.bind();
                    gPathfindingProgram.uniform1f(sTint, 1.f);
                    gPathfindingProgram.uniform1f(sAmbiance, 1.f);
                    gPathfindingProgram.uniform1f(sAlphaScale, 1.f);

                    //Requried character physics capsule render parameters
                    LLUUID id;
                    LLVector3 pos;
                    LLQuaternion rot;

                    if (pathfindingCharacter->isPhysicsCapsuleEnabled(id, pos, rot))
                    {
                        //remove blending artifacts
                        gDX.setColorWriteMask(false, false);
                        llPathingLibInstance->renderSimpleShapeCapsuleID(gDX, id, pos, rot);
                        gDX.setColorWriteMask(true, false);
                        LLGLEnable blend(GL_BLEND);
                        gPathfindingProgram.uniform1f(sAlphaScale, 0.90f);
                        llPathingLibInstance->renderSimpleShapeCapsuleID(gDX, id, pos, rot);
                        gPathfindingProgram.bind();
                    }
                }
            }

            //pathing console renderables
            LLHandle<LLFloaterPathfindingConsole> pathfindingConsoleHandle = LLFloaterPathfindingConsole::getInstanceHandle();
            if (!pathfindingConsoleHandle.isDead())
            {
                LLFloaterPathfindingConsole* pathfindingConsole = pathfindingConsoleHandle.get();

                if (pathfindingConsole->getVisible() || gAgentCamera.cameraMouselook())
                {
                    F32 ambiance = gSavedSettings.getF32("PathfindingAmbiance");

                    gPathfindingProgram.bind();

                    gPathfindingProgram.uniform1f(sTint, 1.f);
                    gPathfindingProgram.uniform1f(sAmbiance, ambiance);
                    gPathfindingProgram.uniform1f(sAlphaScale, 1.f);

                    if (!pathfindingConsole->isRenderWorld())
                    {
                        // S24: no cross-backend equivalent for this user-configurable clear color yet,
                        // so it's guarded out rather than converted. Under DX_RENDER the pathfinding
                        // console's "hide world, navmesh only" mode does not clear to
                        // PathfindingNavMeshClear first.
                        const LLColor4 clearColor = gSavedSettings.getColor4("PathfindingNavMeshClear");
                        gDX.setColorWriteMask(true, true);
                        gDX.setColorWriteMask(true, false);
                    }

                    //NavMesh
                    if (pathfindingConsole->isRenderNavMesh())
                    {
                        gDX.flush();
                        // S24: glLineWidth() has no DX11 equivalent; LLUI::setLineWidth() is the no-op
                        // stand-in under DX_RENDER. glPolygonMode(GL_LINE/GL_FILL) is removed outright:
                        // no D3D11 per-draw wireframe equivalent exists.
                        LLUI::setLineWidth(2.0f);
                        LLGLEnable cull(GL_CULL_FACE);
                        LLGLDisable blend(GL_BLEND);

                        if (pathfindingConsole->isRenderWorld())
                        {
                            LLGLEnable blend(GL_BLEND);
                            gPathfindingProgram.uniform1f(sAlphaScale, 0.66f);
                            llPathingLibInstance->renderNavMesh();
                        }
                        else
                        {
                            llPathingLibInstance->renderNavMesh();
                        }

                        //render edges
                        gPathfindingNoNormalsProgram.bind();
                        gPathfindingNoNormalsProgram.uniform1f(sTint, 1.f);
                        gPathfindingNoNormalsProgram.uniform1f(sAlphaScale, 1.f);
                        llPathingLibInstance->renderNavMeshEdges();
                        gPathfindingProgram.bind();

                        gDX.flush();
                        LLUI::setLineWidth(1.0f);
                        gDX.flush();
                    }
                    //User designated path
                    if (LLPathfindingPathTool::getInstance()->isRenderPath())
                    {
                        //The path
                        gUIProgram.bind();
                        gDX.getTexUnit(0)->bind(LLViewerFetchedTexture::sWhiteImagep);
                        llPathingLibInstance->renderPath();
                        gPathfindingProgram.bind();

                        //The bookends
                        //remove blending artifacts
                        gDX.setColorWriteMask(false, false);
                        llPathingLibInstance->renderPathBookend(gDX, LLPathingLib::LLPL_START);
                        llPathingLibInstance->renderPathBookend(gDX, LLPathingLib::LLPL_END);

                        gDX.setColorWriteMask(true, false);
                        //render the bookends
                        LLGLEnable blend(GL_BLEND);
                        gPathfindingProgram.uniform1f(sAlphaScale, 0.90f);
                        llPathingLibInstance->renderPathBookend(gDX, LLPathingLib::LLPL_START);
                        llPathingLibInstance->renderPathBookend(gDX, LLPathingLib::LLPL_END);
                        gPathfindingProgram.bind();
                    }

                    if (pathfindingConsole->isRenderWaterPlane())
                    {
                        LLGLEnable blend(GL_BLEND);
                        gPathfindingProgram.uniform1f(sAlphaScale, 0.90f);
                        llPathingLibInstance->renderSimpleShapes(gDX, gAgent.getRegion()->getWaterHeight());
                    }
                    //physics/exclusion shapes
                    if (pathfindingConsole->isRenderAnyShapes())
                    {
                        U32 render_order[] = {
                            1 << LLPathingLib::LLST_ObstacleObjects,
                            1 << LLPathingLib::LLST_WalkableObjects,
                            1 << LLPathingLib::LLST_ExclusionPhantoms,
                            1 << LLPathingLib::LLST_MaterialPhantoms,
                        };

                        U32 flags = pathfindingConsole->getRenderShapeFlags();

                        for (U32 i = 0; i < 4; i++)
                        {
                            if (!(flags & render_order[i]))
                            {
                                continue;
                            }

                            //turn off backface culling for volumes so they are visible when camera is inside volume
                            LLGLDisable cull(i >= 2 ? GL_CULL_FACE : 0);

                            gDX.flush();

                            //get rid of some z-fighting
                            LLGLEnable polyOffset(GL_POLYGON_OFFSET_FILL);
                            gDX.setPolygonOffset(1.0f, 1.0f);

                            //render to depth first to avoid blending artifacts
                            gDX.setColorWriteMask(false, false);
                            llPathingLibInstance->renderNavMeshShapesVBO(render_order[i]);
                            gDX.setColorWriteMask(true, false);

                            //get rid of some z-fighting
                            gDX.setPolygonOffset(0.f, 0.f);

                            LLGLEnable blend(GL_BLEND);

                            {
                                gPathfindingProgram.uniform1f(sAmbiance, ambiance);

                                { //draw solid overlay
                                    DXDepthTest depth(true, false, GL_LEQUAL);
                                    llPathingLibInstance->renderNavMeshShapesVBO(render_order[i]);
                                    gDX.flush();
                                }

                                LLGLEnable lineOffset(GL_POLYGON_OFFSET_LINE);

                                F32 offset = gSavedSettings.getF32("PathfindingLineOffset");

                                if (pathfindingConsole->isRenderXRay())
                                {
                                    gPathfindingProgram.uniform1f(sTint, gSavedSettings.getF32("PathfindingXRayTint"));
                                    gPathfindingProgram.uniform1f(sAlphaScale, gSavedSettings.getF32("PathfindingXRayOpacity"));
                                    LLGLEnable blend(GL_BLEND);
                                    DXDepthTest depth(true, false, GL_GREATER);

                                    gDX.setPolygonOffset(offset, -offset);

                                    if (gSavedSettings.getBOOL("PathfindingXRayWireframe"))
                                    { //draw hidden wireframe as darker and less opaque
                                        gPathfindingProgram.uniform1f(sAmbiance, 1.f);
                                        llPathingLibInstance->renderNavMeshShapesVBO(render_order[i]);
                                    }
                                    else
                                    {
                                        gPathfindingProgram.uniform1f(sAmbiance, ambiance);
                                        llPathingLibInstance->renderNavMeshShapesVBO(render_order[i]);
                                    }
                                }

                                { //draw visible wireframe as brighter, thicker and more opaque
                                    // S24: glLineWidth() has no DX11 equivalent; LLUI::setLineWidth() is
                                    // the no-op stand-in under DX_RENDER. glPolygonMode(GL_LINE/GL_FILL)
                                    // is removed outright: no D3D11 per-draw wireframe equivalent exists.
                                    gDX.setPolygonOffset(offset, offset);
                                    gPathfindingProgram.uniform1f(sAmbiance, 1.f);
                                    gPathfindingProgram.uniform1f(sTint, 1.f);
                                    gPathfindingProgram.uniform1f(sAlphaScale, 1.f);

                                    LLUI::setLineWidth(gSavedSettings.getF32("PathfindingLineWidth"));
                                    LLGLDisable blendOut(GL_BLEND);
                                    llPathingLibInstance->renderNavMeshShapesVBO(render_order[i]);
                                    gDX.flush();
                                    LLUI::setLineWidth(1.f);
                                }
                            }
                        }
                    }

                    gDX.setPolygonOffset(0.f, 0.f);

                    if (pathfindingConsole->isRenderNavMesh() && pathfindingConsole->isRenderXRay())
                    {   //render navmesh xray
                        F32 ambiance = gSavedSettings.getF32("PathfindingAmbiance");

                        LLGLEnable lineOffset(GL_POLYGON_OFFSET_LINE);
                        LLGLEnable polyOffset(GL_POLYGON_OFFSET_FILL);

                        F32 offset = gSavedSettings.getF32("PathfindingLineOffset");
                        gDX.setPolygonOffset(offset, -offset);

                        LLGLEnable blend(GL_BLEND);
                        DXDepthTest depth(true, false, GL_GREATER);
                        gDX.flush();
                        LLUI::setLineWidth(2.0f);
                        LLGLEnable cull(GL_CULL_FACE);

                        gPathfindingProgram.uniform1f(sTint, gSavedSettings.getF32("PathfindingXRayTint"));
                        gPathfindingProgram.uniform1f(sAlphaScale, gSavedSettings.getF32("PathfindingXRayOpacity"));

                        if (gSavedSettings.getBOOL("PathfindingXRayWireframe"))
                        { //draw hidden wireframe as darker and less opaque
                            gPathfindingProgram.uniform1f(sAmbiance, 1.f);
                            llPathingLibInstance->renderNavMesh();
                        }
                        else
                        {
                            gPathfindingProgram.uniform1f(sAmbiance, ambiance);
                            llPathingLibInstance->renderNavMesh();
                        }

                        //render edges
                        gPathfindingNoNormalsProgram.bind();
                        gPathfindingNoNormalsProgram.uniform1f(sTint, gSavedSettings.getF32("PathfindingXRayTint"));
                        gPathfindingNoNormalsProgram.uniform1f(sAlphaScale, gSavedSettings.getF32("PathfindingXRayOpacity"));
                        llPathingLibInstance->renderNavMeshEdges();
                        gPathfindingProgram.bind();

                        gDX.flush();
                        LLUI::setLineWidth(1.0f);
                    }

                    gDX.setPolygonOffset(0.f, 0.f);

                    gDX.flush();
                    gPathfindingProgram.unbind();
                }
            }
        }
    }

    gDXLastMatrix = NULL;
    gDX.loadMatrix(gDXModelView);
    gDX.setColorWriteMask(true, false);

    if (!hud_only && !pipeline.mDebugBlips.empty())
    { //render debug blips
        gUIProgram.bind();
        gDX.color4f(1, 1, 1, 1);

        gDX.getTexUnit(0)->bind(LLViewerFetchedTexture::sWhiteImagep, true);

        DXDepthTest depth(true, true, GL_ALWAYS);

        gDX.begin(LLRender::POINTS);
        for (std::list<LLPipeline::DebugBlip>::iterator iter = pipeline.mDebugBlips.begin(); iter != pipeline.mDebugBlips.end(); )
        {
            LLPipeline::DebugBlip& blip = *iter;

            blip.mAge += gFrameIntervalSeconds.value();
            if (blip.mAge > 2.f)
            {
                pipeline.mDebugBlips.erase(iter++);
            }
            else
            {
                iter++;
            }

            blip.mPosition.mV[2] += gFrameIntervalSeconds.value() * 2.f;

            gDX.color4fv(blip.mColor.mV);
            gDX.vertex3fv(blip.mPosition.mV);
        }
        gDX.end();
        gDX.flush();
    }

    // Debug stuff.
    if (pipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_OCTREE |
        LLPipeline::RENDER_DEBUG_OCCLUSION |
        LLPipeline::RENDER_DEBUG_LIGHTS |
        LLPipeline::RENDER_DEBUG_BATCH_SIZE |
        LLPipeline::RENDER_DEBUG_UPDATE_TYPE |
        LLPipeline::RENDER_DEBUG_BBOXES |
        LLPipeline::RENDER_DEBUG_NORMALS |
        LLPipeline::RENDER_DEBUG_POINTS |
        LLPipeline::RENDER_DEBUG_TEXTURE_AREA |
        LLPipeline::RENDER_DEBUG_TEXTURE_ANIM |
        LLPipeline::RENDER_DEBUG_RAYCAST |
        LLPipeline::RENDER_DEBUG_AVATAR_VOLUME |
        LLPipeline::RENDER_DEBUG_AVATAR_JOINTS |
        LLPipeline::RENDER_DEBUG_AGENT_TARGET |
        LLPipeline::RENDER_DEBUG_SHADOW_FRUSTA |
        LLPipeline::RENDER_DEBUG_TEXEL_DENSITY))
    {

        for (LLViewerRegion* region : LLWorld::getInstance()->getRegionList())
        {
            for (U32 i = 0; i < LLViewerRegion::NUM_PARTITIONS; i++)
            {
                LLSpatialPartition* part = region->getSpatialPartition(i);
                if (part)
                {
                    if ((hud_only && (part->mDrawableType == LLPipeline::RENDER_TYPE_HUD || part->mDrawableType == LLPipeline::RENDER_TYPE_HUD_PARTICLES)) ||
                        (!hud_only && DXPipeline::hasRenderType(pipeline, part->mDrawableType)))
                    {
                        part->renderDebug();
                    }
                }
            }
        }

        for (LLCullResult::bridge_iterator i = sCull->beginVisibleBridge(); i != sCull->endVisibleBridge(); ++i)
        {
            LLSpatialBridge* bridge = *i;
            if (!bridge->isDead() && DXPipeline::hasRenderType(pipeline, bridge->mDrawableType))
            {
                gDX.pushMatrix();
                gDX.multMatrix((F32*)bridge->mDrawable->getRenderMatrix().mMatrix);
                bridge->renderDebug();
                gDX.popMatrix();
            }
        }
    }

    LL::GLTFSceneManager::instance().renderDebug();

    if (pipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_OCCLUSION))
    { //render visible selected group occlusion geometry
        gDebugProgram.bind();
        DXDepthTest depth(true, false);
        gDX.diffuseColor3f(1, 0, 1);
        for (std::set<LLSpatialGroup*>::iterator iter = visible_selected_groups.begin(); iter != visible_selected_groups.end(); ++iter)
        {
            LLSpatialGroup* group = *iter;

            LLVector4a fudge(0.25f); //SG_OCCLUSION_FUDGE

            LLVector4a size(0.f);
            const LLVector4a* bounds = group->getBounds();
            size.setAdd(fudge, bounds[1]);

            drawBox(bounds[0], size);
        }
    }

    visible_selected_groups.clear();

    //draw reflection probes and links between them
    if (pipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_REFLECTION_PROBES) && !hud_only)
    {
        pipeline.mSphereProbes.renderDebug();
    }

    static LLCachedControl<bool> render_ref_probe_volumes(gSavedSettings, "RenderReflectionProbeVolumes");
    if (render_ref_probe_volumes && !hud_only)
    {

        DXPipeline::bindDeferredShader(pipeline, gReflectionProbeDisplayProgram, NULL);
        pipeline.mScreenTriangleVB->setBuffer();

        LLGLEnable blend(GL_BLEND);
        DXDepthTest depth(false);

        pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

        DXPipeline::unbindDeferredShader(pipeline, gReflectionProbeDisplayProgram);
    }

    gUIProgram.bind();

    if (pipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_RAYCAST) && !hud_only)
    { //draw crosshairs on particle intersection
        if (gDebugRaycastParticle)
        {
            gDebugProgram.bind();

            gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

            LLVector3 center(gDebugRaycastParticleIntersection.getF32ptr());
            LLVector3 size(0.1f, 0.1f, 0.1f);

            LLVector3 p[6];

            p[0] = center + size.scaledVec(LLVector3(1, 0, 0));
            p[1] = center + size.scaledVec(LLVector3(-1, 0, 0));
            p[2] = center + size.scaledVec(LLVector3(0, 1, 0));
            p[3] = center + size.scaledVec(LLVector3(0, -1, 0));
            p[4] = center + size.scaledVec(LLVector3(0, 0, 1));
            p[5] = center + size.scaledVec(LLVector3(0, 0, -1));

            gDX.begin(LLRender::LINES);
            gDX.diffuseColor3f(1.f, 1.f, 0.f);
            for (U32 i = 0; i < 6; i++)
            {
                gDX.vertex3fv(p[i].mV);
            }
            gDX.end();
            gDX.flush();

            gDebugProgram.unbind();
        }
    }

    if (pipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_SHADOW_FRUSTA) && !hud_only)
    {
        LLVertexBuffer::unbind();

        LLGLEnable blend(GL_BLEND);
        DXDepthTest depth(true, false);
        LLGLDisable cull(GL_CULL_FACE);

        gDX.color4f(1, 1, 1, 1);
        gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

        F32 a = 0.1f;

        F32 col[] =
        {
            1,0,0,a,
            0,1,0,a,
            0,0,1,a,
            1,0,1,a,

            1,1,0,a,
            0,1,1,a,
            1,1,1,a,
            1,0,1,a,
        };

        for (U32 i = 0; i < 8; i++)
        {
            LLVector3* frust = pipeline.mShadowCamera[i].mAgentFrustum;

            if (i > 3)
            { //render shadow frusta as volumes
                if (pipeline.mShadowFrustPoints[i - 4].empty())
                {
                    continue;
                }

                gDX.color4fv(col + (i - 4) * 4);

                gDX.begin(LLRender::TRIANGLE_STRIP);
                gDX.vertex3fv(frust[0].mV); gDX.vertex3fv(frust[4].mV);
                gDX.vertex3fv(frust[1].mV); gDX.vertex3fv(frust[5].mV);
                gDX.vertex3fv(frust[2].mV); gDX.vertex3fv(frust[6].mV);
                gDX.vertex3fv(frust[3].mV); gDX.vertex3fv(frust[7].mV);
                gDX.vertex3fv(frust[0].mV); gDX.vertex3fv(frust[4].mV);
                gDX.end();

                gDX.begin(LLRender::TRIANGLE_STRIP);
                gDX.vertex3fv(frust[0].mV);
                gDX.vertex3fv(frust[1].mV);
                gDX.vertex3fv(frust[3].mV);
                gDX.vertex3fv(frust[2].mV);
                gDX.end();

                gDX.begin(LLRender::TRIANGLE_STRIP);
                gDX.vertex3fv(frust[4].mV);
                gDX.vertex3fv(frust[5].mV);
                gDX.vertex3fv(frust[7].mV);
                gDX.vertex3fv(frust[6].mV);
                gDX.end();
            }

            if (i < 4)
            {
                //if (i == 0 || !mShadowFrustPoints[i].empty())
                {
                    //render visible point cloud
                    gDX.flush();
                    gDX.begin(LLRender::POINTS);

                    F32* c = col + i * 4;
                    gDX.color3fv(c);

                    for (U32 j = 0; j < pipeline.mShadowFrustPoints[i].size(); ++j)
                    {
                        gDX.vertex3fv(pipeline.mShadowFrustPoints[i][j].mV);
                    }
                    gDX.end();

                    gDX.flush();

                    LLVector3* ext = pipeline.mShadowExtents[i];
                    LLVector3 pos = (ext[0] + ext[1]) * 0.5f;
                    LLVector3 size = (ext[1] - ext[0]) * 0.5f;
                    drawBoxOutline(pos, size);

                    //render camera frustum splits as outlines
                    gDX.begin(LLRender::LINES);
                    gDX.vertex3fv(frust[0].mV); gDX.vertex3fv(frust[1].mV);
                    gDX.vertex3fv(frust[1].mV); gDX.vertex3fv(frust[2].mV);
                    gDX.vertex3fv(frust[2].mV); gDX.vertex3fv(frust[3].mV);
                    gDX.vertex3fv(frust[3].mV); gDX.vertex3fv(frust[0].mV);
                    gDX.vertex3fv(frust[4].mV); gDX.vertex3fv(frust[5].mV);
                    gDX.vertex3fv(frust[5].mV); gDX.vertex3fv(frust[6].mV);
                    gDX.vertex3fv(frust[6].mV); gDX.vertex3fv(frust[7].mV);
                    gDX.vertex3fv(frust[7].mV); gDX.vertex3fv(frust[4].mV);
                    gDX.vertex3fv(frust[0].mV); gDX.vertex3fv(frust[4].mV);
                    gDX.vertex3fv(frust[1].mV); gDX.vertex3fv(frust[5].mV);
                    gDX.vertex3fv(frust[2].mV); gDX.vertex3fv(frust[6].mV);
                    gDX.vertex3fv(frust[3].mV); gDX.vertex3fv(frust[7].mV);
                    gDX.end();
                }
            }
        }
    }

    if (pipeline.getRenderDebugMaskMutable() & LLPipeline::RENDER_DEBUG_WIND_VECTORS)
    {
        gAgent.getRegion()->mWind.renderVectors();
    }

    if (pipeline.getRenderDebugMaskMutable() & LLPipeline::RENDER_DEBUG_COMPOSITION)
    {
        // Debug composition layers
        F32 x = 0.f, y = 0.f;

        gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

        if (gAgent.getRegion())
        {
            gDX.begin(LLRender::POINTS);
            // Draw the composition layer for the region that I'm in.
            for (x = 0; x <= 260; x++)
            {
                for (y = 0; y <= 260; y++)
                {
                    if ((x > 255) || (y > 255))
                    {
                        gDX.color4f(1.f, 0.f, 0.f, 1.f);
                    }
                    else
                    {
                        gDX.color4f(0.f, 0.f, 1.f, 1.f);
                    }
                    F32 z = gAgent.getRegion()->getCompositionXY((S32)x, (S32)y);
                    z *= 5.f;
                    z += 50.f;
                    gDX.vertex3f(x, y, z);
                }
            }
            gDX.end();
        }
    }

    gDX.flush();
    gUIProgram.unbind();

    // S24: restores the alpha write mask this function disabled above ("remove blending
    // artifacts" for the debug-blips/pathfinding draws). This is real, sticky D3D11 blend state
    // (OMSetBlendState) that nothing else resets before this function's caller returns:
    // DXPipeline::presentDeferredScreen()'s gamma/tonemap draw into mPostPingMap runs later the
    // same frame with no write-mask reset of its own, so alpha (which carries the glow-luminance
    // signal generateGlow() extracts from) silently never gets written and the whole bloom
    // effect goes dark with no error anywhere. Same leaked-D3D11-state class of bug as
    // DXPipeline::renderGeomPostDeferred()'s own restore before this exact call.
    gDX.setColorWriteMask(true, true);
}

// static
LLVOPartGroup* DXPipeline::lineSegmentIntersectParticle(LLPipeline& pipeline, const LLVector4a& start, const LLVector4a& end, LLVector4a* intersection,
    S32* face_hit)
{
    LLVector4a local_end = end;

    LLVector4a position(0.f);

    LLDrawable* drawable = NULL;

    for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
        iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
    {
        LLViewerRegion* region = *iter;

        LLSpatialPartition* part = region->getSpatialPartition(LLViewerRegion::PARTITION_PARTICLE);
        if (part && DXPipeline::hasRenderType(pipeline, part->mDrawableType))
        {
            LLDrawable* hit = part->lineSegmentIntersect(start, local_end, true, false, true, false, face_hit, &position, NULL, NULL, NULL);
            if (hit)
            {
                drawable = hit;
                local_end = position;
            }
        }
    }

    LLVOPartGroup* ret = NULL;
    if (drawable)
    {
        //make sure we're returning an LLVOPartGroup
        llassert(drawable->getVObj()->getPCode() == LLViewerObject::LL_VO_PART_GROUP);
        ret = (LLVOPartGroup*)drawable->getVObj().get();
    }

    if (intersection)
    {
        *intersection = position;
    }

    return ret;
}

// static
LLViewerObject* DXPipeline::lineSegmentIntersectInWorld(LLPipeline& pipeline, const LLVector4a& start, const LLVector4a& end,
    bool pick_transparent,
    bool pick_rigged,
    bool pick_unselectable,
    bool pick_reflection_probe,
    S32* face_hit,
    S32* gltf_node_hit,
    S32* gltf_primitive_hit,
    LLVector4a* intersection,
    LLVector2* tex_coord,
    LLVector4a* normal,
    LLVector4a* tangent
)
{
    LLDrawable* drawable = NULL;

    LLVector4a local_end = end;

    LLVector4a position(0.f);

    DXPipeline::sPickAvatar = false; //! LLToolMgr::getInstance()->inBuildMode();

    for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
        iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
    {
        LLViewerRegion* region = *iter;

        for (U32 j = 0; j < LLViewerRegion::NUM_PARTITIONS; j++)
        {
            if ((j == LLViewerRegion::PARTITION_VOLUME) ||
                (j == LLViewerRegion::PARTITION_BRIDGE) ||
                (j == LLViewerRegion::PARTITION_AVATAR) || // for attachments
                (j == LLViewerRegion::PARTITION_CONTROL_AV) ||
                (j == LLViewerRegion::PARTITION_TERRAIN) ||
                (j == LLViewerRegion::PARTITION_TREE) ||
                (j == LLViewerRegion::PARTITION_GRASS))  // only check these partitions for now
            {
                LLSpatialPartition* part = region->getSpatialPartition(j);
                if (part && DXPipeline::hasRenderType(pipeline, part->mDrawableType))
                {
                    LLDrawable* hit = part->lineSegmentIntersect(start, local_end, pick_transparent, pick_rigged, pick_unselectable, pick_reflection_probe, face_hit, &position, tex_coord, normal, tangent);
                    if (hit)
                    {
                        drawable = hit;
                        local_end = position;
                    }
                }
            }
        }
    }

    if (!DXPipeline::sPickAvatar)
    {
        //save hit info in case we need to restore
        //due to attachment override
        LLVector4a local_normal(0.f);
        LLVector4a local_tangent(0.f);
        LLVector2 local_texcoord;
        S32 local_face_hit = -1;

        if (face_hit)
        {
            local_face_hit = *face_hit;
        }
        if (tex_coord)
        {
            local_texcoord = *tex_coord;
        }
        if (tangent)
        {
            local_tangent = *tangent;
        }
        else
        {
            local_tangent.clear();
        }
        if (normal)
        {
            local_normal = *normal;
        }
        else
        {
            local_normal.clear();
        }

        const F32 ATTACHMENT_OVERRIDE_DIST = 0.1f;

        //check against avatars
        DXPipeline::sPickAvatar = true;
        for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
            iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
        {
            LLViewerRegion* region = *iter;

            LLSpatialPartition* part = region->getSpatialPartition(LLViewerRegion::PARTITION_AVATAR);
            if (part && DXPipeline::hasRenderType(pipeline, part->mDrawableType))
            {
                LLDrawable* hit = part->lineSegmentIntersect(start, local_end, pick_transparent, pick_rigged, pick_unselectable, pick_reflection_probe, face_hit, &position, tex_coord, normal, tangent);
                if (hit)
                {
                    LLVector4a delta(0.f);
                    delta.setSub(position, local_end);

                    if (!drawable ||
                        !drawable->getVObj()->isAttachment() ||
                        delta.getLength3().getF32() > ATTACHMENT_OVERRIDE_DIST)
                    { //avatar overrides if previously hit drawable is not an attachment or
                      //attachment is far enough away from detected intersection
                        drawable = hit;
                        local_end = position;
                    }
                    else
                    { //prioritize attachments over avatars
                        position = local_end;

                        if (face_hit)
                        {
                            *face_hit = local_face_hit;
                        }
                        if (tex_coord)
                        {
                            *tex_coord = local_texcoord;
                        }
                        if (tangent)
                        {
                            *tangent = local_tangent;
                        }
                        if (normal)
                        {
                            *normal = local_normal;
                        }
                    }
                }
            }
        }
    }

    // check all avatar nametags (silly, isn't it?)
    for (LLCharacter* character : LLCharacter::sInstances)
    {
        LLVOAvatar* avatar = (LLVOAvatar*)character;
        if (avatar->mNameText.notNull() &&
            avatar->mNameText->lineSegmentIntersect(start, local_end, position))
        {
            drawable = avatar->mDrawable;
            local_end = position;
        }
    }

    S32 node_hit = -1;
    S32 primitive_hit = -1;
    LLDrawable* hit = LL::GLTFSceneManager::instance().lineSegmentIntersect(start, local_end, pick_transparent, pick_rigged, pick_unselectable, pick_reflection_probe, &node_hit, &primitive_hit, &position, tex_coord, normal, tangent);
    if (hit)
    {
        drawable = hit;
        local_end = position;
    }

    if (gltf_node_hit)
    {
        *gltf_node_hit = node_hit;
    }

    if (gltf_primitive_hit)
    {
        *gltf_primitive_hit = primitive_hit;
    }

    if (intersection)
    {
        *intersection = position;
    }

    return drawable ? drawable->getVObj().get() : NULL;
}

// static
LLViewerObject* DXPipeline::lineSegmentIntersectInHUD(LLPipeline& pipeline, const LLVector4a& start, const LLVector4a& end,
    bool pick_transparent,
    S32* face_hit,
    LLVector4a* intersection,
    LLVector2* tex_coord,
    LLVector4a* normal,
    LLVector4a* tangent
)
{
    LLDrawable* drawable = NULL;

    for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
        iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
    {
        LLViewerRegion* region = *iter;

        bool toggle = false;
        if (!DXPipeline::hasRenderType(pipeline, LLPipeline::RENDER_TYPE_HUD))
        {
            DXPipeline::toggleRenderType(pipeline, LLPipeline::RENDER_TYPE_HUD);
            toggle = true;
        }

        LLSpatialPartition* part = region->getSpatialPartition(LLViewerRegion::PARTITION_HUD);
        if (part)
        {
            LLDrawable* hit = part->lineSegmentIntersect(start, end, pick_transparent, false, true, false, face_hit, intersection, tex_coord, normal, tangent);
            if (hit)
            {
                drawable = hit;
            }
        }

        if (toggle)
        {
            DXPipeline::toggleRenderType(pipeline, LLPipeline::RENDER_TYPE_HUD);
        }
    }
    return drawable ? drawable->getVObj().get() : NULL;
}

// forAllDrawables() and the 6 beacon/highlight walker callbacks below are postSort()'s private
// helpers, scoped to this file.
namespace
{
    void forAllDrawables(LLCullResult::sg_iterator begin,
        LLCullResult::sg_iterator end,
        void (*func)(LLDrawable*))
    {
        for (LLCullResult::sg_iterator i = begin; i != end; ++i)
        {
            LLSpatialGroup* group = *i;
            if (group->isDead())
            {
                continue;
            }
            for (LLSpatialGroup::element_iter j = group->getDataBegin(); j != group->getDataEnd(); ++j)
            {
                if ((*j)->hasDrawable())
                {
                    func((LLDrawable*)(*j)->getDrawable());
                }
            }
        }
    }

    void renderScriptedBeacons(LLDrawable* drawablep)
    {
        LLViewerObject* vobj = drawablep->getVObj();
        if (vobj
            && !vobj->isAvatar()
            && !vobj->getParent()
            && vobj->flagScripted())
        {
            if (DXPipeline::sRenderBeacons)
            {
                gObjectList.addDebugBeacon(vobj->getPositionAgent(), "", LLColor4(1.f, 0.f, 0.f, 0.5f), LLColor4(1.f, 1.f, 1.f, 0.5f), DXPipeline::DebugBeaconLineWidth);
            }

            if (DXPipeline::sRenderHighlight)
            {
                S32 face_id = 0;
                S32 count = drawablep->getNumFaces();
                for (face_id = 0; face_id < count; face_id++)
                {
                    LLFace* facep = drawablep->getFace(face_id);
                    if (facep)
                    {
                        gPipeline.mHighlightFaces.push_back(facep);
                    }
                }
            }
        }
    }

    void renderScriptedTouchBeacons(LLDrawable* drawablep)
    {
        LLViewerObject* vobj = drawablep->getVObj();
        if (vobj && !vobj->isAvatar() && !vobj->getParent() && vobj->flagScripted() && vobj->flagHandleTouch())
        {
            if (DXPipeline::sRenderBeacons)
            {
                gObjectList.addDebugBeacon(vobj->getPositionAgent(), "", LLColor4(1.f, 0.f, 0.f, 0.5f), LLColor4(1.f, 1.f, 1.f, 0.5f),
                    DXPipeline::DebugBeaconLineWidth);
            }

            if (DXPipeline::sRenderHighlight)
            {
                S32 face_id = 0;
                S32 count = drawablep->getNumFaces();
                for (face_id = 0; face_id < count; face_id++)
                {
                    LLFace* facep = drawablep->getFace(face_id);
                    if (facep)
                    {
                        gPipeline.mHighlightFaces.push_back(facep);
                    }
                }
            }
        }
    }

    void renderPhysicalBeacons(LLDrawable* drawablep)
    {
        LLViewerObject* vobj = drawablep->getVObj();
        if (vobj &&
            !vobj->isAvatar()
            //&& !vobj->getParent()
            && vobj->flagUsePhysics())
        {
            if (DXPipeline::sRenderBeacons)
            {
                gObjectList.addDebugBeacon(vobj->getPositionAgent(), "", LLColor4(0.f, 1.f, 0.f, 0.5f), LLColor4(1.f, 1.f, 1.f, 0.5f),
                    DXPipeline::DebugBeaconLineWidth);
            }

            if (DXPipeline::sRenderHighlight)
            {
                S32 face_id = 0;
                S32 count = drawablep->getNumFaces();
                for (face_id = 0; face_id < count; face_id++)
                {
                    LLFace* facep = drawablep->getFace(face_id);
                    if (facep)
                    {
                        gPipeline.mHighlightFaces.push_back(facep);
                    }
                }
            }
        }
    }

    void renderMOAPBeacons(LLDrawable* drawablep)
    {
        LLViewerObject* vobj = drawablep->getVObj();

        if (!vobj || vobj->isAvatar())
            return;

        bool beacon = false;
        U8   tecount = vobj->getNumTEs();
        for (int x = 0; x < tecount; x++)
        {
            if (vobj->getTE(x)->hasMedia())
            {
                beacon = true;
                break;
            }
        }
        if (beacon)
        {
            if (DXPipeline::sRenderBeacons)
            {
                gObjectList.addDebugBeacon(vobj->getPositionAgent(), "", LLColor4(1.f, 1.f, 1.f, 0.5f), LLColor4(1.f, 1.f, 1.f, 0.5f),
                    DXPipeline::DebugBeaconLineWidth);
            }

            if (DXPipeline::sRenderHighlight)
            {
                S32 face_id = 0;
                S32 count = drawablep->getNumFaces();
                for (face_id = 0; face_id < count; face_id++)
                {
                    LLFace* facep = drawablep->getFace(face_id);
                    if (facep)
                    {
                        gPipeline.mHighlightFaces.push_back(facep);
                    }
                }
            }
        }
    }

    void renderParticleBeacons(LLDrawable* drawablep)
    {
        // Look for attachments, objects, etc.
        LLViewerObject* vobj = drawablep->getVObj();
        if (vobj && vobj->isParticleSource())
        {
            if (DXPipeline::sRenderBeacons)
            {
                LLColor4 light_blue(0.5f, 0.5f, 1.f, 0.5f);
                gObjectList.addDebugBeacon(vobj->getPositionAgent(), "", light_blue, LLColor4(1.f, 1.f, 1.f, 0.5f),
                    DXPipeline::DebugBeaconLineWidth);
            }

            if (DXPipeline::sRenderHighlight)
            {
                S32 face_id = 0;
                S32 count = drawablep->getNumFaces();
                for (face_id = 0; face_id < count; face_id++)
                {
                    LLFace* facep = drawablep->getFace(face_id);
                    if (facep)
                    {
                        gPipeline.mHighlightFaces.push_back(facep);
                    }
                }
            }
        }
    }

    void renderSoundHighlights(LLDrawable* drawablep)
    {
        // Look for attachments, objects, etc.
        LLViewerObject* vobj = drawablep->getVObj();
        if (vobj && vobj->isAudioSource())
        {
            if (DXPipeline::sRenderHighlight)
            {
                S32 face_id = 0;
                S32 count = drawablep->getNumFaces();
                for (face_id = 0; face_id < count; face_id++)
                {
                    LLFace* facep = drawablep->getFace(face_id);
                    if (facep)
                    {
                        gPipeline.mHighlightFaces.push_back(facep);
                    }
                }
            }
        }
    }
}

// ============================================================================
// State sort / post sort
// ============================================================================

void DXPipeline::stateSort(LLPipeline& pipeline, LLCamera& camera, LLCullResult& result)
{
    LL_RECORD_BLOCK_TIME(FTM_STATESORT);

    if (DXPipeline::hasAnyRenderType(pipeline, LLPipeline::RENDER_TYPE_AVATAR,
        LLPipeline::RENDER_TYPE_CONTROL_AV,
        LLPipeline::RENDER_TYPE_TERRAIN,
        LLPipeline::RENDER_TYPE_TREE,
        LLPipeline::RENDER_TYPE_SKY,
        LLPipeline::RENDER_TYPE_VOIDWATER,
        LLPipeline::RENDER_TYPE_WATER,
        LLPipeline::END_RENDER_TYPES))
    {
        //clear faces from face pools
        DXPipeline::resetDrawOrders(pipeline);
    }

    DXPipeline::grabReferences(pipeline, result);
    for (LLCullResult::sg_iterator iter = sCull->beginDrawableGroups(); iter != sCull->endDrawableGroups(); ++iter)
    {
        LLSpatialGroup* group = *iter;
        if (group->isDead())
        {
            continue;
        }
        group->checkOcclusion();
        if (DXPipeline::sUseOcclusion > 1 && group->isOcclusionState(LLSpatialGroup::OCCLUDED))
        {
            DXPipeline::markOccluder(pipeline, group);
        }
        else
        {
            group->setVisible();
            for (LLSpatialGroup::element_iter i = group->getDataBegin(); i != group->getDataEnd(); ++i)
            {
                LLDrawable* drawablep = (LLDrawable*)(*i)->getDrawable();
                DXPipeline::markVisible(pipeline, drawablep, camera);
            }

            { //rebuild mesh as soon as we know it's visible
                group->rebuildMesh();
            }
        }
    }

    if (LLViewerCamera::sCurCameraID == LLViewerCamera::CAMERA_WORLD && !gCubeSnapshot)
    {
        LLSpatialGroup* last_group = NULL;
        bool fov_changed = LLViewerCamera::getInstance()->isDefaultFOVChanged();
        for (LLCullResult::bridge_iterator i = sCull->beginVisibleBridge(); i != sCull->endVisibleBridge(); ++i)
        {
            LLCullResult::bridge_iterator cur_iter = i;
            LLSpatialBridge* bridge = *cur_iter;
            LLSpatialGroup* group = bridge->getSpatialGroup();

            if (last_group == NULL)
            {
                last_group = group;
            }

            if (!bridge->isDead() && group && !group->isOcclusionState(LLSpatialGroup::OCCLUDED))
            {
                DXPipeline::stateSort(pipeline, bridge, camera, fov_changed);
            }

            if (LLViewerCamera::sCurCameraID == LLViewerCamera::CAMERA_WORLD &&
                last_group != group && last_group->changeLOD())
            {
                last_group->mLastUpdateDistance = last_group->mDistance;
            }

            last_group = group;
        }

        if (LLViewerCamera::sCurCameraID == LLViewerCamera::CAMERA_WORLD &&
            last_group && last_group->changeLOD())
        {
            last_group->mLastUpdateDistance = last_group->mDistance;
        }
    }

    for (LLCullResult::sg_iterator iter = sCull->beginVisibleGroups(); iter != sCull->endVisibleGroups(); ++iter)
    {
        LLSpatialGroup* group = *iter;
        if (group->isDead())
        {
            continue;
        }
        group->checkOcclusion();
        if (DXPipeline::sUseOcclusion > 1 && group->isOcclusionState(LLSpatialGroup::OCCLUDED))
        {
            DXPipeline::markOccluder(pipeline, group);
        }
        else
        {
            group->setVisible();
            DXPipeline::stateSort(pipeline, group, camera);

            { //rebuild mesh as soon as we know it's visible
                group->rebuildMesh();
            }
        }
    }

    {
        LL_RECORD_BLOCK_TIME(FTM_STATESORT_DRAWABLE);
        for (LLCullResult::drawable_iterator iter = sCull->beginVisibleList();
            iter != sCull->endVisibleList(); ++iter)
        {
            LLDrawable* drawablep = *iter;
            if (!drawablep->isDead())
            {
                DXPipeline::stateSort(pipeline, drawablep, camera);
            }
        }
    }

    DXPipeline::postSort(pipeline, camera);
}

void DXPipeline::stateSort(LLPipeline& pipeline, LLSpatialGroup* group, LLCamera& camera)
{
    if (group->changeLOD())
    {
        for (LLSpatialGroup::element_iter i = group->getDataBegin(); i != group->getDataEnd(); ++i)
        {
            LLDrawable* drawablep = (LLDrawable*)(*i)->getDrawable();
            DXPipeline::stateSort(pipeline, drawablep, camera);
        }

        if (LLViewerCamera::sCurCameraID == LLViewerCamera::CAMERA_WORLD && !gCubeSnapshot)
        { //avoid redundant stateSort calls
            group->mLastUpdateDistance = group->mDistance;
        }
    }
}

void DXPipeline::stateSort(LLPipeline& pipeline, LLSpatialBridge* bridge, LLCamera& camera, bool fov_changed)
{
    if (bridge->getSpatialGroup()->changeLOD() || fov_changed)
    {
        bool force_update = false;
        bridge->updateDistance(camera, force_update);
    }
}

void DXPipeline::stateSort(LLPipeline& pipeline, LLDrawable* drawablep, LLCamera& camera)
{
    if (!drawablep
        || drawablep->isDead()
        || !DXPipeline::hasRenderType(pipeline, drawablep->getRenderType()))
    {
        return;
    }

    // SL-11353
    // ignore our own geo when rendering spotlight shadowmaps...
    //
    if (DXPipeline::RenderSpotLight && drawablep == DXPipeline::RenderSpotLight)
    {
        return;
    }

    if (LLSelectMgr::getInstance()->mHideSelectedObjects)
    {
        if (drawablep->getVObj().notNull() &&
            drawablep->getVObj()->isSelected())
        {
            return;
        }
    }

    if (drawablep->isAvatar())
    { //don't draw avatars beyond render distance or if we don't have a spatial group.
        if ((drawablep->getSpatialGroup() == NULL) ||
            (drawablep->getSpatialGroup()->mDistance > LLVOAvatar::sRenderDistance))
        {
            return;
        }

        LLVOAvatar* avatarp = (LLVOAvatar*)drawablep->getVObj().get();
        if (!avatarp->isVisible())
        {
            return;
        }
    }

    pipeline.assertInitialized();

    if (DXPipeline::hasRenderType(pipeline, drawablep->getRenderType()))
    {
        if (!drawablep->isState(LLDrawable::INVISIBLE | LLDrawable::FORCE_INVISIBLE))
        {
            drawablep->setVisible(camera, NULL, false);
        }
    }

    if (LLViewerCamera::sCurCameraID == LLViewerCamera::CAMERA_WORLD && !gCubeSnapshot)
    {
        //if (drawablep->isVisible()) isVisible() check here is redundant, if it wasn't visible, it wouldn't be here
        {
            if (!drawablep->isActive())
            {
                bool force_update = false;
                drawablep->updateDistance(camera, force_update);
            }
            else if (drawablep->isAvatar())
            {
                bool force_update = false;
                drawablep->updateDistance(camera, force_update); // calls vobj->updateLOD() which calls LLVOAvatar::updateVisibility()
            }
        }
    }

    if (!drawablep->getVOVolume())
    {
        for (LLDrawable::face_list_t::iterator iter = drawablep->getFaces().begin();
            iter != drawablep->getFaces().end(); iter++)
        {
            LLFace* facep = *iter;

            if (facep->hasGeometry())
            {
                if (facep->getPool())
                {
                    facep->getPool()->enqueue(facep);
                }
                else
                {
                    break;
                }
            }
        }
    }

    pipeline.mNumVisibleFaces += drawablep->getNumFaces();
}

void DXPipeline::forAllVisibleDrawables(LLPipeline& pipeline, void (*func)(LLDrawable*))
{
    forAllDrawables(sCull->beginDrawableGroups(), sCull->endDrawableGroups(), func);
    forAllDrawables(sCull->beginVisibleGroups(), sCull->endVisibleGroups(), func);
}

// S24 Version 2!
void DXPipeline::postSort(LLPipeline& pipeline, LLCamera& camera)
{
    LL_RECORD_BLOCK_TIME(FTM_POST_SORT); // S24: Critical - post-sort operations after state sort

    pipeline.assertInitialized();
    bool cube_snapshot = gCubeSnapshot;
    bool shadow_render = DXPipeline::sShadowRender;
    bool reflection_pass = DXPipeline::sReflectionRender;
    bool can_render_main = !shadow_render && !cube_snapshot;

    S32 occlusion_mode = DXPipeline::sUseOcclusion;
    bool use_occlusion = (occlusion_mode != 0);

    bool render_alpha = DXPipeline::hasRenderType(pipeline, LLPipeline::RENDER_TYPE_PASS_ALPHA);
    bool render_pool_alpha = DXPipeline::hasRenderType(pipeline, LLDrawPool::POOL_ALPHA);
    bool beacons_visible = LLFloaterReg::instanceVisible("beacons") && can_render_main;

    if (!cube_snapshot)
    {
        // rebuild drawable geometry
        for (LLCullResult::sg_iterator i = sCull->beginDrawableGroups(); i != sCull->endDrawableGroups(); ++i)
        {
            LLSpatialGroup* group = *i;
            if (group->isDead())
            {
                continue;
            }

            if (!use_occlusion || !group->isOcclusionState(LLSpatialGroup::OCCLUDED))
            {
                group->rebuildGeom();
            }
        }

        // rebuild groups
        sCull->assertDrawMapsEmpty();

        // S24: same per-frame time budget as updateGeom()/createObjects() (llviewerdisplay.cpp) -
        // see rebuildPriorityGroups()'s own comment for why this now matters.
        const F32 max_group_rebuild_time = 0.005f * 10.f * gFrameIntervalSeconds.value();
        DXPipeline::rebuildPriorityGroups(pipeline, max_group_rebuild_time);
    }

    // build render map
    for (LLCullResult::sg_iterator i = sCull->beginVisibleGroups(); i != sCull->endVisibleGroups(); ++i)
    {
        LLSpatialGroup* group = *i;

        if (group->isDead())
        {
            continue;
        }

        if ((use_occlusion && group->isOcclusionState(LLSpatialGroup::OCCLUDED)) ||
            (DXPipeline::RenderAutoHideSurfaceAreaLimit > 0.f &&
                group->mSurfaceArea > DXPipeline::RenderAutoHideSurfaceAreaLimit * llmax(group->mObjectBoxSize, 10.f)))
        {
            continue;
        }

        if (!cube_snapshot &&
            group->hasState(LLSpatialGroup::NEW_DRAWINFO) &&
            group->hasState(LLSpatialGroup::GEOM_DIRTY))
        {
            // no way this group is going to be drawable without a rebuild
            group->rebuildGeom();
        }

        // push drawinfos
        for (LLSpatialGroup::draw_map_t::iterator j = group->mDrawMap.begin(); j != group->mDrawMap.end(); ++j)
        {
            const U32 render_type = j->first;
            if (!DXPipeline::hasRenderType(pipeline, render_type))
            {
                continue;
            }

            LLSpatialGroup::drawmap_elem_t& src_vec = j->second;
            for (LLSpatialGroup::drawmap_elem_t::iterator k = src_vec.begin(); k != src_vec.end(); ++k)
            {
                LLDrawInfo* info = *k;

                sCull->pushDrawInfo(render_type, info);
                if (!shadow_render && !reflection_pass && !cube_snapshot)
                {
                    DXPipeline::addTrianglesDrawn(pipeline, info->mCount);
                }
            }
        }

        if (render_alpha)
        {
            // alpha groups (distance‑sorted)
            LLSpatialGroup::draw_map_t::iterator alpha_it =
                group->mDrawMap.find(LLRenderPass::PASS_ALPHA);

            if (alpha_it != group->mDrawMap.end())
            {
                if (LLViewerCamera::sCurCameraID == LLViewerCamera::CAMERA_WORLD && !cube_snapshot)
                {
                    if (LLSpatialBridge* bridge = group->getSpatialPartition()->asBridge())
                    {
                        LLCamera trans_camera = bridge->transformCamera(camera);
                        group->updateDistance(trans_camera);
                    }
                    else
                    {
                        group->updateDistance(camera);
                    }
                }

                if (render_pool_alpha)
                {
                    sCull->pushAlphaGroup(group);
                }
            }

            // rigged alpha groups (attachment‑order sorted, no distance update)
            LLSpatialGroup::draw_map_t::iterator rigged_alpha_it =
                group->mDrawMap.find(LLRenderPass::PASS_ALPHA_RIGGED);

            if (rigged_alpha_it != group->mDrawMap.end() && render_pool_alpha)
            {
                sCull->pushRiggedAlphaGroup(group);
            }
        }
    }

    // pack vertex buffers for groups that chose to delay their updates
    {
        for (LLSpatialGroup::sg_vector_t::iterator iter = pipeline.mMeshDirtyGroup.begin();
            iter != pipeline.mMeshDirtyGroup.end(); ++iter)
        {
            (*iter)->rebuildMesh();
        }
    }

    pipeline.mMeshDirtyGroup.clear();

    if (!shadow_render)
    {
        // order alpha groups by distance
        std::sort(sCull->beginAlphaGroups(), sCull->endAlphaGroups(),
            LLSpatialGroup::CompareDepthGreater());

        // order rigged alpha groups by avatar attachment order
        std::sort(sCull->beginRiggedAlphaGroups(), sCull->endRiggedAlphaGroups(),
            LLSpatialGroup::CompareRenderOrder());
    }

    // only render if the flag is set. The flag is only set if we are in edit mode or the toggle is set in the menus
    if (beacons_visible)
    {
        if (DXPipeline::sRenderScriptedTouchBeacons)
        {
            // Only show the beacon on the root object.
            DXPipeline::forAllVisibleDrawables(pipeline, renderScriptedTouchBeacons);
        }
        else if (DXPipeline::sRenderScriptedBeacons)
        {
            // Only show the beacon on the root object.
            DXPipeline::forAllVisibleDrawables(pipeline, renderScriptedBeacons);
        }

        if (DXPipeline::sRenderPhysicalBeacons)
        {
            // Only show the beacon on the root object.
            DXPipeline::forAllVisibleDrawables(pipeline, renderPhysicalBeacons);
        }

        if (DXPipeline::sRenderMOAPBeacons)
        {
            DXPipeline::forAllVisibleDrawables(pipeline, renderMOAPBeacons);
        }

        if (DXPipeline::sRenderParticleBeacons)
        {
            DXPipeline::forAllVisibleDrawables(pipeline, renderParticleBeacons);
        }

        // If god mode, also show audio cues
        if (DXPipeline::sRenderSoundBeacons && gAudiop)
        {
            // Walk all sound sources and render out beacons for them. Note, this isn't done in the ForAllVisibleDrawables function, because
            for (auto iter = gAudiop->getAllSources().begin();
                iter != gAudiop->getAllSources().end(); ++iter)
            {
                LLAudioSource* sourcep = iter->second;

                LLVector3d pos_global = sourcep->getPositionGlobal();
                LLVector3  pos = gAgent.getPosAgentFromGlobal(pos_global);
                if (DXPipeline::sRenderBeacons)
                {
                    gObjectList.addDebugBeacon(
                        pos,
                        "",
                        LLColor4(1.f, 1.f, 0.f, 0.5f),
                        LLColor4(1.f, 1.f, 1.f, 0.5f),
                        DXPipeline::DebugBeaconLineWidth);
                }
            }

            // now deal with highlights for all those seeable sound sources
            DXPipeline::forAllVisibleDrawables(pipeline, renderSoundHighlights);
        }
    }

    // If managing your telehub, draw beacons at telehub and currently selected spawnpoint.
    if (LLFloaterTelehub::renderBeacons() && can_render_main)
    {
        LLFloaterTelehub::addBeacons();
    }

    if (can_render_main)
    {
        pipeline.mSelectedFaces.clear();

        bool tex_index_changed = false;
        if (!gNonInteractive)
        {
            LLRender::eTexIndex tex_index = DXPipeline::sRenderHighlightTextureChannel;
            DXPipeline::setRenderHighlightTextureChannel(
                gFloaterTools->getPanelFace()->getTextureChannelToEdit());
            tex_index_changed = (DXPipeline::sRenderHighlightTextureChannel != tex_index);
        }

        // Draw face highlights for selected faces.
        if (LLSelectMgr::getInstance()->getTEMode())
        {
            struct FaceCollector : public LLSelectedTEFunctor
            {
                virtual bool apply(LLViewerObject* object, S32 te)
                {
                    if (object->mDrawable)
                    {
                        if (LLFace* facep = object->mDrawable->getFace(te))
                        {
                            gPipeline.mSelectedFaces.push_back(facep);
                        }
                    }
                    return true;
                }
            } func;

            LLSelectMgr::getInstance()->getSelection()->applyToTEs(&func);

            if (tex_index_changed)
            {
                // Rebuild geometry for all selected faces with PBR textures
                for (const LLFace* face : pipeline.mSelectedFaces)
                {
                    if (const LLViewerObject* vobj = face->getViewerObject())
                    {
                        if (const LLTextureEntry* tep = vobj->getTE(face->getTEOffset()))
                        {
                            if (tep->getGLTFRenderMaterial())
                            {
                                DXPipeline::markRebuild(pipeline, face->getDrawable(), LLDrawable::REBUILD_VOLUME);
                            }
                        }
                    }
                }
            }
        }
    }

    LLVertexBuffer::flushBuffers();
}

// ============================================================================
// Small standalone utilities
// ============================================================================

bool DXPipeline::shadersLoaded(LLPipeline& pipeline)
{
    return (pipeline.assertInitialized() && pipeline.mShadersLoaded);
}

bool DXPipeline::canUseWindLightShaders(const LLPipeline& pipeline)
{
    return true;
}

bool DXPipeline::canUseAntiAliasing(const LLPipeline& pipeline)
{
    return true;
}

void DXPipeline::unloadShaders(LLPipeline& pipeline)
{
    LLViewerShaderMgr::instance()->unloadShaders();
    pipeline.mShadersLoaded = false;
}

// static
U32 DXPipeline::getPoolTypeFromTE(const LLTextureEntry* te, LLViewerTexture* imagep)
{
    if (!te || !imagep)
    {
        return 0;
    }

    LLMaterial* mat = te->getMaterialParams().get();
    LLGLTFMaterial* gltf_mat = te->getGLTFRenderMaterial();

    bool color_alpha = te->getColor().mV[3] < 0.999f;
    bool alpha = color_alpha;
    if (imagep)
    {
        alpha = alpha || (imagep->getComponents() == 4 && imagep->getType() != LLViewerTexture::MEDIA_TEXTURE) || (imagep->getComponents() == 2);
    }

    if (alpha && mat)
    {
        switch (mat->getDiffuseAlphaMode())
        {
        case 1:
            alpha = true; // Material's alpha mode is set to blend.  Toss it into the alpha draw pool.
            break;
        case 0: //alpha mode set to none, never go to alpha pool
        case 3: //alpha mode set to emissive, never go to alpha pool
            alpha = color_alpha;
            break;
        default: //alpha mode set to "mask", go to alpha pool if fullbright
            alpha = color_alpha; // Material's alpha mode is set to none, mask, or emissive.  Toss it into the opaque material draw pool.
            break;
        }
    }

    if (alpha || (gltf_mat && gltf_mat->mAlphaMode == LLGLTFMaterial::ALPHA_MODE_BLEND))
    {
        return LLDrawPool::POOL_ALPHA;
    }
    else if ((te->getBumpmap() || te->getShiny()) && (!mat || mat->getNormalID().isNull()))
    {
        return LLDrawPool::POOL_BUMP;
    }
    else if (gltf_mat)
    {
        return LLDrawPool::POOL_GLTF_PBR;
    }
    else if (mat && !alpha)
    {
        return LLDrawPool::POOL_MATERIALS;
    }
    else
    {
        return LLDrawPool::POOL_SIMPLE;
    }
}

// static
void DXPipeline::removeMutedAVsLights(LLVOAvatar* muted_avatar)
{
    LLPipeline::light_set_t::iterator iter = gPipeline.getNearbyLightsMutable().begin();
    while (iter != gPipeline.getNearbyLightsMutable().end())
    {
        const LLViewerObject* vobj = iter->drawable->getVObj();
        if (vobj
            && vobj->getAvatar()
            && vobj->isAttachment()
            && vobj->getAvatar() == muted_avatar)
        {
            gPipeline.getLightsMutable().erase(iter->drawable);
            iter = gPipeline.getNearbyLightsMutable().erase(iter);
        }
        else
        {
            iter++;
        }
    }
}

void DXPipeline::resetFrameStats(LLPipeline& pipeline)
{
    pipeline.assertInitialized();

    DXPipeline::sCompiles = 0;
    pipeline.mNumVisibleFaces = 0;

    if (pipeline.getOldRenderDebugMaskMutable() != pipeline.getRenderDebugMaskMutable())
    {
        gObjectList.clearDebugText();
        pipeline.getOldRenderDebugMaskMutable() = pipeline.getRenderDebugMaskMutable();
    }
}

// static
void DXPipeline::updateRenderTransparentWater()
{
    DXPipeline::sRenderTransparentWater = gSavedSettings.getBOOL("RenderTransparentWater");
}

bool DXPipeline::getVisibleExtents(LLPipeline& pipeline, LLCamera& camera, LLVector3& min, LLVector3& max)
{
    const F32 X = 65536.f;

    min = LLVector3(X, X, X);
    max = LLVector3(-X, -X, -X);

    LLViewerCamera::eCameraID saved_camera_id = LLViewerCamera::sCurCameraID;
    LLViewerCamera::sCurCameraID = LLViewerCamera::CAMERA_WORLD;

    bool res = true;

    for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
        iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
    {
        LLViewerRegion* region = *iter;

        for (U32 i = 0; i < LLViewerRegion::NUM_PARTITIONS; i++)
        {
            LLSpatialPartition* part = region->getSpatialPartition(i);
            if (part)
            {
                if (DXPipeline::hasRenderType(pipeline, part->mDrawableType))
                {
                    if (!part->getVisibleExtents(camera, min, max))
                    {
                        res = false;
                    }
                }
            }
        }
    }

    LLViewerCamera::sCurCameraID = saved_camera_id;
    return res;
}

// static
std::string DXPipeline::getBrdfLutAssetPath()
{
    return gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, "luts", "brdflut_512.bin");
}

// ============================================================================
// Move-list updates and the per-frame cull-result builder
// ============================================================================

void DXPipeline::updateMoveDampedAsync(LLPipeline& pipeline, LLDrawable* drawablep)
{
    if (sTLSDeferredPipelineMarks)
    {
        LLPipeline* pPipeline = &pipeline;
        sTLSDeferredPipelineMarks->mActions.push_back([pPipeline, drawablep] { DXPipeline::updateMoveDampedAsync(*pPipeline, drawablep); });
        return;
    }

    if (DXPipeline::FreezeTime)
    {
        return;
    }
    if (!drawablep)
    {
        LL_ERRS() << "updateMove called with NULL drawablep" << LL_ENDL;
        return;
    }
    if (drawablep->isState(LLDrawable::EARLY_MOVE))
    {
        return;
    }

    pipeline.assertInitialized();

    // update drawable now
    drawablep->clearState(LLDrawable::MOVE_UNDAMPED); // force to DAMPED
    drawablep->updateMove(); // returns done
    drawablep->setState(LLDrawable::EARLY_MOVE); // flag says we already did an undamped move this frame
    // Put on move list so that EARLY_MOVE gets cleared
    if (!drawablep->isState(LLDrawable::ON_MOVE_LIST))
    {
        pipeline.mMovedList.push_back(drawablep);
        drawablep->setState(LLDrawable::ON_MOVE_LIST);
    }
}

void DXPipeline::updateMoveNormalAsync(LLPipeline& pipeline, LLDrawable* drawablep)
{
    if (sTLSDeferredPipelineMarks)
    {
        LLPipeline* pPipeline = &pipeline;
        sTLSDeferredPipelineMarks->mActions.push_back([pPipeline, drawablep] { DXPipeline::updateMoveNormalAsync(*pPipeline, drawablep); });
        return;
    }

    if (DXPipeline::FreezeTime)
    {
        return;
    }
    if (!drawablep)
    {
        LL_ERRS() << "updateMove called with NULL drawablep" << LL_ENDL;
        return;
    }
    if (drawablep->isState(LLDrawable::EARLY_MOVE))
    {
        return;
    }

    pipeline.assertInitialized();

    // update drawable now
    drawablep->setState(LLDrawable::MOVE_UNDAMPED); // force to UNDAMPED
    drawablep->updateMove();
    drawablep->setState(LLDrawable::EARLY_MOVE); // flag says we already did an undamped move this frame
    // Put on move list so that EARLY_MOVE gets cleared
    if (!drawablep->isState(LLDrawable::ON_MOVE_LIST))
    {
        pipeline.mMovedList.push_back(drawablep);
        drawablep->setState(LLDrawable::ON_MOVE_LIST);
    }
}

void DXPipeline::updateCull(LLPipeline& pipeline, LLCamera& camera, LLCullResult& result)
{
    LL_RECORD_BLOCK_TIME(FTM_CULL);

    bool water_clip = DXPipeline::isWaterClip();

    if (water_clip)
    {
        LLVector3 pnorm;

        F32 water_height = LLEnvironment::instance().getWaterHeight();

        if (DXPipeline::sUnderWaterRender)
        {
            //camera is below water, cull above water
            pnorm.setVec(0, 0, 1);
        }
        else
        {
            //camera is above water, cull below water
            pnorm = LLVector3(0, 0, -1);
        }

        LLPlane plane(LLVector3(0, 0, water_height), pnorm);

        camera.setUserClipPlane(plane);
    }
    else
    {
        camera.disableUserClipPlane();
    }

    DXPipeline::grabReferences(pipeline, result);

    sCull->clear();

    for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
        iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
    {
        LLViewerRegion* region = *iter;

        for (U32 i = 0; i < LLViewerRegion::NUM_PARTITIONS; i++)
        {
            LLSpatialPartition* part = region->getSpatialPartition(i);
            if (part)
            {
                if (DXPipeline::hasRenderType(pipeline, part->mDrawableType))
                {
                    part->cull(camera);
                }
            }
        }

        //scan the VO Cache tree
        LLVOCachePartition* vo_part = region->getVOCachePartition();
        if (vo_part)
        {
            vo_part->cull(camera, DXPipeline::sUseOcclusion > 0);
        }
    }

    if (DXPipeline::hasRenderType(pipeline, LLPipeline::RENDER_TYPE_SKY) &&
        gSky.mVOSkyp.notNull() &&
        gSky.mVOSkyp->mDrawable.notNull())
    {
        gSky.mVOSkyp->mDrawable->setVisible(camera);
        sCull->pushDrawable(gSky.mVOSkyp->mDrawable);
        gSky.updateCull();
    }

    if (DXPipeline::hasRenderType(pipeline, LLPipeline::RENDER_TYPE_WL_SKY) &&
        DXPipeline::canUseWindLightShaders(pipeline) &&
        gSky.mVOWLSkyp.notNull() &&
        gSky.mVOWLSkyp->mDrawable.notNull())
    {
        gSky.mVOWLSkyp->mDrawable->setVisible(camera);
        sCull->pushDrawable(gSky.mVOWLSkyp->mDrawable);
    }
}

// ============================================================================
// Screen/shadow buffer allocate, resize, release
// ============================================================================

void DXPipeline::requestResizeScreenTexture(LLPipeline& pipeline)
{
    gResizeScreenTexture = true;
}

void DXPipeline::requestResizeShadowTexture(LLPipeline& pipeline)
{
    gResizeShadowTexture = true;
}

void DXPipeline::resizeShadowTexture(LLPipeline& pipeline)
{
    DXPipeline::releaseSunShadowTargets(pipeline);
    DXPipeline::releaseSpotShadowTargets(pipeline);
    DXPipeline::allocateShadowBuffer(pipeline, pipeline.mRT->width, pipeline.mRT->height);
    gResizeShadowTexture = false;
}

void DXPipeline::resizeScreenTexture(LLPipeline& pipeline)
{
    if (DXPipeline::shadersLoaded(pipeline))
    {
        GLuint resX = gViewerWindow->getWorldViewWidthRaw();
        GLuint resY = gViewerWindow->getWorldViewHeightRaw();

        if (gResizeScreenTexture || (resX != pipeline.mRT->screen.getWidth()) || (resY != pipeline.mRT->screen.getHeight()))
        {
            DXPipeline::releaseScreenBuffers(pipeline);
            DXPipeline::releaseSunShadowTargets(pipeline);
            DXPipeline::releaseSpotShadowTargets(pipeline);
            DXPipeline::allocateScreenBuffer(pipeline, resX, resY);
            gResizeScreenTexture = false;
        }
    }
}

bool DXPipeline::allocateScreenBuffer(LLPipeline& pipeline, U32 resX, U32 resY)
{
    int ret = DXPipeline::doAllocateScreenBuffer(pipeline, resX, resY);

    return ret == LLPipeline::FBO_SUCCESS_FULLRES;
}

int DXPipeline::doAllocateScreenBuffer(LLPipeline& pipeline, U32 resX, U32 resY)
{
    // try to allocate screen buffers at requested resolution and samples
    // - on failure, shrink number of samples and try again
    // - if not multisampled, shrink resolution and try again (favor X resolution over Y)
    // Make sure to call "releaseScreenBuffers" after each failure to cleanup the partially loaded state

    // refresh cached settings here to protect against inconsistent event handling order
    DXPipeline::refreshCachedSettings();

    LLPipeline::eFBOStatus ret = LLPipeline::FBO_SUCCESS_FULLRES;
    if (!DXPipeline::allocateScreenBufferInternal(pipeline, resX, resY))
    {
        //failed to allocate at requested specification, return false
        ret = LLPipeline::FBO_FAILURE;

        DXPipeline::releaseScreenBuffers(pipeline);

        //reduce resolution
        while (resY > 0 && resX > 0)
        {
            resY /= 2;
            if (DXPipeline::allocateScreenBufferInternal(pipeline, resX, resY))
            {
                return LLPipeline::FBO_SUCCESS_LOWRES;
            }
            DXPipeline::releaseScreenBuffers(pipeline);

            resX /= 2;
            if (DXPipeline::allocateScreenBufferInternal(pipeline, resX, resY))
            {
                return LLPipeline::FBO_SUCCESS_LOWRES;
            }
            DXPipeline::releaseScreenBuffers(pipeline);
        }

        LL_WARNS() << "Unable to allocate screen buffer at any resolution!" << LL_ENDL;
    }

    return ret;
}

bool DXPipeline::allocateScreenBufferInternal(LLPipeline& pipeline, U32 resX, U32 resY)
{
    static LLCachedControl<bool> has_hdr(gSavedSettings, "RenderHDREnabled", true);
    // Same reasoning as addDeferredAttachments() above (gGLManager.mGLVersion is a hardcoded synthetic
    // value under DX_RENDER, not a real capability query) - this copy also drives screenFormat for
    // mRT->screen/deferredLight/mWaterDis/mSceneMap and whether deferredLight gets allocated at all.
    bool hdr = has_hdr();

    if (pipeline.mRT == &pipeline.mMainRT)
    { // hacky -- allocate auxillary buffer
        gCubeSnapshot = true;

        if (DXPipeline::sReflectionProbesEnabled)
        {
            pipeline.mSphereProbes.initReflectionMaps();
        }

        pipeline.mRT = &pipeline.mAuxillaryRT;
        U32 res = pipeline.mSphereProbes.getProbeResolution() * 4;  //multiply by 4 because probes will be 16x super sampled
        DXPipeline::allocateScreenBufferInternal(pipeline, res, res);

        if (DXPipeline::RenderMirrors)
        {
            pipeline.mMirrorProbes.initReflectionMaps();
            res = pipeline.mMirrorProbes.getProbeResolution();  // We also scale the hero probe RT to the probe res since we don't super sample it.
            pipeline.mRT = &pipeline.mHeroProbeRT;
            DXPipeline::allocateScreenBufferInternal(pipeline, res, res);
        }

        pipeline.mRT = &pipeline.mMainRT;
        gCubeSnapshot = false;
    }

    // remember these dimensions
    pipeline.mRT->width = resX;
    pipeline.mRT->height = resY;

    // S24: percentage-of-native render scale, replacing the old integer
    // RenderResolutionDivisor. Below 100, DXPipeline::presentFinal() auto-
    // engages FSR1 EASU upscale.
    static LLCachedControl<F32> render_scale_pct(gSavedSettings, "RenderScalePercent", 100.0f);
    F32 scale = llclamp(render_scale_pct(), 25.0f, 100.0f) / 100.0f;

    if (scale < 1.0f)
    {
        resX = (U32)(resX * scale);
        resY = (U32)(resY * scale);
    }

    S32 shadow_detail = DXPipeline::RenderShadowDetail;
    bool ssao = DXPipeline::RenderDeferredSSAO;

    //allocate deferred rendering color buffers
    if (!pipeline.mRT->deferredScreen.allocate(resX, resY, GL_RGBA, true)) return false;
    if (!addDeferredAttachments(pipeline.mRT->deferredScreen)) return false;

    GLuint screenFormat = hdr ? GL_RGBA16F : GL_RGBA;

    if (!pipeline.mRT->screen.allocate(resX, resY, GL_RGBA16F)) return false;

    pipeline.mRT->deferredScreen.shareDepthBuffer(pipeline.mRT->screen);

    if (hdr || shadow_detail > 0 || ssao || DXPipeline::RenderDepthOfField)
    { //only need mRT->deferredLight for shadows OR ssao OR dof
        if (!pipeline.mRT->deferredLight.allocate(resX, resY, screenFormat)) return false;
    }
    else
    {
        pipeline.mRT->deferredLight.release();
    }

    DXPipeline::allocateShadowBuffer(pipeline, resX, resY);

    if (!gCubeSnapshot) // hack to not re-allocate various targets for cube snapshots
    {
        if (DXPipeline::RenderUIBuffer)
        {
            if (!pipeline.mUIScreen.allocate(resX, resY, GL_RGBA))
            {
                return false;
            }
        }

        if (DXPipeline::RenderFSAAType > 0)
        {
            if (!pipeline.mFXAAMap.allocate(resX, resY, GL_RGBA)) return false;
            if (DXPipeline::RenderFSAAType == 2)
            {
                if (!pipeline.mSMAABlendBuffer.allocate(resX, resY, GL_RGBA, false)) return false;
            }
        }
        else
        {
            pipeline.mFXAAMap.release();
            pipeline.mSMAABlendBuffer.release();
        }

        //water reflection texture (always needed as scratch space whether or not transparent water is enabled)
        pipeline.mWaterDis.allocate(resX, resY, screenFormat, true);

        if (DXPipeline::RenderScreenSpaceReflections)
        {
            pipeline.mSceneMap.allocate(resX, resY, screenFormat, true);
            pipeline.mSSRBuffer.allocate(resX, resY, screenFormat, false);
            pipeline.mSSRResolved[0].allocate(resX, resY, screenFormat, false);
            pipeline.mSSRResolved[1].allocate(resX, resY, screenFormat, false);
            pipeline.mSSRHistoryValid = false;
        }
        else
        {
            pipeline.mSceneMap.release();
            pipeline.mSSRBuffer.release();
            pipeline.mSSRResolved[0].release();
            pipeline.mSSRResolved[1].release();
        }

        pipeline.mPostPingMap.allocate(resX, resY, GL_RGBA);
        pipeline.mPostPongMap.allocate(resX, resY, GL_RGBA);

        // Anaglyph 3D rewrite - see these members' own comment in pipeline.h.
        pipeline.mStereoEyeL.allocate(resX, resY, GL_RGBA);
        pipeline.mStereoEyeR.allocate(resX, resY, GL_RGBA);

        // Persistent AO history buffer, same format as deferredLight since it's a full-channel copy of
        // it. Seeded to (1,1,1,1) ("fully lit / no occlusion") immediately after allocation, matching
        // mExposureMap's own seeding and the "SSAO off" neutral fallback used for deferred_light_target,
        // so the first frame's blend (real AO vs. neutral history) needs no first-frame flag.
        // blit - see that function for the precedent this mirrors.
        if (ssao)
        {
            pipeline.mSSAOHistory.allocate(resX, resY, screenFormat);
            pipeline.mSSAOHistory.bindTarget();
            pipeline.mSSAOHistory.clearColor(1.f, 1.f, 1.f, 1.f);
            pipeline.mSSAOHistory.flush();
        }
        else
        {
            pipeline.mSSAOHistory.release();
        }

        // The water exclusion mask needs its own depth buffer so we can take care of the problem of multiple water planes.
        // Should we ever make water not just a plane, it also aids with that as well as the water planes will be rendered into the mask.
        // Why do we do this? Because it saves us some janky logic in the exclusion shader when we generate the mask.
        // Regardless, this should always only be an R8 texture unless we choose to start having multiple kinds of exclusion that 8 bits can't handle.
        // - Geenz 2025-02-06
        bool success = pipeline.mWaterExclusionMask.allocate(resX, resY, GL_R8, true);

        assert(success);

        // used to scale down textures
        // See LLViwerTextureList::updateImagesCreateTextures and LLImageDX::scaleDown
        pipeline.mDownResMap.allocate(1024, 1024, GL_RGBA);

        pipeline.mBakeMap.allocate(LLAvatarAppearanceDefines::SCRATCH_TEX_WIDTH, LLAvatarAppearanceDefines::SCRATCH_TEX_HEIGHT, GL_RGBA);
    }
    //HACK make screenbuffer allocations start failing after 30 seconds
    if (gSavedSettings.getBOOL("SimulateFBOFailure"))
    {
        return false;
    }

    gDX.getTexUnit(0)->disable();

    return true;
}

// must be even to avoid a stripe in the horizontal shadow blur
inline U32 BlurHappySize(U32 x, F32 scale) { return U32(x * scale + 16.0f) & ~0xF; }

bool DXPipeline::allocateShadowBuffer(LLPipeline& pipeline, U32 resX, U32 resY)
{
    S32 shadow_detail = DXPipeline::RenderShadowDetail;

    F32 scale = gCubeSnapshot ? 1.0f : llmax(0.f, DXPipeline::RenderShadowResolutionScale); // Don't scale probe shadow maps
    U32 sun_shadow_map_width = BlurHappySize(resX, scale);
    U32 sun_shadow_map_height = BlurHappySize(resY, scale);

    if (shadow_detail > 0)
    { //allocate 4 sun shadow maps
        for (U32 i = 0; i < 4; i++)
        {
            if (!pipeline.mRT->shadow[i].allocate(sun_shadow_map_width, sun_shadow_map_height, 0, true))
            {
                return false;
            }
        }
    }
    else
    {
        for (U32 i = 0; i < 4; i++)
        {
            DXPipeline::releaseSunShadowTarget(pipeline, i);
        }
    }

    if (!gCubeSnapshot) // hack to not allocate spot shadow maps during ReflectionMapManager init
    {
        U32 width = (U32)(resX * scale);
        U32 height = width;

        if (shadow_detail > 1)
        { //allocate two spot shadow maps
            U32 spot_shadow_map_width = width;
            U32 spot_shadow_map_height = height;
            for (U32 i = 0; i < 2; i++)
            {
                if (!pipeline.mSpotShadow[i].allocate(spot_shadow_map_width, spot_shadow_map_height, 0, true))
                {
                    return false;
                }
            }
        }
        else
        {
            DXPipeline::releaseSpotShadowTargets(pipeline);
        }
    }

    return true;
}

void DXPipeline::releaseGLBuffers(LLPipeline& pipeline)
{
    pipeline.assertInitialized();

    if (pipeline.mNoiseMap)
    {
        LLImageDX::deleteTextures(1, &pipeline.mNoiseMap);
        pipeline.mNoiseMap = 0;
    }

    if (pipeline.mTrueNoiseMap)
    {
        LLImageDX::deleteTextures(1, &pipeline.mTrueNoiseMap);
        pipeline.mTrueNoiseMap = 0;
    }

    if (pipeline.mSMAAAreaMap)
    {
        LLImageDX::deleteTextures(1, &pipeline.mSMAAAreaMap);
        pipeline.mSMAAAreaMap = 0;
    }

    if (pipeline.mSMAASearchMap)
    {
        LLImageDX::deleteTextures(1, &pipeline.mSMAASearchMap);
        pipeline.mSMAASearchMap = 0;
    }

    DXPipeline::releaseLUTBuffers(pipeline);

    pipeline.mWaterDis.release();

    pipeline.mSceneMap.release();
    pipeline.mSSRBuffer.release();
    pipeline.mSSRResolved[0].release();
    pipeline.mSSRResolved[1].release();

    pipeline.mWaterExclusionMask.release();

    pipeline.mPostPingMap.release();
    pipeline.mPostPongMap.release();

    pipeline.mStereoEyeL.release();
    pipeline.mStereoEyeR.release();
    pipeline.mSSAOHistory.release();

    // Same reasoning as mPostPingMap/mPostPongMap above: releasing it only in
    // releaseScreenBuffers() (resize path) and not here (full teardown) leaves it holding live
    // GPU resource references through D3D11 device destruction at viewer shutdown, causing a
    // "device removed" crash at quit.
    pipeline.mFSR1EasuBuffer.release();

    pipeline.mFXAAMap.release();

    pipeline.mUIScreen.release();

    pipeline.mDownResMap.release();

    pipeline.mBakeMap.release();

    for (U32 i = 0; i < 3; i++)
    {
        pipeline.mGlow[i].release();
    }

    pipeline.mMirrorProbes.cleanup(); // release hero probes

    DXPipeline::releaseScreenBuffers(pipeline);

    gBumpImageList.destroyGL();
    LLVOAvatar::resetImpostors();
}

void DXPipeline::releaseLUTBuffers(LLPipeline& pipeline)
{
    if (pipeline.mLightFunc)
    {
        pipeline.mDXLightFunc.destroy();
        pipeline.mLightFunc = 0;
    }

    pipeline.mPbrBrdfLut.release();

    pipeline.mExposureMap.release();
    pipeline.mLuminanceMap.release();
    pipeline.mLastExposure.release();
}

void DXPipeline::releaseShadowBuffers(LLPipeline& pipeline)
{
    DXPipeline::releaseSunShadowTargets(pipeline);
    DXPipeline::releaseSpotShadowTargets(pipeline);
}

void DXPipeline::releaseScreenBuffers(LLPipeline& pipeline)
{
    pipeline.mRT->screen.release();
    pipeline.mRT->deferredScreen.release();
    pipeline.mRT->deferredLight.release();

    pipeline.mAuxillaryRT.screen.release();
    pipeline.mAuxillaryRT.deferredScreen.release();
    pipeline.mAuxillaryRT.deferredLight.release();

    pipeline.mHeroProbeRT.screen.release();
    pipeline.mHeroProbeRT.deferredScreen.release();
    pipeline.mHeroProbeRT.deferredLight.release();

    // Unlike mPostPingMap/mPostPongMap/mStereoEyeL/R above (which don't need
    // releasing here because allocateScreenBufferInternal() unconditionally
    // reallocates them every call), mFSR1EasuBuffer is sized off the real
    // destination resolution, not resX/resY, so it's lazily allocated/resized
    // separately in DXPipeline::presentFinal() and never touched by that
    // function - must be released explicitly here instead, both so a stale
    // mismatched-size buffer doesn't survive a resize, and so its VRAM is
    // freed before doAllocateScreenBuffer()'s retry-at-smaller-resolution
    // loop (which this is called from) has to compete for memory.
    pipeline.mFSR1EasuBuffer.release();
}

void DXPipeline::releaseSunShadowTarget(LLPipeline& pipeline, U32 index)
{
    llassert(index < 4);
    pipeline.mRT->shadow[index].release();
}

void DXPipeline::releaseSunShadowTargets(LLPipeline& pipeline)
{
    for (U32 i = 0; i < 4; i++)
    {
        DXPipeline::releaseSunShadowTarget(pipeline, i);
    }
}

void DXPipeline::releaseSpotShadowTargets(LLPipeline& pipeline)
{
    if (!gCubeSnapshot) // hack to avoid freeing spot shadows during ReflectionMapManager init
    {
        for (U32 i = 0; i < 2; i++)
        {
            pipeline.mSpotShadow[i].release();
        }
    }
}

F32 lerpf(F32 a, F32 b, F32 w)
{
    return a + w * (b - a);
}

void DXPipeline::createGLBuffers(LLPipeline& pipeline)
{
    pipeline.assertInitialized();

    GLuint resX = gViewerWindow->getWorldViewWidthRaw();
    GLuint resY = gViewerWindow->getWorldViewHeightRaw();

    // allocate screen space glow buffers
    const U32 glow_res = llmax(1, llmin(512, 1 << gSavedSettings.getS32("RenderGlowResolutionPow")));
    const bool glow_hdr = gSavedSettings.getBOOL("RenderGlowHDR");
    const U32 glow_color_fmt = glow_hdr ? GL_RGBA16F : GL_RGBA;
    for (U32 i = 0; i < 3; i++)
    {
        pipeline.mGlow[i].allocate(512, glow_res, glow_color_fmt);
    }

    DXPipeline::allocateScreenBuffer(pipeline, resX, resY);
    // Do not zero out mRT dimensions here. allocateScreenBuffer() above
    // already sets the correct dimensions. Zeroing them caused resizeShadowTexture()
    // to fail if called immediately after createGLBuffers (e.g., post graphics change).
    // mRT->width = 0;
    // mRT->height = 0;

    if (!pipeline.mNoiseMap)
    {
        const U32 noiseRes = 128;
        LLVector3 noise[noiseRes * noiseRes] = {};

        F32 scaler = gSavedSettings.getF32("RenderDeferredNoise") / 100.f;
        for (U32 i = 0; i < noiseRes * noiseRes; ++i)
        {
            noise[i] = LLVector3(ll_frand() - 0.5f, ll_frand() - 0.5f, 0.f);
            noise[i].normVec();
            noise[i].mV[2] = ll_frand() * scaler + 1.f - scaler / 2.f;
        }

        // Direct DXTexture upload instead of generateTextures()+bindManual()+setManualImage() - that
        // ambient-GL-state trio has no DX_RENDER translation. createFloat() (not create()) because the
        // Z component here is a scale factor centered around 1.0, not a normalized [-1,1] direction -
        // quantizing to 8-bit UNORM would clip it.
        // mNoiseMap itself stays a non-zero sentinel purely so the
        // `if (!mNoiseMap)` guard above still detects "already created" on
        // a later call - never a real GL name under DX_RENDER.
        pipeline.mDXNoiseMap.createFloat(&noise[0].mV[0], noiseRes, noiseRes, 3);
        pipeline.mNoiseMap = 1;
    }

    if (!pipeline.mTrueNoiseMap)
    {
        const U32 noiseRes = 128;
        F32 noise[noiseRes * noiseRes * 3] = {};
        for (U32 i = 0; i < noiseRes * noiseRes * 3; i++)
        {
            noise[i] = ll_frand() * 2.0f - 1.0f;
        }

        pipeline.mDXTrueNoiseMap.createFloat(noise, noiseRes, noiseRes, 3);
        pipeline.mTrueNoiseMap = 1;
    }

    if (!pipeline.mSMAAAreaMap)
    {
        // The row-flip below (srcY = HEIGHT-1-y) converts the reference SMAA data (top-to-bottom -
        // iryoku/SMAA's reference implementation is itself a DirectX demo) into GL's bottom-to-top
        // upload convention. DXTexture::create() does no internal row-reversal, so it wants that same
        // top-to-bottom order - upload areaTexBytes directly, unflipped, not GL's flipped tempBuffer.
        // raw_channels=true: AreaTex is two independent channels (sampled as .rg by
        // SMAABlendWeightsF.hlsl), not luminance-alpha - the default 2-component repack would duplicate
        // channel 0 into both R and G and drop the real second channel into unused alpha.
        pipeline.mDXSMAAAreaMap.create(areaTexBytes, AREATEX_WIDTH, AREATEX_HEIGHT, 2, false, false, false, true);
        pipeline.mSMAAAreaMap = 1;
    }
    if (!pipeline.mSMAASearchMap)
    {
        // Same row-order fix as mSMAAAreaMap above.
        pipeline.mDXSMAASearchMap.create(searchTexBytes, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 1);
        pipeline.mSMAASearchMap = 1;
    }
    if (!pipeline.mSMAASampleMap)
    {
        LLPointer<LLImageRaw>               raw_image = new LLImageRaw;
        LLPointer<LLImagePNG>               png_image = new LLImagePNG;
        static LLCachedControl<std::string> sample_path(gSavedSettings, "SamplePath", "");
        if (gDirUtilp->fileExists(sample_path()) && png_image->load(sample_path()) && png_image->decode(raw_image, 0.0f))
        {
            pipeline.mDXSMAASampleMap.create(raw_image->getData(), raw_image->getWidth(), raw_image->getHeight(), raw_image->getComponents());
            pipeline.mSMAASampleMap = 1;
        }
    }

    DXPipeline::createLUTBuffers(pipeline);

    gBumpImageList.restoreGL();
}

void DXPipeline::createLUTBuffers(LLPipeline& pipeline)
{
    if (!pipeline.mLightFunc)
    {
        U32 lightResX = gSavedSettings.getU32("RenderSpecularResX");
        U32 lightResY = gSavedSettings.getU32("RenderSpecularResY");
        F32* ls = nullptr;
        try
        {
            ls = new F32[lightResX * lightResY];
        }
        catch (std::bad_alloc&)
        {
            LLError::LLUserWarningMsg::showOutOfMemory();
            // might be better to set the error into mFatalMessage and rethrow
            LL_ERRS() << "Bad memory allocation in createLUTBuffers! lightResX: "
                << lightResX << " lightResY: " << lightResY << LL_ENDL;
        }
        F32 specExp = gSavedSettings.getF32("RenderSpecularExponent");
        // Calculate the (normalized) blinn-phong specular lookup texture. (with a few tweaks)
        for (U32 y = 0; y < lightResY; ++y)
        {
            for (U32 x = 0; x < lightResX; ++x)
            {
                ls[y * lightResX + x] = 0;
                F32 sa = (F32)x / (lightResX - 1);
                F32 spec = (F32)y / (lightResY - 1);
                F32 n = spec * spec * specExp;

                // Nothing special here.  Just your typical blinn-phong term.
                spec = powf(sa, n);

                // Apply our normalization function.
                // Note: This is the full equation that applies the full normalization curve, not an approximation.
                // This is fine, given we only need to create our LUT once per buffer initialization.
                spec *= (((n + 2) * (n + 4)) / (8 * F_PI * (powf(2, -n / 2) + n)));

                // Since we use R16F, we no longer have a dynamic range issue we need to work around here.
                // Though some older drivers may not like this, newer drivers shouldn't have this problem.
                ls[y * lightResX + x] = spec;
            }
        }

        // Same reasoning as mNoiseMap above - direct createFloat() upload preserves real dynamic range.
        // The mag/min filter override below (GL_LINEAR/GL_NEAREST, distinct
        // from TFO_TRILINEAR) has no DX_RENDER equivalent yet - sampler
        // state is built fresh at bind time from a single filter_option
        // enum (see LLTexUnit::bind(DXTexture&, ...)), which can't express
        // "linear mag, nearest min" as a separate combination. Bound with
        // TFO_TRILINEAR at this pass's bind call site instead - a real,
        // documented simplification, not a silent behavior change.
        pipeline.mDXLightFunc.createFloat(ls, lightResX, lightResY, 1);
        pipeline.mLightFunc = 1;

        delete[] ls;
    }

    pipeline.mPbrBrdfLut.allocate(512, 512, GL_RG16F);

    // S24: the real (1024-sample Monte Carlo) BRDF LUT generator shader is deliberately NOT
    // compiled or drawn here any more - its driver-side ISA compile stalled startup on some
    // NVIDIA drivers (see genbrdflutF.hlsl's comment). This LUT is a fixed, content-independent
    // function of (NoV, roughness), so instead: load it, already-baked, straight into the GPU
    // resource with no shader at all; only if that baked asset is missing/corrupt/wrong size do
    // we fall back to drawing the cheap, loop-free analytic approximation shader. The real
    // shader now only ever runs from bakeBrdfLut() (dev-only, Develop > Rendering).
    bool loaded_baked = false;
    {
        std::string asset_path = DXPipeline::getBrdfLutAssetPath();
        constexpr S32 kExpectedBytes = 512 * 512 * 4; // RG16F: 2 channels x 2 bytes, no padding
        LLFILE* fp = LLFile::fopen(asset_path, "rb");
        if (fp)
        {
            std::vector<U8> buf(kExpectedBytes);
            size_t read = fread(buf.data(), 1, buf.size(), fp);
            fclose(fp);
            if (read == buf.size())
            {
                loaded_baked = DXReadback::writePixels(pipeline.mPbrBrdfLut.getDXColorTexture(0), 0, 0, 512, 512, 4, buf.data());
            }
            else
            {
                LL_WARNS("BrdfLut") << "Baked BRDF LUT asset " << asset_path << " is the wrong size (" << read << " of " << buf.size() << " bytes) - ignoring" << LL_ENDL;
            }
        }
    }

    if (!loaded_baked)
    {
        LL_WARNS("BrdfLut") << "Baked BRDF LUT asset missing or failed to load - falling back to the analytic approximation shader. Run Develop > Rendering > \"Bake BRDF LUT\" and re-ship the asset." << LL_ENDL;

        pipeline.mPbrBrdfLut.bindTarget();
        if (gDeferredGenBrdfLutFallbackProgram.isComplete())
        {
            gDeferredGenBrdfLutFallbackProgram.bind();
            llassert_always(LLHLSLShader::sCurBoundShaderPtr != nullptr);

            gDX.begin(LLRender::TRIANGLE_STRIP);
            gDX.vertex2f(-1, -1);
            gDX.vertex2f(-1, 1);
            gDX.vertex2f(1, -1);
            gDX.vertex2f(1, 1);
            gDX.end();
            gDX.flush();

            gDeferredGenBrdfLutFallbackProgram.unbind();
        }
        else
        {
            LL_WARNS("BrdfLut") << gDeferredGenBrdfLutFallbackProgram.mName << " failed to load - BRDF LUT will be blank!" << LL_ENDL;
        }
        pipeline.mPbrBrdfLut.flush();
    }

    // Unlike every sibling block in this function, the BRDF LUT's generation above needs no DX_RENDER
    // branch - it relies on gDX.begin/vertex2f/end's already-DX-safe immediate-mode path (same as
    // generateGlow()/combineGlow()).

    pipeline.mExposureMap.allocate(1, 1, GL_R16F);
    pipeline.mExposureMap.bindTarget();
    // The two glClearColor calls below are raw GL, but harmless: LLRenderTarget::clear() under
    // DX_RENDER ignores whatever glClearColor set and always clears to hardcoded black (see
    // DXRenderTarget::clear()).
    pipeline.mExposureMap.clear();
    // LLRenderTarget::clear() under DX_RENDER always clears to hardcoded black, silently ignoring the
    // glClearColor(1,1,1,0) call above - mExposureMap stayed permanently at 0.0 instead of the intended
    // "neutral, no exposure adjustment yet" white default, since
    // generateExposure() (the real per-frame writer) has never been wired
    // for DX_RENDER. Sampling a 0.0 exposureMap in the tonemap
    // pass (presentDeferredScreen()) would multiply every
    // pixel to black - worse than the white-clip bug this was chasing.
    // clearColor() was already built for exactly this gap (its own comment
    // noted "no GL caller exists yet") - this is that caller.
    pipeline.mExposureMap.clearColor(1.f, 1.f, 1.f, 0.f);
    pipeline.mExposureMap.flush();

    pipeline.mLuminanceMap.allocate(256, 256, GL_R16F, false, LLTexUnit::TT_TEXTURE, LLTexUnit::TMG_AUTO);

    pipeline.mLastExposure.allocate(1, 1, GL_R16F);
}

bool DXPipeline::bakeBrdfLut(LLPipeline& pipeline)
{
    if (!gDeferredGenBrdfLutProgram.isComplete())
    {
        if (!LLViewerShaderMgr::instance()->loadShaderGenBrdfLut())
        {
            LL_WARNS("BrdfLut") << "Bake BRDF LUT: failed to compile the real BRDF generator shader - aborting bake" << LL_ENDL;
            return false;
        }
    }

    // Draw the real shader into the live mPbrBrdfLut - same mechanism createLUTBuffers()'s
    // fallback path uses, just with the expensive shader instead of the analytic one. This
    // overwrites whatever createLUTBuffers() populated it with at startup (baked asset or
    // fallback shader output) with the freshly-computed real values, which is also a handy way
    // for whoever runs this to immediately eyeball the real LUT in the current session.
    pipeline.mPbrBrdfLut.bindTarget();
    gDeferredGenBrdfLutProgram.bind();
    llassert_always(LLHLSLShader::sCurBoundShaderPtr != nullptr);

    gDX.begin(LLRender::TRIANGLE_STRIP);
    gDX.vertex2f(-1, -1);
    gDX.vertex2f(-1, 1);
    gDX.vertex2f(1, -1);
    gDX.vertex2f(1, 1);
    gDX.end();
    gDX.flush();

    gDeferredGenBrdfLutProgram.unbind();
    pipeline.mPbrBrdfLut.flush();

    constexpr S32 kBytes = 512 * 512 * 4; // RG16F: 2 channels x 2 bytes, no padding
    std::vector<U8> buf(kBytes);
    if (!DXReadback::readPixels(pipeline.mPbrBrdfLut.getDXColorTexture(0), 0, 0, 512, 512, 4, buf.data()))
    {
        LL_WARNS("BrdfLut") << "Bake BRDF LUT: GPU->CPU readback failed - aborting bake" << LL_ENDL;
        return false;
    }

    std::string asset_path = DXPipeline::getBrdfLutAssetPath();
    std::string asset_dir = gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, "luts", "");
    if (!gDirUtilp->fileExists(asset_dir))
    {
        LLFile::mkdir(asset_dir);
    }

    LLFILE* fp = LLFile::fopen(asset_path, "wb");
    if (!fp)
    {
        LL_WARNS("BrdfLut") << "Bake BRDF LUT: failed to open " << asset_path << " for writing - aborting bake" << LL_ENDL;
        return false;
    }
    size_t written = fwrite(buf.data(), 1, buf.size(), fp);
    fclose(fp);
    if (written != buf.size())
    {
        LL_WARNS("BrdfLut") << "Bake BRDF LUT: short write to " << asset_path << " (" << written << " of " << buf.size() << " bytes)" << LL_ENDL;
        return false;
    }

    LL_INFOS("BrdfLut") << "Bake BRDF LUT: wrote " << asset_path << " (" << written << " bytes)" << LL_ENDL;
    return true;
}

void DXPipeline::connectRefreshCachedSettingsSafe(LLPipeline& pipeline, const std::string& name)
{
    LLControlVariable* const cntrl = gSavedSettings.getControl(name).get();
    if (!cntrl)
    {
        LL_WARNS() << "Global setting name not found: " << name << LL_ENDL;
        return;
    }

    // Commit signal pointer may be null depending on control type.
    auto* const signal = cntrl->getCommitSignal();
    if (!signal)
        return;

    // refreshCachedSettings() is a zero-arg static with no pipeline dependency, so no capture
    // is needed here (unlike markMoved()/updateMoveDampedAsync(), which capture the pipeline
    // pointer by value since their deferred lambdas DO need it).
    signal->connect([](LLControlVariable*, const LLSD&, const LLSD&) noexcept { DXPipeline::refreshCachedSettings(); });
}

// ============================================================================
// Class lifecycle (construction/teardown, settings-signal plumbing)
// ============================================================================

void DXPipeline::init(LLPipeline& pipeline)
{
    DXPipeline::refreshCachedSettings();

    pipeline.mRT = &pipeline.mMainRT;

    gOctreeMaxCapacity = gSavedSettings.getU32("OctreeMaxNodeCapacity");
    gOctreeMinSize = gSavedSettings.getF32("OctreeMinimumNodeSize");
    DXPipeline::sDynamicLOD = gSavedSettings.getBOOL("RenderDynamicLOD");
    DXPipeline::sRenderAttachedLights = gSavedSettings.getBOOL("RenderAttachedLights");
    DXPipeline::sRenderAttachedParticles = gSavedSettings.getBOOL("RenderAttachedParticles");

    pipeline.mSphereProbes.refreshSettings();

    pipeline.mInitialized = true;

    //create render pass pools
    DXPipeline::getPool(pipeline, LLDrawPool::POOL_WATEREXCLUSION);
    DXPipeline::getPool(pipeline, LLDrawPool::POOL_ALPHA_PRE_WATER);
    DXPipeline::getPool(pipeline, LLDrawPool::POOL_ALPHA_POST_WATER);
    DXPipeline::getPool(pipeline, LLDrawPool::POOL_SIMPLE);
    DXPipeline::getPool(pipeline, LLDrawPool::POOL_ALPHA_MASK);
    DXPipeline::getPool(pipeline, LLDrawPool::POOL_FULLBRIGHT_ALPHA_MASK);
    DXPipeline::getPool(pipeline, LLDrawPool::POOL_GRASS);
    DXPipeline::getPool(pipeline, LLDrawPool::POOL_FULLBRIGHT);
    DXPipeline::getPool(pipeline, LLDrawPool::POOL_BUMP);
    DXPipeline::getPool(pipeline, LLDrawPool::POOL_MATERIALS);
    DXPipeline::getPool(pipeline, LLDrawPool::POOL_GLOW);
    DXPipeline::getPool(pipeline, LLDrawPool::POOL_GLTF_PBR);
    DXPipeline::getPool(pipeline, LLDrawPool::POOL_GLTF_PBR_ALPHA_MASK);

    DXPipeline::resetFrameStats(pipeline);

    if (gSavedSettings.getBOOL("DisableAllRenderFeatures"))
    {
        pipeline.clearAllRenderDebugFeatures();
    }
    else
    {
        pipeline.setAllRenderDebugFeatures(); // By default, all debugging features on
    }
    pipeline.clearAllRenderDebugDisplays(); // All debug displays off

    if (gSavedSettings.getBOOL("DisableAllRenderTypes"))
    {
        DXPipeline::clearAllRenderTypes(pipeline);
    }
    else if (gNonInteractive)
    {
        DXPipeline::clearAllRenderTypes(pipeline);
    }
    else
    {
        DXPipeline::setAllRenderTypes(pipeline); // By default, all rendering types start enabled
    }

    // Removed upstream's RenderPerformanceTest hack: it re-disabled trees/grass/terrain AFTER
    // DXPipeline::setAllRenderTypes(pipeline) enabled them, so vegetation vanished on startup
    // whenever RenderPerformanceTest was saved as true. The signal listener in llviewercontrol.cpp
    // already handles that setting.

    pipeline.getOldRenderDebugMaskMutable() = pipeline.getRenderDebugMaskMutable();

    pipeline.mBackfaceCull = true;

    // Enable features
    LLViewerShaderMgr::instance()->setShaders();

    for (U32 i = 0; i < 2; ++i)
    {
        pipeline.mSpotLightFade[i] = 1.f;
    }

    if (pipeline.mCubeVB.isNull())
    {
        pipeline.mCubeVB = ll_create_cube_vb(LLVertexBuffer::MAP_VERTEX);
    }

    pipeline.mDeferredVB = new LLVertexBuffer(DEFERRED_VB_MASK);
    pipeline.mDeferredVB->allocateBuffer(8, 0);

    {
        pipeline.mScreenTriangleVB = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX);
        pipeline.mScreenTriangleVB->allocateBuffer(3, 0);
        LLStrider<LLVector3> vert;
        pipeline.mScreenTriangleVB->getVertexStrider(vert);

        vert[0].set(-1, 1, 0);
        vert[1].set(-1, -3, 0);
        vert[2].set(3, 1, 0);

        pipeline.mScreenTriangleVB->unmapBuffer();
    }

    //
    // Update all settings to trigger a cached settings refresh
    //
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderAutoMaskAlphaDeferred");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderAutoMaskAlphaNonDeferred");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderUseFarClip");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderAvatarMaxNonImpostors");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "UseOcclusion");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderDeferredSunWash");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderFSAAType");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderUIBuffer");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderShadowDetail");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderShadowSplits");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderDeferredSSAO");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderShadowResolutionScale");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderDelayCreation");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderAnimateRes");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "FreezeTime");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "DebugBeaconLineWidth");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderSpotLightsInNondeferred");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "PreviewAmbientColor");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "PreviewDiffuse0");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "PreviewSpecular0");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "PreviewDiffuse1");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "PreviewSpecular1");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "PreviewDiffuse2");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "PreviewSpecular2");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "PreviewDirection0");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "PreviewDirection1");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "PreviewDirection2");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderGlowMaxExtractAlpha");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderGlowWarmthAmount");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderGlowLumWeights");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderGlowWarmthWeights");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderGlowResolutionPow");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderGlowIterations");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderGlowWidth");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderGlowStrength");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderGlowNoise");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderDepthOfField");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderDepthOfFieldInEditMode");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "CameraFocusTransitionTime");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "CameraFNumber");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "CameraFocalLength");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "CameraFieldOfView");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderShadowNoise");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderShadowBlurSize");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderSSAOScale");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderSSAOMaxScale");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderSSAOFactor");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderSSAOEffect");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderShadowBiasError");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderShadowOffset");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderShadowBias");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderSpotShadowOffset");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderSpotShadowBias");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderEdgeDepthCutoff");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderEdgeNormCutoff");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderShadowGaussian");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderShadowBlurDistFactor");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderDeferredAtmospheric");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderFarClip");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderShadowSplitExponent");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderShadowErrorCutoff");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderShadowFOVCutoff");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "CameraOffset");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "CameraMaxCoF");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "CameraDoFResScale");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "CameraAspectRatio");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderAutoHideSurfaceAreaLimit");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderScreenSpaceReflections");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderScreenSpaceReflectionIterations");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderScreenSpaceReflectionRayStep");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderScreenSpaceReflectionDistanceBias");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderScreenSpaceReflectionDepthRejectBias");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderScreenSpaceReflectionAdaptiveStepMultiplier");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderScreenSpaceReflectionGlossySamples");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderScreenSpaceReflectionGlossThreshold");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderWaterSSRIterations");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderWaterSSRRayStep");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderBufferVisualization");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderMirrors");
    DXPipeline::connectRefreshCachedSettingsSafe(pipeline, "RenderAvatarCloth");

    LLPointer<LLControlVariable> cntrl_ptr = gSavedSettings.getControl("CollectFontVertexBuffers");
    if (cntrl_ptr.notNull())
    {
        cntrl_ptr->getCommitSignal()->connect([](LLControlVariable* control, const LLSD& value, const LLSD& previous)
            {
                bool enable_buffers = control->getValue().asBoolean();
                LLFontVertexBuffer::enableBufferCollection(enable_buffers);
                LLFontWidthBuffer::enableBufferCollection(enable_buffers);
            });
    }

    cntrl_ptr = gSavedSettings.getControl("CollectUIImageVertexBuffers");
    if (cntrl_ptr.notNull())
    {
        cntrl_ptr->getCommitSignal()->connect([](LLControlVariable* control, const LLSD& value, const LLSD& previous)
        {
            bool enable_buffers = control->getValue().asBoolean();
            LLUIImage::enableDisplayListsCollection(enable_buffers);
        });
    }
}

void DXPipeline::cleanup(LLPipeline& pipeline)
{
    pipeline.assertInitialized();

    pipeline.mGroupQ1.clear();

    for (auto iter = pipeline.getPoolsMutable().begin();
        iter != pipeline.getPoolsMutable().end(); )
    {
        auto curiter = iter++;
        LLDrawPool* poolp = *curiter;
        if (poolp->isFacePool())
        {
            LLFacePool* face_pool = (LLFacePool*)poolp;
            if (face_pool->mReferences.empty())
            {
                pipeline.getPoolsMutable().erase(curiter);
                DXPipeline::removeFromQuickLookup(pipeline, poolp);
                delete poolp;
            }
        }
        else
        {
            pipeline.getPoolsMutable().erase(curiter);
            DXPipeline::removeFromQuickLookup(pipeline, poolp);
            delete poolp;
        }
    }

    if (!pipeline.mTerrainPools.empty())
    {
        LL_WARNS() << "Terrain Pools not cleaned up" << LL_ENDL;
    }
    if (!pipeline.mTreePools.empty())
    {
        LL_WARNS() << "Tree Pools not cleaned up" << LL_ENDL;
    }

    delete pipeline.mAlphaPoolPreWater;
    pipeline.mAlphaPoolPreWater = nullptr;
    delete pipeline.mAlphaPoolPostWater;
    pipeline.mAlphaPoolPostWater = nullptr;
    delete pipeline.mSkyPool;
    pipeline.mSkyPool = NULL;
    delete pipeline.mTerrainPool;
    pipeline.mTerrainPool = NULL;
    delete pipeline.mWaterPool;
    pipeline.mWaterPool = NULL;
    delete pipeline.mSimplePool;
    pipeline.mSimplePool = NULL;
    delete pipeline.mFullbrightPool;
    pipeline.mFullbrightPool = NULL;
    delete pipeline.mGlowPool;
    pipeline.mGlowPool = NULL;
    delete pipeline.mBumpPool;
    pipeline.mBumpPool = NULL;
    // don't delete wl sky pool it was handled above in the for loop
    //delete mWLSkyPool;
    pipeline.mWLSkyPool = NULL;
    delete pipeline.mWaterExclusionPool;
    pipeline.mWaterExclusionPool = nullptr;

    DXPipeline::releaseGLBuffers(pipeline);

    pipeline.mFaceSelectImagep = NULL;

    pipeline.mMovedList.clear();
    pipeline.mMovedBridge.clear();
    pipeline.mShiftList.clear();

    pipeline.mInitialized = false;

    pipeline.mDeferredVB = NULL;
    pipeline.mScreenTriangleVB = nullptr;

    pipeline.mCubeVB = NULL;

    pipeline.mSphereProbes.cleanup();
    pipeline.mMirrorProbes.cleanup();
}

void DXPipeline::destroyGL(LLPipeline& pipeline)
{
    DXPipeline::unloadShaders(pipeline);
    pipeline.mHighlightFaces.clear();

    DXPipeline::resetDrawOrders(pipeline);

    DXPipeline::releaseGLBuffers(pipeline);

    if (pipeline.mMeshDirtyQueryObject)
    {
        // Routed through DXOcclusionQuery instead of raw glDeleteQueries() (same unfenced-GL-extension
        // crash class as other occlusion-query call sites). Currently unreachable in practice
        // (mMeshDirtyQueryObject's glGenQueries() in postSort is commented out under DX_RENDER), but
        // would be a latent crash if that's ever re-enabled without this fix.
        DXOcclusionQuery::deleteQueries(1, &pipeline.mMeshDirtyQueryObject);
        pipeline.mMeshDirtyQueryObject = 0;
    }
}

void DXPipeline::restoreGL(LLPipeline& pipeline)
{
    pipeline.assertInitialized();

    LLViewerShaderMgr::instance()->setShaders();

    for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
        iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
    {
        LLViewerRegion* region = *iter;
        for (U32 i = 0; i < LLViewerRegion::NUM_PARTITIONS; i++)
        {
            LLSpatialPartition* part = region->getSpatialPartition(i);
            if (part)
            {
                part->restoreGL();
            }
        }
    }
}

// check_references() - private helpers for checkReferences()'s (currently #if-0'd-out) bodies
// below.
namespace
{
    void check_references(LLSpatialGroup* group, LLDrawable* drawable)
    {
        for (LLSpatialGroup::element_iter i = group->getDataBegin(); i != group->getDataEnd(); ++i)
        {
            LLDrawable* drawablep = (LLDrawable*)(*i)->getDrawable();
            if (drawable == drawablep)
            {
                LL_ERRS() << "LLDrawable deleted while actively reference by LLPipeline." << LL_ENDL;
            }
        }
    }

    void check_references(LLDrawable* drawable, LLFace* face)
    {
        for (S32 i = 0; i < drawable->getNumFaces(); ++i)
        {
            if (drawable->getFace(i) == face)
            {
                LL_ERRS() << "LLFace deleted while actively referenced by LLPipeline." << LL_ENDL;
            }
        }
    }

    void check_references(LLSpatialGroup* group, LLFace* face)
    {
        for (LLSpatialGroup::element_iter i = group->getDataBegin(); i != group->getDataEnd(); ++i)
        {
            LLDrawable* drawable = (LLDrawable*)(*i)->getDrawable();
            if (drawable)
            {
                check_references(drawable, face);
            }
        }
    }

    void check_references(LLSpatialGroup* group, LLDrawInfo* draw_info)
    {
        for (LLSpatialGroup::draw_map_t::iterator i = group->mDrawMap.begin(); i != group->mDrawMap.end(); ++i)
        {
            LLSpatialGroup::drawmap_elem_t& draw_vec = i->second;
            for (LLSpatialGroup::drawmap_elem_t::iterator j = draw_vec.begin(); j != draw_vec.end(); ++j)
            {
                LLDrawInfo* params = *j;
                if (params == draw_info)
                {
                    LL_ERRS() << "LLDrawInfo deleted while actively referenced by LLPipeline." << LL_ENDL;
                }
            }
        }
    }
}

// ============================================================================
// Depth of field, reference-tracking debug checks, and the last odds and ends
// ============================================================================

void DXPipeline::checkReferences(LLPipeline& pipeline, LLFace* face)
{
#if 0
    if (sCull)
    {
        for (LLCullResult::sg_iterator iter = sCull->beginVisibleGroups(); iter != sCull->endVisibleGroups(); ++iter)
        {
            LLSpatialGroup* group = *iter;
            check_references(group, face);
        }

        for (LLCullResult::sg_iterator iter = sCull->beginAlphaGroups(); iter != sCull->endAlphaGroups(); ++iter)
        {
            LLSpatialGroup* group = *iter;
            check_references(group, face);
        }

        for (LLCullResult::sg_iterator iter = sCull->beginDrawableGroups(); iter != sCull->endDrawableGroups(); ++iter)
        {
            LLSpatialGroup* group = *iter;
            check_references(group, face);
        }

        for (LLCullResult::drawable_iterator iter = sCull->beginVisibleList(); iter != sCull->endVisibleList(); ++iter)
        {
            LLDrawable* drawable = *iter;
            check_references(drawable, face);
        }
    }
#endif
}

void DXPipeline::checkReferences(LLPipeline& pipeline, LLDrawable* drawable)
{
#if 0
    if (sCull)
    {
        for (LLCullResult::sg_iterator iter = sCull->beginVisibleGroups(); iter != sCull->endVisibleGroups(); ++iter)
        {
            LLSpatialGroup* group = *iter;
            check_references(group, drawable);
        }

        for (LLCullResult::sg_iterator iter = sCull->beginAlphaGroups(); iter != sCull->endAlphaGroups(); ++iter)
        {
            LLSpatialGroup* group = *iter;
            check_references(group, drawable);
        }

        for (LLCullResult::sg_iterator iter = sCull->beginDrawableGroups(); iter != sCull->endDrawableGroups(); ++iter)
        {
            LLSpatialGroup* group = *iter;
            check_references(group, drawable);
        }

        for (LLCullResult::drawable_iterator iter = sCull->beginVisibleList(); iter != sCull->endVisibleList(); ++iter)
        {
            if (drawable == *iter)
            {
                LL_ERRS() << "LLDrawable deleted while actively referenced by LLPipeline." << LL_ENDL;
            }
        }
    }
#endif
}

void DXPipeline::checkReferences(LLPipeline& pipeline, LLDrawInfo* draw_info)
{
#if 0
    if (sCull)
    {
        for (LLCullResult::sg_iterator iter = sCull->beginVisibleGroups(); iter != sCull->endVisibleGroups(); ++iter)
        {
            LLSpatialGroup* group = *iter;
            check_references(group, draw_info);
        }

        for (LLCullResult::sg_iterator iter = sCull->beginAlphaGroups(); iter != sCull->endAlphaGroups(); ++iter)
        {
            LLSpatialGroup* group = *iter;
            check_references(group, draw_info);
        }

        for (LLCullResult::sg_iterator iter = sCull->beginDrawableGroups(); iter != sCull->endDrawableGroups(); ++iter)
        {
            LLSpatialGroup* group = *iter;
            check_references(group, draw_info);
        }
    }
#endif
}

void DXPipeline::checkReferences(LLPipeline& pipeline, LLSpatialGroup* group)
{
#if CHECK_PIPELINE_REFERENCES
    if (sCull)
    {
        for (LLCullResult::sg_iterator iter = sCull->beginVisibleGroups(); iter != sCull->endVisibleGroups(); ++iter)
        {
            if (group == *iter)
            {
                LL_ERRS() << "LLSpatialGroup deleted while actively referenced by LLPipeline." << LL_ENDL;
            }
        }

        for (LLCullResult::sg_iterator iter = sCull->beginAlphaGroups(); iter != sCull->endAlphaGroups(); ++iter)
        {
            if (group == *iter)
            {
                LL_ERRS() << "LLSpatialGroup deleted while actively referenced by LLPipeline." << LL_ENDL;
            }
        }

        for (LLCullResult::sg_iterator iter = sCull->beginDrawableGroups(); iter != sCull->endDrawableGroups(); ++iter)
        {
            if (group == *iter)
            {
                LL_ERRS() << "LLSpatialGroup deleted while actively referenced by LLPipeline." << LL_ENDL;
            }
        }
    }
#endif
}

void DXPipeline::renderDoF(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst)
{
    {
        bool dof_enabled =
            (DXPipeline::RenderDepthOfFieldInEditMode || !LLToolMgr::getInstance()->inBuildMode()) &&
            DXPipeline::RenderDepthOfField &&
            !gCubeSnapshot;

        gViewerWindow->setup3DViewport();

        if (dof_enabled)
        {
            LLGLDisable blend(GL_BLEND);

            // depth of field focal plane calculations
            static F32 current_distance = 16.f;
            static F32 start_distance = 16.f;
            static F32 transition_time = 1.f;

            LLVector3 focus_point;

            LLViewerObject* obj = LLViewerMediaFocus::getInstance()->getFocusedObject();
            if (obj && obj->mDrawable && obj->isSelected())
            { // focus on selected media object
                S32 face_idx = LLViewerMediaFocus::getInstance()->getFocusedFace();
                if (obj && obj->mDrawable)
                {
                    LLFace* face = obj->mDrawable->getFace(face_idx);
                    if (face)
                    {
                        focus_point = face->getPositionAgent();
                    }
                }
            }

            if (focus_point.isExactlyZero())
            {
                if (LLViewerJoystick::getInstance()->getOverrideCamera())
                { // focus on point under cursor
                    focus_point.set(gDebugRaycastIntersection.getF32ptr());
                }
                else if (gAgentCamera.cameraMouselook())
                { // focus on point under mouselook crosshairs
                    LLVector4a result(0.f);

                    gViewerWindow->cursorIntersect(-1, -1, 512.f, nullptr, -1, false, false, true, true, nullptr, nullptr, nullptr, &result);

                    focus_point.set(result.getF32ptr());
                }
                else
                {
                    // focus on alt-zoom target
                    LLViewerRegion* region = gAgent.getRegion();
                    if (region)
                    {
                        focus_point = LLVector3(gAgentCamera.getFocusGlobal() - region->getOriginGlobal());
                    }
                }
            }

            LLVector3 eye = LLViewerCamera::getInstance()->getOrigin();
            F32 target_distance = 16.f;
            if (!focus_point.isExactlyZero())
            {
                target_distance = LLViewerCamera::getInstance()->getAtAxis() * (focus_point - eye);
            }

            if (transition_time >= 1.f && fabsf(current_distance - target_distance) / current_distance > 0.01f)
            { // large shift happened, interpolate smoothly to new target distance
                transition_time = 0.f;
                start_distance = current_distance;
            }
            else if (transition_time < 1.f)
            { // currently in a transition, continue interpolating
                transition_time += 1.f / DXPipeline::CameraFocusTransitionTime * gFrameIntervalSeconds.value();
                transition_time = llmin(transition_time, 1.f);

                F32 t = cosf(transition_time * F_PI + F_PI) * 0.5f + 0.5f;
                current_distance = start_distance + (target_distance - start_distance) * t;
            }
            else
            { // small or no change, just snap to target distance
                current_distance = target_distance;
            }

            // --- DOF lens math (S24 refined) -----------------------------------------

            // Use double for all intermediate math, pack back to F32 at the end.
            F64 subject_distance = (F64)current_distance * 1000.0;          // m -> mm
            F64 fnumber = (F64)DXPipeline::CameraFNumber;
            F64 default_focal = (F64)DXPipeline::CameraFocalLength;                  // mm
            F64 variable_aspect = (F64)DXPipeline::CameraAspectRatio;

            F64 fov = (F64)LLViewerCamera::getInstance()->getView();          // radians (vertical)
            F64 default_fov = (F64)DXPipeline::CameraFieldOfView * F_PI / 180.0;                  // degrees -> radians

            F64 aspect_ratio = (F64)pipeline.mRT->screen.getWidth() / (F64)pipeline.mRT->screen.getHeight();

            // Adjust default_fov by aspect ratio (original intent), but numerically safer.
            F64 half = tan(default_fov * 0.5) * aspect_ratio;
            F64 adjusted_fov = 2.0 * atan(half);

            // dv = 2 * f * tan(adjusted_fov/2) * variable_aspect
            F64 dv = 2.0 * default_focal * tan(adjusted_fov * 0.5) * variable_aspect;

            // Focal length from dv and current FOV
            F64 denom_fov = tan(fov * 0.5);
            if (fabs(denom_fov) < 1e-9)
            {
                denom_fov = (denom_fov >= 0.0 ? 1e-9 : -1e-9);
            }

            F64 focal_length = dv / (2.0 * denom_fov);                      // mm

            // from wikipedia -- c = |s2-s1|/s2 * f^2/(N(S1-f))
            // where     N = fnumber
            //           s2 = dot distance
            //           s1 = subject distance
            //           f = focal length

            // Protect against subject_distance ~= focal_length
            F64 denom_sf = (subject_distance - focal_length);
            if (fabs(denom_sf) < 1e-6)
            {
                denom_sf = (denom_sf >= 0.0 ? 1e-6 : -1e-6);
            }

            // Lens blur constant in mm
            F64 blur_constant = (focal_length * focal_length) / (fnumber * denom_sf);

            // Convert to meters for shader (mm -> m): multiply by 0.001 for better precision
            blur_constant *= 0.001;

            // Magnification uses the same safe denominator
            F64 magnification = focal_length / denom_sf;

            // --- Pack back to F32 for the shader interface ---------------------------

            F32 focal_length_f = (F32)focal_length;
            F32 blur_constant_f = (F32)blur_constant;
            F32 magnification_f = (F32)magnification;

            // Use these where you previously used focal_length / blur_constant / magnification
            focal_length = focal_length_f;
            blur_constant = blur_constant_f;
            magnification = magnification_f;

            { // build diffuse+bloom+CoF
                pipeline.mRT->deferredLight.bindTarget();

                gDeferredCoFProgram.bind();

                gDeferredCoFProgram.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, src, LLTexUnit::TFO_POINT);
                gDeferredCoFProgram.bindTexture(LLShaderMgr::DEFERRED_DEPTH, &pipeline.mRT->deferredScreen, true);

                gDeferredCoFProgram.uniform1f(LLShaderMgr::DEFERRED_DEPTH_CUTOFF, DXPipeline::RenderEdgeDepthCutoff);
                gDeferredCoFProgram.uniform1f(LLShaderMgr::DEFERRED_NORM_CUTOFF, DXPipeline::RenderEdgeNormCutoff);
                gDeferredCoFProgram.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (F32)dst->getWidth(), (F32)dst->getHeight());
                gDeferredCoFProgram.uniform1f(LLShaderMgr::DOF_FOCAL_DISTANCE, -subject_distance / 1000.f);
                gDeferredCoFProgram.uniform1f(LLShaderMgr::DOF_BLUR_CONSTANT, blur_constant);
                gDeferredCoFProgram.uniform1f(LLShaderMgr::DOF_TAN_PIXEL_ANGLE, tanf(1.f / LLDrawable::sCurPixelAngle));
                gDeferredCoFProgram.uniform1f(LLShaderMgr::DOF_MAGNIFICATION, magnification);
                gDeferredCoFProgram.uniform1f(LLShaderMgr::DOF_MAX_COF, DXPipeline::CameraMaxCoF);
                gDeferredCoFProgram.uniform1f(LLShaderMgr::DOF_RES_SCALE, DXPipeline::CameraDoFResScale);

                pipeline.mScreenTriangleVB->setBuffer();
                pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
                gDeferredCoFProgram.unbind();
                pipeline.mRT->deferredLight.flush();
            }

            U32 dof_width = (U32)(pipeline.mRT->screen.getWidth() * DXPipeline::CameraDoFResScale);
            U32 dof_height = (U32)(pipeline.mRT->screen.getHeight() * DXPipeline::CameraDoFResScale);

            { // perform DoF sampling at half-res (preserve alpha channel)
                src->bindTarget();
                // src->bindTarget() sets a D3D11 viewport matching src's FULL size, but this pass
                // shrinks it to half-res (CameraDoFResScale), so it needs a real viewport override here
                // (unlike the other glViewport() calls in this function, which are no-ops under
                // DX_RENDER since bindTarget()'s default already matches). TopLeftY must be the BOTTOM
                // of src, not 0: GL's Y-up glViewport(0,0,dof_width,dof_height) writes to the bottom
                // dof_height rows, and dofCombineF.hlsl's dofSample() (already GL-vs-D3D11 flipped)
                // expects that content at the same physical location in D3D11's top-down texture space.
                gDXContext.setViewport(0, (int)src->getHeight() - (int)dof_height, dof_width, dof_height);

                gDX.setColorWriteMask(true, false);

                gDeferredPostProgram.bind();
                gDeferredPostProgram.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, &pipeline.mRT->deferredLight, LLTexUnit::TFO_POINT);

                gDeferredPostProgram.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (F32)dst->getWidth(), (F32)dst->getHeight());
                gDeferredPostProgram.uniform1f(LLShaderMgr::DOF_MAX_COF, DXPipeline::CameraMaxCoF);
                gDeferredPostProgram.uniform1f(LLShaderMgr::DOF_RES_SCALE, DXPipeline::CameraDoFResScale);

                pipeline.mScreenTriangleVB->setBuffer();
                pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

                gDeferredPostProgram.unbind();

                src->flush();
                gDX.setColorWriteMask(true, true);
            }

            { // combine result based on alpha
                dst->bindTarget();
                // dst->bindTarget() above already set a D3D11 viewport of (0,0,dst width,dst height) -
                // exactly what this call would set anyway, so it's a no-op to skip here (unlike the
                // half-res DoF-sampling viewport above, which genuinely differs from the default).
                gDeferredDoFCombineProgram.bind();
                gDeferredDoFCombineProgram.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, src, LLTexUnit::TFO_POINT);
                gDeferredDoFCombineProgram.bindTexture(LLShaderMgr::DEFERRED_LIGHT, &pipeline.mRT->deferredLight, LLTexUnit::TFO_POINT);

                gDeferredDoFCombineProgram.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (F32)dst->getWidth(), (F32)dst->getHeight());
                gDeferredDoFCombineProgram.uniform1f(LLShaderMgr::DOF_MAX_COF, DXPipeline::CameraMaxCoF);
                gDeferredDoFCombineProgram.uniform1f(LLShaderMgr::DOF_RES_SCALE, DXPipeline::CameraDoFResScale);
                gDeferredDoFCombineProgram.uniform1f(LLShaderMgr::DOF_WIDTH, (dof_width - 1) / (F32)src->getWidth());
                gDeferredDoFCombineProgram.uniform1f(LLShaderMgr::DOF_HEIGHT, (dof_height - 1) / (F32)src->getHeight());

                pipeline.mScreenTriangleVB->setBuffer();
                pipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

                gDeferredDoFCombineProgram.unbind();

                dst->flush();
            }
        }
        else
        {
            DXPipeline::copyRenderTarget(pipeline, src, dst);
        }
    }
}

// Find the lists which have references to this object. Zero callers tree-wide, kept for debug use.
void DXPipeline::findReferences(LLPipeline& pipeline, LLDrawable* drawablep)
{
    pipeline.assertInitialized();
    if (pipeline.getLights().find(drawablep) != pipeline.getLights().end())
    {
        LL_INFOS() << "In mLights" << LL_ENDL;
    }
    if (std::find(pipeline.mMovedList.begin(), pipeline.mMovedList.end(), drawablep) != pipeline.mMovedList.end())
    {
        LL_INFOS() << "In mMovedList" << LL_ENDL;
    }
    if (std::find(pipeline.mShiftList.begin(), pipeline.mShiftList.end(), drawablep) != pipeline.mShiftList.end())
    {
        LL_INFOS() << "In mShiftList" << LL_ENDL;
    }
    if (pipeline.mRetexturedList.find(drawablep) != pipeline.mRetexturedList.end())
    {
        LL_INFOS() << "In mRetexturedList" << LL_ENDL;
    }

    if (std::find(pipeline.mBuildQ1.begin(), pipeline.mBuildQ1.end(), drawablep) != pipeline.mBuildQ1.end())
    {
        LL_INFOS() << "In mBuildQ1" << LL_ENDL;
    }

    S32 count = 0;

    count = gObjectList.findReferences(drawablep);
    if (count)
    {
        LL_INFOS() << "In other drawables: " << count << " references" << LL_ENDL;
    }
}

void DXPipeline::handleShadowDetailChanged(LLPipeline& pipeline)
{
    const S32 saved = gSavedSettings.getS32("RenderShadowDetail");

    if (DXPipeline::RenderShadowDetail > saved)
    {
        DXPipeline::skipRenderingShadows(pipeline);
        return;
    }

    LLViewerShaderMgr::instance()->setShaders();
}
