/**
 * @file dxpipeline.h
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

#pragma once

#include "llrender.h" // LLRender::eTexIndex (nested enum - needs the full class, not just a
                       // forward declaration) - setRenderHighlightTextureChannel()
#include "lldrawable.h" // LLDrawable::EDrawableFlags (nested enum - needs the full class, not
                         // just a forward declaration) - markRebuild()'s default argument
#include "v4color.h" // LLColor4 - needs the full class, not just a forward declaration, since
                      // the relocated cached-settings statics (PreviewAmbientColor/etc.) are
                      // by-value data members, not pointers/references
#include "v3math.h" // LLVector3 - same reason (PreviewDirection0/RenderGlowLumWeights/etc.)
#include "llspatialpartition.h" // LLCullResult::drawinfo_iterator/sg_iterator (nested typedefs -
                                 // need the full class, not just a forward declaration) -
                                 // beginRenderMap()/endRenderMap()/beginAlphaGroups()/etc.

class LLCamera;
class LLDrawInfo;
class LLDrawPool;
class LLGLUpdate;
class LLPipeline;
class LLSpatialGroup;
class LLSpatialPartition;
class LLTextureEntry;
class LLUUID;
class LLVector2;
class LLVector4a;
class LLViewerFetchedTexture;
class LLViewerObject;
class LLViewerTexture;
class LLVOAvatar;
class LLVOPartGroup;
struct LLDeferredPipelineMarks;

// Free functions (not DXPipeline:: members). LLRayAABB() is zero-caller tree-wide, kept for
// debug/future use.
bool LLRayAABB(const LLVector3& center, const LLVector3& size, const LLVector3& origin, const LLVector3& dir, LLVector3& coord, F32 epsilon = 0);
void render_hud_elements();

// LLPipeline is the single source of truth for orchestration - culling, spatial partition
// traversal, visibility, render-type flags; DXPipeline owns the actual draw-call issuing layer
// that operates on an LLPipeline instance. Every function below is a static DXPipeline:: method
// taking the LLPipeline it acts on as its first parameter, grouped into the sections below.
//
// Scope note: only draw pools explicitly whitelisted in the .cpp are drawn - everything else is
// silently skipped (not a bug; those pools haven't been converted to DX_RENDER yet). Expand the
// whitelist as more pools convert.
class DXPipeline
{
public:
    // ================================================================================
    // Geometry rendering (deferred/post-deferred passes) and the final present chain
    // ================================================================================

    // Deferred-pool pass. do_occlusion triggers a real DXOcclusionQuery-backed occlusion pass,
    // matching the POOL_GRASS threshold.
    static void renderGeomDeferred(LLPipeline& pipeline, LLCamera& camera, bool do_occlusion = false);

    // The "forward"/translucent pass (windows, glass, glow, eventually water) - see
    // isConvertedPostDeferredPool() in the .cpp for exactly which pools are wired up so far.
    static void renderGeomPostDeferred(LLPipeline& pipeline, LLCamera& camera);

    // The real post-fx chain: gamma-correct/tonemap -> glow -> DoF (optional) -> FXAA/SMAA
    // (optional) -> real final present (gDeferredPostNoDoFNoiseProgram, depth-aware noise
    // dithering) -> back buffer. Falls back to a raw unlit deferredScreen blit (via
    // presentFinal(), in the .cpp) whenever renderDeferredLighting() didn't complete this frame
    // or the gamma shader failed to compile, so a shader/lighting gap degrades gracefully
    // instead of showing a blank window.
    // HDR auto-exposure (generateLuminance()/generateExposure(), dynamic eye adaptation) is
    // still a no-op under DX_RENDER (see the .cpp) - the tonemap curve itself is correct but
    // exposure never adapts to scene brightness.
    static void presentDeferredScreen(LLPipeline& pipeline);

    // The ONE place the two eyes' color ever actually mixes. Call once, after both eyes'
    // presentDeferredScreen() passes have captured into pipeline.mStereoEyeL/mStereoEyeR (see
    // getCurrentStereoEyeTarget()'s comment in the .cpp), right before the frame's single
    // swap(). No-ops safely if either eye target has nothing in it yet.
    static void presentStereoComposite(LLPipeline& pipeline);

    // The ambient+sun/atmospherics term (softenLightF/V.hlsl) via the bindDeferredShader()
    // chokepoint, writing the lit result into mRT->screen; also covers the sun-shadow/SSAO
    // lightmap pass and the local point/spot light loop further down in the .cpp.
    // presentDeferredScreen() falls back to its existing raw deferredScreen blit whenever
    // gDeferredSoftenProgram isn't complete, so a shader compile failure here degrades
    // gracefully instead of breaking the frame.
    static void renderDeferredLighting(LLPipeline& pipeline);

    // SMAA.hlsl's own LinearSampler(s13)/PointSampler(s14) - must be bound before every SMAA
    // pass (edge-detect, blend-weights, neighborhood-blend); nothing else in this codebase
    // touches these slots. Centralizes sampler binding for generateSMAABuffers()/applySMAA(),
    // which otherwise duplicate it inline.
    static void bindSMAAStaticSamplers();

    // ================================================================================
    // Post-processing chain (buffer visualization, color grading, tonemap, sharpening, AA, glow)
    // ================================================================================

    // Develop > Rendering > Buffer Visualization - draws `src`'s bufferIndex-th channel into
    // `dst` via gDeferredBufferVisualProgram, lazily compiled on first real use (see the .cpp -
    // loadShaderBufferVisualization() isn't called eagerly at startup).
    static void visualizeBuffers(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst, U32 bufferIndex, bool blend_vertical = false);

    // KRLV_TOUCHPOINT: @camdrawmin/@camdrawmax/@camdrawalphamin/@camdrawalphamax/@camdrawcolor -
    // a distance-based fog blend toward fogColor, driven entirely by krlv/'s own restriction
    // state (see KRlv::getCamDrawParams(), krlvcamera.h). See krlv/README.md's Camera section.
    //
    // KRLV_TOUCHPOINT: @camtextures/@setcam_textures share this SAME pass rather than a separate
    // one - camTexturesActive/camTexturesSubstitute (see KRlv::isCamTexturesActive(),
    // krlvcamera.h) gate an independent blank-toward-substitute-texture branch, stencil-masked
    // so already-lit worn-attachment pixels (tagged by DXStateCache::tagAttachmentStencil()
    // during the geometry pass) are skipped. camTexturesSubstitute may be null (falls back to
    // LLViewerFetchedTexture::sDefaultImagep, the spec's own default grey).
    static void applyKrlvCamDrawFog(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst,
        F32 minDist, F32 maxDist, F32 minAlpha, F32 maxAlpha,
        F32 fogColorR, F32 fogColorG, F32 fogColorB,
        bool camTexturesActive, LLViewerFetchedTexture* camTexturesSubstitute);

    // The non-HDR gamma-correct-only path (no tonemap curve) - picks the legacy or deferred
    // program based on the current sky's reflection-probe ambiance.
    static void gammaCorrect(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst);

    // AMD FidelityFX Contrast Adaptive Sharpening pass - falls back to a plain copy
    // (copyRenderTarget() below) when RenderCASSharpness is 0 or either CAS shader failed to
    // compile.
    static void applyCAS(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst);

    // Falls back to a plain copy (copyRenderTarget()) when FXAA isn't the active RenderFSAAType
    // or its shader/mFXAAMap aren't ready.
    static void applyFXAA(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst);

    // applySMAA() always runs right after generateSMAABuffers() when RenderFSAAType==2, sharing
    // mFXAAMap/mSMAABlendBuffer as the edge-detect/blend-weights intermediates.
    // generateSMAABuffers() is a no-op unless its shaders + both those render targets are
    // ready; applySMAA() falls back to a plain copy (copyRenderTarget()) in that case, same
    // convention as applyFXAA()/applyCAS().
    static void generateSMAABuffers(LLPipeline& pipeline, LLRenderTarget* src);
    static void applySMAA(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst);

    // combineGlow() always runs right after generateGlow() in the post-fx chain, consuming its
    // mGlow[1] output. generateGlow() ping-pongs a separable blur through mGlow[0]/mGlow[1]
    // when sRenderGlow is on, or just clears mGlow[1] when it's off.
    static void generateGlow(LLPipeline& pipeline, LLRenderTarget* src);
    static void combineGlow(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst);

    // Copies `src` into `dst` via gCopyDepthProgram, binding mRT->deferredScreen's depth
    // alongside it (the SSR ray march, run next frame, needs this frame's depth to reproject
    // against) - no-op if RenderScreenSpaceReflections is off or during a reflection-probe/
    // snapshot capture.
    static void copyScreenSpaceReflections(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst);

    // Only ever called as a pair, by llgltfmaterialpreviewmgr.cpp's GLTF material-preview
    // exposure bake - the main render path's auto-exposure HDR chain is still a no-op under
    // DX_RENDER (see the .cpp). generateLuminance() downsamples `src` into a 1x1 luminance
    // sample via gLuminanceProgram; generateExposure() derives an adapted exposure value from
    // that sample (optionally blending with `mLastExposure`'s history when use_history is true).
    static void generateLuminance(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst);
    static void generateExposure(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst, bool use_history = true);

    // The shared "just blit src into dst, no post-fx" fallback applyCAS()/applyFXAA()/
    // applySMAA()/applyFSR1Rcas() drop into when their real shader path isn't usable. Also
    // called directly by renderDoF() when depth of field is disabled.
    static void copyRenderTarget(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst);

    // AMD FSR1's EASU-then-RCAS two-pass upscale chain, called from
    // presentDeferredScreen()/presentFinal(). applyFSR1Rcas() always binds its own dst and
    // falls back to copyRenderTarget() above when RenderFSR1Sharpness is 0 or either RCAS
    // shader failed to compile; applyFSR1Easu()/applyFSR1RcasToBoundTarget() instead draw into
    // whatever target the caller already bound (see the .cpp for why).
    static bool applyFSR1Easu(LLPipeline& pipeline, LLRenderTarget* src, U32 dest_width, U32 dest_height);
    static void applyFSR1Rcas(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst);
    static bool applyFSR1RcasToBoundTarget(LLPipeline& pipeline, LLRenderTarget* src);

    // ================================================================================
    // Shader binding, reflection probes, and screen-space reflections
    // ================================================================================

    // bindDeferredShaderFast()'s only caller is itself, calling all three together.
    // bindLightFunc() binds the BRDF LUT + light falloff lookup texture; bindShadowMaps() binds
    // the 4 sun cascades + 2 spot shadow maps; bindDeferredShaderFast() is
    // bindDeferredShader()'s cheap re-bind path for a shader that was already fully bound this
    // frame (LLHLSLShader::mCanBindFast).
    static void bindLightFunc(LLPipeline& pipeline, LLHLSLShader& shader);
    static void bindShadowMaps(LLPipeline& pipeline, LLHLSLShader& shader);
    static void bindDeferredShaderFast(LLPipeline& pipeline, LLHLSLShader& shader);

    // High external call traffic - dxdrawpoolalpha.cpp/dxdrawpoolavatar.cpp/dxdrawpoolbump.cpp/
    // dxdrawpoolmaterials.cpp/dxdrawpoolwater.cpp/gltfscenemanager.cpp/
    // llgltfmaterialpreviewmgr.cpp plus dozens of internal lighting-pass call sites. The
    // slow/full bind path for a deferred-pass shader - every DEFERRED_* texture channel, shadow
    // matrices, SSAO/shadow-bias uniforms, legacy env-map cubemap fallback, sun/moon color - and
    // its exact inverse teardown.
    static void bindDeferredShader(LLPipeline& pipeline, LLHLSLShader& shader, LLRenderTarget* light_target = nullptr, LLRenderTarget* depth_target = nullptr);
    static void unbindDeferredShader(LLPipeline& pipeline, LLHLSLShader& shader);

    // The reflection-probe + screen-space-reflection sub-cluster - bindReflectionProbes() calls
    // setEnvMat()/bindSSRUniforms() directly, and renderSSRPass()/renderSSRResolve() are the SSR
    // trace + temporal resolve pair that always run back-to-back. bindDeferredShader() calls
    // bindReflectionProbes(); unbindDeferredShader() calls unbindReflectionProbes().
    static void setEnvMat(LLPipeline& pipeline, LLHLSLShader& shader);
    static void bindReflectionProbes(LLPipeline& pipeline, LLHLSLShader& shader);
    static void unbindReflectionProbes(LLPipeline& pipeline, LLHLSLShader& shader);
    static void bindSSRUniforms(LLPipeline& pipeline, LLHLSLShader& shader);
    static void bindSSRBuffer(LLPipeline& pipeline, LLHLSLShader& shader);
    static void renderSSRPass(LLPipeline& pipeline);
    static void renderSSRResolve(LLPipeline& pipeline);

    // ================================================================================
    // Render-pass dispatch (per-pool draw-call issuing)
    // ================================================================================

    // renderObjects() also has real external callers (llmaniptranslate.cpp, llviewerdisplay.cpp).
    static void renderObjects(LLPipeline& pipeline, U32 type, bool texture = true, bool batch_texture = false, bool rigged = false);
    static void renderGLTFObjects(LLPipeline& pipeline, U32 type, bool texture = true, bool rigged = false);
    static void renderAlphaObjects(LLPipeline& pipeline, bool rigged = false);
    static void renderMaskedObjects(LLPipeline& pipeline, U32 type, bool texture = true, bool batch_texture = false, bool rigged = false);
    static void renderFullbrightMaskedObjects(LLPipeline& pipeline, U32 type, bool texture = true, bool batch_texture = false, bool rigged = false);

    // ================================================================================
    // Shadow generation
    // ================================================================================

    // renderShadow() draws one shadow cascade/spot's geometry (opaque + alpha/masked/
    // fullbright-masked + GLTF, static and rigged) into the currently-bound shadow map target.
    // getVisiblePointCloud() is the camera-frustum/AABB clipping math that bounds a shadow
    // cascade's frustum to the visible scene extent. Both are called from generateSunShadow()
    // below.
    static void renderShadow(LLPipeline& pipeline, const glm::mat4& view, const glm::mat4& proj, LLCamera& shadow_cam, LLCullResult& result, bool depth_clamp);
    static bool getVisiblePointCloud(LLPipeline& pipeline, LLCamera& camera, LLVector3& min, LLVector3& max, std::vector<LLVector3>& fp, LLVector3 light_dir = LLVector3(0, 0, 0));

    // The big per-frame orchestrator, called once/frame from llviewerdisplay.cpp. Fits the sun
    // shadow cascades (4, cascade-split/fit math + per-cascade renderShadow()) and the 2
    // spot-light shadows into their render targets, computing mSunShadowMatrix[]/
    // mShadowModelview[]/mShadowProjection[] for renderDeferredLighting()'s sun-shadow/SSAO
    // lightmap pass to sample later.
    static void generateSunShadow(LLPipeline& pipeline, LLCamera& camera);

    // ================================================================================
    // Lighting setup
    // ================================================================================

    // setupAvatarLights() sets hardware light slot 1 (edit-mode key light, or the avatar
    // backlight); calcNearbyLights() rebuilds the camera-distance-sorted "lights near camera"
    // candidate list (mNearbyLights) each frame, with per-light fade-in/out state;
    // setupHWLights() consumes that list to fill the 8 hardware light slots (sun/moon + up to 6
    // local lights) every frame.
    static void setupAvatarLights(LLPipeline& pipeline, bool for_edit = false);
    static void calcNearbyLights(LLPipeline& pipeline, LLCamera& camera);
    static void setupHWLights(LLPipeline& pipeline);

    // All thin wrappers around enableLights()'s hardware light-mask toggle. enableLights() is
    // the one real chokepoint - sets the 8 fixed-function light enable/diffuse states from a
    // bitmask; the rest just compute a mask (or, for Preview/AvatarEdit, bind extra lights
    // directly) and call it. setLight() instead just inserts/erases a drawable into/from the
    // plain "has a light attached" set (mLights), unrelated to the per-frame nearby-lights
    // candidate list above.
    static void enableLights(LLPipeline& pipeline, U32 mask);
    static void enableLightsDynamic(LLPipeline& pipeline);
    static void enableLightsAvatar(LLPipeline& pipeline);
    static void enableLightsPreview(LLPipeline& pipeline);
    static void enableLightsAvatarEdit(LLPipeline& pipeline, const LLColor4& color);
    static void enableLightsFullbright(LLPipeline& pipeline);
    static void disableLights(LLPipeline& pipeline);
    static void setLight(LLPipeline& pipeline, LLDrawable* drawablep, bool is_light);

    // Builds the spot/projector light's screen-space-to-light-space projection matrix and
    // uploads its PROJECTOR_* uniforms (shadow index/fade, specular caps, projection texture +
    // focus/LOD), and runs the per-frame shadow-slot priority tournament
    // (mTargetShadowSpotLight[]) that generateSunShadow() reads back next.
    static void setupSpotLight(LLPipeline& pipeline, LLHLSLShader& shader, LLDrawable* drawablep);

    // ================================================================================
    // Avatar impostor baking
    // ================================================================================

    // profileAvatar() GPU-times a whole avatar (or each attachment individually) via a profile
    // query; generateImpostor() is the real impostor-texture bake (also reused, with
    // for_profile true, as profileAvatar()'s actual draw call) - renders the avatar alone from a
    // small avatar-facing camera into avatar->mImpostor.
    static void profileAvatar(LLPipeline& pipeline, LLVOAvatar* avatar, bool profile_attachments = false);
    static void generateImpostor(LLPipeline& pipeline, LLVOAvatar* avatar, bool preview_avatar = false, bool for_profile = false, LLViewerObject* specific_attachment = nullptr);

    // ================================================================================
    // Render-type / render-debug bitmask plumbing, visibility toggles, beacons
    // ================================================================================

    // Plain bitmask plumbing over mRenderTypeEnabled[NUM_RENDER_TYPES] - varargs setters/
    // testers, a push/pop save-stack (serialized as raw bytes), and the two branchless
    // set-all/clear-all memset forms.
    static bool hasRenderType(const LLPipeline& pipeline, const U32 type);
    static void setRenderTypeMask(LLPipeline& pipeline, U32 type, ...);
    static bool hasAnyRenderType(const LLPipeline& pipeline, U32 type, ...);
    static void pushRenderTypeMask(LLPipeline& pipeline);
    static void popRenderTypeMask(LLPipeline& pipeline);
    static void andRenderTypeMask(LLPipeline& pipeline, U32 type, ...);
    static void clearRenderTypeMask(LLPipeline& pipeline, U32 type, ...);
    static void setAllRenderTypes(LLPipeline& pipeline);
    static void clearAllRenderTypes(LLPipeline& pipeline);

    // Pathfinding-console object-visibility bookkeeping (hide/restore permanent objects +
    // terrain skip-render toggle, used while editing navmeshes) and the drawable-visibility
    // pair it's built on; skipRenderingShadows() clears every sun-shadow target to black (used
    // by generateSunShadow() when the sun diffuse color is fully black); rebuildDrawInfo()/
    // rebuildTerrain() force a full geometry/terrain-patch rebuild (Develop menu "Rebuild
    // Terrain"/texture-reload paths).
    static void hidePermanentObjects(LLPipeline& pipeline, std::vector<U32>& restoreList);
    static void restorePermanentObjects(LLPipeline& pipeline, const std::vector<U32>& restoreList);
    static void skipRenderingOfTerrain(LLPipeline& pipeline, bool flag);
    static void hideObject(LLPipeline& pipeline, const LLUUID& id);
    static void hideDrawable(LLPipeline& pipeline, LLDrawable* drawable);
    static void unhideDrawable(LLPipeline& pipeline, LLDrawable* drawable);
    static void restoreHiddenObject(LLPipeline& pipeline, const LLUUID& id);
    static void skipRenderingShadows(LLPipeline& pipeline);
    static void rebuildDrawInfo(LLPipeline& pipeline);
    static void rebuildTerrain(LLPipeline& pipeline);

    // Develop menu render-debug-info toggles, and the beacon/highlight visibility checkboxes
    // (scripted/scripted-touch/MOAP/physical/particle/sound/generic beacons, selection
    // highlights). pushRenderDebugFeatureMask()/popRenderDebugFeatureMask() have genuinely zero
    // callers anywhere, kept for debug/future use.
    static void toggleRenderType(LLPipeline& pipeline, U32 type, bool mark_dirty = true);
    static bool hasRenderTypeControl(LLPipeline& pipeline, U32 data);
    static void toggleRenderDebug(LLPipeline& pipeline, U64 data);
    static void toggleRenderDebugFeature(LLPipeline& pipeline, U32 data);
    static void toggleRenderTypeControl(LLPipeline& pipeline, U32 data);
    static bool toggleRenderTypeControlNegated(LLPipeline& pipeline, S32 data);
    static bool toggleRenderDebugControl(LLPipeline& pipeline, U64 data);
    static bool toggleRenderDebugFeatureControl(LLPipeline& pipeline, U32 data);
    static void setRenderDebugFeatureControl(LLPipeline& pipeline, U32 bit, bool value);
    static void pushRenderDebugFeatureMask(LLPipeline& pipeline);
    static void popRenderDebugFeatureMask(LLPipeline& pipeline);

    static void setRenderParticleBeacons(bool val);
    static void toggleRenderParticleBeacons();
    static bool getRenderParticleBeacons();

    static void setRenderSoundBeacons(bool val);
    static void toggleRenderSoundBeacons();
    static bool getRenderSoundBeacons();

    static void setRenderMOAPBeacons(bool val);
    static void toggleRenderMOAPBeacons();
    static bool getRenderMOAPBeacons();

    static void setRenderPhysicalBeacons(bool val);
    static void toggleRenderPhysicalBeacons();
    static bool getRenderPhysicalBeacons();

    static void setRenderScriptedBeacons(bool val);
    static void toggleRenderScriptedBeacons();
    static bool getRenderScriptedBeacons();

    static void setRenderScriptedTouchBeacons(bool val);
    static void toggleRenderScriptedTouchBeacons();
    static bool getRenderScriptedTouchBeacons();

    static void setRenderBeacons(bool val);
    static void toggleRenderBeacons();
    static bool getRenderBeacons();

    static void setRenderHighlights(bool val);
    static void toggleRenderHighlights();
    static bool getRenderHighlights();
    static void setRenderHighlightTextureChannel(LLRender::eTexIndex channel);

    // Backing storage for the beacon/highlight toggles above lives in the unified
    // static-member block under "Cached settings" below, ordered by first use like
    // everything else there.

    // ================================================================================
    // Pool management
    // ================================================================================

    // Backend-agnostic pool-management bookkeeping - owns mPools, the per-type quick-lookup
    // pointers, and the texture-keyed tree/terrain pool maps.
    static LLDrawPool* findPool(LLPipeline& pipeline, const U32 pool_type, LLViewerTexture* tex0 = NULL);
    static LLDrawPool* getPool(LLPipeline& pipeline, const U32 pool_type, LLViewerTexture* tex0 = NULL);
    static LLDrawPool* getPoolFromTE(LLPipeline& pipeline, const LLTextureEntry* te, LLViewerTexture* te_image);
    static void addPool(LLPipeline& pipeline, LLDrawPool* poolp);
    static void removePool(LLPipeline& pipeline, LLDrawPool* poolp);
    static void rebuildPools(LLPipeline& pipeline);
    static void addToQuickLookup(LLPipeline& pipeline, LLDrawPool* new_poolp);
    static void removeFromQuickLookup(LLPipeline& pipeline, LLDrawPool* poolp);
    static void resetDrawOrders(LLPipeline& pipeline);

    // ================================================================================
    // Drawable/object lifecycle
    // ================================================================================

    static void allocDrawable(LLPipeline& pipeline, LLViewerObject* vobj);
    static void unlinkDrawable(LLPipeline& pipeline, LLDrawable* drawable);
    static U32 addObject(LLPipeline& pipeline, LLViewerObject* vobj);
    static void createObjects(LLPipeline& pipeline, F32 max_dtime);
    static void createObject(LLPipeline& pipeline, LLViewerObject* vobj);
    static void markShift(LLPipeline& pipeline, LLDrawable* drawablep);
    static void shiftObjects(LLPipeline& pipeline, const LLVector3& offset);
    static void markTextured(LLPipeline& pipeline, LLDrawable* drawablep);
    static void processPartitionQ(LLPipeline& pipeline);

    // The TLS-deferred family - each checks sTLSDeferredPipelineMarks (extern, shared with
    // updateMoveDampedAsync()/updateMoveNormalAsync() below) first and, if set (called from a
    // DXPool worker thread during LLViewerObjectList::update()'s parallel dispatch, see
    // LLDeferredPipelineMarks's own comment in pipeline.h), defers itself into mActions for
    // later serial replay on the main thread instead of mutating shared unlocked containers
    // directly.
    static void setDeferredMarksForThisThread(LLDeferredPipelineMarks* marks);
    static void markMoved(LLPipeline& pipeline, LLDrawable* drawablep, bool damped_motion = false);
    static void markGLRebuild(LLPipeline& pipeline, LLGLUpdate* glu);
    static void markPartitionMove(LLPipeline& pipeline, LLDrawable* drawable);
    static void markMeshDirty(LLPipeline& pipeline, LLSpatialGroup* group);
    static void markRebuild(LLPipeline& pipeline, LLSpatialGroup* group);
    static void markRebuild(LLPipeline& pipeline, LLDrawable* drawablep, LLDrawable::EDrawableFlags flag = LLDrawable::REBUILD_ALL);

    // ================================================================================
    // Atmospherics / water
    // ================================================================================

    static void doAtmospherics(LLPipeline& pipeline);
    static void doWaterHaze(LLPipeline& pipeline);
    static void doWaterExclusionMask(LLPipeline& pipeline);

    // ================================================================================
    // Cached settings
    // ================================================================================

    // Populated by refreshCachedSettings(), called once from LLPipeline::refreshCachedSettings()
    // itself (the listener registrations in LLPipeline::init() stay there unchanged - each one
    // just re-runs the whole refreshCachedSettings() cascade on a settings-change signal, not a
    // per-variable callback).
    static void refreshCachedSettings();

    static S32 sUseOcclusion; // 0 = no occlusion, 1 = read only, 2 = read/write
    static bool sRenderingHUDs;
    static bool sUnderWaterRender;
    static bool RenderScreenSpaceReflections;
    static bool RenderDepthOfField;
    static U32 RenderFSAAType;
    static bool RenderDepthOfFieldInEditMode;
    static S32 RenderBufferVisualization;
    static bool RenderDeferredAtmospheric;
    static bool RenderDeferredSSAO;
    static S32 RenderShadowDetail;
    static LLVector3 RenderShadowGaussian;
    static F32 RenderShadowBlurSize;
    static F32 RenderShadowBlurDistFactor;
    // sReflectionProbesEnabled (RenderReflectionsEnabled) gates whether the probe-capture pipeline runs
    // at all, but reflectionProbeF.hlsl's legacy/environmentMap fallback decides per-pixel based on a
    // SEPARATE setting, RenderReflectionProbesEnabled - it takes the legacy branch whenever EITHER is
    // off. C++ producers (LLVOSky::updateSky(), bindDeferredShader()'s environmentMap bind) must key on
    // shouldUseLegacyEnvMap(), not sReflectionProbesEnabled alone, to stay in sync with the shader's
    // condition.
    static bool sReflectionProbesEnabled;
    static bool sRenderAttachedLights;
    static S32 sVisibleLightCount;
    static bool sRenderGlow;
    static F32 RenderGlowMaxExtractAlpha;
    static F32 RenderGlowWarmthAmount;
    static LLVector3 RenderGlowLumWeights;
    static LLVector3 RenderGlowWarmthWeights;
    static bool RenderGlowNoise;
    static S32 RenderGlowResolutionPow;
    static S32 RenderGlowIterations;
    static F32 RenderGlowWidth;
    static F32 RenderGlowStrength;
    static F32 RenderDeferredSunWash;
    static F32 RenderShadowNoise;
    static F32 RenderSSAOScale;
    static U32 RenderSSAOMaxScale;
    static F32 RenderSSAOFactor;
    static LLVector3 RenderSSAOEffect;
    static F32 RenderShadowBiasError;
    static F32 RenderShadowBias;
    static F32 RenderShadowOffset;
    static F32 RenderSpotShadowOffset;
    static F32 RenderSpotShadowBias;
    static F32 RenderEdgeDepthCutoff;
    static F32 RenderEdgeNormCutoff;
    static bool RenderMirrors;
    static S32 RenderScreenSpaceReflectionIterations;
    static S32 RenderWaterSSRIterations;
    static F32 RenderScreenSpaceReflectionRayStep;
    static F32 RenderWaterSSRRayStep;
    static F32 RenderScreenSpaceReflectionDistanceBias;
    static S32 RenderScreenSpaceReflectionGlossySamples;
    static F32 RenderScreenSpaceReflectionDepthRejectBias;
    static F32 RenderScreenSpaceReflectionAdaptiveStepMultiplier;
    static F32 RenderScreenSpaceReflectionGlossThreshold;
    static bool sShadowRender;
    static bool sRenderDeferred;
    static bool CameraOffset;
    static F32 RenderFarClip;
    static LLVector3 RenderShadowSplitExponent;
    static S32 RenderShadowSplits;
    static F32 RenderShadowErrorCutoff;
    static F32 RenderShadowFOVCutoff;
    static LLDrawable* RenderSpotLight;
    static bool sReflectionRender;
    static bool RenderSpotLightsInNondeferred;
    static LLColor4 PreviewAmbientColor;
    static LLColor4 PreviewDiffuse0;
    static LLColor4 PreviewSpecular0;
    static LLColor4 PreviewDiffuse1;
    static LLColor4 PreviewSpecular1;
    static LLColor4 PreviewDiffuse2;
    static LLColor4 PreviewSpecular2;
    static LLVector3 PreviewDirection0;
    static LLVector3 PreviewDirection1;
    static LLVector3 PreviewDirection2;
    static bool sImpostorRender;
    static bool sImpostorRenderAlphaDepthPass;
    static bool sRenderScriptedBeacons;
    static bool sRenderScriptedTouchBeacons;
    static bool sRenderMOAPBeacons;
    static bool sRenderPhysicalBeacons;
    static bool sRenderParticleBeacons;
    static bool sRenderSoundBeacons;
    static bool sRenderBeacons;
    static bool sRenderHighlight;
    static LLRender::eTexIndex sRenderHighlightTextureChannel;
    static bool RenderDelayCreation;
    static bool RenderAnimateRes;
    static bool sAutoMaskAlphaDeferred;
    static bool sAutoMaskAlphaNonDeferred;
    static bool sUseFarClip;
    static bool RenderUIBuffer;
    static F32 RenderShadowResolutionScale;
    static bool FreezeTime;
    static S32 DebugBeaconLineWidth;
    static F32 CameraFocusTransitionTime;
    static F32 CameraFNumber;
    static F32 CameraFocalLength;
    static F32 CameraFieldOfView;
    static F32 CameraMaxCoF;
    static F32 CameraDoFResScale;
    static F32 CameraAspectRatio;
    static F32 RenderAutoHideSurfaceAreaLimit;
    static bool RenderAvatarCloth;
    static bool sRenderTransparentWater;
    // Object whose linked set is culled from the current reflection-probe capture (the probe's own object).
    static LLViewerObject* sCaptureExcludeObject;
    static bool sPickAvatar;
    static S32 sCompiles;
    static bool sDynamicLOD;
    static bool sRenderAttachedParticles;
    static LLTrace::EventStatHandle<S64> sStatBatchSize;
    static const U32 MAX_PREVIEW_WIDTH;
    static bool sShowHUDAttachments;
    static bool sForceOldBakedUpload; // If true will not use capabilities to upload baked textures.
    static bool sBakeSunlight;
    static bool sNoAlpha;
    static bool sTextureBindTest;

    // ================================================================================
    // Per-frame cull-result utilities and small helpers
    // ================================================================================

    // sCull (extern) is the per-frame cull result, set/cleared by grabReferences()/
    // clearReferences() and read by the begin*/end* accessors below.
    static bool shouldUseLegacyEnvMap();
    static bool isWaterClip();
    static F32 calcPixelArea(LLVector3 center, LLVector3 size, LLCamera& camera);
    static F32 calcPixelArea(const LLVector4a& center, const LLVector4a& size, LLCamera& camera);
    static LLSpatialPartition* getSpatialPartition(LLPipeline& pipeline, LLViewerObject* vobj);
    static void resetVertexBuffers(LLPipeline& pipeline, LLDrawable* drawable);
    static void addDebugBlip(LLPipeline& pipeline, const LLVector3& position, const LLColor4& color);
    static void addTrianglesDrawn(LLPipeline& pipeline, S32 index_count);
    static void recordTrianglesDrawn(LLPipeline& pipeline);
    static LLRenderTarget* getSunShadowTarget(LLPipeline& pipeline, U32 i);
    static LLRenderTarget* getSpotShadowTarget(LLPipeline& pipeline, U32 i);
    static void grabReferences(LLPipeline& pipeline, LLCullResult& result);
    static void clearReferences(LLPipeline& pipeline);
    static LLCullResult::drawinfo_iterator beginRenderMap(LLPipeline& pipeline, U32 type);
    static LLCullResult::drawinfo_iterator endRenderMap(LLPipeline& pipeline, U32 type);
    static LLCullResult::sg_iterator beginAlphaGroups(LLPipeline& pipeline);
    static LLCullResult::sg_iterator endAlphaGroups(LLPipeline& pipeline);
    static LLCullResult::sg_iterator beginRiggedAlphaGroups(LLPipeline& pipeline);
    static LLCullResult::sg_iterator endRiggedAlphaGroups(LLPipeline& pipeline);
    static void markAllGeometryDirty(LLPipeline& pipeline);

    // ================================================================================
    // Culling, occlusion, and the update/rebuild queues
    // ================================================================================

    static void markVisible(LLPipeline& pipeline, LLDrawable* drawablep, LLCamera& camera);
    static void markNotCulled(LLPipeline& pipeline, LLSpatialGroup* group, LLCamera& camera);
    static void markOccluder(LLPipeline& pipeline, LLSpatialGroup* group);
    static void renderGeomShadow(LLPipeline& pipeline, LLCamera& camera);

    // clearRebuildDrawables() is zero-caller tree-wide, kept for debug/future use.
    static void doOcclusion(LLPipeline& pipeline, LLCamera& camera);
    static void updateMove(LLPipeline& pipeline);
    static void updateMovedList(LLPipeline& pipeline, LLDrawable::drawable_vector_t& moved_list);
    static bool updateDrawableGeom(LLPipeline& pipeline, LLDrawable* drawablep);
    static void updateGL(LLPipeline& pipeline);
    static void clearRebuildGroups(LLPipeline& pipeline);
    static void clearRebuildDrawables(LLPipeline& pipeline);
    static void rebuildPriorityGroups(LLPipeline& pipeline, F32 max_dtime);
    static void updateGeom(LLPipeline& pipeline, F32 max_dtime);

    // ================================================================================
    // Debug-visualization rendering and picking/intersection
    // ================================================================================

    // renderSelectedFaces()/renderHighlights()/renderPhysicsDisplay() are zero-caller tree-wide,
    // kept for debug/future use - renderHighlights() in particular is a known "still-unwired
    // gap" (not driven by any Render Metadata checkbox). renderDebug() has exactly 1 real caller
    // (presentDeferredScreen() above). The lineSegmentIntersect* family has real external
    // callers in llviewerwindow.cpp.
    static void renderSelectedFaces(LLPipeline& pipeline, const LLColor4& color);
    static void renderHighlights(LLPipeline& pipeline);
    static void renderPhysicsDisplay(LLPipeline& pipeline);
    static void renderDebug(LLPipeline& pipeline);
    static LLVOPartGroup* lineSegmentIntersectParticle(LLPipeline& pipeline, const LLVector4a& start, const LLVector4a& end, LLVector4a* intersection,
        S32* face_hit);
    static LLViewerObject* lineSegmentIntersectInWorld(LLPipeline& pipeline, const LLVector4a& start, const LLVector4a& end,
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
    );
    static LLViewerObject* lineSegmentIntersectInHUD(LLPipeline& pipeline, const LLVector4a& start, const LLVector4a& end,
        bool pick_transparent,
        S32* face_hit,
        LLVector4a* intersection,
        LLVector2* tex_coord,
        LLVector4a* normal,
        LLVector4a* tangent
    );

    // ================================================================================
    // State sort / post sort (per-frame visibility -> draw-order resolution)
    // ================================================================================

    // The 3 group/bridge/drawable overloads are only ever called from the main (camera, result)
    // overload's own body. The LLPipeline& pipeline parameter is kept on all 4 for
    // calling-convention consistency even though the bridge overload genuinely touches no
    // pipeline member.
    static void stateSort(LLPipeline& pipeline, LLCamera& camera, LLCullResult& result);
    static void stateSort(LLPipeline& pipeline, LLSpatialGroup* group, LLCamera& camera);
    static void stateSort(LLPipeline& pipeline, LLSpatialBridge* bridge, LLCamera& camera, bool fov_changed);
    static void stateSort(LLPipeline& pipeline, LLDrawable* drawablep, LLCamera& camera);
    static void postSort(LLPipeline& pipeline, LLCamera& camera);
    static void forAllVisibleDrawables(LLPipeline& pipeline, void (*func)(LLDrawable*));

    // ================================================================================
    // Small standalone utilities
    // ================================================================================

    // getPoolTypeFromTE()/removeMutedAVsLights()/updateRenderTransparentWater()/
    // getBrdfLutAssetPath() keep their original no-pipeline-param static signature - they were
    // never instance methods to begin with. canUseAntiAliasing() is zero-caller tree-wide, kept
    // for debug/future use.
    static bool shadersLoaded(LLPipeline& pipeline);
    static bool canUseWindLightShaders(const LLPipeline& pipeline);
    static bool canUseAntiAliasing(const LLPipeline& pipeline);
    static void unloadShaders(LLPipeline& pipeline);
    static U32 getPoolTypeFromTE(const LLTextureEntry* te, LLViewerTexture* imagep);
    static void removeMutedAVsLights(LLVOAvatar* muted_avatar);
    static void resetFrameStats(LLPipeline& pipeline);
    static void updateRenderTransparentWater();
    static bool getVisibleExtents(LLPipeline& pipeline, LLCamera& camera, LLVector3& min, LLVector3& max);
    static std::string getBrdfLutAssetPath();

    // ================================================================================
    // Move-list updates and the per-frame cull-result builder
    // ================================================================================

    // Same capture-by-value-pointer pattern as the markMoved() family above - a value-captured
    // LLPipeline* has the same lifetime semantics the original "this"-pointer capture relied on,
    // safe because the pointee (gPipeline) is a long-lived global singleton.
    static void updateMoveDampedAsync(LLPipeline& pipeline, LLDrawable* drawablep);
    static void updateMoveNormalAsync(LLPipeline& pipeline, LLDrawable* drawablep);
    static void updateCull(LLPipeline& pipeline, LLCamera& camera, LLCullResult& result);

    // ================================================================================
    // Screen/shadow buffer allocate, resize, release
    // ================================================================================

    static void requestResizeScreenTexture(LLPipeline& pipeline);
    static void requestResizeShadowTexture(LLPipeline& pipeline);
    static void resizeShadowTexture(LLPipeline& pipeline);
    static void resizeScreenTexture(LLPipeline& pipeline);
    static bool allocateScreenBuffer(LLPipeline& pipeline, U32 resX, U32 resY);
    // Returns an LLPipeline::eFBOStatus value, widened to int here so this header never needs the
    // complete LLPipeline type (it only forward-declares the class) - the .cpp, which does include
    // pipeline.h in full, stores/compares the real enum internally.
    static int doAllocateScreenBuffer(LLPipeline& pipeline, U32 resX, U32 resY);
    static bool allocateScreenBufferInternal(LLPipeline& pipeline, U32 resX, U32 resY);
    static bool allocateShadowBuffer(LLPipeline& pipeline, U32 resX, U32 resY);
    static void releaseGLBuffers(LLPipeline& pipeline);
    static void releaseLUTBuffers(LLPipeline& pipeline);
    static void releaseShadowBuffers(LLPipeline& pipeline);
    static void releaseScreenBuffers(LLPipeline& pipeline);
    static void releaseSunShadowTarget(LLPipeline& pipeline, U32 index);
    static void releaseSunShadowTargets(LLPipeline& pipeline);
    static void releaseSpotShadowTargets(LLPipeline& pipeline);
    static void createGLBuffers(LLPipeline& pipeline);
    static void createLUTBuffers(LLPipeline& pipeline);
    static bool bakeBrdfLut(LLPipeline& pipeline);

    // ================================================================================
    // Class lifecycle (construction/teardown, settings-signal plumbing)
    // ================================================================================

    // The LLPipeline constructor/destructor themselves cannot become DXPipeline:: statics (not
    // ordinary member functions) and live in dxpipeline.cpp as plain out-of-class LLPipeline::
    // member definitions instead - as do the ~140 LLPipeline:: static-member storage
    // definitions. connectRefreshCachedSettingsSafe() needs no pipeline capture in its signal
    // lambda, unlike the markMoved() family - refreshCachedSettings() itself is a zero-arg
    // static with no pipeline dependency.
    static void init(LLPipeline& pipeline);
    static void cleanup(LLPipeline& pipeline);
    static void connectRefreshCachedSettingsSafe(LLPipeline& pipeline, const std::string& name);
    static void destroyGL(LLPipeline& pipeline);
    static void restoreGL(LLPipeline& pipeline);

    // ================================================================================
    // Depth of field, reference-tracking debug checks, and the last odds and ends
    // ================================================================================

    // checkReferences()'s 3 non-LLDrawInfo overloads have real callers (LLDrawable/LLFace/
    // LLSpatialGroup destructors), but the bodies themselves are `#if 0`'d out to a no-op.
    static void renderDoF(LLPipeline& pipeline, LLRenderTarget* src, LLRenderTarget* dst);
    static void checkReferences(LLPipeline& pipeline, LLFace* face);
    static void checkReferences(LLPipeline& pipeline, LLDrawable* drawable);
    static void checkReferences(LLPipeline& pipeline, LLDrawInfo* draw_info);
    static void checkReferences(LLPipeline& pipeline, LLSpatialGroup* group);

    // Both zero-caller tree-wide, kept for debug/future use. handleShadowDetailChanged() has a
    // real live listener for the "RenderShadowDetail" setting under a different name
    // (handleShaderAndBufferChanged(), llviewercontrol.cpp) that does NOT replicate this
    // function's "shadow detail increased -> immediately clear shadow maps" shortcut - bug vs.
    // intentionally-dropped optimization is still an open question.
    static void findReferences(LLPipeline& pipeline, LLDrawable* drawablep);
    static void handleShadowDetailChanged(LLPipeline& pipeline);
};
