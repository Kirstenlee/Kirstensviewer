#include "DXDevice.h"
#include "llerror.h"

DXDevice gDXDevice;

bool DXDevice::initialize(ID3D11Device* existing_device, ID3D11DeviceContext* existing_context)
{
    if (mDevice)
    {
        // already initialized
        return true;
    }

    if (existing_device && existing_context)
    {
        mDevice = existing_device;
        mContext = existing_context;
        mDevice->AddRef();
        mContext->AddRef();
        mFeatureLevel = mDevice->GetFeatureLevel();
        return true;
    }

    // No device to adopt (e.g. a single-adapter system, where
    // selectHighPerformanceAdapter() skips device creation entirely) -
    // create our own.
    D3D_FEATURE_LEVEL requested_levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        requested_levels,
        _countof(requested_levels),
        D3D11_SDK_VERSION,
        &mDevice,
        &mFeatureLevel,
        &mContext);

    if (FAILED(hr))
    {
        LL_WARNS("DXRender") << "D3D11CreateDevice failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        mDevice = nullptr;
        mContext = nullptr;
        return false;
    }

    return true;
}

void DXDevice::shutdown()
{
    if (mContext) { mContext->Release(); mContext = nullptr; }
    if (mDevice) { mDevice->Release(); mDevice = nullptr; }
}
