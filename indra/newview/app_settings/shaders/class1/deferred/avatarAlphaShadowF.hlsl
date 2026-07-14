/**
 * @file class1/deferred/avatarAlphaShadowF.hlsl
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

Texture2D diffuseMap : register(t0);
SamplerState diffuseMapSampler : register(s0);

uniform float minimum_alpha;
uniform float4 color;

void bayerDitherDiscard(float alpha, float threshold, float4 fragCoord);

struct PSInput
{
    float4 svPosition : SV_Position;
    float pos_w : TEXCOORD0;
    float target_pos_x : TEXCOORD1;
    float2 vary_texcoord0 : TEXCOORD2;
};

float4 main(PSInput IN) : SV_Target
{
    float alpha = diffuseMap.Sample(diffuseMapSampler, IN.vary_texcoord0.xy).a * color.a;

    bayerDitherDiscard(alpha, minimum_alpha, IN.svPosition);

    return float4(1, 1, 1, 1);
}
