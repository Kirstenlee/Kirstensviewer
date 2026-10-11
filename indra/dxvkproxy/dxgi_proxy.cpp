/**
 * @file dxgi_proxy.cpp
 * @brief S24 DXVK proxy - sits in the install root as dxgi.dll, forwards
 * every export Kirstens-S24.exe or DXVK itself needs to either the genuine
 * Windows dxgi.dll or the bundled DXVK one, decided at first use.
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

#include "dxvkproxy_common.h"
#include <dxgi1_3.h>
#include <mutex>

namespace
{
    HINSTANCE g_hInst = nullptr;
    HMODULE g_real = nullptr;
    std::once_flag g_initFlag;

    HMODULE ensureReal()
    {
        std::call_once(g_initFlag, []()
        {
            g_real = S24DXVKProxy::resolveReal(g_hInst, L"dxgi.dll");
        });
        return g_real;
    }

    template <typename Fn>
    Fn resolveFn(const char* name)
    {
        HMODULE real = ensureReal();
        return real ? reinterpret_cast<Fn>(GetProcAddress(real, name)) : nullptr;
    }
}

extern "C" BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_hInst = hinst;
    }
    return TRUE;
}

// Exported under CreateDXGIFactory1 via dxgi_proxy.def - the only entry
// Kirstens-S24.exe's import table needs. The other three below are exported
// defensively alongside it - they're the rest of what DXVK's own dxgi.dll
// implements, covering any future code path and any third-party component
// sharing this process that calls into dxgi.dll directly.
extern "C" HRESULT WINAPI ProxyCreateDXGIFactory(REFIID riid, void** ppFactory)
{
    using Fn = decltype(&CreateDXGIFactory);
    static Fn real = resolveFn<Fn>("CreateDXGIFactory");
    return real ? real(riid, ppFactory) : E_FAIL;
}

extern "C" HRESULT WINAPI ProxyCreateDXGIFactory1(REFIID riid, void** ppFactory)
{
    using Fn = decltype(&CreateDXGIFactory1);
    static Fn real = resolveFn<Fn>("CreateDXGIFactory1");
    return real ? real(riid, ppFactory) : E_FAIL;
}

extern "C" HRESULT WINAPI ProxyCreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory)
{
    using Fn = decltype(&CreateDXGIFactory2);
    static Fn real = resolveFn<Fn>("CreateDXGIFactory2");
    return real ? real(Flags, riid, ppFactory) : E_FAIL;
}

extern "C" HRESULT WINAPI ProxyDXGIGetDebugInterface1(UINT Flags, REFIID riid, void** pDebug)
{
    using Fn = decltype(&DXGIGetDebugInterface1);
    static Fn real = resolveFn<Fn>("DXGIGetDebugInterface1");
    return real ? real(Flags, riid, pDebug) : E_FAIL;
}

// Undocumented in the public SDK headers - no header declares it, so a
// plain function-pointer typedef instead of decltype.
extern "C" HRESULT WINAPI ProxyDXGIDeclareAdapterRemovalSupport()
{
    using Fn = HRESULT(WINAPI*)();
    static Fn real = resolveFn<Fn>("DXGIDeclareAdapterRemovalSupport");
    return real ? real() : E_FAIL;
}
