/**
 * @file llfloaterpopout.h
 * @brief Floater-outside-viewport framework - phase 1 (Stage A offscreen
 * render, Stage B DirectComposition, Stage C input/drag/lifecycle, shipped
 * r4012 for "sl_about") + phase 2 (multi-instance generalization - any
 * number of registered floaters poppable independently/simultaneously, each
 * with its own host window/thread/render target). See
 * inherited-tickling-kurzweil plan.
 */

#ifndef LL_LLFLOATERPOPOUT_H
#define LL_LLFLOATERPOPOUT_H

#include <string>

class LLFloater;
class LLView;

class LLFloaterPopoutManager
{
public:
    // name is the floater's LLFloaterReg registration name (e.g.
    // "sl_about", "nearby_chat") - must be one of the entries in this
    // file's own POPPABLE_FLOATER_NAMES registry.
    static void popOut(const std::string& name);
    static void popIn(const std::string& name);
    static bool isPoppedOut(const std::string& name);

    // No-arg overload: is ANYTHING currently popped out, regardless of
    // which floater - for callers that don't care which instance (the
    // main-thread background-throttle/focus-loss guards).
    static bool isPoppedOut();

    // True if view is the popped-out floater itself, or a descendant of
    // one, for ANY currently popped-out instance - lets callers outside
    // this framework (LLViewerWindow::updateUI()/updateKeyboardFocus(),
    // see their own comments) tell whether some LLView they're about to
    // touch actually belongs to one, instead of guessing.
    static bool isViewInAnyPoppedOutFloater(LLView* view);

    // Main-thread only, called once per frame near gatherInput()
    // (llappviewer.cpp) - wires/repositions each registered floater's own
    // popout button (regardless of popped-out state) and drains queued
    // host-window input for every currently popped-out instance,
    // dispatching it directly to that floater's LLView handlers.
    static void idle();

    // Main-thread only, called once per frame from render_ui()
    // (llviewerdisplay.cpp, after the main UI draw pass) - renders every
    // currently popped-out floater into its own offscreen target and
    // pushes it to its own DirectComposition surface. No-op for any
    // instance with nothing to do.
    static void renderPoppedOut();

    // Wired as a popped-out floater's own close callback (its own in-UI
    // close button, not popIn()) - see LLMortician same-frame-deletion
    // discipline in the .cpp: nulls state immediately, no deferral.
    static void onFloaterClosed(const std::string& name);

    // Main-thread only, called ONCE from LLAppViewer::cleanup() before
    // static destructors can run - synchronously closes and joins every
    // still-popped-out instance's host thread. Without this, quitting
    // while any floater is popped out (or on a force-quit path that
    // skips gFloaterView->closeAllChildren() entirely) can leave a host
    // thread still joinable when the anonymous-namespace instance map is
    // statically destroyed - destroying a joinable std::thread calls
    // std::terminate() (C++ standard). Blocking join() here is safe and
    // intended - this runs exactly once, at real shutdown, never per
    // frame.
    static void shutdown();
};

#endif // LL_LLFLOATERPOPOUT_H
