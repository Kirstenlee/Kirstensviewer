#include "DXShader.h"
#include "DXDevice.h"
#include "llerror.h"
#include <d3dcompiler.h>

namespace
{
    bool compileHLSL(const std::string& source, const std::string& debugName, const char* entry_point, const char* target, ID3DBlob** out_blob)
    {
        ID3DBlob* error_blob = nullptr;
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

        HRESULT hr = D3DCompile(
            source.c_str(), source.size(),
            debugName.c_str(),
            nullptr, nullptr,
            entry_point, target,
            flags, 0,
            out_blob, &error_blob);

        if (FAILED(hr))
        {
            LL_WARNS("ShaderLoading") << "D3DCompile failed for " << debugName << ": "
                << (error_blob ? (const char*)error_blob->GetBufferPointer() : "unknown error") << LL_ENDL;
            if (error_blob)
            {
                error_blob->Release();
            }
            return false;
        }

        if (error_blob)
        {
            // Non-fatal warnings only - compile still succeeded.
            LL_INFOS("ShaderLoading") << "D3DCompile warnings for " << debugName << ": "
                << (const char*)error_blob->GetBufferPointer() << LL_ENDL;
            error_blob->Release();
        }
        return true;
    }
}

bool DXShader::compileVertexShader(const std::string& source, const std::string& debugName)
{
    ID3DBlob* blob = nullptr;
    if (!compileHLSL(source, debugName, "main", "vs_5_0", &blob))
    {
        return false;
    }

    HRESULT hr = gDXDevice.getDevice()->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &mVS);
    if (FAILED(hr))
    {
        LL_WARNS("ShaderLoading") << "CreateVertexShader failed for " << debugName << LL_ENDL;
        blob->Release();
        return false;
    }

    mVSBytecode = blob;
    return true;
}

bool DXShader::compilePixelShader(const std::string& source, const std::string& debugName)
{
    ID3DBlob* blob = nullptr;
    if (!compileHLSL(source, debugName, "main", "ps_5_0", &blob))
    {
        return false;
    }

    HRESULT hr = gDXDevice.getDevice()->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &mPS);
    blob->Release(); // no input layout involved on the pixel-shader side
    if (FAILED(hr))
    {
        LL_WARNS("ShaderLoading") << "CreatePixelShader failed for " << debugName << LL_ENDL;
        return false;
    }

    return true;
}

void DXShader::reset()
{
    if (mVS) { mVS->Release(); mVS = nullptr; }
    if (mPS) { mPS->Release(); mPS = nullptr; }
    if (mVSBytecode) { mVSBytecode->Release(); mVSBytecode = nullptr; }
}
