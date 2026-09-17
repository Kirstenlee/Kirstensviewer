/**
 * @file llshadermgr.cpp
 * @brief Shader manager implementation.
 *
 * $LicenseInfo:firstyear=2005&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "llshadermgr.h"
#include "llrender.h"
#include "llfile.h"
#include "lldir.h"
#include "llsdutil.h"
#include "llsdserialize.h"
#include "hbxxh.h"

 // Lots of STL stuff in here, using namespace std to keep things more readable
using std::vector;
using std::pair;
using std::make_pair;
using std::string;

LLShaderMgr* LLShaderMgr::sInstance = NULL;

LLShaderMgr::LLShaderMgr()
{
}


LLShaderMgr::~LLShaderMgr()
{
}

// static
LLShaderMgr* LLShaderMgr::instance()
{
	if (NULL == sInstance)
	{
		LL_ERRS("Shaders") << "LLShaderMgr should already have been instantiated by the application!" << LL_ENDL;
	}

	return sInstance;
}

bool LLShaderMgr::attachShaderFeatures(LLHLSLShader* shader)
{
	llassert_always(shader != NULL);
	LLShaderFeatures* features = &shader->mFeatures;

	if (features->attachNothing)
	{
		return true;
	}
	//////////////////////////////////////
	// Attach Vertex Shader Features First
	//////////////////////////////////////

	// NOTE order of shader object attaching is VERY IMPORTANT!!!
	if (features->calculatesAtmospherics || features->hasGamma || features->isDeferred)
	{
		if (!shader->attachVertexObject("windlight/atmosphericsVarsV.glsl"))
		{
			return false;
		}
	}

	if (features->calculatesLighting || features->calculatesAtmospherics)
	{
		if (!shader->attachVertexObject("windlight/atmosphericsHelpersV.glsl"))
		{
			return false;
		}
	}

	if (features->calculatesLighting)
	{
		if (features->isSpecular)
		{
			if (!shader->attachVertexObject("lighting/lightFuncSpecularV.glsl"))
			{
				return false;
			}

			if (!features->isAlphaLighting)
			{
				if (!shader->attachVertexObject("lighting/sumLightsSpecularV.glsl"))
				{
					return false;
				}
			}

			if (!shader->attachVertexObject("lighting/lightSpecularV.glsl"))
			{
				return false;
			}
		}
		else
		{
			if (!shader->attachVertexObject("lighting/lightFuncV.glsl"))
			{
				return false;
			}

			if (!features->isAlphaLighting)
			{
				if (!shader->attachVertexObject("lighting/sumLightsV.glsl"))
				{
					return false;
				}
			}

			if (!shader->attachVertexObject("lighting/lightV.glsl"))
			{
				return false;
			}
		}
	}

	// NOTE order of shader object attaching is VERY IMPORTANT!!!
	if (features->calculatesAtmospherics)
	{
		if (!shader->attachVertexObject("environment/srgbF.glsl")) // NOTE -- "F" suffix is superfluous here, there is nothing fragment specific in srgbF
		{
			return false;
		}

		if (!shader->attachVertexObject("windlight/atmosphericsFuncs.glsl")) {
			return false;
		}

		if (!shader->attachVertexObject("windlight/atmosphericsV.glsl"))
		{
			return false;
		}
	}

	if (features->hasSkinning)
	{
		if (!shader->attachVertexObject("avatar/avatarSkinV.glsl"))
		{
			return false;
		}
	}

	if (features->hasObjectSkinning)
	{
		shader->mRiggedVariant = shader;
		if (!shader->attachVertexObject("avatar/objectSkinV.glsl"))
		{
			return false;
		}
	}

	if (!shader->attachVertexObject("deferred/textureUtilV.glsl"))
	{
		return false;
	}

	///////////////////////////////////////
	// Attach Fragment Shader Features Next
	///////////////////////////////////////

	// NOTE order of shader object attaching is VERY IMPORTANT!!!

	if (!shader->attachFragmentObject("deferred/globalF.glsl"))
	{
		return false;
	}

	if (features->hasSrgb || features->hasAtmospherics || features->calculatesAtmospherics || features->isDeferred)
	{
		if (!shader->attachFragmentObject("environment/srgbF.glsl"))
		{
			return false;
		}
	}

	if (features->calculatesAtmospherics || features->hasGamma || features->isDeferred)
	{
		if (!shader->attachFragmentObject("windlight/atmosphericsVarsF.glsl"))
		{
			return false;
		}
	}

	if (features->calculatesLighting || features->calculatesAtmospherics)
	{
		if (!shader->attachFragmentObject("windlight/atmosphericsHelpersF.glsl"))
		{
			return false;
		}
	}

	// we want this BEFORE shadows and AO because those facilities use pos/norm access
	if (features->isDeferred || features->hasReflectionProbes)
	{
		if (!shader->attachFragmentObject("deferred/deferredUtil.glsl"))
		{
			return false;
		}
	}

	if (features->hasFullGBuffer)
	{
		if (!shader->attachFragmentObject("deferred/gbufferUtil.glsl"))
		{
			return false;
		}
	}

	if (features->hasScreenSpaceReflections || features->hasReflectionProbes)
	{
		if (!shader->attachFragmentObject("deferred/screenSpaceReflUtil.glsl"))
		{
			return false;
		}
	}

	if (features->hasShadows)
	{
		if (!shader->attachFragmentObject("deferred/shadowUtil.glsl"))
		{
			return false;
		}
	}

	if (features->hasReflectionProbes)
	{
		if (!shader->attachFragmentObject("deferred/reflectionProbeF.glsl"))
		{
			return false;
		}
	}

	if (features->hasAmbientOcclusion)
	{
		if (!shader->attachFragmentObject("deferred/aoUtil.glsl"))
		{
			return false;
		}
	}

	if (features->hasGamma || features->isDeferred)
	{
		if (!shader->attachFragmentObject("windlight/gammaF.glsl"))
		{
			return false;
		}
	}

	if (features->hasAtmospherics || features->isDeferred)
	{
		if (!shader->attachFragmentObject("windlight/atmosphericsFuncs.glsl")) {
			return false;
		}

		if (!shader->attachFragmentObject("windlight/atmosphericsF.glsl"))
		{
			return false;
		}
	}

	if (features->isPBRTerrain)
	{
		if (!shader->attachFragmentObject("deferred/pbrterrainUtilF.glsl"))
		{
			return false;
		}
	}

	if (features->hasTonemap)
	{
		if (!shader->attachFragmentObject("deferred/tonemapUtilF.glsl"))
		{
			return false;
		}
	}

	// NOTE order of shader object attaching is VERY IMPORTANT!!!
	if (features->hasAtmospherics)
	{
		if (!shader->attachFragmentObject("environment/waterFogF.glsl"))
		{
			return false;
		}
	}

	if (features->hasLighting)
	{
		if (features->mIndexedTextureChannels <= 1)
		{
			if (features->hasAlphaMask)
			{
				if (!shader->attachFragmentObject("lighting/lightAlphaMaskNonIndexedF.glsl"))
				{
					return false;
				}
			}
			else
			{
				if (!shader->attachFragmentObject("lighting/lightNonIndexedF.glsl"))
				{
					return false;
				}
			}
		}
		else
		{
			if (features->hasAlphaMask)
			{
				if (!shader->attachFragmentObject("lighting/lightAlphaMaskF.glsl"))
				{
					return false;
				}
			}
			else
			{
				if (!shader->attachFragmentObject("lighting/lightF.glsl"))
				{
					return false;
				}
			}
			shader->mFeatures.mIndexedTextureChannels = llmax(LLHLSLShader::sIndexedTextureChannels, 1);
		}
	}

	if (features->mIndexedTextureChannels <= 1)
	{
		if (!shader->attachVertexObject("objects/nonindexedTextureV.glsl"))
		{
			return false;
		}
	}
	else
	{
		if (!shader->attachVertexObject("objects/indexedTextureV.glsl"))
		{
			return false;
		}
	}

	return true;
}

//============================================================================
// Load Shader

// S24: get_shader_log()/get_program_log()/get_object_log()/dumpShaderSource()/dumpObjectLog()
// removed - all dead (zero live callers, confirmed by grep across the whole tree) once
// loadShaderFile()'s GL compile branch and linkProgramObject()/validateProgramObject() (also
// removed below) are gone. Part of task #300 (full GL removal) - this file only ever compiled
// its DX_RENDER branch in this build anyway; these were never-taken GL fallback paths.
GLuint LLShaderMgr::loadShaderFile(const std::string& filename, S32& shader_level, DXenum type, std::map<std::string, std::string>* defines, S32 texture_index_channels, bool attaches_deferred_util)
{

	if (filename.empty())
	{
		LL_WARNS("ShaderLoading") << "tried loading empty filename" << LL_ENDL;
		return 0;
	}

	//read in from file
	LLFILE* file = NULL;

	S32 try_gpu_class = shader_level;
	S32 gpu_class;

	std::string open_file_name;

	// See mRawShaderFileTextCache's own comment (llshadermgr.h) - the resolved file (and its
	// content) for a given (filename, try_gpu_class) pair can't change mid-session outside of a
	// deliberate Develop > Rebuild Shaders / Purge Shader Cache, both of which call
	// clearRawShaderFileCache() first - so a hit here skips the real disk probe/read below
	// entirely. Keyed by the ORIGINAL (pre extension-swap) filename, same as
	// mVertexShaderSourceText/mFragmentShaderSourceText below.
	const std::string raw_cache_key = filename + "@" + std::to_string(try_gpu_class);
	std::string source_text;
	bool have_source = false;
	{
		auto cached = mRawShaderFileTextCache.find(raw_cache_key);
		if (cached != mRawShaderFileTextCache.end())
		{
			source_text = cached->second;
			have_source = true;
		}
	}

#ifdef DX_RENDER
	// filename arrives with ".glsl" already baked in by ~150 call sites in
	// llviewershadermgr.cpp - swap it to the equivalent ".hlsl" sibling on a
	// local copy only. The original (still-".glsl") filename remains the
	// cache key into mVertexShaderSourceText/mFragmentShaderSourceText below,
	// matching what attachShaderFeatures()'s unmodified ".glsl" call sites
	// look up later.
	std::string dx_filename = filename;
	const std::string glsl_ext(".glsl");
	if (dx_filename.size() >= glsl_ext.size() &&
		dx_filename.compare(dx_filename.size() - glsl_ext.size(), glsl_ext.size(), glsl_ext) == 0)
	{
		dx_filename.replace(dx_filename.size() - glsl_ext.size(), glsl_ext.size(), ".hlsl");
	}
#endif

	if (!have_source)
	{
		//find the most relevant file
		for (gpu_class = try_gpu_class; gpu_class > 0; gpu_class--)
		{   //search from the current gpu class down to class 1 to find the most relevant shader
			std::stringstream fname;
			fname << getShaderDirPrefix();
#ifdef DX_RENDER
            fname << gpu_class << gDirUtilp->getDirDelimiter() << dx_filename;
#else
            fname << gpu_class << gDirUtilp->getDirDelimiter() << filename;
#endif

			open_file_name = fname.str();

			/*
			Would be awesome, if we didn't have shaders that re-use files
			with different environments to say, add skinning, etc
			can't depend on cached version to have evaluate ifdefs identically...
			if we can define a deterministic hash for the shader based on
			all the inputs, maybe we can save some time here.
			if (mShaderObjects.count(filename) > 0)
			{
				return mShaderObjects[filename];
			}

			*/

			LL_DEBUGS("ShaderLoading") << "Looking in " << open_file_name << LL_ENDL;
			file = LLFile::fopen(open_file_name, "r");      /* Flawfinder: ignore */
			if (file)
			{
				LL_DEBUGS("ShaderLoading") << "Loading file: " << open_file_name << " (Want class " << gpu_class << ")" << LL_ENDL;
				break; // done
			}
		}
	if (file == NULL)
	{
        if (gDirUtilp->fileExists(open_file_name))
        {
            LL_WARNS("ShaderLoading") << "Shader file failed to open: " << open_file_name << LL_ENDL;
        }
        else
        {
            LL_WARNS("ShaderLoading") << "Shader file not found: " << open_file_name << LL_ENDL;
        }
        return 0;
    }

	// HLSL has no separately-compiled/linkable shader objects (unlike GL,
	// D3DCompile takes one source blob per stage) - just cache the raw file
	// text here. Real per-stage compilation happens later, once
	// LLHLSLShader::createShaderDX() has concatenated this entry file's text
	// with its attached utility files' text (see mVertexShaderSourceText/
	// mFragmentShaderSourceText in llshadermgr.h).
	{
		char line_buf[1024];
		while (fgets(line_buf, sizeof(line_buf), file) != NULL)
		{
			source_text += line_buf;
		}
	}
	fclose(file);

	// Strip a leading UTF-8 BOM if present - D3DCompile takes a raw
	// in-memory string, not a file, so it has no concept of a BOM; and this
	// text gets concatenated after a generated header (buildDXShaderHeader()),
	// so a BOM here lands mid-blob as 3 illegal bytes rather than at the true
	// start of a file, where a text editor would silently have hidden it.
	if (source_text.compare(0, 3, "\xEF\xBB\xBF") == 0)
	{
		source_text.erase(0, 3);
	}

		mRawShaderFileTextCache[raw_cache_key] = source_text;
	}

	if (type == GL_FRAGMENT_SHADER && texture_index_channels > 0)
	{
		// Mirrors the GL branch below's dynamic diffuseLookup() generation
		// (same texture_index_channels parameter), in HLSL instead of GLSL.
		// GL's version gets textually woven into every attached file that
		// contains the "[EXTRA_CODE_HERE]" marker (there's no equivalent of
		// "already attached" for loadShaderFile() - it's called once per
		// file, and multiple attached files can each carry the marker) -
		// GLSL's separate-compile-then-link model tolerates identical
		// redefinitions across linked objects (same reasoning as the
		// duplicate-uniform entries elsewhere in this project), but raw
		// HLSL text concatenation does not, so this is include-guarded the
		// same way those were, ensuring only the first marker position
		// (in final concatenation order) actually keeps its copy.
		std::string extra = "#ifndef LL_DIFFUSELOOKUP_DECLARED\n#define LL_DIFFUSELOOKUP_DECLARED\n";
		// Matches GL's own unconditional "#define HAS_DIFFUSE_LOOKUP" here
		// (llshadermgr.cpp's GL branch, texture_index_channels > 0) - some
		// ported fragment files (e.g. fullbrightShinyF.hlsl) already branch
		// on this macro to choose between calling diffuseLookup() and a
		// plain single-texture Sample() fallback.
		extra += "#define HAS_DIFFUSE_LOOKUP 1\n";
		// tex0.. defaults to t0/s0 (mirroring GL's texture-unit-0-based
		// numbering) - safe for the common case (G-buffer-*write* shaders
		// like gDeferredDiffuseProgram set mIndexedTextureChannels but never
		// attach deferredUtil.hlsl, so t0-t3 is genuinely free there).
		//
		// CORRECTNESS NOTE: an earlier version of this fix unconditionally
		// moved the base to t16/s16 to dodge deferredUtil.hlsl's t0-t3 for
		// shaders that also attach it (alphaF.hlsl/"Deferred Alpha Shader"
		// etc.) - that broke "Skinned Deferred Diffuse Shader" (X4509:
		// sampler register index exceeded) because SM5 only has 16
		// *sampler* slots (s0-s15) even though it has 128 SRV/texture slots
		// - t16 is a valid t-register but s16 doesn't exist. Fixed properly
		// by gating the higher base on attaches_deferred_util (the real
		// attachShaderFeatures() condition for deferredUtil.hlsl is
		// isDeferred || hasReflectionProbes, NOT isDeferred alone -
		// gDeferredAlphaImpostorProgram sets only hasReflectionProbes and
		// still attaches deferredUtil.hlsl, so checking isDeferred alone
		// would have missed it), so the common case (neither flag set) is
		// untouched.
		//
		// When attaches_deferred_util is true (the only shaders that combine
		// this with indexed texturing are "Deferred/HUD Alpha Shader",
		// "Deferred/Skinned Alpha Impostor Shader" and their rigged variants
		// - confirmed via llviewershadermgr.cpp), the real attach set is
		// deferredUtil.hlsl (t0-t3) + reflectionProbeF.hlsl (t4,
		// hasReflectionProbes=true on all of these) + optionally
		// shadowUtil.hlsl (t10-t15, gated on hasShadows/use_sun_shadow, the
		// default-on case) - t5-t9 is the one gap clear of all of those, and
		// 4 channels (sIndexedTextureChannels) fits in t5-t8 with t9 free as
		// margin.
		const S32 kIndexedTexRegisterBase = attaches_deferred_util ? 5 : 0;
		for (S32 i = 0; i < texture_index_channels; ++i)
		{
			extra += llformat("Texture2D tex%d : register(t%d);\n", i, kIndexedTexRegisterBase + i);
			extra += llformat("SamplerState tex%dSampler : register(s%d);\n", i, kIndexedTexRegisterBase + i);
		}

		if (texture_index_channels > 1)
		{
			// Real vertex-to-pixel wiring (a VSOutput/PSInput field
			// populating this from a real semantic, not an ambient "flat
			// in" the way GLSL declares it) is added per-entry-file as
			// needed - see indexedTextureV.hlsl's own comment and the
			// project's open-issues ledger for which files currently do.
			extra += "static int vary_texture_index;\n";
		}

		extra += "float4 diffuseLookup(float2 texcoord)\n{\n";
		if (texture_index_channels == 1)
		{
			extra += "    return tex0.Sample(tex0Sampler, texcoord);\n}\n";
		}
		else
		{
			extra += "    switch (vary_texture_index)\n    {\n";
			for (S32 i = 0; i < texture_index_channels; ++i)
			{
				extra += llformat("        case %d: return tex%d.Sample(tex%dSampler, texcoord);\n", i, i, i);
			}
			extra += "        default: return float4(1,0,1,1);\n    }\n}\n";
		}
		extra += "#endif\n";

		const std::string marker = "/*[EXTRA_CODE_HERE]*/";
		size_t marker_pos = source_text.find(marker);
		if (marker_pos != std::string::npos)
		{
			source_text.replace(marker_pos, marker.length(), extra);
		}
	}

	if (type == GL_VERTEX_SHADER)
	{
		mVertexShaderSourceText[filename] = source_text;
	}
	else if (type == GL_FRAGMENT_SHADER)
	{
		mFragmentShaderSourceText[filename] = source_text;
	}

	shader_level = try_gpu_class;
	return 1; // truthy sentinel - DX_RENDER has no real GL shader object
}

void LLShaderMgr::initShaderCache(bool enabled, const LLUUID& old_cache_version, const LLUUID& current_cache_version, bool second_instance)
{
    LL_INFOS("ShaderMgr") << "Initializing shader cache" << LL_ENDL;

	mShaderCacheEnabled = gGLManager.mGLVersion >= 4.09 && enabled;

    if(!mShaderCacheEnabled || mShaderCacheVersion.notNull())
		return;

    mShaderCacheVersion = current_cache_version;

	mShaderCacheDir = gDirUtilp->getExpandedFilename(LL_PATH_CACHE, "shader_cache");
	LLFile::mkdir(mShaderCacheDir);

	{
		std::string meta_out_path = gDirUtilp->add(mShaderCacheDir, "shaderdata.llsd");
		if (gDirUtilp->fileExists(meta_out_path))
		{
            LL_INFOS("ShaderMgr") << "Loading shader cache metadata" << LL_ENDL;

            llifstream instream(meta_out_path, std::ifstream::in | std::ifstream::binary);
			LLSD in_data;
            try
            {
            LLSDSerialize::fromBinary(in_data, instream, LLSDSerialize::SIZE_UNLIMITED);
            }
            catch( std::bad_alloc& )
            {
                // Try to get a bit more memory back before we try to clear the cache.
                in_data.clear();
                // Just in case it was somehow the cause, clear cache.
                clearShaderCache();
                // If user run out of memory this early in init,
                // we don't want to keep going just to crash again.
                // Notify user and close.
                LLError::LLUserWarningMsg::showOutOfMemory();
                LL_ERRS("ShaderMgr") << "Failed to parse shader cache metadata, potentially due to size. Purged cache." << LL_ENDL;
                return;
            }
			instream.close();

            if (old_cache_version == current_cache_version
                && in_data["version"].asUUID() == current_cache_version)
			{
                for (const auto& data_pair : llsd::inMap(in_data["shaders"]))
				{
					ProgramBinaryData binary_info = ProgramBinaryData();
					binary_info.mBinaryFormat = data_pair.second["binary_format"].asInteger();
					binary_info.mBinaryLength = data_pair.second["binary_size"].asInteger();
					binary_info.mLastUsedTime = (F32)data_pair.second["last_used"].asReal();
					mShaderBinaryCache.insert_or_assign(LLUUID(data_pair.first), binary_info);
				}
			}
            else if (!second_instance)
			{
                LL_INFOS("ShaderMgr") << "Shader cache version mismatch detected. Purging." << LL_ENDL;
				clearShaderCache();
			}
            else
            {
                LL_INFOS("ShaderMgr") << "Shader cache version mismatch detected." << LL_ENDL;
		}
	}
    }
}

void LLShaderMgr::clearShaderCache()
{
	std::string shader_cache = gDirUtilp->getExpandedFilename(LL_PATH_CACHE, "shader_cache");
    LL_INFOS("ShaderMgr") << "Removing shader cache at " << shader_cache << LL_ENDL;
	const std::string mask = "*";
	gDirUtilp->deleteFilesInDir(shader_cache, mask);
    LLFile::rmdir(shader_cache);
	mShaderBinaryCache.clear();
}

void LLShaderMgr::persistShaderCacheMetadata()
{
#ifdef DX_RENDER
    // S24: mShaderBinaryCache is only ever populated by the GL program-
    // linking step, never reached under DX_RENDER, so it's always empty
    // here. The real DX shader cache is the separate content-hash-keyed
    // .dxbc mechanism in DXShader.cpp.
    return;
#endif
	if (!mShaderCacheEnabled) return;
    if (mShaderCacheVersion.isNull())
    {
        LL_WARNS("ShaderMgr") << "Attempted to save shader cache with no version set" << LL_ENDL;
        return;
    }

    if (mShaderCacheDir.empty() || !LLFile::isdir(mShaderCacheDir))
    {
        LL_WARNS("ShaderMgr") << "Invalid shader cache directory: " << mShaderCacheDir << LL_ENDL;
        return;
    }

    size_t total_entries = mShaderBinaryCache.size();
    LL_INFOS("ShaderMgr") << "Persisting shader " << (S32)total_entries << " cache metadata entries to disk" << LL_ENDL;

    LLSD out;
    // Settings and shader cache get saved at different time, thus making
    // RenderShaderCacheVersion unreliable when running multiple viewer
    // instances, or for cases where viewer crashes before saving settings.
    // Dupplicate version to the cache itself.
    out["version"] = mShaderCacheVersion;
    out["shaders"] = LLSD::emptyMap();
    LLSD &shaders = out["shaders"];

    size_t removed = 0;

	static const F32 LRU_TIME = (60.f * 60.f) * 24.f * 7.f; // 14 days
	const F32 current_time = (F32)LLTimer::getTotalSeconds();
	for (auto it = mShaderBinaryCache.begin(); it != mShaderBinaryCache.end();)
	{
		const ProgramBinaryData& shader_metadata = it->second;
		if ((shader_metadata.mLastUsedTime + LRU_TIME) < current_time)
		{
			std::string shader_path = gDirUtilp->add(mShaderCacheDir, it->first.asString() + ".shaderbin");
            LLFile::remove(shader_path, ENOENT);
			it = mShaderBinaryCache.erase(it);
            removed++;
		}
		else
		{
			LLSD data = LLSD::emptyMap();
			data["binary_format"] = LLSD::Integer(shader_metadata.mBinaryFormat);
			data["binary_size"] = LLSD::Integer(shader_metadata.mBinaryLength);
			data["last_used"] = LLSD::Real(shader_metadata.mLastUsedTime);
            shaders[it->first.asString()] = data;
			++it;
		}
	}

	std::string meta_out_path = gDirUtilp->add(mShaderCacheDir, "shaderdata.llsd");
    if (shaders.size() == 0)
    {
        LL_INFOS("ShaderMgr") << "No shader cache entries to persist, removing cache metadata file" << LL_ENDL;
        // S24: suppress ENOENT - a metadata file that's already gone (first
        // run, or a previous persist already cleaned it up) is the desired
        // end state, not a failure worth a warning every time.
        LLFile::remove(meta_out_path, ENOENT);
        return;
    }

    llofstream outstream(meta_out_path, std::ios_base::out | std::ios_base::binary);
    if (!outstream.is_open())
    {
        LL_WARNS("ShaderMgr") << "Failed to open file. Unable to save shader cache to: " << mShaderCacheDir << LL_ENDL;
        return;
    }

    LLSDSerialize::toBinary(out, outstream);
    if (outstream.fail())
    {
        LL_WARNS("ShaderMgr") << "Failed to serialize shader cache metadata" << LL_ENDL;
	outstream.close();
        LLFile::remove(meta_out_path, ENOENT); // Clean up partial write
        return;
    }
    outstream.close();

    LL_INFOS("ShaderMgr") << "Persisted " << (S32)shaders.size()
        << " entries. Removed " << (S32)removed << " entries." << LL_ENDL;
}

// S24: loadCachedProgramBinary()/saveCachedProgramBinary() removed - the
// GL-native shader-binary disk cache (glProgramBinary()-based), unreachable
// under DX_RENDER. DX_RENDER has its own DX-native shader bytecode cache
// (DXShader.h/.cpp).

//virtual
void LLShaderMgr::initAttribsAndUniforms()
{
	//MUST match order of enum in LLVertexBuffer.h
	mReservedAttribs.push_back("position");
	mReservedAttribs.push_back("normal");
	mReservedAttribs.push_back("texcoord0");
	mReservedAttribs.push_back("texcoord1");
	mReservedAttribs.push_back("texcoord2");
	mReservedAttribs.push_back("texcoord3");
	mReservedAttribs.push_back("diffuse_color");
	mReservedAttribs.push_back("emissive");
	mReservedAttribs.push_back("tangent");
	mReservedAttribs.push_back("weight");
	mReservedAttribs.push_back("weight4");
	mReservedAttribs.push_back("clothing");
	mReservedAttribs.push_back("joint");
	mReservedAttribs.push_back("texture_index");

	//matrix state
	mReservedUniforms.push_back("modelview_matrix");
	mReservedUniforms.push_back("projection_matrix");
	mReservedUniforms.push_back("inv_proj");
	mReservedUniforms.push_back("modelview_projection_matrix");
	mReservedUniforms.push_back("inv_modelview");
	mReservedUniforms.push_back("identity_matrix");
	mReservedUniforms.push_back("normal_matrix");
	mReservedUniforms.push_back("texture_matrix0");
	mReservedUniforms.push_back("texture_matrix1");
	mReservedUniforms.push_back("texture_matrix2");
	mReservedUniforms.push_back("texture_matrix3");
	mReservedUniforms.push_back("object_plane_s");
	mReservedUniforms.push_back("object_plane_t");

	mReservedUniforms.push_back("texture_base_color_transform"); // (GLTF)
	mReservedUniforms.push_back("texture_normal_transform"); // (GLTF)
	mReservedUniforms.push_back("texture_metallic_roughness_transform"); // (GLTF)
	mReservedUniforms.push_back("texture_occlusion_transform"); // (GLTF)
	mReservedUniforms.push_back("texture_emissive_transform"); // (GLTF)
	mReservedUniforms.push_back("base_color_texcoord"); // (GLTF)
	mReservedUniforms.push_back("emissive_texcoord"); // (GLTF)
	mReservedUniforms.push_back("normal_texcoord"); // (GLTF)
	mReservedUniforms.push_back("metallic_roughness_texcoord"); // (GLTF)
	mReservedUniforms.push_back("occlusion_texcoord"); // (GLTF)
	mReservedUniforms.push_back("gltf_node_id"); // (GLTF)
	mReservedUniforms.push_back("gltf_material_id"); // (GLTF)

	mReservedUniforms.push_back("terrain_texture_transforms"); // (GLTF)

	llassert(mReservedUniforms.size() == LLShaderMgr::TERRAIN_TEXTURE_TRANSFORMS + 1);

	mReservedUniforms.push_back("viewport");

	mReservedUniforms.push_back("light_position");
	mReservedUniforms.push_back("light_direction");
	mReservedUniforms.push_back("light_attenuation");
	mReservedUniforms.push_back("light_deferred_attenuation");
	mReservedUniforms.push_back("light_diffuse");
	mReservedUniforms.push_back("light_ambient");
	mReservedUniforms.push_back("light_count");
	mReservedUniforms.push_back("light");
	mReservedUniforms.push_back("light_col");
	mReservedUniforms.push_back("far_z");

	llassert(mReservedUniforms.size() == LLShaderMgr::MULTI_LIGHT_FAR_Z + 1);

	//NOTE: MUST match order in eGLSLReservedUniforms
	mReservedUniforms.push_back("proj_mat");
	mReservedUniforms.push_back("proj_near");
	mReservedUniforms.push_back("proj_p");
	mReservedUniforms.push_back("proj_n");
	mReservedUniforms.push_back("proj_origin");
	mReservedUniforms.push_back("proj_range");
	mReservedUniforms.push_back("proj_ambiance");
	mReservedUniforms.push_back("proj_shadow_idx");
	mReservedUniforms.push_back("shadow_fade");
	mReservedUniforms.push_back("proj_focus");
	mReservedUniforms.push_back("proj_lod");
	mReservedUniforms.push_back("proj_ambient_lod");

	llassert(mReservedUniforms.size() == LLShaderMgr::PROJECTOR_AMBIENT_LOD + 1);

	mReservedUniforms.push_back("color");
	mReservedUniforms.push_back("emissiveColor");
	mReservedUniforms.push_back("metallicFactor");
	mReservedUniforms.push_back("roughnessFactor");
	mReservedUniforms.push_back("mirror_flag");
	mReservedUniforms.push_back("clipPlane");
	mReservedUniforms.push_back("clipSign");

	mReservedUniforms.push_back("diffuseMap");
	mReservedUniforms.push_back("altDiffuseMap");
	mReservedUniforms.push_back("specularMap");
	mReservedUniforms.push_back("metallicRoughnessMap");
	mReservedUniforms.push_back("normalMap");
	mReservedUniforms.push_back("occlusionMap");
	mReservedUniforms.push_back("emissiveMap");
	mReservedUniforms.push_back("bumpMap");
	mReservedUniforms.push_back("bumpMap2");
	mReservedUniforms.push_back("environmentMap");
	mReservedUniforms.push_back("sceneMap");
	mReservedUniforms.push_back("sceneDepth");
	mReservedUniforms.push_back("reflectionProbes");
	mReservedUniforms.push_back("irradianceProbes");
	mReservedUniforms.push_back("heroProbes");
	mReservedUniforms.push_back("cloud_noise_texture");
	mReservedUniforms.push_back("cloud_noise_texture_next");
	mReservedUniforms.push_back("lightnorm");
	mReservedUniforms.push_back("sunlight_color");
	mReservedUniforms.push_back("ambient_color");
	mReservedUniforms.push_back("sky_hdr_scale");
	mReservedUniforms.push_back("sky_sunlight_scale");
	mReservedUniforms.push_back("sky_ambient_scale");
	mReservedUniforms.push_back("classic_mode");
	mReservedUniforms.push_back("blue_horizon");
	mReservedUniforms.push_back("blue_density");
	mReservedUniforms.push_back("haze_horizon");
	mReservedUniforms.push_back("haze_density");
	mReservedUniforms.push_back("cloud_shadow");
	mReservedUniforms.push_back("density_multiplier");
	mReservedUniforms.push_back("distance_multiplier");
	mReservedUniforms.push_back("max_y");
	mReservedUniforms.push_back("glow");
	mReservedUniforms.push_back("cloud_color");
	mReservedUniforms.push_back("cloud_pos_density1");
	mReservedUniforms.push_back("cloud_pos_density2");
	mReservedUniforms.push_back("cloud_scale");
	mReservedUniforms.push_back("gamma");
	mReservedUniforms.push_back("scene_light_strength");

	llassert(mReservedUniforms.size() == LLShaderMgr::SCENE_LIGHT_STRENGTH + 1);

	mReservedUniforms.push_back("center");
	mReservedUniforms.push_back("size");
	mReservedUniforms.push_back("falloff");

	mReservedUniforms.push_back("box_center");
	mReservedUniforms.push_back("box_size");

	mReservedUniforms.push_back("minLuminance");
	mReservedUniforms.push_back("maxExtractAlpha");
	mReservedUniforms.push_back("lumWeights");
	mReservedUniforms.push_back("warmthWeights");
	mReservedUniforms.push_back("warmthAmount");
	mReservedUniforms.push_back("glowStrength");
	mReservedUniforms.push_back("glowDelta");
	mReservedUniforms.push_back("glowNoiseMap");

	llassert(mReservedUniforms.size() == LLShaderMgr::GLOW_NOISE_MAP + 1);

	mReservedUniforms.push_back("minimum_alpha");
	mReservedUniforms.push_back("emissive_brightness");

	// Deferred
	mReservedUniforms.push_back("shadow_matrix");
	mReservedUniforms.push_back("env_mat");
	mReservedUniforms.push_back("shadow_clip");
	mReservedUniforms.push_back("sun_wash");
	mReservedUniforms.push_back("shadow_noise");
	mReservedUniforms.push_back("blur_size");
	mReservedUniforms.push_back("ssao_radius");
	mReservedUniforms.push_back("ssao_max_radius");
	mReservedUniforms.push_back("ssao_factor");
	mReservedUniforms.push_back("ssao_factor_inv");
	mReservedUniforms.push_back("ssao_effect_mat");
	mReservedUniforms.push_back("screen_res");
	mReservedUniforms.push_back("near_clip");
	mReservedUniforms.push_back("shadow_offset");
	mReservedUniforms.push_back("shadow_bias");
	mReservedUniforms.push_back("spot_shadow_bias");
	mReservedUniforms.push_back("spot_shadow_offset");
	mReservedUniforms.push_back("sun_dir");
	mReservedUniforms.push_back("moon_dir");
	mReservedUniforms.push_back("shadow_res");
	mReservedUniforms.push_back("proj_shadow_res");
	mReservedUniforms.push_back("depth_cutoff");
	mReservedUniforms.push_back("norm_cutoff");
	mReservedUniforms.push_back("shadow_target_width");

	llassert(mReservedUniforms.size() == LLShaderMgr::DEFERRED_SHADOW_TARGET_WIDTH + 1);

	mReservedUniforms.push_back("iterationCount");
	mReservedUniforms.push_back("rayStep");
	mReservedUniforms.push_back("distanceBias");
	mReservedUniforms.push_back("depthRejectBias");
	mReservedUniforms.push_back("glossySampleCount");
	mReservedUniforms.push_back("noiseSine");
	mReservedUniforms.push_back("adaptiveStepMultiplier");
	mReservedUniforms.push_back("ssrGlossThreshold");

	mReservedUniforms.push_back("modelview_delta");
	mReservedUniforms.push_back("inv_modelview_delta");
	mReservedUniforms.push_back("cube_snapshot");

	mReservedUniforms.push_back("last_projection_matrix");
	mReservedUniforms.push_back("history_map");

	mReservedUniforms.push_back("tc_scale");
	mReservedUniforms.push_back("rcp_screen_res");
	mReservedUniforms.push_back("rcp_frame_opt");
	mReservedUniforms.push_back("rcp_frame_opt2");

	mReservedUniforms.push_back("focal_distance");
	mReservedUniforms.push_back("blur_constant");
	mReservedUniforms.push_back("tan_pixel_angle");
	mReservedUniforms.push_back("magnification");
	mReservedUniforms.push_back("max_cof");
	mReservedUniforms.push_back("res_scale");
	mReservedUniforms.push_back("dof_width");
	mReservedUniforms.push_back("dof_height");

	mReservedUniforms.push_back("depthMap");
	mReservedUniforms.push_back("shadowMap0");
	mReservedUniforms.push_back("shadowMap1");
	mReservedUniforms.push_back("shadowMap2");
	mReservedUniforms.push_back("shadowMap3");
	mReservedUniforms.push_back("shadowMap4");
	mReservedUniforms.push_back("shadowMap5");

	llassert(mReservedUniforms.size() == LLShaderMgr::DEFERRED_SHADOW5 + 1);

	mReservedUniforms.push_back("positionMap");
	mReservedUniforms.push_back("diffuseRect");
	mReservedUniforms.push_back("specularRect");
	mReservedUniforms.push_back("emissiveRect");
	mReservedUniforms.push_back("exposureMap");
	mReservedUniforms.push_back("brdfLut");
	mReservedUniforms.push_back("noiseMap");
	mReservedUniforms.push_back("lightFunc");
	mReservedUniforms.push_back("lightMap");
	mReservedUniforms.push_back("bloomMap");
	mReservedUniforms.push_back("projectionMap");
	mReservedUniforms.push_back("norm_mat");

	mReservedUniforms.push_back("specular_color");
	mReservedUniforms.push_back("env_intensity");

	mReservedUniforms.push_back("matrixPalette");
	mReservedUniforms.push_back("translationPalette");

	mReservedUniforms.push_back("screenTex");
	mReservedUniforms.push_back("screenDepth");
	mReservedUniforms.push_back("refTex");
	mReservedUniforms.push_back("exclusionTex");
	mReservedUniforms.push_back("eyeVec");
	mReservedUniforms.push_back("time");
	mReservedUniforms.push_back("waveDir1");
	mReservedUniforms.push_back("waveDir2");
	mReservedUniforms.push_back("lightDir");
	mReservedUniforms.push_back("specular");
	mReservedUniforms.push_back("lightExp");
	mReservedUniforms.push_back("waterFogColor");
	mReservedUniforms.push_back("waterFogColorLinear");
	mReservedUniforms.push_back("waterFogDensity");
	mReservedUniforms.push_back("waterFogKS");
	mReservedUniforms.push_back("refScale");
	mReservedUniforms.push_back("waterHeight");
	mReservedUniforms.push_back("waterPlane");
	mReservedUniforms.push_back("normScale");
	mReservedUniforms.push_back("fresnelScale");
	mReservedUniforms.push_back("fresnelOffset");
	mReservedUniforms.push_back("blurMultiplier");
	mReservedUniforms.push_back("sunAngle");
	mReservedUniforms.push_back("scaledAngle");
	mReservedUniforms.push_back("sunAngle2");
	mReservedUniforms.push_back("waterMetallic"); // S24
	mReservedUniforms.push_back("waterRoughnessOverride"); // S24
	mReservedUniforms.push_back("waterSpecularIntensity"); // S24
	mReservedUniforms.push_back("waterReflectionIntensity"); // S24
	mReservedUniforms.push_back("waterColorTint"); // S24 Advanced
	mReservedUniforms.push_back("waterColorTintAlpha"); // S24 Advanced
	mReservedUniforms.push_back("waterFresnelPower"); // S24 Advanced
	mReservedUniforms.push_back("waterWaveSpeed"); // S24 Advanced
	mReservedUniforms.push_back("waterShoreFadeDistance"); // S24 Advanced
	mReservedUniforms.push_back("waterUnderwaterFogMult"); // S24 Advanced
	mReservedUniforms.push_back("waterReflectionWarmth"); // S24 Advanced
	mReservedUniforms.push_back("waterColorAbsorptionRate"); // S24 Advanced

	mReservedUniforms.push_back("camPosLocal");

	mReservedUniforms.push_back("gWindDir");
	mReservedUniforms.push_back("gSinWaveParams");
	mReservedUniforms.push_back("gGravity");

	mReservedUniforms.push_back("detail_0");
	mReservedUniforms.push_back("detail_1");
	mReservedUniforms.push_back("detail_2");
	mReservedUniforms.push_back("detail_3");

	mReservedUniforms.push_back("alpha_ramp");
	mReservedUniforms.push_back("paint_map");

	mReservedUniforms.push_back("detail_0_base_color");
	mReservedUniforms.push_back("detail_1_base_color");
	mReservedUniforms.push_back("detail_2_base_color");
	mReservedUniforms.push_back("detail_3_base_color");
	mReservedUniforms.push_back("detail_0_normal");
	mReservedUniforms.push_back("detail_1_normal");
	mReservedUniforms.push_back("detail_2_normal");
	mReservedUniforms.push_back("detail_3_normal");
	mReservedUniforms.push_back("detail_0_metallic_roughness");
	mReservedUniforms.push_back("detail_1_metallic_roughness");
	mReservedUniforms.push_back("detail_2_metallic_roughness");
	mReservedUniforms.push_back("detail_3_metallic_roughness");
	mReservedUniforms.push_back("detail_0_emissive");
	mReservedUniforms.push_back("detail_1_emissive");
	mReservedUniforms.push_back("detail_2_emissive");
	mReservedUniforms.push_back("detail_3_emissive");

	mReservedUniforms.push_back("baseColorFactors");
	mReservedUniforms.push_back("metallicFactors");
	mReservedUniforms.push_back("roughnessFactors");
	mReservedUniforms.push_back("emissiveColors");
	mReservedUniforms.push_back("minimum_alphas");

	mReservedUniforms.push_back("region_scale");

	mReservedUniforms.push_back("origin");
	mReservedUniforms.push_back("display_gamma");

	mReservedUniforms.push_back("inscatter");
	mReservedUniforms.push_back("sun_size");
	mReservedUniforms.push_back("fog_color");

	mReservedUniforms.push_back("transmittance_texture");
	mReservedUniforms.push_back("scattering_texture");
	mReservedUniforms.push_back("single_mie_scattering_texture");
	mReservedUniforms.push_back("irradiance_texture");
	mReservedUniforms.push_back("blend_factor");
	mReservedUniforms.push_back("moisture_level");
	mReservedUniforms.push_back("droplet_radius");
	mReservedUniforms.push_back("ice_level");
	mReservedUniforms.push_back("rainbow_map");
	mReservedUniforms.push_back("halo_map");
	mReservedUniforms.push_back("moon_brightness");
	mReservedUniforms.push_back("cloud_variance");
	mReservedUniforms.push_back("reflection_probe_ambiance");
	mReservedUniforms.push_back("max_probe_lod");
	mReservedUniforms.push_back("probe_strength");

	mReservedUniforms.push_back("sh_input_r");
	mReservedUniforms.push_back("sh_input_g");
	mReservedUniforms.push_back("sh_input_b");

	mReservedUniforms.push_back("sun_moon_glow_factor");
	mReservedUniforms.push_back("water_edge");
	mReservedUniforms.push_back("sun_up_factor");
	mReservedUniforms.push_back("moonlight_color");

	mReservedUniforms.push_back("debug_normal_draw_length");

	mReservedUniforms.push_back("edgesTex");
	mReservedUniforms.push_back("areaTex");
	mReservedUniforms.push_back("searchTex");
	mReservedUniforms.push_back("blendTex");

	llassert(mReservedUniforms.size() == END_RESERVED_UNIFORMS);

	std::set<std::string> dupe_check;

	for (U32 i = 0; i < mReservedUniforms.size(); ++i)
	{
		if (dupe_check.find(mReservedUniforms[i]) != dupe_check.end())
		{
			LL_ERRS() << "Duplicate reserved uniform name found: " << mReservedUniforms[i] << LL_ENDL;
		}
		dupe_check.insert(mReservedUniforms[i]);
	}
}