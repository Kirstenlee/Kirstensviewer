#include "DXSampler.h"
#include "DXDevice.h"
#include "llerror.h"
#include <algorithm>
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
    int sMaxAnisotropy = 8;
}

void DXSampler::setMaxAnisotropy(int level)
{
    level = std::clamp(level, 0, 16);
    if (level != sMaxAnisotropy)
    {
        sMaxAnisotropy = level;
        clear();
    }
}

ID3D11SamplerState* DXSampler::getOrCreate(int address_mode, int filter_option)
{
    int key = (address_mode << 8) | filter_option;
    auto iter = sCache.find(key);
    if (iter != sCache.end())
    {
        return iter->second;
    }

    // Level 0 is the "off" position - fall back to plain trilinear rather
    // than passing MaxAnisotropy=0, which D3D11_SAMPLER_DESC doesn't accept
    // (valid range is 1-16 regardless of Filter).
    bool anisotropic = (filter_option == 3) && (sMaxAnisotropy > 0);
    int effective_filter = (filter_option == 3 && !anisotropic) ? 2 : filter_option;

    D3D11_SAMPLER_DESC desc = {};
    desc.Filter = toFilter(effective_filter);
    desc.AddressU = toAddressMode(address_mode);
    desc.AddressV = toAddressMode(address_mode);
    desc.AddressW = toAddressMode(address_mode);
    desc.MaxAnisotropy = anisotropic ? sMaxAnisotropy : 1;
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
    // BORDER (not CLAMP): a sample whose UV falls outside the shadow map's
    // [0,1] range must read back as "no occluder" - as far from the light as
    // the comparison's convention represents, so it always passes and never
    // falsely shadows geometry outside that map's coverage. S24: this
    // codebase is reversed-Z (near=1.0/far=0.0, GREATER_EQUAL comparison -
    // see llrender.cpp's LLTexUnit::bind() comment) for every comparison
    // sampler currently created here (sun/spot/point shadows all pass
    // GREATER_EQUAL) - "far" is 0.0, not 1.0. The border color here was
    // previously hardcoded to white (1.0), correct only for a LESS_EQUAL/
    // standard-Z convention this codebase no longer uses anywhere - a stale
    // leftover from before the project's reversed-Z conversion. Sun/spot
    // shadows never visibly exposed this (huge cascade margins / a cone
    // frustum already shaped to the light), but point-light shadows' tight
    // 6-face cube pushes samples outside [0,1] constantly right at every
    // face boundary, where the wrong border (1.0) made GREATER_EQUAL fail
    // and falsely shadow fully-unoccluded geometry - producing a hard,
    // cube-shaped light cutoff instead of the light's natural radius/falloff
    // shape. Picking the border from func keeps LESS_EQUAL callers (none
    // currently) correct too, rather than hardcoding either convention.
    bool reversed_z = (func == D3D11_COMPARISON_GREATER_EQUAL || func == D3D11_COMPARISON_GREATER);
    float border_far = reversed_z ? 0.0f : 1.0f;
    desc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
    desc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
    desc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    desc.BorderColor[0] = border_far;
    desc.BorderColor[1] = border_far;
    desc.BorderColor[2] = border_far;
    desc.BorderColor[3] = border_far;
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
