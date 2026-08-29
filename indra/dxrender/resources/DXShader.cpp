#include "DXShader.h"
#include "DXDevice.h"
#include "llerror.h"
#include "lldir.h"
#include "llfile.h"
#include "hbxxh.h"
#include "lluuid.h"
#include <algorithm>
#include <d3dcompiler.h>
#include <fstream>
#include <regex>
#include <unordered_set>
#include <vector>

bool DXShader::sShaderCacheEnabled = false;

namespace
{
    bool compileHLSL(const std::string& source, const std::string& debugName, const char* entry_point, const char* target, ID3DBlob** out_blob)
    {
        ID3DBlob* error_blob = nullptr;
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        // S24 (2026-08-29, task #278): explicit max optimization for every
        // Release shader compile (this project always builds Release per
        // standing convention - see feedback_s24_build_system memory) -
        // D3DCompile's default (no D3DCOMPILE_OPTIMIZATION_LEVEL* flag) is
        // NOT level 3, so this was leaving real GPU-side shader codegen
        // quality on the table this whole time. Free win, zero behavior
        // change beyond faster-running shader bytecode.
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

        HRESULT hr = D3DCompile(
            source.c_str(), source.size(),
            debugName.c_str(),
            nullptr, nullptr,
            entry_point, target,
            flags, 0,
            out_blob, &error_blob);

        if (FAILED(hr))
        {
            // Untagged (not "ShaderLoading") - that tag is suppressed to
            // ERROR-only in logcontrol.xml to quiet routine per-file GL
            // shader-load spam, which would otherwise silently swallow the
            // one piece of text that explains a real compile failure.
            LL_WARNS() << "D3DCompile failed for " << debugName << ": "
                << (error_blob ? (const char*)error_blob->GetBufferPointer() : "unknown error") << LL_ENDL;
            if (error_blob)
            {
                error_blob->Release();
            }

            // Dump the exact failing concatenated source next to the logs -
            // the error's line/column refers to this generated text, not to
            // any single .hlsl file on disk, so the log message alone isn't
            // enough to locate the actual bad line.
            std::string sanitized_name = debugName;
            for (auto& c : sanitized_name)
            {
                if (!isalnum((unsigned char)c)) c = '_';
            }
            std::string dump_path = gDirUtilp->getExpandedFilename(LL_PATH_LOGS, sanitized_name + "_" + target + "_failed.hlsl");
            std::ofstream dump_file(dump_path.c_str());
            if (dump_file)
            {
                dump_file << source;
                LL_WARNS() << "Dumped failing HLSL source to " << dump_path << LL_ENDL;
            }

            return false;
        }

        if (error_blob)
        {
            // Non-fatal warnings only - compile still succeeded. S24
            // (2026-07-23): was LL_INFOS("ShaderLoading") - "ShaderLoading"
            // is suppressed to ERROR-only in logcontrol.xml AND LL_INFOS is
            // separately gated project-wide (see feedback_s24_coding_
            // conventions memory) - stacking both meant compiler warnings
            // for a shader that compiled "successfully" have been
            // completely invisible all session. Untagged LL_WARNS so they
            // actually surface - "no text" investigation, round 8.
            LL_WARNS() << "D3DCompile warnings for " << debugName << ": "
                << (const char*)error_blob->GetBufferPointer() << LL_ENDL;
            error_blob->Release();
        }

        return true;
    }

    // S24 (2026-08-29): see DXShader.h's sShaderCacheEnabled comment for the
    // full rationale. Reuses GL's shader_cache directory (llshadermgr.cpp)
    // so the existing purge/reset mechanisms (KVTweaks "Purge Shader Cache",
    // RenderPurgeShaderCacheOnExit) clear this too, with no changes needed.
    std::string getDXShaderCacheDir()
    {
        std::string dir = gDirUtilp->getExpandedFilename(LL_PATH_CACHE, "shader_cache");
        LLFile::mkdir(dir);
        return dir;
    }

    // Keyed on the exact final concatenated HLSL text (already fully
    // resolved - #include expanded, feature #defines baked in by
    // buildDXShaderHeader() before this text ever reaches DXShader) plus the
    // compile target - any permutation/feature/shader-level difference
    // naturally produces a different key, so no separate cache-version
    // tagging is needed the way GL's mShaderCacheVersion needs one.
    std::string dxShaderCachePath(const std::string& source, const char* target)
    {
        HBXXH128 hash_obj;
        hash_obj.update(source);
        hash_obj.update(std::string(target));
        return gDirUtilp->add(getDXShaderCacheDir(), hash_obj.digest().asString() + ".dxbc");
    }

    bool loadCachedBlob(const std::string& path, std::vector<uint8_t>& out)
    {
        std::ifstream in(path.c_str(), std::ios::binary | std::ios::ate);
        if (!in.is_open())
        {
            return false;
        }
        std::streampos size = in.tellg();
        if (size <= 0)
        {
            return false;
        }
        out.resize((size_t)size);
        in.seekg(0, std::ios::beg);
        return (bool)in.read(reinterpret_cast<char*>(out.data()), size);
    }

    void saveCachedBlob(const std::string& path, const void* data, size_t size)
    {
        std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
        if (out)
        {
            out.write(reinterpret_cast<const char*>(data), size);
        }
    }

    // Tries the disk cache first (if eligible); returns the compiled/loaded
    // blob, or nullptr on outright failure. *used_cache tells the caller
    // whether to fall back to a real compile if CreateVertexShader/
    // CreatePixelShader ends up rejecting this blob (a truncated file from a
    // crash mid-write, e.g.) rather than failing the shader outright.
    ID3DBlob* getOrCompileHLSL(const std::string& source, const std::string& debugName, const char* entry_point, const char* target, std::string* out_cache_path, bool* used_cache)
    {
        *used_cache = false;
        out_cache_path->clear();

        if (DXShader::isCacheEligible(debugName))
        {
            *out_cache_path = dxShaderCachePath(source, target);
            std::vector<uint8_t> bytes;
            if (loadCachedBlob(*out_cache_path, bytes) && !bytes.empty())
            {
                ID3DBlob* blob = nullptr;
                if (SUCCEEDED(D3DCreateBlob(bytes.size(), &blob)))
                {
                    memcpy(blob->GetBufferPointer(), bytes.data(), bytes.size());
                    *used_cache = true;
                    LL_INFOS("ShaderCache") << "S24: Loaded cached DX bytecode for " << debugName << " (" << target << ")" << LL_ENDL;
                    return blob;
                }
            }
        }

        ID3DBlob* blob = nullptr;
        if (!compileHLSL(source, debugName, entry_point, target, &blob))
        {
            return nullptr;
        }

        if (!out_cache_path->empty())
        {
            saveCachedBlob(*out_cache_path, blob->GetBufferPointer(), blob->GetBufferSize());
        }
        return blob;
    }
}

bool DXShader::isCacheEligible(const std::string& debugName)
{
    if (!sShaderCacheEnabled)
    {
        return false;
    }

    // S24 allowlist, extended in passes as confidence grows (see
    // DXShader.h's sShaderCacheEnabled comment) - each addition gets its own
    // live playtest, same as any other DX_RENDER change. Deliberately
    // excludes anything with a known live issue for now: the GLTF/PBR family
    // (an active D3DCompile failure - see "redefinition of 'clipPlane'" in
    // the log - task #262's HUD-black-PBR history), the reflection-probe/SSR
    // family (task #156 umbrella still has open children), the full avatar
    // body shaders (gDeferredAvatarProgram and siblings - the real per-
    // vertex skin animation path, not to be confused with the rigged-
    // attachment "Skinned Material" permutations below, which are simpler
    // and now included), shadow-cascade shaders, and the Buffer
    // Visualization shader specifically (task #261's AMD lazy-compile
    // lockup).
    //
    // 2026-08-26 (pilot): the occlusion probe box shader - simple, no
    // permutations, low blast radius, proven live over one session.
    //
    // 2026-08-29 (pass 2): bump/material shaders plus a batch of other
    // structurally simple, single-purpose utility/highlight shaders with no
    // permutation loop of their own.
    //
    // 2026-08-29 (pass 3): the big one for startup time -
    // gDeferredMaterialProgram[LLMaterial::SHADER_COUNT*2] (llviewershadermgr.cpp)
    // is a 32-way permutation loop (normal map x specular map x 4 alpha
    // modes x sun-shadow x rigged, llmaterial.h's SHADER_COUNT=16) that
    // compiles unconditionally at every startup regardless of what's in
    // view - named "Material Shader %d"/"Skinned Material Shader %d" by
    // index, matched by prefix below since the exact names are only known at
    // runtime. This is most of what's actually driving the "a lot of
    // materials shaders compiling at startup" load-time cost - by far the
    // biggest lever here. Also added: the indexed-texture diffuse/fullbright
    // families (deferred/materialV+F.hlsl's simpler siblings - same permute-
    // by-alpha-mode shape, much smaller permutation count) and the emissive
    // shader.
    static const std::unordered_set<std::string> allowlist = {
        "Occlusion Cube Shader",
        "Occlusion Shader",

        "Deferred Bump Shader",
        "Bump Shader",

        "Highlight Shader",
        "Highlight Normals Shader",
        "Highlight Spec Shader",
        "Solid Color Shader",
        "Debug Shader",
        "Clip Shader",
        "Alpha Mask Shader",
        "Copy Shader",
        "Copy Depth Shader",
        "Draw Color Shader",
        "Two Texture Compare Shader",
        "One Texture Filter Shader",

        "Deferred Diffuse Shader",
        "Deferred Diffuse Alpha Mask Shader",
        "Deferred Diffuse Non-Indexed Alpha Mask Shader",
        "Deferred Diffuse Non-Indexed Alpha Mask No Color Shader",
        "Deferred Fullbright Shader",
        "HUD Fullbright Shader",
        "Deferred Fullbright Alpha Masking Shader",
        "HUD Fullbright Alpha Masking Shader",
        "Deferred Fullbright Alpha Masking Alpha Shader",
        "HUD Fullbright Alpha Masking Alpha Shader",
        "Deferred FullbrightShiny Shader",
        "HUD FullbrightShiny Shader",
        "Deferred Emissive Shader",
    };
    if (allowlist.count(debugName) != 0)
    {
        return true;
    }

    // gDeferredMaterialProgram[]'s 32 runtime-numbered permutations - see
    // this function's own 2026-08-29 (pass 3) comment above.
    static const std::string material_prefix = "Material Shader ";
    static const std::string skinned_material_prefix = "Skinned Material Shader ";
    return debugName.compare(0, material_prefix.size(), material_prefix) == 0
        || debugName.compare(0, skinned_material_prefix.size(), skinned_material_prefix) == 0;
}

bool DXShader::compileVertexShader(const std::string& source, const std::string& debugName)
{
    std::string cache_path;
    bool used_cache = false;
    ID3DBlob* blob = getOrCompileHLSL(source, debugName, "main", "vs_5_0", &cache_path, &used_cache);
    if (!blob)
    {
        return false;
    }

    HRESULT hr = gDXDevice.getDevice()->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &mVS);
    if (FAILED(hr) && used_cache)
    {
        // S24: cached bytecode rejected (e.g. a truncated file from a crash
        // mid-write) - fall back to a real compile rather than failing the
        // shader outright. Re-cache the fresh result so this self-heals.
        LL_WARNS("ShaderCache") << "S24: CreateVertexShader rejected cached bytecode for " << debugName << ", recompiling" << LL_ENDL;
        blob->Release();
        blob = nullptr;
        if (!compileHLSL(source, debugName, "main", "vs_5_0", &blob))
        {
            return false;
        }
        if (!cache_path.empty())
        {
            saveCachedBlob(cache_path, blob->GetBufferPointer(), blob->GetBufferSize());
        }
        hr = gDXDevice.getDevice()->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &mVS);
    }

    if (FAILED(hr))
    {
        LL_WARNS() << "CreateVertexShader failed for " << debugName << LL_ENDL;
        blob->Release();
        return false;
    }

    reflectConstants(blob->GetBufferPointer(), blob->GetBufferSize());
    mVSBytecode = blob;
    return true;
}

bool DXShader::compilePixelShader(const std::string& source, const std::string& debugName)
{
    std::string cache_path;
    bool used_cache = false;
    ID3DBlob* blob = getOrCompileHLSL(source, debugName, "main", "ps_5_0", &cache_path, &used_cache);
    if (!blob)
    {
        return false;
    }

    HRESULT hr = gDXDevice.getDevice()->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &mPS);
    if (FAILED(hr) && used_cache)
    {
        // S24: see compileVertexShader()'s matching comment.
        LL_WARNS("ShaderCache") << "S24: CreatePixelShader rejected cached bytecode for " << debugName << ", recompiling" << LL_ENDL;
        blob->Release();
        blob = nullptr;
        if (!compileHLSL(source, debugName, "main", "ps_5_0", &blob))
        {
            return false;
        }
        if (!cache_path.empty())
        {
            saveCachedBlob(cache_path, blob->GetBufferPointer(), blob->GetBufferSize());
        }
        hr = gDXDevice.getDevice()->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &mPS);
    }

    if (FAILED(hr))
    {
        LL_WARNS() << "CreatePixelShader failed for " << debugName << LL_ENDL;
        blob->Release();
        return false;
    }

    reflectConstants(blob->GetBufferPointer(), blob->GetBufferSize());
    blob->Release(); // no input layout involved on the pixel-shader side, unlike the VS bytecode
    return true;
}

void DXShader::reflectConstants(const void* bytecode, size_t size)
{
    ID3D11ShaderReflection* reflector = nullptr;
    if (FAILED(D3DReflect(bytecode, size, IID_ID3D11ShaderReflection, (void**)&reflector)))
    {
        return;
    }

    // Top-level `uniform` HLSL globals land in an implicit cbuffer named
    // "$Globals" - a shader with none (e.g. a pixel shader that only
    // declares Texture2D/SamplerState) simply won't have one; that's not an
    // error, just nothing to reflect.
    ID3D11ShaderReflectionConstantBuffer* cb = reflector->GetConstantBufferByName("$Globals");
    D3D11_SHADER_BUFFER_DESC cb_desc = {};
    if (cb && SUCCEEDED(cb->GetDesc(&cb_desc)))
    {
        // S24 (task #79 follow-on): don't assume $Globals lands at b0 -
        // if some other cbuffer in this same file explicitly claims b0
        // (e.g. GLTFMaterials in the gltf shaders), the compiler's implicit
        // allocator moves $Globals to the next free slot instead. Look up
        // the real bind point so LLRender::syncMatrices() can bind to it
        // correctly rather than hardcoding 0.
        D3D11_SHADER_INPUT_BIND_DESC globals_bind_desc = {};
        if (SUCCEEDED(reflector->GetResourceBindingDescByName("$Globals", &globals_bind_desc)))
        {
            mConstantBufferBindPoint = globals_bind_desc.BindPoint;
        }

        mConstantStaging.resize(cb_desc.Size, 0);

        for (UINT i = 0; i < cb_desc.Variables; ++i)
        {
            ID3D11ShaderReflectionVariable* var = cb->GetVariableByIndex(i);
            D3D11_SHADER_VARIABLE_DESC var_desc = {};
            if (var && SUCCEEDED(var->GetDesc(&var_desc)))
            {
                mConstants[var_desc.Name] = { var_desc.StartOffset, var_desc.Size };
            }
        }

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = cb_desc.Size;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        if (FAILED(gDXDevice.getDevice()->CreateBuffer(&desc, nullptr, &mConstantBuffer)))
        {
            LL_WARNS("ShaderLoading") << "CreateBuffer ($Globals constant buffer) failed" << LL_ENDL;
        }
    }

    // S24 (2026-08-03): reflect Texture2D resource bind points (register(tN))
    // by name - see getTextureBindPoint()'s header comment for why this is
    // needed. D3D_SIT_TEXTURE only (not D3D_SIT_SAMPLER) - LLGLSLShader's
    // mTexture[] mapping is keyed by the texture's own uniform name, and
    // this codebase's convention always pairs a Texture2D/SamplerState at
    // the same register index (e.g. diffuseMap:t0/diffuseMapSampler:s0), so
    // the texture's bind point alone is sufficient for LLTexUnit's combined
    // SRV+sampler bind.
    D3D11_SHADER_DESC shader_desc = {};
    if (SUCCEEDED(reflector->GetDesc(&shader_desc)))
    {
        for (UINT i = 0; i < shader_desc.BoundResources; ++i)
        {
            D3D11_SHADER_INPUT_BIND_DESC bind_desc = {};
            if (SUCCEEDED(reflector->GetResourceBindingDesc(i, &bind_desc)) && bind_desc.Type == D3D_SIT_TEXTURE)
            {
                mTextureBindPoints[bind_desc.Name] = bind_desc.BindPoint;
            }
        }
    }

    reflector->Release();
}

bool DXShader::getTextureBindPoint(const std::string& name, UINT& out_bind_point) const
{
    auto iter = mTextureBindPoints.find(name);
    if (iter == mTextureBindPoints.end())
    {
        return false;
    }
    out_bind_point = iter->second;
    return true;
}

uint32_t DXShader::reflectVertexAttributeMask() const
{
    if (!mVSBytecode)
    {
        return 0;
    }

    ID3D11ShaderReflection* reflector = nullptr;
    if (FAILED(D3DReflect(mVSBytecode->GetBufferPointer(), mVSBytecode->GetBufferSize(), IID_ID3D11ShaderReflection, (void**)&reflector)))
    {
        return 0;
    }

    D3D11_SHADER_DESC shader_desc = {};
    reflector->GetDesc(&shader_desc);

    // S24 (2026-07-23): a diagnostic here (part of the "no text"
    // investigation, stage 6 discovery) confirmed the real, compiled input
    // signature reflected from bytecode exactly matches what
    // DXVertexLayout.cpp's kAttribs table assumes (including for "UI
    // Shader"'s POSITION+COLOR0+TEXCOORD0) - no anomaly here either. See
    // the project's open-issues ledger for the full history and where the
    // investigation concluded.

    uint32_t mask = 0;
    for (UINT i = 0; i < shader_desc.InputParameters; ++i)
    {
        D3D11_SIGNATURE_PARAMETER_DESC param_desc = {};
        if (FAILED(reflector->GetInputParameterDesc(i, &param_desc)) || !param_desc.SemanticName)
        {
            continue;
        }

        // D3D11 always splits a trailing digit off a semantic name into a
        // separate SemanticIndex - "TEXCOORD0" in .hlsl source reflects back
        // as SemanticName="TEXCOORD"/SemanticIndex=0, never as the literal
        // string "TEXCOORD0". Same for COLOR0/COLOR1 ("COLOR"+0/1). Matches
        // DXVertexLayout.cpp's kAttribs table, which has the same fix.
        const std::string name = param_desc.SemanticName;
        const UINT index = param_desc.SemanticIndex;
        if (name == "POSITION")               mask |= (1u << 0);
        else if (name == "NORMAL")            mask |= (1u << 1);
        else if (name == "TEXCOORD" && index <= 3) mask |= (1u << (2 + index));
        else if (name == "COLOR" && index == 0)    mask |= (1u << 6);
        else if (name == "COLOR" && index == 1)    mask |= (1u << 11);
        else if (name == "TANGENT")           mask |= (1u << 8);
        else if (name == "BLENDWEIGHT")       mask |= (1u << 9);
        else if (name == "TEXTUREINDEX")      mask |= (1u << 13);
    }

    reflector->Release();
    return mask;
}

bool DXShader::setUniformMatrix4(const std::string& name, const float* column_major_4x4)
{
    auto iter = mConstants.find(name);
    if (iter == mConstants.end())
    {
        return false;
    }

    // HLSL's default (column_major) cbuffer packing stores a float4x4 as 4
    // consecutive float4-aligned columns - exactly glm::value_ptr()'s native
    // column-major memory layout, so this is a straight 64-byte copy with no
    // reshuffling or transpose needed.
    memcpy(mConstantStaging.data() + iter->second.offset, column_major_4x4, 16 * sizeof(float));
    mConstantsDirty = true;
    return true;
}

bool DXShader::setUniformMatrix3(const std::string& name, const float* column_major_3x3)
{
    auto iter = mConstants.find(name);
    if (iter == mConstants.end())
    {
        return false;
    }

    // Same column_major convention as setUniformMatrix4(), but each 3-float
    // column is individually padded to a 16-byte slot (HLSL never packs a
    // vec3 flush against the next value) - 3 columns, 12 bytes each, with a
    // 4-byte gap after every column.
    uint8_t* dst = mConstantStaging.data() + iter->second.offset;
    for (int col = 0; col < 3; ++col)
    {
        memcpy(dst + col * 16, column_major_3x3 + col * 3, 3 * sizeof(float));
    }
    mConstantsDirty = true;
    return true;
}

bool DXShader::setUniformFloatArray(const std::string& name, const float* data, size_t float_count)
{
    auto iter = mConstants.find(name);
    if (iter == mConstants.end())
    {
        return false;
    }

    const size_t byte_count = float_count * sizeof(float);
    if (iter->second.offset + byte_count > mConstantStaging.size())
    {
        LL_WARNS("ShaderLoading") << "setUniformFloatArray: '" << name << "' would overrun its reflected constant size" << LL_ENDL;
        return false;
    }

    memcpy(mConstantStaging.data() + iter->second.offset, data, byte_count);
    mConstantsDirty = true;
    return true;
}

bool DXShader::setUniformPaddedArray(const std::string& name, const float* data, int component_count, size_t element_count)
{
    auto iter = mConstants.find(name);
    if (iter == mConstants.end())
    {
        return false;
    }

    // S24 (2026-08-23, REVERTED): briefly added a stricter check here
    // comparing `needed` (element_count*16, assuming full 16-byte padding
    // per element) against iter->second.size (D3D11 shader reflection's
    // reported size for this constant). Live-tested and WRONG - it broke
    // ~90% of scene rendering (avatars, most objects) immediately, meaning
    // reflection's `size` is NOT measured in the same fully-padded-per-
    // element convention `needed` assumes here (most likely the tight/
    // logical size, e.g. element_count*component_count*4 - which is
    // naturally smaller than the padded write size for any array with
    // more than one element), so this check was rejecting essentially
    // every legitimate array upload project-wide, not just malformed ones.
    // Reverted to the original whole-buffer-only check below. The
    // underlying question this was trying to answer (does `kern`
    // specifically ever overrun into `kern_scale`) is still open - the
    // diagnostic log that was here went with it; if revisited, log offset/
    // size WITHOUT rejecting anything, and derive the real per-element
    // padding convention from actual reflected size numbers first instead
    // of assuming one.
    const size_t needed = element_count * 16;
    if (iter->second.offset + needed > mConstantStaging.size())
    {
        LL_WARNS("ShaderLoading") << "setUniformPaddedArray: '" << name << "' would overrun its reflected constant size" << LL_ENDL;
        return false;
    }

    uint8_t* dst = mConstantStaging.data() + iter->second.offset;
    for (size_t i = 0; i < element_count; ++i)
    {
        memcpy(dst + i * 16, data + i * component_count, component_count * sizeof(float));
    }
    mConstantsDirty = true;
    return true;
}

void DXShader::uploadConstants()
{
    if (!mConstantBuffer || !mConstantsDirty)
    {
        return;
    }

    ID3D11DeviceContext* ctx = gDXDevice.getContext();
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(ctx->Map(mConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, mConstantStaging.data(), mConstantStaging.size());
        ctx->Unmap(mConstantBuffer, 0);
        mConstantsDirty = false;
    }
}

// static
void DXShader::injectSkinningInputs(std::string& vertex_source)
{
    // A single entry file can carry more than one BLENDWEIGHT field for
    // different mutually-exclusive permutations - e.g. alphaV.hlsl has
    // both "weight4" (under #ifdef HAS_SKIN, feeding objectSkinV.hlsl's
    // getObjectSkinnedTransform()) and "weight" (under #ifdef
    // IS_AVATAR_SKIN, feeding avatarSkinV.hlsl's getSkinnedTransform(),
    // gDeferredAvatarAlphaProgram in llviewershadermgr.cpp). Each field may
    // be gated behind a different macro, or not gated at all (dedicated
    // always-skinned variants like shadowSkinnedV.hlsl/avatarV.hlsl, which
    // never define any such macro). This runs on the raw, pre-preprocessed
    // text, so every field found needs its own guard determined
    // independently and its own declaration/assignment pair injected -
    // stopping at the first match (as an earlier version of this function
    // did) silently leaves every field after the first undeclared.
    static const std::regex field_pattern(R"((\w+)\s+(\w+)\s*:\s*BLENDWEIGHT)");
    static const std::regex ifdef_pattern(R"(#ifdef\s+(\w+))");
    static const std::regex endif_pattern(R"(#endif\b)");

    struct SkinField
    {
        std::string type;
        std::string name;
        std::string guard_macro; // empty if unconditional
    };
    std::vector<SkinField> fields;

    for (auto it = std::sregex_iterator(vertex_source.begin(), vertex_source.end(), field_pattern);
         it != std::sregex_iterator(); ++it)
    {
        const std::string type = (*it)[1];
        const std::string name = (*it)[2];
        const size_t match_pos = static_cast<size_t>(it->position(0));

        // Guard detection: find the last #ifdef and last #endif appearing
        // anywhere before this match - if the last #ifdef is more recent
        // than the last #endif, this field sits inside that still-open
        // block. Doesn't model nested #ifdefs precisely, but matches every
        // real case in this codebase (single-level guards only).
        const std::string prefix = vertex_source.substr(0, match_pos);
        size_t last_ifdef_pos = std::string::npos;
        std::string last_ifdef_macro;
        for (auto iit = std::sregex_iterator(prefix.begin(), prefix.end(), ifdef_pattern);
             iit != std::sregex_iterator(); ++iit)
        {
            last_ifdef_pos = static_cast<size_t>(iit->position(0));
            last_ifdef_macro = (*iit)[1];
        }
        size_t last_endif_pos = std::string::npos;
        for (auto iit = std::sregex_iterator(prefix.begin(), prefix.end(), endif_pattern);
             iit != std::sregex_iterator(); ++iit)
        {
            last_endif_pos = static_cast<size_t>(iit->position(0));
        }
        const bool guarded = last_ifdef_pos != std::string::npos &&
            (last_endif_pos == std::string::npos || last_ifdef_pos > last_endif_pos);

        if (std::none_of(fields.begin(), fields.end(), [&](const SkinField& f) { return f.name == name; }))
        {
            fields.push_back({type, name, guarded ? last_ifdef_macro : std::string()});
        }
    }

    if (fields.empty())
    {
        return;
    }

    static const std::regex entry_pattern(R"(VSOutput\s+main\s*\(\s*VSInput\s+IN\s*\)\s*\{)");
    std::smatch entry_match;
    if (!std::regex_search(vertex_source, entry_match, entry_pattern))
    {
        LL_WARNS("ShaderLoading") << "DXShader::injectSkinningInputs: found a BLENDWEIGHT VSInput field but no matching 'VSOutput main(VSInput IN) {' entry point" << LL_ENDL;
        return;
    }

    // Insert the bridging assignments first (position is relative to the
    // current string, so later edits must not invalidate it) - then prepend
    // the global declarations, which shifts every subsequent offset but
    // this is the last edit made. Each declaration is always unconditional
    // - the skinning-transform function using it (e.g. objectSkinV.hlsl's
    // getObjectSkinnedTransform(), avatarSkinV.hlsl's getSkinnedTransform())
    // is attached and fully compiled/validated whenever the corresponding
    // mFeatures flag is set, independent of whether THIS specific shader
    // instance also defines the guard macro, so it always needs *some*
    // valid value to compile against even when never actually called at
    // runtime. Only the assignment is conditionally gated, and only when
    // the field itself was found to be gated.
    const size_t insert_pos = static_cast<size_t>(entry_match.position(0)) + entry_match.length(0);
    std::string assignments;
    std::string declarations;
    for (const auto& f : fields)
    {
        assignments += f.guard_macro.empty()
            ? ("\n    " + f.name + " = IN." + f.name + ";")
            : ("\n#ifdef " + f.guard_macro + "\n    " + f.name + " = IN." + f.name + ";\n#endif");
        declarations += "static " + f.type + " " + f.name + ";\n";
    }
    vertex_source.insert(insert_pos, assignments);
    vertex_source.insert(0, declarations);
}

// static
void DXShader::resolveIncludes(std::string& source)
{
    // S24 (2026-07-30): custom raw-string delimiter (RX) required here -
    // the pattern's own "]+)" sequence contains a literal )" which would
    // otherwise prematurely terminate a default R"(...)" raw string right
    // in the middle of the regex, turning the remainder into malformed C++
    // (the exact cause of a real compile error caught on this line).
    // S24 (2026-08-01): anchored to the start of a line (^, with multiline
    // so it matches after every \n, not just string start) - without this,
    // the pattern matched `#include "..."` anywhere in the text, including
    // inside a `//` comment merely talking about a stray include (e.g.
    // pointLightF.hlsl's own comment documenting one that was removed).
    // That phantom match tried to open a file that was never a real
    // directive, failed, and broke the whole loop - leaving every
    // subsequent *real* #include (e.g. the varying/*.hlsli one a few lines
    // later in the same file) unresolved, which is what actually produced
    // the downstream "X1505: no include handler" failure.
    static const std::regex include_pattern(R"RX(^[ \t]*#include\s*"([^"]+)"[^\n]*\n?)RX",
        std::regex::ECMAScript | std::regex::multiline);

    std::string shaders_root = gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, "shaders", "");

    std::smatch match;
    // Loop (not a single regex_replace pass) since each replacement can
    // shift subsequent match positions - re-searching from scratch after
    // every substitution is simplest and safe given how few #include
    // directives any one file has.
    while (std::regex_search(source, match, include_pattern))
    {
        const std::string included_path = match[1];
        const std::string full_path = shaders_root + included_path;

        std::ifstream include_file(full_path.c_str());
        if (!include_file)
        {
            // S24 (2026-08-01): untagged, not "ShaderLoading" - that tag is
            // suppressed to ERROR-only in logcontrol.xml (same reasoning as
            // compileHLSL()'s own warnings), which would otherwise silently
            // swallow the one message explaining why a real "X1505: no
            // include handler" compile failure just happened (a stale
            // packaged tree missing this include file entirely still
            // produces that exact downstream error, with this warning as
            // the only clue why).
            LL_WARNS() << "DXShader::resolveIncludes: could not open '" << full_path
                << "' (referenced via #include \"" << included_path << "\")" << LL_ENDL;
            // Leave the #include line in place - D3DCompile will fail on it
            // with a real, locatable error instead of this silently
            // producing an incomplete shader.
            break;
        }

        std::string included_text((std::istreambuf_iterator<char>(include_file)), std::istreambuf_iterator<char>());
        // Strip a leading UTF-8 BOM, same reasoning as loadShaderFile()'s
        // DX_RENDER branch - this text lands mid-blob, not at a real file
        // start a text editor would have hidden it at.
        if (included_text.compare(0, 3, "\xEF\xBB\xBF") == 0)
        {
            included_text.erase(0, 3);
        }

        source.replace(match.position(0), match.length(0), included_text);
    }
}

// static
void DXShader::injectTextureIndexInputs(std::string& vertex_source)
{
    // indexedTextureV.hlsl's real passTextureIndex() body is the signal
    // that texture_index/vary_texture_index are needed for this compile -
    // nonindexedTextureV.hlsl's no-op has no such text. Unlike BLENDWEIGHT,
    // this isn't a VSInput-struct-field search: texture_index/
    // vary_texture_index are a plain identifier pair with no existing
    // per-file struct declaration to find, and the wiring needs both an
    // input (texture_index <- IN.texture_index) and, uniquely to this
    // function, a real vertex-to-pixel output (OUT.vary_texture_index <-
    // vary_texture_index) that injectSkinningInputs() has no equivalent
    // for at all (BLENDWEIGHT stays vertex-stage-only).
    if (vertex_source.find("vary_texture_index = texture_index;") == std::string::npos)
    {
        return;
    }

    // Guard-macro detection mirrors injectSkinningInputs()'s approach -
    // each entry file's own "int texture_index : TEXTUREINDEX;" VSInput
    // field may or may not be macro-gated (added per-file as this gets
    // wired up; see the project's open-issues ledger for which currently
    // do). Declarations stay unconditional regardless (passTextureIndex()
    // is attached and fully compiled whenever indexedTextureV.hlsl is,
    // independent of whether this specific permutation defines the guard).
    static const std::regex guarded_pattern(R"(#ifdef\s+(\w+)(?:(?!#endif)[\s\S])*?\w+\s+texture_index\s*:\s*TEXTUREINDEX)");
    std::smatch guard_match;
    std::string guard_macro;
    if (std::regex_search(vertex_source, guard_match, guarded_pattern))
    {
        guard_macro = guard_match[1];
    }

    // We only got here because indexed texturing is definitely active for
    // this compile, so define the same guard macro every entry file's
    // VSInput gates its "int texture_index : TEXTUREINDEX;" field behind
    // (HAS_DIFFUSE_LOOKUP, matching GL's own fragment-side #define of the
    // same name - see loadShaderFile()'s DX_RENDER branch). Without this,
    // an entry file compiled for the non-indexed permutation would declare
    // a VSInput field the bound vertex buffer never provides data for,
    // which CreateInputLayout correctly rejects.
    vertex_source.insert(0, "#define HAS_DIFFUSE_LOOKUP 1\nstatic int texture_index;\nstatic int vary_texture_index;\n");

    // Anchor on the passTextureIndex() call site / main()'s final "return
    // OUT;" rather than re-deriving main()'s exact signature text (unlike
    // injectSkinningInputs(), which has to since there's no equivalent
    // fixed call site to anchor on for BLENDWEIGHT) - both are unique,
    // single occurrences per compile (exactly one entry file, exactly one
    // main(), and entry-file text always comes first in concatenation
    // order, before any attached utility file's own content, so the first
    // match is always the right one).
    static const std::string call_site = "passTextureIndex();";
    // rfind, not find - every entry file also has a forward declaration
    // ("void passTextureIndex();") earlier in the file containing this
    // same substring; find() matched that instead of the real call.
    size_t call_pos = vertex_source.rfind(call_site);
    if (call_pos != std::string::npos)
    {
        std::string input_bridge = guard_macro.empty()
            ? ("texture_index = IN.texture_index;\n    " + call_site)
            : ("\n#ifdef " + guard_macro + "\n    texture_index = IN.texture_index;\n#endif\n    " + call_site);
        vertex_source.replace(call_pos, call_site.length(), input_bridge);
    }

    static const std::string return_stmt = "return OUT;";
    size_t return_pos = vertex_source.find(return_stmt);
    if (return_pos != std::string::npos)
    {
        std::string output_bridge = guard_macro.empty()
            ? ("OUT.vary_texture_index = vary_texture_index;\n    " + return_stmt)
            : ("#ifdef " + guard_macro + "\n    OUT.vary_texture_index = vary_texture_index;\n#endif\n    " + return_stmt);
        vertex_source.replace(return_pos, return_stmt.length(), output_bridge);
    }
}

void DXShader::reset()
{
    if (mVS) { mVS->Release(); mVS = nullptr; }
    if (mPS) { mPS->Release(); mPS = nullptr; }
    if (mVSBytecode) { mVSBytecode->Release(); mVSBytecode = nullptr; }
    if (mConstantBuffer) { mConstantBuffer->Release(); mConstantBuffer = nullptr; }
    mConstantBufferBindPoint = 0;
    mConstants.clear();
    mConstantStaging.clear();
    mConstantsDirty = false;
    mTextureBindPoints.clear();
}
