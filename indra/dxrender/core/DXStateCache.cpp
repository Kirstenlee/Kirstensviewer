#include "DXStateCache.h"
#include "DXDevice.h"
#include "llerror.h"
#include <unordered_map>
#include <cmath>

uint64_t DXStateCache::sRTVGeneration = 0;
bool DXStateCache::sWireframeScopeActive = false;

namespace
{
    // D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED is never a real topology any caller
    // passes, so it's a safe "nothing bound yet" sentinel.
    D3D11_PRIMITIVE_TOPOLOGY sLastTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

    // Keyed by this packed struct rather than a fixed bool array since
    // polygon-offset needs real float/int values, not just flags. Mirrors
    // how sBlendState/sDepthStencilState work below.
    struct RasterizerKey
    {
        bool cull_enabled;
        bool scissor_enabled;
        bool depth_clamp_enabled;
        bool wireframe_enabled;
        bool cull_front;           // S24: glCullFace(GL_FRONT) vs the default GL_BACK
        int depth_bias;            // D3D11_RASTERIZER_DESC::DepthBias is int
        float slope_scaled_bias;   // ::SlopeScaledDepthBias is float

        bool operator==(const RasterizerKey& o) const
        {
            return cull_enabled == o.cull_enabled
                && scissor_enabled == o.scissor_enabled
                && depth_clamp_enabled == o.depth_clamp_enabled
                && wireframe_enabled == o.wireframe_enabled
                && cull_front == o.cull_front
                && depth_bias == o.depth_bias
                && slope_scaled_bias == o.slope_scaled_bias;
        }
    };

    struct RasterizerKeyHash
    {
        size_t operator()(const RasterizerKey& k) const
        {
            uint32_t flags = (k.cull_enabled ? 1u : 0u)
                | (k.scissor_enabled ? 2u : 0u)
                | (k.depth_clamp_enabled ? 4u : 0u)
                | (k.wireframe_enabled ? 8u : 0u)
                | (k.cull_front ? 16u : 0u);
            size_t h = std::hash<uint32_t>()(flags);
            h ^= std::hash<int>()(k.depth_bias) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<float>()(k.slope_scaled_bias) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    std::unordered_map<RasterizerKey, ID3D11RasterizerState*, RasterizerKeyHash> sRasterizerState;
    std::unordered_map<uint32_t, ID3D11BlendState*> sBlendState;
    std::unordered_map<uint32_t, ID3D11DepthStencilState*> sDepthStencilState;

    // D3D11_BLEND's real range (1-19) fits in 5 bits; write_mask uses the low
    // 4 (D3D11_COLOR_WRITE_ENABLE_* is a 4-bit RGBA mask); all 6 fields fit
    // in uint32_t (1 + 5+5 + 5+5 + 4 = 25 bits).
    uint32_t blendKey(bool enabled, D3D11_BLEND src, D3D11_BLEND dst, D3D11_BLEND alpha_src, D3D11_BLEND alpha_dst, uint8_t write_mask)
    {
        return (enabled ? 1u : 0u)
            | (static_cast<uint32_t>(src) << 1)
            | (static_cast<uint32_t>(dst) << 6)
            | (static_cast<uint32_t>(alpha_src) << 11)
            | (static_cast<uint32_t>(alpha_dst) << 16)
            | (static_cast<uint32_t>(write_mask) << 21);
    }

    uint32_t depthStencilKey(bool depth_enabled, bool write_enabled, D3D11_COMPARISON_FUNC func)
    {
        return (depth_enabled ? 1u : 0u)
            | (write_enabled ? 2u : 0u)
            | (static_cast<uint32_t>(func) << 2);
    }

    // GL's glBlendFunc(sfactor, dfactor) applies the same two factors to both
    // color and alpha, and GL_DST_COLOR/GL_SRC_COLOR are legal there (alpha
    // has only one component, so "color" just means that component). D3D11
    // has no equivalent leniency - *_COLOR blend enums are rejected outright
    // for RenderTarget[].SrcBlendAlpha/DestBlendAlpha (CreateBlendState fails
    // with E_INVALIDARG). LLRender::applyDXBlendState() reuses the same
    // src/dst for both color and alpha slots, mirroring GL's semantics; this
    // translates a color-referencing factor to its alpha-equivalent only
    // when used in the alpha slot, restoring GL's behavior instead of
    // failing outright.
    D3D11_BLEND toAlphaSafeBlend(D3D11_BLEND b)
    {
        switch (b)
        {
        case D3D11_BLEND_SRC_COLOR:     return D3D11_BLEND_SRC_ALPHA;
        case D3D11_BLEND_INV_SRC_COLOR: return D3D11_BLEND_INV_SRC_ALPHA;
        case D3D11_BLEND_DEST_COLOR:    return D3D11_BLEND_DEST_ALPHA;
        case D3D11_BLEND_INV_DEST_COLOR:return D3D11_BLEND_INV_DEST_ALPHA;
        default:                        return b;
        }
    }
}

ID3D11BlendState* DXStateCache::getBlendState(bool enabled, D3D11_BLEND src, D3D11_BLEND dst, D3D11_BLEND alpha_src, D3D11_BLEND alpha_dst, uint8_t write_mask)
{
    uint32_t key = blendKey(enabled, src, dst, alpha_src, alpha_dst, write_mask);
    auto iter = sBlendState.find(key);
    if (iter != sBlendState.end())
    {
        return iter->second;
    }

    D3D11_BLEND_DESC desc = {};
    desc.RenderTarget[0].BlendEnable = enabled;
    desc.RenderTarget[0].SrcBlend = src;
    desc.RenderTarget[0].DestBlend = dst;
    desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    // toAlphaSafeBlend() applied defensively even for the caller's real
    // alpha-channel factors, since D3D11 rejects *_COLOR enums in the alpha
    // slot regardless of where the factor came from.
    desc.RenderTarget[0].SrcBlendAlpha = toAlphaSafeBlend(alpha_src);
    desc.RenderTarget[0].DestBlendAlpha = toAlphaSafeBlend(alpha_dst);
    desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    desc.RenderTarget[0].RenderTargetWriteMask = write_mask;

    ID3D11BlendState* state = nullptr;
    HRESULT hr = gDXDevice.getDevice()->CreateBlendState(&desc, &state);
    if (FAILED(hr))
    {
        LL_WARNS("StateCache") << "CreateBlendState failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        return nullptr;
    }

    sBlendState[key] = state;
    return state;
}

ID3D11RasterizerState* DXStateCache::getRasterizerState(bool cull_enabled, bool scissor_enabled, bool depth_clamp_enabled, float polygon_offset_factor, float polygon_offset_units, bool wireframe_enabled, bool cull_front)
{
    // GL's "polygon offset disabled" and "enabled with (0,0)" are visually
    // identical; collapsing both onto the same (0, 0.f) key keeps a stable
    // cache entry for the common "no bias" case.
    RasterizerKey key{
        cull_enabled, scissor_enabled, depth_clamp_enabled, wireframe_enabled, cull_front,
        static_cast<int>(std::lround(polygon_offset_units)),
        polygon_offset_factor
    };
    auto iter = sRasterizerState.find(key);
    if (iter != sRasterizerState.end())
    {
        return iter->second;
    }

    D3D11_RASTERIZER_DESC desc = {};
    desc.ScissorEnable = scissor_enabled ? TRUE : FALSE;
    desc.FillMode = wireframe_enabled ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
    desc.CullMode = cull_enabled ? (cull_front ? D3D11_CULL_FRONT : D3D11_CULL_BACK) : D3D11_CULL_NONE;
    // GL's default front face is CCW, and glFrontFace() is never called
    // anywhere in this codebase, so every triangle is wound assuming CCW-is-
    // front. D3D11's default is the opposite (FrontCounterClockwise=FALSE
    // means CW is front); set TRUE to match GL.
    desc.FrontCounterClockwise = TRUE;
    // DepthClipEnable is D3D11's inverse-sense equivalent of GL_DEPTH_CLAMP:
    // TRUE (GL depth-clamp off) clips against near/far planes as usual;
    // FALSE (GL depth-clamp on) clamps depth to [0,1] instead of clipping.
    // LLPipeline::renderShadow() enables GL_DEPTH_CLAMP so shadow casters
    // outside the near/far planes still write depth instead of being culled.
    desc.DepthClipEnable = depth_clamp_enabled ? FALSE : TRUE;
    // D3D11's default "aliased" line rasterizer uses a diamond-exit-rule
    // test that can produce ZERO covered pixels for an axis-aligned 1px line
    // whose endpoints sit on exact integer coordinates (e.g.
    // LLMenuItemSeparatorGL::draw()'s horizontal separator). Filled
    // triangles are unaffected. AntialiasedLineEnable=TRUE switches
    // DXUIBatch's LineList/LineStrip draws to the alpha-coverage
    // antialiasing algorithm instead, which doesn't have this degenerate
    // case; no MSAA render target needed for it to take effect on lines.
    desc.AntialiasedLineEnable = TRUE;

    // DepthBias/SlopeScaledDepthBias are D3D11's exact equivalent of GL's
    // glPolygonOffset(factor, units); key.depth_bias/slope_scaled_bias are
    // already the converted values.
    desc.DepthBias = key.depth_bias;
    desc.SlopeScaledDepthBias = key.slope_scaled_bias;
    desc.DepthBiasClamp = 0.0f;

    ID3D11RasterizerState* state = nullptr;
    HRESULT hr = gDXDevice.getDevice()->CreateRasterizerState(&desc, &state);
    if (FAILED(hr))
    {
        LL_WARNS("StateCache") << "CreateRasterizerState failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        return nullptr;
    }

    sRasterizerState[key] = state;
    return state;
}

ID3D11DepthStencilState* DXStateCache::getDepthStencilState(bool depth_enabled, bool write_enabled, D3D11_COMPARISON_FUNC func)
{
    uint32_t key = depthStencilKey(depth_enabled, write_enabled, func);
    auto iter = sDepthStencilState.find(key);
    if (iter != sDepthStencilState.end())
    {
        return iter->second;
    }

    D3D11_DEPTH_STENCIL_DESC desc = {};
    desc.DepthEnable = depth_enabled;
    desc.DepthWriteMask = write_enabled ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    desc.DepthFunc = func;
    desc.StencilEnable = FALSE;

    ID3D11DepthStencilState* state = nullptr;
    HRESULT hr = gDXDevice.getDevice()->CreateDepthStencilState(&desc, &state);
    if (FAILED(hr))
    {
        LL_WARNS("StateCache") << "CreateDepthStencilState failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        return nullptr;
    }

    sDepthStencilState[key] = state;
    return state;
}

void DXStateCache::clear()
{
    for (auto& entry : sRasterizerState)
    {
        if (entry.second) entry.second->Release();
    }
    sRasterizerState.clear();
    for (auto& entry : sBlendState)
    {
        if (entry.second) entry.second->Release();
    }
    sBlendState.clear();
    for (auto& entry : sDepthStencilState)
    {
        if (entry.second) entry.second->Release();
    }
    sDepthStencilState.clear();

    sLastTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
}

void DXStateCache::setPrimitiveTopology(ID3D11DeviceContext* ctx, D3D11_PRIMITIVE_TOPOLOGY topology)
{
    if (topology == sLastTopology)
    {
        return;
    }
    ctx->IASetPrimitiveTopology(topology);
    sLastTopology = topology;
}
