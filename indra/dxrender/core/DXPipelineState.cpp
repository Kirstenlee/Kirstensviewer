#include "DXPipelineState.h"
#include "DXDevice.h"

void DXPipelineState::bind() const
{
    ID3D11DeviceContext* ctx = gDXDevice.getContext();

    if (mInputLayout)
    {
        ctx->IASetInputLayout(mInputLayout);
    }
    if (mVS)
    {
        ctx->VSSetShader(mVS, nullptr, 0);
    }
    if (mPS)
    {
        ctx->PSSetShader(mPS, nullptr, 0);
    }
    if (mBlendState)
    {
        ctx->OMSetBlendState(mBlendState, nullptr, 0xFFFFFFFF);
    }
    if (mRasterizerState)
    {
        ctx->RSSetState(mRasterizerState);
    }
    ctx->IASetPrimitiveTopology(mTopology);
}
