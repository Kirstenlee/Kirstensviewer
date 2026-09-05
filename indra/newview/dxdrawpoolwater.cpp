/**
 * @file dxdrawpoolwater.cpp
 * @brief Fresh DX11-native implementation of LLDrawPoolWater's post-deferred
 * water surface render path.
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

#include "dxdrawpoolwater.h"

#include "lldrawpoolwater.h"
#include "llrender.h"
#include "pipeline.h"
#include "llviewershadermgr.h"
#include "llviewercamera.h"
#include "llviewercontrol.h"
#include "llenvironment.h"
#include "llsettingssky.h"
#include "llsettingswater.h"

// static
void DXDrawPoolWater::beginPostDeferredPass(LLDrawPoolWater& pool, S32 pass)
{
    (void)pass;
    LL_PROFILE_GPU_ZONE("water beginPostDeferredPass");
    gDX.setColorMask(true, true);

    if (LLPipeline::sRenderTransparentWater)
    {
        // copy framebuffer contents so far to a texture to be used for
        // reflections and refractions
        LLGLDepthTest depth(GL_TRUE, GL_TRUE, GL_ALWAYS);

        LLRenderTarget& src = gPipeline.mRT->screen;
        LLRenderTarget& depth_src = gPipeline.mRT->deferredScreen;
        LLRenderTarget& dst = gPipeline.mWaterDis;

        dst.bindTarget();
        gCopyDepthProgram.bind();

        S32 diff_map = gCopyDepthProgram.getTextureChannel(LLShaderMgr::DIFFUSE_MAP);
        S32 depth_map = gCopyDepthProgram.getTextureChannel(LLShaderMgr::DEFERRED_DEPTH);

        gDX.getTexUnit(diff_map)->bind(&src);
        gDX.getTexUnit(depth_map)->bind(&depth_src, true);

        gPipeline.mScreenTriangleVB->setBuffer();
        gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

        dst.flush();
    }
}

// static
void DXDrawPoolWater::renderPostDeferred(LLDrawPoolWater& pool, S32 pass)
{
    (void)pass;

    // S24 (2026-08-09, task #148): draw-list diagnostic removed - it did
    // its job (confirmed face composition/Z values are normal/expected,
    // ruling out culling as the cause). Real root cause found in
    // DXPipeline::renderGeomPostDeferred() (dxpipeline.cpp) - see there.

    LLGLDisable blend(GL_BLEND);

    gDX.setColorMask(true, true);

    LLColor3 light_diffuse(0, 0, 0);

    LLEnvironment& environment = LLEnvironment::instance();
    LLSettingsWater::ptr_t pwater = environment.getCurrentWater();
    LLSettingsSky::ptr_t   psky   = environment.getCurrentSky();
    LLVector3              light_dir       = environment.getLightDirection();
    bool                   sun_up          = environment.getIsSunUp();
    bool                   moon_up         = environment.getIsMoonUp();
    // S24 (2026-08-23, pre-alpha perf sweep): raw per-frame gSavedSettings
    // lookup - same fix already applied to water_wave_speed just below.
    static LLCachedControl<bool> render_water_mip_normal(gSavedSettings, "RenderWaterMipNormal", true);
    bool                   has_normal_mips = render_water_mip_normal;
    bool                   underwater      = LLViewerCamera::getInstance()->cameraUnderWater();
    LLColor4               fog_color       = LLColor4(pwater->getWaterFogColor(), 0.f);

    if (sun_up)
    {
        light_diffuse += psky->getSunlightColor();
    }
    // moonlight is several orders of magnitude less bright than sunlight,
    // so only use this color when the moon alone is showing
    else if (moon_up)
    {
        light_diffuse += psky->getMoonlightColor();
    }

    // Apply magic numbers translating light direction into intensities
    light_dir.normalize();
    F32 ground_proj_sq = light_dir.mV[0] * light_dir.mV[0] + light_dir.mV[1] * light_dir.mV[1];
    if (0.f < light_diffuse.normalize())  // Normalizing a color? Puzzling...
    {
        light_diffuse *= (1.5f + (6.f * ground_proj_sq));
    }

    LLTexUnit::eTextureFilterOptions filter_mode = has_normal_mips ? LLTexUnit::TFO_ANISOTROPIC : LLTexUnit::TFO_POINT;

    // S24 Advanced - Apply wave animation speed multiplier
    static LLCachedControl<F32> water_wave_speed(gSavedSettings, "RenderWaterWaveSpeed", 1.0f);
    F32           phase_time = (F32) LLFrameTimer::getElapsedSeconds() * 0.5f * llmax(0.0f, (F32)water_wave_speed);
    LLHLSLShader *shader     = nullptr;

    // select shader - see lldrawpoolwater.cpp's comment (Geenz 2025-02-11):
    // one pass now, void/region water share the same shader.
    if (underwater)
    {
        shader = &gUnderWaterProgram;
    }
    else
    {
        shader = &gWaterProgram;
    }

    gPipeline.bindDeferredShader(*shader, nullptr, &gPipeline.mWaterDis);

    // S24 (2026-08-04): mWaterNormp is protected on LLDrawPoolWater -
    // accessible here via the friend declaration added alongside this
    // conversion (see lldrawpoolwater.h).
    LLViewerTexture* tex_a = pool.mWaterNormp[0];
    LLViewerTexture* tex_b = pool.mWaterNormp[1];

    F32 blend_factor = (F32)pwater->getBlendFactor();

    if (tex_a && (!tex_b || (tex_a == tex_b)))
    {
        shader->bindTexture(LLViewerShaderMgr::BUMP_MAP, tex_a);
        tex_a->setFilteringOption(filter_mode);
        blend_factor = 0; // only one tex provided, no blending
    }
    else if (tex_b && !tex_a)
    {
        shader->bindTexture(LLViewerShaderMgr::BUMP_MAP, tex_b);
        tex_b->setFilteringOption(filter_mode);
        blend_factor = 0; // only one tex provided, no blending
    }
    else if (tex_b != tex_a)
    {
        shader->bindTexture(LLViewerShaderMgr::BUMP_MAP, tex_a);
        tex_a->setFilteringOption(filter_mode);
        shader->bindTexture(LLViewerShaderMgr::BUMP_MAP2, tex_b);
        tex_b->setFilteringOption(filter_mode);
    }

    // S24 (2026-08-04): both of these go through LLHLSLShader::bindTexture
    // (S32, LLRenderTarget*, ...) - was a hardcoded DX_RENDER no-op before
    // this conversion, fixed in llhlslshader.cpp (see dxdrawpoolwater.h's
    // comment).
    shader->bindTexture(LLShaderMgr::WATER_EXCLUSIONTEX, &gPipeline.mWaterExclusionMask);

    shader->uniform1f(LLShaderMgr::BLEND_FACTOR, blend_factor);

    // S24 Advanced - Apply underwater fog multiplier for independent underwater visibility control
    static LLCachedControl<F32> water_underwater_fog_mult_setting(gSavedSettings, "RenderWaterUnderwaterFogMult", 1.0f);
    F32 fog_mult = underwater ? llclamp((F32)water_underwater_fog_mult_setting, 0.1f, 5.0f) : 1.0f;
    F32 fog_density = pwater->getModifiedWaterFogDensity(underwater) * fog_mult;

    shader->bindTexture(LLShaderMgr::WATER_SCREENTEX, &gPipeline.mWaterDis);

    if (pool.mShaderLevel == 1)
    {
        fog_color.mV[VALPHA] = (F32)(log(fog_density) / log(2));
    }

    F32 water_height = environment.getWaterHeight();
    F32 camera_height = LLViewerCamera::getInstance()->getOrigin().mV[2];
    shader->uniform1f(LLShaderMgr::WATER_WATERHEIGHT, camera_height - water_height);
    shader->uniform1f(LLShaderMgr::WATER_TIME, phase_time);
    shader->uniform3fv(LLShaderMgr::WATER_EYEVEC, 1, LLViewerCamera::getInstance()->getOrigin().mV);

    shader->uniform3fv(LLShaderMgr::WATER_SPECULAR, 1, light_diffuse.mV);

    shader->uniform2fv(LLShaderMgr::WATER_WAVE_DIR1, 1, pwater->getWave1Dir().mV);
    shader->uniform2fv(LLShaderMgr::WATER_WAVE_DIR2, 1, pwater->getWave2Dir().mV);

    shader->uniform3fv(LLShaderMgr::WATER_LIGHT_DIR, 1, light_dir.mV);

    shader->uniform3fv(LLShaderMgr::WATER_NORM_SCALE, 1, pwater->getNormalScale().mV);
    shader->uniform1f(LLShaderMgr::WATER_FRESNEL_SCALE, pwater->getFresnelScale());
    shader->uniform1f(LLShaderMgr::WATER_FRESNEL_OFFSET, pwater->getFresnelOffset());
    shader->uniform1f(LLShaderMgr::WATER_BLUR_MULTIPLIER, fmaxf(0, pwater->getBlurMultiplier()) * 2);

    // S24 - Advanced water material controls (unlock hardcoded values)
    static LLCachedControl<F32> water_metallic(gSavedSettings, "RenderWaterMetallic", 1.0f);
    static LLCachedControl<F32> water_roughness_override(gSavedSettings, "RenderWaterRoughnessOverride", 0.0f);
    static LLCachedControl<F32> water_specular_intensity(gSavedSettings, "RenderWaterSpecularIntensity", 1.0f);
    static LLCachedControl<F32> water_reflection_intensity(gSavedSettings, "RenderWaterReflectionIntensity", 1.0f);

    shader->uniform1f(LLShaderMgr::WATER_METALLIC, llclamp((F32)water_metallic, 0.35f, 1.0f));
    shader->uniform1f(LLShaderMgr::WATER_ROUGHNESS_OVERRIDE, llclamp((F32)water_roughness_override, 0.0f, 0.60f));
    shader->uniform1f(LLShaderMgr::WATER_SPECULAR_INTENSITY, llmax(0.0f, (F32)water_specular_intensity));
    shader->uniform1f(LLShaderMgr::WATER_REFLECTION_INTENSITY, llclamp((F32)water_reflection_intensity, 0.0f, 3.0f));

    // S24 Advanced - Phase 1 & 2 artistic controls
    static LLCachedControl<F32> water_color_tint_r(gSavedSettings, "RenderWaterColorTintR", 1.0f);
    static LLCachedControl<F32> water_color_tint_g(gSavedSettings, "RenderWaterColorTintG", 1.0f);
    static LLCachedControl<F32> water_color_tint_b(gSavedSettings, "RenderWaterColorTintB", 1.0f);
    static LLCachedControl<F32> water_color_tint_a(gSavedSettings, "RenderWaterColorTintA", 1.0f);
    static LLCachedControl<F32> water_fresnel_power(gSavedSettings, "RenderWaterFresnelPower", 2.0f);
    static LLCachedControl<F32> water_shore_fade_distance(gSavedSettings, "RenderWaterShoreFadeDistance", 60.0f);
    static LLCachedControl<F32> water_reflection_warmth(gSavedSettings, "RenderWaterReflectionWarmth", 1.0f);

    LLVector3 water_color_tint(
        llclamp((F32)water_color_tint_r, 0.0f, 2.0f),
        llclamp((F32)water_color_tint_g, 0.0f, 2.0f),
        llclamp((F32)water_color_tint_b, 0.0f, 2.0f)
    );

    shader->uniform3fv(LLShaderMgr::WATER_COLOR_TINT, 1, water_color_tint.mV);
    shader->uniform1f(LLShaderMgr::WATER_COLOR_TINT_ALPHA, llclamp((F32)water_color_tint_a, 0.0f, 1.0f));
    shader->uniform1f(LLShaderMgr::WATER_FRESNEL_POWER, llclamp((F32)water_fresnel_power, 0.5f, 4.0f));
    shader->uniform1f(LLShaderMgr::WATER_SHORE_FADE_DISTANCE, llmax(1.0f, (F32)water_shore_fade_distance));
    shader->uniform1f(LLShaderMgr::WATER_REFLECTION_WARMTH, llclamp((F32)water_reflection_warmth, 0.5f, 2.0f));

    static LLStaticHashedString s_exposure("exposure");
    static LLStaticHashedString tonemap_mix("tonemap_mix");
    static LLStaticHashedString tonemap_type("tonemap_type");

    static LLCachedControl<F32> exposure(gSavedSettings, "RenderExposure", 1.f);

    F32 e = llclamp(exposure(), 0.5f, 4.f);

    static LLCachedControl<bool> should_auto_adjust(gSavedSettings, "RenderSkyAutoAdjustLegacy", false);

    shader->uniform1f(s_exposure, e);
    static LLCachedControl<U32> tonemap_type_setting(gSavedSettings, "RenderTonemapType", 0U);
    shader->uniform1i(tonemap_type, tonemap_type_setting);
    shader->uniform1f(tonemap_mix, psky->getTonemapMix(should_auto_adjust()));

    shader->uniform1i(LLShaderMgr::SUN_UP_FACTOR, sun_up ? 1 : 0);

    // SL-15861 This was changed from getRotatedLightNorm() as it was causing
    // lightnorm in shaders\class1\windlight\atmosphericsFuncs.glsl in have inconsistent additive lighting for 180 degrees of the FOV.
    LLVector4 rotated_light_direction = LLEnvironment::instance().getClampedLightNorm();
    shader->uniform3fv(LLViewerShaderMgr::LIGHTNORM, 1, rotated_light_direction.mV);

    shader->uniform3fv(LLShaderMgr::WL_CAMPOSLOCAL, 1, LLViewerCamera::getInstance()->getOrigin().mV);

    if (LLViewerCamera::getInstance()->cameraUnderWater())
    {
        shader->uniform1f(LLShaderMgr::WATER_REFSCALE, pwater->getScaleBelow());
    }
    else
    {
        shader->uniform1f(LLShaderMgr::WATER_REFSCALE, pwater->getScaleAbove());
    }

    LLGLDisable cullface(GL_CULL_FACE);

    // S24 (2026-08-09, task #148): depth-test-state probe removed - it did
    // its job (confirmed DepthEnable=true/LEQUAL/real DSV bound on every
    // sampled frame, ruling out a state gap). Real root cause found in
    // DXPipeline::renderGeomPostDeferred() (dxpipeline.cpp): the model
    // matrix was never reset between pools, so water inherited whatever
    // matrix the last-drawn alpha object left behind - see that function's
    // comment.

    // Only push the water planes once - see lldrawpoolwater.cpp's comment
    // (Geenz 2025-02-11) for why there's no separate void-water pass.
    pool.pushWaterPlanes(0);

    // clean up
    gPipeline.unbindDeferredShader(*shader);

    gDX.setColorMask(true, false);
}
