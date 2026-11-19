/**
 * @file /deferred/shadowSkinnedV.hlsl
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

uniform float4x4 modelview_matrix;
uniform float4x4 projection_matrix;

float4x4 getObjectSkinnedTransform();

struct VSInput
{
    float3 position : POSITION;
    float4 weight4 : BLENDWEIGHT;
};

struct VSOutput
{
    float4 position : SV_Position;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    //transform vertex
    float4x4 skin = getObjectSkinnedTransform();

    // S24 : the vertex goes through the skin, view and projection matrices in turn, three matrix-vector
    // products, instead of first multiplying the matrices together for every vertex (the same result)
    float4 pos = mul(skin, float4(IN.position.xyz, 1.0));
    pos = mul(modelview_matrix, pos);
    pos = mul(projection_matrix, pos);

    OUT.position = pos;
    return OUT;
}
