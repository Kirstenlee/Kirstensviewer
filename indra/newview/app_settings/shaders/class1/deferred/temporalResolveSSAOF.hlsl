/**
 * @file class1/deferred/temporalResolveSSAOF.hlsl
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

/*[EXTRA_CODE_HERE]*/

// Reprojects last frame's resolved AO/shadow lightmap (mSSAOHistory) into
// this frame's screen space and blends it with the spatially-blurred
// lightmap, to fix screen-locked-noise flicker in calcAmbientOcclusion()
// (aoUtil.hlsl) that spatial blur alone cannot remove. Only the AO channel
// (green) is temporally blended; shadow channels (r/b/a) pass through
// unchanged to avoid introducing shadow lag/ghosting.

struct PSInput
{
    // SV_Position required here - its absence shifts every interpolant register (see uiF.hlsl).
    float4 position : SV_Position;

    float2 vary_fragcoord : TEXCOORD0;
};

// t0-t3/s0-s3 reserved by deferredUtil.hlsl (isDeferred=true pulls it in).
// t7/s7 is the established free-slot convention for lightMap (blurLightF.hlsl,
// sunLightSSAOF.hlsl); t8/s8 is the next free slot, for the history buffer.
uniform Texture2D lightMap : register(t7);
uniform SamplerState lightMapSampler : register(s7);

uniform Texture2D historyMap : register(t8);
uniform SamplerState historyMapSampler : register(s8);

uniform float4x4 inv_modelview_delta;
uniform float4x4 last_projection_matrix;

#ifndef LL_INV_PROJ_DECLARED
#define LL_INV_PROJ_DECLARED
uniform float2 screen_res;
uniform float4x4 inv_proj;
#endif

float4 getPosition(float2 pos_screen);

// Fixed per-frame history weight - sample-count convergence, not a
// physically-timed adaptation, so this is deliberately NOT dt-based (unlike
// exposureF.hlsl's exponential decay - that would make flicker worse at
// variable framerate, more history weight per real-second at high FPS).
static const float kHistoryBlend = 0.85;

float4 main(PSInput IN) : SV_Target
{
    float2 tc = IN.vary_fragcoord.xy;

    // GL-vs-D3D11 texture-origin flip, inlined at the sample call only - tc
    // itself stays unflipped since getPosition() below needs it in that form.
    float4 current = lightMap.Sample(lightMapSampler, float2(tc.x, 1.0 - tc.y));

    float3 pos_cur_eye = getPosition(tc).xyz;
    float4 pos_last_eye = mul(inv_modelview_delta, float4(pos_cur_eye, 1.0));
    float4 clip_last = mul(last_projection_matrix, pos_last_eye);

    float ao_final = current.g;

    if (clip_last.w > 0.0)
    {
        float2 ndc_last = clip_last.xy / clip_last.w;
        float2 uv_last = ndc_last * 0.5 + 0.5;

        if (uv_last.x >= 0.0 && uv_last.x <= 1.0 && uv_last.y >= 0.0 && uv_last.y <= 1.0)
        {
            float history_ao = historyMap.Sample(historyMapSampler, float2(uv_last.x, 1.0 - uv_last.y)).g;
            ao_final = lerp(current.g, history_ao, kHistoryBlend);
        }
    }

    return float4(current.r, ao_final, current.b, current.a);
}
