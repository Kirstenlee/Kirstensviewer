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
#include "llappviewer.h"

// static
void DXDrawPoolWater::beginPostDeferredPass(LLDrawPoolWater& pool, S32 pass)
{
    (void)pass;
    gDX.setColorWriteMask(true, true);

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

// Real handle defined in lldrawpoolwater.cpp - shared, not a separate instance
// (LLTrace::BlockTimerStatHandle registers itself in a global name-keyed
// registry; two independently-constructed handles with the same display
// name collide and crash during static initialization).
extern LLTrace::BlockTimerStatHandle FTM_RENDER_WATER_OPAQUE;

// static
void DXDrawPoolWater::renderPostDeferred(LLDrawPoolWater& pool, S32 pass)
{
    (void)pass;
    LL_RECORD_BLOCK_TIME(FTM_RENDER_WATER_OPAQUE);

    LLGLDisable blend(GL_BLEND);

    gDX.setColorWriteMask(true, true);

    LLColor3 light_diffuse(0, 0, 0);

    LLEnvironment& environment = LLEnvironment::instance();
    LLSettingsWater::ptr_t pwater = environment.getCurrentWater();
    LLSettingsSky::ptr_t   psky   = environment.getCurrentSky();
    LLVector3              light_dir       = environment.getLightDirection();
    bool                   sun_up          = environment.getIsSunUp();
    bool                   moon_up         = environment.getIsMoonUp();
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

    static LLCachedControl<F32> water_wave_speed(gSavedSettings, "RenderWaterWaveSpeed", 1.0f);
    static LLCachedControl<F32> water_wind_influence(gSavedSettings, "RenderWaterWindInfluence", 0.0f);
    static LLCachedControl<F32> water_wind_magnitude_cap(gSavedSettings, "RenderWaterWindMagnitudeCap", 15.0f);

    // S24: live wind coupling - gWindVec (llappviewer.cpp, resampled every frame at the agent's
    // position from the region's LLWind grid, see llwind.h/.cpp) blends into both wave scroll
    // direction (below, at the WATER_WAVE_DIR1/2 upload) and animation speed here, gated by
    // RenderWaterWindInfluence so EEP-authored water presets stay fully in control by default -
    // 0.0 (the default) ignores wind entirely, matching pre-existing behavior exactly.
    F32 wind_influence = llclamp((F32)water_wind_influence, 0.0f, 1.0f);
    F32 wind_speed_scale = 1.0f;
    LLVector2 wind_dir(0.f, 0.f);
    F32 wind_speed = 0.f;
    if (wind_influence > 0.0f)
    {
        // S24: raw gWindVec is spatially AND temporally noisy - sampled at the agent's current
        // position from a 16x16 per-region grid (llwind.cpp) that changes in discrete jumps
        // whenever a new wind patch arrives from the simulator (no interpolation between old/new
        // values), and re-sampled at a different grid cell every frame the avatar moves. Fed
        // straight into wave direction this made the water visibly jerk/snap on every raw sample
        // change - low-pass filtering it with a simple frame-rate-independent exponential smooth
        // turns that into a gradual drift (like a real gust building or a lull settling) instead.
        // S24: a single exponential low-pass filter (one pole) has its FASTEST rate of change the
        // instant a target changes and eases off only as it nears the new value - the opposite of
        // "smooth acceleration" (it snaps straight into fast motion, then coasts), which read as
        // "zero to 100 too readily" once the outright jerkiness above was fixed. Cascading the same
        // filter twice in series (two poles) gives the classic S-curve response instead - it starts
        // at zero rate of change, eases into motion, then eases out approaching the target - with no
        // overshoot risk, unlike a spring/damper system would have if mistuned.
        static LLVector2 s_windStage1(0.f, 0.f);
        static LLVector2 s_windStage2(0.f, 0.f);
        static F32 s_lastWindSampleTime = -1.f;

        F32 now = (F32)LLFrameTimer::getElapsedSeconds();
        F32 dt  = (s_lastWindSampleTime < 0.f) ? 0.f : llclamp(now - s_lastWindSampleTime, 0.0f, 0.5f);
        s_lastWindSampleTime = now;

        static const F32 kWindSmoothingTau = 6.0f; // seconds - how long a gust/lull takes to settle in
        F32 alpha = (dt <= 0.f) ? 0.0f : (1.0f - expf(-dt / kWindSmoothingTau));

        // S24: hard cap on the raw wind vector's own magnitude, applied before it ever reaches the
        // smoothing filter - limits how strong a real storm/gust can push the water regardless of
        // how extreme the simulator's actual wind data gets. 0.0 caps to zero, i.e. wind is always
        // treated as calm (a real, if extreme, tuning point - not the same as RenderWaterWindInfluence
        // itself being 0, which disables wind coupling entirely).
        LLVector2 raw_wind(gWindVec.mV[VX], gWindVec.mV[VY]);
        F32 magnitude_cap = llmax(0.0f, (F32)water_wind_magnitude_cap);
        F32 raw_speed = raw_wind.length();
        if (raw_speed > magnitude_cap && raw_speed > 0.0001f)
        {
            raw_wind = raw_wind * (magnitude_cap / raw_speed);
        }
        s_windStage1 = s_windStage1 + (raw_wind    - s_windStage1) * alpha;
        s_windStage2 = s_windStage2 + (s_windStage1 - s_windStage2) * alpha;

        wind_speed = s_windStage2.length();
        if (wind_speed > 0.01f)
        {
            wind_dir = s_windStage2 * (1.0f / wind_speed);
            // gWindVec's magnitude is an SL-relative measure (WIND_SCALE_HACK-scaled in llwind.cpp),
            // not real-world m/s - map its typical range to a 0.6x (calm) - 2.0x (stormy) multiplier
            // on top of the user's own RenderWaterWaveSpeed base, scaled by wind_influence so this
            // stays a blend rather than a hard override.
            F32 target_scale = llclamp(0.6f + wind_speed * 0.12f, 0.6f, 2.0f);
            wind_speed_scale = 1.0f + (target_scale - 1.0f) * wind_influence;
        }
    }

    F32           phase_time = (F32) LLFrameTimer::getElapsedSeconds() * 0.5f * llmax(0.0f, (F32)water_wave_speed) * wind_speed_scale;
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

    // mWaterNormp is protected on LLDrawPoolWater; accessible here via a
    // friend declaration in lldrawpoolwater.h.
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

    // Both go through the LLHLSLShader::bindTexture(S32, LLRenderTarget*, ...)
    // overload; see llhlslshader.cpp.
    shader->bindTexture(LLShaderMgr::WATER_EXCLUSIONTEX, &gPipeline.mWaterExclusionMask);

    shader->uniform1f(LLShaderMgr::BLEND_FACTOR, blend_factor);

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

    LLVector2 wave1_dir = pwater->getWave1Dir();
    LLVector2 wave2_dir = pwater->getWave2Dir();
    if (wind_influence > 0.0f && wind_speed > 0.01f)
    {
        // Second bump layer offset ~30 degrees off the first so the two wave layers don't scroll
        // in lockstep under strong wind influence - matches the EEP-authored presets' own
        // convention of two non-parallel wave directions (see waterV.hlsl's own waveDir3
        // comment for the same anti-lockstep concern on the third, derived layer).
        LLVector2 wind_dir2(wind_dir.mV[VX] * 0.866f - wind_dir.mV[VY] * 0.5f,
                             wind_dir.mV[VX] * 0.5f  + wind_dir.mV[VY] * 0.866f);
        wave1_dir = wave1_dir + (wind_dir  - wave1_dir) * wind_influence;
        wave2_dir = wave2_dir + (wind_dir2 - wave2_dir) * wind_influence;
    }
    shader->uniform2fv(LLShaderMgr::WATER_WAVE_DIR1, 1, wave1_dir.mV);
    shader->uniform2fv(LLShaderMgr::WATER_WAVE_DIR2, 1, wave2_dir.mV);

    shader->uniform3fv(LLShaderMgr::WATER_LIGHT_DIR, 1, light_dir.mV);

    shader->uniform3fv(LLShaderMgr::WATER_NORM_SCALE, 1, pwater->getNormalScale().mV);
    shader->uniform1f(LLShaderMgr::WATER_FRESNEL_SCALE, pwater->getFresnelScale());
    shader->uniform1f(LLShaderMgr::WATER_FRESNEL_OFFSET, pwater->getFresnelOffset());
    shader->uniform1f(LLShaderMgr::WATER_BLUR_MULTIPLIER, fmaxf(0, pwater->getBlurMultiplier()) * 2);

    static LLCachedControl<F32> water_metallic(gSavedSettings, "RenderWaterMetallic", 1.0f);
    static LLCachedControl<F32> water_roughness_override(gSavedSettings, "RenderWaterRoughnessOverride", 0.0f);
    static LLCachedControl<F32> water_specular_intensity(gSavedSettings, "RenderWaterSpecularIntensity", 1.0f);
    static LLCachedControl<F32> water_reflection_intensity(gSavedSettings, "RenderWaterReflectionIntensity", 1.0f);

    shader->uniform1f(LLShaderMgr::WATER_METALLIC, llclamp((F32)water_metallic, 0.35f, 1.0f));
    shader->uniform1f(LLShaderMgr::WATER_ROUGHNESS_OVERRIDE, llclamp((F32)water_roughness_override, 0.0f, 0.60f));
    shader->uniform1f(LLShaderMgr::WATER_SPECULAR_INTENSITY, llmax(0.0f, (F32)water_specular_intensity));
    shader->uniform1f(LLShaderMgr::WATER_REFLECTION_INTENSITY, llclamp((F32)water_reflection_intensity, 0.0f, 3.0f));

    static LLCachedControl<F32> water_color_tint_r(gSavedSettings, "RenderWaterColorTintR", 1.0f);
    static LLCachedControl<F32> water_color_tint_g(gSavedSettings, "RenderWaterColorTintG", 1.0f);
    static LLCachedControl<F32> water_color_tint_b(gSavedSettings, "RenderWaterColorTintB", 1.0f);
    static LLCachedControl<F32> water_color_tint_a(gSavedSettings, "RenderWaterColorTintA", 1.0f);
    static LLCachedControl<F32> water_fresnel_power(gSavedSettings, "RenderWaterFresnelPower", 2.0f);
    static LLCachedControl<F32> water_shore_fade_distance(gSavedSettings, "RenderWaterShoreFadeDistance", 60.0f);
    static LLCachedControl<F32> water_reflection_warmth(gSavedSettings, "RenderWaterReflectionWarmth", 1.0f);
    static LLCachedControl<F32> water_color_absorption_rate(gSavedSettings, "RenderWaterColorAbsorptionRate", 0.15f);

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
    shader->uniform1f(LLShaderMgr::WATER_COLOR_ABSORPTION_RATE, llclamp((F32)water_color_absorption_rate, 0.01f, 1.0f));

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

    // NOTE: the model matrix must be reset before this pool runs - see
    // DXPipeline::renderGeomPostDeferred() - or water inherits whatever
    // matrix the last-drawn alpha object left behind.

    // Only push the water planes once - see lldrawpoolwater.cpp's comment
    // (Geenz 2025-02-11) for why there's no separate void-water pass.
    pool.pushWaterPlanes(0);

    // clean up
    gPipeline.unbindDeferredShader(*shader);

    gDX.setColorWriteMask(true, false);
}
