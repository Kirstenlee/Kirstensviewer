/**
 * @file class1/deferred/hiZReduceF.hlsl
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

// Min-reduce downsample for Hi-Z pyramid generation.
// Reads 2x2 block from source mip level, outputs min to SV_Depth.
// Handles odd source dimensions by sampling the extra column/row,
// matching Godot's MODE_ODD_WIDTH / MODE_ODD_HEIGHT behavior.

Texture2D depthMap : register(t0);
uniform int srcLevel;

struct PSInput
{
    float4 svPosition : SV_Position;
};

float main(PSInput IN) : SV_Depth
{
    int2 destCoord = int2(IN.svPosition.xy);
    int2 srcCoord  = destCoord * 2;
    uint srcSizeX, srcSizeY, srcLevels;
    depthMap.GetDimensions(srcLevel, srcSizeX, srcSizeY, srcLevels);
    int2 srcSize = int2(srcSizeX, srcSizeY);

    float d0 = depthMap.Load(int3(srcCoord, srcLevel)).r;
    float d1 = depthMap.Load(int3(min(srcCoord + int2(1, 0), srcSize - 1), srcLevel)).r;
    float d2 = depthMap.Load(int3(min(srcCoord + int2(0, 1), srcSize - 1), srcLevel)).r;
    float d3 = depthMap.Load(int3(min(srcCoord + int2(1, 1), srcSize - 1), srcLevel)).r;

    float depth = min(min(d0, d1), min(d2, d3));

    // When the source has an odd dimension, the last dest texel's 2x2 block
    // doesn't cover the final row/column. Sample the extra texels so the
    // Hi-Z guarantee (min of all covered texels) is maintained.
    if ((srcSize.x & 1) == 1)
    {
        depth = min(depth, depthMap.Load(int3(min(srcCoord + int2(2, 0), srcSize - 1), srcLevel)).r);
        depth = min(depth, depthMap.Load(int3(min(srcCoord + int2(2, 1), srcSize - 1), srcLevel)).r);
    }
    if ((srcSize.y & 1) == 1)
    {
        depth = min(depth, depthMap.Load(int3(min(srcCoord + int2(0, 2), srcSize - 1), srcLevel)).r);
        depth = min(depth, depthMap.Load(int3(min(srcCoord + int2(1, 2), srcSize - 1), srcLevel)).r);
    }
    if ((srcSize.x & 1) == 1 && (srcSize.y & 1) == 1)
    {
        depth = min(depth, depthMap.Load(int3(min(srcCoord + int2(2, 2), srcSize - 1), srcLevel)).r);
    }

    return depth;
}
