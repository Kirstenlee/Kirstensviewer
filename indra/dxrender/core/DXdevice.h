#pragma once
#include <d3d11.h>

// Wraps the ID3D11Device/ID3D11DeviceContext used by the DX_RENDER backend.
// initialize() can adopt an already-created device (llwindowwin32.cpp's
// selectHighPerformanceAdapter() creates one for GPU-selection purposes on
// multi-adapter systems) instead of creating a new one, so the DX_RENDER
// path never ends up with two competing devices on the same adapter.
class DXDevice
{
public:
    bool initialize(ID3D11Device* existing_device = nullptr, ID3D11DeviceContext* existing_context = nullptr);
    void shutdown();

    ID3D11Device* getDevice() const { return mDevice; }
    ID3D11DeviceContext* getContext() const { return mContext; }
    D3D_FEATURE_LEVEL getFeatureLevel() const { return mFeatureLevel; }

private:
    ID3D11Device* mDevice = nullptr;
    ID3D11DeviceContext* mContext = nullptr;
    D3D_FEATURE_LEVEL mFeatureLevel = D3D_FEATURE_LEVEL_11_0;
};

extern DXDevice gDXDevice;
