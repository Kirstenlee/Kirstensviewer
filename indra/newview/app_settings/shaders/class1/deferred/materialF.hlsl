/**
 * @file class1/deferred/materialF.hlsl
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

#define DIFFUSE_ALPHA_MODE_BLEND 1

#if (DIFFUSE_ALPHA_MODE == DIFFUSE_ALPHA_MODE_BLEND)
float4 main() : SV_Target
{
    return float4(0.5, 0, 1, 0.5);
}
#else
struct PSOutput
{
    float4 target0 : SV_Target0;
    float4 target1 : SV_Target1;
    float4 target2 : SV_Target2;
#if defined(HAS_EMISSIVE)
    float4 target3 : SV_Target3;
#endif
};

PSOutput main()
{
    PSOutput OUT;
    OUT.target0 = float4(0.5, 0, 1, 0);
    OUT.target1 = float4(0, 0, 0, 0);
    OUT.target2 = float4(0, 0, 0, 0);
#if defined(HAS_EMISSIVE)
    OUT.target3 = float4(0, 0, 0, 0);
#endif
    return OUT;
}
#endif
