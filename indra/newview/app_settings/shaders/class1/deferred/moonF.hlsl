/**
 * @file class1/deferred/moonF.hlsl
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

uniform float4 color;
uniform float3 moon_dir;
uniform float moon_brightness;
Texture2D diffuseMap : register(t0);
SamplerState diffuseMapSampler : register(s0);

struct PSInput
{
    float2 vary_texcoord0 : TEXCOORD0;
};

struct PSOutput
{
    float4 data0 : SV_Target0;
    float4 data1 : SV_Target1;
    float4 data2 : SV_Target2;
#if defined(HAS_EMISSIVE)
    float4 data3 : SV_Target3;
#endif
};

PSOutput main(PSInput IN)
{
    PSOutput OUT;

    // Restore Pre-EEP alpha fade moon near horizon
    float fade = 1.0;
    if( moon_dir.z > 0 )
        fade = clamp( moon_dir.z*moon_dir.z*4.0, 0.0, 1.0 );

    float4 c      = diffuseMap.Sample(diffuseMapSampler, IN.vary_texcoord0.xy);

    // SL-14113 Don't write to depth; prevent moon's quad from hiding stars which should be visible
    // Moon texture has transparent pixels <0x55,0x55,0x55,0x00>
    if (c.a <= 2./255.) // 0.00784
    {
        discard;
    }

    c.rgb *= moon_brightness;
    c.a   *= fade;

    OUT.data0 = float4(0, 0, 0, 0);
    OUT.data1 = float4(0.0, 0.0, 0.0, 0.0);
    OUT.data2 = float4(0.0, 0.0, 0.0, GBUFFER_FLAG_SKIP_ATMOS);

#if defined(HAS_EMISSIVE)
    OUT.data0 = float4(0, 0, 0, 0);
    OUT.data3 = float4(c.rgb, c.a);
#else
    OUT.data0 = float4(c.rgb, c.a);
#endif

    // Added and commented out for a ground truth.  Do not uncomment - Geenz
    //gl_FragDepth = 0.999985f;

    return OUT;
}
