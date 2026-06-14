/**
 * @file llfloatersnapshot.cpp
 * @brief Snapshot preview window, allowing saving, e-mailing, etc.
 *
 * $LicenseInfo:firstyear=2004&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2016, Linden Research, Inc....
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llfloatersnapshot.h"

#include "llfloaterreg.h"
#include "llimagefiltersmanager.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "llpostcard.h"
#include "llresmgr.h"       // LLLocale
#include "llsdserialize.h"
#include "llsidetraypanelcontainer.h"
#include "llsnapshotlivepreview.h"
#include "llspinctrl.h"
#include "llviewercontrol.h"
#include "lltoolfocus.h"
#include "lltoolmgr.h"
#include "llwebprofile.h"
#include "llagentcamera.h"
#include "llcallbacklist.h"
#include "lltextbox.h"
 ///----------------------------------------------------------------------------
 /// Local function declarations, constants, enums, and typedefs
 ///----------------------------------------------------------------------------
LLSnapshotFloaterView* gSnapshotFloaterView = nullptr;

constexpr F32 AUTO_SNAPSHOT_TIME_DELAY = 1.f;

constexpr S32 MAX_POSTCARD_DATASIZE = 1572864; // 1.5 megabyte, similar to simulator limit
constexpr S32 MAX_TEXTURE_SIZE = 2048; //max upload texture size 2048 * 2048

static LLDefaultChildRegistry::Register<LLSnapshotFloaterView> r("snapshot_floater_view");

// virtual
LLPanelSnapshot* LLFloaterSnapshot::Impl::getActivePanel(LLFloaterSnapshotBase* floater, bool ok_if_not_found)
{
	LLSideTrayPanelContainer* panel_container = floater->getChild<LLSideTrayPanelContainer>("panel_container");
	LLPanelSnapshot* active_panel = dynamic_cast<LLPanelSnapshot*>(panel_container->getCurrentPanel());

	if (!ok_if_not_found)
	{
		if (!active_panel)
		{
			LL_WARNS() << "No snapshot active panel, current panel index: " << panel_container->getCurrentPanelIndex() << LL_ENDL;
		}
		llassert_always(active_panel != NULL);
	}
	return active_panel;
}

// virtual
LLSnapshotModel::ESnapshotType LLFloaterSnapshotBase::ImplBase::getActiveSnapshotType(LLFloaterSnapshotBase* floater)
{
	LLPanelSnapshot* spanel = getActivePanel(floater);

	if (spanel)
	{
		return spanel->getSnapshotType();
	}
	return LLSnapshotModel::SNAPSHOT_WEB;
}

// virtual
LLSnapshotModel::ESnapshotFormat LLFloaterSnapshot::Impl::getImageFormat(LLFloaterSnapshotBase* floater)
{
	LLPanelSnapshot* active_panel = getActivePanel(floater);
	// FIXME: if the default is not PNG, profile uploads may fail.
	return active_panel ? active_panel->getImageFormat() : LLSnapshotModel::SNAPSHOT_FORMAT_PNG;
}

LLSpinCtrl* LLFloaterSnapshot::Impl::getWidthSpinner(LLFloaterSnapshotBase* floater)
{
	LLPanelSnapshot* active_panel = getActivePanel(floater);
	return active_panel ? active_panel->getWidthSpinner() : nullptr;
}

LLSpinCtrl* LLFloaterSnapshot::Impl::getHeightSpinner(LLFloaterSnapshotBase* floater)
{
	LLPanelSnapshot* active_panel = getActivePanel(floater);
	return active_panel ? active_panel->getHeightSpinner() : nullptr;
}

void LLFloaterSnapshot::Impl::enableAspectRatioCheckbox(LLFloaterSnapshotBase* floater, bool enable)
{
	LLPanelSnapshot* active_panel = getActivePanel(floater);
	if (active_panel)
	{
		active_panel->enableAspectRatioCheckbox(enable);
	}
}

void LLFloaterSnapshot::Impl::setAspectRatioCheckboxValue(LLFloaterSnapshotBase* floater, bool checked)
{
	LLPanelSnapshot* active_panel = getActivePanel(floater);
	if (active_panel)
	{
		active_panel->getChild<LLUICtrl>(active_panel->getAspectRatioCBName())->setValue(checked);
	}
}

LLSnapshotLivePreview* LLFloaterSnapshotBase::getPreviewView()
{
	return impl->getPreviewView();
}

LLSnapshotLivePreview* LLFloaterSnapshotBase::ImplBase::getPreviewView()
{
	LLSnapshotLivePreview* previewp = (LLSnapshotLivePreview*)mPreviewHandle.get();
	return previewp;
}

// virtual
LLSnapshotModel::ESnapshotLayerType LLFloaterSnapshot::Impl::getLayerType(LLFloaterSnapshotBase* floater)
{
	LLSnapshotModel::ESnapshotLayerType type = LLSnapshotModel::SNAPSHOT_TYPE_COLOR;
	LLSD value = floater->getChild<LLUICtrl>("layer_types")->getValue();
	const std::string id = value.asString();
	if (id == "colors")
		type = LLSnapshotModel::SNAPSHOT_TYPE_COLOR;
	else if (id == "depth")
		type = LLSnapshotModel::SNAPSHOT_TYPE_DEPTH;
	return type;
}

void LLFloaterSnapshot::Impl::setResolution(LLFloaterSnapshotBase* floater, const std::string& comboname)
{
	LLComboBox* combo = floater->getChild<LLComboBox>(comboname);
	combo->setVisible(true);
	updateResolution(combo, floater, false);
}

//virtual
void LLFloaterSnapshotBase::ImplBase::updateLayout(LLFloaterSnapshotBase* floaterp)
{
	LLSnapshotLivePreview* previewp = getPreviewView();

	// Legacy auto-sizing path: scale preview area to world aspect ratio,
	// clamped to 700px wide. Used by non-main floaters that are still resizable.
	F32 panel_width = 400.f * gViewerWindow->getWorldViewAspectRatio();
	if (panel_width > 700.f)
		panel_width = 700.f;

	S32 floater_width{ 224 };
	if (mAdvanced)
		floater_width = floater_width + (S32)panel_width;

	LLUICtrl* thumbnail_placeholder = floaterp->getChild<LLUICtrl>("thumbnail_placeholder");
	thumbnail_placeholder->setVisible(mAdvanced);

	floaterp->getChild<LLUICtrl>("image_res_text")->setVisible(mAdvanced);
	floaterp->getChild<LLUICtrl>("file_size_label")->setVisible(mAdvanced);
	if (floaterp->hasChild("360_label", true))
		floaterp->getChild<LLUICtrl>("360_label")->setVisible(mAdvanced);

	// S24: main Snapshot floater uses fixed-size layout driven from explicit
	// state transitions only (postBuild/onOpen/onExtendFloater). Never reshape
	// from the per-frame draw path — that caused a reshape<->clamp lockup.
	const bool resizable = floaterp->isResizable();
	const bool is_main_snapshot = (floaterp->getName() == "Snapshot");
	if (!is_main_snapshot && !mSkipReshaping && !resizable)
	{
		thumbnail_placeholder->reshape((S32)panel_width, thumbnail_placeholder->getRect().getHeight());
		if (!floaterp->isMinimized())
			floaterp->reshape(floater_width, floaterp->getRect().getHeight());
	}

	if (previewp && mAdvanced)
		previewp->setThumbnailPlaceholderRect(floaterp->getThumbnailPlaceholderRect());

	bool use_freeze_frame = floaterp->mFreezeFrameCheck && floaterp->mFreezeFrameCheck->getValue().asBoolean();

	if (use_freeze_frame)
	{
		floaterp->getParent()->setMouseOpaque(true);
		floaterp->reshape(floaterp->getRect().getWidth(), floaterp->getRect().getHeight());
		if (previewp)
		{
			previewp->setVisible(true);
			previewp->setEnabled(true);
		}
		for (LLCharacter* character : LLCharacter::sInstances)
			floaterp->impl->mAvatarPauseHandles.push_back(character->requestPause());
		gSavedSettings.setBOOL("FreezeTime", true);
		if (LLToolMgr::getInstance()->getCurrentToolset() != gCameraToolset)
		{
			floaterp->impl->mLastToolset = LLToolMgr::getInstance()->getCurrentToolset();
			LLToolMgr::getInstance()->setCurrentToolset(gCameraToolset);
		}
	}
	else
	{
		floaterp->getParent()->setMouseOpaque(false);
		floaterp->reshape(floaterp->getRect().getWidth(), floaterp->getRect().getHeight());
		if (previewp)
		{
			previewp->setVisible(false);
			previewp->setEnabled(false);
		}
		floaterp->impl->mAvatarPauseHandles.clear();
		gSavedSettings.setBOOL("FreezeTime", false);
		if (floaterp->impl->mLastToolset)
			LLToolMgr::getInstance()->setCurrentToolset(floaterp->impl->mLastToolset);
	}
}

// Keeps all GUI controls in sync with saved settings. Call whenever a setting
// changes that could affect the controls. No control should be changed directly
// except via helpers called from here.
// virtual
void LLFloaterSnapshot::Impl::updateControls(LLFloaterSnapshotBase* floater)
{
	LLSnapshotModel::ESnapshotType shot_type = getActiveSnapshotType(floater);
	LLSnapshotModel::ESnapshotFormat shot_format = (LLSnapshotModel::ESnapshotFormat)gSavedSettings.getS32("SnapshotFormat");
	LLSnapshotModel::ESnapshotLayerType layer_type = getLayerType(floater);

	floater->getChild<LLComboBox>("local_format_combo")->selectNthItem(gSavedSettings.getS32("SnapshotFormat"));
	floater->getChildView("layer_types")->setEnabled(shot_type == LLSnapshotModel::SNAPSHOT_LOCAL);

	LLPanelSnapshot* active_panel = getActivePanel(floater);
	if (active_panel)
	{
		LLSpinCtrl* width_ctrl = getWidthSpinner(floater);
		LLSpinCtrl* height_ctrl = getHeightSpinner(floater);

		// Initialize spinners to window size when first opened.
		if (width_ctrl->getValue().asInteger() == 0)
		{
			S32 w = gViewerWindow->getWindowWidthRaw();
			LL_DEBUGS() << "Initializing width spinner (" << width_ctrl->getName() << "): " << w << LL_ENDL;
			width_ctrl->setValue(w);
			if (getActiveSnapshotType(floater) == LLSnapshotModel::SNAPSHOT_TEXTURE)
			{
				width_ctrl->setIncrement((F32)(w >> 1));
			}
		}
		if (height_ctrl->getValue().asInteger() == 0)
		{
			S32 h = gViewerWindow->getWindowHeightRaw();
			LL_DEBUGS() << "Initializing height spinner (" << height_ctrl->getName() << "): " << h << LL_ENDL;
			height_ctrl->setValue(h);
			if (getActiveSnapshotType(floater) == LLSnapshotModel::SNAPSHOT_TEXTURE)
			{
				height_ctrl->setIncrement((F32)(h >> 1));
			}
		}

		// Clamp snapshot resolution to window size when showing UI or HUD in snapshot.
		if (gSavedSettings.getBOOL("RenderUIInSnapshot") || gSavedSettings.getBOOL("RenderHUDInSnapshot"))
		{
			S32 width = gViewerWindow->getWindowWidthRaw();
			S32 height = gViewerWindow->getWindowHeightRaw();

			width_ctrl->setMaxValue((F32)width);

			height_ctrl->setMaxValue((F32)height);

			if (width_ctrl->getValue().asInteger() > width)
			{
				width_ctrl->forceSetValue(width);
			}
			if (height_ctrl->getValue().asInteger() > height)
			{
				height_ctrl->forceSetValue(height);
			}
		}
		else
		{
			width_ctrl->setMaxValue(MAX_SNAPSHOT_IMAGE_SIZE);
			height_ctrl->setMaxValue(MAX_SNAPSHOT_IMAGE_SIZE);
		}
	}

	LLSnapshotLivePreview* previewp = getPreviewView();
	bool got_bytes = previewp && previewp->getDataSize() > 0;
	bool got_snap = previewp && previewp->getSnapshotUpToDate();

	LL_DEBUGS() << "Is snapshot up-to-date? " << got_snap << LL_ENDL;

	LLLocale locale(LLLocale::USER_LOCALE);
	std::string bytes_string;
	if (got_snap)
	{
		LLResMgr::getInstance()->getIntegerString(bytes_string, (previewp->getDataSize()) >> 10);
	}

	// Update displayed image resolution.
	LLTextBox* image_res_tb = floater->getChild<LLTextBox>("image_res_text");
	image_res_tb->setVisible(got_snap);
	if (got_snap)
	{
		image_res_tb->setTextArg("[WIDTH]", llformat("%d", previewp->getEncodedImageWidth()));
		image_res_tb->setTextArg("[HEIGHT]", llformat("%d", previewp->getEncodedImageHeight()));
	}

	LLTextBox* file_size_label = floater->getChild<LLTextBox>("file_size_label");
	file_size_label->setTextArg("[SIZE]", got_snap ? bytes_string : floater->getString("unknown"));

	LLUIColor color = LLUIColorTable::instance().getColor("LabelTextColor");
	if (shot_type == LLSnapshotModel::SNAPSHOT_POSTCARD
		&& got_bytes
		&& previewp->getDataSize() > MAX_POSTCARD_DATASIZE)
	{
		color = LLUIColor(LLColor4::red);
	}
	if (shot_type == LLSnapshotModel::SNAPSHOT_WEB
		&& got_bytes
		&& previewp->getDataSize() > LLWebProfile::MAX_WEB_DATASIZE)
	{
		color = LLUIColor(LLColor4::red);
	}

	file_size_label->setColor(color);
	file_size_label->setReadOnlyColor(color); // field gets disabled during upload

	// Update resolution combos and spinners for the active snapshot type.
	switch (shot_type)
	{
	case LLSnapshotModel::SNAPSHOT_WEB:
		layer_type = LLSnapshotModel::SNAPSHOT_TYPE_COLOR;
		floater->getChild<LLUICtrl>("layer_types")->setValue("colors");
		setResolution(floater, "profile_size_combo");
		break;
	case LLSnapshotModel::SNAPSHOT_POSTCARD:
		layer_type = LLSnapshotModel::SNAPSHOT_TYPE_COLOR;
		floater->getChild<LLUICtrl>("layer_types")->setValue("colors");
		setResolution(floater, "postcard_size_combo");
		break;
	case LLSnapshotModel::SNAPSHOT_TEXTURE:
		layer_type = LLSnapshotModel::SNAPSHOT_TYPE_COLOR;
		floater->getChild<LLUICtrl>("layer_types")->setValue("colors");
		setResolution(floater, "texture_size_combo");
		break;
	case  LLSnapshotModel::SNAPSHOT_LOCAL:
		setResolution(floater, "local_size_combo");
		break;
	default:
		break;
	}
	setAspectRatioCheckboxValue(floater, !floater->impl->mAspectRatioCheckOff && gSavedSettings.getBOOL("KeepAspectForSnapshot"));

	if (previewp)
	{
		previewp->setSnapshotType(shot_type);
		previewp->setSnapshotFormat(shot_format);
		previewp->setSnapshotBufferType(layer_type);
	}

	LLPanelSnapshot* current_panel = Impl::getActivePanel(floater);
	if (current_panel)
	{
		LLSD info;
		info["have-snapshot"] = got_snap;
		current_panel->updateControls(info);
	}
	LL_DEBUGS() << "finished updating controls" << LL_ENDL;
}

//virtual
void LLFloaterSnapshotBase::ImplBase::setStatus(EStatus status, bool ok, const std::string& msg)
{
	switch (status)
	{
	case STATUS_READY:
		setWorking(false);
		setFinished(false);
		break;
	case STATUS_WORKING:
		setWorking(true);
		setFinished(false);
		break;
	case STATUS_FINISHED:
		setWorking(false);
		setFinished(true, ok, msg);
		break;
	}

	mStatus = status;
}

// virtual
void LLFloaterSnapshotBase::ImplBase::setNeedRefresh(bool need)
{
	if (!mFloater) return;

	// Don't display the "Refresh to save" message if we're in auto-refresh mode.
	if (gSavedSettings.getBOOL("AutoSnapshot"))
	{
		need = false;
	}

	mFloater->setRefreshLabelVisible(need);
	mNeedRefresh = need;
}

// virtual
void LLFloaterSnapshotBase::ImplBase::checkAutoSnapshot(LLSnapshotLivePreview* previewp, bool update_thumbnail)
{
	if (previewp)
	{
		bool autosnap = gSavedSettings.getBOOL("AutoSnapshot");
		LL_DEBUGS() << "updating " << (autosnap ? "snapshot" : "thumbnail") << LL_ENDL;
		previewp->updateSnapshot(autosnap, update_thumbnail, autosnap ? AUTO_SNAPSHOT_TIME_DELAY : 0.f);
	}
}

// static
void LLFloaterSnapshotBase::ImplBase::onClickNewSnapshot(void* data)
{
	LLFloaterSnapshotBase* floater = (LLFloaterSnapshotBase*)data;
	LLSnapshotLivePreview* previewp = floater->getPreviewView();
	if (previewp)
	{
		floater->impl->setStatus(ImplBase::STATUS_READY);
		LL_DEBUGS() << "updating snapshot" << LL_ENDL;
		previewp->mForceUpdateSnapshot = true;
	}
}

// static
void LLFloaterSnapshotBase::ImplBase::onClickAutoSnap(LLUICtrl* ctrl, void* data)
{
	LLCheckBoxCtrl* check = (LLCheckBoxCtrl*)ctrl;
	gSavedSettings.setBOOL("AutoSnapshot", check->get());

	LLFloaterSnapshotBase* view = (LLFloaterSnapshotBase*)data;
	if (view)
	{
		view->impl->checkAutoSnapshot(view->getPreviewView());
		view->impl->updateControls(view);
	}
}

// static
void LLFloaterSnapshotBase::ImplBase::onClickNoPost(LLUICtrl* ctrl, void* data)
{
	bool no_post = ((LLCheckBoxCtrl*)ctrl)->get();
	gSavedSettings.setBOOL("RenderSnapshotNoPost", no_post);

	LLFloaterSnapshotBase* view = (LLFloaterSnapshotBase*)data;
	view->getPreviewView()->updateSnapshot(true, true);
	view->impl->updateControls(view);
}

// static
void LLFloaterSnapshotBase::ImplBase::onClickFilter(LLUICtrl* ctrl, void* data)
{
	LLFloaterSnapshotBase* view = (LLFloaterSnapshotBase*)data;
	if (view)
	{
		view->impl->updateControls(view);
		LLSnapshotLivePreview* previewp = view->getPreviewView();
		if (previewp)
		{
			view->impl->checkAutoSnapshot(previewp);
			// Note : index 0 of the filter drop down is assumed to be "No filter" in whichever locale
			LLComboBox* filterbox = static_cast<LLComboBox*>(view->getChild<LLComboBox>("filters_combobox"));
			std::string filter_name = (filterbox->getCurrentIndex() ? filterbox->getSimple() : "");
			previewp->setFilter(filter_name);
			previewp->updateSnapshot(true);
		}
	}
}

// static
void LLFloaterSnapshotBase::ImplBase::onClickDisplaySetting(LLUICtrl* ctrl, void* data)
{
	LLFloaterSnapshot* view = (LLFloaterSnapshot*)data;
	if (view)
	{
		LLSnapshotLivePreview* previewp = view->getPreviewView();
		if (previewp)
		{
			previewp->updateSnapshot(true, true);
		}
		view->impl->updateControls(view);
	}
}

void LLFloaterSnapshot::Impl::applyKeepAspectCheck(LLFloaterSnapshotBase* view, bool checked)
{
	gSavedSettings.setBOOL("KeepAspectForSnapshot", checked);

	if (view)
	{
		LLPanelSnapshot* active_panel = getActivePanel(view);
		if (checked && active_panel)
		{
			LLComboBox* combo = view->getChild<LLComboBox>(active_panel->getImageSizeComboName());
			combo->setCurrentByIndex(combo->getItemCount() - 1); // "custom" is always the last index
		}

		LLSnapshotLivePreview* previewp = getPreviewView();
		if (previewp)
		{
			previewp->mKeepAspectRatio = gSavedSettings.getBOOL("KeepAspectForSnapshot");

			S32 w, h;
			previewp->getSize(w, h);
			updateSpinners(view, previewp, w, h, true); // may change w and h

			LL_DEBUGS() << "updating thumbnail" << LL_ENDL;
			previewp->setSize(w, h);
			previewp->updateSnapshot(true);
			checkAutoSnapshot(previewp, true);
		}
	}
}

// static
void LLFloaterSnapshotBase::ImplBase::onCommitFreezeFrame(LLUICtrl* ctrl, void* data)
{
	LLCheckBoxCtrl* check_box = (LLCheckBoxCtrl*)ctrl;
	LLFloaterSnapshotBase* view = (LLFloaterSnapshotBase*)data;
	LLSnapshotLivePreview* previewp = view->getPreviewView();

	if (!view || !check_box || !previewp)
	{
		return;
	}

	gSavedSettings.setBOOL("UseFreezeFrame", check_box->get());

	if (check_box->get())
	{
		previewp->prepareFreezeFrame();
	}

	view->impl->updateLayout(view);
}

void LLFloaterSnapshot::Impl::checkAspectRatio(LLFloaterSnapshotBase* view, S32 index)
{
	LLSnapshotLivePreview* previewp = getPreviewView();

	// Don't round texture sizes; textures are commonly stretched in world, profiles, etc and need to be "squashed" during upload, not cropped here
	if (LLSnapshotModel::SNAPSHOT_TEXTURE == getActiveSnapshotType(view))
	{
		previewp->mKeepAspectRatio = false;
		return;
	}

	bool keep_aspect = false, enable_cb = false;

	if (0 == index) // current window size
	{
		enable_cb = false;
		keep_aspect = true;
	}
	else if (-1 == index) // custom
	{
		enable_cb = true;
		keep_aspect = gSavedSettings.getBOOL("KeepAspectForSnapshot");
	}
	else // predefined resolution
	{
		enable_cb = false;
		keep_aspect = false;
	}

	view->impl->mAspectRatioCheckOff = !enable_cb;

	if (previewp)
	{
		previewp->mKeepAspectRatio = keep_aspect;
	}
}

// Show/hide upload progress indicators.
void LLFloaterSnapshotBase::ImplBase::setWorking(bool working)
{
	LLUICtrl* working_lbl = mFloater->getChild<LLUICtrl>("working_lbl");
	working_lbl->setVisible(working);
	mFloater->getChild<LLUICtrl>("working_indicator")->setVisible(working);

	// All controls should be disabled while posting.
	mFloater->setCtrlsEnabled(!working);
	if (LLPanelSnapshot* active_panel = getActivePanel(mFloater))
	{
		active_panel->enableControls(!working);
		if (working)
		{
			const std::string panel_name = active_panel->getName();
			const std::string prefix = panel_name.substr(getSnapshotPanelPrefix().size());
			std::string progress_text = mFloater->getString(prefix + "_" + "progress_str");
			working_lbl->setValue(progress_text);
		}
	}
}

//virtual
std::string LLFloaterSnapshot::Impl::getSnapshotPanelPrefix()
{
	return "panel_snapshot_";
}

// Show/hide upload status message.
// virtual
void LLFloaterSnapshot::Impl::setFinished(bool finished, bool ok, const std::string& msg)
{
	mFloater->setSuccessLabelPanelVisible(finished && ok);
	mFloater->setFailureLabelPanelVisible(finished && !ok);

	if (finished)
	{
		LLUICtrl* finished_lbl = mFloater->getChild<LLUICtrl>(ok ? "succeeded_lbl" : "failed_lbl");
		std::string result_text = mFloater->getString(msg + "_" + (ok ? "succeeded_str" : "failed_str"));
		finished_lbl->setValue(result_text);
	}
}

// Apply a new resolution selected from the given combobox.
void LLFloaterSnapshot::Impl::updateResolution(LLUICtrl* ctrl, void* data, bool do_update)
{
	LLComboBox* combobox = (LLComboBox*)ctrl;
	LLFloaterSnapshot* view = (LLFloaterSnapshot*)data;

	if (!view || !combobox)
	{
		llassert(view && combobox);
		return;
	}

	std::string sdstring = combobox->getSelectedValue();
	LLSD sdres;
	std::stringstream sstream(sdstring);
	LLSDSerialize::fromNotation(sdres, sstream, sdstring.size());

	S32 width = sdres[0];
	S32 height = sdres[1];

	LLSnapshotLivePreview* previewp = getPreviewView();
	if (previewp && combobox->getCurrentIndex() >= 0)
	{
		S32 original_width = 0, original_height = 0;
		previewp->getSize(original_width, original_height);

		if (gSavedSettings.getBOOL("RenderUIInSnapshot") || gSavedSettings.getBOOL("RenderHUDInSnapshot"))
		{ // clamp to window size when rendering UI/HUD into snapshot
			width = llmin(width, gViewerWindow->getWindowWidthRaw());
			height = llmin(height, gViewerWindow->getWindowHeightRaw());
		}

		if (width == 0 || height == 0)
		{
			// Window size preset.
			LL_DEBUGS() << "Setting preview res from window: " << gViewerWindow->getWindowWidthRaw() << "x" << gViewerWindow->getWindowHeightRaw() << LL_ENDL;
			previewp->setSize(gViewerWindow->getWindowWidthRaw(), gViewerWindow->getWindowHeightRaw());
		}
		else if (width == -1 || height == -1)
		{
			// Custom resolution — read from panel spinners if available, otherwise
			// preserve the size already set in the preview (e.g. from a preset button
			// pressed while no panel is active).
			S32 new_width = 0, new_height = 0;
			LLPanelSnapshot* spanel = getActivePanel(view);
			if (spanel)
			{
				LL_DEBUGS() << "Loading typed res from panel " << spanel->getName() << LL_ENDL;
				new_width = spanel->getTypedPreviewWidth();
				new_height = spanel->getTypedPreviewHeight();
				if (getActiveSnapshotType(view) == LLSnapshotModel::SNAPSHOT_TEXTURE)
				{
					new_width = llmin(new_width, MAX_TEXTURE_SIZE);
					new_height = llmin(new_height, MAX_TEXTURE_SIZE);
				}
			}
			else
			{
				// No active panel — preserve whatever size applyCustomResolution set.
				previewp->getSize(new_width, new_height);
				if (new_width <= 0 || new_height <= 0)
				{
					new_width = gViewerWindow->getWindowWidthRaw();
					new_height = gViewerWindow->getWindowHeightRaw();
				}
				LL_DEBUGS() << "No active panel, preserving preset res: " << new_width << "x" << new_height << LL_ENDL;
			}
			llassert(new_width > 0 && new_height > 0);
			previewp->setSize(new_width, new_height);
		}
		else
		{
			// Fixed preset resolution from combo.
			LL_DEBUGS() << "Setting preview res selected from combo: " << width << "x" << height << LL_ENDL;
			previewp->setSize(width, height);
		}

		checkAspectRatio(view, width);

		previewp->getSize(width, height);

		// Sync spinners with the resolved size. Height-spinner dirty flag drives
		// the aspect-ratio correction direction (height follows width by default).
		updateSpinners(view, previewp, width, height, !getHeightSpinner(view) || !getHeightSpinner(view)->isDirty());

		LLSpinCtrl* w_spin2 = getWidthSpinner(view);
		LLSpinCtrl* h_spin2 = getHeightSpinner(view);
		if (w_spin2 && h_spin2 &&
			(w_spin2->getValue().asInteger() != width || h_spin2->getValue().asInteger() != height))
		{
			w_spin2->setValue(width);
			h_spin2->setValue(height);
			if (getActiveSnapshotType(view) == LLSnapshotModel::SNAPSHOT_TEXTURE)
			{
				w_spin2->setIncrement((F32)(width >> 1));
				h_spin2->setIncrement((F32)(height >> 1));
			}
		}

		if (original_width != width || original_height != height)
		{
			previewp->setSize(width, height);
			checkAutoSnapshot(previewp, false);
			LL_DEBUGS() << "updating thumbnail" << LL_ENDL;
			getPreviewView()->updateSnapshot(true, true, 1.f);
			if (do_update)
			{
				LL_DEBUGS() << "Will update controls" << LL_ENDL;
				updateControls(view);
			}
		}
	}
}

// static
void LLFloaterSnapshot::Impl::onCommitLayerTypes(LLUICtrl* ctrl, void* data)
{
	LLComboBox* combobox = (LLComboBox*)ctrl;

	LLFloaterSnapshot* view = (LLFloaterSnapshot*)data;

	if (view)
	{
		LLSnapshotLivePreview* previewp = view->getPreviewView();
		if (previewp)
		{
			previewp->setSnapshotBufferType((LLSnapshotModel::ESnapshotLayerType)combobox->getCurrentIndex());
		}
		view->impl->checkAutoSnapshot(previewp, true);
		previewp->updateSnapshot(true, true);
	}
}

void LLFloaterSnapshot::Impl::onImageQualityChange(LLFloaterSnapshotBase* view, S32 quality_val)
{
	LLSnapshotLivePreview* previewp = getPreviewView();
	if (previewp)
	{
		previewp->setSnapshotQuality(quality_val);
	}
}

void LLFloaterSnapshot::Impl::onImageFormatChange(LLFloaterSnapshotBase* view)
{
	if (view)
	{
		gSavedSettings.setS32("SnapshotFormat", getImageFormat(view));
		LL_DEBUGS() << "image format changed, updating snapshot" << LL_ENDL;
		getPreviewView()->updateSnapshot(true);
		updateControls(view);
	}
}

// Sets the named size combo to custom mode. Safe to call with any combo name.
void LLFloaterSnapshot::Impl::comboSetCustom(LLFloaterSnapshotBase* floater, const std::string& comboname)
{
	LLComboBox* combo = floater->getChild<LLComboBox>(comboname);
	combo->setCurrentByIndex(combo->getItemCount() - 1); // custom is always last
	// NOTE: checkAspectRatio is NOT called here - caller must handle mKeepAspectRatio explicitly.
}

// Clamps width/height to maintain aspect ratio and max size when KeepAspectForSnapshot is set.
bool LLFloaterSnapshot::Impl::checkImageSize(LLSnapshotLivePreview* previewp, S32& width, S32& height, bool isWidthChanged, S32 max_value)
{
	S32 w = width;
	S32 h = height;

	if (previewp && previewp->mKeepAspectRatio)
	{
		if (gViewerWindow->getWindowWidthRaw() < 1 || gViewerWindow->getWindowHeightRaw() < 1)
			return false;

		F32 aspect_ratio = (F32)gViewerWindow->getWindowWidthRaw() / gViewerWindow->getWindowHeightRaw();

		if (isWidthChanged)
			height = ll_round(width / aspect_ratio);
		else
			width = ll_round(height * aspect_ratio);

		if (width > max_value || height > max_value)
		{
			if (width > height)
			{
				width = max_value;
				height = (S32)(width / aspect_ratio);
			}
			else
			{
				height = max_value;
				width = (S32)(height * aspect_ratio);
			}
		}
	}

	return (w != width || h != height);
}

void LLFloaterSnapshot::Impl::setImageSizeSpinnersValues(LLFloaterSnapshotBase* view, S32 width, S32 height)
{
	LLSpinCtrl* w_spin = getWidthSpinner(view);
	LLSpinCtrl* h_spin = getHeightSpinner(view);
	if (!w_spin || !h_spin) return;
	w_spin->forceSetValue(width);
	h_spin->forceSetValue(height);
	if (getActiveSnapshotType(view) == LLSnapshotModel::SNAPSHOT_TEXTURE)
	{
		w_spin->setIncrement((F32)(width >> 1));
		h_spin->setIncrement((F32)(height >> 1));
	}
}

void LLFloaterSnapshot::Impl::updateSpinners(LLFloaterSnapshotBase* view, LLSnapshotLivePreview* previewp, S32& width, S32& height, bool is_width_changed)
{
	LLSpinCtrl* w_spin = getWidthSpinner(view);
	LLSpinCtrl* h_spin = getHeightSpinner(view);
	if (!w_spin || !h_spin) return;
	w_spin->resetDirty();
	h_spin->resetDirty();
	if (checkImageSize(previewp, width, height, is_width_changed, previewp->getMaxImageSize()))
	{
		setImageSizeSpinnersValues(view, width, height);
	}
}

void LLFloaterSnapshot::Impl::applyCustomResolution(LLFloaterSnapshotBase* view, S32 w, S32 h)
{
	LL_DEBUGS() << "applyCustomResolution(" << w << ", " << h << ")" << LL_ENDL;
	if (!view) return;

	LLSnapshotLivePreview* previewp = getPreviewView();
	if (previewp)
	{
		S32 curw, curh;
		previewp->getSize(curw, curh);
		if (w != curw || h != curh)
		{
			// Cap to spinner max when a panel is active.
			LLSpinCtrl* w_spinner = getWidthSpinner(view);
			if (w_spinner)
				previewp->setMaxImageSize((S32)w_spinner->getMaxValue());

			previewp->setSize(w, h);

			// Force all size combos to custom BEFORE any update so that
			// updateResolution sees [-1,-1] and preserves w,h.
			comboSetCustom(view, "profile_size_combo");
			comboSetCustom(view, "postcard_size_combo");
			comboSetCustom(view, "texture_size_combo");
			comboSetCustom(view, "local_size_combo");

			// S24: Set mKeepAspectRatio based on whether snapshot aspect differs from window.
			// When aspects differ, force mKeepAspectRatio=false to show crop overlay.
			// Also update the KeepAspectForSnapshot setting to sync checkbox state.
			F32 snapshot_aspect = (F32)w / (F32)h;
			F32 window_aspect = (F32)gViewerWindow->getWindowWidthRaw() / (F32)gViewerWindow->getWindowHeightRaw();
			bool keep_aspect = (fabsf(snapshot_aspect - window_aspect) <= 0.01f);
			previewp->mKeepAspectRatio = keep_aspect;
			gSavedSettings.setBOOL("KeepAspectForSnapshot", keep_aspect);

			LL_DEBUGS() << "applied custom resolution, updating thumbnail" << LL_ENDL;
			previewp->updateSnapshot(true, true, 1.0f);
		}
	}
}

// static
void LLFloaterSnapshot::Impl::onSnapshotUploadFinished(LLFloaterSnapshotBase* floater, bool status)
{
	floater->impl->setStatus(STATUS_FINISHED, status, "profile");
}

// static
void LLFloaterSnapshot::Impl::onSendingPostcardFinished(LLFloaterSnapshotBase* floater, bool status)
{
	floater->impl->setStatus(STATUS_FINISHED, status, "postcard");
}

///----------------------------------------------------------------------------
/// Class LLFloaterSnapshotBase
///----------------------------------------------------------------------------

// Default constructor
LLFloaterSnapshotBase::LLFloaterSnapshotBase(const LLSD& key)
	: LLFloater(key),
	mRefreshBtn(NULL),
	mRefreshLabel(NULL),
	mSucceessLblPanel(NULL),
	mFailureLblPanel(NULL)
{}

LLFloaterSnapshotBase::~LLFloaterSnapshotBase()
{
	if (impl->mPreviewHandle.get()) impl->mPreviewHandle.get()->die();

	//unfreeze everything else
	gSavedSettings.setBOOL("FreezeTime", false);

	if (impl->mLastToolset)
	{
		LLToolMgr::getInstance()->setCurrentToolset(impl->mLastToolset);
	}

	delete impl;
}

///----------------------------------------------------------------------------
/// Class LLFloaterSnapshot
///----------------------------------------------------------------------------

static void applySnapshotFixedLayout(LLFloaterSnapshot* self, bool advanced);
static void applyInspectorLayout(LLFloaterSnapshot* self, bool open);

// Default constructor
LLFloaterSnapshot::LLFloaterSnapshot(const LLSD& key)
	: LLFloaterSnapshotBase(key)
{
	impl = new Impl(this);
}

LLFloaterSnapshot::~LLFloaterSnapshot()
{}

// virtual
bool LLFloaterSnapshot::postBuild()
{
	mRefreshBtn = getChild<LLUICtrl>("new_snapshot_btn");
	childSetAction("new_snapshot_btn", ImplBase::onClickNewSnapshot, this);
	mRefreshLabel = getChild<LLUICtrl>("refresh_lbl");
	mSucceessLblPanel = getChild<LLUICtrl>("succeeded_panel");
	mFailureLblPanel = getChild<LLUICtrl>("failed_panel");

	childSetCommitCallback("ui_check", ImplBase::onClickDisplaySetting, this);
	childSetCommitCallback("balance_check", ImplBase::onClickDisplaySetting, this);
	childSetCommitCallback("hud_check", ImplBase::onClickDisplaySetting, this);

	((Impl*)impl)->setAspectRatioCheckboxValue(this, gSavedSettings.getBOOL("KeepAspectForSnapshot"));

	childSetCommitCallback("layer_types", Impl::onCommitLayerTypes, this);
	getChild<LLUICtrl>("layer_types")->setValue("colors");
	getChildView("layer_types")->setEnabled(false);

	mFreezeFrameCheck = getChild<LLUICtrl>("freeze_frame_check");
	mFreezeFrameCheck->setValue(gSavedSettings.getBOOL("UseFreezeFrame"));
	mFreezeFrameCheck->setCommitCallback(&ImplBase::onCommitFreezeFrame, this);

	getChild<LLUICtrl>("auto_snapshot_check")->setValue(gSavedSettings.getBOOL("AutoSnapshot"));
	childSetCommitCallback("auto_snapshot_check", ImplBase::onClickAutoSnap, this);

	getChild<LLUICtrl>("no_post_check")->setValue(gSavedSettings.getBOOL("RenderSnapshotNoPost"));
	childSetCommitCallback("no_post_check", ImplBase::onClickNoPost, this);

	getChild<LLButton>("retract_btn")->setCommitCallback(boost::bind(&LLFloaterSnapshot::onExtendFloater, this));
	getChild<LLButton>("extend_btn")->setCommitCallback(boost::bind(&LLFloaterSnapshot::onExtendFloater, this));

	getChild<LLTextBox>("360_label")->setSoundFlags(LLView::MOUSE_UP);
	getChild<LLTextBox>("360_label")->setShowCursorHand(false);
	getChild<LLTextBox>("360_label")->setClickedCallback(boost::bind(&LLFloaterSnapshot::on360Snapshot, this));

	// Filters
	LLComboBox* filterbox = getChild<LLComboBox>("filters_combobox");
	std::vector<std::string> filter_list = LLImageFiltersManager::getInstance()->getFiltersList();
	for (U32 i = 0; i < filter_list.size(); i++)
	{
		filterbox->add(filter_list[i]);
	}
	childSetCommitCallback("filters_combobox", ImplBase::onClickFilter, this);

	LLWebProfile::setImageUploadResultCallback(boost::bind(&Impl::onSnapshotUploadFinished, this, _1));
	LLPostCard::setPostResultCallback(boost::bind(&Impl::onSendingPostcardFinished, this, _1));

	mThumbnailPlaceholder = getChild<LLUICtrl>("thumbnail_placeholder");

	// create preview window
	LLRect full_screen_rect = getRootView()->getRect();
	LLSnapshotLivePreview::Params p;
	p.rect(full_screen_rect);
	LLSnapshotLivePreview* previewp = new LLSnapshotLivePreview(p);
	LLView* parent_view = gSnapshotFloaterView->getParent();

	parent_view->removeChild(gSnapshotFloaterView);
	// make sure preview is below snapshot floater
	parent_view->addChild(previewp);
	parent_view->addChild(gSnapshotFloaterView);

	//move snapshot floater to special purpose snapshotfloaterview
	gFloaterView->removeChild(this);
	gSnapshotFloaterView->addChild(this);

	// Pre-select "Current Window" resolution.
	getChild<LLComboBox>("profile_size_combo")->selectNthItem(0);
	getChild<LLComboBox>("postcard_size_combo")->selectNthItem(0);
	getChild<LLComboBox>("texture_size_combo")->selectNthItem(0);
	getChild<LLComboBox>("local_size_combo")->selectNthItem(0);
	getChild<LLComboBox>("local_format_combo")->selectNthItem(0);

	impl->mPreviewHandle = previewp->getHandle();
	previewp->setContainer(this);
	impl->updateControls(this);
	impl->setAdvanced(gSavedSettings.getBOOL("AdvanceSnapshot"));
	impl->updateLayout(this);
	applySnapshotFixedLayout(this, gSavedSettings.getBOOL("AdvanceSnapshot"));

	// S24: wire inspector toggle and apply initial state.
	if (LLButton* inspector_btn = findChild<LLButton>("inspector_toggle_btn"))
		inspector_btn->setCommitCallback(boost::bind(&LLFloaterSnapshot::onToggleInspector, this));
	applyInspectorLayout(this, gSavedSettings.getBOOL("SnapshotInspectorOpen"));

	// S24: bind preset / burst / interpolation callbacks.
	{
		static const char* preset_names[] = {
			"preset_169_1080", "preset_169_4k", "preset_916_reel",
			"preset_11_square", "preset_239_cine", "preset_current_window"
		};
		for (const char* nm : preset_names)
		{
			if (LLButton* b = findChild<LLButton>(nm))
			{
				std::string name(nm);
				b->setCommitCallback(boost::bind(&LLFloaterSnapshot::onPresetClicked, this, name));
			}
		}
		if (LLButton* b = findChild<LLButton>("burst_start_btn"))
			b->setCommitCallback(boost::bind(&LLFloaterSnapshot::onBurstStart, this));
		if (LLButton* b = findChild<LLButton>("interp_set_a_btn"))
			b->setCommitCallback(boost::bind(&LLFloaterSnapshot::onInterpSetA, this));
		if (LLButton* b = findChild<LLButton>("interp_set_b_btn"))
			b->setCommitCallback(boost::bind(&LLFloaterSnapshot::onInterpSetB, this));
		if (LLButton* b = findChild<LLButton>("interp_render_btn"))
			b->setCommitCallback(boost::bind(&LLFloaterSnapshot::onInterpRender, this));
		updateInterpStatus();
	}

	previewp->setThumbnailPlaceholderRect(getThumbnailPlaceholderRect());

	return true;
}

// virtual
void LLFloaterSnapshotBase::draw()
{
	LLSnapshotLivePreview* previewp = getPreviewView();

	if (previewp && (previewp->isSnapshotActive() || previewp->getThumbnailLock()))
	{
		// don't render snapshot window in snapshot, even if "show ui" is turned on
		return;
	}

	LLFloater::draw();

	if (previewp && !isMinimized() && mThumbnailPlaceholder->getVisible())
	{
		if (previewp->getThumbnailImage())
		{
			bool working = impl->getStatus() == ImplBase::STATUS_WORKING;
			const LLRect& thumbnail_rect = getThumbnailPlaceholderRect();
			const S32 thumbnail_w = previewp->getThumbnailWidth();
			const S32 thumbnail_h = previewp->getThumbnailHeight();

			// calc preview offset within the preview rect
			const S32 local_offset_x = (thumbnail_rect.getWidth() - thumbnail_w) / 2;
			const S32 local_offset_y = (thumbnail_rect.getHeight() - thumbnail_h) / 2; // preview y pos within the preview rect

			// calc preview offset within the floater rect
			S32 offset_x = thumbnail_rect.mLeft + local_offset_x;
			S32 offset_y = thumbnail_rect.mBottom + local_offset_y;

			gGL.matrixMode(LLRender::MM_MODELVIEW);
			// Apply floater transparency to the texture unless the floater is focused.
			F32 alpha = getTransparencyType() == TT_ACTIVE ? 1.0f : getCurrentTransparency();
			LLColor4 color = working ? LLColor4::grey4 : LLColor4::white;
			gl_draw_scaled_image(offset_x, offset_y,
				thumbnail_w, thumbnail_h,
				previewp->getThumbnailImage(), color % alpha);

			previewp->drawPreviewRect(offset_x, offset_y);

			// S24 Snapshot revamp - Phase 2: composition guides overlay
			// drawn over the floater thumbnail (the live preview view is only
			// visible during freeze-frame fullscreen, so the guides have to be
			// drawn here for the normal in-floater preview).
			previewp->drawGuides(offset_x, offset_y, thumbnail_w, thumbnail_h);

			gGL.pushUIMatrix();
			LLUI::translate((F32)thumbnail_rect.mLeft, (F32)thumbnail_rect.mBottom);
			mThumbnailPlaceholder->draw();
			gGL.popUIMatrix();
		}
	}
	impl->updateLayout(this);
}

//virtual
void LLFloaterSnapshot::onOpen(const LLSD& key)
{
	LLSnapshotLivePreview* preview = getPreviewView();
	if (preview)
	{
		LL_DEBUGS() << "opened, updating snapshot" << LL_ENDL;
		preview->setAllowFullScreenPreview(true);
		preview->updateSnapshot(true);
	}
	focusFirstItem(false);
	gSnapshotFloaterView->setEnabled(true);
	gSnapshotFloaterView->setVisible(true);
	gSnapshotFloaterView->adjustToFitScreen(this, false);

	impl->updateControls(this);
	impl->setAdvanced(gSavedSettings.getBOOL("AdvanceSnapshot"));
	impl->updateLayout(this);
	applySnapshotFixedLayout(this, gSavedSettings.getBOOL("AdvanceSnapshot"));
	applyInspectorLayout(this, gSavedSettings.getBOOL("SnapshotInspectorOpen"));

	// Initialize default tab.
	getChild<LLSideTrayPanelContainer>("panel_container")->getCurrentPanel()->onOpen(LLSD());
}

// S24: one-shot reshape applied only on state transitions (postBuild/onOpen/onExtendFloater).
// Never called from draw() to avoid a reshape<->clamp feedback loop.
static void applySnapshotFixedLayout(LLFloaterSnapshot* self, bool advanced)
{
	if (!self || self->isMinimized()) return;

	constexpr S32 BASE_W = 1064;
	constexpr S32 MAX_W = 1600;
	constexpr S32 COLLAPSED_W = 224;
	constexpr S32 FIXED_H = 600;
	constexpr S32 CHROME_TOP = 23;
	constexpr S32 CHROME_BOT = 30;
	constexpr S32 LEFT_COL_W = 215;
	constexpr S32 RIGHT_PAD = 10;

	S32 target_w = COLLAPSED_W;
	S32 target_h = FIXED_H;
	if (advanced)
	{
		const S32 preview_h = FIXED_H - CHROME_TOP - CHROME_BOT; // 547
		const F32 wa = llclamp(gViewerWindow->getWorldViewAspectRatio(), 0.5f, 3.0f);
		S32 ideal_w = (S32)((F32)preview_h * wa) + LEFT_COL_W + RIGHT_PAD;
		target_w = llclamp(ideal_w, BASE_W, MAX_W);
	}

	const LLRect& cur = self->getRect();
	if (cur.getWidth() != target_w || cur.getHeight() != target_h)
	{
		self->reshape(target_w, target_h);
	}
}

void LLFloaterSnapshot::onExtendFloater()
{
	impl->setAdvanced(gSavedSettings.getBOOL("AdvanceSnapshot"));
	applySnapshotFixedLayout(this, gSavedSettings.getBOOL("AdvanceSnapshot"));
	// After a width change the inspector's anchored rect and the preview
	// placeholder both need to be re-applied against the new floater width.
	applyInspectorLayout(this, gSavedSettings.getBOOL("SnapshotInspectorOpen"));
}

// S24: right-side pro inspector toggle. Shows/hides the inspector panel and
// adjusts the thumbnail placeholder. One-shot only — never called from draw().
static void applyInspectorLayout(LLFloaterSnapshot* self, bool open)
{
	if (!self) return;
	// S24: inspector is always permanently visible in advanced mode.
	const bool advanced = gSavedSettings.getBOOL("AdvanceSnapshot");
	const bool effective_open = advanced; // always open when advanced, ignore toggle
	if (LLUICtrl* inspector = self->findChild<LLUICtrl>("inspector_panel"))
		inspector->setVisible(effective_open);

	// Skip when collapsed — placeholder rect would be degenerate/negative.
	if (!advanced)
		return;

	if (LLUICtrl* thumb = self->findChild<LLUICtrl>("thumbnail_placeholder"))
	{
		// Inspector occupies the right ~246 px; shrink preview to match.
		const S32 INSPECTOR_RESERVE = 246;
		LLRect r = thumb->getRect();
		const LLRect& fr = self->getRect();
		const S32 new_right = effective_open
			? (fr.getWidth() - INSPECTOR_RESERVE)
			: (fr.getWidth() - 10);
		if (new_right <= r.mLeft + 16)
			return;
		r.mRight = new_right;
		thumb->setShape(r);
		if (LLSnapshotLivePreview* previewp = self->getPreviewView())
			previewp->setThumbnailPlaceholderRect(r);
	}
}

void LLFloaterSnapshot::onToggleInspector()
{
	// control_name on the button already flipped the setting before this fires.
	applyInspectorLayout(this, gSavedSettings.getBOOL("SnapshotInspectorOpen"));
}

void LLFloaterSnapshot::on360Snapshot()
{
	LLFloaterReg::showInstance("360capture");
	closeFloater();
}

//virtual
void LLFloaterSnapshotBase::onClose(bool app_quitting)
{
	getParent()->setMouseOpaque(false);

	//unfreeze everything, hide fullscreen preview
	LLSnapshotLivePreview* previewp = getPreviewView();
	if (previewp)
	{
		previewp->setAllowFullScreenPreview(false);
		previewp->setVisible(false);
		previewp->setEnabled(false);
	}

	gSavedSettings.setBOOL("FreezeTime", false);
	impl->mAvatarPauseHandles.clear();

	if (impl->mLastToolset)
	{
		LLToolMgr::getInstance()->setCurrentToolset(impl->mLastToolset);
	}
}

// virtual
S32 LLFloaterSnapshotBase::notify(const LLSD& info)
{
	if (info.has("set-ready"))
	{
		impl->setStatus(ImplBase::STATUS_READY);
		return 1;
	}

	if (info.has("set-working"))
	{
		impl->setStatus(ImplBase::STATUS_WORKING);
		return 1;
	}

	if (info.has("set-finished"))
	{
		LLSD data = info["set-finished"];
		impl->setStatus(ImplBase::STATUS_FINISHED, data["ok"].asBoolean(), data["msg"].asString());
		return 1;
	}

	if (info.has("snapshot-updating"))
	{
		// Disable the send/post/save buttons until snapshot is ready.
		impl->updateControls(this);
		return 1;
	}

	if (info.has("snapshot-updated"))
	{
		// Enable the send/post/save buttons.
		impl->updateControls(this);
		// We've just done refresh.
		impl->setNeedRefresh(false);

		// The refresh button is initially hidden. We show it after the first update,
		// i.e. when preview appears.
		if (mRefreshBtn && !mRefreshBtn->getVisible())
		{
			mRefreshBtn->setVisible(true);
		}
		return 1;
	}

	return 0;
}

// virtual
S32 LLFloaterSnapshot::notify(const LLSD& info)
{
	bool res = LLFloaterSnapshotBase::notify(info);
	if (res)
		return res;
	// A child panel wants to change snapshot resolution.
	if (info.has("combo-res-change"))
	{
		std::string combo_name = info["combo-res-change"]["control-name"].asString();
		((Impl*)impl)->updateResolution(getChild<LLUICtrl>(combo_name), this);
		return 1;
	}

	if (info.has("custom-res-change"))
	{
		LLSD res = info["custom-res-change"];
		((Impl*)impl)->applyCustomResolution(this, res["w"].asInteger(), res["h"].asInteger());
		return 1;
	}

	if (info.has("keep-aspect-change"))
	{
		((Impl*)impl)->applyKeepAspectCheck(this, info["keep-aspect-change"].asBoolean());
		return 1;
	}

	if (info.has("image-quality-change"))
	{
		((Impl*)impl)->onImageQualityChange(this, info["image-quality-change"].asInteger());
		return 1;
	}

	if (info.has("image-format-change"))
	{
		((Impl*)impl)->onImageFormatChange(this);
		return 1;
	}

	return 0;
}

bool LLFloaterSnapshot::isWaitingState()
{
	return (impl->getStatus() == ImplBase::STATUS_WORKING);
}

bool LLFloaterSnapshotBase::ImplBase::updatePreviewList(bool initialized)
{
	if (!initialized)
		return false;

	bool changed = false;
	LL_DEBUGS() << "npreviews: " << LLSnapshotLivePreview::sList.size() << LL_ENDL;
	for (std::set<LLSnapshotLivePreview*>::iterator iter = LLSnapshotLivePreview::sList.begin();
		iter != LLSnapshotLivePreview::sList.end(); ++iter)
	{
		changed |= LLSnapshotLivePreview::onIdle(*iter);
	}
	return changed;
}

void LLFloaterSnapshotBase::ImplBase::updateLivePreview()
{
	// don't update preview for hidden floater
	if (mFloater && mFloater->isInVisibleChain() && ImplBase::updatePreviewList(true))
	{
		LL_DEBUGS() << "changed" << LL_ENDL;
		updateControls(mFloater);
	}
}

//static
void LLFloaterSnapshot::update()
{
	LLFloaterSnapshot* inst = findInstance();
	if (inst != NULL)
	{
		inst->impl->updateLivePreview();
	}
	else
	{
		ImplBase::updatePreviewList(false);
	}
}

// static
LLFloaterSnapshot* LLFloaterSnapshot::findInstance()
{
	return LLFloaterReg::findTypedInstance<LLFloaterSnapshot>("snapshot");
}

// static
LLFloaterSnapshot* LLFloaterSnapshot::getInstance()
{
	return LLFloaterReg::getTypedInstance<LLFloaterSnapshot>("snapshot");
}

// virtual
void LLFloaterSnapshot::saveTexture()
{
	LL_DEBUGS() << "saveTexture" << LL_ENDL;

	LLSnapshotLivePreview* previewp = getPreviewView();
	if (!previewp)
	{
		llassert(previewp != NULL);
		return;
	}

	previewp->saveTexture();
}

void LLFloaterSnapshot::saveLocal(const snapshot_saved_signal_t::slot_type& success_cb, const snapshot_saved_signal_t::slot_type& failure_cb)
{
	LL_DEBUGS() << "saveLocal" << LL_ENDL;
	LLSnapshotLivePreview* previewp = getPreviewView();
	llassert(previewp != NULL);
	if (previewp)
	{
		previewp->saveLocal(success_cb, failure_cb);
	}
}

void LLFloaterSnapshotBase::postSave()
{
	impl->updateControls(this);
	impl->setStatus(ImplBase::STATUS_WORKING);
}

// virtual
void LLFloaterSnapshotBase::postPanelSwitch()
{
	impl->updateControls(this);

	// Remove the success/failure indicator whenever user presses a snapshot option button.
	impl->setStatus(ImplBase::STATUS_READY);
}

void LLFloaterSnapshotBase::inventorySaveFailed()
{
	impl->updateControls(this);
	impl->setStatus(ImplBase::STATUS_FINISHED, false, "inventory");
}

LLPointer<LLImageFormatted> LLFloaterSnapshotBase::getImageData()
{
	// FIXME: May not work for textures.

	LLSnapshotLivePreview* previewp = getPreviewView();
	if (!previewp)
	{
		llassert(previewp != NULL);
		return NULL;
	}

	LLPointer<LLImageFormatted> img = previewp->getFormattedImage();
	if (!img.get())
	{
		LL_WARNS() << "Empty snapshot image data" << LL_ENDL;
		llassert(img.get() != NULL);
	}

	return img;
}

const LLVector3d& LLFloaterSnapshotBase::getPosTakenGlobal()
{
	LLSnapshotLivePreview* previewp = getPreviewView();
	if (!previewp)
	{
		llassert(previewp != NULL);
		return LLVector3d::zero;
	}

	return previewp->getPosTakenGlobal();
}

// static
void LLFloaterSnapshot::setAgentEmail(const std::string& email)
{
	LLFloaterSnapshot* instance = findInstance();
	if (instance)
	{
		LLSideTrayPanelContainer* panel_container = instance->getChild<LLSideTrayPanelContainer>("panel_container");
		LLPanel* postcard_panel = panel_container->getPanelByName("panel_snapshot_postcard");
		postcard_panel->notify(LLSD().with("agent-email", email));
	}
}

///----------------------------------------------------------------------------
/// S24 Inspector: Presets, Burst, Interpolation
///----------------------------------------------------------------------------

void LLFloaterSnapshot::onPresetClicked(const std::string& preset_name)
{
	S32 w = 1920, h = 1080;
	if (preset_name == "preset_169_1080") { w = 1920; h = 1080; }
	else if (preset_name == "preset_169_4k") { w = 3840; h = 2160; }
	else if (preset_name == "preset_916_reel") { w = 1080; h = 1920; }
	else if (preset_name == "preset_11_square") { w = 1080; h = 1080; }
	else if (preset_name == "preset_239_cine") { w = 2048; h = 858; }
	else if (preset_name == "preset_current_window") {
		w = gViewerWindow->getWindowWidthRaw();
		h = gViewerWindow->getWindowHeightRaw();
	}

	if (!getPreviewView()) return;

	// Update spinners if visible; always route through applyCustomResolution.
	Impl* simpl = (Impl*)impl;
	LLSpinCtrl* w_spin = simpl->getWidthSpinner(this);
	LLSpinCtrl* h_spin = simpl->getHeightSpinner(this);
	if (w_spin) w_spin->forceSetValue(w);
	if (h_spin) h_spin->forceSetValue(h);

	notify(LLSD().with("custom-res-change", LLSD().with("w", w).with("h", h)));
}

void LLFloaterSnapshot::onBurstStart()
{
	if (mBurstActive) return;

	mBurstRemaining = llmax(1, (S32)gSavedSettings.getS32("SnapshotBurstCount"));
	mBurstInterval = llmax(0.1f, (F32)gSavedSettings.getF32("SnapshotBurstIntervalSec"));
	mBurstBracket = gSavedSettings.getBOOL("SnapshotBracketEnable");
	mBurstEVStep = (F32)gSavedSettings.getF32("SnapshotBracketEVStep");
	mBurstOrigExposure = (F32)gSavedSettings.getF32("RenderExposure");
	mBurstShotIndex = 0;
	mBurstActive = true;
	onBurstTick();
}

void LLFloaterSnapshot::onBurstTick()
{
	if (!mBurstActive) return;

	if (mBurstBracket)
	{
		// Symmetric EV bracket centred on the original exposure.
		S32 count_total = mBurstShotIndex + mBurstRemaining;
		F32 center = (count_total - 1) * 0.5f;
		F32 ev = (mBurstShotIndex - center) * mBurstEVStep;
		gSavedSettings.setF32("RenderExposure", llclamp(mBurstOrigExposure + ev, 0.f, 16.f));
	}

	LLSnapshotLivePreview* previewp = getPreviewView();
	if (previewp)
	{
		previewp->saveLocal(snapshot_saved_signal_t(), snapshot_saved_signal_t());
	}

	mBurstShotIndex++;
	mBurstRemaining--;

	if (mBurstRemaining <= 0)
	{
		if (mBurstBracket)
			gSavedSettings.setF32("RenderExposure", mBurstOrigExposure);
		mBurstActive = false;
		return;
	}

	LLHandle<LLFloater> handle = getHandle();
	doAfterInterval(
		[handle]()
		{
			LLFloaterSnapshot* self = dynamic_cast<LLFloaterSnapshot*>(handle.get());
			if (self) self->onBurstTick();
		},
		mBurstInterval);
}

void LLFloaterSnapshot::onInterpSetA()
{
	mInterpPosA = gAgentCamera.getCameraPositionGlobal();
	mInterpFocusA = gAgentCamera.getFocusGlobal();
	mInterpHasA = true;
	updateInterpStatus();
}

void LLFloaterSnapshot::onInterpSetB()
{
	mInterpPosB = gAgentCamera.getCameraPositionGlobal();
	mInterpFocusB = gAgentCamera.getFocusGlobal();
	mInterpHasB = true;
	updateInterpStatus();
}

void LLFloaterSnapshot::updateInterpStatus()
{
	if (LLTextBox* tb = findChild<LLTextBox>("interp_status"))
	{
		std::string s = std::string("A: ") + (mInterpHasA ? "set" : "not set")
			+ "    B: " + (mInterpHasB ? "set" : "not set");
		tb->setText(s);
	}
}

static F32 s_ease(F32 t, const std::string& mode)
{
	t = llclamp(t, 0.f, 1.f);
	if (mode == "ease_in")     return t * t;
	if (mode == "ease_out")    return 1.f - (1.f - t) * (1.f - t);
	if (mode == "ease_in_out") return t < 0.5f ? 2.f * t * t : 1.f - 2.f * (1.f - t) * (1.f - t);
	return t; // linear
}

void LLFloaterSnapshot::onInterpRender()
{
	if (mInterpActive || !mInterpHasA || !mInterpHasB) return;

	mInterpTotalSteps = llmax(2, gSavedSettings.getS32("SnapshotInterpSteps"));
	mInterpCurStep = 0;
	mInterpActive = true;
	mInterpStepInterval = 0.25f;
	onInterpTick();
}

void LLFloaterSnapshot::onInterpTick()
{
	if (!mInterpActive) return;

	std::string ease = gSavedSettings.getString("SnapshotInterpEase");
	F32 raw = (F32)mInterpCurStep / (F32)(mInterpTotalSteps - 1);
	F32 t = s_ease(raw, ease);

	LLVector3d pos = mInterpPosA + (mInterpPosB - mInterpPosA) * t;
	LLVector3d focus = mInterpFocusA + (mInterpFocusB - mInterpFocusA) * t;

	gAgentCamera.setFocusOnAvatar(false, false);
	gAgentCamera.setCameraPosAndFocusGlobal(pos, focus, LLUUID::null);

	// Defer capture one frame so the renderer draws the new camera pose first —
	// without this every frame samples the same stale buffer.
	S32 step_index = mInterpCurStep;
	LLHandle<LLFloater> handle = getHandle();
	doAfterInterval(
		[handle, step_index]()
		{
			LLFloaterSnapshot* self = dynamic_cast<LLFloaterSnapshot*>(handle.get());
			if (!self || !self->mInterpActive)
				return;

			LLSnapshotLivePreview* previewp = self->getPreviewView();
			S32 img_w = 1024, img_h = 768;
			LLSnapshotModel::ESnapshotFormat fmt = LLSnapshotModel::SNAPSHOT_FORMAT_PNG;
			std::string ext = "png";
			if (previewp)
			{
				previewp->getSize(img_w, img_h);
				fmt = previewp->getSnapshotFormat();
				if (fmt == LLSnapshotModel::SNAPSHOT_FORMAT_JPEG) ext = "jpg";
				else if (fmt == LLSnapshotModel::SNAPSHOT_FORMAT_BMP)  ext = "bmp";
			}
			if (img_w <= 0 || img_h <= 0) { img_w = 1024; img_h = 768; }

			std::string dir = LLViewerWindow::getLastSnapshotDir();
			std::string base = gSavedPerAccountSettings.getString("SnapshotBaseName");
			if (base.empty()) base = "Snapshot";
			if (dir.empty())
				dir = gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "");

			// Build a unique filename: <dir>/<base>_interp_NNN[_n].<ext>
			char idxbuf[16];
			snprintf(idxbuf, sizeof(idxbuf), "_interp_%03d", step_index + 1);
			std::string stem = base + idxbuf;
			std::string filepath = gDirUtilp->getExpandedFilename(LL_PATH_NONE, dir, stem + "." + ext);
			S32 dupe = 1;
			while (gDirUtilp->fileExists(filepath))
			{
				char dbuf[16];
				snprintf(dbuf, sizeof(dbuf), "_%d", dupe++);
				filepath = gDirUtilp->getExpandedFilename(LL_PATH_NONE, dir, stem + dbuf + "." + ext);
			}

			gViewerWindow->saveSnapshot(
				filepath,
				img_w, img_h,
				/*show_ui*/ false, /*show_hud*/ false,
				/*do_rebuild*/ false, /*show_balance*/ false,
				LLSnapshotModel::SNAPSHOT_TYPE_COLOR, fmt);

			self->mInterpCurStep++;
			if (self->mInterpCurStep >= self->mInterpTotalSteps)
			{
				self->mInterpActive = false;
				return;
			}
			self->onInterpTick();
		},
		mInterpStepInterval);
}

///----------------------------------------------------------------------------
/// Class LLSnapshotFloaterView
///----------------------------------------------------------------------------

LLSnapshotFloaterView::LLSnapshotFloaterView(const Params& p) : LLFloaterView(p)
{}

LLSnapshotFloaterView::~LLSnapshotFloaterView()
{}

// virtual
bool LLSnapshotFloaterView::handleKey(KEY key, MASK mask, bool called_from_parent)
{
	// use default handler when not in freeze-frame mode
	if (!gSavedSettings.getBOOL("FreezeTime"))
	{
		return LLFloaterView::handleKey(key, mask, called_from_parent);
	}

	if (called_from_parent)
	{
		// pass all keystrokes down
		LLFloaterView::handleKey(key, mask, called_from_parent);
	}
	else
	{
		// bounce keystrokes back down
		LLFloaterView::handleKey(key, mask, true);
	}
	return true;
}

// virtual
bool LLSnapshotFloaterView::handleMouseDown(S32 x, S32 y, MASK mask)
{
	// use default handler when not in freeze-frame mode
	if (!gSavedSettings.getBOOL("FreezeTime"))
	{
		return LLFloaterView::handleMouseDown(x, y, mask);
	}
	// give floater a change to handle mouse, else camera tool
	if (childrenHandleMouseDown(x, y, mask) == NULL)
	{
		LLToolMgr::getInstance()->getCurrentTool()->handleMouseDown(x, y, mask);
	}
	return true;
}

// virtual
bool LLSnapshotFloaterView::handleMouseUp(S32 x, S32 y, MASK mask)
{
	// use default handler when not in freeze-frame mode
	if (!gSavedSettings.getBOOL("FreezeTime"))
	{
		return LLFloaterView::handleMouseUp(x, y, mask);
	}
	// give floater a change to handle mouse, else camera tool
	if (childrenHandleMouseUp(x, y, mask) == NULL)
	{
		LLToolMgr::getInstance()->getCurrentTool()->handleMouseUp(x, y, mask);
	}
	return true;
}

// virtual
bool LLSnapshotFloaterView::handleHover(S32 x, S32 y, MASK mask)
{
	// use default handler when not in freeze-frame mode
	if (!gSavedSettings.getBOOL("FreezeTime"))
	{
		return LLFloaterView::handleHover(x, y, mask);
	}
	// give floater a change to handle mouse, else camera tool
	if (childrenHandleHover(x, y, mask) == NULL)
	{
		LLToolMgr::getInstance()->getCurrentTool()->handleHover(x, y, mask);
	}
	return true;
}