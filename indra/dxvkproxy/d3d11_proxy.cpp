/**
 * @file d3d11_proxy.cpp
 * @brief S24 DXVK proxy - sits in the install root as d3d11.dll, forwards
 * every export Kirstens-S24.exe or DXVK itself needs to either the genuine
 * Windows d3d11.dll or the bundled DXVK one, decided at first use.
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
#include <d3d11.h>
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
            g_real = S24DXVKProxy::resolveReal(g_hInst, L"d3d11.dll");
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

// Exported under D3D11CreateDevice via d3d11_proxy.def - the only entry
// Kirstens-S24.exe's import table needs, kept here under a distinct C++
// name to avoid colliding with <d3d11.h>'s own dllimport-decorated
// declaration of the real name.
extern "C" HRESULT WINAPI ProxyD3D11CreateDevice(
    IDXGIAdapter* pAdapter,
    D3D_DRIVER_TYPE DriverType,
    HMODULE Software,
    UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    ID3D11Device** ppDevice,
    D3D_FEATURE_LEVEL* pFeatureLevel,
    ID3D11DeviceContext** ppImmediateContext)
{
    using Fn = decltype(&D3D11CreateDevice);
    static Fn real = resolveFn<Fn>("D3D11CreateDevice");
    if (!real)
    {
        return E_FAIL;
    }
    return real(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels,
                SDKVersion, ppDevice, pFeatureLevel, ppImmediateContext);
}

// Not currently called by this viewer, but exported defensively alongside
// D3D11CreateDevice above - it's the only other export DXVK's own d3d11.dll
// implements, so covering it costs nothing and avoids a missing-entry-point
// failure if a future code path starts using it.
extern "C" HRESULT WINAPI ProxyD3D11CreateDeviceAndSwapChain(
    IDXGIAdapter* pAdapter,
    D3D_DRIVER_TYPE DriverType,
    HMODULE Software,
    UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
    IDXGISwapChain** ppSwapChain,
    ID3D11Device** ppDevice,
    D3D_FEATURE_LEVEL* pFeatureLevel,
    ID3D11DeviceContext** ppImmediateContext)
{
    using Fn = decltype(&D3D11CreateDeviceAndSwapChain);
    static Fn real = resolveFn<Fn>("D3D11CreateDeviceAndSwapChain");
    if (!real)
    {
        return E_FAIL;
    }
    return real(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels,
                SDKVersion, pSwapChainDesc, ppSwapChain, ppDevice, pFeatureLevel, ppImmediateContext);
}
