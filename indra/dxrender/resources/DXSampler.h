#pragma once
#include <d3d11.h>

// Caches ID3D11SamplerState objects process-wide, keyed by
// (address_mode, filter_option). Uses plain integers matching
// LLTexUnit::eTextureAddressMode/eTextureFilterOptions (llrender/llrender.h)
// rather than including that header, so dxrender stays free of an
// llrender/GL dependency (same convention as DXVertexLayout's attribute
// bit table):
//   address_mode: 0=WRAP 1=MIRROR 2=CLAMP
//   filter_option: 0=POINT 1=BILINEAR 2=TRILINEAR 3=ANISOTROPIC
class DXSampler
{
public:
    static ID3D11SamplerState* getOrCreate(int address_mode, int filter_option);

    // User-facing anisotropy level (0-16; 0 disables anisotropic filtering
    // and falls back to trilinear, 1-16 matches D3D11_REQ_MAXANISOTROPY)
    // for any sampler created with filter_option==3 (TFO_ANISOTROPIC).
    // Clamps the input and, if the effective value actually changed,
    // clears the sampler cache so subsequent getOrCreate() calls rebuild
    // every cached sampler (aniso and non-aniso alike - cheap, lazy,
    // simpler than folding the level into the cache key) with the new
    // level baked in.
    static void setMaxAnisotropy(int level);

    // A genuinely different D3D11 object from the regular filtering samplers
    // above - HLSL's SamplerComparisonState (shadowUtil.hlsl's
    // shadowMap0-5Sampler, sampled via .SampleCmp()/.SampleCmpLevelZero(),
    // not .Sample()) requires a sampler created with a comparison filter
    // mode and a bound ComparisonFunc baked into the state object itself;
    // binding a regular sampler to such a register is undefined behavior.
    // Cached separately, keyed by comparison func alone - this codebase
    // only ever wants LESS_EQUAL (matching GL's shadow-sampler default
    // GL_LEQUAL), but keeping this generic costs nothing.
    static ID3D11SamplerState* getOrCreateComparison(D3D11_COMPARISON_FUNC func);

    // Binds a sampler directly to a fixed PS sampler slot, bypassing the
    // LLTexUnit::bind()-per-texture-channel convention (sampler register N
    // always paired with texture register N). Needed for shaders like
    // SMAA.hlsl that declare their own standalone SamplerState objects at
    // explicit high register slots. Plain D3D11 HLSL never auto-creates/
    // binds samplers from an inline `SamplerState X { Filter = ...; }`
    // initializer block - that syntax is state-object metadata only; the
    // app must still explicitly CreateSamplerState + PSSetSamplers to the
    // compiler-assigned slot, or the slot keeps whatever sampler a prior,
    // unrelated draw call left bound that frame.
    static void bindStatic(UINT slot, int address_mode, int filter_option);

    // Releases every cached sampler - call on full renderer shutdown.
    static void clear();
};
