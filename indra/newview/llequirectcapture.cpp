/**
 * @file llequirectcapture.cpp
 * @brief 360 equirectangular capture - cube-face capture + equirect compose
 *
 * Copyright (c) 2026 Kirstenlee Cinquetti (Lee Quick)
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

#include "llequirectcapture.h"

#include "llviewerwindow.h"
#include "llviewercamera.h"
#include "llvoavatarself.h"
#include "llviewershadermgr.h"
#include "llagentcamera.h"
#include "pipeline.h"
#include "dxpipeline.h"
#include "llimage.h"
#include "lldir.h"
#include "llcoros.h"
#include "lleventcoro.h"

#include "DXReadback.h"
#include "DXCubeTexture.h"
#include "DXCubeMapFaces.h"

// True only during cubeSnapshot()'s own internal display_cube_face() call.
extern bool gCubeSnapshot;

// True for the whole capture, unlike gCubeSnapshot - opts probe-detail
// gates (sky clouds, atmospherics/water haze, local lights) back to full
// detail for a real 360 capture.
extern bool gEquirectCapture;

// Fully composited (tonemap+glow+DoF/FXAA/SMAA+CAS) result of the most
// recent DXPipeline::presentDeferredScreen() call.
extern LLRenderTarget* gLastCompositedPostTarget;

namespace
{
    // D3D11 readback is top-left-origin; LLImageRaw is bottom-up - same
    // row-flip repack simpleSnapshot() uses.
    LLPointer<LLImageRaw> readbackToRaw(ID3D11Texture2D* dx_source, U32 width, U32 height)
    {
        if (!dx_source)
        {
            return nullptr;
        }

        static thread_local std::vector<U8> rgba_scratch;
        rgba_scratch.resize((size_t)width * height * 4);
        if (!DXReadback::readPixels(dx_source, 0, 0, (int)width, (int)height, 4, rgba_scratch.data()))
        {
            LL_WARNS("EquirectCapture") << "readPixels failed" << LL_ENDL;
            return nullptr;
        }

        LLPointer<LLImageRaw> raw = new LLImageRaw((U16)width, (U16)height, 3);
        for (U32 row = 0; row < height; ++row)
        {
            const U8* src_row = rgba_scratch.data() + (size_t)row * width * 4;
            U8* dst_row = raw->getData() + (size_t)(height - 1 - row) * width * 3;
            for (U32 px = 0; px < width; ++px)
            {
                dst_row[px * 3 + 0] = src_row[px * 4 + 0];
                dst_row[px * 3 + 1] = src_row[px * 4 + 1];
                dst_row[px * 3 + 2] = src_row[px * 4 + 2];
            }
        }
        return raw;
    }

    bool saveToJpeg(ID3D11Texture2D* dx_source, U32 width, U32 height, const std::string& filepath)
    {
        LLPointer<LLImageRaw> raw = readbackToRaw(dx_source, width, height);
        if (!raw)
        {
            return false;
        }

        LLPointer<LLImageFormatted> formatted = LLImageFormatted::createFromType(IMG_CODEC_JPEG);
        if (!formatted->encode(raw, 0.0f))
        {
            LL_WARNS("EquirectCapture") << "encode failed for " << filepath << LL_ENDL;
            return false;
        }
        return formatted->save(filepath);
    }

    // Reloads a JPEG this module already wrote, for UI preview - not used
    // by the capture path itself.
    LLPointer<LLImageRaw> loadJpegToRaw(const std::string& filepath)
    {
        LLPointer<LLImageFormatted> formatted = LLImageFormatted::createFromType(IMG_CODEC_JPEG);
        if (!formatted->load(filepath))
        {
            LL_WARNS("EquirectCapture") << "load failed for " << filepath << LL_ENDL;
            return nullptr;
        }

        LLPointer<LLImageRaw> raw = new LLImageRaw();
        if (!formatted->decode(raw, 0.0f))
        {
            LL_WARNS("EquirectCapture") << "decode failed for " << filepath << LL_ENDL;
            return nullptr;
        }
        return raw;
    }

    // S24 : DISABLED (2026-10-09) - call site below no longer invokes this.
    // Was meant to prime real-camera visibility/geometry streaming (both
    // skipped under gCubeSnapshot) before the capture loop runs. Suspected
    // live cause of a serious regression: geometry permanently missing from
    // the REAL game view after a capture, confirmed live. updateCull()/
    // stateSort() below run with gCubeSnapshot genuinely false - stateSort()
    // has a spatial-bridge LOD block gated on exactly that condition which
    // writes PERSISTENT distance/LOD state (mLastUpdateDistance, changeLOD())
    // into the real spatial partition. This function feeds that block an
    // artificial camera (avatar-head origin, forced 90-degree square FOV) -
    // a plausible mechanism for corrupting live LOD state that doesn't self-
    // heal. An earlier live A/B test only ruled this out as a cause of the
    // CAPTURE's own content being wrong, never checked whether it corrupts
    // the live scene afterward - this is a materially different question.
    // Left here, not deleted, pending a confirmed root cause - see
    // project_360capture_v3_plan_2026_10_08.md (memory).
    void warmUpVisibility(const LLVector3& origin)
    {
        LLViewerCamera* camera = LLViewerCamera::getInstance();
        LLViewerCamera saved_camera = LLViewerCamera::instance();

        for (S32 face = 0; face < 6; ++face)
        {
            camera->setAspect(1.0);
            camera->setViewNoBroadcast(F_PI_BY_TWO);
            camera->setOrigin(origin);
            camera->lookDir(LLVector3(DXCubeMapFaces::sLookDirs[face]), LLVector3(DXCubeMapFaces::sUpVecs[face]));

            LLViewerCamera::sCurCameraID = LLViewerCamera::CAMERA_WORLD;
            static LLCullResult result;
            gPipeline.updateCull(*camera, result);
            gPipeline.stateSort(*camera, result);

            *camera = saved_camera;

            llcoro::suspendUntilTimeout(0.15f);
        }
    }

    // Shared 6-face capture loop for captureTestFaces() and captureEquirect().
    // onFace(face, dx_texture) runs once per captured face; dx_texture is
    // only valid for the duration of the call.
    template <typename OnFaceFn>
    bool runCaptureLoop(U32 faceRes, const OnFaceFn& onFace)
    {
        if (faceRes == 0)
        {
            return false;
        }

        const U32 origW = gPipeline.mRT->deferredScreen.getWidth();
        const U32 origH = gPipeline.mRT->deferredScreen.getHeight();

        if (!gPipeline.allocateScreenBuffer(faceRes, faceRes))
        {
            LL_WARNS("EquirectCapture") << "allocateScreenBuffer(" << faceRes << ", " << faceRes << ") failed" << LL_ENDL;
            gPipeline.allocateScreenBuffer(origW, origH);
            return false;
        }

        // Avatar's own head, not the third-person orbit camera's eye.
        const LLVector3 origin = isAgentAvatarValid()
            ? gAgentAvatarp->mHeadp->getWorldPosition()
            : LLViewerCamera::getInstance()->getOrigin();
        bool all_ok = true;

        gEquirectCapture = true;

        for (S32 face = 0; face < 6; ++face)
        {
            // Matches LLReflectionMap::getNearClip()'s MINIMUM_NEAR_CLIP floor.
            const F32 near_clip = 0.1f;
            // dynamic_render=false excludes avatars/control-avs/particles -
            // avoids point-blank self-occlusion at the poles (origin = head).
            bool ok = gViewerWindow->cubeSnapshot(origin, faceRes, face, near_clip, /*dynamic_render=*/false,
                                                   /*useCustomClipPlane=*/false, LLPlane());
            if (!ok)
            {
                LL_WARNS("EquirectCapture") << "cubeSnapshot failed on face " << face << LL_ENDL;
                all_ok = false;
                continue;
            }

            // display_cube_face() never reaches presentDeferredScreen() - run
            // the tonemap resolve explicitly, re-engaging gCubeSnapshot since
            // its own DoF/kveffects/local-light gates key off this flag.
            gCubeSnapshot = true;
            DXPipeline::presentDeferredScreen(gPipeline);
            gCubeSnapshot = false;

            if (!gLastCompositedPostTarget)
            {
                LL_WARNS("EquirectCapture") << "no gLastCompositedPostTarget after face " << face << LL_ENDL;
                all_ok = false;
                continue;
            }

            onFace(face, gLastCompositedPostTarget->getDXColorTexture(0));
        }

        gEquirectCapture = false;
        gPipeline.allocateScreenBuffer(origW, origH);
        return all_ok;
    }
}

bool LLEquirectCapture::captureTestFaces(U32 faceRes)
{
    bool all_saved = true;
    bool ran = runCaptureLoop(faceRes, [&](S32 face, ID3D11Texture2D* dx_texture)
    {
        std::string filename = "equirect_test_face_" + std::to_string(face) + ".jpg";
        std::string filepath = gDirUtilp->getExpandedFilename(LL_PATH_LOGS, filename);
        if (!saveToJpeg(dx_texture, faceRes, faceRes, filepath))
        {
            LL_WARNS("EquirectCapture") << "save failed for face " << face << LL_ENDL;
            all_saved = false;
        }
    });
    return ran && all_saved;
}

bool LLEquirectCapture::captureEquirect(U32 faceRes, U32 outputWidth, U32 outputHeight)
{
    if (outputWidth == 0 || outputHeight == 0)
    {
        return false;
    }

    DXCubeTexture cubeTex;
    if (!cubeTex.create((int)faceRes, (int)faceRes, /*generate_mips=*/true))
    {
        LL_WARNS("EquirectCapture") << "DXCubeTexture::create failed" << LL_ENDL;
        return false;
    }

    // Scratch target for the per-face un-mirror flip (flipXF.hlsl).
    LLRenderTarget flipScratch;
    if (!flipScratch.allocate(faceRes, faceRes, GL_RGBA))
    {
        LL_WARNS("EquirectCapture") << "flipScratch.allocate failed" << LL_ENDL;
        return false;
    }

    bool ran = runCaptureLoop(faceRes, [&](S32 face, ID3D11Texture2D* /*dx_texture*/)
    {
        // Un-mirror this face before storing it in the cube.
        LLGLDisable blend(GL_BLEND);
        flipScratch.bindTarget();
        gFlipXProgram.bind();
        gDX.getTexUnit(0)->bind(gLastCompositedPostTarget);
        gPipeline.mScreenTriangleVB->setBuffer();
        gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
        gFlipXProgram.unbind();
        flipScratch.flush();

        if (!cubeTex.copyFace(face, flipScratch.getDXColorTexture(0)))
        {
            LL_WARNS("EquirectCapture") << "copyFace failed for face " << face << LL_ENDL;
        }
    });

    if (!ran)
    {
        return false;
    }

    cubeTex.generateMipMaps();

    // Compose: one full-screen pass, hardware TextureCube.Sample().
    LLRenderTarget equirectTarget;
    if (!equirectTarget.allocate(outputWidth, outputHeight, GL_RGBA))
    {
        LL_WARNS("EquirectCapture") << "equirectTarget.allocate(" << outputWidth << ", " << outputHeight << ") failed" << LL_ENDL;
        return false;
    }

    {
        LLGLDisable blend(GL_BLEND);
        equirectTarget.bindTarget();
        gEquirectProjectProgram.bind();
        gDX.getTexUnit(0)->bind(&cubeTex);
        gPipeline.mScreenTriangleVB->setBuffer();
        gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
        gEquirectProjectProgram.unbind();
        equirectTarget.flush();
    }

    std::string filepath = gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "equirect_capture.jpg");
    if (!saveToJpeg(equirectTarget.getDXColorTexture(0), outputWidth, outputHeight, filepath))
    {
        LL_WARNS("EquirectCapture") << "save failed for composed equirect" << LL_ENDL;
        return false;
    }

    return true;
}

void LLEquirectCapture::captureEquirectAsync(U32 faceRes, U32 outputWidth, U32 outputHeight,
                                              bool want_previews,
                                              const completion_signal_t::slot_type& on_complete)
{
    LLCoros::instance().launch("EquirectCaptureCoro", [faceRes, outputWidth, outputHeight, want_previews, on_complete]()
    {
        Result result;

        // S24 : warmUpVisibility() call disabled - see its own comment.
        // Suspected cause of a live-view geometry-corruption regression.

        result.success = LLEquirectCapture::captureEquirect(faceRes, outputWidth, outputHeight);
        result.equirectFilePath = gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "equirect_capture.jpg");

        if (!result.success)
        {
            LL_WARNS("EquirectCapture") << "captureEquirect failed, see warnings above" << LL_ENDL;
        }
        else if (want_previews)
        {
            // Second capture pass, deliberately - keeps captureEquirect()
            // itself untouched rather than threading raw buffers out of
            // its capture loop. Acceptable cost for an infrequent action.
            if (!LLEquirectCapture::captureTestFaces(faceRes))
            {
                LL_WARNS("EquirectCapture") << "captureTestFaces (for preview) failed, see warnings above" << LL_ENDL;
            }
            for (S32 face = 0; face < 6; ++face)
            {
                std::string filepath = gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "equirect_test_face_" + std::to_string(face) + ".jpg");
                result.facePreview[face] = loadJpegToRaw(filepath);
            }
            result.equirectPreview = loadJpegToRaw(result.equirectFilePath);
        }

        on_complete(result);
    });
}
