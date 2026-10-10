/**
 * @file llfloater360capture.h
 * @brief UI for LLEquirectCapture - resolution choice, preview, save to disk
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

#include "llfloater.h"
#include "llequirectcapture.h"

class LLButton;
class LLRadioGroup;
class LLTextBox;
class LLViewerTexture;

class LLFloater360Capture : public LLFloater
{
public:
    LLFloater360Capture(const LLSD& key);
    /*virtual*/ ~LLFloater360Capture();

    /*virtual*/ bool postBuild();
    /*virtual*/ void draw();

private:
    void onTakeSnapshot();
    void onSaveToDisk();
    void onSaveLocationPicked(const std::vector<std::string>& filenames);
    U32 getSelectedFaceRes() const;
    void onCaptureComplete(const LLEquirectCapture::Result& result);
    void setCapturing(bool capturing);

    LLRadioGroup* mResolutionRadio;
    LLButton* mTakeSnapshotBtn;
    LLButton* mSaveToDiskBtn;
    LLTextBox* mStatusText;

    LLView* mFacePlaceholder[6];
    LLView* mEquirectPlaceholder;

    LLPointer<LLViewerTexture> mFacePreviewTex[6];
    LLPointer<LLViewerTexture> mEquirectPreviewTex;

    // S24 : already-written LL_PATH_LOGS path - Save to Disk copies this
    // file rather than re-encoding the in-memory preview.
    std::string mLastEquirectPath;

    bool mCapturing;
};
