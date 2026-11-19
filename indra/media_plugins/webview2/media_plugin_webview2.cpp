/**
 * @file media_plugin_webview2.cpp
 * @brief WebView2 (Edge) plugin for the LLMedia API plugin system.
 *
 * Replaces media_plugin_cef: same plugin protocol, but the engine is the Windows-maintained
 * WebView2 runtime, rendered off-screen (see webview2_host.h) into the plugin's shared memory.
 *
 * Copyright (c) 2025-2026 Kirsten's Viewer Contributors
 * SPDX-License-Identifier: MIT
 */

#include "linden_common.h"
#include "indra_constants.h"

#include "llglheaders.h" // for GL_* constants
#include "llsdutil.h"
#include "llplugininstance.h"
#include "llpluginmessage.h"
#include "llpluginmessageclasses.h"
#include "llstring.h"
#include "media_plugin_base.h"

#include "webview2_host.h"

#include <sstream>

////////////////////////////////////////////////////////////////////////////////
//
class MediaPluginWebView2 :
    public MediaPluginBase
{
public:
    MediaPluginWebView2(LLPluginInstance::sendMessageFunction host_send_func, void* host_user_data);
    ~MediaPluginWebView2();

    /*virtual*/
    void receiveMessage(const char* message_string);

private:
    void startHost(F32 zoom_factor);
    void postDebugMessage(const std::string& msg);
    void authResponse(LLPluginMessage& message);
    void nativeKey(U32 msg, U32 wparam, U64 lparam);
    void editCommand(const char* command);
    void sendHistoryFlags(LLPluginMessage& message);

    wv2::Host mHost;

    bool mEnableMediaPluginDebugging;
    std::string mHostLanguage;
    bool mJavascriptEnabled;
    bool mProxyEnabled;
    std::string mProxyHost;
    int mProxyPort;
    bool mDisableGPU;
    std::string mUserAgentSubtring;
    std::string mRootCachePath;
    F32 mCurVolume;

    // keyboard translation state
    unsigned mHighSurrogate;
};

////////////////////////////////////////////////////////////////////////////////
//
MediaPluginWebView2::MediaPluginWebView2(LLPluginInstance::sendMessageFunction host_send_func, void* host_user_data) :
    MediaPluginBase(host_send_func, host_user_data)
{
    mWidth = 0;
    mHeight = 0;
    mDepth = 4;
    mPixels = 0;
    mEnableMediaPluginDebugging = true;
    mHostLanguage = "en";
    mJavascriptEnabled = true;
    mProxyEnabled = false;
    mProxyPort = 0;
    mDisableGPU = false;
    mCurVolume = 0.0f;  // silent until the viewer sends a volume, matching the CEF plugin
    mHighSurrogate = 0;
}

MediaPluginWebView2::~MediaPluginWebView2()
{
    mHost.shutdown();
}

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginWebView2::postDebugMessage(const std::string& msg)
{
    if (mEnableMediaPluginDebugging)
    {
        std::stringstream str;
        str << "@Media Msg> " << msg;

        LLPluginMessage debug_message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "debug_message");
        debug_message.setValue("message_text", str.str());
        debug_message.setValue("message_level", "info");
        sendMessage(debug_message);
    }
}

void MediaPluginWebView2::sendHistoryFlags(LLPluginMessage& message)
{
    message.setValueBoolean("history_back_available", mHost.canGoBack());
    message.setValueBoolean("history_forward_available", mHost.canGoForward());
}

////////////////////////////////////////////////////////////////////////////////
// Builds the host callbacks (all fire on this thread, from within pump()) and starts it.
void MediaPluginWebView2::startHost(F32 zoom_factor)
{
    wv2::Settings settings;
    settings.userDataFolder = mRootCachePath;
    settings.language = mHostLanguage;
    settings.userAgentSuffix = mUserAgentSubtring;
    settings.javascript = mJavascriptEnabled;
    settings.disableGpu = mDisableGPU;
    settings.zoom = zoom_factor > 0.f ? zoom_factor : 1.0;
    settings.width = 1024;
    settings.height = 1024;
    if (mProxyEnabled && !mProxyHost.empty())
    {
        std::ostringstream proxy;
        proxy << mProxyHost << ":" << mProxyPort;
        settings.proxyHostPort = proxy.str();
    }

    wv2::Callbacks cb;
    cb.onLog = [this](const std::string& s) { postDebugMessage(s); };

    cb.onReady = [this](bool ok, const std::string& detail)
    {
        if (!ok)
        {
            // The viewer's media system reports a plugin that never becomes usable.
            postDebugMessage("WebView2 unavailable: " + detail);
            setStatus(STATUS_ERROR);
        }
    };

    cb.onNavigationStarting = [this](const std::string& uri, bool user_initiated, bool redirected) -> bool
    {
        // The viewer owns secondlife:// (SLAPP) links; hand them over instead of navigating.
        if (uri.size() > 11 && LLStringUtil::startsWith(uri, "secondlife:"))
        {
            LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA_BROWSER, "click_nofollow");
            message.setValue("uri", uri);
            // a user click is okay on a SLAPP, a scripted navigation is not
            message.setValue("nav_type", user_initiated ? "clicked" : "navigated");
            message.setValueBoolean("is_redirect", redirected);
            sendMessage(message);
            return true; // cancel
        }

        LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA_BROWSER, "navigate_begin");
        sendHistoryFlags(message);
        sendMessage(message);
        return false;
    };

    cb.onNavigationCompleted = [this](int status, const std::string& uri, bool ok)
    {
        LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA_BROWSER, "navigate_complete");
        message.setValueS32("result_code", status);
        sendHistoryFlags(message);
        message.setValue("uri", uri);
        sendMessage(message);
    };

    cb.onLocationChanged = [this](const std::string& uri)
    {
        LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA_BROWSER, "location_changed");
        message.setValue("uri", uri);
        sendMessage(message);
    };

    cb.onTitleChanged = [this](const std::string& title)
    {
        LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "name_text");
        message.setValue("name", title);
        sendHistoryFlags(message);
        sendMessage(message);
    };

    cb.onNewWindow = [this](const std::string& uri, const std::string& target)
    {
        LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA_BROWSER, "click_href");
        message.setValue("uri", uri);
        message.setValue("target", target);
        sendMessage(message);
    };

    cb.onCursor = [this](const std::string& name)
    {
        LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "cursor_changed");
        message.setValue("name", name);
        sendMessage(message);
    };

    cb.onStatusText = [this](const std::string& text)
    {
        LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA_BROWSER, "status_text");
        message.setValue("status", text);
        sendMessage(message);
    };

    mHost.setMuted(mCurVolume <= 0.0f);  // applied once the engine is ready
    mHost.start(settings, cb);
}

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginWebView2::authResponse(LLPluginMessage& message)
{
    // S24 : WebView2 sign-in is handled by the engine; the viewer's reply is not used
}

////////////////////////////////////////////////////////////////////////////////
// Editing commands go through the DevTools protocol so they act on the focused element.
void MediaPluginWebView2::editCommand(const char* command)
{
    std::string params = "{\"type\":\"rawKeyDown\",\"windowsVirtualKeyCode\":0,\"commands\":[\"";
    params += command;
    params += "\"]}";
    mHost.callDevTools("Input.dispatchKeyEvent", params);
}

////////////////////////////////////////////////////////////////////////////////
//
void MediaPluginWebView2::receiveMessage(const char* message_string)
{
    LLPluginMessage message_in;

    if (message_in.parse(message_string) >= 0)
    {
        std::string message_class = message_in.getClass();
        std::string message_name = message_in.getName();
        if (message_class == LLPLUGIN_MESSAGE_CLASS_BASE)
        {
            if (message_name == "init")
            {
                LLPluginMessage message("base", "init_response");
                LLSD versions = LLSD::emptyMap();
                versions[LLPLUGIN_MESSAGE_CLASS_BASE] = LLPLUGIN_MESSAGE_CLASS_BASE_VERSION;
                versions[LLPLUGIN_MESSAGE_CLASS_MEDIA] = LLPLUGIN_MESSAGE_CLASS_MEDIA_VERSION;
                versions[LLPLUGIN_MESSAGE_CLASS_MEDIA_BROWSER] = LLPLUGIN_MESSAGE_CLASS_MEDIA_BROWSER_VERSION;
                message.setValueLLSD("versions", versions);

                std::string plugin_version = "WebView2 plugin 0.1";
                message.setValue("plugin_version", plugin_version);
                sendMessage(message);
            }
            else if (message_name == "idle")
            {
                mHost.pump();

                if (mPixels && mWidth > 0 && mHeight > 0)
                {
                    // frames arrive only when the page changes; the viewer's texture is bottom-up
                    if (mHost.pollFrame(mPixels, mWidth, mHeight, true))
                    {
                        setDirty(0, 0, mWidth, mHeight);
                    }
                }
            }
            else if (message_name == "cleanup")
            {
                mHost.shutdown();

                LLPluginMessage message("base", "goodbye");
                sendMessage(message);

                // Will trigger delete on next staticReceiveMessage()
                mDeleteMe = true;
            }
            else if (message_name == "force_exit")
            {
                mDeleteMe = true;
            }
            else if (message_name == "shm_added")
            {
                SharedSegmentInfo info;
                info.mAddress = message_in.getValuePointer("address");
                info.mSize = (size_t)message_in.getValueS32("size");
                std::string name = message_in.getValue("name");

                mSharedSegments.insert(SharedSegmentMap::value_type(name, info));
            }
            else if (message_name == "shm_remove")
            {
                std::string name = message_in.getValue("name");

                SharedSegmentMap::iterator iter = mSharedSegments.find(name);
                if (iter != mSharedSegments.end())
                {
                    if (mPixels == iter->second.mAddress)
                    {
                        mPixels = NULL;
                        mTextureSegmentName.clear();
                    }
                    mSharedSegments.erase(iter);
                }

                LLPluginMessage message("base", "shm_remove_response");
                message.setValue("name", name);
                sendMessage(message);
            }
        }
        else if (message_class == LLPLUGIN_MESSAGE_CLASS_MEDIA)
        {
            if (message_name == "init")
            {
                F32 factor = (F32)message_in.getValueReal("factor");
                startHost(factor);

                // Plugin gets to decide the texture parameters to use.
                mDepth = 4;
                LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "texture_params");
                message.setValueS32("default_width", 1024);
                message.setValueS32("default_height", 1024);
                message.setValueS32("depth", mDepth);
                message.setValueU32("internalformat", GL_RGB);
                message.setValueU32("format", GL_BGRA);
                message.setValueU32("type", GL_UNSIGNED_BYTE);
                message.setValueBoolean("coords_opengl", true);
                sendMessage(message);

                LLPluginMessage name_message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "name_text");
                name_message.setValue("name", "WebView2 Plugin");
                sendMessage(name_message);
            }
            else if (message_name == "set_user_data_path")
            {
                // The parent owns and keys the per-instance profile folder (see
                // LLViewerMediaImpl::initializePlugin() in llviewermedia.cpp).
                mRootCachePath = message_in.getValue("cache_path");
            }
            else if (message_name == "size_change")
            {
                std::string name = message_in.getValue("name");
                S32 width = message_in.getValueS32("width");
                S32 height = message_in.getValueS32("height");
                S32 texture_width = message_in.getValueS32("texture_width");
                S32 texture_height = message_in.getValueS32("texture_height");

                if (!name.empty())
                {
                    // Find the shared memory region with this name
                    SharedSegmentMap::iterator iter = mSharedSegments.find(name);
                    if (iter != mSharedSegments.end())
                    {
                        mPixels = (unsigned char*)iter->second.mAddress;
                        mWidth = width;
                        mHeight = height;

                        mTextureWidth = texture_width;
                        mTextureHeight = texture_height;

                        mHost.resize(mWidth, mHeight);
                        // the destination buffer may be new: make sure the current frame is copied into it
                        mHost.invalidateFrame();
                    }
                }

                LLPluginMessage message(LLPLUGIN_MESSAGE_CLASS_MEDIA, "size_change_response");
                message.setValue("name", name);
                message.setValueS32("width", width);
                message.setValueS32("height", height);
                message.setValueS32("texture_width", texture_width);
                message.setValueS32("texture_height", texture_height);
                sendMessage(message);
            }
            else if (message_name == "set_language_code")
            {
                mHostLanguage = message_in.getValue("language");
            }
            else if (message_name == "load_uri")
            {
                mHost.navigate(message_in.getValue("uri"));
            }
            else if (message_name == "execute_javascript")
            {
                mHost.executeScript(message_in.getValue("code"));
            }
            else if (message_name == "set_cookie")
            {
                mHost.setCookie(message_in.getValue("uri"), message_in.getValue("name"), message_in.getValue("value"),
                                message_in.getValue("domain"), message_in.getValue("path"),
                                message_in.getValueBoolean("httponly"), message_in.getValueBoolean("secure"));
            }
            else if (message_name == "mouse_event")
            {
                std::string event = message_in.getValue("event");

                S32 x = message_in.getValueS32("x");
                S32 y = message_in.getValueS32("y");

                // only the left button is forwarded; the viewer draws its own context menu for the rest
                S32 button = message_in.getValueS32("button");

                if (event == "down" && button == 0)
                {
                    mHost.mouse(wv2::MouseKind::LeftDown, x, y);
                    mHost.focus();
                }
                else if (event == "up" && button == 0)
                {
                    mHost.mouse(wv2::MouseKind::LeftUp, x, y);
                }
                else if (event == "double_click")
                {
                    mHost.mouse(wv2::MouseKind::LeftDoubleClick, x, y);
                }
                else
                {
                    mHost.mouse(wv2::MouseKind::Move, x, y);
                }
            }
            else if (message_name == "scroll_event")
            {
                S32 x = message_in.getValueS32("x");
                S32 y = message_in.getValueS32("y");
                S32 clicks_x = message_in.getValueS32("clicks_x");
                S32 clicks_y = message_in.getValueS32("clicks_y");

                // viewer clicks: positive = down/right; wheel units: 120 per notch, positive = up/right
                mHost.wheel(x, y, clicks_x * 120, clicks_y * -120);
            }
            else if (message_name == "text_event" || message_name == "key_event")
            {
                LLSD native_key_data = message_in.getValueLLSD("native_key_data");
                U32 msg = ll_U32_from_sd(native_key_data["msg"]);
                U32 wparam = ll_U32_from_sd(native_key_data["w_param"]);
                U64 lparam = ll_U32_from_sd(native_key_data["l_param"]);
                nativeKey(msg, wparam, lparam);
            }
            else if (message_name == "enable_media_plugin_debugging")
            {
                mEnableMediaPluginDebugging = message_in.getValueBoolean("enable");
            }
            else if (message_name == "auth_response")
            {
                authResponse(message_in);
            }
            else if (message_name == "edit_undo")       { editCommand("undo"); }
            else if (message_name == "edit_redo")       { editCommand("redo"); }
            else if (message_name == "edit_cut")        { editCommand("cut"); }
            else if (message_name == "edit_copy")       { editCommand("copy"); }
            else if (message_name == "edit_paste")      { editCommand("paste"); }
            else if (message_name == "edit_delete")     { editCommand("delete"); }
            else if (message_name == "edit_select_all") { editCommand("selectAll"); }
            else if (message_name == "edit_show_source")
            {
                mHost.openDevTools();
            }
        }
        else if (message_class == LLPLUGIN_MESSAGE_CLASS_MEDIA_BROWSER)
        {
            if (message_name == "set_page_zoom_factor")
            {
                mHost.setZoom((F32)message_in.getValueReal("factor"));
            }
            else if (message_name == "browse_stop")
            {
                mHost.stop();
            }
            else if (message_name == "browse_reload")
            {
                mHost.reload();
            }
            else if (message_name == "browse_forward")
            {
                mHost.goForward();
            }
            else if (message_name == "browse_back")
            {
                mHost.goBack();
            }
            else if (message_name == "cookies_enabled")
            {
                // S24 : accepted and ignored; the engine keeps its own cookie policy
            }
            else if (message_name == "clear_cookies")
            {
                mHost.deleteAllCookies();
            }
            else if (message_name == "set_user_agent")
            {
                mUserAgentSubtring = message_in.getValue("user_agent");
            }
            else if (message_name == "show_web_inspector")
            {
                mHost.openDevTools();
            }
            else if (message_name == "plugins_enabled")
            {
                // S24 : accepted and ignored; WebView2 has no plugin setting
            }
            else if (message_name == "javascript_enabled")
            {
                mJavascriptEnabled = message_in.getValueBoolean("enable");
            }
            else if (message_name == "gpu_disabled")
            {
                mDisableGPU = message_in.getValueBoolean("disable");
            }
            else if (message_name == "proxy_setup")
            {
                mProxyEnabled = message_in.getValueBoolean("enable");
                mProxyHost = message_in.getValue("host");
                mProxyPort = message_in.getValueS32("port");
            }
        }
        else if (message_class == LLPLUGIN_MESSAGE_CLASS_MEDIA_TIME)
        {
            if (message_name == "set_volume")
            {
                mCurVolume = (F32)message_in.getValueReal("volume");
                // Per-instance volume needs the audio-session work of a later phase; mute at zero for now.
                mHost.setMuted(mCurVolume <= 0.0f);
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// Translate the viewer's native Win32 key messages into DevTools Input.dispatchKeyEvent calls,
// which act on the page directly and need no window focus.
namespace
{
std::string jsonEscape(const std::string& s)
{
    std::string out;
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
            else out += (char)c;
        }
    }
    return out;
}

std::string utf8FromCodepoint(unsigned cp)
{
    std::string s;
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    return s;
}

// DOM "key" and "code" for a Windows virtual key
void keyNames(unsigned vk, bool shift, std::string& key, std::string& code)
{
    key.clear(); code.clear();
    switch (vk)
    {
    case 0x0D: key = "Enter"; code = "Enter"; return;
    case 0x08: key = "Backspace"; code = "Backspace"; return;
    case 0x09: key = "Tab"; code = "Tab"; return;
    case 0x1B: key = "Escape"; code = "Escape"; return;
    case 0x20: key = " "; code = "Space"; return;
    case 0x2E: key = "Delete"; code = "Delete"; return;
    case 0x2D: key = "Insert"; code = "Insert"; return;
    case 0x24: key = "Home"; code = "Home"; return;
    case 0x23: key = "End"; code = "End"; return;
    case 0x21: key = "PageUp"; code = "PageUp"; return;
    case 0x22: key = "PageDown"; code = "PageDown"; return;
    case 0x25: key = "ArrowLeft"; code = "ArrowLeft"; return;
    case 0x26: key = "ArrowUp"; code = "ArrowUp"; return;
    case 0x27: key = "ArrowRight"; code = "ArrowRight"; return;
    case 0x28: key = "ArrowDown"; code = "ArrowDown"; return;
    case 0x10: case 0xA0: case 0xA1: key = "Shift"; code = "ShiftLeft"; return;
    case 0x11: case 0xA2: case 0xA3: key = "Control"; code = "ControlLeft"; return;
    case 0x12: case 0xA4: case 0xA5: key = "Alt"; code = "AltLeft"; return;
    case 0x14: key = "CapsLock"; code = "CapsLock"; return;
    default: break;
    }
    if (vk >= 0x70 && vk <= 0x7B) { code = "F" + std::to_string(vk - 0x70 + 1); key = code; return; }
    if (vk >= 'A' && vk <= 'Z')
    {
        key = std::string(1, (char)(shift ? vk : vk + 32));
        code = std::string("Key") + (char)vk;
        return;
    }
    if (vk >= '0' && vk <= '9')
    {
        key = std::string(1, (char)vk);
        code = std::string("Digit") + (char)vk;
        return;
    }
    key = "Unidentified";
}

// S24 : the live OS key state, not tracked from messages, so a modifier released while focus was
// elsewhere can never stay stuck
bool modifierDown(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}
} // namespace

void MediaPluginWebView2::nativeKey(U32 msg, U32 wparam, U64 lparam)
{
    enum { kKeyDown = 0x0100, kKeyUp = 0x0101, kChar = 0x0102, kSysKeyDown = 0x0104, kSysKeyUp = 0x0105, kSysChar = 0x0106, kImeChar = 0x0286 };

    if (msg == kKeyDown || msg == kSysKeyDown || msg == kKeyUp || msg == kSysKeyUp)
    {
        const bool down = (msg == kKeyDown || msg == kSysKeyDown);
        const bool shift = modifierDown(VK_SHIFT);
        const bool ctrl = modifierDown(VK_CONTROL);
        const bool alt = modifierDown(VK_MENU);

        std::string key, code;
        keyNames(wparam, shift, key, code);
        const bool repeat = down && ((lparam >> 30) & 1);

        std::ostringstream p;
        p << "{\"type\":\"" << (down ? "rawKeyDown" : "keyUp") << "\""
          << ",\"windowsVirtualKeyCode\":" << wparam
          << ",\"nativeVirtualKeyCode\":" << wparam
          << ",\"key\":\"" << jsonEscape(key) << "\""
          << ",\"code\":\"" << code << "\""
          << ",\"modifiers\":" << ((alt ? 1 : 0) | (ctrl ? 2 : 0) | (shift ? 8 : 0))
          << ",\"autoRepeat\":" << (repeat ? "true" : "false")
          << ",\"isSystemKey\":" << ((msg == kSysKeyDown || msg == kSysKeyUp) ? "true" : "false") << "}";
        mHost.callDevTools("Input.dispatchKeyEvent", p.str());
    }
    else if (msg == kChar || msg == kSysChar || msg == kImeChar)
    {
        // control characters (Ctrl+letter etc.) are not text; Enter and Tab are
        if (wparam < 0x20 && wparam != 0x0D && wparam != 0x09) return;
        if (wparam == 0x7F) return;

        unsigned cp = wparam;
        if (cp >= 0xD800 && cp <= 0xDBFF) { mHighSurrogate = cp; return; }
        if (cp >= 0xDC00 && cp <= 0xDFFF)
        {
            if (!mHighSurrogate) return;
            cp = 0x10000 + ((mHighSurrogate - 0xD800) << 10) + (cp - 0xDC00);
            mHighSurrogate = 0;
        }

        // Typed text never carries Ctrl/Alt: with them Chromium treats chars such as A and C as editing
        // shortcuts (select all, copy) instead of inserting them, and AltGr text arrives as plain chars.
        const int text_modifiers = modifierDown(VK_SHIFT) ? 8 : 0;
        const std::string text = (cp == 0x0D) ? std::string("\r") : utf8FromCodepoint(cp);
        std::ostringstream p;
        p << "{\"type\":\"char\",\"text\":\"" << jsonEscape(text) << "\",\"unmodifiedText\":\"" << jsonEscape(text) << "\""
          << ",\"windowsVirtualKeyCode\":" << wparam << ",\"modifiers\":" << text_modifiers << "}";
        mHost.callDevTools("Input.dispatchKeyEvent", p.str());
    }
}

////////////////////////////////////////////////////////////////////////////////
//
int init_media_plugin(LLPluginInstance::sendMessageFunction host_send_func,
    void* host_user_data,
    LLPluginInstance::sendMessageFunction* plugin_send_func,
    void** plugin_user_data)
{
    MediaPluginWebView2* self = new MediaPluginWebView2(host_send_func, host_user_data);
    *plugin_send_func = MediaPluginWebView2::staticReceiveMessage;
    *plugin_user_data = (void*)self;

    return 0;
}
