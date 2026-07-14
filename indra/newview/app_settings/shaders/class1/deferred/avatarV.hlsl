/**
 * @file class1/deferred/avatarV.hlsl
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

uniform float4x4 projection_matrix;

float4x4 getSkinnedTransform();

#ifdef AVATAR_CLOTH
uniform float4 gWindDir;
uniform float4 gSinWaveParams;
uniform float4 gGravity;

static const float4 gMinMaxConstants = float4(1.0, 0.166666, 0.0083143, .00018542);     // #minimax-generated coefficients
static const float4 gPiConstants = float4(0.159154943, 6.28318530, 3.141592653, 1.5707963); // # {1/2PI, 2PI, PI, PI/2}
#endif

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord0 : TEXCOORD0;
    float4 weight : BLENDWEIGHT;
#ifdef AVATAR_CLOTH
    float4 clothing : COLOR1;
#endif
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 vary_normal : TEXCOORD0;
    float2 vary_texcoord0 : TEXCOORD1;
    float3 vary_position : TEXCOORD2;
};

VSOutput main(VSInput IN)
{
    VSOutput OUT;

    OUT.vary_texcoord0 = IN.texcoord0;

    float4 pos;
    float3 norm;

    float4 pos_in = float4(IN.position.xyz, 1.0);
    float4x4 trans = getSkinnedTransform();

    norm.x = dot(trans[0].xyz, IN.normal);
    norm.y = dot(trans[1].xyz, IN.normal);
    norm.z = dot(trans[2].xyz, IN.normal);
    norm = normalize(norm);

#ifdef AVATAR_CLOTH
    //wind
    float4 windEffect;
    windEffect = float4(dot(norm, gWindDir.xyz), dot(norm, gWindDir.xyz), dot(norm, gWindDir.xyz), dot(norm, gWindDir.xyz));
    pos.x = dot(trans[2], pos_in);
    windEffect.xyz = pos.x * float3(0.015, 0.015, 0.015)
                        + windEffect.xyz;
    windEffect.w = windEffect.w * 2.0 + 1.0;                // move wind offset value to [-1, 3]
    windEffect.w = windEffect.w*gWindDir.w;                 // modulate wind strength

    windEffect.xyz = windEffect.xyz*gSinWaveParams.xyz
                        +float3(gSinWaveParams.w, gSinWaveParams.w, gSinWaveParams.w);            // use sin wave params to scale and offset input

    //reduce to period of 2 PI
    float4 temp1, temp0, temp2, offsetPos;
    temp1.xyz = windEffect.xyz * gPiConstants.x;            // change input as multiple of [0-2PI] to [0-1]
    temp0.y = fmod(temp1.x, 1.0);
    windEffect.x = temp0.y * gPiConstants.y;                // scale from [0,1] to [0, 2PI]
    temp1.z = temp1.z - gPiConstants.w;                     // shift normal oscillation by PI/2
    temp0.y = fmod(temp1.z, 1.0);

    windEffect.z = temp0.y * gPiConstants.y;                // scale from [0,1] to [0, 2PI]
    windEffect.xyz = windEffect.xyz + float3(-3.141592, -3.141592, -3.141592);      // offset to [-PI, PI]

    //calculate sinusoid
    float4 sinWave;
    temp1 = windEffect*windEffect;
    sinWave = -temp1 * gMinMaxConstants.w
                + float4(gMinMaxConstants.z, gMinMaxConstants.z, gMinMaxConstants.z, gMinMaxConstants.z);                 // y = -(x^2)/7! + 1/5!
    sinWave = sinWave * -temp1 + float4(gMinMaxConstants.y, gMinMaxConstants.y, gMinMaxConstants.y, gMinMaxConstants.y);  // y = -(x^2) * (-(x^2)/7! + 1/5!) + 1/3!
    sinWave = sinWave * -temp1 + float4(gMinMaxConstants.x, gMinMaxConstants.x, gMinMaxConstants.x, gMinMaxConstants.x);  // y = -(x^2) * (-(x^2) * (-(x^2)/7! + 1/5!) + 1/3!) + 1
    sinWave = sinWave * windEffect;                         // y = x * (-(x^2) * (-(x^2) * (-(x^2)/7! + 1/5!) + 1/3!) + 1)

    // sinWave.x holds sin(norm . wind_direction) with primary frequency
    // sinWave.y holds sin(norm . wind_direction) with secondary frequency
    // sinWave.z hold cos(norm . wind_direction) with primary frequency
    sinWave.xyz = sinWave.xyz * gWindDir.w
                + float3(windEffect.w, windEffect.w, windEffect.w);                       // multiply by wind strength in gWindDir.w [-wind, wind]

    // add normal facing bias offset [-wind,wind] -> [-wind - .25, wind + 1]
    temp1 = float4(dot(norm, gGravity.xyz), dot(norm, gGravity.xyz), dot(norm, gGravity.xyz), dot(norm, gGravity.xyz));                  // how much is this normal facing in direction of gGravity?
    temp1 = min(temp1, float4(0.2, 0.0, 0.0, 0.0));              // clamp [-1, 1] to [-1, 0.2]
    temp1 = temp1*float4(1.5, 0.0, 0.0, 0.0);                    // scale from [-1,0.2] to [-1.5, 0.3]
    sinWave.x = sinWave.x + temp1.x;                        // add gGravity effect to sinwave (only primary frequency)
    sinWave.xyz = sinWave.xyz * IN.clothing.w;                 // modulate by clothing coverage

    sinWave.xyz = max(sinWave.xyz, float3(-1.0, -1.0, -1.0)); // clamp to underlying body shape
    offsetPos = IN.clothing * sinWave.x;                       // multiply wind effect times clothing displacement
    temp2 = gWindDir*sinWave.z + float4(norm, 0);              // calculate normal offset due to wind oscillation
    offsetPos = float4(1.0, 1.0, 1.0, 0.0)*offsetPos+pos_in;     // add to offset vertex position, and zero out effect from w
    norm += temp2.xyz*2.0;                                  // add sin wave effect on normals (exaggerated)

    //renormalize normal (again)
    norm = normalize(norm);

    pos.x = dot(trans[0], offsetPos);
    pos.y = dot(trans[1], offsetPos);
    pos.z = dot(trans[2], offsetPos);
    pos.w = 1.0;
#else
    pos.x = dot(trans[0], pos_in);
    pos.y = dot(trans[1], pos_in);
    pos.z = dot(trans[2], pos_in);
    pos.w = 1.0;
#endif

    OUT.vary_normal = norm;

    OUT.vary_position = pos.xyz;
    OUT.position = mul(projection_matrix, pos);

    return OUT;
}
