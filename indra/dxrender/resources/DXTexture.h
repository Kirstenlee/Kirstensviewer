#pragma once
#include <d3d11.h>

class DXTexture
{
public:
    bool createFromMemory(const void* data, int width, int height);
    ID3D11ShaderResourceView* getSRV() const { return mSRV; }

private:
    ID3D11Texture2D* mTexture = nullptr;
    ID3D11ShaderResourceView* mSRV = nullptr;
};
