#include "DXStateCache.h"
#include "DXDevice.h"
#include "llerror.h"
#include <unordered_map>
#include <cmath>

uint64_t DXStateCache::sRTVGeneration = 0;

namespace
{
    // S24 (2026-08-29, task #278/#275) - see DXStateCache.h's
    // setPrimitiveTopology() comment. D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED is
    // never a real topology any caller passes, so it's a safe "nothing
    // bound yet" sentinel that always forces the first real call through.
    D3D11_PRIMITIVE_TOPOLOGY sLastTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

    // S24 (2026-08-28, task #242): switched from a fixed [2][2][2][2][2] bool
    // array to an unordered_map keyed by this packed struct once
    // polygon-offset stopped being a single hardcoded bool - see
    // DXStateCache.h's comment on getRasterizerState(). Mirrors how
    // sBlendState/sDepthStencilState already work below.
    struct RasterizerKey
    {
        bool cull_enabled;
        bool scissor_enabled;
        bool depth_clamp_enabled;
        bool wireframe_enabled;
        int depth_bias;            // D3D11_RASTERIZER_DESC::DepthBias is int
        float slope_scaled_bias;   // ::SlopeScaledDepthBias is float

        bool operator==(const RasterizerKey& o) const
        {
            return cull_enabled == o.cull_enabled
                && scissor_enabled == o.scissor_enabled
                && depth_clamp_enabled == o.depth_clamp_enabled
                && wireframe_enabled == o.wireframe_enabled
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
                | (k.wireframe_enabled ? 8u : 0u);
            size_t h = std::hash<uint32_t>()(flags);
            h ^= std::hash<int>()(k.depth_bias) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<float>()(k.slope_scaled_bias) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    std::unordered_map<RasterizerKey, ID3D11RasterizerState*, RasterizerKeyHash> sRasterizerState;
    std::unordered_map<uint32_t, ID3D11BlendState*> sBlendState;
    std::unordered_map<uint32_t, ID3D11DepthStencilState*> sDepthStencilState;

    // S24 (2026-08-06): widened to include alpha_src/alpha_dst alongside
    // src/dst - D3D11_BLEND's real range (1-19) fits comfortably in 5 bits,
    // write_mask only ever uses the low 4 (D3D11_COLOR_WRITE_ENABLE_* are a
    // 4-bit RGBA mask), so all 6 fields still fit well inside uint32_t
    // (1 + 5+5 + 5+5 + 4 = 25 bits).
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

    // S24 (2026-08-03): GL's glBlendFunc(sfactor, dfactor) applies the same
    // two factors to both the color AND alpha equations, and GL_DST_COLOR/
    // GL_SRC_COLOR are perfectly legal there (for the alpha equation they
    // just mean "the destination/source alpha component", since alpha only
    // has one component to reference). D3D11 has no equivalent leniency -
    // *_COLOR blend enums are explicitly rejected for RenderTarget[].
    // SrcBlendAlpha/DestBlendAlpha (confirmed via the D3D11 debug layer:
    // "is trying to use a D3D11_BLEND value that manipulates color, which
    // is invalid" - CreateBlendState then genuinely fails with
    // E_INVALIDARG, not just a warning). LLRender::applyDXBlendState()
    // reuses the same src/dst for both color and alpha slots unconditionally
    // (mirroring GL's single-glBlendFunc semantics) - this translates a
    // color-referencing factor to its alpha-equivalent only when used in
    // the alpha slot, restoring the GL behavior instead of failing outright.
    // Confirmed real caller: LLRender::setSceneBlendType(BT_MULT_X2)'s
    // blendFunc(BF_DEST_COLOR, BF_SOURCE_COLOR) - failed 300+ times in a
    // single session before this fix, silently falling back to D3D11's
    // opaque default blend state (blend disabled) every single time.
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
    // S24 (2026-08-06): now uses the CALLER's real alpha-channel factors
    // (see this function's header comment) instead of always deriving them
    // from src/dst - toAlphaSafeBlend() is still applied, since D3D11
    // rejects *_COLOR enums in the alpha slot outright regardless of where
    // the factor came from (defensive, not expected to trigger for a
    // genuine alpha_src/alpha_dst pair in practice).
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

ID3D11RasterizerState* DXStateCache::getRasterizerState(bool cull_enabled, bool scissor_enabled, bool depth_clamp_enabled, float polygon_offset_factor, float polygon_offset_units, bool wireframe_enabled)
{
    // S24 (2026-08-28, task #242): GL's "polygon offset disabled" and
    // "polygon offset enabled with (0,0)" are visually identical (no bias
    // either way) - collapsing both onto the same (0, 0.f) key here means
    // callers that pass a still-nonzero factor/units while genuinely
    // disabled (shouldn't happen, but not asserted against) can't
    // accidentally fragment the cache, and keeps this key stable for the
    // overwhelmingly common "no bias" case shared by every call site that
    // doesn't care about this dimension.
    RasterizerKey key{
        cull_enabled, scissor_enabled, depth_clamp_enabled, wireframe_enabled,
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
    desc.CullMode = cull_enabled ? D3D11_CULL_BACK : D3D11_CULL_NONE;
    // S24 (2026-07-23): GL's default front face is CCW (glFrontFace() is
    // never called anywhere in this codebase - grep-confirmed - so every
    // triangle here is wound assuming GL's CCW-is-front convention). D3D11's
    // default is the opposite (FrontCounterClockwise=FALSE means CW is
    // front) - left FALSE, any CCW-wound (i.e. every) triangle culled as
    // "back-facing" whenever cull_enabled is true. Set TRUE to match GL.
    desc.FrontCounterClockwise = TRUE;
    // S24 (2026-08-10, task #158 milestone 1): DepthClipEnable is D3D11's
    // inverse-sense equivalent of GL_DEPTH_CLAMP - TRUE (GL depth-clamp off,
    // the pre-existing hardcoded default) clips primitives against the near/
    // far planes as usual; FALSE (GL depth-clamp on) clamps depth values
    // outside [0,1] to the range instead of clipping. LLPipeline::renderShadow()
    // enables GL_DEPTH_CLAMP for its shadow-map pass so shadow casters
    // outside the near/far planes still write depth instead of being culled.
    desc.DepthClipEnable = depth_clamp_enabled ? FALSE : TRUE;
    // S24 (2026-08-01): zero-initialized above left AntialiasedLineEnable/
    // MultisampleEnable both FALSE - D3D11's default "aliased" line
    // rasterizer, which uses a diamond-exit-rule test that can produce
    // ZERO covered pixels for a perfectly axis-aligned 1px line whose
    // endpoints sit on exact integer coordinates (e.g.
    // LLMenuItemSeparatorGL::draw()'s horizontal separator - both endpoints
    // share the same integer Y). Filled triangles (rects/text) are
    // unaffected by this, which is why those already rendered correctly
    // while lines specifically could vanish. AntialiasedLineEnable=TRUE
    // switches DXUIBatch's LineList/LineStrip draws (menu separators,
    // gl_line_2d/gl_line_3d/gl_corners_2d/unfilled gl_rect_2d/gl_arc_2d/
    // gl_circle_2d - all still share this one cached rasterizer state) to
    // the alpha-coverage antialiasing algorithm instead, which doesn't have
    // this degenerate case. No MSAA render target needed for this flag to
    // take effect on lines.
    desc.AntialiasedLineEnable = TRUE;

    // S24 (2026-08-19, widened 2026-08-28 task #242): DepthBias/
    // SlopeScaledDepthBias are D3D11's exact equivalent of GL's
    // glPolygonOffset(factor, units) - see this function's header comment
    // (DXStateCache.h) for the mapping. key.depth_bias/slope_scaled_bias are
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
