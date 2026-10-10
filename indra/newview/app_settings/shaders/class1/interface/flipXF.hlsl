/**
 * @file class1/interface/flipXF.hlsl
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

// Un-mirrors one captured cube face before it is stored in the cube
// resource. DXCubeMapFaces.h: capture stores each face mirrored in x -
// texel (x,y) holds content for cubeFaceDir(face,-x,y), not the hardware-
// standard cubeFaceDir(face,x,y). Flipping once here, before storage,
// keeps the stored cube a plain standard-convention TextureCube so the
// compose shader can sample it with hardware seamless filtering intact.
struct PSInput
{
    float4 position : SV_Position;
    float2 tc : TEXCOORD0;
};

Texture2D srcMap : register(t0);
SamplerState srcMapSampler : register(s0);

float4 main(PSInput IN) : SV_Target
{
    // X flips the DXCubeMapFaces capture-mirror convention. Y corrects for
    // D3D11's top-left-origin vs this engine's UV convention when sampling
    // a post-fx render target - same flip copyF.hlsl/glowcombineF.hlsl/
    // fxaaF.hlsl/dofCombineF.hlsl apply for this category of source.
    return srcMap.Sample(srcMapSampler, float2(1.0 - IN.tc.x, 1.0 - IN.tc.y));
}
