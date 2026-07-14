/**
 * @file class1/interface/pbrTerrainBakeF.hlsl
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

struct TerrainMix
{
    float4 weight;
    int type;
};

TerrainMix get_terrain_mix_weights(float alpha1, float alpha2, float alphaFinal);

Texture2D alpha_ramp : register(t0);
SamplerState alpha_rampSampler : register(s0);

struct PSInput
{
    // vary_texcoord* are used for terrain composition
    float4 vary_texcoord0 : TEXCOORD0;
    float4 vary_texcoord1 : TEXCOORD1;
};

float4 main(PSInput IN) : SV_Target
{
    TerrainMix tm;
    float alpha1 = alpha_ramp.Sample(alpha_rampSampler, IN.vary_texcoord0.zw).a;
    float alpha2 = alpha_ramp.Sample(alpha_rampSampler, IN.vary_texcoord1.xy).a;
    float alphaFinal = alpha_ramp.Sample(alpha_rampSampler, IN.vary_texcoord1.zw).a;

    tm = get_terrain_mix_weights(alpha1, alpha2, alphaFinal);

    // tm.weight.x can be ignored. The paintmap saves a channel by not storing
    // swatch 1, and assuming the weights of the 4 swatches add to 1.
    // TERRAIN_PAINT_PRECISION emulates loss of precision at lower bit depth
    // when a corresponding low-bit image format is not available. Because
    // integral values at one depth cannot be precisely represented at another
    // bit depth, rounding is required. To maximize numerical stability for
    // future conversions, bit depth conversions should round to the nearest
    // integer, not floor or ceil.
    return max(float4(round(tm.weight.yzw * TERRAIN_PAINT_PRECISION) / TERRAIN_PAINT_PRECISION, 1.0), float4(0, 0, 0, 0));
}
