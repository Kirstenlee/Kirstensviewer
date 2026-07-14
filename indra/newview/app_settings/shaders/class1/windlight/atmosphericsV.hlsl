/**
 * @file class1/windlight/atmosphericsV.hlsl
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

uniform float3 sun_dir;
uniform float3 moon_dir;
uniform int sun_up_factor;

void setSunlitColor(float3 v);
void setAmblitColor(float3 v);
void setAdditiveColor(float3 v);
void setAtmosAttenuation(float3 v);
void setPositionEye(float3 v);

float3 getAdditiveColor();

void calcAtmosphericVars(float3 inPositionEye, float3 light_dir, float ambFactor, out float3 sunlit, out float3 amblit, out float3 additive, out float3 atten);

void calcAtmospherics(float3 inPositionEye)
{
    float3 tmpsunlit = float3(1, 1, 1);
    float3 tmpamblit = float3(1, 1, 1);
    float3 tmpaddlit = float3(1, 1, 1);
    float3 tmpattenlit = float3(1, 1, 1);
    float3 light_dir = (sun_up_factor == 1) ? sun_dir : moon_dir;
    calcAtmosphericVars(inPositionEye, light_dir, 1.0, tmpsunlit, tmpamblit, tmpaddlit, tmpattenlit);
    setSunlitColor(tmpsunlit);
    setAmblitColor(tmpamblit);
    setAdditiveColor(tmpaddlit);
    setAtmosAttenuation(tmpattenlit);
}
