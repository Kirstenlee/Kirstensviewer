#include "DXUIBatch.h"
#include "DXDevice.h"
#include "DXVertexLayout.h"
#include "DXStateCache.h"
#include "llerror.h"

DXUIBatch gDXUIBatch;

void DXUIBatch::push(const DXUIVertex* vertices, size_t count)
{
    mVertices.insert(mVertices.end(), vertices, vertices + count);
    mLastPushCount = count;
}

namespace
{
    D3D11_PRIMITIVE_TOPOLOGY toD3DTopology(DXUITopology topology)
    {
        switch (topology)
        {
        case DXUITopology::TriangleStrip: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case DXUITopology::LineList:      return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
        case DXUITopology::LineStrip:     return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case DXUITopology::TriangleList:
        default:                          return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        }
    }
}

void DXUIBatch::flush(const void* vs_bytecode, size_t vs_bytecode_size, ID3D11VertexShader* vs, ID3D11PixelShader* ps, bool alpha_blend, const char* debug_name, DXUITopology topology,
    bool depth_test, bool depth_write, D3D11_COMPARISON_FUNC depth_func)
{
    BatchState new_state;
    new_state.vs = vs;
    new_state.ps = ps;
    new_state.vsBytecode = vs_bytecode;
    new_state.vsBytecodeSize = vs_bytecode_size;
    new_state.debugName = debug_name;
    new_state.topology = topology;
    new_state.alphaBlend = alpha_blend;
    new_state.depthTest = depth_test;
    new_state.depthWrite = depth_write;
    new_state.depthFunc = depth_func;

    // The vertices for THIS call's primitive are the tail-end mLastPushCount
    // entries (the push() that must have immediately preceded this flush()
    // call, per this class's documented push()+flush() pairing contract).
    // Anything before that belongs to whatever batch was already pending.
    size_t new_count = mLastPushCount;
    size_t old_count = (mVertices.size() >= new_count) ? (mVertices.size() - new_count) : 0;

    if (mHasPending && old_count > 0)
    {
        if (new_state.sameDrawState(mPendingState))
        {
            // Same shader/topology/blend/depth as what's already pending -
            // just keep accumulating, don't draw yet. mVertices already has
            // the new vertices appended (push() already ran) - nothing else
            // to do.
            mPendingState = new_state; // refresh bytecode/debugName pointers
            return;
        }

        // State changed - draw the old batch now, using ITS OWN state, then
        // drop those vertices and continue with the new ones as a fresh
        // pending batch.
        drawAndPop(old_count, mPendingState);
    }

    mPendingState = new_state;
    mHasPending = true;
}

void DXUIBatch::flushPending()
{
    if (!mHasPending || mVertices.empty())
    {
        mHasPending = false;
        mVertices.clear();
        return;
    }

    drawAndPop(mVertices.size(), mPendingState);
    mHasPending = false;
}

void DXUIBatch::drawAndPop(size_t count, const BatchState& state)
{
    if (count == 0 || count > mVertices.size())
    {
        return;
    }

    const size_t byte_size = count * sizeof(DXUIVertex);
    if (count > mVertexCapacity)
    {
        if (!mVertexBuffer.createVertexBuffer(byte_size, nullptr))
        {
            mVertices.erase(mVertices.begin(), mVertices.begin() + count);
            return;
        }
        mVertexCapacity = count;
    }

    if (!mVertexBuffer.upload(mVertices.data(), byte_size))
    {
        mVertices.erase(mVertices.begin(), mVertices.begin() + count);
        return;
    }

    ID3D11DeviceContext* ctx = gDXDevice.getContext();

    // S24 (2026-08-02): set unconditionally, every call - see this
    // function's header comment for why trusting the caller's own
    // shader.bind() call was found to be unverifiable (bind()'s
    // sCurBoundShaderPtr-based early-out can silently no-op if anything
    // else touched VS/PS in between without updating that bookkeeping).
    // This is the single submission chokepoint for all of 2D UI/text, so
    // guaranteeing it here closes the whole bug class at once rather than
    // chasing every place that could theoretically leave stale state.
    ctx->VSSetShader(state.vs, nullptr, 0);
    ctx->PSSetShader(state.ps, nullptr, 0);

    ID3D11InputLayout* layout = DXVertexLayout::getOrCreateUILayout(state.vsBytecode, state.vsBytecodeSize, state.debugName);
    ctx->IASetInputLayout(layout);

    ID3D11Buffer* vb = mVertexBuffer.getBuffer();
    UINT stride = sizeof(DXUIVertex);
    UINT offset = 0;
    ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    DXStateCache::setPrimitiveTopology(ctx, toD3DTopology(state.topology));

    // Matches GL's BT_ALPHA (LLRender::setSceneBlendType) - src alpha /
    // inverse src alpha, full RGBA write mask. Non-blended (alpha_blend =
    // false) draws (e.g. opaque icon backgrounds) get straight overwrite.
    ID3D11BlendState* bs = state.alphaBlend
        ? DXStateCache::getBlendState(true, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, 0xF)
        : DXStateCache::getBlendState(false, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, 0xF);
    ctx->OMSetBlendState(bs, nullptr, 0xFFFFFFFF);

    // S24 (DX_RENDER, 2026-07-30): this is a dedicated 2D screen-space UI
    // batch renderer - it should never be depth-culled by 3D scene content
    // regardless of whatever the last 3D pass left bound. depth_test/
    // depth_write/depth_func are real, per-batch parameters (task #191) so
    // gl_segmented_rect_3d_tex()'s genuine 3D world-space callers (HUD
    // nametags/icons wanting real depth-testing) get their own correct
    // state instead of this always forcing DepthEnable=false - see the
    // pass-boundary flushPending() calls in llviewerdisplay.cpp/
    // llhudobject.cpp that keep depth-tested HUD batches from ever merging
    // with non-depth-tested screen-space UI batches.
    ID3D11DepthStencilState* ds = DXStateCache::getDepthStencilState(state.depthTest, state.depthWrite, state.depthFunc);
    ctx->OMSetDepthStencilState(ds, 0);

    ctx->Draw((UINT)count, 0);

    mVertices.erase(mVertices.begin(), mVertices.begin() + count);
}
