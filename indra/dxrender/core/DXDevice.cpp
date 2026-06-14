#include "DXDevice.h"

bool DXDevice::initialize()
{
    // Placeholder — real implementation later
    return true;
}

void DXDevice::shutdown()
{
    if (mContext) mContext->Release();
    if (mDevice) mDevice->Release();
}
