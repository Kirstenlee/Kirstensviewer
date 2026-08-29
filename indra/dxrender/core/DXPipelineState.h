#pragma once
#include <d3d11.h>

// Bundles {input layout, VS, PS, blend state, rasterizer state, primitive
// topology} into one object so a draw pool's DX_RENDER branch can issue one
// bind() call instead of scattering IASetInputLayout/VSSetShader/PSSetShader/
// OMSetBlendState/RSSetState/IASetPrimitiveTopology calls through its own
// code. This is organizational, not a functional requirement -
// LLDrawPoolSimple's conversion works fine without it (it calls
// LLGLSLShader::bind()/LLVertexBuffer's methods directly, which already
// issue these same individual *Set* calls); intended for pools converted
// after this one, where bundling starts to reduce real duplication across
// several pools sharing similar state combinations.
//
// Deliberately does not include depth-stencil state (see DXStateCache's
// comment - nothing converted so far toggles stencil/depth test away from
// D3D11's default) or per-texture-slot SRV/sampler binding (that's still
// per-draw-call state via LLTexUnit, not part of a pool-level "pipeline").
class DXPipelineState
{
public:
    void setInputLayout(ID3D11InputLayout* layout) { mInputLayout = layout; }
    void setShaders(ID3D11VertexShader* vs, ID3D11PixelShader* ps) { mVS = vs; mPS = ps; }
    void setBlendState(ID3D11BlendState* blend) { mBlendState = blend; }
    void setRasterizerState(ID3D11RasterizerState* raster) { mRasterizerState = raster; }
    void setTopology(D3D11_PRIMITIVE_TOPOLOGY topology) { mTopology = topology; }

    // Issues all the *Set* calls this bundles. A null *State/layout/shader
    // pointer leaves that piece of state untouched (whatever was already
    // bound stays bound) rather than forcing a default - lets a pool bundle
    // only the state it actually wants to override.
    //
    // S24 (2026-08-28, task #179 audit): currently unused - no real caller
    // anywhere in the tree, confirmed via exhaustive grep (this class was
    // scaffolding for future pools, per the class comment above, that never
    // materialized). WARNING before wiring this up: mVS/mPS are bound via a
    // raw ctx->VSSetShader()/PSSetShader() call below, which does NOT update
    // LLGLSLShader::sCurBoundShaderPtr - the exact same bug class as the
    // 2026-07-26 "zero textures, zero fonts" incident (DXPipeline's
    // placeholder-shader fallback, newview/dxpipeline.cpp, fixed there by
    // calling LLGLSLShader::unbind() immediately after to null the stale
    // bookkeeping and force the next real bind() to re-apply for real). Any
    // caller of THIS bind() needs the same treatment - either call
    // LLGLSLShader::unbind() right after, or (better, once there's a real
    // caller to design around) route mVS/mPS through the owning
    // LLGLSLShader's own bind() instead of raw pointers here.
    void bind() const;

private:
    ID3D11InputLayout* mInputLayout = nullptr;
    ID3D11VertexShader* mVS = nullptr;
    ID3D11PixelShader* mPS = nullptr;
    ID3D11BlendState* mBlendState = nullptr;
    ID3D11RasterizerState* mRasterizerState = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY mTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
};
