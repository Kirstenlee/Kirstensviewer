/**
 * @file class1/interface/irradianceGenV.hlsl
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

// Same closed-form per-face direction formula as radianceGenV.hlsl - see its
// header comment for the derivation. Kept in sync with that file.
uniform int cubeFace;

struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 vary_dir : TEXCOORD0;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    // IN.position.z is a constant -1, invalid under D3D11's [0,1] clip-z
    // range - would clip this pass's whole quad away. Depth test/write are
    // disabled for this pass so 0.0 is otherwise arbitrary.
    OUT.position = float4(IN.position.xy, 0.0, 1.0);

    // y is left unmodified here; the Y-flip is paired with a negative-height
    // viewport at the C++ call sites instead (see radianceGenV.hlsl).
    float x = IN.position.x;
    float y = IN.position.y;

    float3 dir;
    if (cubeFace == 0)      dir = float3( 1.0,    y,    x); // +X
    else if (cubeFace == 1) dir = float3(-1.0,    y,   -x); // -X
    else if (cubeFace == 2) dir = float3(  -x,  1.0,   -y); // +Y
    else if (cubeFace == 3) dir = float3(  -x, -1.0,    y); // -Y
    else if (cubeFace == 4) dir = float3(  -x,    y,  1.0); // +Z
    else                    dir = float3(   x,    y, -1.0); // -Z

    OUT.vary_dir = dir;

    return OUT;
}
