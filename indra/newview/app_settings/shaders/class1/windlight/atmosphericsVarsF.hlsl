/**
 * @file class1/windlight/atmosphericsVarsF.hlsl
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

// Original GLSL (atmosphericsVarsF.glsl) declares these as real fragment
// inputs ("in vec3 vary_AdditiveColor;") with zero-arg accessor functions
// reading them directly - a struct-parameter version was invented during
// the HLSL port and both collided with real shaders' own "PSInput" struct
// name AND didn't match what atmosphericsF.hlsl's atmosLighting() (a real
// caller) expects (zero args). Matches the same static-global pattern
// already used on the vertex side (atmosphericsVarsV.hlsl) - any real
// shader's main() that actually needs these propagated from its vertex
// stage must populate them from its own PSInput's matching fields at the
// top of main(); none currently do (documented gap), so these read as
// zero-initialized until that wiring exists.
static float3 vary_AdditiveColor;
static float3 vary_AtmosAttenuation;

float3 getSunlitColor()
{
    return float3(0, 0, 0);
}

float3 getAmblitColor()
{
    return float3(0, 0, 0);
}

float3 getAdditiveColor()
{
    return vary_AdditiveColor;
}

float3 getAtmosAttenuation()
{
    return vary_AtmosAttenuation;
}
