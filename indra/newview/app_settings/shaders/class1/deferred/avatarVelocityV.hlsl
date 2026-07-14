/**
 * @file class1/deferred/avatarVelocityV.hlsl
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

uniform float4x4 projection_matrix;
uniform float4 lastMatrixPalette[45];

float4x4 getSkinnedTransform();
void writeVaryVelocity(float4 pos, float4 last_pos, out float4 vary_cur_clip, out float4 vary_last_clip);

float4x4 getLastSkinnedTransform(float4 weight)
{
    float4x4 ret;
    int i = int(floor(weight.x));
    float x = frac(weight.x);

    ret[0] = lerp(lastMatrixPalette[i + 0], lastMatrixPalette[i + 1], x);
    ret[1] = lerp(lastMatrixPalette[i + 15], lastMatrixPalette[i + 16], x);
    ret[2] = lerp(lastMatrixPalette[i + 30], lastMatrixPalette[i + 31], x);
    ret[3] = float4(0, 0, 0, 1);

    return ret;
}

struct VSInput
{
    float3 position : POSITION;
    float4 weight : BLENDWEIGHT;
    float2 texcoord0 : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 vary_cur_clip : TEXCOORD0;
    float4 vary_last_clip : TEXCOORD1;
    float2 vary_texcoord0 : TEXCOORD2;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    float4 pos = float4(IN.position.xyz, 1.0);

    float4x4 current_skin = getSkinnedTransform();
    float4 current_clip = mul(projection_matrix, mul(current_skin, pos));

    float4x4 last_skin = getLastSkinnedTransform(IN.weight);
    float4 last_clip = mul(projection_matrix, mul(last_skin, pos));

    OUT.position = current_clip;

    writeVaryVelocity(current_clip, last_clip, OUT.vary_cur_clip, OUT.vary_last_clip);

    OUT.vary_texcoord0 = IN.texcoord0;

    return OUT;
}
