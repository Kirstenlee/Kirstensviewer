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
#include "llglslshader.h"
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

    LLGLSLShader* cloud_shader = nullptr;
    LLGLSLShader* sky_shader   = nullptr;
    LLGLSLShader* sun_shader   = nullptr;
    LLGLSLShader* moon_shader  = nullptr;

    float sStarTime = 0.f;

    bool use_hdri_sky()
    {
        static LLCachedControl<F32> hdri_split(gSavedSettings, "RenderHDRISplitScreen", 1.f);
        static LLCachedControl<bool> irradiance_only(gSavedSettings, "RenderHDRIIrradianceOnly", false);

        return gCubeSnapshot && (!irradiance_only || !gPipeline.mReflectionMapManager.isRadiancePass()) ? gEXRImage.notNull() :
            gEXRImage.notNull() ? hdri_split > 0.f :
            false;
    }

    void renderDome(const LLVector3& camPosLocal, F32 camHeightLocal, LLGLSLShader* shader)
    {
        llassert_always(nullptr != shader);

        gGL.matrixMode(LLRender::MM_MODELVIEW);
        gGL.pushMatrix();

        if (LLPipeline::sReflectionRender && camPosLocal.mV[2] > 256.f)
        {
            gGL.translatef(camPosLocal.mV[0], camPosLocal.mV[1], 256.f - camPosLocal.mV[2] * 0.5f);
        }
        else
        {
            gGL.translatef(camPosLocal.mV[0], camPosLocal.mV[1], camPosLocal.mV[2]);
        }

        // the windlight sky dome works most conveniently in a coordinate
        // system where Y is up, so permute our basis vectors accordingly.
        gGL.rotatef(120.f, 1.f / F_SQRT3, 1.f / F_SQRT3, 1.f / F_SQRT3);

        gGL.scalef(0.333f, 0.333f, 0.333f);

        gGL.translatef(0.f, -camHeightLocal, 0.f);

        shader->uniform3f(sCamPosLocal, 0.f, camHeightLocal, 0.f);

        gSky.mVOWLSkyp->drawDome();

        gGL.matrixMode(LLRender::MM_MODELVIEW);
        gGL.popMatrix();
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
                    gGL.getTexUnit(idx)->bind(gEXRImage);
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
        gGL.setSceneBlendType(LLRender::BT_ADD_WITH_ALPHA);

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
            gGL.getTexUnit(0)->bind(star_tex_current);
            gGL.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);
            blend_factor = 0.0f;
        }
        else if (star_tex_next && !star_tex_current)
        {
            gGL.getTexUnit(0)->bind(star_tex_next);
            gGL.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);
            blend_factor = 0.0f;
        }
        else if (star_tex_next != star_tex_current)
        {
            gGL.getTexUnit(0)->bind(star_tex_current);
            gGL.getTexUnit(1)->bind(star_tex_next);
        }

        gGL.pushMatrix();
        gGL.translatef(camPosLocal.mV[0], camPosLocal.mV[1], camPosLocal.mV[2]);

        gGL.rotatef(gFrameTimeSeconds * 0.01f, 0.f, 0.f, 1.f);

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

        gGL.scalef(aspect_scale, aspect_scale, aspect_scale);

        gDeferredStarProgram.uniform1f(LLShaderMgr::BLEND_FACTOR, blend_factor);
        gDeferredStarProgram.uniform1f(sCustomAlpha, star_alpha);

        sStarTime = (F32)LLFrameTimer::getElapsedSeconds() * 0.5f;
        gDeferredStarProgram.uniform1f(LLShaderMgr::WATER_TIME, sStarTime);

        gSky.mVOWLSkyp->drawStars();

        gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
        gGL.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);

        gDeferredStarProgram.unbind();
        gGL.popMatrix();
    }

    void renderSkyCloudsDeferred(const LLVector3& camPosLocal, F32 camHeightLocal, LLGLSLShader* cloudshader)
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

            gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
            gGL.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);

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

            renderDome(camPosLocal, camHeightLocal, cloudshader);

            cloudshader->unbind();

            gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
            gGL.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);
        }
    }

    void renderHeavenlyBodies()
    {
        if (!gSky.mVOSkyp || use_hdri_sky()) return;

        LLGLSPipelineBlendSkyBox gls_skybox(true, true); // SL-14113 we need moon to write to depth to clip stars behind

        LLVector3 const& origin = LLViewerCamera::getInstance()->getOrigin();
        gGL.pushMatrix();
        gGL.translatef(origin.mV[0], origin.mV[1], origin.mV[2]);

        LLFace* face = gSky.mVOSkyp->mFace[LLVOSky::FACE_SUN];

        F32 blend_factor = (F32)LLEnvironment::instance().getCurrentSky()->getBlendFactor();
        bool can_use_vertex_shaders = gPipeline.shadersLoaded();
        bool can_use_windlight_shaders = gPipeline.canUseWindLightShaders();

        if (gSky.mVOSkyp->getSun().getDraw() && face && face->getGeomCount())
        {
            LLPointer<LLViewerTexture> tex_a = face->getTexture(LLRender::DIFFUSE_MAP);
            LLPointer<LLViewerTexture> tex_b = face->getTexture(LLRender::ALTERNATE_DIFFUSE_MAP);

            gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
            gGL.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);

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

                    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
                    gGL.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);

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

                gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
                gGL.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);

                moon_shader->unbind();
            }
        }

        gGL.popMatrix();
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
        }

        if (!gCubeSnapshot || gPipeline.mReflectionMapManager.isRadiancePass())
        {
            renderSkyCloudsDeferred(origin, camHeightLocal, cloud_shader);
        }
    }
}
