/**
 * @file class1/interface/normaldebugG.hlsl
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

// *NOTE: Geometry shaders have a reputation for being slow. Consider using
// compute shaders instead, which have a reputation for being fast. This
// geometry shader in particular seems to run fine on my machine, but I won't
// vouch for this in performance-critical areas.
// -Cosmic,2023-09-28

struct GSInput
{
    float4 position : SV_Position;
    float4 normal_g : TEXCOORD0;
#ifdef HAS_ATTRIBUTE_TANGENT
    float4 tangent_g : TEXCOORD1;
#endif
};

struct GSOutput
{
    float4 position : SV_Position;
    float4 vertex_color : COLOR0;
};

void triangle_normal_debug(int i, triangle GSInput input[3], inout LineStream<GSOutput> outStream)
{
    GSOutput v;

    // Normal
    float4 normal_color = float4(1.0, 1.0, 0.0, 1.0);
    v.position = input[i].position;
    v.vertex_color = normal_color;
    outStream.Append(v);
    v.position = input[i].normal_g;
    v.vertex_color = normal_color;
    outStream.Append(v);
    outStream.RestartStrip();

#ifdef HAS_ATTRIBUTE_TANGENT
    // Tangent
    float4 tangent_color = float4(0.0, 1.0, 1.0, 1.0);
    v.position = input[i].position;
    v.vertex_color = tangent_color;
    outStream.Append(v);
    v.position = input[i].tangent_g;
    v.vertex_color = tangent_color;
    outStream.Append(v);
    outStream.RestartStrip();
#endif
}

#ifdef HAS_ATTRIBUTE_TANGENT
[maxvertexcount(12)]
#else
[maxvertexcount(6)]
#endif
void main(triangle GSInput input[3], inout LineStream<GSOutput> outStream)
{
    triangle_normal_debug(0, input, outStream);
    triangle_normal_debug(1, input, outStream);
    triangle_normal_debug(2, input, outStream);
}
