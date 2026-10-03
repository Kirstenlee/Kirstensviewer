/**
 * @file llfloaterkrlvcontrol.cpp
 * @brief S24 KRLV Control floater - see llfloaterkrlvcontrol.h.
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

#include "llfloaterkrlvcontrol.h"

#include "llagent.h"
#include "llcheckboxctrl.h"
#include "llfloaterreg.h"
#include "llnotificationsutil.h"
#include "lllineeditor.h"
#include "llpanel.h"
#include "llscrolllistctrl.h"
#include "llscrolllistitem.h"
#include "llcontrol.h"
#include "llmd5.h"
#include "lluuid.h"
#include "llviewercontrol.h"
#include "krlvhandler.h"
#include "krlvnotice.h"
#include "krlvsafeword.h"
#include "krlvstatus.h"
#include "krlvintegrity.h"
#include "llcombobox.h"
#include "llfloaterkrlvcatalog.h"
#include <algorithm>

namespace
{
	// Salted MD5 of the PIN. This keeps a casual reader of the settings file from
	// seeing the PIN; it is a deterrent for shared machines, not a strong secret.
	std::string hashPin(const std::string& salt, const std::string& pin)
	{
		LLMD5 md5;
		md5.update(salt + ":" + pin);
		md5.finalize();
		char hex[33];
		md5.hex_digest(hex);
		return std::string(hex);
	}
}

LLFloaterKRLVControl::LLFloaterKRLVControl(const LLSD& key)
	: LLFloater(key),
	  mUnlocked(false),
	  mLockedPanel(nullptr),
	  mContentPanel(nullptr),
	  mPinEntry(nullptr),
	  mPinNew(nullptr),
	  mPinConfirm(nullptr),
	  mEnableCheck(nullptr),
	  mLockTimeoutWarning(nullptr),
	  mOwnerList(nullptr),
	  mNoticeSettings(nullptr),
	  mNoticeTamper(nullptr),
	  mNoticeTimestamp(nullptr),
	  mNoticeOwnerChanges(nullptr),
	  mOwnerName(nullptr),
	  mOwnerUuid(nullptr),
	  mSafewordStatus(nullptr),
	  mSafewordNew(nullptr),
	  mSafewordConfirm(nullptr),
	  mRestrCategory(nullptr),
	  mRestrSearch(nullptr),
	  mRestrList(nullptr),
	  mRestrSummary(nullptr)
{
}

LLFloaterKRLVControl::~LLFloaterKRLVControl()
{
	mMaturityConnection.disconnect();
}

bool LLFloaterKRLVControl::postBuild()
{
	// Adult-only: close the moment the maturity preference drops below Adult, so an
	// open floater cannot stay on screen for a younger user.
	mMaturityConnection = gSavedSettings.getControl("PreferredMaturity")->getCommitSignal()->connect(
		boost::bind(&LLFloaterKRLVControl::onMaturityChanged, this));

	mLockedPanel = getChild<LLPanel>("locked_panel");
	mContentPanel = getChild<LLPanel>("content_panel");
	mPinEntry = getChild<LLLineEditor>("pin_entry");
	mPinNew = getChild<LLLineEditor>("pin_new");
	mPinConfirm = getChild<LLLineEditor>("pin_confirm");
	mEnableCheck = getChild<LLCheckBoxCtrl>("krlv_enable_check");

	childSetAction("pin_unlock_btn", boost::bind(&LLFloaterKRLVControl::onPinUnlock, this));
	childSetAction("pin_set_btn", boost::bind(&LLFloaterKRLVControl::onPinSetOrChange, this));
	mEnableCheck->setCommitCallback(boost::bind(&LLFloaterKRLVControl::onEnableToggled, this));

	mLockTimeoutWarning = getChild<LLUICtrl>("lock_timeout_warning");
	getChild<LLUICtrl>("lock_timeout_spin")->setCommitCallback(boost::bind(&LLFloaterKRLVControl::onLockTimeoutChanged, this));

	// Communicate tab
	mOwnerList = getChild<LLScrollListCtrl>("owner_list");
	mNoticeSettings = getChild<LLCheckBoxCtrl>("notify_settings");
	mNoticeTamper = getChild<LLCheckBoxCtrl>("notify_tamper");
	mNoticeTimestamp = getChild<LLCheckBoxCtrl>("notify_timestamp");
	mNoticeOwnerChanges = getChild<LLCheckBoxCtrl>("notify_ownerchanges");
	mOwnerName = getChild<LLLineEditor>("owner_name");
	mOwnerUuid = getChild<LLLineEditor>("owner_uuid");
	mOwnerList->setCommitCallback(boost::bind(&LLFloaterKRLVControl::onOwnerSelected, this));
	childSetAction("owner_add_btn", boost::bind(&LLFloaterKRLVControl::onOwnerAdd, this));
	childSetAction("owner_remove_btn", boost::bind(&LLFloaterKRLVControl::onOwnerRemove, this));
	childSetAction("owner_update_btn", boost::bind(&LLFloaterKRLVControl::onOwnerUpdate, this));
	childSetAction("owner_test_btn", boost::bind(&LLFloaterKRLVControl::onOwnerTest, this));

	// Safeword tab
	mSafewordStatus = getChild<LLUICtrl>("safeword_status");
	mSafewordNew = getChild<LLLineEditor>("safeword_new");
	mSafewordConfirm = getChild<LLLineEditor>("safeword_confirm");
	childSetAction("safeword_set_btn", boost::bind(&LLFloaterKRLVControl::onSafewordSet, this));

	// Auto-reply tab: command log
	mCmdLog = getChild<LLScrollListCtrl>("cmd_log");
	populateCommandLog();
	childSetAction("cmd_white_btn", boost::bind(&LLFloaterKRLVControl::onLogListObject, this, true));
	childSetAction("cmd_black_btn", boost::bind(&LLFloaterKRLVControl::onLogListObject, this, false));

	// Objects tab
	mObjectClosedCheck = getChild<LLCheckBoxCtrl>("object_closed_check");
	mObjectClosedConfirm = getChild<LLCheckBoxCtrl>("object_closed_confirm");
	mObjectWhiteList = getChild<LLScrollListCtrl>("object_white_list");
	mObjectBlackList = getChild<LLScrollListCtrl>("object_black_list");
	childSetAction("object_mode_apply", boost::bind(&LLFloaterKRLVControl::onObjectModeApply, this));
	childSetAction("object_white_remove", boost::bind(&LLFloaterKRLVControl::onObjectListRemove, this, true));
	childSetAction("object_black_remove", boost::bind(&LLFloaterKRLVControl::onObjectListRemove, this, false));
	refreshObjectLists(true);

	// Commands tab
	mRestrCategory = getChild<LLComboBox>("restr_category");
	mRestrSearch = getChild<LLLineEditor>("restr_search");
	mRestrList = getChild<LLScrollListCtrl>("restr_list");
	mRestrSummary = getChild<LLUICtrl>("restr_summary");
	mRestrCategory->add("All categories");
	{
		size_t count = 0;
		const KRLVCatalogEntry* catalog = krlvCatalog(count);
		std::vector<std::string> categories;
		for (size_t i = 0; i < count; ++i)
		{
			const std::string category = catalog[i].category;
			if (std::find(categories.begin(), categories.end(), category) == categories.end())
			{
				categories.push_back(category);
				mRestrCategory->add(category);
			}
		}
	}
	mRestrCategory->setCommitCallback(boost::bind(&LLFloaterKRLVControl::populateCommands, this));
	mRestrSearch->setCommitCallback(boost::bind(&LLFloaterKRLVControl::populateCommands, this));
	childSetAction("restr_set_red", boost::bind(&LLFloaterKRLVControl::onCommandSetSelected, this, false));
	childSetAction("restr_set_green", boost::bind(&LLFloaterKRLVControl::onCommandSetSelected, this, true));
	childSetAction("restr_apply", boost::bind(&LLFloaterKRLVControl::onCommandsApply, this));
	populateCommands();
	updateSafewordStatus();
	updateLockTimeoutWarning();

	return true;
}

void LLFloaterKRLVControl::onOpen(const LLSD& key)
{
	// The lock is re-applied by closing the floater (onClose) and by the minimise timer,
	// not on every onOpen call, so a re-open of a shown floater keeps its unlock.
	mPinEntry->setText(LLStringUtil::null);
	refreshLockState();
	syncEnableCheckbox();
	updateLockTimeoutWarning();
	loadOwnerList();
	refreshObjectLists(true);
	updateStatusPanel();

	// Re-check the age gate on every open: the maturity preference can change
	// while the viewer is running.
	if (!passesAgeGate())
	{
		LLNotificationsUtil::add("KRLVControlAgeGate");
		closeFloater();
	}
}

void LLFloaterKRLVControl::openFromMenu()
{
	if (!passesAgeGate())
	{
		LLNotificationsUtil::add("KRLVControlAgeGate");
		return;
	}
	LLFloaterReg::showInstance("krlv_control");
}

bool LLFloaterKRLVControl::passesAgeGate()
{
	return gAgent.prefersAdult();
}

void LLFloaterKRLVControl::refreshLockState()
{
	const bool pin_set = !gSavedSettings.getString("KRLVControlPinHash").empty();
	const bool show_locked = pin_set && !mUnlocked;
	mLockedPanel->setVisible(show_locked);
	mContentPanel->setVisible(!show_locked);
}

void LLFloaterKRLVControl::onPinUnlock()
{
	const std::string salt = gSavedSettings.getString("KRLVControlPinSalt");
	const std::string stored = gSavedSettings.getString("KRLVControlPinHash");
	if (hashPin(salt, mPinEntry->getText()) == stored)
	{
		mUnlocked = true;
	}
	else
	{
		LLNotificationsUtil::add("KRLVControlPinWrong");
	}
	mPinEntry->setText(LLStringUtil::null);
	refreshLockState();
}

void LLFloaterKRLVControl::onPinSetOrChange()
{
	const std::string new_pin = mPinNew->getText();
	if (new_pin.empty() || new_pin != mPinConfirm->getText())
	{
		LLNotificationsUtil::add("KRLVControlPinMismatch");
		return;
	}

	// New salt on every change, so an old hash can't be reused against a new PIN.
	const std::string salt = LLUUID::generateNewID().asString();
	gSavedSettings.setString("KRLVControlPinSalt", salt);
	gSavedSettings.setString("KRLVControlPinHash", hashPin(salt, new_pin));

	mPinNew->setText(LLStringUtil::null);
	mPinConfirm->setText(LLStringUtil::null);
	LLNotificationsUtil::add("KRLVControlPinSet");
}

void LLFloaterKRLVControl::onEnableToggled()
{
	const bool enable = mEnableCheck->get();
	KRlvHandler::instance().setEnabled(enable);
	gSavedSettings.setBOOL("KRLVControlEnabled", enable);
}

void LLFloaterKRLVControl::syncEnableCheckbox()
{
	mEnableCheck->set(KRlvHandler::instance().isEnabled());
}

void LLFloaterKRLVControl::onClose(bool app_quitting)
{
	// Closing always re-locks: the next open asks for the PIN again.
	mUnlocked = false;
	LLFloater::onClose(app_quitting);
}

void LLFloaterKRLVControl::setMinimized(bool minimized)
{
	// Only a real transition matters: focus changes also call setMinimized(false) on a
	// floater that was never minimised, and must not re-lock it.
	const bool was_minimized = isMinimized();
	LLFloater::setMinimized(minimized);
	if (minimized)
	{
		if (!was_minimized)
		{
			mMinimizeTimer.reset();
		}
	}
	else if (was_minimized)
	{
		// Restoring after the lock period must ask for the PIN again.
		relockIfMinimizedTooLong();
	}
}

void LLFloaterKRLVControl::draw()
{
	if (isMinimized())
	{
		relockIfMinimizedTooLong();
	}
	if (mCmdLogTimer.getElapsedTimeF32().value() >= 2.f)
	{
		populateCommandLog();
		mCmdLogTimer.reset();
	}
	LLFloater::draw();
}

void LLFloaterKRLVControl::populateCommandLog()
{
	if (!mCmdLog)
	{
		return;
	}
	const std::vector<KRlvHandler::KRlvCommandLogEntry> entries = KRlvHandler::instance().recentCommands();

	// Rebuild only when the log changed. The 2 s refresh would otherwise clear the
	// selection the Whitelist/Blacklist buttons act on.
	std::string key;
	for (const KRlvHandler::KRlvCommandLogEntry& entry : entries)
	{
		key += entry.time + "|" + entry.source.asString() + "|" + entry.owner.asString() + "|" + entry.behaviour + "|" + entry.outcome + "\n";
	}
	if (key == mCmdLogKey)
	{
		return;
	}
	mCmdLogKey = key;

	mCmdLog->deleteAllItems();
	// Newest first.
	for (auto it = entries.rbegin(); it != entries.rend(); ++it)
	{
		LLSD row;
		row["columns"][0]["column"] = "time";
		row["columns"][0]["value"] = it->time;
		row["columns"][1]["column"] = "source";
		row["columns"][1]["value"] = it->source.asString();
		row["columns"][2]["column"] = "command";
		row["columns"][2]["value"] = "@" + it->behaviour;
		row["columns"][3]["column"] = "outcome";
		row["columns"][3]["value"] = it->outcome;
		row["value"]["source"] = it->source.asString();
		row["value"]["owner"] = it->owner.asString();
		mCmdLog->addElement(row);
	}
}

void LLFloaterKRLVControl::onLogListObject(bool whitelist)
{
	LLScrollListItem* item = mCmdLog ? mCmdLog->getFirstSelected() : nullptr;
	if (!item)
	{
		LLNotificationsUtil::add("KRLVObjectSelectSource");
		return;
	}
	const LLUUID object(item->getValue()["source"].asString());
	const LLUUID owner(item->getValue()["owner"].asString());
	if (object.isNull() || owner.isNull())
	{
		LLNotificationsUtil::add("KRLVObjectNoSource");
		return;
	}

	// An object sits on one list at a time: a blacklist match always wins, so
	// whitelisting takes it off the blacklist, and blacklisting takes it off the whitelist.
	KRlvHandler& handler = KRlvHandler::instance();
	handler.removeObjectEntry(!whitelist, object, owner);
	handler.addObjectEntry(whitelist, object, owner);
	notifyOwners(std::string(whitelist ? "KRLV Control: object whitelisted - " : "KRLV Control: object blacklisted - ")
		+ object.asString() + " (owner " + owner.asString() + ").");
	refreshObjectLists();
}

void LLFloaterKRLVControl::refreshObjectLists(bool syncMode)
{
	if (!mObjectWhiteList || !mObjectBlackList)
	{
		return;
	}
	const KRlv::KRlvObjectLists lists = KRlvHandler::instance().getObjectLists();
	if (syncMode)
	{
		const bool closed = lists.mode == KRlv::KRlvObjectMode::Closed;
		mObjectClosedCheck->set(closed);
		mObjectClosedConfirm->set(closed);
	}

	auto fill = [](LLScrollListCtrl* list, const std::vector<KRlv::KRlvObjectEntry>& entries)
	{
		list->deleteAllItems();
		for (const KRlv::KRlvObjectEntry& entry : entries)
		{
			LLSD row;
			row["columns"][0]["column"] = "object";
			row["columns"][0]["value"] = entry.object;
			row["columns"][1]["column"] = "owner";
			row["columns"][1]["value"] = entry.owner;
			row["value"]["object"] = entry.object;
			row["value"]["owner"] = entry.owner;
			list->addElement(row);
		}
	};
	fill(mObjectWhiteList, lists.whitelist);
	fill(mObjectBlackList, lists.blacklist);
}

void LLFloaterKRLVControl::onObjectModeApply()
{
	const bool closed = mObjectClosedCheck->get();
	if (closed && !mObjectClosedConfirm->get())
	{
		LLNotificationsUtil::add("KRLVObjectClosedNeedConfirm");
		return;
	}

	KRlvHandler& handler = KRlvHandler::instance();
	const bool wasClosed = handler.getObjectLists().mode == KRlv::KRlvObjectMode::Closed;
	handler.setObjectMode(closed ? KRlv::KRlvObjectMode::Closed : KRlv::KRlvObjectMode::Open);
	if (wasClosed != closed)
	{
		notifyOwners(closed
			? "KRLV Control: object mode set to closed - only whitelisted objects are heard."
			: "KRLV Control: object mode set to open - only blacklisted objects are refused.");
	}
	refreshObjectLists(true);
}

void LLFloaterKRLVControl::onObjectListRemove(bool whitelist)
{
	LLScrollListCtrl* list = whitelist ? mObjectWhiteList : mObjectBlackList;
	LLScrollListItem* item = list ? list->getFirstSelected() : nullptr;
	if (!item)
	{
		LLNotificationsUtil::add("KRLVObjectSelectEntry");
		return;
	}
	const LLUUID object(item->getValue()["object"].asString());
	const LLUUID owner(item->getValue()["owner"].asString());
	KRlvHandler::instance().removeObjectEntry(whitelist, object, owner);
	notifyOwners(std::string(whitelist ? "KRLV Control: object removed from whitelist - " : "KRLV Control: object removed from blacklist - ")
		+ object.asString() + " (owner " + owner.asString() + ").");
	refreshObjectLists();
}

void LLFloaterKRLVControl::relockIfMinimizedTooLong()
{
	if (!mUnlocked)
	{
		return;
	}
	static LLCachedControl<F32> lock_seconds(gSavedSettings, "KRLVControlLockSeconds", 120.f);
	const F32 limit = lock_seconds();
	if (limit <= 0.f)
	{
		// 0 = never re-lock on minimise (closing still re-locks).
		return;
	}
	if (mMinimizeTimer.getElapsedTimeF32().value() >= limit)
	{
		mUnlocked = false;
		refreshLockState();
	}
}

void LLFloaterKRLVControl::onLockTimeoutChanged()
{
	updateLockTimeoutWarning();
}

void LLFloaterKRLVControl::updateLockTimeoutWarning()
{
	if (mLockTimeoutWarning)
	{
		mLockTimeoutWarning->setVisible(gSavedSettings.getF32("KRLVControlLockSeconds") <= 0.f);
	}
}

// ---- Communicate tab: owners ---------------------------------------------

void LLFloaterKRLVControl::loadOwnerList()
{
	mOwners.clear();
	KRlv::loadOwners(mOwners);
	refreshOwnerList();
}

void LLFloaterKRLVControl::refreshOwnerList()
{
	const int previous = selectedOwnerIndex();
	mOwnerList->deleteAllItems();
	for (const KRlv::KRlvOwner& owner : mOwners)
	{
		LLSD row;
		row["columns"][0]["column"] = "name";
		row["columns"][0]["value"] = owner.name;
		row["columns"][1]["column"] = "uuid";
		row["columns"][1]["value"] = owner.uuid;
		mOwnerList->addElement(row);
	}
	if (previous >= 0 && previous < static_cast<int>(mOwners.size()))
	{
		mOwnerList->selectNthItem(previous);
	}
	onOwnerSelected();
}

int LLFloaterKRLVControl::selectedOwnerIndex() const
{
	return mOwnerList->getFirstSelectedIndex();
}

void LLFloaterKRLVControl::onOwnerSelected()
{
	const int idx = selectedOwnerIndex();
	const bool has_selection = idx >= 0 && idx < static_cast<int>(mOwners.size());
	mNoticeSettings->setEnabled(has_selection);
	mNoticeTamper->setEnabled(has_selection);
	mNoticeTimestamp->setEnabled(has_selection);
	mNoticeOwnerChanges->setEnabled(has_selection);
	if (has_selection)
	{
		const KRlv::KRlvOwner& owner = mOwners[idx];
		mNoticeSettings->set(owner.notifySettings);
		mNoticeTamper->set(owner.notifyTamper);
		mNoticeTimestamp->set(owner.notifyTimestamp);
		mNoticeOwnerChanges->set(owner.notifyOwnerChanges);
		mOwnerName->setText(owner.name);
	}
}

void LLFloaterKRLVControl::updateStatusPanel()
{
	LLUICtrl* status = findChild<LLUICtrl>("status_text");
	if (!status)
	{
		return;
	}

	const std::string tamper = KRlv::tamperChecksActive()
		? std::string("ACTIVE")
		: std::string("not started (waiting for login)");
	const std::string signing = KRlv::hasHmacSha256Hook()
		? std::string("HMAC-SHA256, per-install key")
		: std::string("legacy (HMAC not available)");

	std::string text;
	text += "KRLV version: " + KRlv::krlvVersionText() + "\n";
	text += "RLV API version: " + KRlv::rlvApiVersionText()
		+ " - spec: wiki.secondlife.com/wiki/LSL_Protocol/RestrainedLoveAPI\n";
	text += "Tamper protection: " + tamper + "\n";
	text += "Last state check: " + KRlv::tamperLastCheckText() + "\n";
	text += "Timestamp checks: tolerance 5 minutes; a problem must persist 60 seconds to be reported\n";
	text += "Owners notified by IM: " + std::to_string(mOwners.size()) + "\n";
	text += "Safeword: " + std::string(KRlv::hasSafeword() ? "set" : "not set") + "\n";
	text += "State file signing: " + signing;
	status->setValue(LLSD(text));
}

void LLFloaterKRLVControl::updateSafewordStatus()
{
	if (mSafewordStatus)
	{
		mSafewordStatus->setValue(KRlv::hasSafeword()
			? LLSD("Safeword: set. Change it below if needed.")
			: LLSD("Safeword: not set. Set one now."));
	}
}

void LLFloaterKRLVControl::onSafewordSet()
{
	const std::string entered = mSafewordNew->getText();
	if (entered != mSafewordConfirm->getText())
	{
		LLNotificationsUtil::add("KRLVSafewordMismatch");
		return;
	}

	std::string error;
	if (!KRlv::setSafeword(entered, error))
	{
		LLSD args;
		args["REASON"] = error;
		LLNotificationsUtil::add("KRLVSafewordInvalid", args);
		return;
	}

	mSafewordNew->setText(LLStringUtil::null);
	mSafewordConfirm->setText(LLStringUtil::null);
	updateSafewordStatus();
	LLNotificationsUtil::add("KRLVSafewordSet");
}

void LLFloaterKRLVControl::notifyOwners(const std::string& text)
{
	// Owner-list notices go through the flood limit, to owners who asked for them.
	KRlv::sendOwnerNotice(KRlv::KRlvNoticeKind::OwnerChange, text);
}

void LLFloaterKRLVControl::onOwnerUpdate()
{
	// Saves the selected owner's name and notice choices, then IMs the owners who get
	// owner-list notices so every change is visible to them.
	const int idx = selectedOwnerIndex();
	if (idx < 0 || idx >= static_cast<int>(mOwners.size()))
	{
		LLNotificationsUtil::add("KRLVControlNoOwnerSelected");
		return;
	}
	KRlv::KRlvOwner& owner = mOwners[idx];
	if (!mOwnerName->getText().empty())
	{
		owner.name = mOwnerName->getText();
	}
	owner.notifySettings = mNoticeSettings->get();
	owner.notifyTamper = mNoticeTamper->get();
	owner.notifyTimestamp = mNoticeTimestamp->get();
	owner.notifyOwnerChanges = mNoticeOwnerChanges->get();
	KRlv::saveOwners(mOwners);
	refreshOwnerList();

	notifyOwners("KRLV Control: owner settings changed for " + owner.name + " (" + owner.uuid + ").");
}

void LLFloaterKRLVControl::onOwnerAdd()
{
	const std::string uuid_text = mOwnerUuid->getText();
	const LLUUID id(uuid_text);
	if (id.isNull())
	{
		LLNotificationsUtil::add("KRLVControlOwnerInvalid");
		return;
	}
	const std::string canonical = id.asString();
	for (const KRlv::KRlvOwner& owner : mOwners)
	{
		if (owner.uuid == canonical)
		{
			LLNotificationsUtil::add("KRLVControlOwnerExists");
			return;
		}
	}
	KRlv::KRlvOwner owner;
	owner.uuid = canonical;
	owner.name = mOwnerName->getText().empty() ? std::string("Owner") : mOwnerName->getText();
	mOwners.push_back(owner);
	KRlv::saveOwners(mOwners);

	mOwnerUuid->setText(LLStringUtil::null);
	mOwnerName->setText(LLStringUtil::null);
	refreshOwnerList();
	mOwnerList->selectNthItem(static_cast<S32>(mOwners.size()) - 1);
	onOwnerSelected();

	KRlvHandler::instance().requestSendIm(LLUUID(canonical),
		"KRLV Control: you have been added as an owner. You will receive notices as set by the wearer.");
	notifyOwners("KRLV Control: owner added - " + owner.name + " (" + owner.uuid + ").");
}

void LLFloaterKRLVControl::onOwnerRemove()
{
	const int idx = selectedOwnerIndex();
	if (idx < 0 || idx >= static_cast<int>(mOwners.size()))
	{
		LLNotificationsUtil::add("KRLVControlNoOwnerSelected");
		return;
	}
	const KRlv::KRlvOwner removed = mOwners[idx];
	mOwners.erase(mOwners.begin() + idx);
	KRlv::saveOwners(mOwners);
	refreshOwnerList();

	// The removed owner is always told, whatever their notice choices; the remaining
	// owners get the usual owner-list notice.
	KRlvHandler::instance().requestSendIm(LLUUID(removed.uuid),
		"KRLV Control: you have been removed as an owner. You will no longer receive notices.");
	notifyOwners("KRLV Control: owner removed - " + removed.name + " (" + removed.uuid + ").");
}

void LLFloaterKRLVControl::onOwnerTest()
{
	const int idx = selectedOwnerIndex();
	if (idx < 0 || idx >= static_cast<int>(mOwners.size()))
	{
		LLNotificationsUtil::add("KRLVControlNoOwnerSelected");
		return;
	}
	KRlvHandler::instance().requestSendIm(LLUUID(mOwners[idx].uuid),
		"KRLV Control test: owner notice route check. No action needed.");
	LLNotificationsUtil::add("KRLVControlOwnerTestSent");
}

// ---- Commands tab -------------------------------------------------------------

int LLFloaterKRLVControl::committedCommandState(const std::string& behaviour) const
{
	// 0 = red (available), 1 = green (off, ignored by KRLV), 2 = yellow (set by a script).
	if (KRlvHandler::instance().isBlacklisted(behaviour))
	{
		return 1;
	}
	if (KRlvHandler::instance().isRestricted(behaviour))
	{
		return 2;
	}
	return 0;
}

void LLFloaterKRLVControl::populateCommands()
{
	const std::string category_filter = mRestrCategory->getSimple();
	std::string search = mRestrSearch->getText();
	LLStringUtil::toLower(search);
	const bool tamper_on = KRlv::tamperChecksActive();

	mRestrList->deleteAllItems();
	size_t count = 0;
	const KRLVCatalogEntry* catalog = krlvCatalog(count);
	for (size_t i = 0; i < count; ++i)
	{
		const std::string behaviour = catalog[i].behaviour;
		const std::string category = catalog[i].category;
		if (category_filter != "All categories" && category != category_filter)
		{
			continue;
		}
		std::string haystack = behaviour + " " + catalog[i].title;
		LLStringUtil::toLower(haystack);
		if (!search.empty() && haystack.find(search) == std::string::npos)
		{
			continue;
		}

		const int committed = committedCommandState(behaviour);
		auto pending = mPendingCommands.find(behaviour);
		const int shown = (pending != mPendingCommands.end()) ? pending->second : committed;

		// A committed change that is not red sits in a signed state file, so it carries the lock.
		const bool locked = tamper_on && committed != 0;

		std::string state_text;
		LLColor4 state_colour;
		if (shown == 1)
		{
			state_text = "DISABLED";
			state_colour = LLColor4(0.2f, 0.8f, 0.3f, 1.f);      // green: off
		}
		else if (shown == 2)
		{
			state_text = "SCRIPT";
			state_colour = LLColor4(1.f, 0.85f, 0.2f, 1.f);      // yellow: set by a script
		}
		else
		{
			state_text = "ACTIVE";
			state_colour = LLColor4(0.9f, 0.2f, 0.2f, 1.f);      // red: available
		}
		if (pending != mPendingCommands.end())
		{
			state_text += "*";
		}

		LLSD row;
		row["columns"][0]["column"] = "command";
		row["columns"][0]["value"] = behaviour;
		row["columns"][1]["column"] = "category";
		row["columns"][1]["value"] = category;
		row["columns"][2]["column"] = "state";
		row["columns"][2]["value"] = state_text;
		row["columns"][2]["font_color"] = state_colour.getValue();
		row["columns"][3]["column"] = "lock";
		// Text marker: the scroll list sizes rows to a cell's full image, which would stretch
		// the table, so the lock is shown as text.
		row["columns"][3]["value"] = locked ? std::string("LOCKED") : std::string();
		mRestrList->addElement(row);
	}
}

void LLFloaterKRLVControl::onCommandSetSelected(bool green)
{
	const std::vector<LLScrollListItem*> selected = mRestrList->getAllSelected();
	if (selected.empty())
	{
		LLNotificationsUtil::add("KRLVControlNoOwnerSelected");
		return;
	}
	for (LLScrollListItem* item : selected)
	{
		const std::string behaviour = item->getColumn(0)->getValue().asString();
		const int wanted = green ? 1 : 0;
		if (committedCommandState(behaviour) == wanted)
		{
			// Already in the wanted state, so nothing to apply for this row.
			mPendingCommands.erase(behaviour);
		}
		else
		{
			mPendingCommands[behaviour] = wanted;
		}
	}
	populateCommands();
}

void LLFloaterKRLVControl::onCommandsApply()
{
	if (mPendingCommands.empty())
	{
		mRestrSummary->setValue(LLSD("No changes to apply."));
		return;
	}

	std::string changed;
	int applied = 0;
	for (const auto& [behaviour, wanted] : mPendingCommands)
	{
		if (wanted == 1)
		{
			if (!KRlvHandler::instance().isBlacklisted(behaviour))
			{
				KRlvHandler::instance().addToBlacklist(behaviour);
			}
		}
		else
		{
			if (KRlvHandler::instance().isBlacklisted(behaviour))
			{
				KRlvHandler::instance().removeFromBlacklist(behaviour);
			}
			if (KRlvHandler::instance().isRestricted(behaviour))
			{
				KRlvHandler::instance().clearScriptRestrictions(behaviour);
			}
		}
		changed += behaviour + (wanted == 1 ? " (DISABLED)" : " (ACTIVE)") + "; ";
		++applied;
	}
	mPendingCommands.clear();
	populateCommands();

	mRestrSummary->setValue(LLSD("Applied " + std::to_string(applied) + " change(s)."));
	KRlv::sendOwnerNotice(KRlv::KRlvNoticeKind::Settings,
		"KRLV Control: command states changed by the wearer: " + changed);
}

void LLFloaterKRLVControl::onMaturityChanged()
{
	if (!passesAgeGate())
	{
		// Drop any unlock for this open and close; the next open repeats the gate.
		mUnlocked = false;
		LLNotificationsUtil::add("KRLVControlAgeGate");
		closeFloater();
	}
}
