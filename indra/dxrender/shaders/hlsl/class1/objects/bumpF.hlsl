// -----------------------------------------------------------------------------
// Textures + Samplers
// In DX11, textures and samplers are separate objects.
// -----------------------------------------------------------------------------
Texture2D texture0 : register(t0);
Texture2D texture1 : register(t1);

SamplerState samp0 : register(s0);
SamplerState samp1 : register(s1);

// -----------------------------------------------------------------------------
// Inputs from the vertex shader
// -----------------------------------------------------------------------------
struct PSInput
{
    float2 texcoord0 : TEXCOORD0;
    float2 texcoord1 : TEXCOORD1;
    float3 position : TEXCOORD2; // vary_position
};

// -----------------------------------------------------------------------------
// mirrorClip() equivalent
// GLSL version discards via clip(). In HLSL, clip(x) works the same.
// -----------------------------------------------------------------------------
void mirrorClip(float3 pos)
{
    // You didn’t include the GLSL body, so I assume it calls clip()
    // Example:
    // clip(pos.z);  // or whatever the original logic is
}

// -----------------------------------------------------------------------------
// Pixel Shader
// -----------------------------------------------------------------------------
float4 main(PSInput IN) : SV_Target
{
    mirrorClip(IN.position);

    float tex0 = texture0.Sample(samp0, IN.texcoord0).a;
    float tex1 = texture1.Sample(samp1, IN.texcoord1).a;

    float value = tex0 + (1.0 - tex1) - 0.5;

    return max(float4(value, value, value, value), float4(0, 0, 0, 0));
}
