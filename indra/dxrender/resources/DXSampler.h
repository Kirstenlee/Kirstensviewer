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

    // S24 (2026-08-09, task #124): a genuinely different D3D11 object from
    // the regular filtering samplers above - HLSL's SamplerComparisonState
    // (shadowUtil.hlsl's shadowMap0-5Sampler, sampled via .SampleCmp()/
    // .SampleCmpLevelZero() inside pcfShadow()/pcfSpotShadow(), not
    // .Sample()) requires a sampler created with a comparison filter mode
    // and a bound ComparisonFunc baked into the state object itself -
    // binding a regular sampler to a register a shader declares as
    // SamplerComparisonState is undefined behavior (confirmed via D3D11
    // debug-layer warning: "expects a Sampler configured for comparison
    // filtering... but the sampler bound at this slot is configured for
    // default filtering"). Cached separately from the regular cache above,
    // keyed by comparison func alone - this codebase only ever wants
    // LESS_EQUAL (matching GL's shadow-sampler default GL_LEQUAL compare
    // mode), but keeping this generic costs nothing.
    static ID3D11SamplerState* getOrCreateComparison(D3D11_COMPARISON_FUNC func);

    // S24 (2026-09-04): binds a sampler directly to a fixed PS sampler slot,
    // bypassing the LLTexUnit::bind()-per-texture-channel convention this
    // codebase otherwise always uses (sampler register N always paired with
    // texture register N). Needed for shaders like SMAA.hlsl that declare
    // their own standalone SamplerState objects (not tied 1:1 to a texture
    // channel) at explicit high register slots - see that file's own
    // LinearSampler/PointSampler comment. Plain D3D11 HLSL (no Effects11
    // framework) never auto-creates/binds samplers from an inline
    // initializer block like `SamplerState X { Filter = ...; }` - that
    // syntax is state-object metadata only; the app must still explicitly
    // CreateSamplerState + PSSetSamplers to the compiler-assigned slot, or
    // the slot keeps whatever sampler a prior, unrelated draw call left
    // bound that frame.
    static void bindStatic(UINT slot, int address_mode, int filter_option);

    // Releases every cached sampler - call on full renderer shutdown.
    static void clear();
};
