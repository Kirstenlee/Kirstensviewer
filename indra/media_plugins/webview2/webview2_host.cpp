/**
 * @file webview2_host.cpp
 * @brief Off-screen WebView2 host: composition hosting + Windows.Graphics.Capture.
 *
 * Copyright (c) 2025-2026 Kirsten's Viewer Contributors
 * SPDX-License-Identifier: MIT
 */
#include "webview2_host.h"

#include <windows.h>
#include <unknwn.h>
#include <wrl.h>
#include <d3d11_4.h>
#include <dxgi.h>
#include <DispatcherQueue.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <Windows.Graphics.DirectX.Direct3D11.interop.h>

#include "WebView2.h"
#include "WebView2EnvironmentOptions.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgd = winrt::Windows::Graphics::DirectX;
namespace wgd3 = winrt::Windows::Graphics::DirectX::Direct3D11;
namespace wuc = winrt::Windows::UI::Composition;

namespace
{
struct __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1")) IDxgiInterfaceAccess : ::IUnknown
{
    virtual HRESULT __stdcall GetInterface(REFIID id, void** object) = 0;
};

std::wstring ToWide(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string ToUtf8(const wchar_t* w)
{
    if (!w || !*w) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::string HrText(HRESULT hr)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%08lX", (unsigned long)hr);
    return buf;
}

// UI cursor names the viewer understands (same set media_plugin_cef maps to)
const char* CursorNameForSystemId(UINT32 id)
{
    switch (id)
    {
    case 32512: return "UI_CURSOR_ARROW";       // IDC_ARROW
    case 32513: return "UI_CURSOR_IBEAM";       // IDC_IBEAM
    case 32514: return "UI_CURSOR_WAIT";        // IDC_WAIT
    case 32515: return "UI_CURSOR_CROSS";       // IDC_CROSS
    case 32516: return "UI_CURSOR_ARROW";       // IDC_UPARROW
    case 32642: return "UI_CURSOR_SIZENWSE";    // IDC_SIZENWSE
    case 32643: return "UI_CURSOR_SIZENESW";    // IDC_SIZENESW
    case 32644: return "UI_CURSOR_SIZEWE";      // IDC_SIZEWE
    case 32645: return "UI_CURSOR_SIZENS";      // IDC_SIZENS
    case 32646: return "UI_CURSOR_SIZEALL";     // IDC_SIZEALL
    case 32648: return "UI_CURSOR_NOLOCKED";    // IDC_NO
    case 32649: return "UI_CURSOR_HAND";        // IDC_HAND
    case 32650: return "UI_CURSOR_WORKING";     // IDC_APPSTARTING
    default:    return "UI_CURSOR_ARROW";
    }
}

LRESULT CALLBACK HostWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProcW(hwnd, msg, wp, lp);
}
} // namespace

namespace wv2
{

struct Host::Impl
{
    Settings settings;
    Callbacks cb;
    bool started = false;
    bool ready = false;
    bool shuttingDown = false;
    std::atomic<int> curW{ 1024 }, curH{ 1024 };   // size the page is being rendered at (read by the frame thread)
    double zoom = 1.0;
    bool leftDown = false;
    bool muted = false;

    HWND hwnd = nullptr;

    // composition (a visual attached to no window target)
    wgd3::IDirect3DDevice d3dDevice{ nullptr };
    winrt::Windows::System::DispatcherQueueController dqc{ nullptr };
    wuc::Compositor compositor{ nullptr };
    wuc::ContainerVisual root{ nullptr };

    // WebView2
    ComPtr<ICoreWebView2Environment> env;
    ComPtr<ICoreWebView2CompositionController> comp;
    ComPtr<ICoreWebView2Controller> ctrl;
    ComPtr<ICoreWebView2> wv;
    EventRegistrationToken tokNavStart{}, tokNavDone{}, tokSource{}, tokTitle{}, tokNewWin{}, tokPerm{}, tokCursor{}, tokStatus{};

    // capture
    winrt::com_ptr<ID3D11Device> dev;
    winrt::com_ptr<ID3D11DeviceContext> ctx;
    wgc::Direct3D11CaptureFramePool pool{ nullptr };
    wgc::GraphicsCaptureSession session{ nullptr };
    winrt::event_token frameToken{};
    std::mutex mtx;                       // guards ctx use, staging and the counters below
    winrt::com_ptr<ID3D11Texture2D> staging;
    int stagingW = 0, stagingH = 0;
    int poolW = 0, poolH = 0;
    uint64_t frameSeq = 0, lastSeq = 0;

    // actions requested before the control exists
    std::string pendingUri;
    std::vector<std::string> pendingScripts;

    void log(const std::string& s) { if (cb.onLog) cb.onLog(s); }
    void fail(const std::string& why)
    {
        log("WebView2 start failed: " + why);
        if (cb.onReady) cb.onReady(false, why);
    }

    void createEnvironment();
    void onController(ICoreWebView2CompositionController* cc);
    void configure();
    void hookEvents();
    void startCapture();
    void onFrame(wgc::Direct3D11CaptureFramePool const& p);
};

Host::Host() : m(new Impl) {}
Host::~Host() { shutdown(); }

bool Host::start(const Settings& s, Callbacks c)
{
    m->settings = s;
    m->cb = std::move(c);
    m->curW = s.width > 0 ? s.width : 1024;
    m->curH = s.height > 0 ? s.height : 1024;
    m->zoom = s.zoom;

    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (hrCo == RPC_E_CHANGED_MODE)
    {
        m->fail("this thread is already a multithreaded COM apartment");
        return false;
    }
    try
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
    }
    catch (winrt::hresult_error const&) { /* already initialised */ }

    try
    {
        DispatcherQueueOptions dqo{ sizeof(dqo), DQTYPE_THREAD_CURRENT, DQTAT_COM_NONE };
        HRESULT hr = CreateDispatcherQueueController(dqo, reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(m->dqc)));
        if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS) && hr != E_ACCESSDENIED)
        {
            m->fail("CreateDispatcherQueueController " + HrText(hr));
            return false;
        }
        m->compositor = wuc::Compositor();
        m->root = m->compositor.CreateContainerVisual();
        m->root.Size({ (float)m->curW.load(), (float)m->curH.load() });
    }
    catch (winrt::hresult_error const& e)
    {
        m->fail("composition setup " + HrText(e.code().value));
        return false;
    }

    static ATOM s_class = 0;
    if (!s_class)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = HostWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"S24WebView2Host";
        s_class = RegisterClassW(&wc);
    }
    // The control needs a parent HWND, but nothing is ever drawn to it and it is never shown.
    m->hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"S24WebView2Host", L"", WS_POPUP, 0, 0, m->curW.load(), m->curH.load(),
                              nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!m->hwnd)
    {
        m->fail("CreateWindow " + HrText(HRESULT_FROM_WIN32(GetLastError())));
        return false;
    }

    m->started = true;
    m->createEnvironment();
    return true;
}

void Host::Impl::createEnvironment()
{
    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    std::wstring args = L"--autoplay-policy=no-user-gesture-required --disable-background-timer-throttling "
                        L"--disable-renderer-backgrounding --disable-backgrounding-occluded-windows";
    if (settings.disableGpu) args += L" --disable-gpu";
    if (!settings.proxyHostPort.empty()) args += L" --proxy-server=" + ToWide(settings.proxyHostPort);
    options->put_AdditionalBrowserArguments(args.c_str());
    if (!settings.language.empty()) options->put_Language(ToWide(settings.language).c_str());

    std::wstring udf = ToWide(settings.userDataFolder);
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, udf.empty() ? nullptr : udf.c_str(), options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* e) -> HRESULT
            {
                if (shuttingDown) return S_OK;
                if (FAILED(result) || !e) { fail("environment creation " + HrText(result)); return S_OK; }
                env = e;
                ComPtr<ICoreWebView2Environment3> env3;
                if (FAILED(env.As(&env3))) { fail("WebView2 runtime too old for composition hosting"); return S_OK; }
                env3->CreateCoreWebView2CompositionController(hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
                        [this](HRESULT r2, ICoreWebView2CompositionController* cc) -> HRESULT
                        {
                            if (shuttingDown) return S_OK;
                            if (FAILED(r2) || !cc) { fail("controller creation " + HrText(r2)); return S_OK; }
                            onController(cc);
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
    if (FAILED(hr))
    {
        // 0x80070002 (file not found) is the usual "WebView2 runtime is not installed"
        fail("CreateCoreWebView2EnvironmentWithOptions " + HrText(hr));
    }
}

void Host::Impl::onController(ICoreWebView2CompositionController* cc)
{
    comp = cc;
    comp->put_RootVisualTarget(winrt::get_unknown(root));
    comp.As(&ctrl);
    RECT rc{ 0, 0, curW.load(), curH.load() };
    ctrl->put_Bounds(rc);
    ctrl->put_IsVisible(TRUE);
    ctrl->get_CoreWebView2(&wv);

    ComPtr<ICoreWebView2Controller3> ctrl3;
    if (SUCCEEDED(ctrl.As(&ctrl3)))
    {
        ctrl3->put_ShouldDetectMonitorScaleChanges(FALSE);
        ctrl3->put_RasterizationScale(1.0);
    }

    configure();
    hookEvents();

    try { startCapture(); }
    catch (winrt::hresult_error const& e) { fail("capture setup " + HrText(e.code().value) + " " + winrt::to_string(e.message())); return; }

    ctrl->put_ZoomFactor(zoom);
    ready = true;
    if (cb.onReady) cb.onReady(true, "");

    for (auto& js : pendingScripts) wv->ExecuteScript(ToWide(js).c_str(), nullptr);
    pendingScripts.clear();
    if (!pendingUri.empty()) { wv->Navigate(ToWide(pendingUri).c_str()); pendingUri.clear(); }
}

void Host::Impl::configure()
{
    ComPtr<ICoreWebView2Settings> st;
    if (FAILED(wv->get_Settings(&st)) || !st) return;
    st->put_IsScriptEnabled(settings.javascript ? TRUE : FALSE);
    st->put_AreDefaultScriptDialogsEnabled(FALSE);          // alert/confirm/prompt are suppressed (as in the CEF plugin)
    st->put_AreDefaultContextMenusEnabled(FALSE);           // the viewer draws its own context menu
    st->put_IsStatusBarEnabled(FALSE);
    st->put_IsZoomControlEnabled(FALSE);
    st->put_IsBuiltInErrorPageEnabled(TRUE);

    if (!settings.userAgentSuffix.empty())
    {
        ComPtr<ICoreWebView2Settings2> st2;
        if (SUCCEEDED(st.As(&st2)))
        {
            LPWSTR ua = nullptr;
            if (SUCCEEDED(st2->get_UserAgent(&ua)) && ua)
            {
                std::wstring full = std::wstring(ua) + L" " + ToWide(settings.userAgentSuffix);
                CoTaskMemFree(ua);
                st2->put_UserAgent(full.c_str());
            }
        }
    }
}

void Host::Impl::hookEvents()
{
    wv->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
        [this](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* a) -> HRESULT
        {
            LPWSTR uri = nullptr; BOOL user = FALSE, redirected = FALSE;
            a->get_Uri(&uri); a->get_IsUserInitiated(&user); a->get_IsRedirected(&redirected);
            std::string u = ToUtf8(uri);
            if (uri) CoTaskMemFree(uri);
            if (cb.onNavigationStarting && cb.onNavigationStarting(u, user != FALSE, redirected != FALSE))
            {
                a->put_Cancel(TRUE);
            }
            return S_OK;
        }).Get(), &tokNavStart);

    wv->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
        [this](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* a) -> HRESULT
        {
            BOOL ok = FALSE; a->get_IsSuccess(&ok);
            int status = ok ? 200 : 0;
            ComPtr<ICoreWebView2NavigationCompletedEventArgs2> a2;
            if (SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&a2)))) { int code = 0; if (SUCCEEDED(a2->get_HttpStatusCode(&code)) && code) status = code; }
            LPWSTR src = nullptr; sender->get_Source(&src);
            std::string u = ToUtf8(src);
            if (src) CoTaskMemFree(src);
            if (cb.onNavigationCompleted) cb.onNavigationCompleted(status, u, ok != FALSE);
            return S_OK;
        }).Get(), &tokNavDone);

    wv->add_SourceChanged(Callback<ICoreWebView2SourceChangedEventHandler>(
        [this](ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs*) -> HRESULT
        {
            LPWSTR src = nullptr; sender->get_Source(&src);
            std::string u = ToUtf8(src);
            if (src) CoTaskMemFree(src);
            if (cb.onLocationChanged) cb.onLocationChanged(u);
            return S_OK;
        }).Get(), &tokSource);

    wv->add_DocumentTitleChanged(Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
        [this](ICoreWebView2* sender, IUnknown*) -> HRESULT
        {
            LPWSTR t = nullptr; sender->get_DocumentTitle(&t);
            std::string s = ToUtf8(t);
            if (t) CoTaskMemFree(t);
            if (cb.onTitleChanged) cb.onTitleChanged(s);
            return S_OK;
        }).Get(), &tokTitle);

    wv->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
        [this](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* a) -> HRESULT
        {
            LPWSTR uri = nullptr; a->get_Uri(&uri);
            std::string u = ToUtf8(uri);
            if (uri) CoTaskMemFree(uri);
            a->put_Handled(TRUE);   // never open a native window; the viewer decides what to do with it
            if (cb.onNewWindow) cb.onNewWindow(u, "_blank");
            return S_OK;
        }).Get(), &tokNewWin);

    // no camera / microphone / notification / clipboard prompts (the CEF plugin also disabled media streams)
    wv->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>(
        [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* a) -> HRESULT
        {
            a->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
            return S_OK;
        }).Get(), &tokPerm);

    comp->add_CursorChanged(Callback<ICoreWebView2CursorChangedEventHandler>(
        [this](ICoreWebView2CompositionController* sender, IUnknown*) -> HRESULT
        {
            UINT32 id = 32512;
            sender->get_SystemCursorId(&id);
            if (cb.onCursor) cb.onCursor(CursorNameForSystemId(id));
            return S_OK;
        }).Get(), &tokCursor);

    ComPtr<ICoreWebView2_12> wv12;
    if (SUCCEEDED(wv.As(&wv12)))
    {
        wv12->add_StatusBarTextChanged(Callback<ICoreWebView2StatusBarTextChangedEventHandler>(
            [this](ICoreWebView2* sender, IUnknown*) -> HRESULT
            {
                ComPtr<ICoreWebView2_12> s12;
                if (SUCCEEDED(sender->QueryInterface(IID_PPV_ARGS(&s12))))
                {
                    LPWSTR t = nullptr; s12->get_StatusBarText(&t);
                    std::string s = ToUtf8(t);
                    if (t) CoTaskMemFree(t);
                    if (cb.onStatusText) cb.onStatusText(s);
                }
                return S_OK;
            }).Get(), &tokStatus);
    }
}

void Host::Impl::startCapture()
{
    D3D_FEATURE_LEVEL fl;
    winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                           nullptr, 0, D3D11_SDK_VERSION, dev.put(), &fl, ctx.put()));
    if (auto mt = dev.try_as<ID3D11Multithread>()) mt->SetMultithreadProtected(TRUE);

    auto dxgi = dev.as<IDXGIDevice>();
    winrt::com_ptr<::IInspectable> insp;
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), insp.put()));
    d3dDevice = insp.as<wgd3::IDirect3DDevice>();

    auto item = wgc::GraphicsCaptureItem::CreateFromVisual(root);
    poolW = item.Size().Width; poolH = item.Size().Height;
    pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(d3dDevice, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, 3, item.Size());
    frameToken = pool.FrameArrived([this](wgc::Direct3D11CaptureFramePool const& p, winrt::Windows::Foundation::IInspectable const&) { onFrame(p); });
    session = pool.CreateCaptureSession(item);
    session.IsCursorCaptureEnabled(false);
    session.StartCapture();
}

// Runs on a thread-pool thread (free-threaded frame pool).
void Host::Impl::onFrame(wgc::Direct3D11CaptureFramePool const& p)
{
    if (shuttingDown) return;
    try
    {
        auto frame = p.TryGetNextFrame();
        if (!frame) return;

        auto cs = frame.ContentSize();
        const int wantW = curW.load(), wantH = curH.load();
        if (cs.Width != wantW || cs.Height != wantH)
        {
            // A frame rendered before the latest resize: drop it, and make sure the pool is the size we want.
            std::lock_guard<std::mutex> lk(mtx);
            if (poolW != wantW || poolH != wantH)
            {
                poolW = wantW; poolH = wantH;
                p.Recreate(d3dDevice, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, 3, { wantW, wantH });
            }
            return;
        }

        winrt::com_ptr<IDxgiInterfaceAccess> access;
        winrt::check_hresult(winrt::get_unknown(frame.Surface())->QueryInterface(__uuidof(IDxgiInterfaceAccess), access.put_void()));
        winrt::com_ptr<ID3D11Texture2D> tex;
        winrt::check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D), tex.put_void()));
        D3D11_TEXTURE2D_DESC d; tex->GetDesc(&d);

        std::lock_guard<std::mutex> lk(mtx);
        if (!staging || stagingW != (int)d.Width || stagingH != (int)d.Height)
        {
            D3D11_TEXTURE2D_DESC sd = d;
            sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0;
            staging = nullptr;
            winrt::check_hresult(dev->CreateTexture2D(&sd, nullptr, staging.put()));
            stagingW = (int)d.Width; stagingH = (int)d.Height;
        }
        ctx->CopyResource(staging.get(), tex.get());
        ++frameSeq;
    }
    catch (winrt::hresult_error const&)
    {
        // device lost / pool closing during shutdown: the next frame (or shutdown) sorts it out
    }
}

bool Host::pollFrame(uint8_t* dst, int width, int height, bool flipY)
{
    if (!m->ready || !dst || width <= 0 || height <= 0) return false;
    std::lock_guard<std::mutex> lk(m->mtx);
    if (m->frameSeq == m->lastSeq || !m->staging) return false;
    if (m->stagingW != width || m->stagingH != height) return false;   // resize in flight
    D3D11_MAPPED_SUBRESOURCE map;
    if (FAILED(m->ctx->Map(m->staging.get(), 0, D3D11_MAP_READ, 0, &map))) return false;
    const size_t rowBytes = (size_t)width * 4;
    const uint8_t* src = static_cast<const uint8_t*>(map.pData);
    for (int y = 0; y < height; ++y)
    {
        uint8_t* d = dst + (size_t)(flipY ? (height - 1 - y) : y) * rowBytes;
        memcpy(d, src + (size_t)y * map.RowPitch, rowBytes);
    }
    m->ctx->Unmap(m->staging.get(), 0);
    m->lastSeq = m->frameSeq;
    return true;
}

void Host::pump()
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

bool Host::ready() const { return m->ready; }

void Host::resize(int w, int h)
{
    if (w <= 0 || h <= 0) return;
    if (w == m->curW.load() && h == m->curH.load()) return;
    m->curW = w; m->curH = h;
    if (m->root) m->root.Size({ (float)w, (float)h });
    if (m->hwnd) SetWindowPos(m->hwnd, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (m->ctrl) { RECT rc{ 0, 0, w, h }; m->ctrl->put_Bounds(rc); }
    if (m->pool)
    {
        // Resize the capture pool now: a static page produces no further frames after the one that
        // reveals the new size, so waiting for that frame would leave the texture blank.
        std::lock_guard<std::mutex> lk(m->mtx);
        m->poolW = w; m->poolH = h;
        try { m->pool.Recreate(m->d3dDevice, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, 3, { w, h }); }
        catch (winrt::hresult_error const&) {}
    }
}

void Host::invalidateFrame()
{
    std::lock_guard<std::mutex> lk(m->mtx);
    m->lastSeq = m->frameSeq > 0 ? m->frameSeq - 1 : 0;
}

void Host::navigate(const std::string& uri)
{
    if (!m->wv) { m->pendingUri = uri; return; }
    m->wv->Navigate(ToWide(uri).c_str());
}

void Host::executeScript(const std::string& js)
{
    if (!m->wv) { m->pendingScripts.push_back(js); return; }
    m->wv->ExecuteScript(ToWide(js).c_str(), nullptr);
}

void Host::goBack() { if (m->wv) m->wv->GoBack(); }
void Host::goForward() { if (m->wv) m->wv->GoForward(); }
void Host::reload() { if (m->wv) m->wv->Reload(); }
void Host::stop() { if (m->wv) m->wv->Stop(); }

bool Host::canGoBack() const { BOOL b = FALSE; if (m->wv) m->wv->get_CanGoBack(&b); return b != FALSE; }
bool Host::canGoForward() const { BOOL b = FALSE; if (m->wv) m->wv->get_CanGoForward(&b); return b != FALSE; }

void Host::setZoom(double factor)
{
    m->zoom = factor;
    if (m->ctrl) m->ctrl->put_ZoomFactor(factor);
}

void Host::setMuted(bool muted)
{
    m->muted = muted;
    ComPtr<ICoreWebView2_8> w8;
    if (m->wv && SUCCEEDED(m->wv.As(&w8))) w8->put_IsMuted(muted ? TRUE : FALSE);
}

void Host::focus()
{
    if (m->ctrl) m->ctrl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}

void Host::openDevTools() { if (m->wv) m->wv->OpenDevToolsWindow(); }

void Host::deleteAllCookies()
{
    ComPtr<ICoreWebView2_2> w2;
    ComPtr<ICoreWebView2CookieManager> cm;
    if (m->wv && SUCCEEDED(m->wv.As(&w2)) && SUCCEEDED(w2->get_CookieManager(&cm))) cm->DeleteAllCookies();
}

void Host::setCookie(const std::string& uri, const std::string& name, const std::string& value,
                     const std::string& domain, const std::string& path, bool httpOnly, bool secure)
{
    ComPtr<ICoreWebView2_2> w2;
    ComPtr<ICoreWebView2CookieManager> cm;
    if (!m->wv || FAILED(m->wv.As(&w2)) || FAILED(w2->get_CookieManager(&cm))) return;
    std::wstring dom = ToWide(domain);
    if (dom.empty())
    {
        // derive the host from the URI
        size_t s = uri.find("://");
        std::string host = uri.substr(s == std::string::npos ? 0 : s + 3);
        host = host.substr(0, host.find_first_of("/:?#"));
        dom = ToWide(host);
    }
    ComPtr<ICoreWebView2Cookie> cookie;
    if (SUCCEEDED(cm->CreateCookie(ToWide(name).c_str(), ToWide(value).c_str(), dom.c_str(), ToWide(path.empty() ? "/" : path).c_str(), &cookie)) && cookie)
    {
        cookie->put_IsHttpOnly(httpOnly ? TRUE : FALSE);
        cookie->put_IsSecure(secure ? TRUE : FALSE);
        cm->AddOrUpdateCookie(cookie.Get());
    }
}

void Host::mouse(MouseKind kind, int x, int y)
{
    if (!m->comp) return;
    COREWEBVIEW2_MOUSE_EVENT_KIND k = COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE;
    switch (kind)
    {
    case MouseKind::Move:            k = COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE; break;
    case MouseKind::LeftDown:        k = COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOWN; m->leftDown = true; break;
    case MouseKind::LeftUp:          k = COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP; break;
    case MouseKind::LeftDoubleClick: k = COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOUBLE_CLICK; break;
    case MouseKind::Leave:           k = COREWEBVIEW2_MOUSE_EVENT_KIND_LEAVE; break;
    }
    COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS keys = m->leftDown ? COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_LEFT_BUTTON : COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE;
    if (kind == MouseKind::LeftUp) { m->leftDown = false; keys = COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE; }
    POINT pt{ x, y };
    m->comp->SendMouseInput(k, keys, 0, pt);
}

void Host::wheel(int x, int y, int deltaX, int deltaY)
{
    if (!m->comp) return;
    POINT pt{ x, y };
    COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS keys = m->leftDown ? COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_LEFT_BUTTON : COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE;
    if (deltaY) m->comp->SendMouseInput(COREWEBVIEW2_MOUSE_EVENT_KIND_WHEEL, keys, (UINT32)deltaY, pt);
    if (deltaX) m->comp->SendMouseInput(COREWEBVIEW2_MOUSE_EVENT_KIND_HORIZONTAL_WHEEL, keys, (UINT32)deltaX, pt);
}

void Host::callDevTools(const std::string& method, const std::string& jsonParams)
{
    if (!m->wv) return;
    m->wv->CallDevToolsProtocolMethod(ToWide(method).c_str(), ToWide(jsonParams).c_str(),
        Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
            [](HRESULT, LPCWSTR) -> HRESULT { return S_OK; }).Get());
}

void Host::shutdown()
{
    if (m->shuttingDown) return;
    m->shuttingDown = true;
    m->ready = false;
    try
    {
        if (m->wv)
        {
            m->wv->remove_NavigationStarting(m->tokNavStart);
            m->wv->remove_NavigationCompleted(m->tokNavDone);
            m->wv->remove_SourceChanged(m->tokSource);
            m->wv->remove_DocumentTitleChanged(m->tokTitle);
            m->wv->remove_NewWindowRequested(m->tokNewWin);
            m->wv->remove_PermissionRequested(m->tokPerm);
            ComPtr<ICoreWebView2_12> w12;
            if (SUCCEEDED(m->wv.As(&w12))) w12->remove_StatusBarTextChanged(m->tokStatus);
        }
        if (m->comp) m->comp->remove_CursorChanged(m->tokCursor);
        if (m->session) { m->session.Close(); m->session = nullptr; }
        if (m->pool) { m->pool.FrameArrived(m->frameToken); m->pool.Close(); m->pool = nullptr; }
        if (m->ctrl) m->ctrl->Close();
    }
    catch (winrt::hresult_error const&) {}

    m->wv.Reset(); m->ctrl.Reset(); m->comp.Reset(); m->env.Reset();
    m->root = nullptr; m->compositor = nullptr;
    if (m->dqc) { try { m->dqc.ShutdownQueueAsync(); } catch (...) {} m->dqc = nullptr; }
    if (m->hwnd) { DestroyWindow(m->hwnd); m->hwnd = nullptr; }
    {
        std::lock_guard<std::mutex> lk(m->mtx);
        m->staging = nullptr; m->ctx = nullptr; m->dev = nullptr;
    }
}

} // namespace wv2
