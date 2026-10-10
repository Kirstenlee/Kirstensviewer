/**
 * @file llequirectcapture.h
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

#pragma once

#include <string>
#include <boost/signals2.hpp>
#include "llpointer.h"

class LLImageRaw;

// Captures the 6 cube faces via the same mechanism the reflection-probe
// system uses (cubeSnapshot()), explicitly re-running the tonemap/post
// resolve per face, then composes them into one equirectangular JPEG via
// hardware TextureCube.Sample().
class LLEquirectCapture
{
public:
    // Result handed to captureEquirectAsync()'s completion callback.
    // facePreview/equirectPreview are only populated when that call's
    // want_previews is true.
    struct Result
    {
        bool success = false;
        std::string equirectFilePath;
        LLPointer<LLImageRaw> facePreview[6];
        LLPointer<LLImageRaw> equirectPreview;
    };

    typedef boost::signals2::signal<void(const Result&)> completion_signal_t;

    // faceRes: capture resolution per face (square). Writes
    // equirect_test_face_0.jpg..face_5.jpg (D3D11 cubemap slice order:
    // 0=+X,1=-X,2=+Y,3=-Y,4=+Z,5=-Z) into LL_PATH_LOGS. Returns false if
    // any face failed to capture or save.
    static bool captureTestFaces(U32 faceRes);

    // Captures the 6 cube faces into a real D3D11 TextureCube, then runs
    // the equirect-projection shader into an outputWidth x outputHeight
    // target and saves equirect_capture.jpg into LL_PATH_LOGS. outputWidth
    // should be 2x outputHeight. Synchronous/blocking - call from a
    // coroutine (see captureEquirectAsync()) if calling from a UI handler.
    static bool captureEquirect(U32 faceRes, U32 outputWidth, U32 outputHeight);

    // Fire-and-forget wrapper around captureEquirect() - runs on a coroutine.
    // want_previews also decodes the 6 raw faces + composed equirect back
    // into Result for UI preview (costs a second capture pass - see
    // captureTestFaces()). on_complete fires on the coroutine's resuming
    // fiber even on failure.
    static void captureEquirectAsync(U32 faceRes, U32 outputWidth, U32 outputHeight,
                                      bool want_previews,
                                      const completion_signal_t::slot_type& on_complete);
};
