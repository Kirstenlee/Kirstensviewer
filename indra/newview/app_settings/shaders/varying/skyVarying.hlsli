/**
 * @file varying/skyVarying.hlsli
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

// Shared vertex-to-pixel varying struct for skyV.hlsl/skyF.hlsl - see
// varying/uiVarying.hlsli's comment for why this exists. The HAS_HDRI
// conditional fields must stay defined identically in both files (same
// #define set at compile time for a given shader instance, so this is safe)
// - see LLShaderMgr's per-instance mDefines.
struct SkyVarying
{
    float3 vary_HazeColor : TEXCOORD0;
    float vary_LightNormPosDot : TEXCOORD1;
#ifdef HAS_HDRI
    float4 vary_position : TEXCOORD2;
    float3 vary_rel_pos : TEXCOORD3;
#endif
};
