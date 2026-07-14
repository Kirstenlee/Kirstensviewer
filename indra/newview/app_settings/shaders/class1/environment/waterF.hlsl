/**
 * @file class1/environment/waterF.hlsl
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

/*[EXTRA_CODE_HERE]*/

out vec4 frag_color;

uniform sampler2D diffuseMap;
uniform sampler2D refTex;
uniform sampler2D bumpMap;
uniform sampler2D screnTex;
uniform sampler2D depthMap;
uniform sampler2D reflectionTex;
uniform sampler2D exclusionTex;

uniform vec3 eyeVec;
uniform vec3 normScale;
uniform vec3 specular;
uniform vec4 waterFogColor;
uniform float waterFogDensity;
uniform float waterFogKS;
uniform vec3 lightDir;
uniform float blurMultiplier;
uniform float refScale;
uniform float kd;
uniform float fresnelScale;
uniform float scaleFactor;
uniform float time;
uniform vec4 waterPlane;

float3 srgb_to_linear(float3 c);

void mirrorClip(float3 pos);

struct PSInput
{
    float4 vary_texcoord0 : TEXCOORD0;
    float4 vary_texcoord1 : TEXCOORD1;
    float4 vary_texcoord2 : TEXCOORD2;
    float4 vary_position : TEXCOORD3;
    float3 vary_eyeVec : TEXCOORD4;
};

float4 main(PSInput IN) : SV_Target
{
    mirrorClip(IN.vary_position.xyz);
    // ... water rendering logic would go here
    return float4(0, 0, 0, 1);
}
