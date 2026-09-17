/**
 * @file class3/deferred/waterHazeF.hlsl
 *
 * Copyright (c) 2025 Kirstenlee Cinquetti (Lee Quick)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

// Inputs
struct PSInput
{
    // SV_Position must be declared first in PSInput to match VS output register order - see uiF.hlsl.
    float4 position : SV_Position;

    float4 vary_fragcoord : TEXCOORD0;
};

float4 getPositionWithDepth(float2 pos_screen, float depth);
float getDepth(float2 pos_screen);

float4 getWaterFogView(float3 pos);

uniform int above_water;

Texture2D exclusionTex : register(t0);
SamplerState exclusionTexSampler : register(s0);

float4 main(PSInput IN) : SV_Target
{
    float2  tc           = IN.vary_fragcoord.xy/IN.vary_fragcoord.w*0.5+0.5;
    float depth        = getDepth(tc.xy);
    // GL-vs-D3D11 texture-origin flip inlined here only; tc itself stays
    // unflipped since getDepth() above already applies its own internal flip.
    float mask = exclusionTex.Sample(exclusionTexSampler, float2(tc.x, 1.0 - tc.y)).r;

    if (above_water > 0)
    {
        // Just discard if we're in the exclusion mask.
        // The previous invisiprim hack we're replacing would also crank up water fog desntiy.
        // But doing that makes exclusion surfaces very slow as we'd need to render even more into the mask.
        // - Geenz 2025-02-06
        if (mask < 1)
        {
            discard;
        }

        // we want to depth test when the camera is above water, but some GPUs have a hard time
        // with depth testing against render targets that are bound for sampling in the same shader
        // so we do it manually here

        // No "*0.5+0.5" remap needed: vary_fragcoord (waterHazeV.hlsl) is set to
        // the same value as SV_Position, built from modelview_projection_matrix,
        // which already carries the D3D depth remap (kGLtoDXDepthRemap,
        // llrender.cpp) - so vary_fragcoord.z/w is already the stored-depth-buffer
        // convention directly.
        float cur_depth = IN.vary_fragcoord.z / IN.vary_fragcoord.w;
        // Reversed-Z: "haze plane occluded by real scene" means a SMALLER stored
        // value now (near=1.0/far=0.0), hence < instead of >.
        if (cur_depth < depth)
        {
            discard;
        }

    }

    float4  pos          = getPositionWithDepth(tc, depth);

    float4 fogged = getWaterFogView(pos.xyz);
    fogged.a = max(pow(fogged.a, 1.7), 0);

    return max(fogged, float4(0, 0, 0, 0)); //output linear since local lights will be added to this shader's results
}
