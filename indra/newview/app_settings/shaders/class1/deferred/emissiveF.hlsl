/**
 * @file class1/deferred/emissiveF.hlsl
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

#include "varying/emissiveVarying.hlsli"

// S24 (2026-08-02): see uiF.hlsl's comment - real register mismatch,
// confirmed via fxc.exe disassembly, affects every bare-Varying PS input -
// this file already had a custom PSInput wrapper (for vary_texture_index)
// but was still missing the SV_Position field itself.
struct PSInput
{
    float4 position : SV_Position;
    EmissiveVarying varying;
#ifdef HAS_DIFFUSE_LOOKUP
    nointerpolation int vary_texture_index : VARYTEXTUREINDEX;
#endif
};

float4 main(PSInput IN) : SV_Target
{
#ifdef HAS_DIFFUSE_LOOKUP
    vary_texture_index = IN.vary_texture_index;
#endif

    float a = diffuseLookup(IN.varying.vary_texcoord0.xy).a * IN.varying.vertex_color.a;
    return max(float4(0, 0, 0, a), float4(0, 0, 0, 0));
}
