/**
 * @file webview2_host.h
 * @brief Off-screen WebView2 host for the S24 media plugin.
 *
 * Hosts a WebView2 control in a composition visual attached to no window, captures it
 * with Windows.Graphics.Capture into a D3D11 texture and hands the pixels out through
 * pollFrame(). Deliberately free of Linden headers: it uses only the C++ standard
 * library, Win32, WinRT and the WebView2 SDK, and everything crossing this interface
 * is plain data (UTF-8 std::string).
 *
 * Threading: every method must be called from the thread that called start()
 * (the plugin's main thread, which also runs pump()). Callbacks fire on that thread.
 *
 * Copyright (c) 2025-2026 Kirsten's Viewer Contributors
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace wv2
{

enum class MouseKind { Move, LeftDown, LeftUp, LeftDoubleClick, Leave };

struct Callbacks
{
    // Called once when the control exists (ok) or could not be created (!ok, detail says why).
    std::function<void(bool ok, const std::string& detail)> onReady;
    // Return true to cancel the navigation (used to hand secondlife:// URLs to the viewer).
    std::function<bool(const std::string& uri, bool userInitiated, bool redirected)> onNavigationStarting;
    std::function<void(int httpStatus, const std::string& uri, bool ok)> onNavigationCompleted;
    std::function<void(const std::string& uri)> onLocationChanged;
    std::function<void(const std::string& title)> onTitleChanged;
    std::function<void(const std::string& uri, const std::string& target)> onNewWindow;
    std::function<void(const std::string& cursorName)> onCursor;
    std::function<void(const std::string& text)> onStatusText;
    std::function<void(const std::string& message)> onLog;
};

struct Settings
{
    std::string userDataFolder;     // per-instance profile folder (UTF-8)
    std::string language;           // e.g. "en"
    std::string userAgentSuffix;    // appended to the engine's own user agent
    std::string proxyHostPort;      // "host:port" or empty
    bool javascript = true;
    bool disableGpu = false;
    double zoom = 1.0;
    int width = 1024;
    int height = 1024;
};

class Host
{
public:
    Host();
    ~Host();

    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    // Starts asynchronous creation. onReady reports the outcome from within pump().
    bool start(const Settings& settings, Callbacks callbacks);
    void shutdown();

    // Runs the thread's message loop (non-blocking). Call from the plugin's idle handler.
    void pump();

    bool ready() const;
    void resize(int width, int height);

    void navigate(const std::string& utf8Uri);
    void executeScript(const std::string& utf8Js);
    void goBack();
    void goForward();
    void reload();
    void stop();
    bool canGoBack() const;
    bool canGoForward() const;
    void setZoom(double factor);
    void setMuted(bool muted);
    void focus();
    void openDevTools();
    void deleteAllCookies();
    void setCookie(const std::string& uri, const std::string& name, const std::string& value,
                   const std::string& domain, const std::string& path, bool httpOnly, bool secure);

    void mouse(MouseKind kind, int x, int y);
    // deltas in wheel units: 120 per notch, positive = away from the user (up / right)
    void wheel(int x, int y, int deltaX, int deltaY);
    // Chrome DevTools Protocol call, e.g. Input.dispatchKeyEvent
    void callDevTools(const std::string& method, const std::string& jsonParams);

    // Copies the newest captured frame (BGRA, tightly packed width*4 per row) into dst if a new
    // one has arrived since the last call and its size matches width x height. flipY writes the
    // rows bottom-up (the viewer's OpenGL-style texture convention).
    bool pollFrame(uint8_t* dst, int width, int height, bool flipY);

    // Forget that the newest frame was delivered, so the next pollFrame() copies it again (used when
    // the destination buffer changes, e.g. after the viewer hands the plugin a new shared segment).
    void invalidateFrame();

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace wv2
