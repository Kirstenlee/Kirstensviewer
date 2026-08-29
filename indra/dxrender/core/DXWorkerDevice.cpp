// S24 (2026-08-26, task #260): llwin32headers.h MUST come first - it
// sequences winsock2.h/ws2tcpip.h/windows.h correctly (WIN32_LEAN_AND_MEAN +
// winsock2 before windows.h). This is the first dxrender file to combine a
// raw <d3d11.h>/<dxgi.h> include with llcommon's threading headers
// (llthread.h -> llapr.h -> APR's own winsock2 usage) in one translation
// unit - without this, windows.h gets pulled in raw (legacy winsock.h) by
// the d3d11/dxgi headers first, and ws2tcpip.h later fails to compile.
#include "llwin32headers.h"
#include "DXWorkerDevice.h"
#include "DXDevice.h"
#include "DXSharedResource.h"
#include "llerror.h"
#include "llthread.h"
#include <dxgi.h>
#include <memory>

DXWorkerDevice::~DXWorkerDevice()
{
    if (mContext) { mContext->Release(); mContext = nullptr; }
    if (mDevice) { mDevice->Release(); mDevice = nullptr; }
}

bool DXWorkerDevice::initialize()
{
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)) || !factory)
    {
        LL_WARNS_ONCE("DXRender") << "DXWorkerDevice: CreateDXGIFactory1 failed" << LL_ENDL;
        return false;
    }

    // S24 (2026-08-26, task #260): match the adapter LUID by manual
    // enumeration (same shape as LLWindowWin32::selectHighPerformanceAdapter(),
    // llwindow/llwindowwin32.cpp) rather than IDXGIFactory4::EnumAdapterByLuid -
    // no need for a newer factory interface when this pattern is already
    // proven in this codebase.
    const LUID target_luid = gDXDevice.getAdapterLuid();
    IDXGIAdapter1* matched_adapter = nullptr;
    for (UINT i = 0; ; ++i)
    {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }
        if (!adapter)
        {
            continue;
        }

        DXGI_ADAPTER_DESC1 desc;
        if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
            desc.AdapterLuid.LowPart == target_luid.LowPart &&
            desc.AdapterLuid.HighPart == target_luid.HighPart)
        {
            matched_adapter = adapter;
            break;
        }
        adapter->Release();
    }
    factory->Release();

    if (!matched_adapter)
    {
        LL_WARNS_ONCE("DXRender") << "DXWorkerDevice: failed to find gDXDevice's adapter by LUID" << LL_ENDL;
        return false;
    }

    D3D_FEATURE_LEVEL requested_levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL got_level;
    HRESULT hr = D3D11CreateDevice(
        matched_adapter,
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        DXDevice::sDebugLayerEnabled ? D3D11_CREATE_DEVICE_DEBUG : 0,
        requested_levels,
        _countof(requested_levels),
        D3D11_SDK_VERSION,
        &mDevice,
        &got_level,
        &mContext);

    matched_adapter->Release();

    if (FAILED(hr))
    {
        LL_WARNS_ONCE("DXRender") << "DXWorkerDevice: D3D11CreateDevice failed, hr=0x"
            << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        mDevice = nullptr;
        mContext = nullptr;
        return false;
    }

    return true;
}

DXWorkerDevice* DXWorkerDevice::getForCurrentThread()
{
    if (on_main_thread())
    {
        return nullptr;
    }
    if (DXSharedResource::isSharingDisabled())
    {
        return nullptr;
    }

    // S24 (2026-08-26, task #260): thread_local unique_ptr, not a
    // ThreadPoolBase::run() override - works identically for ANY thread (see
    // this class's header comment for why that matters), and cleans up
    // automatically at thread exit via ordinary thread_local destruction
    // (unique_ptr's destructor releases the device/context). A cached
    // nullptr instance on failure (tried==true, instance==nullptr) means a
    // broken-driver system doesn't retry a full D3D11CreateDevice on every
    // single texture create.
    thread_local bool tried = false;
    thread_local std::unique_ptr<DXWorkerDevice> instance;

    if (!tried)
    {
        tried = true;
        auto candidate = std::unique_ptr<DXWorkerDevice>(new DXWorkerDevice());
        if (candidate->initialize())
        {
            instance = std::move(candidate);
        }
    }

    return instance.get();
}
