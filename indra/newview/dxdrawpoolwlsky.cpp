/**
 * @file dxdrawpoolwlsky.cpp
 * @brief Fresh DX11-native implementation of LLDrawPoolWLSky's windlight
 * sky dome/clouds/stars/sun/moon render path.
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

#include "llviewerprecompiledheaders.h"

#include "dxdrawpoolwlsky.h"

#include "lldrawpoolwlsky.h"
#include "llface.h"
#include "llrender.h"
#include "llenvironment.h"
#include "llhlslshader.h"
#include "llgl.h"
#include "llviewershadermgr.h"
#include "llviewercamera.h"
#include "pipeline.h"
#include "llsky.h"
#include "llvowlsky.h"
#include "llsettingsvo.h"
#include "llviewercontrol.h"

extern bool gCubeSnapshot;
extern LLPointer<LLImageGL> gEXRImage;

namespace
{
    LLStaticHashedString sCamPosLocal("camPosLocal");
    LLStaticHashedString sCustomAlpha("custom_alpha");

    // S24 (task #279 stage 2, "RENDER WOW"): KVTweaks-exposed night-sky
    // controls - see starsF.hlsl for consumption and settings.xml for the
    // RenderStar*/RenderNebula*/RenderShootingStar* keys these read.
    LLStaticHashedString sStarGlow("star_glow");
    LLStaticHashedString sStarDensity("star_density");
    LLStaticHashedString sStarDustIntensity("star_dust_intensity");
    LLStaticHashedString sNebulaEnabled("nebula_enabled");
    LLStaticHashedString sNebulaIntensity("nebula_intensity");
    // S24 (task #301/#302): 0=Default, 1=Real Constellations (llvowlsky.cpp
    // placement only), 2=Starry Night (starsF.hlsl blue/gold+swirl).
    LLStaticHashedString sSkyStyle("sky_style");

    // S24 (task #303, "2.5D cloud layers"): extra cloud decks reuse
    // cloudsV/F.hlsl unchanged, just with overridden CLOUD_SCALE/
    // CLOUD_POS_DENSITY1/2 and this tint/alpha pair - see
    // renderSkyCloudsDeferred().
    LLStaticHashedString sCloudLayerTint("cloud_layer_tint");
    LLStaticHashedString sCloudLayerAlphaMult("cloud_layer_alpha_mult");

    LLHLSLShader* cloud_shader = nullptr;
    LLHLSLShader* sky_shader   = nullptr;
    LLHLSLShader* sun_shader   = nullptr;
    LLHLSLShader* moon_shader  = nullptr;

    float sStarTime = 0.f;

    bool use_hdri_sky()
    {
        static LLCachedControl<F32> hdri_split(gSavedSettings, "RenderHDRISplitScreen", 1.f);
        static LLCachedControl<bool> irradiance_only(gSavedSettings, "RenderHDRIIrradianceOnly", false);

        return gCubeSnapshot && (!irradiance_only || !gPipeline.mReflectionMapManager.isRadiancePass()) ? gEXRImage.notNull() :
            gEXRImage.notNull() ? hdri_split > 0.f :
            false;
    }

    // S24 (task #303, "cloud base" height stratification, user follow-up):
    // layer_height_skew is a non-uniform Y-scale applied to the dome, only
    // for the extra cloud-layer draws (default 1.0 = no change, every
    // other caller - sky haze, base cloud layer - is untouched). >1.0
    // stretches the dome taller (a layer's clouds read as sitting higher,
    // closer to zenith); <1.0 squashes it (reads lower, closer to the
    // horizon). NOT true altitude - the dome recenters on the camera every
    // frame (see the translatef() below), so there's no real depth to
    // separate layers in; this is a pure visual skew trick, cheap and
    // effective for the decorative "these read as different heights" cue
    // without touching geometry or the shared sky-haze pass at all.
    void renderDome(const LLVector3& camPosLocal, F32 camHeightLocal, LLHLSLShader* shader, F32 layer_height_skew = 1.0f)
    {
        llassert_always(nullptr != shader);

        gDX.matrixMode(LLRender::MM_MODELVIEW);
        gDX.pushMatrix();

        if (LLPipeline::sReflectionRender && camPosLocal.mV[2] > 256.f)
        {
            gDX.translatef(camPosLocal.mV[0], camPosLocal.mV[1], 256.f - camPosLocal.mV[2] * 0.5f);
        }
        else
        {
            gDX.translatef(camPosLocal.mV[0], camPosLocal.mV[1], camPosLocal.mV[2]);
        }

        // the windlight sky dome works most conveniently in a coordinate
        // system where Y is up, so permute our basis vectors accordingly.
        gDX.rotatef(120.f, 1.f / F_SQRT3, 1.f / F_SQRT3, 1.f / F_SQRT3);

        gDX.scalef(0.333f, 0.333f, 0.333f);

        if (layer_height_skew != 1.0f)
        {
            gDX.scalef(1.0f, layer_height_skew, 1.0f);
        }

        gDX.translatef(0.f, -camHeightLocal, 0.f);

        shader->uniform3f(sCamPosLocal, 0.f, camHeightLocal, 0.f);

        gSky.mVOWLSkyp->drawDome();

        gDX.matrixMode(LLRender::MM_MODELVIEW);
        gDX.popMatrix();
    }

    void renderSkyHazeDeferred(const LLVector3& camPosLocal, F32 camHeightLocal)
    {
        if (!gSky.mVOSkyp)
        {
            return;
        }

        LLVector3 const& origin = LLViewerCamera::getInstance()->getOrigin();

        if (gPipeline.canUseWindLightShaders() && gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_SKY))
        {
            if (use_hdri_sky())
            {
                sky_shader = &gEnvironmentMapProgram;
                sky_shader->bind();
                S32 idx = sky_shader->enableTexture(LLShaderMgr::ENVIRONMENT_MAP);
                if (idx > -1)
                {
                    gDX.getTexUnit(idx)->bind(gEXRImage);
                }

                static LLCachedControl<F32> hdri_exposure(gSavedSettings, "RenderHDRIExposure", 0.0f);
                static LLCachedControl<F32> hdri_rotation(gSavedSettings, "RenderHDRIRotation", 0.f);
                static LLCachedControl<F32> hdri_split(gSavedSettings, "RenderHDRISplitScreen", 1.f);
                static LLStaticHashedString hdri_split_screen("hdri_split_screen");

                LLMatrix3 rot;
                rot.setRot(0.f, hdri_rotation * DEG_TO_RAD, 0.f);

                sky_shader->uniform1f(LLShaderMgr::SKY_HDR_SCALE, powf(2.f, hdri_exposure));
                sky_shader->uniformMatrix3fv(LLShaderMgr::DEFERRED_ENV_MAT, 1, GL_FALSE, (F32*)rot.mMatrix);
                sky_shader->uniform1f(hdri_split_screen, gCubeSnapshot ? 1.f : hdri_split);
            }
            else
            {
                sky_shader->bind();
            }

            LLGLSPipelineDepthTestSkyBox sky(true, true);

            sky_shader->uniform1i(LLShaderMgr::CUBE_SNAPSHOT, gCubeSnapshot ? 1 : 0);

            LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();

            LLViewerTexture* rainbow_tex = gSky.mVOSkyp->getRainbowTex();
            LLViewerTexture* halo_tex = gSky.mVOSkyp->getHaloTex();

            sky_shader->bindTexture(LLShaderMgr::RAINBOW_MAP, rainbow_tex);
            sky_shader->bindTexture(LLShaderMgr::HALO_MAP, halo_tex);

            F32 moisture_level = (float)psky->getSkyMoistureLevel();
            F32 droplet_radius = (float)psky->getSkyDropletRadius();
            F32 ice_level = (float)psky->getSkyIceLevel();

            if (!psky->getIsSunUp() && !psky->getIsMoonUp())
            {
                moisture_level = 0.0f;
                ice_level = 0.0f;
            }

            sky_shader->uniform1f(LLShaderMgr::MOISTURE_LEVEL, moisture_level);
            sky_shader->uniform1f(LLShaderMgr::DROPLET_RADIUS, droplet_radius);
            sky_shader->uniform1f(LLShaderMgr::ICE_LEVEL, ice_level);

            sky_shader->uniform1f(LLShaderMgr::SUN_MOON_GLOW_FACTOR, psky->getSunMoonGlowFactor());

            sky_shader->uniform1i(LLShaderMgr::SUN_UP_FACTOR, psky->getIsSunUp() ? 1 : 0);

            renderDome(origin, camHeightLocal, sky_shader);

            sky_shader->unbind();
        }
    }

    void renderStarsDeferred(const LLVector3& camPosLocal)
    {
        if (!gSky.mVOSkyp || use_hdri_sky())
        {
            return;
        }

        LLGLSPipelineBlendSkyBox gls_sky(true, false);
        gDX.setSceneBlendType(LLRender::BT_ADD_WITH_ALPHA);

        constexpr F32 STAR_BRIGHTNESS_SCALE = 500.0f;
        F32 star_alpha = LLEnvironment::instance().getCurrentSky()->getStarBrightness() / STAR_BRIGHTNESS_SCALE;

        if (LLPipeline::sReflectionRender)
        {
            star_alpha = 1.0f;
        }

        if (star_alpha < 0.001f)
        {
            LL_DEBUGS("SKY") << "star_brightness below threshold." << LL_ENDL;
            return;
        }

        gDeferredStarProgram.bind();

        LLViewerTexture* star_tex_current = gSky.mVOSkyp->getBloomTex();
        LLViewerTexture* star_tex_next = gSky.mVOSkyp->getBloomTexNext();

        F32 blend_factor = (F32)LLEnvironment::instance().getCurrentSky()->getBlendFactor();

        if (star_tex_current && (!star_tex_next || (star_tex_current == star_tex_next)))
        {
            gDX.getTexUnit(0)->bind(star_tex_current);
            gDX.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);
            blend_factor = 0.0f;
        }
        else if (star_tex_next && !star_tex_current)
        {
            gDX.getTexUnit(0)->bind(star_tex_next);
            gDX.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);
            blend_factor = 0.0f;
        }
        else if (star_tex_next != star_tex_current)
        {
            gDX.getTexUnit(0)->bind(star_tex_current);
            gDX.getTexUnit(1)->bind(star_tex_next);
        }

        gDX.pushMatrix();
        gDX.translatef(camPosLocal.mV[0], camPosLocal.mV[1], camPosLocal.mV[2]);

        gDX.rotatef(gFrameTimeSeconds * 0.01f, 0.f, 0.f, 1.f);

        S32 viewport_width_int = gGLViewport[2];
        S32 viewport_height_int = gGLViewport[3];
        F32 viewport_width = (F32)viewport_width_int;
        F32 viewport_height = (F32)viewport_height_int;

        F32 aspect_ratio = viewport_width / llmax(viewport_height, 1.0f);

        F32 aspect_scale = 1.0f;
        if (aspect_ratio > 1.0f)
        {
            aspect_scale = 1.0f / sqrtf(aspect_ratio);
        }

        gDX.scalef(aspect_scale, aspect_scale, aspect_scale);

        gDeferredStarProgram.uniform1f(LLShaderMgr::BLEND_FACTOR, blend_factor);
        gDeferredStarProgram.uniform1f(sCustomAlpha, star_alpha);

        sStarTime = (F32)LLFrameTimer::getElapsedSeconds() * 0.5f;
        gDeferredStarProgram.uniform1f(LLShaderMgr::WATER_TIME, sStarTime);

        gDeferredStarProgram.uniform1f(sStarGlow, gSavedSettings.getF32("RenderStarGlow"));
        gDeferredStarProgram.uniform1f(sStarDensity, gSavedSettings.getF32("RenderStarDensity"));
        gDeferredStarProgram.uniform1f(sStarDustIntensity, gSavedSettings.getF32("RenderStarDustIntensity"));
        gDeferredStarProgram.uniform1f(sNebulaEnabled, gSavedSettings.getBOOL("RenderNebulaEnabled") ? 1.0f : 0.0f);
        gDeferredStarProgram.uniform1f(sNebulaIntensity, gSavedSettings.getF32("RenderNebulaIntensity"));
        gDeferredStarProgram.uniform1f(sSkyStyle, (F32)gSavedSettings.getS32("RenderSkyStyle"));

        gSky.mVOWLSkyp->drawStars();

        gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
        gDX.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);

        gDeferredStarProgram.unbind();
        gDX.popMatrix();
    }

    void renderShootingStarsDeferred(const LLVector3& camPosLocal)
    {
        // S24 (task #279 stage 2, "RENDER WOW"): small dedicated program +
        // dynamic buffer - see LLVOWLSky::drawShootingStars()/
        // updateShootingStarGeometry(). Gated the same way as the main star
        // field (skip during HDRI sky / no VOSky).
        if (!gSky.mVOSkyp || use_hdri_sky())
        {
            return;
        }

        // S24 (task #279 stage 2 follow-up): spawn/age/expire is driven
        // from here, NOT LLVOWLSky::idleUpdate() - that override is dead
        // code (LLVOWLSky::isActive() hardcodes false, so the engine's
        // active-object idle dispatch never calls it; see idleUpdate()'s
        // own comment). This render call happens every frame regardless,
        // matching how sStarTime etc. are already computed fresh here
        // rather than via idle ticks. updateShootingStars() itself checks
        // RenderShootingStars/RenderShootingStarFrequency internally.
        //
        // S24 (2026-09-04): kept unconditional (even though the draw below
        // is now gated on daylight, see star_alpha) so the spawn timer/pool
        // keeps ticking normally through daylight hours rather than
        // accumulating one huge dt and bursting streaks the moment night
        // falls again.
        static LLFrameTimer shooting_star_timer;
        F32 dt = shooting_star_timer.getElapsedTimeF32();
        shooting_star_timer.reset();
        gSky.mVOWLSkyp->updateShootingStars(dt);

        // S24 (2026-09-04): user report - shooting stars (and nebula, fixed
        // alongside in starsF.hlsl) stayed fully visible in broad daylight
        // instead of fading the same way the point-star field does. Mirrors
        // renderStarsDeferred()'s own star_alpha computation above in this
        // file - same reasoning, same early-out threshold, but only skips
        // the draw (see comment above on why the update stays unconditional).
        constexpr F32 STAR_BRIGHTNESS_SCALE = 500.0f;
        F32 star_alpha = LLEnvironment::instance().getCurrentSky()->getStarBrightness() / STAR_BRIGHTNESS_SCALE;

        if (LLPipeline::sReflectionRender)
        {
            star_alpha = 1.0f;
        }

        if (star_alpha < 0.001f)
        {
            return;
        }

        LLGLSPipelineBlendSkyBox gls_sky(true, false);
        gDX.setSceneBlendType(LLRender::BT_ADD_WITH_ALPHA);

        gDeferredStarShootingProgram.bind();

        gDX.pushMatrix();
        gDX.translatef(camPosLocal.mV[0], camPosLocal.mV[1], camPosLocal.mV[2]);

        gDeferredStarShootingProgram.uniform1f(sCustomAlpha, star_alpha);

        gSky.mVOWLSkyp->drawShootingStars();

        gDeferredStarShootingProgram.unbind();
        gDX.popMatrix();
    }

    void renderSkyCloudsDeferred(const LLVector3& camPosLocal, F32 camHeightLocal, LLHLSLShader* cloudshader)
    {
        // S24 (task #149, resolved): renderDome() (the actual geometry
        // draw) is shared between sky haze and clouds - only the bound
        // shader/texture differs.
        if (use_hdri_sky())
        {
            return;
        }

        if (gPipeline.canUseWindLightShaders() && gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_CLOUDS) && gSky.mVOSkyp && gSky.mVOSkyp->getCloudNoiseTex())
        {
            LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();

            LLGLSPipelineBlendSkyBox pipeline_state(true, true);

            cloudshader->bind();

            LLPointer<LLViewerTexture> cloud_noise = gSky.mVOSkyp->getCloudNoiseTex();
            LLPointer<LLViewerTexture> cloud_noise_next = gSky.mVOSkyp->getCloudNoiseTexNext();

            gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
            gDX.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);

            F32 cloud_variance = psky ? (F32)psky->getCloudVariance() : 0.0f;
            F32 blend_factor = psky ? (F32)psky->getBlendFactor() : 0.0f;

            if (psky->getCloudScrollRate().isExactlyZero())
            {
                blend_factor = 0.f;
            }

            if (cloud_noise || cloud_noise_next)
            {
                if (cloud_noise && (!cloud_noise_next || (cloud_noise == cloud_noise_next)))
                {
                    cloudshader->bindTexture(LLShaderMgr::CLOUD_NOISE_MAP, cloud_noise, LLTexUnit::TT_TEXTURE);
                    blend_factor = 0;
                }
                else if (cloud_noise_next && !cloud_noise)
                {
                    cloudshader->bindTexture(LLShaderMgr::CLOUD_NOISE_MAP, cloud_noise_next, LLTexUnit::TT_TEXTURE);
                    blend_factor = 0;
                }
                else if (cloud_noise_next != cloud_noise)
                {
                    cloudshader->bindTexture(LLShaderMgr::CLOUD_NOISE_MAP, cloud_noise, LLTexUnit::TT_TEXTURE);
                    cloudshader->bindTexture(LLShaderMgr::CLOUD_NOISE_MAP_NEXT, cloud_noise_next, LLTexUnit::TT_TEXTURE);
                }
            }

            cloudshader->uniform1f(LLShaderMgr::BLEND_FACTOR, blend_factor);
            cloudshader->uniform1f(LLShaderMgr::CLOUD_VARIANCE, cloud_variance);
            cloudshader->uniform1f(LLShaderMgr::SUN_MOON_GLOW_FACTOR, psky->getSunMoonGlowFactor());

            // S24 (task #303, "2.5D cloud layers", user: "take the math and
            // the EEP/Windlight settings and extrapolate... volumetric?" ->
            // agreed on a cheaper 2.5D approach instead of true raymarched
            // volumetrics). The dome/shader are shared with the sky haze
            // pass and drawn once today (renderDome() below, unchanged from
            // before this task - default/off behaviour is byte-identical).
            // Extra "layers" are just this same dome+shader redrawn with
            // overridden CLOUD_SCALE (bigger number = bigger-looking cells,
            // see cloudsV.hlsl's uv/=cloud_scale) and CLOUD_POS_DENSITY1's
            // xy (independently-scaled wind scroll, added on top of the
            // static EEP base position - NOT true camera-motion parallax,
            // the WL dome recenters on the camera every frame so there's no
            // real depth to parallax against; the depth cue here comes from
            // differential wind-scroll speed, cell size, and the new tint/
            // alpha uniforms below, not from geometry).
            cloudshader->uniform3f(sCloudLayerTint, 1.f, 1.f, 1.f);
            cloudshader->uniform1f(sCloudLayerAlphaMult, 1.f);
            renderDome(camPosLocal, camHeightLocal, cloudshader);

            if (gSavedSettings.getBOOL("RenderCloudLayers"))
            {
                F32 layer_opacity = gSavedSettings.getF32("RenderCloudLayerOpacity");
                LLColor3 base_pd1 = psky->getCloudPosDensity1();
                LLColor3 base_pd2 = psky->getCloudPosDensity2();
                F32 base_scale = psky->getCloudScale();

                LLVector2 scroll = LLEnvironment::instance().getCloudScrollDelta();
                scroll.mV[0] = -scroll.mV[0]; // match applySpecial()'s X-flip

                // S24 (task #303 follow-up, user: "can we have some
                // controls like cloudbase layers etc"): cell-size/scroll
                // ratios are now user-tunable (were hardcoded 0.55/1.7 and
                // 1.6/0.6). Height skew ("cloud base" stratification) is a
                // single 0..1 strength that maps to a taller/shorter dome
                // per layer via renderDome()'s layer_height_skew - see that
                // function's comment for why this is a skew trick, not true
                // altitude.
                F32 cirrus_scale_ratio = gSavedSettings.getF32("RenderCloudCirrusScale");
                F32 cumulus_scale_ratio = gSavedSettings.getF32("RenderCloudCumulusScale");
                F32 cirrus_scroll_mult = gSavedSettings.getF32("RenderCloudCirrusScrollMult");
                F32 cumulus_scroll_mult = gSavedSettings.getF32("RenderCloudCumulusScrollMult");
                F32 height_skew = gSavedSettings.getF32("RenderCloudLayerHeightSkew");
                F32 cirrus_skew = 1.0f + height_skew * 0.6f;
                F32 cumulus_skew = 1.0f - height_skew * 0.4f;

                // Layer 1: high wispy cirrus - smaller cells (finer noise
                // repeat), faster independent scroll, thin and cool-tinted.
                {
                    LLColor3 pd1(base_pd1.mV[0] + scroll.mV[0] * cirrus_scroll_mult,
                                 base_pd1.mV[1] + scroll.mV[1] * cirrus_scroll_mult,
                                 base_pd1.mV[2]);
                    LLColor3 pd2(base_pd2.mV[0], base_pd2.mV[1], base_pd2.mV[2] * 0.5f);

                    cloudshader->uniform1f(LLShaderMgr::CLOUD_SCALE, base_scale * cirrus_scale_ratio);
                    cloudshader->uniform3f(LLShaderMgr::CLOUD_POS_DENSITY1, pd1.mV[0], pd1.mV[1], pd1.mV[2]);
                    cloudshader->uniform3f(LLShaderMgr::CLOUD_POS_DENSITY2, pd2.mV[0], pd2.mV[1], pd2.mV[2]);
                    cloudshader->uniform3f(sCloudLayerTint, 0.97f, 0.98f, 1.05f);
                    cloudshader->uniform1f(sCloudLayerAlphaMult, layer_opacity * 0.4f);
                    renderDome(camPosLocal, camHeightLocal, cloudshader, cirrus_skew);
                }

                // Layer 2: big cumulus - bigger cells, slower independent
                // scroll, denser and slightly warm-tinted (closer/lower).
                {
                    LLColor3 pd1(base_pd1.mV[0] + scroll.mV[0] * cumulus_scroll_mult,
                                 base_pd1.mV[1] + scroll.mV[1] * cumulus_scroll_mult,
                                 base_pd1.mV[2]);
                    LLColor3 pd2(base_pd2.mV[0], base_pd2.mV[1], base_pd2.mV[2] * 1.15f);

                    cloudshader->uniform1f(LLShaderMgr::CLOUD_SCALE, base_scale * cumulus_scale_ratio);
                    cloudshader->uniform3f(LLShaderMgr::CLOUD_POS_DENSITY1, pd1.mV[0], pd1.mV[1], pd1.mV[2]);
                    cloudshader->uniform3f(LLShaderMgr::CLOUD_POS_DENSITY2, pd2.mV[0], pd2.mV[1], pd2.mV[2]);
                    cloudshader->uniform3f(sCloudLayerTint, 1.04f, 1.01f, 0.96f);
                    cloudshader->uniform1f(sCloudLayerAlphaMult, layer_opacity * 0.75f);
                    renderDome(camPosLocal, camHeightLocal, cloudshader, cumulus_skew);
                }
            }

            cloudshader->unbind();

            gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
            gDX.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);
        }
    }

    void renderHeavenlyBodies()
    {
        if (!gSky.mVOSkyp || use_hdri_sky()) return;

        LLGLSPipelineBlendSkyBox gls_skybox(true, true); // SL-14113 we need moon to write to depth to clip stars behind

        LLVector3 const& origin = LLViewerCamera::getInstance()->getOrigin();
        gDX.pushMatrix();
        gDX.translatef(origin.mV[0], origin.mV[1], origin.mV[2]);

        LLFace* face = gSky.mVOSkyp->mFace[LLVOSky::FACE_SUN];

        F32 blend_factor = (F32)LLEnvironment::instance().getCurrentSky()->getBlendFactor();
        bool can_use_vertex_shaders = gPipeline.shadersLoaded();
        bool can_use_windlight_shaders = gPipeline.canUseWindLightShaders();

        if (gSky.mVOSkyp->getSun().getDraw() && face && face->getGeomCount())
        {
            LLPointer<LLViewerTexture> tex_a = face->getTexture(LLRender::DIFFUSE_MAP);
            LLPointer<LLViewerTexture> tex_b = face->getTexture(LLRender::ALTERNATE_DIFFUSE_MAP);

            gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
            gDX.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);

            if (tex_a || tex_b)
            {
                if (can_use_vertex_shaders && can_use_windlight_shaders)
                {
                    sun_shader->bind();

                    if (tex_a && (!tex_b || (tex_a == tex_b)))
                    {
                        sun_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, tex_a, LLTexUnit::TT_TEXTURE);
                        blend_factor = 0;
                    }
                    else if (tex_b && !tex_a)
                    {
                        sun_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, tex_b, LLTexUnit::TT_TEXTURE);
                        blend_factor = 0;
                    }
                    else if (tex_b != tex_a)
                    {
                        sun_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, tex_a, LLTexUnit::TT_TEXTURE);
                        sun_shader->bindTexture(LLShaderMgr::ALTERNATE_DIFFUSE_MAP, tex_b, LLTexUnit::TT_TEXTURE);
                    }

                    LLColor4 color(gSky.mVOSkyp->getSun().getInterpColor());

                    sun_shader->uniform4fv(LLShaderMgr::DIFFUSE_COLOR, 1, color.mV);
                    sun_shader->uniform1f(LLShaderMgr::BLEND_FACTOR, blend_factor);

                    face->renderIndexed();

                    gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
                    gDX.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);

                    sun_shader->unbind();
                }
            }
        }

        face = gSky.mVOSkyp->mFace[LLVOSky::FACE_MOON];

        if (gSky.mVOSkyp->getMoon().getDraw() && face && face->getTexture(LLRender::DIFFUSE_MAP) && face->getGeomCount() && moon_shader)
        {
            LLViewerTexture* tex_a = face->getTexture(LLRender::DIFFUSE_MAP);
            LLViewerTexture* tex_b = face->getTexture(LLRender::ALTERNATE_DIFFUSE_MAP);

            LLColor4 color(gSky.mVOSkyp->getMoon().getInterpColor());

            if (can_use_vertex_shaders && can_use_windlight_shaders && (tex_a || tex_b))
            {
                moon_shader->bind();

                if (tex_a && (!tex_b || (tex_a == tex_b)))
                {
                    moon_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, tex_a, LLTexUnit::TT_TEXTURE);
                }
                else if (tex_b && !tex_a)
                {
                    moon_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, tex_b, LLTexUnit::TT_TEXTURE);
                }
                else if (tex_b != tex_a)
                {
                    moon_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, tex_a, LLTexUnit::TT_TEXTURE);
                }

                LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();

                F32 moon_brightness = (float)psky->getMoonBrightness();

                moon_shader->uniform1f(LLShaderMgr::MOON_BRIGHTNESS, moon_brightness);
                moon_shader->uniform3fv(LLShaderMgr::MOONLIGHT_COLOR, 1, gSky.mVOSkyp->getMoon().getColor().mV);
                moon_shader->uniform4fv(LLShaderMgr::DIFFUSE_COLOR, 1, color.mV);
                moon_shader->uniform3fv(LLShaderMgr::DEFERRED_MOON_DIR, 1, psky->getMoonDirection().mV);

                face->renderIndexed();

                gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
                gDX.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);

                moon_shader->unbind();
            }
        }

        gDX.popMatrix();
    }
}

// static
void DXDrawPoolWLSky::beginDeferredPass(LLDrawPoolWLSky& pool, S32 pass)
{
    (void)pool;
    (void)pass;

    sky_shader = &gDeferredWLSkyProgram;
    cloud_shader = &gDeferredWLCloudProgram;
    sun_shader = &gDeferredWLSunProgram;
    moon_shader = &gDeferredWLMoonProgram;
}

// static
void DXDrawPoolWLSky::endDeferredPass(LLDrawPoolWLSky& pool, S32 pass)
{
    (void)pool;
    (void)pass;

    sky_shader = nullptr;
    cloud_shader = nullptr;
    sun_shader = nullptr;
    moon_shader = nullptr;

    // Mirrors the GL source's glClear(GL_DEPTH_BUFFER_BIT) ("clear the
    // depth buffer so haze shaders can use unwritten depth as a mask") -
    // routed through the deferred G-buffer target's own clear() (already
    // DX-safe, see LLRenderTarget::clear()) instead of a raw GL call.
    gPipeline.mRT->deferredScreen.clear(GL_DEPTH_BUFFER_BIT);
}

// static
void DXDrawPoolWLSky::renderDeferred(LLDrawPoolWLSky& pool, S32 pass)
{
    (void)pool;
    (void)pass;

    LL_RECORD_BLOCK_TIME(FTM_RENDER_WL_SKY);
    if (!gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_SKY) || gSky.mVOSkyp.isNull())
    {
        return;
    }

    // TODO: remove gSky.mVOSkyp and fold sun/moon into LLVOWLSky
    gSky.mVOSkyp->updateGeometry(gSky.mVOSkyp->mDrawable);

    const F32 camHeightLocal = LLEnvironment::instance().getCamHeight();

    LLVector3 const& origin = LLViewerCamera::getInstance()->getOrigin();

    if (gPipeline.canUseWindLightShaders())
    {
        renderSkyHazeDeferred(origin, camHeightLocal);
        renderHeavenlyBodies();

        if (!gCubeSnapshot)
        {
            renderStarsDeferred(origin);
            renderShootingStarsDeferred(origin);
        }

        if (!gCubeSnapshot || gPipeline.mReflectionMapManager.isRadiancePass())
        {
            renderSkyCloudsDeferred(origin, camHeightLocal, cloud_shader);
        }
    }
}
