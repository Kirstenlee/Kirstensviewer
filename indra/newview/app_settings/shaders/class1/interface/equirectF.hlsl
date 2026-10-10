/**
 * @file class1/interface/equirectF.hlsl
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

// Cube-to-equirectangular projection. Samples a real hardware TextureCube -
// seamless, filtered, no manual face/UV lookup.
struct PSInput
{
    float4 position : SV_Position;
    float2 tc : TEXCOORD0;
};

TextureCube cubeTex : register(t0);
SamplerState cubeSampler : register(s0);

struct PSOutput
{
    float4 frag_color : SV_Target;
};

PSOutput main(PSInput IN)
{
    PSOutput OUT;

    // tc.y=1 is the top of the output (sky), tc.y=0 the bottom (ground),
    // matching SL's Z-up world convention. Azimuth sign: with front=+X and
    // Z-up, "right" = cross(forward,up) = -Y, so (0.5 - tc.x) is the
    // correct sign for a texel right of center to sample toward -Y.
    float phi = (0.5 - IN.tc.x) * 2.0 * 3.14159265;
    float latitude = (IN.tc.y - 0.5) * 3.14159265;

    float dz = sin(latitude);
    float r = cos(latitude);
    float3 dir = normalize(float3(r * cos(phi), r * sin(phi), dz));

    // Each cube face is un-mirrored once before storage by flipXF.hlsl, so
    // this is a plain, uncorrected, seamless hardware sample.
    OUT.frag_color = cubeTex.Sample(cubeSampler, dir);

    return OUT;
}
