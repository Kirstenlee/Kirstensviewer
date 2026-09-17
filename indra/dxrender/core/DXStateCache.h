#pragma once
#include <d3d11.h>
#include <cstdint>

// Caches ID3D11BlendState/ID3D11RasterizerState/ID3D11DepthStencilState
// objects process-wide. Scoped to what this codebase actually toggles rather
// than the full generality GL exposes. SamplerState caching exists
// separately (see DXSampler) - this class only covers blend/rasterizer/
// depth-stencil.
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
    // D3D11_COLOR_WRITE_ENABLE_* bit values. alpha_src/alpha_dst carry the
    // real current alpha-channel factors (LLRender::blendFunc()'s 2-factor
    // overload sets these equal to the color factors; the 4-factor overload
    // - glBlendFuncSeparate() equivalent, used by dxdrawpoolalpha.cpp for
    // glow/highlight blend states - sets them independently), rather than
    // always deriving them from src/dst.
    static ID3D11BlendState* getBlendState(bool enabled, D3D11_BLEND src, D3D11_BLEND dst, D3D11_BLEND alpha_src, D3D11_BLEND alpha_dst, uint8_t write_mask);

    // enabled=true: cull back faces (D3D11_CULL_BACK, matching GL's default
    // glCullFace(GL_BACK) - this codebase doesn't override cull direction
    // commonly, so front-face culling is a documented gap, not handled).
    // enabled=false: D3D11_CULL_NONE.
    //
    // scissor_enabled matches GL_SCISSOR_TEST; D3D11 bundles ScissorEnable
    // into the rasterizer state object the same way it bundles cull mode.
    //
    // depth_clamp_enabled matches GL_DEPTH_CLAMP (used by
    // LLPipeline::renderShadow()'s shadow-map pass). D3D11's DepthClipEnable
    // is the inverse-sense equivalent: TRUE (GL depth-clamp off, the
    // default) clips as usual; FALSE (GL depth-clamp on) clamps depth
    // instead of clipping.
    //
    // glPolygonOffset(factor, units) has NO effect under DX_RENDER (it's a
    // statically-linked core-GL symbol with no dispatch-table entry, so
    // calling it with no live GL context silently no-ops). D3D11's
    // DepthBias/SlopeScaledDepthBias fields are the exact equivalent of GL's
    // units/factor. polygon_offset_units maps to the integer DepthBias
    // (rounded - both already tick in "smallest resolvable depth increment"
    // units, D3D11's field is just int-typed); polygon_offset_factor maps
    // directly to SlopeScaledDepthBias. (0.f, 0.f) is a true no-op in D3D11,
    // matching GL's polygon-offset-disabled state.
    //
    // wireframe_enabled: glPolygonMode(GL_FRONT_AND_BACK, GL_LINE) has no
    // D3D11 per-draw equivalent either - fill mode is a rasterizer-state
    // creation-time field (D3D11_FILL_WIREFRAME vs D3D11_FILL_SOLID). Note
    // D3D11 wireframe has no line-width control (always 1px, unlike GL's
    // glLineWidth() at the same call sites) - a real, smaller residual
    // visual gap.
    //
    // cull_front: GL's glCullFace(GL_FRONT) vs the default glCullFace(GL_BACK) - only matters
    // when cull_enabled is true. Tracked via DXState::getCullFace() (llgl.h/.cpp), read by
    // LLRender::applyDXRasterizerState(). Used by LLViewerJoint::render()'s hair/skirt "render
    // inside" pass (llviewerjoint.cpp) - previously always culled back faces regardless under
    // DX_RENDER, silently losing that pass's front-face cull (see git history for the fix).
    static ID3D11RasterizerState* getRasterizerState(bool cull_enabled, bool scissor_enabled, bool depth_clamp_enabled = false, float polygon_offset_factor = 0.f, float polygon_offset_units = 0.f, bool wireframe_enabled = false, bool cull_front = false);

    // True only while Develop > Rendering > Wireframe's real DX11 render span is active
    // (LLPipeline::renderGeomDeferred()/renderGeomPostDeferred()'s DX_RENDER brackets in
    // dxpipeline.cpp) - NOT the same as gUseWireframe itself. GL_CULL_FACE/SCISSOR_TEST/
    // DEPTH_CLAMP/POLYGON_OFFSET_* toggle constantly during normal rendering (every
    // LLGLEnable/LLGLDisable), each one forcing a full rasterizer-state rebuild via
    // LLRender::applyDXRasterizerState() - D3D11 bundles fill mode into that same object, unlike
    // GL's independent glPolygonMode(). Without this scope flag, wireframe mode got silently
    // clobbered back to solid fill by the very first unrelated cull/scissor toggle after being
    // set (which happens almost immediately - the first drawpool), well before anything actually
    // drew with it - the root cause of wireframe mode appearing completely non-functional despite
    // gUseWireframe itself toggling correctly.
    static bool sWireframeScopeActive;

    // Mirrors LLGLDepthTest (llrender/llglstates.h) - depth_enabled/
    // write_enabled/func together, since D3D11 bundles them into one
    // ID3D11DepthStencilState object the same way it bundles blend state.
    // Stencil is always off - nothing converted so far toggles it.
    static ID3D11DepthStencilState* getDepthStencilState(bool depth_enabled, bool write_enabled, D3D11_COMPARISON_FUNC func);

    // Skips the IASetPrimitiveTopology() driver call when `topology` already
    // matches what's currently bound - mirrors llhlslshader.cpp's
    // sLastBoundVS/sLastBoundPS shader-bind cache. This app uses exactly one
    // D3D11 context (no deferred contexts), so a single process-wide
    // last-value is correct, not per-context state.
    //
    // ALL real IASetPrimitiveTopology call sites must go through this, not
    // call it directly, or the cache silently desyncs and a later "looks
    // unchanged" skip submits geometry with the WRONG topology. Live sites:
    // llrender/llvertexbuffer.cpp (drawRange/drawRangeFast/drawArrays),
    // newview/dxpipeline.cpp, dxrender/resources/DXUIBatch.cpp.
    // (dxrender/core/DXPipelineState.cpp has a raw call too but is unused -
    // left untouched.)
    static void setPrimitiveTopology(ID3D11DeviceContext* ctx, D3D11_PRIMITIVE_TOPOLOGY topology);

    // Monotonic counter, bumped from every real OMSetRenderTargets() call
    // site (DXRenderTarget::bindTarget()/bindBackBuffer(),
    // DXContext::beginFrame() - grep for call sites before adding a new one,
    // and bump here too). LLTexUnit stamps this value alongside its cached
    // SRV pointer and forces a real rebind if the generation has moved on
    // since, even if the SRV pointer still matches - closes a hazard where
    // D3D11 auto-unbinds an SRV when the same resource becomes a render
    // target.
    static uint64_t getRTVGeneration() { return sRTVGeneration; }
    static void bumpRTVGeneration() { ++sRTVGeneration; }

    // Releases every cached state object, and resets the topology cache
    // above - call on full renderer shutdown/device rebuild, since neither
    // survives a device reset.
    static void clear();

private:
    static uint64_t sRTVGeneration;
};
