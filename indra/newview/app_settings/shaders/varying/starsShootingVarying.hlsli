/**
 * @file varying/starsShootingVarying.hlsli
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

// Shared vertex-to-pixel varying struct for class1/deferred/starsShootingV.hlsl/
// starsShootingF.hlsl (task #279 stage 2, "RENDER WOW").
//
// vary_texcoord0.x: streak-local position, 0 at the tail (transparent) to 1
// at the head (bright) - see LLVOWLSky::updateShootingStarGeometry().
// vary_texcoord0.y: streak-local width position, 0..1 across the quad.
// vertex_color.a: the spawn/expire fade envelope (0..1), same meaning as the
// alpha LLVOWLSky::updateShootingStarGeometry() bakes per-vertex.
struct StarsShootingVarying
{
    float4 vertex_color : COLOR0;
    float2 vary_texcoord0 : TEXCOORD0;
};
