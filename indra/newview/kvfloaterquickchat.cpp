/**
 * @file kvfloaterquickchat.cpp
 * @brief KV Quick Chat - Compact chat bar for immersive in-world chatting
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

#include "kvfloaterquickchat.h"
#include "lllineeditor.h"
#include "llfocusmgr.h"
#include "llagent.h"
#include "llviewercontrol.h"
#include "llgesturemgr.h"
#include "llstring.h"
#include "llbutton.h"
#include "llemojihelper.h"
#include "lltoolbarview.h"

extern void send_chat_from_viewer(const std::string& utf8_out_text, EChatType type, S32 channel);

KVFloaterQuickChat::KVFloaterQuickChat(const LLSD& key)
:   LLDockableFloater(NULL, false, key),
    mInputEditor(NULL),
    mEmojiPickerBtn(NULL)
{
}

KVFloaterQuickChat::~KVFloaterQuickChat()
{
}

bool KVFloaterQuickChat::postBuild()
{
    if (!LLDockableFloater::postBuild() || !gToolBarView)
        return false;

    // Get the chat input field
    mInputEditor = getChild<LLLineEditor>("chat_box");

    if (mInputEditor)
    {
        mInputEditor->setKeystrokeCallback(&KVFloaterQuickChat::onInputEditorKeystroke, this);
        mInputEditor->setCommitCallback(boost::bind(&KVFloaterQuickChat::sendChat, this, CHAT_TYPE_NORMAL));
        mInputEditor->setCommitOnFocusLost(false);
        mInputEditor->setRevertOnEsc(false);
        mInputEditor->setIgnoreTab(true);
        mInputEditor->setPassDelete(true);
        mInputEditor->setReplaceNewlinesWithSpaces(false);
        mInputEditor->setEnableLineHistory(true);
    }

    // Get the emoji picker button
    mEmojiPickerBtn = getChild<LLButton>("emoji_picker_btn");
    if (mEmojiPickerBtn)
    {
        mEmojiPickerBtn->setClickedCallback(boost::bind(&KVFloaterQuickChat::onEmojiPickerClicked, this));
        mEmojiPickerBtn->setMouseDownCallback(boost::bind(&KVFloaterQuickChat::onEmojiPickerClicked, this));
    }

    // Lock the height - floater should only resize horizontally
    // Height is just big enough for: controls (23px) + padding (9px) = 32px (no title bar)
    setResizeLimits(200, QUICK_CHAT_HEIGHT);  // min_width=200, min_height=32 (same as XML)

    // Prevent vertical resizing by making min and max height the same
    LLRect rect = getRect();
    rect.mTop = rect.mBottom + QUICK_CHAT_HEIGHT;  // Force height to exactly QUICK_CHAT_HEIGHT
    setRect(rect);
    setCanResize(true);  // Allow horizontal resize only

    // Dock against whichever toolbar (left/right/bottom) currently hosts
    // the "kvquickchat" button, instead of floating freely on screen.
    dockToToolbarButton("kvquickchat");
    setDocked(true);

    return true;
}

void KVFloaterQuickChat::onOpen(const LLSD& key)
{
    LLFloater::onOpen(key);

    // Focus the chat input field when opened
    if (mInputEditor)
    {
        mInputEditor->setFocus(true);
        mInputEditor->selectAll();
    }
}

void KVFloaterQuickChat::setVisible(bool visible)
{
    LLDockableFloater::setVisible(visible);

    if (visible && mInputEditor)
    {
        // Focus the chat input when made visible
        mInputEditor->setFocus(true);
    }
}

void KVFloaterQuickChat::dockToToolbarButton(const std::string& toolbarButtonName)
{
    LLDockControl::DocAt dock_pos = getDockControlPos(toolbarButtonName);
    LLView* anchor_panel = gToolBarView->findChildView(toolbarButtonName);

    if (!anchor_panel)
        return;

    // No dock tongue (arrow): keeps the flyout flush against the toolbar
    // instead of leaving a gap for the arrow to draw in.
    setUseTongue(false);
    setDockControl(new LLDockControl(anchor_panel, this, getDockTongue(dock_pos), dock_pos));
}

LLDockControl::DocAt KVFloaterQuickChat::getDockControlPos(const std::string& toolbarButtonName)
{
    LLCommandId command_id(toolbarButtonName);
    S32 toolbar_loc = gToolBarView->hasCommand(command_id);

    // Default (bottom toolbar, or button not yet placed on any toolbar):
    // dock above the button, pointing down at it.
    LLDockControl::DocAt doc_at = LLDockControl::TOP;

    switch (toolbar_loc)
    {
        case LLToolBarEnums::TOOLBAR_LEFT:
            doc_at = LLDockControl::RIGHT;
            break;

        case LLToolBarEnums::TOOLBAR_RIGHT:
            doc_at = LLDockControl::LEFT;
            break;
    }

    return doc_at;
}

void KVFloaterQuickChat::reshape(S32 width, S32 height, bool called_from_parent)
{
    // Always force height to fixed value - only allow horizontal resizing
    LLFloater::reshape(width, QUICK_CHAT_HEIGHT, called_from_parent);
}

bool KVFloaterQuickChat::handleKeyHere(KEY key, MASK mask)
{
    bool handled = false;

    if (KEY_RETURN == key)
    {
        if (mask == MASK_CONTROL)
        {
            // Ctrl+Enter = shout
            sendChat(CHAT_TYPE_SHOUT);
            handled = true;
        }
        else if (mask == MASK_NONE)
        {
            // Enter = normal chat (but let the line editor handle it via commit callback)
            handled = false; // Let line editor's commit callback handle it
        }
    }
    else if (KEY_ESCAPE == key && mask == MASK_NONE)
    {
        // ESC closes the floater
        closeFloater();
        handled = true;
    }

    if (!handled)
    {
        handled = LLFloater::handleKeyHere(key, mask);
    }

    return handled;
}

void KVFloaterQuickChat::sendChat(EChatType type)
{
    if (!mInputEditor)
        return;

    LLWString text = mInputEditor->getConvertedText();
    if (text.empty())
        return;

    // Check for channel number (e.g., /20 message)
    S32 channel = 0;
    LLWString out_text = text;

    // Strip channel number if present
    if (!text.empty() && text[0] == '/')
    {
        size_t pos = 1;
        while (pos < text.length() && isdigit(text[pos]))
        {
            pos++;
        }
        if (pos > 1 && (pos >= text.length() || isspace(text[pos])))
        {
            std::string channel_str = wstring_to_utf8str(text.substr(1, pos - 1));
            channel = atoi(channel_str.c_str());
            if (pos < text.length())
            {
                out_text = text.substr(pos + 1);
            }
            else
            {
                out_text.clear();
            }
        }
    }

    std::string utf8_text = wstring_to_utf8str(out_text);
    std::string utf8_revised_text;

    // Try to trigger a gesture if on channel 0
    if (channel == 0)
    {
        // Process gestures - returns true if gesture found
        if (!LLGestureMgr::instance().triggerAndReviseString(utf8_text, &utf8_revised_text))
        {
            utf8_revised_text = utf8_text;
        }
    }
    else
    {
        utf8_revised_text = utf8_text;
    }

    utf8_revised_text = utf8str_trim(utf8_revised_text);

    if (!utf8_revised_text.empty())
    {
        // Send the chat with animation
        send_chat_from_viewer(utf8_revised_text, type, channel);
    }

    // Clear the input and add to history
    mInputEditor->updateHistory();
    mInputEditor->setText(LLStringUtil::null);

    // Stop typing indicator
    gAgent.stopTyping();
}

void KVFloaterQuickChat::onEmojiPickerClicked()
{
    if (!mInputEditor || !mEmojiPickerBtn)
        return;

    // Toggle emoji helper
    if (LLEmojiHelper::instance().isActive(mInputEditor))
    {
        // Hide if already showing
        LLEmojiHelper::instance().hideHelper(mInputEditor);
    }
    else
    {
        // Show emoji helper positioned near the button
        // The helper will automatically insert emojis into the focused line editor
        LLRect button_rect = mEmojiPickerBtn->getRect();
        S32 local_x = button_rect.mLeft;
        S32 local_y = button_rect.mBottom;

        // Create commit callback that inserts emoji into our input editor
        // LLLineEditor doesn't have insertEmoji() and handleUnicodeCharHere() requires focus,
        // so we manually insert the emoji at cursor position
        auto commit_cb = [this](llwchar emoji)
        {
            if (mInputEditor)
            {
                // Ensure the editor has focus
                mInputEditor->setFocus(true);

                // Get current text and cursor position
                LLWString text = mInputEditor->getWText();
                S32 cursor_pos = mInputEditor->getCursor();

                // Insert emoji at cursor position
                text.insert(cursor_pos, 1, emoji);

                // Set the new text
                mInputEditor->setText(wstring_to_utf8str(text));

                // Move cursor after the inserted emoji
                mInputEditor->setCursor(cursor_pos + 1);
            }
        };

        // Show the helper with empty short code (shows all emojis)
        LLEmojiHelper::instance().showHelper(mInputEditor, local_x, local_y, "", commit_cb);
    }
}

// static
void KVFloaterQuickChat::onInputEditorKeystroke(LLLineEditor* caller, void* userdata)
{
    // Handle typing indicator
    KVFloaterQuickChat* self = (KVFloaterQuickChat*)userdata;

    if (self && self->mInputEditor)
    {
        // Start typing if there's text
        std::string text = self->mInputEditor->getText();
        if (!text.empty())
        {
            gAgent.startTyping();
        }
        else
        {
            gAgent.stopTyping();
        }
    }
}
