/**
 * @file kvfloaterquickchat.h
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

#ifndef KV_KVFLOATERQUICKCHAT_H
#define KV_KVFLOATERQUICKCHAT_H

#include "lldockablefloater.h"
#include "llchat.h"

class LLLineEditor;
class LLButton;

class KVFloaterQuickChat : public LLDockableFloater
{
public:
    KVFloaterQuickChat(const LLSD& key);
    virtual ~KVFloaterQuickChat();

    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void setVisible(bool visible) override;
    bool handleKeyHere(KEY key, MASK mask) override;
    void reshape(S32 width, S32 height, bool called_from_parent = true) override;

    // Docks the floater against the toolbar button that opens it, on
    // whichever toolbar (left/right/bottom) the user has placed it.
    void dockToToolbarButton(const std::string& toolbarButtonName);

private:
    void sendChat(EChatType type);
    void onEmojiPickerClicked();
    static void onInputEditorKeystroke(LLLineEditor* caller, void* userdata);
    LLDockControl::DocAt getDockControlPos(const std::string& toolbarButtonName);

    LLLineEditor* mInputEditor;
    LLButton* mEmojiPickerBtn;

    static const S32 QUICK_CHAT_HEIGHT = 32;  // Fixed height: controls + padding (no title bar)
};

#endif // KV_KVFLOATERQUICKCHAT_H
