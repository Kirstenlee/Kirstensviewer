/**
 * @file llfloater360capture.cpp
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

#include "llviewerprecompiledheaders.h"

#include "llfloater360capture.h"

#include "llbutton.h"
#include "llradiogroup.h"
#include "lltextbox.h"
#include "llviewertexture.h"
#include "llrender2dutils.h"
#include "llfilepicker.h"
#include "llviewermenufile.h"
#include "llfile.h"
#include "lldir.h"

LLFloater360Capture::LLFloater360Capture(const LLSD& key)
    : LLFloater(key)
    , mResolutionRadio(nullptr)
    , mTakeSnapshotBtn(nullptr)
    , mSaveToDiskBtn(nullptr)
    , mStatusText(nullptr)
    , mEquirectPlaceholder(nullptr)
    , mCapturing(false)
{
    for (int i = 0; i < 6; ++i)
    {
        mFacePlaceholder[i] = nullptr;
    }
}

LLFloater360Capture::~LLFloater360Capture()
{
}

bool LLFloater360Capture::postBuild()
{
    mResolutionRadio = getChild<LLRadioGroup>("resolution_radio");
    mTakeSnapshotBtn = getChild<LLButton>("take_snapshot_button");
    mSaveToDiskBtn = getChild<LLButton>("save_to_disk_button");
    mStatusText = getChild<LLTextBox>("status_text");

    for (int i = 0; i < 6; ++i)
    {
        mFacePlaceholder[i] = getChild<LLView>("face_preview_" + std::to_string(i));
    }
    mEquirectPlaceholder = getChild<LLView>("equirect_preview");

    mResolutionRadio->setSelectedIndex(1); // Medium (1024) by default

    mTakeSnapshotBtn->setCommitCallback(boost::bind(&LLFloater360Capture::onTakeSnapshot, this));
    mSaveToDiskBtn->setCommitCallback(boost::bind(&LLFloater360Capture::onSaveToDisk, this));
    mSaveToDiskBtn->setEnabled(false);

    mStatusText->setText(LLStringExplicit("Ready."));

    return true;
}

U32 LLFloater360Capture::getSelectedFaceRes() const
{
    return (U32)mResolutionRadio->getValue().asInteger();
}

void LLFloater360Capture::setCapturing(bool capturing)
{
    mCapturing = capturing;
    mResolutionRadio->setEnabled(!capturing);
    mTakeSnapshotBtn->setEnabled(!capturing);
    mStatusText->setText(LLStringExplicit(capturing ? "Capturing... do not move." : "Ready."));
}

void LLFloater360Capture::onTakeSnapshot()
{
    if (mCapturing)
    {
        return;
    }

    setCapturing(true);
    U32 faceRes = getSelectedFaceRes();

    LLEquirectCapture::captureEquirectAsync(
        faceRes, faceRes * 4, faceRes * 2,
        /*want_previews=*/true,
        boost::bind(&LLFloater360Capture::onCaptureComplete, this, _1));
}

void LLFloater360Capture::onCaptureComplete(const LLEquirectCapture::Result& result)
{
    setCapturing(false);

    if (!result.success)
    {
        mStatusText->setText(LLStringExplicit("Capture failed - see logs."));
        return;
    }

    for (int i = 0; i < 6; ++i)
    {
        if (result.facePreview[i])
        {
            mFacePreviewTex[i] = LLViewerTextureManager::getLocalTexture(result.facePreview[i].get(), false);
        }
    }
    if (result.equirectPreview)
    {
        mEquirectPreviewTex = LLViewerTextureManager::getLocalTexture(result.equirectPreview.get(), false);
    }

    mLastEquirectPath = result.equirectFilePath;
    mSaveToDiskBtn->setEnabled(true);
    mStatusText->setText(LLStringExplicit("Capture complete."));
}

void LLFloater360Capture::onSaveToDisk()
{
    if (mLastEquirectPath.empty())
    {
        return;
    }

    LLFilePickerReplyThread::startPicker(
        boost::bind(&LLFloater360Capture::onSaveLocationPicked, this, _1),
        LLFilePicker::FFSAVE_JPEG,
        "equirect_360.jpg");
}

void LLFloater360Capture::onSaveLocationPicked(const std::vector<std::string>& filenames)
{
    if (filenames.empty() || mLastEquirectPath.empty())
    {
        return;
    }

    if (!LLFile::copy(mLastEquirectPath, filenames[0]))
    {
        LL_WARNS("EquirectCapture") << "failed to copy " << mLastEquirectPath << " to " << filenames[0] << LL_ENDL;
        mStatusText->setText(LLStringExplicit("Save failed - see logs."));
        return;
    }

    mStatusText->setText(LLStringExplicit("Saved."));
}

void LLFloater360Capture::draw()
{
    LLFloater::draw();

    for (int i = 0; i < 6; ++i)
    {
        if (mFacePreviewTex[i] && mFacePlaceholder[i])
        {
            LLRect r = mFacePlaceholder[i]->getRect();
            dx_draw_scaled_image(r.mLeft, r.mBottom, r.getWidth(), r.getHeight(), mFacePreviewTex[i].get());
        }
    }
    if (mEquirectPreviewTex && mEquirectPlaceholder)
    {
        LLRect r = mEquirectPlaceholder->getRect();
        dx_draw_scaled_image(r.mLeft, r.mBottom, r.getWidth(), r.getHeight(), mEquirectPreviewTex.get());
    }
}
