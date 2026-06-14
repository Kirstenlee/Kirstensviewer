#pragma once
#include <d3d11.h>

class DXBuffer
{
public:
    bool createVertexBuffer(size_t size, const void* data);
    bool createIndexBuffer(size_t size, const void* data);

    ID3D11Buffer* getBuffer() const { return mBuffer; }

private:
    ID3D11Buffer* mBuffer = nullptr;
};
