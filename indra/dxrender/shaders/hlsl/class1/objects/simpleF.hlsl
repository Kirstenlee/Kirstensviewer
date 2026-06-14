// -----------------------------------------------------------------------------
// Forward declaration of the lighting function
// (You will fill this in with the actual lighting logic)
// -----------------------------------------------------------------------------
float4 default_lighting();

// -----------------------------------------------------------------------------
// Pixel Shader Entry Point
// -----------------------------------------------------------------------------
float4 main() : SV_Target
{
    return default_lighting();
}
