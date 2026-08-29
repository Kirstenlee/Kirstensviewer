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
    // depth-bias support added - glPolygonOffset(factor, units) has NO
    // effect under DX_RENDER at all (it's a real, statically-linked core-GL
    // symbol, not one of this codebase's loaded extension-function
    // pointers, so calling it with no live GL context is a silent no-op -
    // confirmed by grep, no dispatch-table entry for it exists anywhere in
    // this tree). D3D11's per-rasterizer-state DepthBias/SlopeScaledDepthBias
    // fields are the documented, exact equivalent of GL's units/factor
    // (Microsoft's own docs describe the same "scaled by the smallest
    // resolvable depth increment" semantics GL uses) - bundled into the same
    // state object as cull/scissor/depth-clamp, so same treatment again.
    //
    // S24 (2026-08-28, task #242): widened from a hardcoded bool (matching
    // only LLDrawPoolBump::renderBump()'s single -1.0f/-1.0f value) to real
    // float parameters - the many OTHER glPolygonOffset call sites in this
    // codebase (terrain, build-tool gizmos, debug wireframe, glow/shadow-
    // cascade overlays) use different values, and the point of this pass
    // was to close all of them via LLRender::setPolygonOffset() (llrender.h)
    // rather than one hardcoded case. polygon_offset_units maps to D3D11's
    // integer DepthBias (rounded - GL's "units" and D3D11's DepthBias are
    // both already expressed in "smallest resolvable depth increment"
    // ticks, so no scaling conversion is needed, just the int truncation
    // D3D11's field type requires); polygon_offset_factor maps to
    // SlopeScaledDepthBias directly (both float, same semantics). (0.f, 0.f)
    // is a true no-op in D3D11 exactly like GL's polygon-offset-disabled
    // state, so this defaults identically to the old depth_bias_enabled=false
    // behavior for existing callers that don't pass it.
    // Storage switched from the fixed 5D bool array to an unordered_map
    // keyed by a packed struct (see DXStateCache.cpp) since float bias
    // values don't fit a small fixed index range - mirrors how
    // sBlendState/sDepthStencilState already work.
    //
    // S24 (2026-08-27, task #264): wireframe_enabled added -
    // glPolygonMode(GL_FRONT_AND_BACK, GL_LINE) has no D3D11 per-draw
    // equivalent either (same class of gap as glPolygonOffset above - fill
    // mode is a rasterizer-state CREATION-time field, D3D11_FILL_WIREFRAME
    // vs D3D11_FILL_SOLID). Previously every DX_RENDER call site that wanted
    // GL_LINE mode just skipped the call (#ifndef DX_RENDER-guarded),
    // silently leaving fill mode at the default SOLID - LLFace::
    // renderOneWireframe() (the edit-mode mesh selection outline) is the
    // confirmed real caller this was fixed for: mesh objects were rendering
    // as a solid filled blob in their highlight color instead of an outline.
    // Bundled into the same state object as the other dimensions, same
    // treatment. Note D3D11 wireframe fill mode has no line-width control
    // (always 1px, unlike GL's glLineWidth(5.f) at this same call site) -
    // a real, smaller residual visual gap, not fixed by this.
    static ID3D11RasterizerState* getRasterizerState(bool cull_enabled, bool scissor_enabled, bool depth_clamp_enabled = false, float polygon_offset_factor = 0.f, float polygon_offset_units = 0.f, bool wireframe_enabled = false);

    // Mirrors LLGLDepthTest (llrender/llglstates.h) - depth_enabled/
    // write_enabled/func together, since D3D11 bundles them into one
    // ID3D11DepthStencilState object the same way it bundles blend state.
    // Stencil is always off - nothing converted so far toggles it (see
    // class comment history in stage-3/4 memory).
    static ID3D11DepthStencilState* getDepthStencilState(bool depth_enabled, bool write_enabled, D3D11_COMPARISON_FUNC func);

    // S24 (2026-08-29, task #278/#275): skips the IASetPrimitiveTopology()
    // driver call entirely when `topology` already matches what's currently
    // bound - mirrors llglslshader.cpp's sLastBoundVS/sLastBoundPS shader-
    // bind cache (same "cheap but not free at this call frequency"
    // reasoning, just never extended to topology until now). This app
    // confirmed to use exactly one D3D11 context (no deferred contexts, see
    // DXDevice.cpp's D3D11_CREATE_DEVICE_SINGLETHREADED comment), so a
    // single process-wide last-value is correct - not per-context state.
    //
    // ALL real IASetPrimitiveTopology call sites must go through this, not
    // call it directly, or the cache silently desyncs and the next "looks
    // unchanged" skip here submits geometry with the WRONG topology (e.g. a
    // triangle list drawn as a line list) - a real, confirmed hazard from
    // when this was first investigated (task #275): llrender/llvertexbuffer.cpp
    // (drawRange/drawRangeFast/drawArrays), newview/dxpipeline.cpp, and
    // dxrender/resources/DXUIBatch.cpp are the 3 live sites, all converted
    // together in the same commit as this function. (dxrender/core/
    // DXPipelineState.cpp has a 4th raw call but is confirmed dead code with
    // no live callers - task #179's audit - left untouched.)
    static void setPrimitiveTopology(ID3D11DeviceContext* ctx, D3D11_PRIMITIVE_TOPOLOGY topology);

    // S24 (2026-08-29, task #278/#273): monotonic counter, bumped from every
    // real OMSetRenderTargets() call site (DXRenderTarget::bindTarget()/
    // bindBackBuffer(), DXContext::beginFrame() - grep for the call sites
    // before adding a new one, and bump here too). LLTexUnit (llrender.h/
    // .cpp) stamps this value alongside its cached SRV pointer and forces a
    // real rebind if the generation has moved on since, even if the SRV
    // pointer still matches - see mDXSRVGeneration's own comment for the
    // full hazard this exists to close (D3D11 auto-unbinding an SRV when
    // the same resource becomes a render target).
    static uint64_t getRTVGeneration() { return sRTVGeneration; }
    static void bumpRTVGeneration() { ++sRTVGeneration; }

    // Releases every cached state object, and resets the topology cache
    // above - call on full renderer shutdown/device rebuild, since neither
    // survives a device reset.
    static void clear();

private:
    static uint64_t sRTVGeneration;
};
