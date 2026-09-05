#include "DXSampler.h"
#include "DXDevice.h"
#include "llerror.h"
#include <unordered_map>

namespace
{
    D3D11_TEXTURE_ADDRESS_MODE toAddressMode(int address_mode)
    {
        switch (address_mode)
        {
        case 1: return D3D11_TEXTURE_ADDRESS_MIRROR;
        case 2: return D3D11_TEXTURE_ADDRESS_CLAMP;
        default: return D3D11_TEXTURE_ADDRESS_WRAP;
        }
    }

    D3D11_FILTER toFilter(int filter_option)
    {
        switch (filter_option)
        {
        case 0: return D3D11_FILTER_MIN_MAG_MIP_POINT;
        case 1: return D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        case 3: return D3D11_FILTER_ANISOTROPIC;
        default: return D3D11_FILTER_MIN_MAG_MIP_LINEAR; // TRILINEAR
        }
    }

    std::unordered_map<int, ID3D11SamplerState*> sCache;
    std::unordered_map<int, ID3D11SamplerState*> sComparisonCache;
}

ID3D11SamplerState* DXSampler::getOrCreate(int address_mode, int filter_option)
{
    int key = (address_mode << 8) | filter_option;
    auto iter = sCache.find(key);
    if (iter != sCache.end())
    {
        return iter->second;
    }

    D3D11_SAMPLER_DESC desc = {};
    desc.Filter = toFilter(filter_option);
    desc.AddressU = toAddressMode(address_mode);
    desc.AddressV = toAddressMode(address_mode);
    desc.AddressW = toAddressMode(address_mode);
    desc.MaxAnisotropy = (filter_option == 3) ? 8 : 1;
    desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    desc.MinLOD = 0;
    desc.MaxLOD = D3D11_FLOAT32_MAX;

    ID3D11SamplerState* sampler = nullptr;
    HRESULT hr = gDXDevice.getDevice()->CreateSamplerState(&desc, &sampler);
    if (FAILED(hr))
    {
        LL_WARNS("Texture") << "CreateSamplerState failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        return nullptr;
    }

    sCache[key] = sampler;
    return sampler;
}

ID3D11SamplerState* DXSampler::getOrCreateComparison(D3D11_COMPARISON_FUNC func)
{
    int key = (int)func;
    auto iter = sComparisonCache.find(key);
    if (iter != sComparisonCache.end())
    {
        return iter->second;
    }

    D3D11_SAMPLER_DESC desc = {};
    // COMPARISON_MIN_MAG_LINEAR_MIP_POINT gives real hardware bilinear PCF
    // via .SampleCmp()/.SampleCmpLevelZero() - matches GL's shadow2D
    // GL_LINEAR-filtered-depth-compare default (a plain COMPARISON_POINT
    // filter would still be valid HLSL/D3D11, just harder-edged shadows,
    // not the softer PCF look this codebase's shaders already assume).
    desc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    // BORDER (not CLAMP) with a white border: matches GL's own shadow-map
    // edge convention (samples outside the shadow map's [0,1] UV range read
    // as depth=1.0, i.e. "as far as possible" - always passes a LESS_EQUAL
    // comparison against real scene depth, so geometry outside any shadow
    // map's coverage never gets incorrectly shadowed at the edges).
    desc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
    desc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
    desc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    desc.BorderColor[0] = 1.0f;
    desc.BorderColor[1] = 1.0f;
    desc.BorderColor[2] = 1.0f;
    desc.BorderColor[3] = 1.0f;
    desc.ComparisonFunc = func;
    desc.MinLOD = 0;
    desc.MaxLOD = D3D11_FLOAT32_MAX;

    ID3D11SamplerState* sampler = nullptr;
    HRESULT hr = gDXDevice.getDevice()->CreateSamplerState(&desc, &sampler);
    if (FAILED(hr))
    {
        LL_WARNS("Texture") << "CreateSamplerState (comparison) failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        return nullptr;
    }

    sComparisonCache[key] = sampler;
    return sampler;
}

void DXSampler::bindStatic(UINT slot, int address_mode, int filter_option)
{
    ID3D11SamplerState* sampler = getOrCreate(address_mode, filter_option);
    if (sampler)
    {
        gDXDevice.getContext()->PSSetSamplers(slot, 1, &sampler);
    }
}

void DXSampler::clear()
{
    for (auto& entry : sCache)
    {
        if (entry.second)
        {
            entry.second->Release();
        }
    }
    sCache.clear();

    for (auto& entry : sComparisonCache)
    {
        if (entry.second)
        {
            entry.second->Release();
        }
    }
    sComparisonCache.clear();
}
