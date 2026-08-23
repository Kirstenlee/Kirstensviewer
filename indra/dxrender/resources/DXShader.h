#pragma once
#include <d3d11.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

// Compiles + owns one program's D3D11 vertex/pixel shader pair. Unlike GL,
// there is no separate link step - LLGLSLShader::createShaderDX() hands this
// class one fully concatenated HLSL blob per stage (entry file text + all
// attached utility files' text) and this class compiles+creates the shader
// object directly.
class DXShader
{
public:
    ~DXShader() { reset(); }

    bool compileVertexShader(const std::string& source, const std::string& debugName);
    bool compilePixelShader(const std::string& source, const std::string& debugName);
    void reset();

    // GLSL lets an attached utility file declare a free-standing
    // "in vec4 weight;" attribute (e.g. class1/avatar/avatarSkinV.glsl's
    // getSkinnedTransform()) that main()'s own entry file never touches -
    // legal there since GL links multiple compiled objects into one program.
    // HLSL has no equivalent: every vertex input must flow through the entry
    // function's parameter struct, so a separately-concatenated utility
    // function (e.g. avatarSkinV.hlsl) referencing a bare "weight" global
    // won't compile as-is. Call this on the fully concatenated vertex source
    // (entry file + attached utility files, before compileVertexShader())
    // to bridge the two: if a VSInput field with HLSL semantic BLENDWEIGHT
    // is found (the avatar body mesh's single-float soft-skin blend value -
    // see DXVertexLayout.h's bit-9 comment), declares a matching global and
    // assigns it from IN.<name> as main()'s first statement. No-ops if no
    // such field is present (i.e. every non-skinned shader, unaffected).
    static void injectSkinningInputs(std::string& vertex_source);

    // Same idea as injectSkinningInputs(), for indexedTextureV.hlsl's
    // texture_index (real vertex attribute, TEXTUREINDEX semantic)/
    // vary_texture_index (consumed by diffuseLookup() in the fragment
    // stage) pair - detects that file's real passTextureIndex() body (vs.
    // nonindexedTextureV.hlsl's no-op) and wires both the input and the
    // vertex-to-pixel output side. No-ops if indexed texturing isn't in
    // use for this compile.
    static void injectTextureIndexInputs(std::string& vertex_source);

    // S24 (DX_RENDER, 2026-07-30): D3DCompile() is called with a null
    // ID3DInclude (see compileHLSL() in the .cpp), so `#include "x.hlsli"`
    // directives are not natively resolved by the compiler. This does the
    // resolution ourselves, in plain text, the same way
    // injectSkinningInputs()/injectTextureIndexInputs() already mutate the
    // concatenated source before compilation - simpler and lower-risk than
    // wiring up a real ID3DInclude COM object for what's currently a small,
    // fixed set of shared struct headers. Paths are resolved relative to
    // the shaders root directory (one level above "classN/") - e.g.
    // `#include "varying/uiVarying.hlsli"` reads
    // app_settings/shaders/varying/uiVarying.hlsli. Single-pass (included
    // files are not themselves scanned for further #includes) - sufficient
    // for plain struct-definition headers with no nested includes of their
    // own. No-op (leaves the line untouched, D3DCompile will then fail with
    // a normal "unexpected token" error pointing at it) if the referenced
    // file can't be read, rather than silently swallowing a real authoring
    // mistake.
    static void resolveIncludes(std::string& source);

    ID3D11VertexShader* getVS() const { return mVS; }
    ID3D11PixelShader* getPS() const { return mPS; }

    // Kept alive after compileVertexShader() - Milestone 3's DXVertexLayout
    // needs the exact VS bytecode to build a matching ID3D11InputLayout.
    ID3DBlob* getVSBytecode() const { return mVSBytecode; }

    // GL populates LLGLSLShader::mAttributeMask via mapAttributes()'s
    // glGetAttribLocation() calls after linking - createShaderDX() has no
    // link step to hook an equivalent into, so mAttributeMask silently
    // stayed 0 for every shader under DX_RENDER (broke LLRender::flush()'s
    // genBuffer(shader->mAttributeMask, ...), the first real immediate-mode
    // 2D/UI vertex buffer allocation to ever run). This is the DX11-native
    // equivalent: reflects which of the known attribute semantics this VS's
    // real input signature actually declares, via D3DReflect. Bit positions
    // match LLVertexBuffer::AttributeType - same table as DXVertexLayout.h,
    // reproduced here for the same "no GL/llrender dependency" reason.
    uint32_t reflectVertexAttributeMask() const;

    // Top-level `uniform` HLSL globals (e.g. "modelview_matrix") land in an
    // implicit "$Globals" constant buffer that D3DCompile builds for you -
    // reflected out via D3DReflect right after a successful compile (see
    // reflectConstants()) so callers can push CPU-side matrix/scalar data by
    // the same names the .hlsl source already uses, without this class or
    // its callers needing to hand-compute cbuffer packing/offsets.
    // Returns false (no-op) if this shader doesn't declare that uniform -
    // e.g. a pixel shader that only declares textures/samplers has no
    // $Globals cbuffer at all.
    bool setUniformMatrix4(const std::string& name, const float* column_major_4x4);
    bool setUniformMatrix3(const std::string& name, const float* column_major_3x3);

    // Raw contiguous-float-array uniform (e.g. "matrixPalette[45]", a plain
    // array of float4 elements with no inter-element padding in HLSL, unlike
    // a float3[] which pads each element to 16 bytes) - a straight memcpy of
    // float_count floats into the reflected offset, no reshaping. Returns
    // false if the constant isn't declared or float_count*4 bytes would
    // overrun its reflected size (name found in the wrong shader stage, or a
    // caller passing more elements than the shader declares).
    bool setUniformFloatArray(const std::string& name, const float* data, size_t float_count);

    // S24 (2026-08-03): general form of setUniformMatrix3()'s per-column
    // padding trick - HLSL's cbuffer packing rule stores EVERY array
    // element (regardless of whether the underlying type is scalar,
    // float2, or float3 - float4 is the only size that doesn't need this)
    // in its own 16-byte-aligned slot, never packed tighter. Needed for
    // uniform1fv()/uniform2fv()/uniform3fv() (e.g. terrain's
    // "float3 emissiveColors[4]") - a plain setUniformFloatArray() memcpy
    // would read/write at the wrong offsets past the first element.
    // component_count is the real per-element float count (1-3);
    // element_count is how many padded 16-byte slots to write.
    bool setUniformPaddedArray(const std::string& name, const float* data, int component_count, size_t element_count);

    // Pushes the CPU staging buffer to the GPU (Map(WRITE_DISCARD)/Unmap) if
    // any setUniform* call queued a change since the last upload. No-op if
    // this shader has no $Globals cbuffer.
    void uploadConstants();

    ID3D11Buffer* getConstantBuffer() const { return mConstantBuffer; }

    // S24 (task #79 follow-on): the register(bN) slot the compiler actually
    // assigned to $Globals for THIS shader. Almost always 0 (every shader
    // examined so far declares no other explicit register(bN) cbuffer, so
    // the compiler's implicit-cbuffer allocator picks the lowest free slot,
    // which is always b0 when nothing else claims it) - but not guaranteed:
    // e.g. class1/gltf/pbrmetallicroughnessV/F.hlsl explicitly declare
    // `cbuffer GLTFMaterials : register(b0)`, which pushes $Globals to b1
    // for those two files specifically. Reflected via
    // GetResourceBindingDescByName() in reflectConstants() rather than
    // assumed, so callers (LLRender::syncMatrices()) bind to the real slot
    // instead of a hardcoded 0.
    UINT getConstantBufferBindPoint() const { return mConstantBufferBindPoint; }

    // S24 (2026-08-03): named Texture2D resource -> its register(tN) bind
    // point (D3D11_SHADER_INPUT_BIND_DESC::BindPoint), reflected the same
    // way reflectConstants() reflects $Globals variables. This is the
    // DX-native equivalent of GL's glGetUniformLocation()-based texture-
    // channel mapping (LLGLSLShader::mTexture[], populated via
    // mapUniformTextureChannel()) - without it, LLGLSLShader::bindTexture()
    // has no way to know which register a named uniform like "diffuseMap"
    // corresponds to, and was a hardcoded DX_RENDER no-op as a result (see
    // llglslshader.cpp's bindTexture(S32, LLTexture*, ...) - real root
    // cause of PBR materials showing an unrelated, leftover-bound texture
    // depending on draw order/camera angle, since every bind() call for
    // t0-t3 was silently skipped).
    bool getTextureBindPoint(const std::string& name, UINT& out_bind_point) const;

private:
    struct ConstantInfo
    {
        UINT offset;
        UINT size;
    };

    void reflectConstants(const void* bytecode, size_t size);

    ID3D11VertexShader* mVS = nullptr;
    ID3D11PixelShader* mPS = nullptr;
    ID3DBlob* mVSBytecode = nullptr;

    ID3D11Buffer* mConstantBuffer = nullptr;
    UINT mConstantBufferBindPoint = 0;
    std::unordered_map<std::string, ConstantInfo> mConstants;
    std::vector<uint8_t> mConstantStaging;
    bool mConstantsDirty = false;

    std::unordered_map<std::string, UINT> mTextureBindPoints;
};
