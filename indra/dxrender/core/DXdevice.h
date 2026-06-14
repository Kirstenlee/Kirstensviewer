#pragma once
#include <d3d11.h>

class DXDevice
{
public:
    bool initialize();
    void shutdown();

    ID3D11Device* getDevice() const { return mDevice; }
    ID3D11DeviceContext* getContext() const { return mContext; }

private:
    ID3D11Device* mDevice = nullptr;
    ID3D11DeviceContext* mContext = nullptr;
};
