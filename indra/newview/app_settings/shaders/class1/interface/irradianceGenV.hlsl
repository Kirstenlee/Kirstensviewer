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

// S24 (2026-08-22, plan item A - CLOSED-FORM REWRITE): see
// radianceGenV.hlsl's identical rewrite for the full derivation - replaces
// modelview_matrix + flipCol + fixHandedness + poleRotate with Direct3D's
// documented per-face cubemap addressing formula, hardcoded per face.
uniform int cubeFace;

// S24 (2026-08-23, LIVE ORIENTATION TUNER): see radianceGenV.hlsl's
// identical uniforms/main() for the full explanation - mirrored here so
// radiance and irradiance stay in sync.
uniform int dbgSwap;
uniform float dbgSignA;
uniform float dbgSignB;

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

    // S24 (task #194 follow-up, 2026-08-13): see radianceGenV.hlsl's
    // identical fix for the full derivation - IN.position.z is a constant
    // -1 (valid under GL's [-1,1] clip-z range, invalid under D3D11's
    // [0,1] range), which was causing this pass's whole quad to be
    // clipped away every draw call (DepthClipEnable=TRUE by default, all
    // 4 vertices sharing the same out-of-range z). 0.0 matches
    // LLPipeline::mScreenTriangleVB's already-proven-safe convention;
    // depth test/write are both disabled for this whole pass so the
    // specific value is otherwise irrelevant.
    OUT.position = float4(IN.position.xy, 0.0, 1.0);

    // S24 (2026-08-23, LIVE ORIENTATION TUNER): see radianceGenV.hlsl's
    // identical formula for the full explanation - dbgSwap/dbgSignA/
    // dbgSignB are live-driven from KVTweaks, default reproduces the
    // pre-tuner baseline formula exactly.
    float qx = IN.position.x;
    float qy = IN.position.y;

    float qa = (dbgSwap != 0) ? qy : qx;
    float qb = (dbgSwap != 0) ? qx : qy;
    float freeA = dbgSignA * qa;
    float freeB = dbgSignB * qb;

    float3 dir;
    if (cubeFace == 0)      dir = float3( 1.0, freeB, freeA); // +X
    else if (cubeFace == 1) dir = float3(-1.0, freeB, freeA); // -X
    else if (cubeFace == 2) dir = float3(freeA,  1.0, freeB); // +Y
    else if (cubeFace == 3) dir = float3(freeA, -1.0, freeB); // -Y
    else if (cubeFace == 4) dir = float3(freeA, freeB,  1.0); // +Z
    else                    dir = float3(freeA, freeB, -1.0); // -Z

    OUT.vary_dir = dir;

    return OUT;
}
