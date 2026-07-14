/**
 * @file class1/deferred/velocityAlphaF.hlsl
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

float4 diffuseLookup(float2 texcoord);
void bayerDitherDiscard(float alpha, float threshold, float4 fragCoord);

struct PSInput
{
    float4 svPosition : SV_Position;
    float4 vary_cur_clip : TEXCOORD0;
    float4 vary_last_clip : TEXCOORD1;
    float2 vary_texcoord0 : TEXCOORD2;
    float4 vertex_color : COLOR0;
};

float4 main(PSInput IN) : SV_Target
{
    float alpha = diffuseLookup(IN.vary_texcoord0.xy).a;
    alpha *= IN.vertex_color.a;

    bayerDitherDiscard(alpha, 0.88, IN.svPosition);

    float2 cur_ndc = IN.vary_cur_clip.xy / IN.vary_cur_clip.w;
    float2 last_ndc = IN.vary_last_clip.xy / IN.vary_last_clip.w;

    return float4(cur_ndc - last_ndc, 0.0, 1.0);
}
