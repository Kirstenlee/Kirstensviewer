/**
 * @file class1/windlight/atmosphericsVarsV.hlsl
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


// HLSL globals with no "static" qualifier are implicitly const shader
// inputs - unlike GLSL, where a bare global is an ordinary mutable
// variable. These are read/written entirely within one compiled stage
// (get/set accessor pattern below), so "static" is the correct HLSL
// equivalent of GLSL's default global mutability, not a behavior change.
static float3 additive_color;
static float3 atmos_attenuation;
static float3 sunlit_color;
static float3 amblit_color;

// vary_AdditiveColor/vary_AtmosAttenuation were declared here as loose
// globals with a semantic annotation, mirroring GLSL's "varying" globals -
// HLSL has no such concept; a true vertex-to-pixel varying must be a
// VSOutput struct field returned from main(), which these are not wired
// to for any shader that currently attaches this file (confirmed for
// water - waterV.hlsl's real VSOutput has no vary_AdditiveColor/
// vary_AtmosAttenuation fields, and its main() never calls
// setAdditiveColor()/setAtmosAttenuation()). Kept as static/intra-stage
// only, same as the four color accumulators above - any shader that
// actually needs these propagated to its pixel stage will silently not
// get them and needs real struct-field wiring, not this accessor pattern.
static float3 vary_AdditiveColor;
static float3 vary_AtmosAttenuation;

float3 getSunlitColor()
{
    return sunlit_color;
}

float3 getAmblitColor()
{
    return amblit_color;
}

float3 getAdditiveColor()
{
    return additive_color;
}

float3 getAtmosAttenuation()
{
    return atmos_attenuation;
}

void setSunlitColor(float3 v)
{
    sunlit_color = v;
}

void setAmblitColor(float3 v)
{
    amblit_color = v;
}

void setAdditiveColor(float3 v)
{
    additive_color = v;
    vary_AdditiveColor = v;
}

void setAtmosAttenuation(float3 v)
{
    atmos_attenuation = v;
    vary_AtmosAttenuation = v;
}
