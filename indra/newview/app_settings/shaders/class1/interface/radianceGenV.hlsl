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

// Placement and sampling follow cubeFaceConvention.hlsli. This pass writes
// face `cubeFace` of a TextureCubeArray through a full-screen quad and
// CopySubresourceRegion, so the texel position is the placement and vary_dir
// is the centre of the sample lobe taken from the scratch cube.
//
// cubeFace must be D3D11's per-face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z,
// 5=-Z). It also selects the destination array slice (probe->mCubeIndex*6+cubeFace).
#include "cubeFaceConvention.hlsli"

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

    // The quad's [-1,1] position is the texel's (x, y) on the face. The
    // negative-height viewport at the C++ call sites supplies the row flip.
    OUT.vary_dir = cubeFaceGenDir(cubeFace, IN.position.x, IN.position.y);

    return OUT;
}
