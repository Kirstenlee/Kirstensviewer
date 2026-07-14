/**
 * @file class2/interface/irradianceGenF.hlsl
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

TextureCubeArray reflectionProbes : register(t0);
SamplerState reflectionProbesSampler : register(s0);
uniform int sourceIdx;

uniform float max_probe_lod;

struct PSInput
{
    float3 vary_dir : TEXCOORD0;
};

// Code below is derived from the Khronos GLTF Sample viewer:
// https://github.com/KhronosGroup/glTF-Sample-Viewer/blob/master/source/shaders/ibl_filtering.frag


#define MATH_PI 3.1415926535897932384626433832795

static float u_roughness = 1.0;
static int u_sampleCount = 32;
static float u_lodBias = 2.0;
static int u_width = 64;

// Hammersley Points on the Hemisphere
// CC BY 3.0 (Holger Dammertz)
// http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
// with adapted interface
float radicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

// hammersley2d describes a sequence of points in the 2d unit square [0,1)^2
// that can be used for quasi Monte Carlo integration
float2 hammersley2d(int i, int N) {
    return float2(float(i)/float(N), radicalInverse_VdC(uint(i)));
}

// Hemisphere Sample

// TBN generates a tangent bitangent normal coordinate frame from the normal
// (the normal must be normalized)
float3x3 generateTBN(float3 normal)
{
    float3 bitangent = float3(0.0, 1.0, 0.0);

    float NdotUp = dot(normal, float3(0.0, 1.0, 0.0));
    float epsilon = 0.0000001;
    /*if (1.0 - abs(NdotUp) <= epsilon)
    {
        // Sampling +Y or -Y, so we need a more robust bitangent.
        if (NdotUp > 0.0)
        {
            bitangent = float3(0.0, 0.0, 1.0);
        }
        else
        {
            bitangent = float3(0.0, 0.0, -1.0);
        }
    }*/

    float3 tangent = normalize(cross(bitangent, normal));
    bitangent = cross(normal, tangent);

    return float3x3(tangent, bitangent, normal);
}

struct MicrofacetDistributionSample
{
    float pdf;
    float cosTheta;
    float sinTheta;
    float phi;
};

MicrofacetDistributionSample Lambertian(float2 xi, float roughness)
{
    MicrofacetDistributionSample lambertian;

    // Cosine weighted hemisphere sampling
    // http://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/2D_Sampling_with_Multidimensional_Transformations.html#Cosine-WeightedHemisphereSampling
    lambertian.cosTheta = sqrt(1.0 - xi.y);
    lambertian.sinTheta = sqrt(xi.y); // equivalent to `sqrt(1.0 - cosTheta*cosTheta)`;
    lambertian.phi = 2.0 * MATH_PI * xi.x;

    lambertian.pdf = lambertian.cosTheta / MATH_PI; // evaluation for solid angle, therefore drop the sinTheta

    return lambertian;
}


// getImportanceSample returns an importance sample direction with pdf in the .w component
float4 getImportanceSample(int sampleIndex, float3 N, float roughness)
{
    // generate a quasi monte carlo point in the unit square [0.1)^2
    float2 xi = hammersley2d(sampleIndex, u_sampleCount);

    MicrofacetDistributionSample importanceSample;

    // generate the points on the hemisphere with a fitting mapping for
    // the distribution (e.g. lambertian uses a cosine importance)
    importanceSample = Lambertian(xi, roughness);

    // transform the hemisphere sample to the normal coordinate frame
    // i.e. rotate the hemisphere to the normal direction
    float3 localSpaceDirection = normalize(float3(
        importanceSample.sinTheta * cos(importanceSample.phi),
        importanceSample.sinTheta * sin(importanceSample.phi),
        importanceSample.cosTheta
    ));
    float3x3 TBN = generateTBN(N);
    float3 direction = mul(TBN, localSpaceDirection);

    return float4(direction, importanceSample.pdf);
}

// Mipmap Filtered Samples (GPU Gems 3, 20.4)
// https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-20-gpu-based-importance-sampling
// https://cgg.mff.cuni.cz/~jaroslav/papers/2007-sketch-fis/Final_sap_0073.pdf
float computeLod(float pdf)
{
    // // Solid angle of current sample -- bigger for less likely samples
    // float omegaS = 1.0 / (float(u_sampleCount) * pdf);
    // // Solid angle of texel
    // // note: the factor of 4.0 * MATH_PI
    // float omegaP = 4.0 * MATH_PI / (6.0 * float(u_width) * float(u_width));
    // // Mip level is determined by the ratio of our sample's solid angle to a texel's solid angle
    // // note that 0.5 * log2 is equivalent to log4
    // float lod = 0.5 * log2(omegaS / omegaP);

    // babylon introduces a factor of K (=4) to the solid angle ratio
    // this helps to avoid undersampling the environment map
    // this does not appear in the original formulation by Jaroslav Krivanek and Mark Colbert
    // log4(4) == 1
    // lod += 1.0;

    // We achieved good results by using the original formulation from Krivanek & Colbert adapted to cubemaps

    // https://cgg.mff.cuni.cz/~jaroslav/papers/2007-sketch-fis/Final_sap_0073.pdf
    float lod = 0.5 * log2( 6.0 * float(u_width) * float(u_width) / (float(u_sampleCount) * pdf));


    return lod;
}

float4 filterColor(float3 N)
{
    float4 color = float4(0.f, 0.f, 0.f, 0.f);

    for(int i = 0; i < u_sampleCount; ++i)
    {
        float4 importanceSample = getImportanceSample(i, N, 1.0);

        float3 H = float3(importanceSample.xyz);
        float pdf = importanceSample.w;

        // mipmap filtered samples (GPU Gems 3, 20.4)
        float lod = computeLod(pdf);

        // apply the bias to the lod
        lod += u_lodBias;

        lod = clamp(lod, 0, max_probe_lod);
        // sample lambertian at a lower resolution to avoid fireflies
        float4 lambertian = reflectionProbes.SampleLevel(reflectionProbesSampler, float4(H, sourceIdx), lod);

        color += lambertian;
    }

    color /= float(u_sampleCount);

    return color;
}

// entry point
float4 main(PSInput IN) : SV_Target
{
    float4 color = float4(0, 0, 0, 0);

    color = filterColor(IN.vary_dir);

    return max(color, float4(0, 0, 0, 0));
}
