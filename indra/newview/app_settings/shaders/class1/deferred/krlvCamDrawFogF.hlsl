/**
 * @file class1/deferred/krlvCamDrawFogF.hlsl
 * @brief KRLV @camdrawmin/@camdrawmax/@camdrawalphamin/@camdrawalphamax/
 * @camdrawcolor - a distance-based fog blend toward a settable color,
 * entirely driven by krlv/'s own restriction state (see
 * DXPipeline::presentDeferredScreen(), krlv/README.md's Camera section).
 * Everything within krlv_fog_min_dist of the camera is untouched;
 * everything beyond krlv_fog_max_dist is blended toward krlv_fog_color
 * at krlv_fog_max_alpha opacity; in between, alpha interpolates linearly
 * from krlv_fog_min_alpha to krlv_fog_max_alpha. Sky/no-geometry pixels
 * (reversed-Z depth <= 0) are left untouched rather than fogged.
 *
 * ALSO does dual duty for KRLV's @camtextures/@setcam_textures (a
 * SEPARATE, independently-gated restriction sharing this same pass
 * rather than a new pipeline stage - see krlv/README.md's Camera
 * section for the full design note): when krlv_camtextures_active is
 * set, every pixel whose stencil tag (krlvStencilTex, written per-batch
 * during the geometry pass by DXStateCache::tagAttachmentStencil() -
 * see LLRenderPass::pushBatch()/DXDrawPoolAlpha::renderAlpha()) reads 0
 * (world geometry, not a worn attachment) is replaced outright with
 * krlvSubstituteTex's colour instead of being fog-blended. Sampled at
 * SCREEN-SPACE UV, not the object's own surface UV - a post-process
 * pass has no access to per-object UV coordinates, so the substitute
 * texture's pattern is visible but not correctly mapped onto individual
 * world-object geometry - a documented, honest approximation, not a
 * bug. GLTF/PBR base-colour materials and PBR terrain are NOT tagged
 * (v1 scope cut - see krlv/README.md) and so are never blanked, even
 * when this restriction is active.
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

// t0-t3/s0-s3 reserved by deferredUtil.hlsl (attached below, isDeferred=true
// on gKrlvCamDrawFogProgram) - own diffuse texture moved to t7/s7, same
// reservation postDeferredNoDoFF.hlsl already uses for the same reason.
Texture2D diffuseRect : register(t7);
SamplerState diffuseRectSampler : register(s7);

// @camtextures/@setcam_textures - t8/s8/t9/s9, same "own registers past the
// deferredUtil.hlsl/diffuseRect reservations" convention as above.
//
// krlvStencilTex is UINT (DXGI_FORMAT_X24_TYPELESS_G8_UINT,
// DXRenderTarget::getStencilSRV()) - HLSL forbids SamplerState/.Sample() on
// an integer texture, so it's read via .Load() at the exact pixel
// SV_Position already gives us, not a UV Sample() like every other texture
// in this shader. That's also exactly right for a binary tag: .Load() is
// an unfiltered, single-texel fetch, so there's no blend-fringe risk at
// attachment/world boundaries the way a bilinear Sample() would have had.
Texture2D<uint> krlvStencilTex : register(t8);
Texture2D krlvSubstituteTex : register(t9);
SamplerState krlvSubstituteTexSampler : register(s9);

uniform float krlv_fog_min_dist;
uniform float krlv_fog_max_dist;
uniform float krlv_fog_min_alpha;
uniform float krlv_fog_max_alpha;
uniform float3 krlv_fog_color;
uniform int krlv_camtextures_active;

float getDepth(float2 pos_screen);
float4 getPositionWithDepth(float2 pos_screen, float depth);

struct PSInput
{
    // S24: SV_Position semantic required here, or every subsequent VS/PS interpolant register shifts (see uiF.hlsl).
    float4 position : SV_Position;

    float2 vary_fragcoord : TEXCOORD0;
};

struct PSOutput
{
    float4 color : SV_Target;
};

PSOutput main(PSInput IN)
{
    PSOutput OUT;

    // getDepth()/getPositionWithDepth() apply their own GL/D3D11 texture-
    // origin correction internally (matching hazeF.hlsl's convention,
    // which calls them with the raw, unflipped vary_fragcoord) - the
    // OWN diffuseRect sample below needs the flip done here instead
    // (matching postDeferredNoDoFF.hlsl's convention for its own raw
    // texture reads).
    float2 tc_raw = IN.vary_fragcoord.xy;
    float2 tc_flip = float2(tc_raw.x, 1.0 - tc_raw.y);

    float4 diff = diffuseRect.Sample(diffuseRectSampler, tc_flip);

    float depth = getDepth(tc_raw);

    float fogAlpha = 0.0;
    // S24: reversed-Z under DX_RENDER - far is 0.0, near is 1.0 (see kGLtoDXDepthRemap, llrender.cpp).
    if (depth > 0.0)
    {
        float4 pos = getPositionWithDepth(tc_raw, depth);
        float dist = length(pos.xyz); // eye-space, camera at the origin

        float t = saturate((dist - krlv_fog_min_dist) / max(0.0001, krlv_fog_max_dist - krlv_fog_min_dist));
        fogAlpha = lerp(krlv_fog_min_alpha, krlv_fog_max_alpha, t);
    }

    float3 color = lerp(diff.rgb, krlv_fog_color, fogAlpha);

    // @camtextures/@setcam_textures - see this file's header comment.
    if (krlv_camtextures_active != 0 && depth > 0.0)
    {
        uint stencilValue = krlvStencilTex.Load(int3(int2(IN.position.xy), 0));
        if (stencilValue == 0) // 0 = world geometry, not a tagged worn attachment
        {
            color = krlvSubstituteTex.Sample(krlvSubstituteTexSampler, tc_flip).rgb;
        }
    }

    OUT.color = float4(color, diff.a);
    return OUT;
}
