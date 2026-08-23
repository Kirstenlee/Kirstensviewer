#pragma once
#include <d3d11.h>
#include <cstdint>
#include <cstddef>

// Builds and caches ID3D11InputLayout objects for LLVertexBuffer's
// DX_RENDER path. CreateInputLayout validates a layout against a specific
// compiled vertex shader's input signature, so a layout is a function of
// (which attributes are present, which VS it will be bound with) - cached
// process-wide, keyed on both.
//
// LLVertexBuffer is struct-of-arrays, not interleaved: calcOffsets()
// (llrender/llvertexbuffer.cpp) gives every attribute type its own
// contiguous block spanning all vertices, rather than interleaving
// attributes per-vertex. D3D11 has no single-slot "stride" that can express
// that, so each active attribute gets its own input slot, all bound to the
// same physical ID3D11Buffer at different offsets/strides via
// IASetVertexBuffers (see LLVertexBuffer::setupVertexBuffer()'s DX_RENDER
// branch, which must enumerate attributes in this exact same bit order so
// its per-slot buffer/stride/offset arrays line up with the InputSlot
// indices assigned here).
//
// data_mask uses the same bit positions as LLVertexBuffer::AttributeType
// (llrender/llvertexbuffer.h) - this header intentionally does not include
// that one (dxrender has no GL/llrender dependency by design), so the bit
// positions are reproduced here as plain integers and must be kept in sync:
//   0 POSITION(R32G32B32)   1 NORMAL(R32G32B32)     2 TEXCOORD0(R32G32)
//   3 TEXCOORD1(R32G32)     4 TEXCOORD2(R32G32)     5 TEXCOORD3(R32G32)
//   6 COLOR0(R8G8B8A8_UNORM)                        8 TANGENT(R32G32B32A32)
//   9 BLENDWEIGHT(R32)                             11 COLOR1(R32G32B32A32)
//  13 TEXTUREINDEX(R32_UINT)
// "TEXCOORD0"/"COLOR0"/"COLOR1" above are the .hlsl source spelling only -
// D3D11 always splits a trailing digit off any semantic name into a
// separate SemanticIndex at the ABI level (both in a compiled shader's own
// reflected input signature and in what CreateInputLayout matches against),
// so the real SemanticName/SemanticIndex pairs used in the .cpp are
// "TEXCOORD"+0/1/2/3 and "COLOR"+0/1, never the literal digit-suffixed
// strings. Getting this wrong silently produces a data_mask that
// under-detects real inputs (DXShader::reflectVertexAttributeMask()) and/or
// an input-element array CreateInputLayout rejects as incomplete (this
// file) - this was a real, shipped bug, not a hypothetical one.
// Bit 7 (EMISSIVE) has no dedicated element yet - mirrors LLVertexBuffer's
// own GL fallback (setupVertexBuffer()) of feeding emissive data into the
// COLOR0 slot when no separate color attribute is present. Bit 9 (WEIGHT)
// is the avatar body mesh's single-float soft-skin blend value (GL binds it
// as a 1-component glVertexAttribPointer feeding a vec4/float4 shader input -
// D3D11's input assembler auto-fills unspecified components to (0,0,0,1)
// exactly like GL does, so R32_FLOAT here reproduces that without needing a
// 4-component element). Bit 11 (CLOTHWEIGHT) is the avatar cloth-simulation
// wind-offset input, always requested alongside WEIGHT for
// LLDrawPoolAvatar's body mesh (see LLDrawPoolAvatar::VERTEX_DATA_MASK) -
// phase 5.10a, 2026-07-19. Bit 13 (TEXTURE_INDEX, stage 6 phase 1,
// 2026-07-21) has no separate storage at all - per LLVertexBuffer's own
// sTypeSize comment ("actually exists as position.w"), it's packed into
// POSITION's otherwise-unused W component (position is stored as a full
// 16-byte LLVector4 even though only XYZ is geometric data). GL reads it via
// a *second*, separate glVertexAttribIPointer call at byte offset 12 into
// the same buffer, reinterpreted as GL_UNSIGNED_INT (not normalized/
// converted) - reproduced here the same way: its own input slot, same
// ID3D11Buffer as POSITION, DXGI_FORMAT_R32_UINT, stride matching POSITION's
// own. The +12 shift itself is applied once, on the IASetVertexBuffers side
// (mOffsets[TYPE_TEXTURE_INDEX] is already computed as
// mOffsets[TYPE_VERTEX]+12 by the shared, non-DX_RENDER-specific
// calcOffsets()) - this element's own AlignedByteOffset must be 0, matching
// every other slot, or the shift gets applied twice.
// Bits 10/12 (WEIGHT4/JOINT - rigged mesh-attachment hardware skinning)
// remain unsupported; getOrCreate() still asserts if asked for them -
// deliberately deferred (see the project's rigged-geometry gap notes),
// since WEIGHT4-driven skinning (getObjectSkinnedTransform()) additionally
// needs its own vertex-buffer slot wiring and joint-palette constant-buffer
// delivery, neither built yet for that path.
class DXVertexLayout
{
public:
    // debug_name is purely diagnostic (logged on CreateInputLayout failure,
    // see .cpp) - pass the bound LLGLSLShader's mName so a failure log
    // identifies which shader/mask combo actually failed, rather than just
    // the raw HRESULT.
    static ID3D11InputLayout* getOrCreate(uint32_t data_mask, const void* vs_bytecode, size_t vs_bytecode_size, const char* debug_name = nullptr);

    // DXUIBatch plan: 2D UI/text (interface/uiV.hlsl and friends) never
    // needs the struct-of-arrays, up-to-11-input-slot machinery above - it's
    // one fixed, interleaved vertex format (DXUIBatch.h's DXUIVertex:
    // POSITION float3 @0, COLOR0 R8G8B8A8_UNORM @12, TEXCOORD0 float2 @16,
    // stride 24, all in input slot 0). A shader that doesn't consume one of
    // these elements (e.g. solidcolorV.hlsl has no COLOR0 input) is fine -
    // CreateInputLayout only requires every element the shader's input
    // signature DOES declare to be present and matching, extra elements are
    // simply unused. Cached separately from getOrCreate() above (keyed only
    // on vs_bytecode - the element layout itself never varies) so this
    // dedicated path can't collide with or be confused for a mesh data_mask.
    static ID3D11InputLayout* getOrCreateUILayout(const void* vs_bytecode, size_t vs_bytecode_size, const char* debug_name = nullptr);

    // Releases every cached layout (both caches) - call on full renderer shutdown.
    static void clear();
};
