#include "DXVertexLayout.h"
#include "DXDevice.h"
#include "llerror.h"
#include <unordered_map>

namespace
{
    struct AttribDesc
    {
        const char* semantic;
        UINT semantic_index;
        DXGI_FORMAT format;
    };

    // Indexed by attribute bit position - see DXVertexLayout.h's table comment.
    // HLSL/D3D11 always splits a trailing digit off a semantic name into a
    // separate SemanticIndex, both in the compiled shader's own reflected
    // signature AND in how CreateInputLayout matches against it - "TEXCOORD0"
    // in .hlsl source is NOT the literal string "TEXCOORD0" at the ABI level,
    // it's SemanticName="TEXCOORD"/SemanticIndex=0. Supplying "TEXCOORD0" as
    // a literal SemanticName here (as an earlier version of this table did)
    // never matches the shader's real "TEXCOORD"+0 - CreateInputLayout
    // rejects it as if the element weren't provided at all. Same applies to
    // COLOR0/COLOR1 ("COLOR"+0/"COLOR"+1). POSITION/NORMAL/TANGENT/
    // BLENDWEIGHT/TEXTUREINDEX have no embedded digit, so they're unaffected.
    const AttribDesc kAttribs[6] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT },    // bit 0 - TYPE_VERTEX
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT },    // bit 1 - TYPE_NORMAL
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT },       // bit 2 - TYPE_TEXCOORD0
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT },       // bit 3 - TYPE_TEXCOORD1
        { "TEXCOORD", 2, DXGI_FORMAT_R32G32_FLOAT },       // bit 4 - TYPE_TEXCOORD2
        { "TEXCOORD", 3, DXGI_FORMAT_R32G32_FLOAT },       // bit 5 - TYPE_TEXCOORD3
    };
    const AttribDesc kColor = { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM };        // bit 6/7 - TYPE_COLOR/TYPE_EMISSIVE
    const AttribDesc kTangent = { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT }; // bit 8 - TYPE_TANGENT
    const AttribDesc kWeight = { "BLENDWEIGHT", 0, DXGI_FORMAT_R32_FLOAT };            // bit 9 - TYPE_WEIGHT (avatar body soft-skin)
    // S24 (2026-08-09, task #171 fix): CORRECTED - bit 10 is TYPE_WEIGHT4,
    // NOT bit 12. The real AttributeType enum order (llvertexbuffer.h) is
    // ...,TANGENT=8,WEIGHT=9,WEIGHT4=10,CLOTHWEIGHT=11,JOINT=12,
    // TEXTURE_INDEX=13 - task #168's original fix trusted a user-compiled
    // report's claim ("bit 10=JOINT, bit 12=WEIGHT4") without re-deriving it
    // from the actual enum, and built support for the WRONG bit (12,
    // genuinely TYPE_JOINT) while leaving the real WEIGHT4 bit (10) still
    // rejected - confirmed via log evidence: "data_mask 0x254f ... not yet
    // supported ... shader='Skinned Material Shader 24'" (0x254f has bit 10
    // set, not bit 12), the actual reason rigged mesh bodies/clothing still
    // didn't render after task #170's pool work. kWeight4 (rigged-mesh
    // attachment/clothing skinning, objectSkinV.hlsl's "weight4 :
    // BLENDWEIGHT" input) now correctly lives at bit 10. Shares the same
    // "BLENDWEIGHT" semantic name/index as kWeight (bit 9, float) - safe
    // because a given draw's data_mask only ever carries ONE of the two
    // (classic avatar body vs. rigged attachment are mutually exclusive
    // vertex formats), never both at once.
    const AttribDesc kWeight4 = { "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT };  // bit 10 - TYPE_WEIGHT4 (rigged mesh soft-skin)
    const AttribDesc kClothWeight = { "COLOR", 1, DXGI_FORMAT_R32G32B32A32_FLOAT };   // bit 11 - TYPE_CLOTHWEIGHT
    const AttribDesc kTextureIndex = { "TEXTUREINDEX", 0, DXGI_FORMAT_R32_UINT };      // bit 13 - TYPE_TEXTURE_INDEX (packed into position.w)

    // S24 (2026-08-09, task #171 fix): bit 10 (WEIGHT4) removed from this
    // set - real HLSL support now exists (kWeight4 above, corrected to the
    // right bit). Bit 12 (TYPE_JOINT) stays unsupported: it's GLTF-scene-only
    // (gltf/primitive.cpp), and no HLSL shader in the tree declares a
    // BLENDINDICES input yet (GLTF skinning uses a separate GLTFJoints-
    // cbuffer technique, not a vertex attribute - see task #154) - fixing
    // this bit alone would unblock nothing today.
    constexpr uint32_t kUnsupportedBits = (1u << 12);

    struct Key
    {
        uint32_t data_mask;
        const void* vs_bytecode;

        bool operator==(const Key& other) const
        {
            return data_mask == other.data_mask && vs_bytecode == other.vs_bytecode;
        }
    };

    struct KeyHash
    {
        size_t operator()(const Key& k) const
        {
            return std::hash<uint32_t>()(k.data_mask) ^ (std::hash<const void*>()(k.vs_bytecode) << 1);
        }
    };

    std::unordered_map<Key, ID3D11InputLayout*, KeyHash> sCache;

    // DXUIBatch plan: fixed 3-element layout matching DXUIVertex exactly
    // (pos float3 @0, color R8G8B8A8_UNORM @12, uv float2 @16, stride 24,
    // one input slot). Keyed only on vs_bytecode - see header comment.
    std::unordered_map<const void*, ID3D11InputLayout*> sUICache;
}

ID3D11InputLayout* DXVertexLayout::getOrCreate(uint32_t data_mask, const void* vs_bytecode, size_t vs_bytecode_size, const char* debug_name)
{
    if (data_mask & kUnsupportedBits)
    {
        LL_WARNS("VertexBuffer") << "DXVertexLayout: data_mask 0x" << std::hex << data_mask << std::dec
            << " requests attributes not yet supported under DX_RENDER (skinning/indexed-texture), shader='"
            << (debug_name ? debug_name : "?") << "'" << LL_ENDL;
        llassert(false);
        return nullptr;
    }

    Key key{ data_mask, vs_bytecode };
    auto iter = sCache.find(key);
    if (iter != sCache.end())
    {
        return iter->second;
    }

    // One input slot per active attribute - see this file's header comment
    // for why (LLVertexBuffer is struct-of-arrays, not interleaved). Slot
    // assignment order here (bits 0-5, then color/emissive, then tangent,
    // then weight, then weight4, then clothweight, then texture index) MUST
    // match LLVertexBuffer::setupVertexBuffer()'s DX_RENDER branch, which
    // binds the actual buffers/strides/offsets to these same slots.
    // S24 (2026-08-09, task #157): bumped 11->12 to fit the new WEIGHT4
    // element (max possible: 6 simple + color/emissive(1) + tangent + weight
    // + weight4 + clothweight + textureindex = 12).
    D3D11_INPUT_ELEMENT_DESC elements[12];
    UINT count = 0;

    for (uint32_t bit = 0; bit < 6; ++bit)
    {
        if (data_mask & (1u << bit))
        {
            elements[count].SemanticName = kAttribs[bit].semantic;
            elements[count].SemanticIndex = kAttribs[bit].semantic_index;
            elements[count].Format = kAttribs[bit].format;
            elements[count].InputSlot = count;
            elements[count].AlignedByteOffset = 0;
            elements[count].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
            elements[count].InstanceDataStepRate = 0;
            ++count;
        }
    }

    bool has_color = (data_mask & (1u << 6)) != 0;
    bool has_emissive = (data_mask & (1u << 7)) != 0;
    if (has_color || has_emissive)
    {
        elements[count].SemanticName = kColor.semantic;
        elements[count].SemanticIndex = kColor.semantic_index;
        elements[count].Format = kColor.format;
        elements[count].InputSlot = count;
        elements[count].AlignedByteOffset = 0;
        elements[count].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        elements[count].InstanceDataStepRate = 0;
        ++count;
    }

    if (data_mask & (1u << 8))
    {
        elements[count].SemanticName = kTangent.semantic;
        elements[count].SemanticIndex = kTangent.semantic_index;
        elements[count].Format = kTangent.format;
        elements[count].InputSlot = count;
        elements[count].AlignedByteOffset = 0;
        elements[count].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        elements[count].InstanceDataStepRate = 0;
        ++count;
    }

    if (data_mask & (1u << 9))
    {
        elements[count].SemanticName = kWeight.semantic;
        elements[count].SemanticIndex = kWeight.semantic_index;
        elements[count].Format = kWeight.format;
        elements[count].InputSlot = count;
        elements[count].AlignedByteOffset = 0;
        elements[count].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        elements[count].InstanceDataStepRate = 0;
        ++count;
    }

    if (data_mask & (1u << 10))
    {
        elements[count].SemanticName = kWeight4.semantic;
        elements[count].SemanticIndex = kWeight4.semantic_index;
        elements[count].Format = kWeight4.format;
        elements[count].InputSlot = count;
        elements[count].AlignedByteOffset = 0;
        elements[count].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        elements[count].InstanceDataStepRate = 0;
        ++count;
    }

    if (data_mask & (1u << 11))
    {
        elements[count].SemanticName = kClothWeight.semantic;
        elements[count].SemanticIndex = kClothWeight.semantic_index;
        elements[count].Format = kClothWeight.format;
        elements[count].InputSlot = count;
        elements[count].AlignedByteOffset = 0;
        elements[count].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        elements[count].InstanceDataStepRate = 0;
        ++count;
    }

    if (data_mask & (1u << 13))
    {
        elements[count].SemanticName = kTextureIndex.semantic;
        elements[count].SemanticIndex = kTextureIndex.semantic_index;
        elements[count].Format = kTextureIndex.format;
        elements[count].InputSlot = count;
        // The +12 (position.w) shift is already applied to this slot's
        // IASetVertexBuffers offset (LLVertexBuffer::setupVertexBuffer()
        // passes mOffsets[TYPE_TEXTURE_INDEX], which calcOffsets() computes
        // as mOffsets[TYPE_VERTEX]+12) - AlignedByteOffset must be 0 here,
        // matching every other slot's convention, or the +12 gets applied
        // twice (reads 24 bytes past position's start instead of 12).
        elements[count].AlignedByteOffset = 0;
        elements[count].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        elements[count].InstanceDataStepRate = 0;
        ++count;
    }

    ID3D11InputLayout* layout = nullptr;
    HRESULT hr = gDXDevice.getDevice()->CreateInputLayout(elements, count, vs_bytecode, vs_bytecode_size, &layout);
    if (FAILED(hr))
    {
        LL_WARNS("VertexBuffer") << "CreateInputLayout failed, hr=0x" << std::hex << (unsigned long)hr
            << ", data_mask=0x" << data_mask << std::dec << ", shader='" << (debug_name ? debug_name : "?")
            << "'" << LL_ENDL;
        // Cache the failure too (nullptr), not just successes - this is
        // called every frame a given (data_mask, vs_bytecode) combo draws,
        // and CreateInputLayout's own outcome for a fixed bytecode blob can
        // never change on a later call. Without this, an unresolved failure
        // re-runs CreateInputLayout and re-logs every single frame - this is
        // exactly what produced a 130MB log in one session (675517 repeats
        // of the same line) before this was added.
        sCache[key] = nullptr;
        return nullptr;
    }

    sCache[key] = layout;
    return layout;
}

ID3D11InputLayout* DXVertexLayout::getOrCreateUILayout(const void* vs_bytecode, size_t vs_bytecode_size, const char* debug_name)
{
    auto iter = sUICache.find(vs_bytecode);
    if (iter != sUICache.end())
    {
        return iter->second;
    }

    // Matches DXUIBatch.h's DXUIVertex exactly: pos float3 @0, color
    // R8G8B8A8_UNORM @12, uv float2 @16, stride 24, one input slot. A shader
    // that doesn't consume one of these (e.g. solidcolorV.hlsl has no COLOR0
    // input) is fine - CreateInputLayout only requires elements the shader's
    // input signature DOES declare; unconsumed extras are simply ignored.
    D3D11_INPUT_ELEMENT_DESC elements[3] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,   0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,     0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    ID3D11InputLayout* layout = nullptr;
    HRESULT hr = gDXDevice.getDevice()->CreateInputLayout(elements, 3, vs_bytecode, vs_bytecode_size, &layout);
    if (FAILED(hr))
    {
        LL_WARNS("VertexBuffer") << "DXVertexLayout::getOrCreateUILayout: CreateInputLayout failed, hr=0x" << std::hex << (unsigned long)hr
            << std::dec << ", shader='" << (debug_name ? debug_name : "?") << "'" << LL_ENDL;
        sUICache[vs_bytecode] = nullptr;
        return nullptr;
    }

    sUICache[vs_bytecode] = layout;
    return layout;
}

void DXVertexLayout::clear()
{
    for (auto& entry : sCache)
    {
        if (entry.second)
        {
            entry.second->Release();
        }
    }
    sCache.clear();

    for (auto& entry : sUICache)
    {
        if (entry.second)
        {
            entry.second->Release();
        }
    }
    sUICache.clear();
}
