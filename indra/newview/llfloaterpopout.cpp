/**
 * @file llfloaterpopout.cpp
 * @brief Floater-outside-viewport framework. Phase 1 (Stage A offscreen
 * render correctness + Stage B DirectComposition display + Stage C input/
 * drag/lifecycle) proved the whole approach end-to-end for one floater
 * ("sl_about") and shipped at r4012 - see
 * [[project_floater_popout_v2_directcomposition_2026_09_26]] for the full
 * round-by-round history. Phase 2 (this revision) generalizes every single
 * piece of that from file-scope statics (one floater at a time) to a
 * per-instance PopoutInstance map, so any number of registered floaters can
 * be popped out independently and simultaneously, each with its own host
 * window/thread/render target/composition surface. See inherited-tickling-
 * kurzweil plan: render-to-texture + DirectComposition, phase 2.
 */

#include "llviewerprecompiledheaders.h"

#include "llfloaterpopout.h"

#include "llbutton.h"
#include "lleditmenuhandler.h"
#include "llfloater.h"
#include "llfloaterimcontainer.h"
#include "llfloaterreg.h"
#include "llfocusmgr.h"
#include "llfontdx.h"
#include "llgestureautocompletehelper.h"
#include "llhandle.h"
#include "llkeyboard.h"
#include "llmenugl.h"
#include "llpanel.h"
#include "llrender.h"
#include "llrendertarget.h"
#include "llrender2dutils.h"
#include "lltextbox.h"
#include "llui.h"
#include "lluictrlfactory.h"
#include "llthreadsafequeue.h"

#include "DXUIBatch.h"
#include "DXCompositionSurface.h"
#include <map>
#include <memory>
#include <thread>
#include <future>
#include <vector>
#include <windowsx.h>

namespace
{
    // The registry of floaters this framework knows how to pop out. Adding
    // a new one is exactly one entry here - no XML edits required (see
    // ensurePopoutButtonWired()'s own comment: the button is created in
    // C++ if the floater's own XML didn't already provide one).
    //
    // "im_container" ("Conversations") is a single_instance/reuse_instance
    // LLMultiFloater (floater_im_container.xml) that normally HOSTS
    // "nearby_chat" as one of its tabs - popping IT out draws the whole
    // conversations window (tab strip + whichever conversation, including
    // nearby chat, is currently selected) via the same generic
    // floater->draw() this framework already uses for everything else, no
    // special-casing needed for the hosting relationship. Its close
    // button just hides rather than destroys it (reuse_instance="true"),
    // which the existing generic close-callback handling already accounts
    // for correctly - it never assumed destroy-on-close in the first
    // place (see onFloaterClosed()'s own comment).
    //
    // "nearby_chat" is deliberately NOT independently registered here -
    // confirmed live as popping it out via its OWN icon detaches it from
    // im_container's tab content (floater->setVisible(false) on popOut(),
    // same as every other floater this framework pops), leaving a gap in
    // the container while chat renders in its own separate window instead.
    // The user's call: chat should always just ride along with whatever
    // currently hosts it (im_container normally), never be independently
    // poppable on its own.
    const char* const POPPABLE_FLOATER_NAMES[] = { "sl_about", "im_container" };

    const wchar_t* const HOST_WINDOW_CLASS = L"S24FloaterPopoutHost";

    // Custom messages so window-state changes (resize/show/hide) always
    // run ON the host thread that owns the window, posted via PostMessage()
    // from the main thread rather than calling SetWindowPos/ShowWindow/
    // SetForegroundWindow on it directly cross-thread. Some window messages
    // (WM_WINDOWPOSCHANGING etc) are dispatched SYNCHRONOUSLY to the owning
    // thread by USER32 - a cross-thread SetWindowPos call (this ran every
    // frame, from ensureHostWindow()) blocks the CALLING thread until the
    // owning thread's message loop processes it. If the host thread is
    // ever even briefly unresponsive for any reason, that blocks the MAIN
    // thread too - a real, live deadlock, confirmed as "it has also stopped
    // responding to any interaction". PostMessage() never blocks the caller
    // regardless of the target thread's state.
    constexpr UINT WM_POPOUT_RESIZE = WM_APP + 1; // wParam=width, lParam=height
    constexpr UINT WM_POPOUT_SHOW = WM_APP + 2;   // show+foreground (see popIn(): hiding is a full WM_CLOSE teardown, not this)

    enum class DragMode { None, Moving, Resizing };

    struct PopoutMouseEvent
    {
        enum Type { Down, Up, Move, Wheel, RightDown, RightUp } type = Move;
        S32 x = 0;
        S32 y = 0;
        MASK mask = MASK_NONE;
        S32 clicks = 0;
    };

    struct PopoutKeyEvent
    {
        enum Type { KeyDown, KeyUp, Char } type = KeyDown;
        WORD vk = 0;         // raw virtual-key code - translated to indra's
                              // KEY enum on the MAIN thread (idle()), not
                              // here: LLKeyboard::translateKey() is a
                              // main-thread-owned object, not safe to call
                              // from this thread.
        MASK mask = MASK_NONE;
        llwchar uni_char = 0; // WM_CHAR only; surrogate pairs (characters
                              // outside the Basic Multilingual Plane) are
                              // not reassembled - a known, minor gap, not
                              // relevant to Ctrl+C/Ctrl+A-style shortcuts.
    };

    // Everything specific to ONE popped-out floater - phase 1's file-scope
    // statics, mechanically grouped so any number of these can exist at
    // once, keyed by floater registration name in sInstances below. An
    // instance is created when popOut() succeeds and erased once its full
    // teardown (see idle()'s hostWindowDestroyedExternally handling)
    // completes - it outlives poppedOut briefly during that teardown, which
    // is why isPoppedOut() checks poppedOut specifically, not just presence
    // in the map.
    struct PopoutInstance
    {
        LLRenderTarget offscreen;

        HWND hostWindow = nullptr;
        std::thread hostThread;
        DXCompositionSurface compositionSurface;

        // Set (from the host thread) when the host window is destroyed -
        // both the "user force-closed it via the taskbar" case this was
        // originally written for, AND the normal case now (popIn()/
        // onFloaterClosed() both just post WM_CLOSE and land here too, see
        // their own comments). Checked and cleared on the main thread
        // (idle()) each frame, which fully tears this instance down and
        // erases it from sInstances.
        bool hostWindowDestroyedExternally = false;

        // LLHandle<LLFloater>, not a raw LLFloater* - resolved fresh via
        // .get() every single use, never cached across a call. A raw
        // pointer nulled only by our own close callback was confirmed live
        // (phase 1) to miss at least one real destruction path.
        LLHandle<LLFloater> poppedFloaterHandle;
        bool poppedOut = false;

        // Producer: hostWindowProc() on this instance's own host thread.
        // Consumer: LLFloaterPopoutManager::idle() on the main render
        // thread.
        LLThreadSafeQueue<PopoutMouseEvent> inputQueue;
        LLThreadSafeQueue<PopoutKeyEvent> keyQueue;

        // Non-blocking drag/resize state - this instance's host thread
        // only, never touched from the main thread. See hostWindowProc()'s
        // own comment for why this replaced the OS's modal WM_NCLBUTTONDOWN
        // loop.
        DragMode dragMode = DragMode::None;
        UINT resizeEdge = 0;
        POINT dragStartCursorScreen = {};
        RECT dragStartWindowRect = {};

        // Set by the host thread when a resize gesture ends (WM_LBUTTONUP
        // while dragMode==Resizing) - the resize handler above only ever
        // moves/sizes the real host WINDOW, it never touches the
        // FLOATER's own rect. Without this, the floater's own content
        // (in particular any LLLayoutStack children, e.g. im_container's
        // participant-list/message-pane split) never learns its
        // container actually changed size at all - confirmed live as "a
        // resizable floater that needs to be plumbed in". Read and
        // cleared once per frame on the main thread (idle(), before
        // renderPoppedOut() runs so the floater's rect is already
        // correct by the time it's read this same frame), which calls
        // floater->reshape(resizedWidth, resizedHeight, true) - the
        // normal LLView reshape path, which propagates to children
        // exactly the way a real window resize would.
        bool resizeJustCompleted = false;
        S32 resizedWidth = 0;
        S32 resizedHeight = 0;

        // Set alongside resizeJustCompleted (idle()) - counts down over a
        // few frames in renderPoppedOut(), forcing the offscreen render
        // target to be fully release()d and freshly reallocated/redrawn
        // rather than trusting whatever it already contains. A fast
        // resize drag reallocates this target every single frame it's in
        // progress (see renderPoppedOut()'s own comment on tracking the
        // live window size mid-drag) - a rapid string of GPU resource
        // (re)allocations, confirmed live as occasionally leaving the
        // very first frame or two after release looking like it hadn't
        // fully caught up ("if done too fast"). A few forced clean
        // frames right at the transition is a deliberately cheap,
        // guaranteed-correct hedge rather than trying to pin down the
        // exact GPU-timing race - costs a handful of extra reallocations
        // exactly once per completed resize, not per frame in general.
        S8 forceFullRedrawFrames = 0;

        // Tracks whether offscreen's LAST allocate() call actually
        // succeeded - LLRenderTarget::allocate() (llrendertarget.cpp)
        // unconditionally commits mResX/mResY to the requested size
        // BEFORE it knows whether the underlying D3D11 resource creation
        // succeeded, so getWidth()/getHeight() alone can't tell a
        // genuine successful allocation apart from a failed one that
        // still "looks" like it matches the size we asked for. Without
        // this, a single transient allocation failure (plausible given
        // renderPoppedOut() can reallocate every frame during a live
        // resize drag, or every forced frame after forceFullRedrawFrames)
        // would get masked by the size-match check below and never
        // retried - the popped-out window would silently freeze/stay
        // blank with zero color attachments, no crash, no visible error.
        bool offscreenAllocOk = true;

        // Written by the main thread each frame (renderPoppedOut()), read
        // by this instance's host thread (hostWindowProc()) - plain
        // primitive copies, never a pointer into the floater itself,
        // specifically so the host thread never has to dereference
        // LLFloater* at all.
        S32 headerHeightCache = 0;
        bool floaterResizableCache = false;

        // This framework's own replacement for LLTextEditor's real
        // (wrongly-rendered-when-popped-out) right-click context menu -
        // see showPopoutEditMenu()'s own comment. A direct child of the
        // popped-out floater, so at most one is ever open per instance.
        LLPanel* contextMenuPanel = nullptr;
        bool contextMenuPendingDestroy = false;

        // Same idea, same deferred-destroy discipline, for
        // LLGestureAutocompleteHelper's own real LLFloaterGestureAutocomplete-
        // Picker - see updatePopoutGestureHelper()'s own comment for why
        // that real floater can never correctly render here either.
        LLPanel* gestureHelperPanel = nullptr;
        bool gestureHelperPendingDestroy = false;

        // The one child view (of `floater`) that last received a hover
        // event via idle()'s own Move dispatch - see idle()'s own comment
        // on why this framework has to track this itself instead of
        // relying on LLViewerWindow's real per-frame hover-set diffing
        // (mMouseHoverViews), which only ever walks the MAIN window's own
        // tree. LLHandle, not a raw LLView* - safely null once the view
        // it pointed to is destroyed (e.g. a menu substitute panel/button
        // torn down between one Move event and the next), same
        // discipline as poppedFloaterHandle elsewhere in this file.
        LLHandle<LLView> lastHoverView;
    };

    // Only ever contains entries for floaters CURRENTLY popped out (or
    // mid-teardown - see PopoutInstance's own comment). unique_ptr gives
    // each instance a stable heap address even as the map itself grows/
    // shrinks - required, since hostWindowProc() recovers a raw
    // PopoutInstance* from GWLP_USERDATA (Win32 wndprocs have no implicit
    // "this"; SetWindowLongPtr() is set once, right after CreateWindowExW,
    // in hostWindowThreadMain() below).
    std::map<std::string, std::unique_ptr<PopoutInstance>> sInstances;

    // Persist for a floater regardless of whether it's currently popped
    // out - the button has to work the moment the floater exists, docked
    // or not (see ensurePopoutButtonWired()'s own comment), independent of
    // any single PopoutInstance's lifetime.
    std::map<std::string, void*> sWiredButtonPtrs;
    std::map<std::string, void*> sCloseCallbackWiredFor;

    // A real, explicit, always-visible popout button - NOT a reuse of
    // LLFloater's own BUTTON_TEAR_OFF slot. That reuse attempt
    // (LLFloater::setCanPopout()) failed twice in phase 1 despite correct-
    // looking logic, for a reason never fully pinned down without live
    // diagnostics the user explicitly ruled out continuing to burn rounds
    // on. About's own instance is a hand-authored XUI button
    // (floater_about.xml's "popout_btn") reusing LLFloater's own tear-off
    // icon assets; createPopoutButton() below builds an identical one in
    // C++ for any OTHER registered floater whose XML doesn't already
    // provide one (phase 2's whole point: zero XML edits needed to add a
    // new poppable floater). Position is never trusted from either source -
    // see repositionPopoutButton() below.
    const char* const POPOUT_BUTTON_NAME = "popout_btn";

    // The real title buttons (close/restore/minimize/tear-off/dock/help),
    // in LLFloater::sButtonNames's own order (llfloater.cpp) - direct
    // children of the floater itself (buildButtons() -> addChild()), not
    // of the drag handle.
    const char* const TITLE_BUTTON_NAMES[] = {
        "llfloater_close_btn", "llfloater_restore_btn", "llfloater_minimize_btn",
        "llfloater_tear_off_btn", "llfloater_dock_btn", "llfloater_help_btn"
    };

    // Builds a popout_btn identical to About's own hand-authored XUI one,
    // for a floater whose own XML doesn't provide one - reuses the exact
    // same skin-provided tear-off icon assets. Position is a harmless
    // placeholder; repositionPopoutButton() (below) recomputes it every
    // frame regardless of how the button was created.
    LLButton* createPopoutButton(LLFloater* floater)
    {
        LLButton::Params btn_p;
        btn_p.name(POPOUT_BUTTON_NAME);
        btn_p.rect(LLRect(0, 16, 16, 0));
        btn_p.tab_stop(false);
        btn_p.scale_image(true);
        btn_p.image_unselected(LLUI::getUIImage("tearoffbox.tga"));
        btn_p.image_selected(LLUI::getUIImage("tearoff_pressed.tga"));
        btn_p.image_hover_selected(LLUI::getUIImage("tearoff_pressed.tga"));
        LLButton* btn = LLUICtrlFactory::create<LLButton>(btn_p);
        floater->addChild(btn);
        return btn;
    }

    // LLView::getChild<T>()/getChildView() (llview.h) NEVER return nullptr
    // for a widget type with a registered default - on a miss they
    // silently fall back to a global "dummy widget" (LLUICtrlFactory::
    // getDefaultWidget<T>(), added to a SEPARATE global container, not
    // this floater) instead. Confirmed live via one-shot diagnostic
    // logging (round 40): for "nearby_chat"/"im_container", the "existing
    // button" this file kept finding had a degenerate zero-width rect and
    // a parent pointer that was NOT the floater at all - exactly that
    // dummy, not a real button. createPopoutButton() therefore never ran
    // for either (the dummy is non-null, so `if (!btn)` was never true) -
    // this is why neither ever showed an icon; About was never affected
    // only because its REAL button already exists via XML, so the
    // fallback path never triggers for it. Only LLView::findChildView()
    // (used here directly, not through the templated wrapper) is the
    // honest, nullable raw lookup - direct children only (recurse=false),
    // for the same hosting-relationship reason given in
    // ensurePopoutButtonWired()'s own comment.
    LLButton* findOwnButton(LLFloater* floater, const char* name)
    {
        return dynamic_cast<LLButton*>(floater->findChildView(name, false));
    }

    // A static XML left= for popout_btn can NEVER stay correctly aligned:
    // LLFloater::updateTitleButtons() (llfloater.cpp) packs the REAL title
    // buttons right-to-left using a runtime button_count that only counts
    // whichever of close/restore/minimize/tear-off/dock/help are actually
    // ENABLED right now (help disables itself while minimized or torn off;
    // other floater types may not have tear-off/dock at all), spaced by
    // `UIFloaterCloseBoxSize` read from settings.xml (NOT a fixed 16 -
    // see feedback_s24_settings_xml_authoritative_over_cpp_defaults) times
    // this floater's own mButtonScale (private, no accessor). Instead,
    // every frame: find whichever real title button currently has the
    // smallest mLeft (i.e. is actually the leftmost one drawn right now,
    // whatever the current enabled-set/size/scale happen to be), and place
    // popout_btn immediately to its left, matching its exact size and top -
    // so this tracks correctly regardless of which buttons are enabled or
    // what UIFloaterCloseBoxSize/mButtonScale are, without duplicating any
    // of updateTitleButtons()'s own arithmetic.
    void repositionPopoutButton(LLFloater* floater)
    {
        // findOwnButton(), not getChild<LLButton>() - see its own comment
        // for why the templated getChild()/getChildView() can never
        // return nullptr here.
        LLButton* btn = findOwnButton(floater, POPOUT_BUTTON_NAME);
        if (!btn)
        {
            return;
        }

        bool found = false;
        LLRect leftmost;
        for (const char* name : TITLE_BUTTON_NAMES)
        {
            LLButton* title_btn = findOwnButton(floater, name);
            if (title_btn && title_btn->getVisible() &&
                (!found || title_btn->getRect().mLeft < leftmost.mLeft))
            {
                leftmost = title_btn->getRect();
                found = true;
            }
        }

        // No real title button currently visible - leave popout_btn
        // wherever it last was rather than guess. Nothing to align to yet.
        if (!found)
        {
            return;
        }

        const S32 w = leftmost.getWidth();
        const S32 h = leftmost.getHeight();
        LLRect popout_rect;
        // Clamped to never go negative (off the floater's own left edge,
        // where it would be entirely invisible/unclickable) - a floater
        // narrow enough that there's no room to its left (e.g.
        // floater_im_container.xml's min_width="38", barely enough for
        // close+minimize alone) would otherwise silently place this
        // completely off-screen. Overlapping the real title button in
        // that rare case is still strictly better than invisible - it's
        // drawn on top (a genuine child, same as everything else in this
        // file), so it stays clickable either way.
        popout_rect.setLeftTopAndSize(llmax(0, leftmost.mLeft - (w + 1)), leftmost.mTop, w, h);

        if (!(popout_rect == btn->getRect()))
        {
            btn->setRect(popout_rect);
        }
    }

    void updatePopoutButtonToolTip(LLFloater* floater, bool popped_out)
    {
        if (LLButton* btn = findOwnButton(floater, POPOUT_BUTTON_NAME))
        {
            btn->setToolTip(std::string(popped_out
                ? "Pop back into the main window"
                : "Pop out to its own window"));
        }
    }

    // Called every frame (from idle(), below), for every registered
    // floater that currently exists, regardless of popped-out state -
    // wiring the button as soon as the floater exists, not only from
    // inside popOut(), means it's clickable the moment the floater is
    // first opened, with no menu trigger needed at all.
    //
    // Deliberately NOT a one-shot-ever bool per floater: closing a popped-
    // out floater via its OWN close button (onFloaterClosed(), not popIn())
    // may destroy the underlying LLFloater instance rather than just hide
    // it (confirmed live for About, phase 1 round 26: neither
    // single_instance nor reuse_instance is set), in which case reopening
    // it later creates a genuinely NEW button object that a one-shot flag
    // would never rewire. Comparing the actual button POINTER (cheap: one
    // getChild() lookup + one comparison, nowhere near the cost of a
    // relayout) per floater name instead of a bool means this self-heals
    // correctly whether a given floater persists or gets recreated,
    // without needing to know which is actually true.
    void ensurePopoutButtonWired(LLFloater* floater, const std::string& name)
    {
        if (!floater)
        {
            return;
        }

        // findOwnButton(), not getChild<LLButton>() - see its own comment.
        // This was the actual bug behind "no icon at all" on nearby_chat/
        // im_container (round 40, confirmed via one-shot diagnostic
        // logging, since removed): getChild<LLButton>() NEVER returns
        // nullptr for a widget type with a registered default, so the
        // "does it already have one?" check below was never true, and
        // createPopoutButton() never ran for either floater at all -
        // About was never affected only because its real XML-authored
        // button already satisfies the lookup honestly.
        LLButton* btn = findOwnButton(floater, POPOUT_BUTTON_NAME);
        if (!btn)
        {
            btn = createPopoutButton(floater);
            if (!btn)
            {
                return;
            }
        }

        void*& wired = sWiredButtonPtrs[name];
        if (btn == wired)
        {
            return;
        }

        btn->setClickedCallback([name](LLUICtrl*, const LLSD&)
        {
            if (LLFloaterPopoutManager::isPoppedOut(name))
            {
                LLFloaterPopoutManager::popIn(name);
            }
            else
            {
                LLFloaterPopoutManager::popOut(name);
            }
        });
        updatePopoutButtonToolTip(floater, false);
        wired = btn;
    }

    // Real LLMenuGL context menus (LLTextEditor::showContextMenu() etc)
    // parent themselves into LLMenuGL::sMenuContainer - a single GLOBAL
    // holder that is itself part of the MAIN window's own view tree and
    // drawn by the MAIN window's own UI pass, entirely separate from
    // floater->draw() (which is the only thing renderPoppedOut() actually
    // composites into this framework's own desktop windows). Their
    // position is also computed via localPointToScreen() - the same
    // stale-docked-position bug already fixed for LLDragHandle/
    // LLResizeHandle/LLResizeBar, but never fixable for these, since even
    // with correct coordinates the menu would still be rendered as part of
    // the wrong window/surface. Fixed instead by never letting the real
    // menu survive: after dispatching a right-click normally (so
    // gEditMenuHandler still gets set exactly as it always did),
    // immediately hide whatever LLMenuGL::sMenuContainer just spawned and
    // show this framework's own minimal equivalent - a plain LLPanel/
    // LLButton stack that is a genuine CHILD of the popped-out floater, so
    // it participates in the SAME floater->draw() call and the SAME
    // idle() input dispatch as everything else already does, with no new
    // rendering or input plumbing required. Reuses the exact
    // LLEditMenuHandler::gEditMenuHandler mechanism the Ctrl+C/X/V/A
    // keyboard interception already relies on (see idle()'s key-dispatch
    // loop) - one shared, already-proven source of truth for what these
    // actions mean, not a second copy of that logic. Not chat-specific -
    // works for any widget that sets gEditMenuHandler on right-click,
    // which is the standard LL pattern (confirmed for both About's text
    // editor and chat's LLChatEntry/LLChatHistory, both real LLTextEditor
    // instances).
    void destroyPopoutContextMenu(PopoutInstance& inst)
    {
        if (inst.contextMenuPanel)
        {
            delete inst.contextMenuPanel; // LLView's dtor detaches from its parent.
            inst.contextMenuPanel = nullptr;
        }
        inst.contextMenuPendingDestroy = false;
    }

    struct PopoutEditMenuItem
    {
        const char* label;
        bool (LLEditMenuHandler::*can)() const;
        void (LLEditMenuHandler::*act)();
    };

    // Mirrors menu_text_editor.xml's own Cut/Copy/Paste/Delete/Select All
    // block (its spellcheck-suggestion items are out of scope here - this
    // is about restoring basic editing, not spellcheck UI).
    const PopoutEditMenuItem POPOUT_EDIT_MENU_ITEMS[] = {
        { "Cut",        &LLEditMenuHandler::canCut,       &LLEditMenuHandler::cut },
        { "Copy",       &LLEditMenuHandler::canCopy,      &LLEditMenuHandler::copy },
        { "Paste",      &LLEditMenuHandler::canPaste,     &LLEditMenuHandler::paste },
        { "Delete",     &LLEditMenuHandler::canDoDelete,  &LLEditMenuHandler::doDelete },
        { "Select All", &LLEditMenuHandler::canSelectAll, &LLEditMenuHandler::selectAll },
    };

    // x,y are floater-local (the same space every other popped-out input
    // coordinate in this file already uses).
    void showPopoutEditMenu(PopoutInstance& inst, LLFloater* floater, S32 x, S32 y)
    {
        destroyPopoutContextMenu(inst);

        if (!floater || !LLEditMenuHandler::gEditMenuHandler)
        {
            return;
        }

        const S32 ITEM_W = 110;
        const S32 ITEM_H = 20;
        const S32 count = (S32)(sizeof(POPOUT_EDIT_MENU_ITEMS) / sizeof(POPOUT_EDIT_MENU_ITEMS[0]));
        const S32 menu_h = ITEM_H * count;

        // Opens down-and-right from the click point, like the real menu -
        // clamped so it never spills outside the floater's own rect
        // (there's nothing rendered beyond that edge to spill onto).
        const S32 floater_w = floater->getRect().getWidth();
        const S32 floater_h = floater->getRect().getHeight();
        const S32 left = llclamp(x, 0, llmax(0, floater_w - ITEM_W));
        const S32 top = (y >= menu_h) ? y : llmin(floater_h, y + menu_h);

        LLPanel::Params panel_p;
        panel_p.name("popout_context_menu");
        panel_p.rect(LLRect(left, top, left + ITEM_W, top - menu_h));
        panel_p.background_visible(true);
        panel_p.background_opaque(true);
        panel_p.bg_opaque_color(LLColor4(0.05f, 0.05f, 0.05f, 1.f));
        panel_p.has_border(true);
        inst.contextMenuPanel = LLUICtrlFactory::create<LLPanel>(panel_p);
        floater->addChild(inst.contextMenuPanel);

        for (S32 i = 0; i < count; ++i)
        {
            const PopoutEditMenuItem& item = POPOUT_EDIT_MENU_ITEMS[i];

            // Flat, text-only menu-item look - LLButton's SKIN DEFAULT
            // images (a distinct beveled/highlighted toolbar-button
            // graphic, applied to any field left unset here) is what a
            // plain, no-gap vertical stack of these actually looked like
            // live in phase 1: "highlighted buttons for available
            // options" rather than a menu list. Keeping the skin's own
            // default (valid, loadable) image pointers and making them
            // fully transparent via color - NOT nulling the pointers
            // themselves - avoids LLButton::draw()'s missing-texture
            // fallback (this renderer's magenta "invalid resource" tell,
            // confirmed live when the pointers were nulled instead) while
            // still hiding the beveled look. label_color_disabled dims the
            // text for a currently-unavailable action instead of hiding
            // it, matching the real menu's own on_enable graying.
            LLButton::Params btn_p;
            btn_p.name(item.label);
            btn_p.label(item.label);
            btn_p.font(LLFontDX::getFontSansSerifSmall());
            btn_p.rect(LLRect(0, menu_h - i * ITEM_H, ITEM_W, menu_h - (i + 1) * ITEM_H));
            // LLButton::handleMouseDown() calls setFocus(true) on itself
            // whenever hasTabStop() is true (llbutton.cpp) - stealing
            // keyboard focus away from whatever currently holds it (the
            // text editor this menu is for) on the SAME click that opens
            // the button. LLTextEditor::focusLostHelper() reacts to losing
            // focus by explicitly clearing LLEditMenuHandler::
            // gEditMenuHandler if it was the one holding it - so by the
            // time this button's OWN click callback runs (on mouse-up,
            // after the focus change already happened on mouse-down),
            // gEditMenuHandler is already null and every action below
            // would silently no-op. LLFloater::buildButtons() already
            // avoids this exact trap for its own title buttons the same
            // way.
            btn_p.tab_stop(false);
            btn_p.image_color(LLColor4::transparent);
            btn_p.image_color_disabled(LLColor4::transparent);
            btn_p.label_color(LLColor4::white);
            btn_p.label_color_disabled(LLColor4(0.5f, 0.5f, 0.5f, 1.f));
            LLButton* btn = LLUICtrlFactory::create<LLButton>(btn_p);
            btn->setEnabled((LLEditMenuHandler::gEditMenuHandler->*item.can)());

            bool (LLEditMenuHandler::*can)() const = item.can;
            void (LLEditMenuHandler::*act)() = item.act;
            PopoutInstance* inst_ptr = &inst;
            btn->setClickedCallback([can, act, inst_ptr](LLUICtrl*, const LLSD&)
            {
                if (LLEditMenuHandler::gEditMenuHandler && (LLEditMenuHandler::gEditMenuHandler->*can)())
                {
                    (LLEditMenuHandler::gEditMenuHandler->*act)();
                }
                // NOT destroyPopoutContextMenu() directly - this callback
                // is running from WITHIN LLButton::handleMouseUp() on THIS
                // SAME button, which is a child of inst_ptr->contextMenuPanel.
                // Deleting the panel here would delete the button object
                // out from under its own still-executing member function
                // (delete-this-on-the-call-stack) - undefined behaviour
                // that doesn't necessarily crash immediately (confirmed
                // live in phase 1 as the menu working exactly once and
                // then never again, consistent with a use-after-free
                // quietly corrupting heap/global state rather than
                // crashing outright). Just flag it; idle() performs the
                // real deletion once this whole dispatch has fully
                // unwound. inst_ptr is stable for this purpose - it's the
                // address of a PopoutInstance owned by a unique_ptr in
                // sInstances, which does not move even if the map itself
                // grows/shrinks.
                inst_ptr->contextMenuPendingDestroy = true;
            });
            inst.contextMenuPanel->addChild(btn);
        }
    }

    // Generic replacement for ANY real LLMenuGL that would otherwise
    // render in the wrong place when spawned from a popped-out floater -
    // e.g. right-clicking an avatar/object name inside chat history
    // spawns menu_avatar_icon.xml/menu_object_icon.xml via the same
    // LLMenuGL::sMenuContainer global as the text-editing case above, but
    // with a completely different, per-menu-type action set (Profile/IM/
    // Add Friend/Mute/etc via CommitCallbackRegistry "AvatarIcon.Action"/
    // "ObjectIcon.Action" - llchathistory.cpp), not LLEditMenuHandler's
    // cut/copy/paste. Hand-building a SEPARATE hardcoded replacement
    // panel per menu type wouldn't scale to whatever menu shows up next -
    // LLMenuGL::getItemCount()/getItem(S32) and LLMenuItemGL::getLabel()/
    // getEnabled()/getVisible()/onCommit() are all public (llmenugl.h),
    // so this instead walks whatever REAL menu just spawned and rebuilds
    // an equivalent using the exact same plain-LLPanel-plus-LLButtons
    // technique showPopoutEditMenu() above already uses, firing each
    // item's OWN already-registered onCommit() callback directly on
    // click - the exact same action a click on the real menu item would
    // have fired, no second copy of any menu's own logic needed. This
    // subsumes showPopoutEditMenu()'s own case too, since
    // LLTextEditor::showContextMenu() ALSO creates a real LLMenuGL - see
    // idle()'s RightDown case for why showPopoutEditMenu() is kept as a
    // fallback rather than removed outright.
    //
    // Raw LLMenuItemGL* captured by value in each button's click callback
    // below is safe: menus reached this way are cached/reused by their
    // owner (e.g. LLChatHistoryHeader's mPopupMenuHandleAvatar/Object)
    // across repeated right-clicks, and hideMenus() (called by the
    // caller right after this) only hides, never destroys them.
    void showPopoutMenuFromLLMenuGL(PopoutInstance& inst, LLFloater* floater, LLMenuGL* menu, S32 x, S32 y)
    {
        destroyPopoutContextMenu(inst);

        if (!floater || !menu)
        {
            return;
        }

        // LLMenuItemSeparatorGL skipped outright - a real separator has
        // no label/action of its own, and this rebuild has no divider-
        // line rendering to give it anyway; including it as an empty
        // full-height row was the single biggest contributor to
        // menu_participant_view.xml's substitute (3 separators) looking
        // noticeably taller/less compact than the real menu.
        std::vector<LLMenuItemGL*> visible_items;
        const U32 raw_count = menu->getItemCount();
        visible_items.reserve(raw_count);
        for (U32 i = 0; i < raw_count; ++i)
        {
            LLMenuItemGL* item = menu->getItem((S32)i);
            if (item && item->getVisible() && !dynamic_cast<LLMenuItemSeparatorGL*>(item))
            {
                visible_items.push_back(item);
            }
        }

        if (visible_items.empty())
        {
            return;
        }

        const S32 LABEL_PAD = 16;
        const S32 MIN_ITEM_W = 60;
        const S32 MAX_ITEM_W = 260;
        const S32 MIN_ITEM_H = 16;
        LLFontDX* font = LLFontDX::getFontSansSerifSmall();

        // Width/height pulled from the REAL menu item's own already-
        // arranged rect/getNominalHeight() (both public, llmenugl.h) -
        // menu->arrangeAndClear() (LLMenuButton::toggleMenu(), or
        // LLMenuGL::showPopup() for a right-click menu) already laid this
        // item out at its real, compact size before this ever runs;
        // trusting that instead of a guessed font-width+padding constant
        // is what was making every one of these substitutes look
        // noticeably more "expanded" than the original menu it's
        // replacing. checkable is true if ANY item is an
        // LLMenuItemCheckGL - such a menu reserves a little extra left
        // margin for the "*" checked-marker below, common to the whole
        // menu rather than per-row, so checked and unchecked rows still
        // line up with each other.
        S32 item_w = MIN_ITEM_W;
        S32 item_h = MIN_ITEM_H;
        bool checkable = false;
        for (LLMenuItemGL* item : visible_items)
        {
            item_w = llmax(item_w, item->getRect().getWidth());
            item_h = llmax(item_h, (S32)item->getNominalHeight());
            if (dynamic_cast<LLMenuItemCheckGL*>(item))
            {
                checkable = true;
            }
        }
        if (checkable)
        {
            item_w += LABEL_PAD;
        }
        item_w = llclamp(item_w, MIN_ITEM_W, MAX_ITEM_W);

        const S32 count = (S32)visible_items.size();
        const S32 menu_h = item_h * count;

        const S32 floater_w = floater->getRect().getWidth();
        const S32 floater_h = floater->getRect().getHeight();
        const S32 left = llclamp(x, 0, llmax(0, floater_w - item_w));
        const S32 top = (y >= menu_h) ? y : llmin(floater_h, y + menu_h);

        LLPanel::Params panel_p;
        panel_p.name("popout_context_menu");
        panel_p.rect(LLRect(left, top, left + item_w, top - menu_h));
        panel_p.background_visible(true);
        panel_p.background_opaque(true);
        panel_p.bg_opaque_color(LLColor4(0.05f, 0.05f, 0.05f, 1.f));
        panel_p.has_border(true);
        inst.contextMenuPanel = LLUICtrlFactory::create<LLPanel>(panel_p);
        floater->addChild(inst.contextMenuPanel);

        for (S32 i = 0; i < count; ++i)
        {
            LLMenuItemGL* item = visible_items[i];
            std::string label = item->getLabel();

            // LLMenuItemCheckGL::getValue() (public) re-runs the item's
            // own on_check callback right here, same as the real menu
            // does right before drawing its own checkmark glyph - so this
            // always reflects whichever sort/toggle mode is ACTUALLY
            // active, not a snapshot taken once when the menu first
            // opened. Plain "*" prefix, not a glyph - matches this
            // rebuild's existing plain-text style (see
            // showPopoutEditMenu()'s own comment on why no font-shadow/
            // image tricks are used here) and needs no font-coverage
            // assumption beyond plain ASCII.
            LLMenuItemCheckGL* check_item = dynamic_cast<LLMenuItemCheckGL*>(item);
            if (check_item)
            {
                label = (check_item->getValue().asBoolean() ? "* " : "   ") + label;
            }

            // See showPopoutEditMenu()'s own comment on tab_stop(false)/
            // image_color(transparent) - identical reasoning applies here.
            LLButton::Params btn_p;
            btn_p.name(item->getLabel());
            btn_p.label(label);
            btn_p.font(font);
            btn_p.rect(LLRect(0, menu_h - i * item_h, item_w, menu_h - (i + 1) * item_h));
            btn_p.tab_stop(false);
            btn_p.image_color(LLColor4::transparent);
            btn_p.image_color_disabled(LLColor4::transparent);
            btn_p.label_color(LLColor4::white);
            btn_p.label_color_disabled(LLColor4(0.5f, 0.5f, 0.5f, 1.f));
            btn_p.font_halign(LLFontDX::LEFT);
            LLButton* btn = LLUICtrlFactory::create<LLButton>(btn_p);
            btn->setEnabled(item->getEnabled());

            PopoutInstance* inst_ptr = &inst;
            btn->setClickedCallback([item, inst_ptr](LLUICtrl*, const LLSD&)
            {
                if (item->getEnabled())
                {
                    item->onCommit();
                }
                // See showPopoutEditMenu()'s own click-callback comment
                // for why the panel's destruction must be deferred to
                // idle(), never done directly from here.
                inst_ptr->contextMenuPendingDestroy = true;
            });
            inst.contextMenuPanel->addChild(btn);
        }
    }

    void destroyPopoutGestureHelper(PopoutInstance& inst)
    {
        if (inst.gestureHelperPanel)
        {
            delete inst.gestureHelperPanel; // LLView's dtor detaches from its parent.
            inst.gestureHelperPanel = nullptr;
        }
        inst.gestureHelperPendingDestroy = false;
    }

    // Reverse of floaterPointToViewLocal() (below) - walks the SAME
    // ancestor chain but ADDS each ancestor's own rect offset instead of
    // subtracting, since this converts a point already local to `view`
    // INTO floater-local space (the opposite direction: view-local plus
    // the accumulated offset equals floater-local, per that function's
    // own `out = floater_local - offset` derivation).
    bool viewPointToFloaterLocal(LLFloater* floater, LLView* view, S32 x, S32 y, S32* out_x, S32* out_y)
    {
        S32 offset_x = 0, offset_y = 0;
        LLView* v = view;
        for (int guard = 0; v && v != floater && guard < 64; ++guard, v = v->getParent())
        {
            const LLRect& r = v->getRect();
            offset_x += r.mLeft;
            offset_y += r.mBottom;
        }
        if (v != floater)
        {
            return false;
        }
        *out_x = x + offset_x;
        *out_y = y + offset_y;
        return true;
    }

    // LLGestureAutocompleteHelper::showHelper() (llgestureautocompletehelper.cpp)
    // is the mechanism behind "the menus... still spawning in main view":
    // its real picker (LLFloaterGestureAutocompletePicker, registered as
    // "gesture_autocomplete_picker" - see [[project_gesture_autocomplete_
    // missing_xui_crash_2026_09_26]] for the crash that was blocking this
    // path entirely until now) is a genuine top-level LLFloater, opened via
    // openFloater() straight into gFloaterView - the MAIN window's own
    // floater layer, entirely separate from this framework's per-instance
    // composited render target. Repositioning it can't fix this: a
    // gFloaterView floater can only ever draw within the MAIN window's own
    // client area, which has nothing to do with wherever this instance's
    // own host window actually sits on screen (possibly a different
    // monitor entirely). Fixed the same way as any other real popup this
    // framework can't host directly (showPopoutMenuFromLLMenuGL/
    // showPopoutEditMenu above): suppress the real floater every time it
    // would show, and rebuild an equivalent as a genuine CHILD of the
    // popped-out floater instead, driven entirely by
    // LLGestureAutocompleteHelper's own public isActive()/rows()/total()/
    // onCommitGesture() - no second copy of the trigger-matching logic
    // needed, just its rendering. The emoji-helper (colon-triggered,
    // llemojihelper.cpp) and the full emoji-picker/@mention-picker floaters
    // share this EXACT same structural gap (all three are real top-level
    // LLFloaters opened the same way) but are NOT covered by this function -
    // each has its own, much more complex widget (an emoji grid, an avatar
    // list) that a generic item-walk can't rebuild the way
    // showPopoutMenuFromLLMenuGL walks a plain LLMenuGL - left as a known,
    // separately-scoped follow-up rather than guessed at here.
    //
    // Called once per keystroke dispatched to a popped-out floater's own
    // focused control (idle()'s key-event loop, both KeyDown and Char) -
    // isActive()/rows() are recomputed by nearby chat on every keystroke
    // regardless, so re-deriving this every time matches that same cadence
    // exactly (see LLFloaterIMNearbyChat::onChatBoxKeystroke()).
    void updatePopoutGestureHelper(PopoutInstance& inst, LLFloater* floater, LLUICtrl* target_ctrl)
    {
        if (!target_ctrl || !LLGestureAutocompleteHelper::instance().isActive(target_ctrl))
        {
            destroyPopoutGestureHelper(inst);
            return;
        }

        // refreshPicker() (llgestureautocompletehelper.cpp) calls the real
        // floater's onOpen()/openFloater() again on EVERY keystroke while
        // active, not just once - suppressed every time for the same
        // reason, not just on first activation.
        if (LLFloater* real_floater = LLFloaterReg::findInstance("gesture_autocomplete_picker"))
        {
            real_floater->setVisible(false);
        }

        // Full rebuild every keystroke, matching the real floater's own
        // onOpen(), which does the same (clearRows() then re-adds).
        destroyPopoutGestureHelper(inst);

        const std::vector<LLGestureAutocompleteHelper::Row>& rows = LLGestureAutocompleteHelper::instance().rows();
        if (rows.empty())
        {
            return;
        }

        const S32 ITEM_H = 20;
        const S32 LABEL_PAD = 24;
        const S32 MIN_ITEM_W = 160;
        const S32 MAX_ITEM_W = 280;
        const size_t MAX_SHOWN_ROWS = 8;
        LLFontDX* font = LLFontDX::getFontSansSerifSmall();

        const size_t shown = llmin(rows.size(), MAX_SHOWN_ROWS);
        const size_t total = LLGestureAutocompleteHelper::instance().total();
        const bool truncated = total > shown;

        S32 item_w = MIN_ITEM_W;
        for (size_t i = 0; i < shown; ++i)
        {
            item_w = llmax(item_w, font->getWidth(rows[i].trigger + "  " + rows[i].name) + LABEL_PAD);
        }
        item_w = llmin(item_w, MAX_ITEM_W);

        const S32 row_count = (S32)shown + (truncated ? 1 : 0);
        const S32 menu_h = ITEM_H * row_count;

        // Anchors below the input control's own top-left corner, in
        // floater-local space - the same anchor point LLGestureAutocompleteHelper::
        // showHelper() itself computes (host_ctrl's own rect top-left via
        // localPointToOtherView()), just converted into THIS floater's own
        // local space instead of gFloaterView's.
        S32 anchor_x = 0, anchor_y = 0;
        if (!viewPointToFloaterLocal(floater, target_ctrl, 0, target_ctrl->getRect().getHeight(), &anchor_x, &anchor_y))
        {
            anchor_x = 0;
            anchor_y = floater->getRect().getHeight();
        }

        const S32 floater_w = floater->getRect().getWidth();
        const S32 floater_h = floater->getRect().getHeight();
        const S32 left = llclamp(anchor_x, 0, llmax(0, floater_w - item_w));
        const S32 top = llmin(anchor_y + menu_h, floater_h);

        LLPanel::Params panel_p;
        panel_p.name("popout_gesture_helper");
        panel_p.rect(LLRect(left, top, left + item_w, top - menu_h));
        panel_p.background_visible(true);
        panel_p.background_opaque(true);
        panel_p.bg_opaque_color(LLColor4(0.05f, 0.05f, 0.05f, 1.f));
        panel_p.has_border(true);
        inst.gestureHelperPanel = LLUICtrlFactory::create<LLPanel>(panel_p);
        floater->addChild(inst.gestureHelperPanel);

        for (size_t i = 0; i < shown; ++i)
        {
            const std::string label = rows[i].trigger + "  " + rows[i].name;
            const std::string value = rows[i].value;

            LLButton::Params btn_p;
            btn_p.name(rows[i].trigger);
            btn_p.label(label);
            btn_p.font(font);
            btn_p.rect(LLRect(0, menu_h - (S32)i * ITEM_H, item_w, menu_h - ((S32)i + 1) * ITEM_H));
            // See showPopoutEditMenu()'s own comment on tab_stop(false) -
            // identical reasoning (this must not steal focus from the
            // chat input on mouse-down, or gEditMenuHandler/this whole
            // helper's own host-ctrl tracking would be disturbed by the
            // click that's meant to just commit a gesture).
            btn_p.tab_stop(false);
            btn_p.image_color(LLColor4::transparent);
            btn_p.image_color_disabled(LLColor4::transparent);
            btn_p.label_color(LLColor4::white);
            LLButton* btn = LLUICtrlFactory::create<LLButton>(btn_p);

            PopoutInstance* inst_ptr = &inst;
            btn->setClickedCallback([value, inst_ptr](LLUICtrl*, const LLSD&)
            {
                LLGestureAutocompleteHelper::instance().onCommitGesture(value);
                // See showPopoutEditMenu()'s own click-callback comment
                // for why the panel's destruction must be deferred to
                // idle(), never done directly from here.
                inst_ptr->gestureHelperPendingDestroy = true;
            });
            inst.gestureHelperPanel->addChild(btn);
        }

        if (truncated)
        {
            LLTextBox::Params txt_p;
            txt_p.name("popout_gesture_helper_more");
            txt_p.rect(LLRect(0, ITEM_H, item_w, 0));
            txt_p.font(font);
            txt_p.text_color(LLColor4(0.6f, 0.6f, 0.6f, 1.f));
            txt_p.initial_value(llformat("...and %d more", (int)(total - shown)));
            LLTextBox* txt = LLUICtrlFactory::create<LLTextBox>(txt_p);
            inst.gestureHelperPanel->addChild(txt);
        }
    }

    // Width, in logical pixels, reserved on the right end of the header
    // strip for title-bar buttons - approximate, not pixel-perfect against
    // their real rects (which live behind a protected accessor). Sized
    // generously for the minimized state specifically: LLFloater::
    // handleMouseDown() offers a minimized floater's click to close,
    // restore, tear-off AND dock buttons in turn (llfloater.cpp) - up to 4
    // buttons side by side, more than the ~2 shown docked/expanded.
    const S32 HEADER_BUTTON_ZONE_WIDTH = 120;

    // Shared by both the mouse-capture reachability check (idle(), mouse
    // dispatch) and keyboard-focus reachability below - gFocusMgr's
    // keyboard focus, like its mouse capture, is a single global shared
    // with the ENTIRE main window, not scoped to any one floater, so it
    // can legitimately hold something unrelated that later dies through
    // ordinary main-window activity. See floaterPointToViewLocal()'s
    // comment for the mouse-capture case this was first confirmed on (a
    // live CTD in phase 1) - the same risk applies to keyboard focus.
    bool isDescendantOfFloater(LLFloater* floater, LLView* view)
    {
        LLView* v = view;
        for (int guard = 0; v && v != floater && guard < 64; ++guard, v = v->getParent())
        {
        }
        return v == floater;
    }

    // Win32 client coords are top-down (y=0 at top); LLView-space is
    // bottom-up ortho (dx_state_for_2d(), same convention LLWindowWin32's
    // own convertCoords(LLCoordWindow, LLCoordGL*) uses) - matches that
    // exact -1 pixel-center convention.
    inline S32 flipY(S32 win32_y, S32 client_height)
    {
        return client_height - win32_y - 1;
    }

    // Shared by WM_LBUTTONDOWN's own drag-start detection AND WM_SETCURSOR's
    // hover-cursor feedback below - factored out so both always agree on
    // exactly where the resize border is, rather than keeping two copies
    // of the same six-way edge/corner check in sync by hand. x/raw_y are
    // both in the SAME raw, top-down Win32 client-coordinate convention
    // WM_LBUTTONDOWN's own lParam already uses (no flipY() needed here -
    // ScreenToClient() gives that same convention directly for the
    // WM_SETCURSOR caller). Returns 0 (no edge) if the floater isn't
    // actually resizable - "sl_about" isn't, see WM_LBUTTONDOWN's own
    // comment on why that gate exists.
    UINT hitTestResizeEdge(bool resizable, S32 x, S32 raw_y, S32 client_w, S32 client_h)
    {
        if (!resizable)
        {
            return 0;
        }
        const S32 RESIZE_BORDER = 6;
        const bool at_left = x < RESIZE_BORDER;
        const bool at_right = x >= client_w - RESIZE_BORDER;
        const bool at_top_edge = raw_y < RESIZE_BORDER;
        const bool at_bottom_edge = raw_y >= client_h - RESIZE_BORDER;

        if (at_top_edge && at_left) return HTTOPLEFT;
        if (at_top_edge && at_right) return HTTOPRIGHT;
        if (at_bottom_edge && at_left) return HTBOTTOMLEFT;
        if (at_bottom_edge && at_right) return HTBOTTOMRIGHT;
        if (at_top_edge) return HTTOP;
        if (at_bottom_edge) return HTBOTTOM;
        if (at_left) return HTLEFT;
        if (at_right) return HTRIGHT;
        return 0;
    }

    LRESULT CALLBACK hostWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        // Recovered instance pointer - set once, right after
        // CreateWindowExW, in hostWindowThreadMain() below, before this
        // window is ever shown or receives any message this switch cares
        // about. Absent only for the handful of messages Windows can
        // deliver synchronously during CreateWindowExW itself (WM_CREATE/
        // WM_NCCREATE), neither of which this switch handles anyway.
        PopoutInstance* inst = reinterpret_cast<PopoutInstance*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (!inst)
        {
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        switch (msg)
        {
        case WM_DESTROY:
            inst->hostWindowDestroyedExternally = true;
            PostQuitMessage(0);
            return 0;

        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_MOUSEMOVE:
        {
            RECT r;
            GetClientRect(hwnd, &r);
            const S32 client_w = r.right - r.left;
            const S32 client_h = r.bottom - r.top;
            const S32 x = GET_X_LPARAM(lParam);
            const S32 raw_y = GET_Y_LPARAM(lParam);
            const S32 fy = flipY(raw_y, client_h);

            // Continue an in-progress non-blocking drag/resize (see
            // DragMode's comment) - takes priority over everything below,
            // including the normal LLView dispatch queue.
            if (inst->dragMode != DragMode::None)
            {
                if (msg == WM_LBUTTONUP)
                {
                    if (inst->dragMode == DragMode::Resizing)
                    {
                        RECT cr;
                        GetClientRect(hwnd, &cr);
                        inst->resizedWidth = cr.right - cr.left;
                        inst->resizedHeight = cr.bottom - cr.top;
                        inst->resizeJustCompleted = true;
                    }
                    inst->dragMode = DragMode::None;
                    ReleaseCapture();
                }
                else if (msg == WM_MOUSEMOVE)
                {
                    POINT cur;
                    GetCursorPos(&cur);
                    const S32 dx = cur.x - inst->dragStartCursorScreen.x;
                    const S32 dy = cur.y - inst->dragStartCursorScreen.y;

                    if (inst->dragMode == DragMode::Moving)
                    {
                        SetWindowPos(hwnd, NULL, inst->dragStartWindowRect.left + dx, inst->dragStartWindowRect.top + dy,
                            0, 0, SWP_NOSIZE | SWP_NOZORDER);
                    }
                    else // Resizing
                    {
                        RECT nr = inst->dragStartWindowRect;
                        const UINT edge = inst->resizeEdge;
                        if (edge == HTLEFT || edge == HTTOPLEFT || edge == HTBOTTOMLEFT) nr.left += dx;
                        if (edge == HTRIGHT || edge == HTTOPRIGHT || edge == HTBOTTOMRIGHT) nr.right += dx;
                        if (edge == HTTOP || edge == HTTOPLEFT || edge == HTTOPRIGHT) nr.top += dy;
                        if (edge == HTBOTTOM || edge == HTBOTTOMLEFT || edge == HTBOTTOMRIGHT) nr.bottom += dy;

                        // Never let the rect invert - clamp to a floor size.
                        const int MIN_SIZE = 50;
                        if (nr.right - nr.left < MIN_SIZE)
                        {
                            if (edge == HTLEFT || edge == HTTOPLEFT || edge == HTBOTTOMLEFT) nr.left = nr.right - MIN_SIZE;
                            else nr.right = nr.left + MIN_SIZE;
                        }
                        if (nr.bottom - nr.top < MIN_SIZE)
                        {
                            if (edge == HTTOP || edge == HTTOPLEFT || edge == HTTOPRIGHT) nr.top = nr.bottom - MIN_SIZE;
                            else nr.bottom = nr.top + MIN_SIZE;
                        }

                        SetWindowPos(hwnd, NULL, nr.left, nr.top, nr.right - nr.left, nr.bottom - nr.top, SWP_NOZORDER);
                    }
                }
                return 0;
            }

            // Resize-by-edge / drag-by-titlebar: start a non-blocking
            // gesture (DragMode) instead of using the floater's own
            // LLResizeHandle/LLResizeBar/LLDragHandle OR the OS's modal
            // WM_NCLBUTTONDOWN loop. Those LL widgets compute deltas via
            // localPointToScreen(), which walks the REAL parent chain
            // (floater -> gFloaterView -> mRootView) using the floater's
            // stale DOCKED position - a coordinate frame with nothing to
            // do with where this popped-out window actually sits on
            // screen - confirmed live in phase 1 as "mouse location is all
            // over the place" for dragging; the identical bug is latent in
            // resize. The OS's own modal move/resize loop was the
            // ORIGINAL fix for that, but it's a BLOCKING call that never
            // returns until the gesture ends - confirmed live, repeatedly,
            // as this thread going unresponsive. This is entirely self-
            // contained instead: a plain SetCapture()+GetCursorPos()+
            // SetWindowPos() loop, the same non-blocking approach already
            // proven reliable for ordinary mouse dispatch below.
            //
            // Resize border is checked first so a corner grab (which can
            // overlap the header strip) wins over a plain drag. The drag
            // zone is capped at half the window's current width -
            // UIMinimizedWidth defaults to 160px, narrower than
            // HEADER_BUTTON_ZONE_WIDTH (120px) alone, which left only a
            // ~40px sliver draggable on a minimized floater (most clicks
            // missed it entirely) - confirmed live as "unmovable [while
            // minimized]". Real button rects (behind a protected accessor)
            // are much narrower than half the header even at this width.
            if (msg == WM_LBUTTONDOWN)
            {
                // Gated on the floater's ACTUAL resizability - "sl_about"
                // isn't resizable, and this was a real, confirmed bug:
                // dragging an edge visibly resized the window regardless,
                // with nothing behind it. hitTestResizeEdge() applies the
                // same gate internally (see its own comment).
                UINT ht = hitTestResizeEdge(inst->floaterResizableCache, x, raw_y, client_w, client_h);

                const S32 button_zone = llmin(HEADER_BUTTON_ZONE_WIDTH, client_w / 2);
                const bool in_drag_zone = fy >= client_h - inst->headerHeightCache && x < client_w - button_zone;

                if (ht != 0 || in_drag_zone)
                {
                    SetCapture(hwnd);
                    GetCursorPos(&inst->dragStartCursorScreen);
                    GetWindowRect(hwnd, &inst->dragStartWindowRect);
                    inst->dragMode = (ht != 0) ? DragMode::Resizing : DragMode::Moving;
                    inst->resizeEdge = ht;
                    return 0;
                }
            }

            // OS-level mouse capture for the drag gesture itself, mirroring
            // what LLViewerWindow::handleAnyMouseClick() does for the main
            // window - without it, Windows simply stops delivering mouse
            // messages to this window the moment the cursor leaves its
            // client rect, which a fast or wide drag (e.g. selecting a
            // line of text) does routinely. SetCapture() here is unrelated
            // to gFocusMgr's own (LLView-level) capture concept below.
            if (msg == WM_LBUTTONDOWN)
            {
                SetCapture(hwnd);
            }
            else if (msg == WM_LBUTTONUP)
            {
                ReleaseCapture();
            }

            PopoutMouseEvent ev;
            ev.type = (msg == WM_LBUTTONDOWN) ? PopoutMouseEvent::Down
                     : (msg == WM_LBUTTONUP) ? PopoutMouseEvent::Up
                     : PopoutMouseEvent::Move;
            ev.x = x;
            ev.y = fy;
            // tryPush(), NOT pushFront()/push() - LLThreadSafeQueue defaults
            // to a BOUNDED capacity (1024) and push() BLOCKS the caller once
            // full. This runs on this instance's own host thread, inside
            // its WNDPROC - if the consumer (idle(), main thread) ever
            // falls behind for any sustained reason, a blocking push() here
            // freezes this thread's message loop forever - Windows then
            // shows the window as "Not Responding". Dropping an occasional
            // mouse-move under real backpressure is harmless; blocking this
            // thread is not.
            inst->inputQueue.tryPush(ev);
            return 0;
        }

        // This window has no real non-client area at all (WS_POPUP, and
        // WM_NCHITTEST is never handled - see WM_LBUTTONDOWN's own
        // comment on why the OS's own modal NC resize loop was rejected
        // in favor of a self-contained drag), so Windows has no idea it
        // has "resizable edges" - WM_SETCURSOR's own default handling
        // just resets to the class cursor (IDC_ARROW, set once at
        // RegisterClassExW time) even directly over the resize border -
        // confirmed live as "the resize needs a visual indicator... thats
        // not the case in a popped out floater". Computed with the exact
        // same hitTestResizeEdge() WM_LBUTTONDOWN's own drag-start check
        // uses, so the cursor and the actual click behavior always agree
        // on where the border is. lParam's own hit-test word is useless
        // here (always HTCLIENT, since nothing ever answers WM_NCHITTEST
        // differently) - GetCursorPos()+ScreenToClient() instead.  While
        // a resize gesture is already in progress, inst->resizeEdge (the
        // edge that STARTED the gesture) is authoritative instead of
        // re-hit-testing the current position - a fast/wide drag
        // routinely puts the cursor outside the border mid-gesture, but
        // the gesture itself hasn't changed which edge it's resizing, and
        // the cursor shouldn't flicker back to an arrow because of it.
        case WM_SETCURSOR:
        {
            UINT edge = 0;
            if (inst->dragMode == DragMode::Resizing)
            {
                edge = inst->resizeEdge;
            }
            else if (inst->dragMode == DragMode::None)
            {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                RECT r;
                GetClientRect(hwnd, &r);
                edge = hitTestResizeEdge(inst->floaterResizableCache, pt.x, pt.y, r.right - r.left, r.bottom - r.top);
            }

            if (edge != 0)
            {
                LPCWSTR cursor_id = IDC_ARROW;
                switch (edge)
                {
                case HTLEFT: case HTRIGHT: cursor_id = IDC_SIZEWE; break;
                case HTTOP: case HTBOTTOM: cursor_id = IDC_SIZENS; break;
                case HTTOPLEFT: case HTBOTTOMRIGHT: cursor_id = IDC_SIZENWSE; break;
                case HTTOPRIGHT: case HTBOTTOMLEFT: cursor_id = IDC_SIZENESW; break;
                default: break;
                }
                SetCursor(LoadCursor(NULL, cursor_id));
                return TRUE;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        case WM_MOUSEWHEEL:
        {
            RECT r;
            GetClientRect(hwnd, &r);
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);

            PopoutMouseEvent ev;
            ev.type = PopoutMouseEvent::Wheel;
            ev.x = pt.x;
            ev.y = flipY(pt.y, r.bottom - r.top);
            ev.clicks = -(GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA);
            inst->inputQueue.tryPush(ev);
            return 0;
        }

        // Modifier mask computed via GetKeyState() - a stateless OS query,
        // safe from any thread - rather than gKeyboard->currentMask() (a
        // main-thread-owned object). Actual KEY translation is deferred to
        // the main thread (idle()) for the same reason: LLKeyboard::
        // translateKey() isn't safe to call from here.
        case WM_KEYDOWN:
        case WM_KEYUP:
        {
            MASK mask = MASK_NONE;
            if (GetKeyState(VK_SHIFT) & 0x8000) mask |= MASK_SHIFT;
            if (GetKeyState(VK_CONTROL) & 0x8000) mask |= MASK_CONTROL;
            if (GetKeyState(VK_MENU) & 0x8000) mask |= MASK_ALT;

            PopoutKeyEvent ev;
            ev.type = (msg == WM_KEYDOWN) ? PopoutKeyEvent::KeyDown : PopoutKeyEvent::KeyUp;
            ev.vk = (WORD)wParam;
            ev.mask = mask;
            inst->keyQueue.tryPush(ev);
            return 0;
        }

        case WM_CHAR:
        {
            PopoutKeyEvent ev;
            ev.type = PopoutKeyEvent::Char;
            ev.uni_char = (llwchar)wParam;
            inst->keyQueue.tryPush(ev);
            return 0;
        }

        // Routed through the same queue as left-click, same OS-level
        // SetCapture discipline, so it never falls to DefWindowProcW's
        // default WM_RBUTTONUP handling (which generates a WM_CONTEXTMENU
        // notification this window never processes, but still disturbs
        // z-order/redraw state around whatever's under the cursor).
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        {
            RECT r;
            GetClientRect(hwnd, &r);
            const S32 client_h = r.bottom - r.top;

            if (msg == WM_RBUTTONDOWN)
            {
                SetCapture(hwnd);
            }
            else
            {
                ReleaseCapture();
            }

            PopoutMouseEvent ev;
            ev.type = (msg == WM_RBUTTONDOWN) ? PopoutMouseEvent::RightDown : PopoutMouseEvent::RightUp;
            ev.x = GET_X_LPARAM(lParam);
            ev.y = flipY(GET_Y_LPARAM(lParam), client_h);
            inst->inputQueue.tryPush(ev);
            return 0;
        }

        case WM_POPOUT_RESIZE:
            SetWindowPos(hwnd, NULL, 0, 0, (int)wParam, (int)lParam, SWP_NOMOVE | SWP_NOZORDER);
            return 0;

        case WM_POPOUT_SHOW:
            // Show-only now - popIn()/onFloaterClosed() post WM_CLOSE and
            // let the whole instance be torn down and recreated fresh next
            // time (see popIn()'s comment), never hide-and-reuse.
            ShowWindow(hwnd, SW_SHOW);
            // SetForegroundWindow() alone is subject to Windows' anti-
            // focus-stealing throttling once a few calls have happened
            // without a very recent, DIRECTLY-associated user input event -
            // by the time this posted message reaches the host thread,
            // that association is often already lost. The SetWindowPos
            // TOPMOST/NOTOPMOST toggle is a pure Z-ORDER change, not
            // subject to that same throttling, and reliably brings the
            // window visually to front regardless. SetForegroundWindow()
            // is kept as a best-effort add-on for when the OS does allow
            // it to also grant real input focus.
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            SetForegroundWindow(hwnd);
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }

    // Runs entirely on its own dedicated thread, mirroring
    // LLSplashScreenWin32::showImpl()'s pattern - the only D3D11-adjacent
    // work this thread ever does is CreateWindowEx() itself; every D3D11/
    // DirectComposition call happens on the main render thread via this
    // instance's own compositionSurface, called from renderPoppedOut().
    // Input messages this WNDPROC receives are queued, never handled
    // synchronously here. WS_EX_NOREDIRECTIONBITMAP - DWM never allocates
    // a GDI redirection surface for this window; DirectComposition
    // supplies all its pixels.
    void hostWindowThreadMain(PopoutInstance* inst, U32 width, U32 height, std::promise<HWND>* ready)
    {
        HINSTANCE hinst = GetModuleHandle(NULL);

        static bool class_registered = false;
        if (!class_registered)
        {
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(wc);
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = hostWindowProc;
            wc.hInstance = hinst;
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            wc.lpszClassName = HOST_WINDOW_CLASS;
            RegisterClassExW(&wc);
            class_registered = true;
        }

        HWND hwnd = CreateWindowExW(
            WS_EX_NOREDIRECTIONBITMAP,
            HOST_WINDOW_CLASS,
            L"S24 Floater Popout",
            WS_POPUP,
            100, 100, (int)width, (int)height,
            NULL, NULL, hinst, NULL);

        // Set BEFORE signaling ready - the main thread only starts posting
        // messages to hwnd once ready->set_value() unblocks it, so this
        // ordering guarantees hostWindowProc() never sees a message it
        // cares about (WM_CREATE/WM_NCCREATE aside, neither handled by its
        // switch) before GWLP_USERDATA is populated.
        if (hwnd)
        {
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)inst);
        }

        ready->set_value(hwnd);

        if (!hwnd)
        {
            return;
        }

        MSG msg;
        BOOL got_msg;
        while ((got_msg = GetMessage(&msg, NULL, 0, 0)) != 0)
        {
            if (got_msg == -1) break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    // Converts a point already in floater-local space (the host window's
    // own client coordinates, per this framework's own convention) to a
    // coordinate local to `view`, which must be `floater` or one of its
    // descendants. Deliberately NOT LLView::screenPointToLocal() - that
    // walks all the way to the true root view, which would incorrectly
    // fold in the floater's own (possibly nonzero, once dragged) position
    // within gFloaterView. Needed for mouse-capture dispatch below: a
    // widget like LLDragHandle/a slider calls gFocusMgr.setMouseCapture(this)
    // and then expects follow-up events in ITS OWN local space, not the
    // floater's.
    //
    // Returns false (out_x/out_y untouched) if `view` never reaches
    // `floater` while walking up its ancestors - gFocusMgr is a single
    // global shared with the ENTIRE main window, not scoped to any one
    // floater, so its captor can legitimately be some unrelated main-
    // window widget that later gets destroyed through ordinary main-window
    // activity having nothing to do with any popout. Forwarding events to
    // it regardless was a real, live CTD in phase 1 - the caller must fall
    // back to `floater` when this returns false. The guard counter bounds
    // the walk in case of a corrupted/cyclic parent chain.
    bool floaterPointToViewLocal(LLFloater* floater, LLView* view, S32 x, S32 y, S32* out_x, S32* out_y)
    {
        S32 offset_x = 0, offset_y = 0;
        LLView* v = view;
        for (int guard = 0; v && v != floater && guard < 64; ++guard, v = v->getParent())
        {
            const LLRect& r = v->getRect();
            offset_x += r.mLeft;
            offset_y += r.mBottom;
        }
        if (v != floater)
        {
            return false;
        }
        *out_x = x - offset_x;
        *out_y = y - offset_y;
        return true;
    }

    // Main render thread only. Lazily creates the host window + composition
    // surface on first call, resizes the host window (cross-thread
    // SetWindowPos is safe, unlike DestroyWindow) if the floater's size
    // changes on later calls.
    bool ensureHostWindow(PopoutInstance& inst, U32 width, U32 height)
    {
        if (!inst.hostWindow)
        {
            std::promise<HWND> ready;
            auto ready_future = ready.get_future();
            inst.hostThread = std::thread(hostWindowThreadMain, &inst, width, height, &ready);
            inst.hostWindow = ready_future.get();

            if (!inst.hostWindow)
            {
                return false;
            }

            return inst.compositionSurface.createForWindow(inst.hostWindow, width, height);
        }

        // Skipped entirely while the user is actively dragging a resize
        // gesture (dragMode, host-thread-owned, read here as a plain
        // aligned enum/int - same informal-but-already-established cross-
        // thread safety as headerHeightCache elsewhere in this file: at
        // worst a single frame stale, never a correctness problem).
        // Without this, THIS call (every frame, from renderPoppedOut(),
        // using the floater's own rect - which only updates once the
        // gesture ENDS, see resizeJustCompleted) kept posting
        // WM_POPOUT_RESIZE to snap the window back to its OLD size every
        // single frame, fighting the SetWindowPos() calls the drag
        // handler in hostWindowProc() is ALSO issuing every frame to grow
        // it - two opposing resize forces racing each other, confirmed
        // live as "a terrible flickering mess" during a resize drag. The
        // real window's current size is authoritative while a gesture is
        // in progress; the floater's own (still-stale) size resumes being
        // authoritative the moment it ends and reshape() catches up.
        if (inst.dragMode == DragMode::None)
        {
            // GetClientRect is a safe cross-thread read (no synchronous
            // dispatch to the owning thread); the actual resize is posted
            // to the host thread instead of calling SetWindowPos here
            // directly - see WM_POPOUT_RESIZE's comment.
            RECT r;
            if (GetClientRect(inst.hostWindow, &r) &&
                ((UINT)(r.right - r.left) != width || (UINT)(r.bottom - r.top) != height))
            {
                PostMessage(inst.hostWindow, WM_POPOUT_RESIZE, (WPARAM)width, (LPARAM)height);
            }
        }

        return inst.compositionSurface.isValid();
    }

    // Fully tears an instance down (join its host thread, destroy its
    // composition surface, drop any leftover context menu/drag state,
    // restore the floater's visibility) and erases it from sInstances -
    // the one teardown path for every pop-in/close/external-destroy case,
    // called only once idle() has confirmed the host window has actually
    // finished being destroyed (hostWindowDestroyedExternally).
    std::map<std::string, std::unique_ptr<PopoutInstance>>::iterator teardownInstance(
        std::map<std::string, std::unique_ptr<PopoutInstance>>::iterator it)
    {
        PopoutInstance& inst = *it->second;

        if (inst.hostThread.joinable())
        {
            inst.hostThread.join();
        }
        inst.hostWindow = nullptr;
        inst.compositionSurface.destroy();

        PopoutMouseEvent stale_mev;
        while (inst.inputQueue.tryPop(stale_mev)) {}
        PopoutKeyEvent stale_kev;
        while (inst.keyQueue.tryPop(stale_kev)) {}
        inst.dragMode = DragMode::None;
        inst.resizeEdge = 0;
        destroyPopoutContextMenu(inst);
        destroyPopoutGestureHelper(inst);

        if (LLFloater* floater = inst.poppedFloaterHandle.get())
        {
            floater->setVisible(true);
            updatePopoutButtonToolTip(floater, false);
        }
        inst.poppedFloaterHandle.markDead();
        inst.poppedOut = false;

        return sInstances.erase(it);
    }
}

void LLFloaterPopoutManager::popOut(const std::string& name)
{
    if (isPoppedOut(name))
    {
        return;
    }

    // showInstance(), not findInstance() - closing a floater via its own
    // close button can DESTROY the instance (see sCloseCallbackWiredFor's
    // comment), so findInstance() would return nullptr and silently no-op
    // here on every popOut() after the first close, until something else
    // happened to reopen it first. showInstance() builds+opens it fresh if
    // needed, identical to the user manually reopening it via a menu first.
    LLFloater* floater = LLFloaterReg::showInstance(name);
    if (!floater)
    {
        return;
    }

    const LLRect rect = floater->getRect();
    const U32 w = (U32)llmax(rect.getWidth(), 1);
    const U32 h = (U32)llmax(rect.getHeight(), 1);

    auto& instance_ptr = sInstances[name];
    if (!instance_ptr)
    {
        instance_ptr = std::make_unique<PopoutInstance>();
    }
    PopoutInstance& inst = *instance_ptr;

    if (!ensureHostWindow(inst, w, h))
    {
        // ensureHostWindow() can fail in two different ways, and they need
        // different cleanup - a raw sInstances.erase(name) here (this
        // file's own earlier approach) is a real, confirmed crash: it
        // destructs PopoutInstance::hostThread while it may still be
        // joinable, and destroying a joinable std::thread calls
        // std::terminate() (C++ standard). Even the "CreateWindowExW
        // itself failed" case already started inst.hostThread (it's
        // created unconditionally in ensureHostWindow() before the null-
        // hwnd check) - it returns almost immediately in that case, but
        // it's still joinable until join()/detach() actually runs.
        if (inst.hostWindow)
        {
            // The real window WAS created (compositionSurface.createForWindow()
            // is what actually failed) - its message loop is genuinely
            // running right now on the host thread, so join()ing it here
            // would block this (the main) thread forever, waiting for a
            // WM_QUIT that nothing has posted yet. Post WM_CLOSE instead
            // and leave this instance in sInstances for now - idle()'s
            // existing hostWindowDestroyedExternally path (its own "ONE
            // teardown path" for every close, see its own comment) will
            // join the thread and erase the entry on the very next frame,
            // exactly like popIn()/onFloaterClosed() already rely on.
            // inst.poppedOut is still false at this point (only set true
            // further below), so idle()'s render/input loop won't try to
            // do anything with this instance in the meantime.
            PostMessage(inst.hostWindow, WM_CLOSE, 0, 0);
        }
        else
        {
            // CreateWindowExW itself failed - hostWindowThreadMain()
            // already returned (or is finishing up right now) having
            // done nothing but signal the promise, so join() here is
            // safe and fast, not a hang risk.
            if (inst.hostThread.joinable())
            {
                inst.hostThread.join();
            }
            sInstances.erase(name);
        }
        return;
    }

    void*& close_wired_for = sCloseCallbackWiredFor[name];
    if (close_wired_for != (void*)floater)
    {
        floater->setCloseCallback([name](LLUICtrl*, const LLSD&) { LLFloaterPopoutManager::onFloaterClosed(name); });
        close_wired_for = (void*)floater;
    }

    // Defensive/idempotent - idle() already wires this every frame as
    // soon as the floater exists, but this covers the (unlikely) case of
    // popOut() being called before idle() has run even once.
    ensurePopoutButtonWired(floater, name);

    inst.poppedFloaterHandle = floater->getHandle();
    inst.poppedOut = true;
    updatePopoutButtonToolTip(floater, true);
    // Removes it from the main window's draw walk AND hit-test tree in one
    // step (LLView::drawChildren()/hit-testing both already gate on
    // getVisible()) - no second LLFloaterView, no custom skip-logic needed.
    floater->setVisible(false);

    // Forces one real layout pass on the floater's own children right
    // now, even though its size isn't actually changing - LLView::
    // reshape() (llview.cpp) early-exits ("if (delta_width || delta_height
    // || sForceReshape)") whenever the requested size equals the current
    // one, unless sForceReshape is set. Any LLLayoutStack child (e.g.
    // im_container's participant-list/message-pane split) only correctly
    // sizes its own content once ITS OWN updateLayout() runs off a REAL
    // reshape - this framework never triggered one at all before
    // (docked layout stays valid; popping out doesn't change the
    // floater's rect, only where its pixels end up), so a layout that
    // depended on one - e.g. the message pane - stayed uninitialized
    // until something else (any actual resize) forced it. Confirmed live
    // as needing to "collapse the side tabs and re-expand them for the
    // chat to appear". Saved/restored rather than just set-then-clear,
    // in case this is somehow already true for an unrelated reason.
    const bool prev_force_reshape = LLView::sForceReshape;
    LLView::sForceReshape = true;
    floater->reshape(rect.getWidth(), rect.getHeight(), true);
    LLView::sForceReshape = prev_force_reshape;

    // The sForceReshape trick above still wasn't enough for
    // LLFloaterIMContainer specifically. First attempt (collapsing then
    // re-expanding the MESSAGES pane via collapseMessagesPane()) was
    // ALSO insufficient - confirmed live: user still needed to manually
    // collapse/re-expand the side tabs afterward. Root cause, traced
    // properly this time: the visible content isn't missing because of a
    // stale LLLayoutStack rect at all - mSelectedSession's tab panel
    // itself needs its own setVisible(true) reasserted.
    // LLTabContainer::setTab() (lltabcontainer.cpp) is what actually
    // calls tuple->mTabPanel->setVisible(is_selected) for whichever
    // session tab is hosted inside the messages pane - and it runs that
    // full loop unconditionally on EVERY call, even when the requested
    // tab is already the selected one, with no early-exit for "already
    // selected". LLFloaterIMContainer::reSelectConversation() (public,
    // llfloaterimcontainer.h) is exactly what
    // onExpandCollapseButtonClicked() calls, every time, right after its
    // own pane-collapse toggle - it's THAT call, not the collapse/expand
    // itself, that was actually making the chat "appear" when the user
    // did this by hand (LLFloaterIMSessionTab::getConversation(
    // mSelectedSession)->getHost() -> selectFloater() ->
    // mTabContainer->selectTabPanel() -> selectTab() -> setTab(), the
    // exact path above). Calling it directly reproduces that real fix
    // without needing the pane-collapse side effect at all.
    //
    // Scoped to LLFloaterIMContainer specifically via dynamic_cast rather
    // than a new generic LLFloater-wide virtual hook - no other floater
    // in POPPABLE_FLOATER_NAMES[] currently hosts a tabbed session this
    // way. If this recurs for a different floater type, generalize this
    // into a small virtual on LLFloater then, rather than guessing at the
    // right abstraction now for floaters not yet in the registry.
    if (LLFloaterIMContainer* im_container = dynamic_cast<LLFloaterIMContainer*>(floater))
    {
        im_container->reSelectConversation();
    }

    // Forces this floater's own subtree back to TT_DEFAULT, regardless
    // of whatever real docked-focus history left it at.
    // LLFloater::setFocus() may have set the WHOLE subtree to TT_ACTIVE/
    // TT_INACTIVE at some point before this was ever popped out (any
    // ordinary click on it while docked does this), and nothing in this
    // framework calls setFocus()/updateTransparency() again afterward
    // (rounds 48/49 deliberately stopped managing this at all) - so it
    // just stays frozen at whatever it happened to be. That matters here
    // specifically because TT_ACTIVE/TT_INACTIVE bypass the draw-context
    // mechanism entirely (LLUICtrl::getCurrentTransparency(),
    // lluictrl.cpp) and read straight from the real mainview
    // ActiveFloaterTransparency/InactiveFloaterTransparency settings
    // instead - exactly the coupling rounds 48/49 already rejected, and
    // the reason the floater's own chrome/background image was rendering
    // fully opaque regardless of this file's own fixed offscreen backing
    // color, effectively cancelling it out - confirmed live. TT_DEFAULT
    // is the ONLY type that respects an explicitly pushed
    // LLViewDrawContext (renderPoppedOut()'s own fixed alpha, see its
    // own comment) - forced once here, at pop-out time, since nothing
    // changes it again afterward.
    floater->updateTransparency(LLFloater::TT_DEFAULT);

    // Posted to the host thread, not called directly - see
    // WM_POPOUT_SHOW's comment.
    PostMessage(inst.hostWindow, WM_POPOUT_SHOW, 0, 0);
}

void LLFloaterPopoutManager::popIn(const std::string& name)
{
    auto it = sInstances.find(name);
    if (it == sInstances.end() || !it->second->poppedOut)
    {
        return;
    }
    PopoutInstance& inst = *it->second;

    // The menu is a direct child of the floater being restored to the
    // docked window - a leftover one would still be sitting there,
    // clickable, once it's shown again in the main window.
    destroyPopoutContextMenu(inst);
    destroyPopoutGestureHelper(inst);

    if (LLFloater* floater = inst.poppedFloaterHandle.get())
    {
        floater->setVisible(true);
        updatePopoutButtonToolTip(floater, false);
    }
    inst.poppedFloaterHandle.markDead();
    inst.poppedOut = false;

    // Full teardown, not a hide - the host window/thread/composition
    // surface are never reused across a pop-in/pop-out cycle. WM_CLOSE's
    // default DefWindowProc handling calls DestroyWindow(), which fires
    // WM_DESTROY synchronously on the host thread - that handler already
    // sets hostWindowDestroyedExternally and posts WM_QUIT, so idle()'s
    // existing (already-proven, taskbar-force-close-tested) cleanup path
    // reaps the thread and the composition surface with no new code path
    // needed here.
    if (inst.hostWindow)
    {
        PostMessage(inst.hostWindow, WM_CLOSE, 0, 0);
    }
}

bool LLFloaterPopoutManager::isPoppedOut(const std::string& name)
{
    auto it = sInstances.find(name);
    return it != sInstances.end() && it->second->poppedOut;
}

bool LLFloaterPopoutManager::isPoppedOut()
{
    for (const auto& kv : sInstances)
    {
        if (kv.second->poppedOut)
        {
            return true;
        }
    }
    return false;
}

bool LLFloaterPopoutManager::isViewInAnyPoppedOutFloater(LLView* view)
{
    if (!view)
    {
        return false;
    }
    for (const auto& kv : sInstances)
    {
        if (!kv.second->poppedOut)
        {
            continue;
        }
        if (LLFloater* floater = kv.second->poppedFloaterHandle.get())
        {
            if (view == floater || isDescendantOfFloater(floater, view))
            {
                return true;
            }
        }
    }
    return false;
}

void LLFloaterPopoutManager::onFloaterClosed(const std::string& name)
{
    auto it = sInstances.find(name);
    if (it == sInstances.end() || !it->second->poppedOut)
    {
        return;
    }
    PopoutInstance& inst = *it->second;

    // The menu is a direct child of the floater that may be about to be
    // destroyed (see LLFloater::closeFloater()'s own Hide-or-Destroy step) -
    // drop our own pointer to it first rather than let it dangle.
    destroyPopoutContextMenu(inst);
    destroyPopoutGestureHelper(inst);

    // Immediate, best-effort notice (nicer UX: hides the window right on
    // the close click instead of a frame later) - NOT the safety net.
    // idle()/renderPoppedOut() re-resolve poppedFloaterHandle.get() fresh
    // every use regardless, so even if this callback never fires for some
    // reason, a dead handle is still caught there.
    if (LLFloater* floater = inst.poppedFloaterHandle.get())
    {
        updatePopoutButtonToolTip(floater, false);
    }
    inst.poppedFloaterHandle.markDead();
    inst.poppedOut = false;

    // Full teardown, not a hide - see popIn()'s comment.
    if (inst.hostWindow)
    {
        PostMessage(inst.hostWindow, WM_CLOSE, 0, 0);
    }
}

void LLFloaterPopoutManager::shutdown()
{
    // Real, synchronous teardown - every OTHER close path in this file
    // (popIn()/onFloaterClosed()/idle()'s own hostWindowDestroyedExternally
    // handling) deliberately posts WM_CLOSE and waits for idle() to reap
    // the join on a LATER frame, specifically to never block the main
    // thread. That's the wrong tradeoff here: there IS no later frame -
    // this runs once, at real process shutdown, and the alternative to
    // blocking here is a guaranteed std::terminate() the moment
    // sInstances's own static destructor runs against a still-joinable
    // std::thread.
    for (auto& kv : sInstances)
    {
        PopoutInstance& inst = *kv.second;
        if (inst.hostWindow)
        {
            PostMessage(inst.hostWindow, WM_CLOSE, 0, 0);
        }
        if (inst.hostThread.joinable())
        {
            inst.hostThread.join();
        }
    }
    sInstances.clear();
}

void LLFloaterPopoutManager::idle()
{
    // Called every frame, unconditionally, for every REGISTERED floater
    // that currently exists - see ensurePopoutButtonWired()'s own comment
    // for why this can't skip itself once wired (a getChild() lookup +
    // pointer comparison, not a relayout - cheap enough to run every frame
    // regardless of state). repositionPopoutButton() is the same cost
    // class (a handful of getChild()/getRect() reads, one setRect() only
    // when something actually moved).
    for (const char* name : POPPABLE_FLOATER_NAMES)
    {
        if (LLFloater* floater = LLFloaterReg::findInstance(name))
        {
            ensurePopoutButtonWired(floater, name);
            repositionPopoutButton(floater);
        }
    }

    for (auto it = sInstances.begin(); it != sInstances.end(); )
    {
        PopoutInstance& inst = *it->second;

        if (inst.hostWindowDestroyedExternally)
        {
            // The window is already gone; joining just reaps the thread
            // once its GetMessage loop has exited on the WM_QUIT
            // PostQuitMessage() posted from WM_DESTROY. This is now the
            // ONE teardown path for every pop-in/close cycle (popIn()/
            // onFloaterClosed() both just post WM_CLOSE and land here
            // too), not only the rare externally-killed-window case it
            // was originally written for - teardownInstance() drops
            // anything that could otherwise carry over stale into the
            // next, freshly-created host window/thread for this same
            // floater name, and erases this entry, returning the next
            // valid iterator (the standard map::erase idiom).
            it = teardownInstance(it);
            continue;
        }

        if (!inst.poppedOut)
        {
            ++it;
            continue;
        }

        // See resizeJustCompleted's own comment on PopoutInstance - done
        // here, before renderPoppedOut() runs later this same frame, so
        // the floater's rect (and therefore its own LLLayoutStack
        // children's reflow) is already correct by the time anything
        // reads it this frame.
        if (inst.resizeJustCompleted)
        {
            inst.resizeJustCompleted = false;
            if (LLFloater* floater = inst.poppedFloaterHandle.get())
            {
                floater->reshape(inst.resizedWidth, inst.resizedHeight, true);
            }
            // See forceFullRedrawFrames's own comment on PopoutInstance.
            inst.forceFullRedrawFrames = 3;
        }

        PopoutMouseEvent ev;
        while (inst.inputQueue.tryPop(ev))
        {
            // Resolved fresh every iteration, never cached across events
            // or frames - see poppedFloaterHandle's own comment for why.
            LLFloater* floater = inst.poppedFloaterHandle.get();
            if (!floater)
            {
                inst.poppedOut = false;
                break;
            }

            // Dismiss any open popout context menu on a click outside it -
            // see showPopoutEditMenu()'s own comment. Checked before
            // capture resolution below since the menu is always a direct
            // CHILD of `floater`, so ev.x/ev.y (floater-local) is already
            // the right space to test against its rect. Deliberately
            // doesn't consume the event - mirrors LLMenuHolderGL::
            // handleMouseDown()'s own "clicked off the menu" behavior of
            // hiding it but still letting the same click reach whatever's
            // actually there once it's gone.
            if (inst.contextMenuPanel &&
                (ev.type == PopoutMouseEvent::Down || ev.type == PopoutMouseEvent::RightDown) &&
                !inst.contextMenuPanel->getRect().pointInRect(ev.x, ev.y))
            {
                destroyPopoutContextMenu(inst);
            }

            // Same idea for the gesture-autocomplete substitute - only on
            // a plain Down (this one has no right-click concept of its
            // own to also guard against).
            if (inst.gestureHelperPanel &&
                ev.type == PopoutMouseEvent::Down &&
                !inst.gestureHelperPanel->getRect().pointInRect(ev.x, ev.y))
            {
                destroyPopoutGestureHelper(inst);
            }

            // Mirrors LLViewerWindow's own top-level dispatch: a widget
            // mid-drag (LLDragHandle, a slider) calls gFocusMgr.
            // setMouseCapture(this) and expects follow-up events routed
            // straight to it, in ITS OWN local space, even once the mouse
            // has moved outside its own rect.
            S32 x = ev.x, y = ev.y;
            LLMouseHandler* target = floater;
            // A plain bool, NOT "was captor_view non-null" - captor_view
            // is ALSO null for a real, unowned captor that simply isn't
            // an LLView at all (e.g. LLToolGrab/LLToolCamera, live during
            // ordinary 3D-viewport click-drag interaction - LLMouseHandler
            // does not require LLView) - conflating that case with "no
            // foreign captor at all" meant this file's own clear-on-new-
            // gesture fix below silently never fired for it, leaving a
            // real 3D-tool's capture in place while dispatching INTO this
            // floater - confirmed as the exact scenario LLTextEditor's own
            // "refuse to take capture if anything else already holds it"
            // guard (below) was meant to be defended against.
            bool has_unowned_captor = false;
            if (LLMouseHandler* captor = gFocusMgr.getMouseCapture())
            {
                LLView* captor_view = dynamic_cast<LLView*>(captor);
                if (captor_view && floaterPointToViewLocal(floater, captor_view, ev.x, ev.y, &x, &y))
                {
                    target = captor;
                }
                else
                {
                    // Capture belongs to something this floater doesn't
                    // own - not ours to forward to, fall back to the
                    // floater. If we're about to start a NEW gesture
                    // (Down), also clear it (see below) rather than just
                    // working around it.
                    has_unowned_captor = true;
                }
            }

            // LLTextEditor (and any widget with the same pattern) only
            // takes its OWN capture if `!gFocusMgr.getMouseCapture()` -
            // i.e. it REFUSES if anything else currently holds capture,
            // even capture this floater doesn't own and that our own
            // fallback above already correctly routes around. Clearing it
            // right before a NEW Down gesture is safe: it's neither ours
            // nor reachable from our floater, so releasing it doesn't
            // affect whatever it belonged to any differently than that
            // widget's own handleMouseUp releasing it would have.
            if ((ev.type == PopoutMouseEvent::Down || ev.type == PopoutMouseEvent::RightDown) && has_unowned_captor)
            {
                gFocusMgr.setMouseCapture(NULL);
            }

            switch (ev.type)
            {
            case PopoutMouseEvent::Down:
            {
                target->handleMouseDown(x, y, ev.mask);
                // LLMenuButton::handleMouseDown() (llmenubutton.cpp) -
                // e.g. "sort_btn"/"view_options_btn"/"gear_btn"
                // (menu_participant_view.xml/menu_im_session_showmodes.xml/
                // menu_im_conversation.xml) - calls toggleMenu()
                // synchronously on a plain LEFT click, spawning a real
                // LLToggleableMenu (an LLMenuGL subclass, per
                // toggleMenu()'s own updateParent(LLMenuGL::sMenuContainer)
                // + LLMenuGL::showPopup() calls) into the SAME
                // LLMenuGL::sMenuContainer global as the right-click case
                // below - confirmed live as "view_options_button &
                // sort_button... still spawning" even after that fix,
                // since this file only ever checked for a just-spawned
                // menu after RightDown, never after a plain Down. Same
                // redirect, reusing showPopoutMenuFromLLMenuGL() as-is
                // (it doesn't care whether the menu came from a right-
                // click or a menu-button's left-click) - no fallback
                // branch needed here, unlike RightDown's showPopoutEditMenu()
                // safety net: an ordinary click that never touched a menu
                // button has nothing to find here, and shouldn't spawn a
                // Cut/Copy/Paste menu just because it happened to click
                // some other kind of widget.
                if (LLMenuGL::sMenuContainer)
                {
                    if (LLMenuGL* real_menu = dynamic_cast<LLMenuGL*>(LLMenuGL::sMenuContainer->getVisibleMenu()))
                    {
                        LLMenuGL::sMenuContainer->hideMenus();
                        showPopoutMenuFromLLMenuGL(inst, floater, real_menu, ev.x, ev.y);
                    }
                }
                break;
            }
            case PopoutMouseEvent::Up:
                target->handleMouseUp(x, y, ev.mask);
                break;
            case PopoutMouseEvent::Move:
            {
                // LLButton::handleHover() (llbutton.cpp) unconditionally
                // sets mNeedsHighlight=true on itself whenever hover
                // reaches it - LLView::childrenHandleHover()'s own default
                // dispatch (llview.cpp) finds and calls hover on exactly
                // the one child under the cursor each time, same as it
                // always has, so that part was never the bug. What's
                // missing is the UN-highlight: for the MAIN window,
                // LLViewerWindow::updateUI() computes a full hover-set
                // every frame and explicitly calls onMouseLeave() on
                // whatever left it (llviewerwindow.cpp) - LLButton::
                // onMouseLeave() is what actually clears mNeedsHighlight.
                // This framework's own dispatch only ever calls
                // handleHover() on the new target, never onMouseLeave()
                // on whichever sibling had it before - so every button
                // ever hovered inside a popped-out floater stayed
                // permanently highlighted, stacking up one at a time -
                // confirmed live as "the sort menu highlight for options
                // highlights all options as you mouse over them" (most
                // visible here since these substitute menus are several
                // small adjacent buttons, but the same bug affects any
                // hoverable widget in any popped-out floater). Fixed by
                // tracking the one view that last received hover
                // (lastHoverView, per-instance) and explicitly leaving it
                // the moment hover moves to a DIFFERENT view - mirrors
                // LLViewerWindow's own enter/leave diff, just scoped to
                // this floater's own subtree instead of the whole main
                // window. childFromPoint(x, y, true) walks the same
                // target `target` itself would already dispatch into,
                // just without also calling handleHover() a second time.
                LLView* target_view = dynamic_cast<LLView*>(target);
                LLView* hovered = target_view ? target_view->childFromPoint(x, y, true) : nullptr;
                if (!hovered)
                {
                    hovered = target_view;
                }
                LLView* prev_hover = inst.lastHoverView.get();
                if (prev_hover && prev_hover != hovered)
                {
                    prev_hover->onMouseLeave(0, 0, ev.mask);
                }
                inst.lastHoverView = hovered ? hovered->getHandle() : LLHandle<LLView>();

                target->handleHover(x, y, ev.mask);
                break;
            }
            case PopoutMouseEvent::Wheel:
                target->handleScrollWheel(x, y, ev.clicks);
                break;
            case PopoutMouseEvent::RightDown:
            {
                target->handleRightMouseDown(x, y, ev.mask);
                // Suppress whatever real LLContextMenu the call above may
                // have just spawned and show this framework's own
                // equivalent instead. ev.x/ev.y, not x/y - the menu is
                // always a direct child of `floater`, so it needs
                // floater-local coordinates regardless of what target/x/y
                // resolved to above. Read the real menu BEFORE hiding it -
                // hideMenus() only hides, but read first anyway, matching
                // the safer ordering. Prefer the generic LLMenuGL-item
                // walk (showPopoutMenuFromLLMenuGL(), handles ANY menu
                // type - text-editing, avatar/object icon, future ones -
                // with no per-menu-type code); fall back to the
                // LLEditMenuHandler-specific path only if no real menu
                // actually spawned this time (gEditMenuHandler was still
                // set some other way) - keeps the original, already-
                // proven-live behavior as a safety net, not a regression.
                LLMenuGL* real_menu = LLMenuGL::sMenuContainer
                    ? dynamic_cast<LLMenuGL*>(LLMenuGL::sMenuContainer->getVisibleMenu())
                    : nullptr;
                if (LLMenuGL::sMenuContainer)
                {
                    LLMenuGL::sMenuContainer->hideMenus();
                }
                if (real_menu)
                {
                    showPopoutMenuFromLLMenuGL(inst, floater, real_menu, ev.x, ev.y);
                }
                else
                {
                    showPopoutEditMenu(inst, floater, ev.x, ev.y);
                }
                break;
            }
            case PopoutMouseEvent::RightUp:
                target->handleRightMouseUp(x, y, ev.mask);
                break;
            }
        }

        // A menu item's own click callback (inside the mouse-dispatch
        // loop just above) only flags this - see its own comment for why
        // the actual deletion can't happen there. This point is safely
        // outside any call stack rooted in one of contextMenuPanel's own
        // children, since the entire dispatch loop that could have called
        // into it has now fully returned.
        if (inst.contextMenuPendingDestroy)
        {
            destroyPopoutContextMenu(inst);
        }

        if (inst.gestureHelperPendingDestroy)
        {
            destroyPopoutGestureHelper(inst);
        }

        PopoutKeyEvent kev;
        while (inst.keyQueue.tryPop(kev))
        {
            LLFloater* floater = inst.poppedFloaterHandle.get();
            if (!floater)
            {
                inst.poppedOut = false;
                break;
            }

            // Mirrors LLViewerWindow::handleKey()'s own dispatch: keyboard
            // focus, not the mouse-capture/tree-walk path, decides the
            // target. Same reachability discipline as mouse capture -
            // gFocusMgr's keyboard focus is a single global shared with
            // the whole main window.
            LLFocusableElement* target = floater;
            if (LLFocusableElement* kb_focus = gFocusMgr.getKeyboardFocus())
            {
                LLView* kb_view = dynamic_cast<LLView*>(kb_focus);
                if (kb_view && isDescendantOfFloater(floater, kb_view))
                {
                    target = kb_focus;
                }
            }

            switch (kev.type)
            {
            case PopoutKeyEvent::KeyDown:
            {
                // Ctrl+C/X/V/A/Z/Y are NOT dispatched via a widget's own
                // handleKey() in this codebase at all - they're menu
                // accelerators, normally checked by the main menu bar's
                // own keyboard handling and routed to LLEditMenuHandler::
                // gEditMenuHandler (a widget sets itself as this when it
                // receives focus - already happens correctly via this
                // file's existing mouse-click dispatch). Same reachability
                // discipline as mouse capture/keyboard focus -
                // gEditMenuHandler is just as capable of holding something
                // unrelated to this floater.
                if (kev.mask & MASK_CONTROL)
                {
                    LLEditMenuHandler* edit = LLEditMenuHandler::gEditMenuHandler;
                    LLView* edit_view = edit ? dynamic_cast<LLView*>(edit) : nullptr;
                    if (edit_view && isDescendantOfFloater(floater, edit_view))
                    {
                        switch (kev.vk)
                        {
                        case 'C': if (edit->canCopy()) edit->copy(); break;
                        case 'X': if (edit->canCut()) edit->cut(); break;
                        case 'V': if (edit->canPaste()) edit->paste(); break;
                        case 'A': if (edit->canSelectAll()) edit->selectAll(); break;
                        case 'Z': if (edit->canUndo()) edit->undo(); break;
                        case 'Y': if (edit->canRedo()) edit->redo(); break;
                        default: break;
                        }
                        break;
                    }
                }

                KEY key;
                if (gKeyboard->translateKey(kev.vk, &key))
                {
                    target->handleKey(key, kev.mask, false);
                }
                break;
            }
            case PopoutKeyEvent::KeyUp:
            {
                KEY key;
                if (gKeyboard->translateKey(kev.vk, &key))
                {
                    target->handleKeyUp(key, kev.mask, false);
                }
                break;
            }
            case PopoutKeyEvent::Char:
                target->handleUnicodeChar(kev.uni_char, false);
                break;
            }

            // Re-derived after EVERY key event, matching the same
            // per-keystroke cadence LLFloaterIMNearbyChat::
            // onChatBoxKeystroke() itself uses to decide whether to (re)show
            // its own real gesture-autocomplete floater - see
            // updatePopoutGestureHelper()'s own comment.
            if (LLUICtrl* target_ui = dynamic_cast<LLUICtrl*>(target))
            {
                updatePopoutGestureHelper(inst, floater, target_ui);
            }
        }

        ++it;
    }
}

void LLFloaterPopoutManager::renderPoppedOut()
{
    for (auto& kv : sInstances)
    {
        PopoutInstance& inst = *kv.second;
        if (!inst.poppedOut)
        {
            continue;
        }
        LLFloater* floater = inst.poppedFloaterHandle.get();
        if (!floater)
        {
            inst.poppedOut = false;
            continue;
        }

        // While a resize gesture is actively in progress, the FLOATER's
        // own rect is deliberately still stale (see resizeJustCompleted's
        // own comment: floater->reshape() is deferred to drag-end, on
        // purpose, to avoid re-fighting the drag / the round-43 flicker
        // fix) - but the REAL host window's client rect is NOT stale, it
        // tracks the live drag every frame via hostWindowProc()'s own
        // SetWindowPos() calls. Using the floater's stale rect for the
        // offscreen target/composition surface size here (as this used
        // to) meant BOTH stayed frozen at the PRE-drag size for the
        // entire gesture while the real window kept growing underneath -
        // DirectComposition's surface has no implicit stretch-to-fit and
        // is drawn at a fixed offset (DXCompositionSurface::update()),
        // so the mismatch showed up as the floater's own edge-anchored
        // elements (the resize handle itself, title buttons) appearing to
        // vanish/go stale once the real window grew far enough past
        // them - confirmed live as "seems there is a hard limit in
        // resizing". Tracking the real window's live size here instead
        // means the offscreen target/composition surface now genuinely
        // keep pace with the drag in real time; the floater's OWN content
        // still only reflows once the drag ends and reshape() finally
        // runs (unchanged, still deliberate), so the visible result while
        // actively dragging OUTWARD is the existing content anchored in
        // one corner with a plain black margin (this file's own fixed
        // offscreen clear color) filling in the newly-exposed area -
        // consistent and well-defined, never a stale/mismatched surface.
        S32 w, h;
        if (inst.dragMode == DragMode::Resizing && inst.hostWindow)
        {
            RECT live_rect;
            GetClientRect(inst.hostWindow, &live_rect);
            w = llmax((S32)(live_rect.right - live_rect.left), 1);
            h = llmax((S32)(live_rect.bottom - live_rect.top), 1);
        }
        else
        {
            const LLRect rect = floater->getRect();
            w = llmax(rect.getWidth(), 1);
            h = llmax(rect.getHeight(), 1);
        }

        // Refreshed each frame for hostWindowProc()'s header-click test -
        // dragging is OS-driven via HTCAPTION-equivalent logic now, not via
        // the floater's own LLDragHandle, so this floater's rect position
        // itself is never touched by dragging and needs no sync back to
        // the window.
        inst.headerHeightCache = floater->getHeaderHeight();
        inst.floaterResizableCache = floater->isResizable();

        // The actual HWND's client size must track the floater's current
        // size every frame, not just once at popOut() time - the
        // composition surface already resizes below, but that's a
        // separate DirectComposition-side buffer; without also resizing
        // the real window, its client area (and therefore every click/
        // hit-test coordinate hostWindowProc() computes) stays at whatever
        // size it was when first created.
        ensureHostWindow(inst, (U32)w, (U32)h);

        // See forceFullRedrawFrames's own comment on PopoutInstance -
        // release() resets mResX/mResY to 0 (llrendertarget.cpp), which
        // guarantees the size check right below always reallocates fresh
        // this frame, regardless of whether w/h already happened to
        // match what was already there.
        if (inst.forceFullRedrawFrames > 0)
        {
            inst.offscreen.release();
            --inst.forceFullRedrawFrames;
        }

        // !offscreenAllocOk (not just a size mismatch) also forces a
        // retry - see its own comment on PopoutInstance for why a failed
        // allocate() can't be detected from getWidth()/getHeight() alone.
        if ((S32)inst.offscreen.getWidth() != w || (S32)inst.offscreen.getHeight() != h || !inst.offscreenAllocOk)
        {
            // GL_BGRA (not GL_RGBA) - see glColorFormatToDX()'s comment in
            // llrendertarget.cpp: DirectComposition's CreateSurface rejects
            // R8G8B8A8_UNORM outright, so the offscreen target is
            // allocated directly in the composition-compatible channel
            // order.
            inst.offscreenAllocOk = inst.offscreen.allocate(w, h, GL_BGRA, false);
        }

        gDXUIBatch.flushPending();

        inst.offscreen.bindTarget();
        // The floater's OWN real background color (getBackgroundColor()/
        // getTransparentColor(), settings/colors.xml-driven, whichever
        // isBackgroundOpaque() says this floater actually uses), always
        // at alpha=1 (our own backing must stay fully opaque regardless -
        // there is nothing further behind it but the real desktop).
        // Tried flat pure black here first (rounds 48/49), which is what
        // exposed a SEPARATE, real bug (round 52: the chrome was
        // rendering fully opaque, unrelated to this color choice) - but
        // once THAT was fixed, pure black turned out to have its own
        // problem: several widgets throughout this skin (e.g.
        // floater_im_session.xml's chat_editor: bg_writeable_color=
        // "Black_50") use a semi-transparent BLACK overlay color
        // (colors.xml: Black_50 = "0 0 0 0.5") specifically designed to
        // darken a NON-black docked floater background - blended over
        // OUR pure-black backing instead, black-at-50%-alpha-over-black
        // is still just black, mathematically indistinguishable from
        // nothing drawn at all - confirmed live as "the unfocused white
        // chat bar is nearly 100% see through" and equally transparent-
        // looking titlebars. Using the REAL background color here is
        // exactly what every such Black_NN/White_NN overlay color in
        // this skin was actually designed against - fixes this whole
        // class of widget uniformly, not just the one that was reported.
        // Safe to revisit now (this is what round 47 originally tried,
        // reverted in round 48 for an UNRELATED reason: the floater's
        // real transparency type reading mainview's Active/
        // InactiveFloaterTransparency settings, permanently looking
        // "inactive" regardless of this instance's own real focus state -
        // round 52's forced TT_DEFAULT + fixed LLViewDrawContext already
        // decouples that completely, so the original reason to avoid the
        // real color no longer applies).
        const LLColor4& bg_color = floater->isBackgroundOpaque() ? floater->getBackgroundColor() : floater->getTransparentColor();
        inst.offscreen.clearColor(bg_color.mV[VRED], bg_color.mV[VGREEN], bg_color.mV[VBLUE], 1.f);
        inst.offscreen.clear();

        gDX.matrixMode(LLRender::MM_PROJECTION);
        gDX.pushMatrix();
        gDX.matrixMode(LLRender::MM_MODELVIEW);
        gDX.pushMatrix();
        LLUI::pushMatrix();

        // Renders this floater into a target sized exactly to its own
        // rect, so its local (0,0)-(w,h) draw calls land at the target's
        // (0,0) directly - no translate needed.
        dx_state_for_2d(w, h);
        LLUI::loadIdentity();

        // Must bind a shader before draw()ing - gViewerWindow->
        // drawDebugText() (llviewerdisplay.cpp, called right before this
        // in the main frame) explicitly unbinds gUIProgram at its own end,
        // so nothing is bound by the time this runs otherwise.
        gUIProgram.bind();
        gDX.color4f(1.f, 1.f, 1.f, 1.f);

        // Fixed, deliberately NOT mainview-setting-driven (rounds 48/49's
        // own conclusion) alpha for the floater's own chrome/content -
        // LLUICtrl::getCurrentTransparency()'s TT_DEFAULT branch (the
        // type popOut() now forces this floater's whole subtree to,
        // exactly so it respects this) reads
        // LLViewDrawContext::getCurrentContext().mAlpha, which this push
        // supplies directly - without it, TT_DEFAULT's own empty-stack
        // fallback IS already alpha=1.0 (llview.cpp), so pushing 1.0
        // here explicitly isn't strictly necessary for THAT default, but
        // is kept anyway as the single, obvious place controlling this
        // framework's own fixed chrome alpha, rather than relying on an
        // incidental fallback value staying 1.0 forever. Deliberately
        // NOT less than 1.0: this file's own offscreen backing color is
        // now the floater's own REAL background color (see this
        // function's own clearColor comment) specifically so every
        // widget's own authored color/alpha (including things like
        // floater_im_session.xml's chat_editor: bg_writeable_color=
        // "Black_50") renders EXACTLY as originally designed - an
        // artificial extra dimming factor here would only work against
        // that, reintroducing a milder version of the same "renders
        // wrong against whatever we clear to" problem this round fixed.
        LLViewDrawContext chrome_alpha_ctx(1.f);
        floater->draw();

        gUIProgram.unbind();

        LLUI::popMatrix();
        gDX.matrixMode(LLRender::MM_MODELVIEW);
        gDX.popMatrix();
        gDX.matrixMode(LLRender::MM_PROJECTION);
        gDX.popMatrix();

        gDXUIBatch.flushPending();

        inst.offscreen.flush();

        inst.compositionSurface.update(inst.offscreen.getDXColorTexture(0), (UINT)w, (UINT)h);
    }
}
