/**
 * @file cubeFaceConvention.hlsli
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

// Single source for probe-cube face addressing. Used by radianceGenV.hlsl and
// irradianceGenV.hlsl; the C++ capture tables are DXCubeMapFaces (sLookDirs,
// sUpVecs), which must stay consistent with this file.
//
// Face order is D3D11's TextureCube slice order: 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z.
//
// cubeFaceDir() is Direct3D's documented cube addressing: for a face, the
// sampler resolves a direction to (sc, tc) on that face, with
//   s = (sc/|ma| + 1)/2, t = (tc/|ma| + 1)/2, t increasing down the rows.
// A texel at (x, y) with x = 2s-1 and y = 2t-1 therefore holds the direction
// returned by cubeFaceDir(face, x, y). y is the NDC y of a negative-height
// viewport, which puts row 0 at y = -1.
//
// Capture stores each face mirrored in x (LLCoordFrame's right axis is
// at x up, and the capture tables in DXCubeMapFaces.cpp are built on that),
// so the scratch texel at (x, y) holds world content for cubeFaceDir(face, -x, y).
// Radiance and irradiance generation therefore place their output at the spec
// texel (x, y) and sample the scratch at cubeFaceCaptureDir(face, x, y). The
// output is then correct for hardware sampling by direction.

float3 cubeFaceDir(int face, float sc, float tc)
{
    if (face == 0)      return float3( 1.0, -tc, -sc); // +X
    else if (face == 1) return float3(-1.0, -tc,  sc); // -X
    else if (face == 2) return float3(   sc, 1.0,  tc); // +Y
    else if (face == 3) return float3(   sc,-1.0, -tc); // -Y
    else if (face == 4) return float3(   sc, -tc, 1.0); // +Z
    else                return float3(  -sc, -tc,-1.0); // -Z
}

float3 cubeFaceCaptureDir(int face, float x, float y)
{
    return cubeFaceDir(face, -x, y);
}

// Live orientation controls (KVTweaks). Applied to the quad position before the base mapping, so
// all zero is the default orientation. The eight combinations cover every flip and swap of the square.
uniform int conv_swap_xy;
uniform int conv_flip_x;
uniform int conv_flip_y;

float3 cubeFaceGenDir(int face, float x, float y)
{
    float px = conv_swap_xy != 0 ? y : x;
    float py = conv_swap_xy != 0 ? x : y;
    if (conv_flip_x != 0) px = -px;
    if (conv_flip_y != 0) py = -py;
    return cubeFaceCaptureDir(face, px, py);
}
