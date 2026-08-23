/**
 * @file class1/lighting/lightF.hlsl
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

struct PSInput
{
    // S24 (2026-08-02): missing SV_Position - see uiF.hlsl's comment (fxc.exe-confirmed VS/PS register-shift bug).
    float4 position : SV_Position;

    float4 vertex_color : COLOR0;
    float2 vary_texcoord0 : TEXCOORD0;
};

// diffuseLookup() forward declaration removed - the original GLSL has none
// either (relies on GL's link-time resolution against whichever attached
// object provides it); the real HLSL definition is generated with a
// float4 return (dxrender's loadShaderFile() DX_RENDER branch), which
// would conflict with an incorrectly float3-typed forward declaration here.
float3 atmosLighting(float3 light);
float3 scaleSoftClip(float3 light);

float4 main(PSInput IN) : SV_Target
{
    float4 color = diffuseLookup(IN.vary_texcoord0.xy) * IN.vertex_color;

    color.rgb = atmosLighting(color.rgb);
    color.rgb = scaleSoftClip(color.rgb);

    return max(color, float4(0, 0, 0, 0));
}
