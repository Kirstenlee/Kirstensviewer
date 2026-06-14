#pragma once
#include <d3d11.h>
#include <string>

class DXShader
{
public:
    bool loadVertexShader(const std::string& path);
    bool loadPixelShader(const std::string& path);

    ID3D11VertexShader* getVS() const { return mVS; }
    ID3D11PixelShader* getPS() const { return mPS; }

private:
    ID3D11VertexShader* mVS = nullptr;
    ID3D11PixelShader* mPS = nullptr;
};
