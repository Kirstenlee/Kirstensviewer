/**
 * @file class3/deferred/ssrResolveF.hlsl
 *
 * Copyright (c) 2026 Kirstenlee Cinquetti (Lee Quick)
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

// Temporal resolve for the screen-space reflection buffer. Reprojects last frame's resolved
// result into this frame with the same last-frame matrices as temporalResolveSSAOF.hlsl, clamps it
// to the current 3x3 neighbourhood so stale values can't ghost, and blends in the new trace.
// Attached helpers are declared below because the loader places the body first.

Texture2D ssrBuffer : register(t21);
Texture2D ssrHistory : register(t22);
uniform SamplerState ssrSampler : register(s9);

uniform float4x4 inv_modelview_delta;
uniform float4x4 last_projection_matrix;
uniform int ssr_history_valid;

#ifndef LL_INV_PROJ_DECLARED
#define LL_INV_PROJ_DECLARED
uniform float4x4 inv_proj;
uniform float2 screen_res;
#endif

float4 getPosition(float2 pos_screen);

static const float kSSRHistoryBlend = 0.9;

struct PSInput
{
    // SV_Position required here - its absence shifts every interpolant register (see uiF.hlsl).
    float4 position : SV_Position;
    float2 vary_texcoord0 : TEXCOORD0;
};

float4 main(PSInput IN) : SV_Target
{
    // GL-convention tc, as in ssrF.hlsl and softenLightF.hlsl; flipped only at the texture reads.
    float2 tc = float2(IN.position.x / screen_res.x, 1.0 - IN.position.y / screen_res.y);
    float2 flipped = float2(tc.x, 1.0 - tc.y);

    float4 current = ssrBuffer.SampleLevel(ssrSampler, flipped, 0);
    if (ssr_history_valid == 0)
    {
        return current;
    }

    float2 texel = 1.0 / screen_res;
    float4 mn = current;
    float4 mx = current;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            float4 s = ssrBuffer.SampleLevel(ssrSampler, flipped + float2(x, y) * texel, 0);
            mn = min(mn, s);
            mx = max(mx, s);
        }
    }

    float3 pos_cur_eye = getPosition(tc).xyz;
    float4 pos_last_eye = mul(inv_modelview_delta, float4(pos_cur_eye, 1.0));
    float4 clip_last = mul(last_projection_matrix, pos_last_eye);
    if (clip_last.w <= 0.0)
    {
        return current;
    }

    float2 uv_last = (clip_last.xy / clip_last.w) * 0.5 + 0.5;
    if (uv_last.x < 0.0 || uv_last.x > 1.0 || uv_last.y < 0.0 || uv_last.y > 1.0)
    {
        return current;
    }

    float4 history = ssrHistory.SampleLevel(ssrSampler, float2(uv_last.x, 1.0 - uv_last.y), 0);
    history = clamp(history, mn, mx);
    return lerp(current, history, kSSRHistoryBlend);
}
