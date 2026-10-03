/**
 * @file llfloaterkrlvcontrol.h
 * @brief S24 KRLV Control floater - the single control surface for KRLV (RLVa).
 * Gated on the viewer's Adult maturity preference, carries an always-visible
 * warning, and requires a per-viewer PIN on every open.
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

#ifndef LL_LLFLOATERKRLVCONTROL_H
#define LL_LLFLOATERKRLVCONTROL_H

#include "llfloater.h"
#include <boost/signals2.hpp>
#include <map>
#include <vector>
#include "krlvowners.h"

class LLCheckBoxCtrl;
class LLLineEditor;
class LLPanel;
class LLScrollListCtrl;
class LLComboBox;

class LLFloaterKRLVControl : public LLFloater
{
public:
	LLFloaterKRLVControl(const LLSD& key);
	~LLFloaterKRLVControl() override;

	bool postBuild() override;
	void onOpen(const LLSD& key) override;
	void onClose(bool app_quitting) override;
	void setMinimized(bool minimized) override;
	void draw() override;

	// Menu/hotkey entry point: checks the maturity gate, then shows the floater.
	static void openFromMenu();

	// True when the viewer's Adult maturity preference allows KRLV Control to open.
	static bool passesAgeGate();

private:
	void refreshLockState();
	void relockIfMinimizedTooLong();
	void onLockTimeoutChanged();
	void updateLockTimeoutWarning();
	void onPinUnlock();
	void onPinSetOrChange();
	void onEnableToggled();
	void syncEnableCheckbox();
	void onMaturityChanged();

	// Communicate tab: owners are added, removed and changed only with the PIN.
	void loadOwnerList();
	void refreshOwnerList();
	void onOwnerSelected();
	void onOwnerAdd();
	void onOwnerRemove();
	void onOwnerUpdate();
	void onOwnerTest();
	void notifyOwners(const std::string& text);

	// Auto-reply tab: command log. Rows carry {source, owner} as their value.
	void populateCommandLog();
	void onLogListObject(bool whitelist);
	LLScrollListCtrl* mCmdLog = nullptr;
	LLTimer mCmdLogTimer;
	std::string mCmdLogKey;   // last rendered contents, so a refresh never clears the selection

	// Objects tab: whitelist/blacklist of source objects, and open/closed mode.
	// Rebuilds both lists. The mode checkboxes are only reset when syncMode is true, so an
	// unapplied edit survives a list change.
	void refreshObjectLists(bool syncMode = false);
	void onObjectModeApply();
	void onObjectListRemove(bool whitelist);
	LLCheckBoxCtrl*   mObjectClosedCheck = nullptr;
	LLCheckBoxCtrl*   mObjectClosedConfirm = nullptr;
	LLScrollListCtrl* mObjectWhiteList = nullptr;
	LLScrollListCtrl* mObjectBlackList = nullptr;

	// Commands tab
	void populateCommands();
	void onCommandSetSelected(bool green);
	void onCommandsApply();
	int  committedCommandState(const std::string& behaviour) const;

	// Safeword tab
	void onSafewordSet();
	void updateSafewordStatus();
	void updateStatusPanel();
	int  selectedOwnerIndex() const;

	bool mUnlocked;
	boost::signals2::connection mMaturityConnection;
	LLTimer           mMinimizeTimer;   // counts minimised time for the auto re-lock

	LLPanel*          mLockedPanel;
	LLPanel*          mContentPanel;
	LLLineEditor*     mPinEntry;
	LLLineEditor*     mPinNew;
	LLLineEditor*     mPinConfirm;
	LLCheckBoxCtrl*   mEnableCheck;
	LLUICtrl*         mLockTimeoutWarning;

	LLComboBox*       mRestrCategory;
	LLLineEditor*     mRestrSearch;
	LLScrollListCtrl* mRestrList;
	LLUICtrl*         mRestrSummary;
	std::map<std::string, int> mPendingCommands;   // behaviour -> 0 = red (available), 1 = green (off)

	LLUICtrl*         mSafewordStatus;
	LLLineEditor*     mSafewordNew;
	LLLineEditor*     mSafewordConfirm;

	LLScrollListCtrl* mOwnerList;
	LLCheckBoxCtrl*   mNoticeSettings;
	LLCheckBoxCtrl*   mNoticeTamper;
	LLCheckBoxCtrl*   mNoticeTimestamp;
	LLCheckBoxCtrl*   mNoticeOwnerChanges;
	LLLineEditor*     mOwnerName;
	LLLineEditor*     mOwnerUuid;
	std::vector<KRlv::KRlvOwner> mOwners;
};

#endif // LL_LLFLOATERKRLVCONTROL_H
