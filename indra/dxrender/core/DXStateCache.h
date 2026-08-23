#pragma once
#include <d3d11.h>
#include <cstdint>

// Caches ID3D11BlendState/ID3D11RasterizerState/ID3D11DepthStencilState
// objects process-wide. Scoped to what this codebase actually toggles -
// confirmed by reading real call sites, not guessed - rather than the full
// generality GL exposes. SamplerState caching already exists separately
// (see DXSampler, from the texture-binding work) - this class only covers
// blend/rasterizer/depth-stencil.
class DXStateCache
{
public:
    // GL blend enable/disable is a separate, independently-toggled piece of
    // state from the blend function (glBlendFunc) in GL's model - D3D11
    // bundles both into one ID3D11BlendState object instead. Callers (see
    // LLRender::applyDXBlendState(), llrender.cpp - the only caller) are
    // responsible for gathering the *current* combination of all three
    // pieces (enabled, factors, color write mask) and passing them together
    // here, since any one of them can change independently of the others.
    // src/dst use D3D11_BLEND directly (LLRender::eBlendFactor maps onto it
    // 1:1 - see llrender.cpp). write_mask matches
    // D3D11_COLOR_WRITE_ENABLE_* bit values.
    //
    // S24 (2026-08-06): alpha_src/alpha_dst added - previously this function
    // always derived the alpha-channel factors from src/dst via
    // toAlphaSafeBlend() (mirroring GL's single-glBlendFunc "same factors
    // for color and alpha" semantics), silently ignoring whatever
    // LLRender::blendFunc()'s 4-factor overload (glBlendFuncSeparate()
    // equivalent) actually asked for - a real, confirmed bug (dxdrawpoolalpha.cpp
    // calls the 4-factor overload for its main/glow/highlight blend states,
    // e.g. glow accumulation wants (ZERO,ONE,ONE,ONE) but was silently
    // getting (ZERO,ONE,ZERO,ONE) instead). The 2-factor overload already
    // sets mCurrBlendAlphaSFactor/DFactor equal to the color factors
    // (llrender.cpp), so callers can now always pass the real current alpha
    // factors here - the 2-factor case naturally gets the same result as
    // before, the 4-factor case finally gets what it actually asked for.
    static ID3D11BlendState* getBlendState(bool enabled, D3D11_BLEND src, D3D11_BLEND dst, D3D11_BLEND alpha_src, D3D11_BLEND alpha_dst, uint8_t write_mask);

    // enabled=true: cull back faces (D3D11_CULL_BACK, matching GL's default
    // glCullFace(GL_BACK) - this codebase doesn't override cull direction
    // commonly, so front-face culling is a documented gap, not handled).
    // enabled=false: D3D11_CULL_NONE.
    //
    // S24 (2026-08-07): scissor_enabled added, matching GL_SCISSOR_TEST's
    // enable/disable (LLScreenClipRect, llui/lllocalcliprect.cpp) - D3D11
    // bundles ScissorEnable into the rasterizer state object the same way it
    // bundles cull mode, so this needed the same 2-state-object treatment as
    // cull_enabled rather than a separate toggle. See llgl.cpp's
    // applyDXState() GL_SCISSOR_TEST case (mirrors the existing GL_CULL_FACE
    // case) - both dimensions are read from LLGLState::isEnabled() so a
    // change to either one always rebinds with the OTHER's current value
    // preserved, not clobbered.
    //
    // S24 (2026-08-10, task #158 milestone 1): depth_clamp_enabled added -
    // GL_DEPTH_CLAMP (used by LLPipeline::renderShadow()'s shadow-map pass
    // to avoid near/far-plane clipping shadow casters) previously had no
    // case in applyDXState() at all and silently no-op'd. D3D11's
    // DepthClipEnable is the inverse-sense equivalent (TRUE = GL's default/
    // depth-clamp-off, FALSE = depth clamp on) - bundled into the same
    // rasterizer state object as cull/scissor, so it gets the same 2-state-
    // object treatment. Defaulted to false (matching every pre-existing call
    // site's implicit "depth clamp off" behavior) so callers that don't care
    // about this dimension don't need updating.
    //
    // S24 (2026-08-19, degenerate-triangle foliage investigation):
    // depth_bias_enabled added - glPolygonOffset(factor, units) has NO
    // effect under DX_RENDER at all (it's a real, statically-linked core-GL
    // symbol, not one of this codebase's loaded extension-function
    // pointers, so calling it with no live GL context is a silent no-op -
    // confirmed by grep, no dispatch-table entry for it exists anywhere in
    // this tree). D3D11's per-rasterizer-state DepthBias/SlopeScaledDepthBias
    // fields are the documented, exact equivalent of GL's units/factor
    // (Microsoft's own docs describe the same "scaled by the smallest
    // resolvable depth increment" semantics GL uses) - bundled into the same
    // state object as cull/scissor/depth-clamp, so same treatment again.
    // Hardcoded to GL's own `glPolygonOffset(-1.0f, -1.0f)` value (the only
    // value LLDrawPoolBump::renderBump()'s emboss-bump pass ever uses,
    // confirmed by reading its real source) rather than threading an
    // arbitrary float through the cache - the many OTHER glPolygonOffset
    // call sites in this codebase (terrain, build-tool gizmos, tree
    // shadows, debug wireframe - all still silently no-op under DX_RENDER
    // too) use different values and are explicitly NOT covered by this
    // boolean flag; see task tracking for that broader, separately-scoped
    // follow-up. Defaulted to false so existing callers are unaffected.
    static ID3D11RasterizerState* getRasterizerState(bool cull_enabled, bool scissor_enabled, bool depth_clamp_enabled = false, bool depth_bias_enabled = false);

    // Mirrors LLGLDepthTest (llrender/llglstates.h) - depth_enabled/
    // write_enabled/func together, since D3D11 bundles them into one
    // ID3D11DepthStencilState object the same way it bundles blend state.
    // Stencil is always off - nothing converted so far toggles it (see
    // class comment history in stage-3/4 memory).
    static ID3D11DepthStencilState* getDepthStencilState(bool depth_enabled, bool write_enabled, D3D11_COMPARISON_FUNC func);

    // Releases every cached state object - call on full renderer shutdown.
    static void clear();
};
