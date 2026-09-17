/**
 * @file class1/interface/radianceGenV.hlsl
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

// The direction formula below is derived from Direct3D/OpenGL's documented
// TextureCube per-face addressing table (sc/tc/ma per face,
// s=(sc/ma+1)/2 etc. - the OpenGL spec's "Cubemap Texture Selection" table,
// which D3D deliberately matches), with one global 180-degree UV rotation
// (both x and y of this pass's quad-space position run opposite the
// table's direct (sc,tc)=(ma*x,ma*y) mapping - from this pass's own
// screen-space quad/viewport convention, not a per-face anomaly).
//
// This pass writes pixels directly into face `cubeFace` of a TextureCubeArray
// via a full-screen quad + CopySubresourceRegion (see the C++ call site) -
// it does NOT use D3D11's own cubemap rasterization/array-index machinery,
// so the formula below must independently match what D3D11's hardware
// sampler will later associate with (face,x,y) when this texture gets
// sampled for real (reflectionProbeF.hlsl's tapRefMap()/tapIrradianceMap()).
//
// cubeFace must be D3D11's real per-face index (0=+X, 1=-X, 2=+Y, 3=-Y,
// 4=+Z, 5=-Z), since this value also selects the destination array slice
// (probe->mCubeIndex*6+cubeFace) that hardware sampling will later read
// with its own real face addressing.
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

    // IN.position.z is a constant -1 for every vertex of this fixed quad -
    // valid under GL's [-1,1] clip-z range but outside D3D11's [0,1] range,
    // which would clip the whole primitive away. Depth test/write are
    // disabled for this pass so 0.0 is otherwise arbitrary.
    OUT.position = float4(IN.position.xy, 0.0, 1.0);

    // x/y are this quad's [-1,1] position; the sign pattern below is
    // Direct3D/OpenGL's documented TextureCube table with one global
    // 180-degree UV rotation (see header comment). y is left unmodified
    // here - the Y-flip is paired with a negative-height viewport at the
    // C++ call sites instead; do not duplicate it with a `y` negation here,
    // that combination breaks hero-probe mirror orientation.
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
