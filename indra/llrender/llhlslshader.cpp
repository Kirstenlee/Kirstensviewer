/**
 * @file llhlslshader.cpp
 * @brief GLSL helper functions and state.
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

#include "llhlslshader.h"

#include "llshadermgr.h"
#include "llfile.h"
#include "llrender.h"
#include "llvertexbuffer.h"
#include "llrendertarget.h"
#include "llwindow.h" // S24 (2026-08-16): LLSplashScreen::update() for startup shader-compile progress
#include "DXUIBatch.h"

#include "hbxxh.h"
#include "llsdserialize.h"

#if LL_DARWIN
#include "OpenGL/OpenGL.h"
#endif

 // Print-print list of shader included source files that are linked together via glAttachShader()
 // i.e. On macOS / OSX the AMD GLSL linker will display an error if a varying is left in an undefined state.
#define DEBUG_SHADER_INCLUDES 0

// Lots of STL stuff in here, using namespace std to keep things more readable
using std::vector;
using std::pair;
using std::make_pair;
using std::string;

GLuint LLHLSLShader::sCurBoundShader = 0;
LLHLSLShader* LLHLSLShader::sCurBoundShaderPtr = NULL;
S32 LLHLSLShader::sIndexedTextureChannels = 0;
U32 LLHLSLShader::sMaxGLTFMaterials = 0;
U32 LLHLSLShader::sMaxGLTFNodes = 0;
bool LLHLSLShader::sProfileEnabled = false;
bool LLHLSLShader::sCanProfile = true;
std::set<LLHLSLShader*> LLHLSLShader::sInstances;
LLHLSLShader::defines_map_t LLHLSLShader::sGlobalDefines;
U64 LLHLSLShader::sTotalTimeElapsed = 0;
U32 LLHLSLShader::sTotalTrianglesDrawn = 0;
U64 LLHLSLShader::sTotalSamplesDrawn = 0;
U32 LLHLSLShader::sTotalBinds = 0;
boost::json::value LLHLSLShader::sDefaultStats;

//UI shader -- declared here so llui_libtest will link properly
LLHLSLShader    gUIProgram;
LLHLSLShader    gSolidColorProgram;

// NOTE: Keep gShaderConsts* and LLHLSLShader::ShaderConsts_e in sync!
const std::string gShaderConstsKey[LLHLSLShader::NUM_SHADER_CONSTS] =
{
	  "LL_SHADER_CONST_CLOUD_MOON_DEPTH"
	, "LL_SHADER_CONST_STAR_DEPTH"
};

// NOTE: Keep gShaderConsts* and LLHLSLShader::ShaderConsts_e in sync!
const std::string gShaderConstsVal[LLHLSLShader::NUM_SHADER_CONSTS] =
{
	  "0.99998" // SHADER_CONST_CLOUD_MOON_DEPTH // SL-14113
	, "0.99999" // SHADER_CONST_STAR_DEPTH       // SL-14113
};


bool shouldChange(const LLVector4& v1, const LLVector4& v2)
{
	return v1 != v2;
}

//===============================
// LLGLSL Shader implementation
//===============================

//static
void LLHLSLShader::initProfile()
{
	sProfileEnabled = true;
	sTotalTimeElapsed = 0;
	sTotalTrianglesDrawn = 0;
	sTotalSamplesDrawn = 0;
	sTotalBinds = 0;

	for (auto ptr : sInstances)
	{
		ptr->clearStats();
	}
}


struct LLGLSLShaderCompareTimeElapsed
{
	bool operator()(const LLHLSLShader* const& lhs, const LLHLSLShader* const& rhs)
	{
		return lhs->mTimeElapsed < rhs->mTimeElapsed;
	}
};

//static
void LLHLSLShader::finishProfile(boost::json::value& statsv)
{
	sProfileEnabled = false;

	if (!statsv.is_null())
	{
		std::vector<LLHLSLShader*> sorted(sInstances.begin(), sInstances.end());
		std::sort(sorted.begin(), sorted.end(), LLGLSLShaderCompareTimeElapsed());

		auto& stats = statsv.as_object();
		auto shadersit = stats.emplace("shaders", boost::json::array_kind).first;
		auto& shaders = shadersit->value().as_array();
		bool unbound = false;
		for (auto ptr : sorted)
		{
			if (ptr->mBinds == 0)
			{
				unbound = true;
			}
			else
			{
				auto& shaderit = shaders.emplace_back(boost::json::object_kind);
				ptr->dumpStats(shaderit.as_object());
			}
		}

		constexpr float mega = 1'000'000.f;
		float totalTimeMs = sTotalTimeElapsed / mega;
		LL_INFOS() << "-----------------------------------" << LL_ENDL;
		LL_INFOS() << "Total rendering time: " << llformat("%.4f ms", totalTimeMs) << LL_ENDL;
		LL_INFOS() << "Total samples drawn: " << llformat("%.4f million", sTotalSamplesDrawn / mega) << LL_ENDL;
		LL_INFOS() << "Total triangles drawn: " << llformat("%.3f million", sTotalTrianglesDrawn / mega) << LL_ENDL;
		LL_INFOS() << "-----------------------------------" << LL_ENDL;
		auto totalsit = stats.emplace("totals", boost::json::object_kind).first;
		auto& totals = totalsit->value().as_object();
		totals.emplace("time", totalTimeMs / 1000.0);
		totals.emplace("binds", sTotalBinds);
		totals.emplace("samples", sTotalSamplesDrawn);
		totals.emplace("triangles", sTotalTrianglesDrawn);

		auto unusedit = stats.emplace("unused", boost::json::array_kind).first;
		auto& unused = unusedit->value().as_array();
		if (unbound)
		{
			LL_INFOS() << "The following shaders were unused: " << LL_ENDL;
			for (auto ptr : sorted)
			{
				if (ptr->mBinds == 0)
				{
					LL_INFOS() << ptr->mName << LL_ENDL;
					unused.emplace_back(ptr->mName);
				}
			}
		}
	}
}

void LLHLSLShader::clearStats()
{
	mTrianglesDrawn = 0;
	mTimeElapsed = 0;
	mSamplesDrawn = 0;
	mBinds = 0;
}

void LLHLSLShader::dumpStats(boost::json::object& stats)
{
	stats.emplace("name", mName);
	auto filesit = stats.emplace("files", boost::json::array_kind).first;
	auto& files = filesit->value().as_array();
    LL_INFOS() << "=============================================" << LL_ENDL;
    LL_INFOS() << mName << LL_ENDL;
	for (U32 i = 0; i < mShaderFiles.size(); ++i)
	{
		LL_INFOS() << mShaderFiles[i].first << LL_ENDL;
		files.emplace_back(mShaderFiles[i].first);
	}
    LL_INFOS() << "=============================================" << LL_ENDL;

	constexpr float  mega = 1'000'000.f;
	constexpr double giga = 1'000'000'000.0;
	F32 ms = mTimeElapsed / mega;
	F32 seconds = ms / 1000.f;

	F32 pct_tris = (F32)mTrianglesDrawn / (F32)sTotalTrianglesDrawn * 100.f;
	F32 tris_sec = (F32)(mTrianglesDrawn / mega);
	tris_sec /= seconds;

	F32 pct_samples = (F32)((F64)mSamplesDrawn / (F64)sTotalSamplesDrawn) * 100.f;
	F32 samples_sec = (F32)(mSamplesDrawn / giga);
	samples_sec /= seconds;

	F32 pct_binds = (F32)mBinds / (F32)sTotalBinds * 100.f;

	LL_INFOS() << "Triangles Drawn: " << mTrianglesDrawn << " " << llformat("(%.2f pct of total, %.3f million/sec)", pct_tris, tris_sec) << LL_ENDL;
	LL_INFOS() << "Binds: " << mBinds << " " << llformat("(%.2f pct of total)", pct_binds) << LL_ENDL;
	LL_INFOS() << "SamplesDrawn: " << mSamplesDrawn << " " << llformat("(%.2f pct of total, %.3f billion/sec)", pct_samples, samples_sec) << LL_ENDL;
	LL_INFOS() << "Time Elapsed: " << mTimeElapsed << " " << llformat("(%.2f pct of total, %.5f ms)\n", (F32)((F64)mTimeElapsed / (F64)sTotalTimeElapsed) * 100.f, ms) << LL_ENDL;
	stats.emplace("time", seconds);
	stats.emplace("binds", mBinds);
	stats.emplace("samples", mSamplesDrawn);
	stats.emplace("triangles", mTrianglesDrawn);
}

//static
void LLHLSLShader::startProfile()
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
	if (sProfileEnabled && sCurBoundShaderPtr)
	{
		sCurBoundShaderPtr->placeProfileQuery();
	}
}

//static
void LLHLSLShader::stopProfile()
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	if (sProfileEnabled && sCurBoundShaderPtr)
	{
		sCurBoundShaderPtr->unbind();
	}
}

void LLHLSLShader::placeProfileQuery(bool for_runtime)
{
	// S24 (2026-09-04): raw OpenGL GPU timer/samples/primitives queries,
	// completely unguarded - OpenGL is deliberately not linked into a
	// DX_RENDER=ON build at all (newview/CMakeLists.txt's own enforcement
	// comment), so these are unresolved function pointers there. Same bug
	// class, found and fixed alongside a confirmed live crash in
	// LLVOAvatar::placeProfileQuery()/readProfileQuery() (see that file's
	// matching comment) - this is the sibling per-shader profiling path
	// (LLPipeline::profileAvatar()'s per-attachment branch, gDebugProgram,
	// for_runtime=true), same risk, not yet hit live but reachable the same
	// way. sProfileEnabled/sCanProfile are plain bools with no DX_RENDER
	// awareness of their own, so they don't gate this on their own. No
	// DX11-native GPU timer-query equivalent exists yet - safe no-op.
#ifndef DX_RENDER
	if (sProfileEnabled || for_runtime)
	{
		if (mTimerQuery == 0)
		{
			glGenQueries(1, &mSamplesQuery);
			glGenQueries(1, &mTimerQuery);
			glGenQueries(1, &mPrimitivesQuery);
		}

		glBeginQuery(GL_TIME_ELAPSED, mTimerQuery);

		if (!for_runtime)
		{
			glBeginQuery(GL_SAMPLES_PASSED, mSamplesQuery);
			glBeginQuery(GL_PRIMITIVES_GENERATED, mPrimitivesQuery);
		}
	}
#endif
}

bool LLHLSLShader::readProfileQuery(bool for_runtime, bool force_read)
{
    // S24 (2026-09-04): see placeProfileQuery()'s matching comment - same
    // raw-GL crash risk, same fix.
#ifdef DX_RENDER
    return false;
#endif
    if ((sProfileEnabled || for_runtime) && sCanProfile)
	{
		if (!mProfilePending)
		{
			glEndQuery(GL_TIME_ELAPSED);
			if (!for_runtime)
			{
				glEndQuery(GL_SAMPLES_PASSED);
				glEndQuery(GL_PRIMITIVES_GENERATED);
			}
			mProfilePending = for_runtime;
		}

		if (mProfilePending && for_runtime && !force_read)
		{
			GLuint64 result = 0;
			glGetQueryObjectui64v(mTimerQuery, GL_QUERY_RESULT_AVAILABLE, &result);

			if (result != GL_TRUE)
			{
				return false;
			}
		}

		GLuint64 time_elapsed = 0;
		glGetQueryObjectui64v(mTimerQuery, GL_QUERY_RESULT, &time_elapsed);
		mTimeElapsed += time_elapsed;
		mProfilePending = false;

		if (!for_runtime)
		{
			GLuint64 samples_passed = 0;
			glGetQueryObjectui64v(mSamplesQuery, GL_QUERY_RESULT, &samples_passed);

			GLuint64 primitives_generated = 0;
			glGetQueryObjectui64v(mPrimitivesQuery, GL_QUERY_RESULT, &primitives_generated);
			sTotalTimeElapsed += time_elapsed;

			sTotalSamplesDrawn += samples_passed;
			mSamplesDrawn += samples_passed;

			U32 tri_count = (U32)primitives_generated / 3;

			mTrianglesDrawn += tri_count;
			sTotalTrianglesDrawn += tri_count;

			sTotalBinds++;
			mBinds++;
		}
	}

	return true;
}

LLHLSLShader::LLHLSLShader()
	: mProgramObject(0),
	mAttributeMask(0),
	mTotalUniformSize(0),
	mActiveTextureChannels(0),
	mShaderLevel(0),
	mShaderGroup(SG_DEFAULT),
	mFeatures(),
	mUniformsDirty(false),
	mTimerQuery(0),
	mSamplesQuery(0),
	mPrimitivesQuery(0)
{
	// S24 (2026-09-10, task #274): was left default-constructed (garbage) -
	// syncMatrices()'s GL branch (and, as of this task, its DX_RENDER
	// branch too) compares LLRender::mMatHash[mode] against this array to
	// decide whether a real matrix re-upload is needed. Uninitialized
	// garbage could coincidentally match LLRender's real current hash on
	// this shader's very first sync, wrongly skipping its first-ever
	// matrix upload and leaving its constant buffer at stale/zero matrices
	// indefinitely. UINT32_MAX matches the same "force a first sync"
	// sentinel GL's own static caches already use (cached_mvp_mdv_hash
	// etc., llrender.cpp).
	for (U32 i = 0; i < LLRender::NUM_MATRIX_MODES; ++i)
	{
		mMatHash[i] = UINT32_MAX;
	}
}

LLHLSLShader::~LLHLSLShader()
{
}

void LLHLSLShader::unload()
{
	mShaderFiles.clear();
	mDefines.clear();
	mFeatures = LLShaderFeatures();

	unloadInternal();
}

void LLHLSLShader::unloadInternal()
{
	sInstances.erase(this);

	mDXVertexShader.reset();
	mDXPixelShader.reset();
	mDXVertexSource.clear();
	mDXPixelSource.clear();
}

bool LLHLSLShader::createShader()
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	// S24 (2026-08-16): single chokepoint for all ~130 createShader() call
	// sites - covers startup shader-compile progress without touching each
	// one. isVisible() guards against reopening the splash dialog on a
	// mid-session shader reload (graphics settings change etc.) - update()
	// unconditionally recreates the splash window if it was already hidden.
	if (LLSplashScreen::isVisible())
	{
		LLSplashScreen::update("Compiling shader: " + mName);
	}

	return createShaderDX();
}

namespace
{
	// Mirrors the small set of #define's LLShaderMgr::loadShaderFile() (GL
	// path) unconditionally injects into every file via extra_code_text -
	// real shaders reference these directly (e.g. diffuseF.hlsl's
	// GBUFFER_FLAG_HAS_ATMOS). Built once per stage per program and prepended
	// to the concatenated blob, rather than baked into each cached file's
	// text, so attached utility files don't each carry their own duplicate
	// copy of the same defines.
	std::string buildDXShaderHeader(bool is_fragment, const LLHLSLShader::defines_map_t& defines)
	{
		std::string out = is_fragment ? "#define FRAGMENT_SHADER 1\n" : "#define VERTEX_SHADER 1\n";

		out += "#define GBUFFER_FLAG_SKIP_ATMOS 0.0\n";
		out += "#define GBUFFER_FLAG_HAS_ATMOS 0.34\n";
		out += "#define GBUFFER_FLAG_HAS_PBR 0.67\n";
		out += "#define GBUFFER_FLAG_HAS_HDRI 1.0\n";
		out += "#define GET_GBUFFER_FLAG(data, flag) (abs(data-flag)< 0.1)\n";

		for (auto& d : defines)
		{
			out += "#define " + d.first + " " + d.second + "\n";
		}

		// sGlobalDefines (llviewershadermgr.cpp's loadBasicShaders(), e.g.
		// MAX_JOINTS_PER_MESH_OBJECT/SUN_SHADOW/SSR/REFMAP_LEVEL/terrain-PBR
		// settings) was never emitted here - any HLSL file referencing one
		// of these directly (not just inside #if defined(...)) saw it as
		// undeclared. Per-shader defines above take precedence on conflict.
		for (auto& d : LLHLSLShader::sGlobalDefines)
		{
			if (defines.find(d.first) == defines.end())
			{
				out += "#define " + d.first + " " + d.second + "\n";
			}
		}

		return out;
	}
}

bool LLHLSLShader::buildDXSource()
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	sInstances.insert(this);

	llassert_always(!mShaderFiles.empty());

	mDXVertexSource.clear();
	mDXPixelSource.clear();

	// Entry file(s) first (matches GL's compile-entry-then-attach-features
	// order) - loadShaderFile() caches each file's raw (extension-swapped)
	// HLSL text into mVertexShaderSourceText/mFragmentShaderSourceText.
	for (auto& file : mShaderFiles)
	{
		GLuint ok = LLShaderMgr::instance()->loadShaderFile(file.first, mShaderLevel, file.second, &mDefines, mFeatures.mIndexedTextureChannels, mFeatures.isDeferred || mFeatures.hasReflectionProbes);
		if (!ok)
		{
			LL_SHADER_LOADING_WARNS() << "Failed to load " << file.first << " for shader " << mName << LL_ENDL;
			return false;
		}

		if (file.second == GL_VERTEX_SHADER)
		{
			mDXVertexSource += LLShaderMgr::instance()->mVertexShaderSourceText[file.first];
		}
		else if (file.second == GL_FRAGMENT_SHADER)
		{
			mDXPixelSource += LLShaderMgr::instance()->mFragmentShaderSourceText[file.first];
		}
	}

	// Attached utility files next, in the exact order GL attaches them -
	// attachVertexObject()/attachFragmentObject()'s DX_RENDER branches below
	// append to mDXVertexSource/mDXPixelSource instead of glAttachShader'ing
	// a precompiled object, so attachShaderFeatures() itself is unchanged.
	if (!LLShaderMgr::instance()->attachShaderFeatures(this))
	{
		return false;
	}

	if (!mDXVertexSource.empty())
	{
		// S24 (DX_RENDER, 2026-07-30): resolve `#include "varying/....hlsli"`
		// directives (see DXShader::resolveIncludes()'s comment) before the
		// other text-injection passes below, so a shared varying struct is
		// defined before anything in this file's own text references it.
		// No-op for every shader with no #include directive.
		DXShader::resolveIncludes(mDXVertexSource);
		// Avatar body skinning (getSkinnedTransform(), class1/avatar/
		// avatarSkinV.hlsl) references a bare "weight" global that only
		// exists as a free-standing GLSL attribute on the GL side - see
		// DXShader::injectSkinningInputs()'s comment. No-op for every
		// non-skinned shader.
		DXShader::injectSkinningInputs(mDXVertexSource);
		// Same idea for indexedTextureV.hlsl's texture_index/
		// vary_texture_index pair - see DXShader::injectTextureIndexInputs()'s
		// comment. No-op for every non-indexed-texture shader.
		DXShader::injectTextureIndexInputs(mDXVertexSource);
		mDXVertexSource = buildDXShaderHeader(false, mDefines) + mDXVertexSource;
	}
	if (!mDXPixelSource.empty())
	{
		DXShader::resolveIncludes(mDXPixelSource);
		mDXPixelSource = buildDXShaderHeader(true, mDefines) + mDXPixelSource;
	}

	return true;
}

bool LLHLSLShader::createShaderDX()
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	// Release any previously-compiled shader objects before rebuilding -
	// createShaderDX() can be called again on this instance (e.g. a reload).
	mDXVertexShader.reset();
	mDXPixelShader.reset();

	if (!buildDXSource())
	{
		return false;
	}

	bool success = true;
	if (!mDXVertexSource.empty())
	{
		bool vs_ok = mDXVertexShader.compileVertexShader(mDXVertexSource, mName);
		success = vs_ok && success;
		if (vs_ok)
		{
			// See DXShader::reflectVertexAttributeMask()'s comment - GL's
			// mapAttributes() has no createShaderDX() equivalent to hook
			// into, so this is populated here instead.
			mAttributeMask = mDXVertexShader.reflectVertexAttributeMask();
		}
	}
	if (!mDXPixelSource.empty())
	{
		success = mDXPixelShader.compilePixelShader(mDXPixelSource, mName) && success;
	}

	if (!success)
	{
		LL_SHADER_LOADING_WARNS() << "Failed to compile HLSL shader: " << mName << LL_ENDL;
	}
	else
	{
		// S24 (2026-08-03): DX-native equivalent of mapUniforms()'s GL-only
		// mTexture[] population (glGetUniformLocation-based, never runs
		// under DX_RENDER - createShaderDX() returns before mapUniforms()).
		// Without this, every named-uniform texture bind (bindTexture(),
		// enableTexture(), etc.) was a hardcoded no-op - confirmed root
		// cause of PBR materials showing an unrelated, leftover-bound
		// texture depending on draw order/camera angle (every diffuse/bump/
		// specular/emissive bind for a PBR material silently did nothing,
		// leaving whatever the previous draw call's SRV was in that
		// register). Source the name->register(tN) mapping via
		// DXShader::getTextureBindPoint() (D3D11 reflection) instead of GL
		// reflection - same shape as reflectConstants() already does for
		// $Globals uniforms.
		mTexture.clear();
		mTexture.resize(LLShaderMgr::instance()->mReservedUniforms.size(), -1);
		for (size_t i = 0; i < LLShaderMgr::instance()->mReservedUniforms.size(); ++i)
		{
			UINT bind_point = 0;
			if (mDXPixelShader.getTextureBindPoint(LLShaderMgr::instance()->mReservedUniforms[i], bind_point))
			{
				mTexture[i] = (GLint)bind_point;
			}
		}
	}

	return success;
}

#if DEBUG_SHADER_INCLUDES
void dumpAttachObject(const char* func_name, GLuint program_object, const std::string& object_path)
{
	GLchar* info_log;
	GLint      info_len_expect = 0;
	GLint      info_len_actual = 0;

	glGetShaderiv(program_object, GL_INFO_LOG_LENGTH, , &info_len_expect);
	fprintf(stderr, " * %-20s(), log size: %d, %s\n", func_name, info_len_expect, object_path.c_str());

	if (info_len_expect > 0)
	{
		fprintf(stderr, " ========== %s() ========== \n", func_name);
		info_log = new GLchar[info_len_expect];
		glGetProgramInfoLog(program_object, info_len_expect, &info_len_actual, info_log);
		fprintf(stderr, "%s\n", info_log);
		delete[] info_log;
	}
}
#endif // DEBUG_SHADER_INCLUDES

bool LLHLSLShader::attachVertexObject(std::string object_path)
{
	// No GL program/glAttachShader concept under DX_RENDER - append this
	// utility file's cached HLSL text to the vertex-stage source blob being
	// built up by createShaderDX(), in the exact order attachShaderFeatures()
	// calls this (that ordering is what makes textual concatenation valid).
	auto iter = LLShaderMgr::instance()->mVertexShaderSourceText.find(object_path);
	if (iter != LLShaderMgr::instance()->mVertexShaderSourceText.end())
	{
		mDXVertexSource += iter->second;
		return true;
	}

	LL_SHADER_LOADING_WARNS() << "Attempting to attach shader object: '" << object_path << "' that hasn't been compiled." << LL_ENDL;
	return false;
}

bool LLHLSLShader::attachFragmentObject(std::string object_path)
{
	// See attachVertexObject() - same idea, fragment-stage source blob.
	auto iter = LLShaderMgr::instance()->mFragmentShaderSourceText.find(object_path);
	if (iter != LLShaderMgr::instance()->mFragmentShaderSourceText.end())
	{
		mDXPixelSource += iter->second;
		return true;
	}

	LL_SHADER_LOADING_WARNS() << "Attempting to attach shader object: '" << object_path << "' that hasn't been compiled." << LL_ENDL;
	return false;
}

void LLHLSLShader::attachObject(GLuint object)
{
	if (mUsingBinaryProgram)
		return;

	if (object != 0)
	{
		stop_glerror();
		glAttachShader(mProgramObject, object);
#if DEBUG_SHADER_INCLUDES
		std::string object_path("???");
		dumpAttachObject("attachObject", mProgramObject, object_path);
#endif // DEBUG_SHADER_INCLUDES
		stop_glerror();
	}
	else
	{
		LL_SHADER_LOADING_WARNS() << "Attempting to attach non existing shader object. " << LL_ENDL;
	}
}

void LLHLSLShader::attachObjects(GLuint* objects, S32 count)
{
	if (mUsingBinaryProgram)
		return;

	for (S32 i = 0; i < count; i++)
	{
		attachObject(objects[i]);
	}
}

bool LLHLSLShader::mapAttributes()
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	bool res = true;
	if (!mUsingBinaryProgram)
	{
		//before linking, make sure reserved attributes always have consistent locations
		for (U32 i = 0; i < LLShaderMgr::instance()->mReservedAttribs.size(); i++)
		{
			const char* name = LLShaderMgr::instance()->mReservedAttribs[i].c_str();
			glBindAttribLocation(mProgramObject, i, (const GLchar*)name);
		}

		//link the program
		res = link();
	}

	mAttribute.clear();
#if LL_RELEASE_WITH_DEBUG_INFO
	mAttribute.resize(LLShaderMgr::instance()->mReservedAttribs.size(), { -1, NULL });
#else
	mAttribute.resize(LLShaderMgr::instance()->mReservedAttribs.size(), -1);
#endif

	if (res)
	{ //read back channel locations
		mAttributeMask = 0;

		//read back reserved channels first
		for (U32 i = 0; i < LLShaderMgr::instance()->mReservedAttribs.size(); i++)
		{
			const char* name = LLShaderMgr::instance()->mReservedAttribs[i].c_str();
			S32 index = glGetAttribLocation(mProgramObject, (const GLchar*)name);
			if (index != -1)
			{
#if LL_RELEASE_WITH_DEBUG_INFO
				mAttribute[i] = { index, name };
#else
				mAttribute[i] = index;
#endif
				mAttributeMask |= 1 << i;
				LL_DEBUGS("ShaderUniform") << "Attribute " << name << " assigned to channel " << index << LL_ENDL;
			}
		}

		return true;
	}

	return false;
}

void LLHLSLShader::mapUniform(GLint index)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	if (index == -1)
	{
		return;
	}

	GLenum type;
	GLsizei length;
	GLint size = -1;
	char name[1024];        /* Flawfinder: ignore */
	name[0] = 0;

	glGetActiveUniform(mProgramObject, index, 1024, &length, &size, &type, (GLchar*)name);
	if (size > 0)
	{
		switch (type)
		{
		case GL_FLOAT_VEC2: size *= 2; break;
		case GL_FLOAT_VEC3: size *= 3; break;
		case GL_FLOAT_VEC4: size *= 4; break;
		case GL_DOUBLE: size *= 2; break;
		case GL_DOUBLE_VEC2: size *= 2; break;
		case GL_DOUBLE_VEC3: size *= 6; break;
		case GL_DOUBLE_VEC4: size *= 8; break;
		case GL_INT_VEC2: size *= 2; break;
		case GL_INT_VEC3: size *= 3; break;
		case GL_INT_VEC4: size *= 4; break;
		case GL_UNSIGNED_INT_VEC2: size *= 2; break;
		case GL_UNSIGNED_INT_VEC3: size *= 3; break;
		case GL_UNSIGNED_INT_VEC4: size *= 4; break;
		case GL_BOOL_VEC2: size *= 2; break;
		case GL_BOOL_VEC3: size *= 3; break;
		case GL_BOOL_VEC4: size *= 4; break;
		case GL_FLOAT_MAT2: size *= 4; break;
		case GL_FLOAT_MAT3: size *= 9; break;
		case GL_FLOAT_MAT4: size *= 16; break;
		case GL_FLOAT_MAT2x3: size *= 6; break;
		case GL_FLOAT_MAT2x4: size *= 8; break;
		case GL_FLOAT_MAT3x2: size *= 6; break;
		case GL_FLOAT_MAT3x4: size *= 12; break;
		case GL_FLOAT_MAT4x2: size *= 8; break;
		case GL_FLOAT_MAT4x3: size *= 12; break;
		case GL_DOUBLE_MAT2: size *= 8; break;
		case GL_DOUBLE_MAT3: size *= 18; break;
		case GL_DOUBLE_MAT4: size *= 32; break;
		case GL_DOUBLE_MAT2x3: size *= 12; break;
		case GL_DOUBLE_MAT2x4: size *= 16; break;
		case GL_DOUBLE_MAT3x2: size *= 12; break;
		case GL_DOUBLE_MAT3x4: size *= 24; break;
		case GL_DOUBLE_MAT4x2: size *= 16; break;
		case GL_DOUBLE_MAT4x3: size *= 24; break;
		}
		mTotalUniformSize += size;
	}

	S32 location = glGetUniformLocation(mProgramObject, name);
	if (location != -1)
	{
		//chop off "[0]" so we can always access the first element
		//of an array by the array name
		char* is_array = strstr(name, "[0]");
		if (is_array)
		{
			is_array[0] = 0;
		}

		LLStaticHashedString hashedName(name);
		mUniformMap[hashedName] = location;

		LL_DEBUGS("ShaderUniform") << "Uniform " << name << " is at location " << location << LL_ENDL;

		//find the index of this uniform
		for (S32 i = 0; i < (S32)LLShaderMgr::instance()->mReservedUniforms.size(); i++)
		{
			if ((mUniform[i] == -1)
				&& (LLShaderMgr::instance()->mReservedUniforms[i] == name))
			{
				//found it
				mUniform[i] = location;
				mTexture[i] = mapUniformTextureChannel(location, type, size);
				if (mTexture[i] != -1)
				{
					LL_DEBUGS("GLSLTextureChannels") << name << " assigned to texture channel " << mTexture[i] << LL_ENDL;
				}
				return;
			}
		}
	}
}

void LLHLSLShader::clearPermutations()
{
	mDefines.clear();
}

void LLHLSLShader::addPermutation(std::string name, std::string value)
{
	mDefines[name] = value;
}

void LLHLSLShader::addConstant(const LLHLSLShader::eShaderConsts shader_const)
{
	addPermutation(gShaderConstsKey[shader_const], gShaderConstsVal[shader_const]);
}

void LLHLSLShader::removePermutation(std::string name)
{
	mDefines.erase(name);
}

GLint LLHLSLShader::mapUniformTextureChannel(GLint location, GLenum type, GLint size)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	if ((type >= GL_SAMPLER_1D && type <= GL_SAMPLER_2D_RECT_SHADOW) ||
		type == GL_SAMPLER_2D_MULTISAMPLE ||
		type == GL_SAMPLER_CUBE_MAP_ARRAY)
	{   //this here is a texture
		GLint ret = mActiveTextureChannels;
		if (size == 1)
		{
			glUniform1i(location, mActiveTextureChannels);
			mActiveTextureChannels++;
		}
		else
		{
			//is array of textures, make sequential after this texture
			GLint channel[16]; // <=== only support up to 16 texture channels
			llassert(size <= 16);
			size = llmin(size, 16);
			for (int i = 0; i < size; ++i)
			{
				channel[i] = mActiveTextureChannels++;
			}
			glUniform1iv(location, size, channel);
		}

		return ret;
	}
	return -1;
}

bool LLHLSLShader::mapUniforms()
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	bool res = true;

	mTotalUniformSize = 0;
	mActiveTextureChannels = 0;
	mUniform.clear();
	mUniformMap.clear();
	mTexture.clear();
	mValue.clear();
	//initialize arrays
	mUniform.resize(LLShaderMgr::instance()->mReservedUniforms.size(), -1);
	mTexture.resize(LLShaderMgr::instance()->mReservedUniforms.size(), -1);

	bind();

	//get the number of active uniforms
	GLint activeCount;
	glGetProgramiv(mProgramObject, GL_ACTIVE_UNIFORMS, &activeCount);

	//........................................................................................................................................
	//........................................................................................

	/*
	EXPLANATION:
	This is part of code is temporary because as the final result the mapUniform() should be rewrited.
	But it's a huge a volume of work which is need to be a more carefully performed for avoid possible
	regression's (i.e. it should be formalized a separate ticket in JIRA).

	RESON:
	The reason of this code is that SL engine is very sensitive to fact that "diffuseMap" should be appear
	first as uniform parameter which is should get 0-"texture channel" index (see mapUniformTextureChannel() and mActiveTextureChannels)
	it influence to which is texture matrix will be updated during rendering.

	But, order of indexe's of uniform variables is not defined and GLSL compiler can change it as want
	, even if the "diffuseMap" will be appear and use first in shader code.

	As example where this situation appear see: "Deferred Material Shader 28/29/30/31"
	And tickets: MAINT-4165, MAINT-4839, MAINT-3568, MAINT-6437

	--- davep TODO -- pretty sure the entire block here is superstitious and that the uniform index has nothing to do with the texture channel
				texture channel should follow the uniform VALUE
	*/

	S32 diffuseMap = glGetUniformLocation(mProgramObject, "diffuseMap");
	S32 specularMap = glGetUniformLocation(mProgramObject, "specularMap");
	S32 bumpMap = glGetUniformLocation(mProgramObject, "bumpMap");
	S32 altDiffuseMap = glGetUniformLocation(mProgramObject, "altDiffuseMap");
	S32 environmentMap = glGetUniformLocation(mProgramObject, "environmentMap");
	S32 reflectionMap = glGetUniformLocation(mProgramObject, "reflectionMap");

	std::set<S32> skip_index;

	if (-1 != diffuseMap && (-1 != specularMap || -1 != bumpMap || -1 != environmentMap || -1 != altDiffuseMap))
	{
		GLenum type;
		GLsizei length;
		GLint size = -1;
		char name[1024];

		diffuseMap = altDiffuseMap = specularMap = bumpMap = environmentMap = -1;

		for (S32 i = 0; i < activeCount; i++)
		{
			name[0] = '\0';

			glGetActiveUniform(mProgramObject, i, 1024, &length, &size, &type, (GLchar*)name);

			if (-1 == diffuseMap && std::string(name) == "diffuseMap")
			{
				diffuseMap = i;
				continue;
			}

			if (-1 == specularMap && std::string(name) == "specularMap")
			{
				specularMap = i;
				continue;
			}

			if (-1 == bumpMap && std::string(name) == "bumpMap")
			{
				bumpMap = i;
				continue;
			}

			if (-1 == environmentMap && std::string(name) == "environmentMap")
			{
				environmentMap = i;
				continue;
			}

			if (-1 == reflectionMap && std::string(name) == "reflectionMap")
			{
				reflectionMap = i;
				continue;
			}

			if (-1 == altDiffuseMap && std::string(name) == "altDiffuseMap")
			{
				altDiffuseMap = i;
				continue;
			}
		}

		bool specularDiff = specularMap < diffuseMap && -1 != specularMap;
		bool bumpLessDiff = bumpMap < diffuseMap && -1 != bumpMap;
		bool envLessDiff = environmentMap < diffuseMap && -1 != environmentMap;
		bool refLessDiff = reflectionMap < diffuseMap && -1 != reflectionMap;

		if (specularDiff || bumpLessDiff || envLessDiff || refLessDiff)
		{
			mapUniform(diffuseMap);
			skip_index.insert(diffuseMap);

			if (-1 != specularMap) {
				mapUniform(specularMap);
				skip_index.insert(specularMap);
			}

			if (-1 != bumpMap) {
				mapUniform(bumpMap);
				skip_index.insert(bumpMap);
			}

			if (-1 != environmentMap) {
				mapUniform(environmentMap);
				skip_index.insert(environmentMap);
			}

			if (-1 != reflectionMap) {
				mapUniform(reflectionMap);
				skip_index.insert(reflectionMap);
			}
		}
	}

	//........................................................................................

	for (S32 i = 0; i < activeCount; i++)
	{
		//........................................................................................
		if (skip_index.end() != skip_index.find(i)) continue;
		//........................................................................................

		mapUniform(i);
	}
	//........................................................................................................................................

	// Set up block binding, in a way supported by Apple (rather than binding = 1 in .glsl).
	// See slide 35 and more of https://docs.huihoo.com/apple/wwdc/2011/session_420__advances_in_opengl_for_mac_os_x_lion.pdf
	const char* ubo_names[] =
	{
		"ReflectionProbes", // UB_REFLECTION_PROBES
		"GLTFJoints",       // UB_GLTF_JOINTS
		"GLTFNodes",        // UB_GLTF_NODES
		"GLTFMaterials",    // UB_GLTF_MATERIALS
	};

	llassert(LL_ARRAY_SIZE(ubo_names) == NUM_UNIFORM_BLOCKS);

	for (U32 i = 0; i < NUM_UNIFORM_BLOCKS; ++i)
	{
		GLuint UBOBlockIndex = glGetUniformBlockIndex(mProgramObject, ubo_names[i]);
		if (UBOBlockIndex != GL_INVALID_INDEX)
		{
			glUniformBlockBinding(mProgramObject, UBOBlockIndex, i);
		}
	}

	unbind();

	LL_DEBUGS("ShaderUniform") << "Total Uniform Size: " << mTotalUniformSize << LL_ENDL;
	return res;
}

bool LLHLSLShader::link(bool suppress_errors)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	bool success = LLShaderMgr::instance()->linkProgramObject(mProgramObject, suppress_errors);

	if (!success && !suppress_errors)
	{
		LLShaderMgr::instance()->dumpObjectLog(mProgramObject, !success, mName);
	}

	if (success)
	{
		LLShaderMgr::instance()->saveCachedProgramBinary(this);
	}

	return success;
}

void LLHLSLShader::bind()
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	// Minimum needed so LLVertexBuffer::setBuffer() (which asserts
	// sCurBoundShaderPtr) is reachable: bind the compiled DX shaders and
	// track "current shader" by pointer identity instead of mProgramObject
	// (always 0 under DX_RENDER - no GL program object exists).
	// Matrix uniforms (modelview/projection/normal/texture0) ARE wired -
	// see LLRender::syncMatrices()'s DX_RENDER branch, called from
	// LLVertexBuffer::drawRange()/drawArrays() same as GL. mAttributeMask-
	// driven client-array setup (setupClientArrays()) still has no DX_RENDER
	// equivalent, deferred to whenever a pool needing it is converted.
	llassert_always(mDXVertexShader.getVS() != nullptr);

	// S24 (2026-08-25, task #224): removed the "skip if sCurBoundShaderPtr
	// already == this" optimization - same bug class as the one just fixed
	// in LLVertexBuffer::setBuffer() (see its comment). DXUIBatch::
	// drawAndPop() (dxrender/resources/DXUIBatch.cpp) calls
	// ctx->VSSetShader()/PSSetShader() directly for its own pending batch's
	// shader, WITHOUT updating sCurBoundShaderPtr - so this bookkeeping can
	// silently desync from what's actually bound in D3D11. When that
	// happens, a subsequent bind() call for the shader sCurBoundShaderPtr
	// (wrongly) still thinks is current would skip the real VSSetShader/
	// PSSetShader calls entirely, leaving whatever gDXUIBatch last bound
	// active instead - confirmed via a live log capture (task #224) showing
	// the same persistently-cached LLVertexBuffer replayed one frame under
	// "UI Shader" and the very next under "Solid Color Shader" with no
	// legitimate call in between that should have changed it. Root cause of
	// the button hover-highlight flicker/settle. gDXUIBatch.flushPending()
	// stays unconditional too - still needed regardless of the dedup.
	gDXUIBatch.flushPending();
	gDXDevice.getContext()->VSSetShader(mDXVertexShader.getVS(), nullptr, 0);
	gDXDevice.getContext()->PSSetShader(mDXPixelShader.getPS(), nullptr, 0);
	sCurBoundShaderPtr = this;

	// S24 (2026-08-05): updateShaderUniforms() (env/lighting uniforms via
	// LLEnvironment - ambient_color/blue_horizon/blue_density/haze_horizon/
	// haze_density/cloud_shadow/density_multiplier/distance_multiplier/
	// max_y/glow/sun_moon_glow_factor/sky_sunlight_scale/sky_ambient_scale/
	// lightnorm/sunlight_color/moonlight_color/classic_mode, everything
	// atmosphericsFuncs.hlsl declares) was the one piece this comment used
	// to say "has no DX_RENDER equivalent yet" - gDeferredSoftenProgram
	// (softenLightF.hlsl) is the first DX_RENDER shader that actually needs
	// any of them, confirmed via a direct readback showing amblit/sunlit
	// reading exactly zero and atten reading exactly (1,1,1) (density_
	// multiplier's zero-initialized default collapsing exp(-0*x) to 1) at
	// every sampled pixel - not a math bug, a "never uploaded" bug.
	// Gated on mUniformsDirty rather than calling unconditionally -
	// mUniformsDirty defaults false
	// (llhlslshader.cpp ctor) and is only ever set true by
	// LLEnvironment::update()'s per-frame "mark every registered shader
	// dirty" loop, so this can never fire before LLEnvironment has actually
	// run at least one real frame update. An earlier unconditional-call
	// version of this fix caused an instant startup crash (before window
	// creation) - almost certainly this exact call reached LLEnvironment::
	// instance()/its sky/water uniform tables during early shader
	// compilation, well before the main loop (and LLEnvironment) is up.
	if (mUniformsDirty)
	{
		LLShaderMgr::instance()->updateShaderUniforms(this);
		mUniformsDirty = false;
	}
}

void LLHLSLShader::bind(U8 variant)
{
	llassert_always(mGLTFVariants.size() == LLHLSLShader::NUM_GLTF_VARIANTS);
	llassert_always(variant < LLHLSLShader::NUM_GLTF_VARIANTS);
	mGLTFVariants[variant].bind();
}

void LLHLSLShader::bind(bool rigged)
{
	if (rigged)
	{
		llassert_always(mRiggedVariant);
		mRiggedVariant->bind();
	}
	else
	{
		bind();
	}
}

void LLHLSLShader::unbind(void)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	// S24 (DX_RENDER, 2026-07-25): must flush() BEFORE clearing
	// sCurBoundShaderPtr - this branch used to null the pointer directly
	// without flushing first. LLRender::flush() reads sCurBoundShaderPtr to
	// determine which shader/attribute-mask a pending immediate-mode batch
	// (gDX.begin()/vertex.../end(), not yet auto-flushed - batches under
	// ~2048 verts don't auto-flush) belongs to; finding it null, it
	// silently DROPS the batch (logged once, rate-limited to 5 total
	// warnings for the whole process). Any geometry still queued at the
	// moment ANY LLHLSLShader::unbind() call runs - not just
	// LLViewerWindow::draw()'s own gUIProgram.unbind() at the very end of
	// the whole UI tree, where this was found while tracing that exact
	// call - was silently vanishing under DX_RENDER. Same missing-flush-
	// before-state-switch bug class as this session's earlier
	// beginTextRender()/gl_draw_scaled_rotated_image()/LLTexUnit SRV-switch
	// fixes, just at the shader-unbind chokepoint instead - and since
	// unbind() runs at the end of essentially every shader-bound render
	// pass in the whole pipeline, this was a systemic, not UI-specific, gap.
	gDX.flush();
	// S24 (2026-08-16): also flush gDXUIBatch's separate pending queue -
	// same missing-flush hazard as above, see DXUIBatch.h's top comment.
	gDXUIBatch.flushPending();
	LLVertexBuffer::unbind();
	gDXDevice.getContext()->VSSetShader(nullptr, nullptr, 0);
	gDXDevice.getContext()->PSSetShader(nullptr, nullptr, 0);
	sCurBoundShaderPtr = nullptr;
}

S32 LLHLSLShader::bindTexture(const std::string& uniform, LLTexture* texture, LLTexUnit::eTextureType mode)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	S32 channel = 0;
	channel = getUniformLocation(uniform);

	return bindTexture(channel, texture, mode);
}

S32 LLHLSLShader::bindTexture(S32 uniform, LLTexture* texture, LLTexUnit::eTextureType mode)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	// S24 (2026-08-03): mTexture[] is now populated (see createShaderDX()'s
	// post-compile reflection pass), using bindFast() (already DX-safe) as
	// the real bind call. Was a
	// hardcoded no-op before - the actual root cause of PBR materials
	// showing an unrelated leftover-bound texture depending on draw order.
	if (uniform < 0 || uniform >= (S32)mTexture.size())
	{
		return -1;
	}

	uniform = mTexture[uniform];

	if (uniform > -1)
	{
		gDX.getTexUnit(uniform)->bindFast(texture);
	}

	return uniform;
}

S32 LLHLSLShader::bindTexture(S32 uniform, LLRenderTarget* texture, bool depth, LLTexUnit::eTextureFilterOptions mode, U32 index)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	// S24 (2026-08-04): was a hardcoded no-op (same shape as the old
	// bindTexture(S32, LLTexture*, ...)/enableTexture() gap fixed in stage
	// 8 part 4) - mTexture[] reflection (via getTextureChannel(), already
	// DX-safe) + LLTexUnit::bind(LLRenderTarget*, bool) (already DX-safe,
	// task #62) are exactly what's needed, no new machinery. Found while
	// converting LLDrawPoolWater (task #109) - its WATER_SCREENTEX/
	// WATER_EXCLUSIONTEX binds both go through this exact overload, so
	// water's screen-reflection and exclusion-mask textures would
	// otherwise silently never bind. `index` (multi-attachment selection)
	// and `mode` (filter option) aren't threaded through
	// LLTexUnit::bind(LLRenderTarget*, bool) yet - it always binds
	// attachment 0 with a fixed CLAMP+BILINEAR sampler (matches the
	// screen-space-sampling convention already used everywhere else this
	// session) - irrelevant for water's two single-attachment targets,
	// a documented simplification if a future multi-attachment caller
	// ever needs it.
	S32 channel = getTextureChannel(uniform);
	if (channel > -1)
	{
		gDX.getTexUnit(channel)->bind(texture, depth);
	}
	return channel;
}

S32 LLHLSLShader::bindTexture(const std::string& uniform, LLRenderTarget* texture, bool depth, LLTexUnit::eTextureFilterOptions mode)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	S32 channel = 0;
	channel = getUniformLocation(uniform);

	return bindTexture(channel, texture, depth, mode);
}

S32 LLHLSLShader::unbindTexture(const std::string& uniform, LLTexUnit::eTextureType mode)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	S32 channel = 0;
	channel = getUniformLocation(uniform);

	return unbindTexture(channel);
}

S32 LLHLSLShader::unbindTexture(S32 uniform, LLTexUnit::eTextureType mode)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	// S24 (2026-08-03): mTexture[] is now populated - see bindTexture(S32,
	// LLTexture*, ...)'s comment.
	if (uniform < 0 || uniform >= (S32)mTexture.size())
	{
		return -1;
	}

	uniform = mTexture[uniform];

	if (uniform > -1)
	{
		gDX.getTexUnit(uniform)->unbindFast(mode);
	}

	return uniform;
}

S32 LLHLSLShader::getTextureChannel(S32 uniform) const
{
	// S24 (2026-08-03): mTexture[] is now populated - see bindTexture(S32,
	// LLTexture*, ...)'s comment. Bounds-checked since this is a const
	// accessor with no established assert-on-misuse convention of its own.
	if (uniform < 0 || uniform >= (S32)mTexture.size())
	{
		return -1;
	}
	return mTexture[uniform];
}

S32 LLHLSLShader::enableTexture(S32 uniform, LLTexUnit::eTextureType mode)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	// S24 (2026-08-03): mTexture[] is now populated - see bindTexture(S32,
	// LLTexture*, ...)'s comment. activate()/enable() are already
	// documented no-ops under DX_RENDER (texture binding goes through
	// bindFast() instead), so this just returns the resolved channel index.
	if (uniform < 0 || uniform >= (S32)mTexture.size())
	{
		return -1;
	}

	S32 index = mTexture[uniform];
	if (index != -1)
	{
		gDX.getTexUnit(index)->activate();
		gDX.getTexUnit(index)->enable(mode);
	}
	return index;
}

S32 LLHLSLShader::disableTexture(S32 uniform, LLTexUnit::eTextureType mode)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	// S24 (2026-08-03): mTexture[] is now populated - see bindTexture(S32,
	// LLTexture*, ...)'s comment. mCurrTexType stays TT_NONE forever under
	// DX_RENDER (enable()'s DX_RENDER branch never sets it - see its own
	// comment), so the curr_type-gated disable()/debug-validation block
	// below is simply never reached here - harmless, just resolves and
	// returns the channel index like every other fixed accessor above.
	if (uniform < 0 || uniform >= (S32)mTexture.size())
	{
		return -1;
	}

	S32 index = mTexture[uniform];
	if (index < 0)
	{
		return index;
	}

	LLTexUnit* tex_unit = gDX.getTexUnit(index);
	if (!tex_unit)
	{
		return index;
	}

	// S24 (2026-08-10, task #158): mCurrTexType never leaves TT_NONE under
	// DX_RENDER (enable()'s DX_RENDER branch never sets it - see its own
	// comment), so this gate always evaluated false, meaning
	// tex_unit->disable() was never actually called from here - even though
	// LLTexUnit::disable() itself was already fixed (see its own comment) to
	// bypass this exact stale-bookkeeping trap by unconditionally calling
	// unbind(). This function had its own unfixed copy of the same gate one
	// level up, silently blocking every call. Confirmed root cause of the
	// "Forcing PS shader resource slot N to NULL" D3D11 debug-layer warnings
	// (thousands/session, e.g. "Deferred Soften Shader" slot 7/lightMap) -
	// every disableTexture() call was leaving its SRV bound at the GPU level
	// indefinitely, relying entirely on D3D11's automatic OM/SRV hazard
	// resolution (and its warning spam) to ever clear it, one frame late.
	tex_unit->disable();

	return index;
}

void LLHLSLShader::uniform1i(U32 index, GLint x)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
	llassert(sCurBoundShaderPtr == this);

	// S24 (2026-08-05): mirrors uniform4f(U32,...)'s DX_RENDER branch - real
	// caller found this session: LLShaderUniforms::apply()'s mIntegers loop
	// (LLEnvironment::updateShaderUniforms(), now wired into
	// LLHLSLShader::bind() for DX_RENDER too - see that function's comment)
	// pushes int uniforms (e.g. classic_mode) through exactly this
	// overload. Previously a silent no-op (mProgramObject always 0 here).
	// Reinterprets the int's own bits through a float* rather than
	// converting the VALUE to float - setUniformFloatArray() is a raw
	// memcpy into the constant buffer, and HLSL's cbuffer packs `int` as a
	// plain 4-byte int32; writing the float-converted bit pattern instead
	// (e.g. int 1 -> float 1.0f -> bits 0x3F800000) would corrupt any
	// shader reading this slot as int.
	if (index < LLShaderMgr::instance()->mReservedUniforms.size())
	{
		const std::string& name = LLShaderMgr::instance()->mReservedUniforms[index];
		const float* v = reinterpret_cast<const float*>(&x);
		// S24 (2026-08-09, task #149): was "try VS, only try PS if VS
		// failed" - see uniform1f(U32,...)'s comment just below for the
		// real bug this caused (a uniform declared in BOTH stages, e.g.
		// cloud_scale, only ever reached whichever stage was tried first).
		// Both calls are safe unconditionally - setUniformFloatArray()
		// just returns false with zero side effects when the named
		// constant isn't declared in that stage (DXShader.cpp).
		mDXVertexShader.setUniformFloatArray(name, v, 1);
		mDXPixelShader.setUniformFloatArray(name, v, 1);
	}
}

void LLHLSLShader::uniform1f(U32 index, F32 x)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
	llassert(sCurBoundShaderPtr == this);

	// S24 (2026-08-05): mirrors uniform4f(U32,...)'s DX_RENDER branch - real
	// caller found this session: LLShaderUniforms::apply()'s mFloats loop
	// (density_multiplier/haze_density/cloud_shadow and other scalar
	// windlight uniforms) pushes through exactly this overload. Previously
	// a silent no-op (mProgramObject always 0 here) - confirmed via a
	// direct C++ readback showing atten reading exactly (1,1,1) (density_
	// multiplier's zero default collapsing exp(-0*x) to 1) at every pixel.
	//
	// S24 (2026-08-09, task #149): real root cause of "no clouds at all" -
	// cloud_scale is declared in BOTH cloudsV.hlsl (UV scaling) and
	// cloudsF.hlsl (the "if (cloud_scale < 0.001) discard;" gate). The
	// exclusive "try VS, only try PS if VS failed" pattern below meant
	// cloud_scale only ever reached the vertex shader (tried first,
	// always succeeds since it's declared there) - the pixel shader's own
	// separate copy stayed at its zero-initialized constant-buffer
	// default, so the discard fired on literally every pixel, every
	// frame. Fixed by setting BOTH stages unconditionally - safe, since
	// setUniformFloatArray() just returns false with no side effects when
	// a stage doesn't declare the named constant (DXShader.cpp). This same
	// exclusive pattern existed in 9 other uniform-setter overloads in
	// this file (uniform1i/2f/3f/4f, the *fv(count) array variants,
	// uniformMatrix3fv/4fv) - all fixed the same way alongside this one,
	// since any of them could be silently dropping a legitimately
	// dual-stage uniform the same way.
	if (index < LLShaderMgr::instance()->mReservedUniforms.size())
	{
		const std::string& name = LLShaderMgr::instance()->mReservedUniforms[index];
		const float v[1] = { x };
		mDXVertexShader.setUniformFloatArray(name, v, 1);
		mDXPixelShader.setUniformFloatArray(name, v, 1);
	}
}

void LLHLSLShader::fastUniform1f(U32 index, F32 x)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
	llassert(sCurBoundShaderPtr == this);
	llassert(mProgramObject);
	llassert(mUniform.size() <= index);
	llassert(mUniform[index] >= 0);
	glUniform1f(mUniform[index], x);
}

void LLHLSLShader::uniform2f(U32 index, F32 x, F32 y)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
	llassert(sCurBoundShaderPtr == this);

	// S24 (2026-08-09, task #138): mirrors uniform1f/uniform4f(U32,...)'s
	// DX_RENDER branches - was a silent no-op here (mProgramObject always 0
	// under DX_RENDER, so the old unconditional `if (mProgramObject)` guard
	// skipped the whole body), same bug class, just never audited for this
	// specific overload until glow's GLOW_DELTA (blur-kernel offset) and
	// DEFERRED_SCREEN_RES calls were traced through it. mReservedUniforms is
	// the same plain backend-agnostic name table used by every other fixed
	// overload - index->name resolution works identically here.
	if (index < LLShaderMgr::instance()->mReservedUniforms.size())
	{
		const std::string& name = LLShaderMgr::instance()->mReservedUniforms[index];
		const float v[2] = { x, y };
		// S24 (2026-08-09, task #149): both stages unconditionally now -
		// see uniform1f(U32,...)'s comment for the real bug this exclusive
		// "try VS, else PS" pattern caused (cloud_scale, declared in both
		// stages, silently never reached the pixel shader).
		mDXVertexShader.setUniformFloatArray(name, v, 2);
		mDXPixelShader.setUniformFloatArray(name, v, 2);
	}
}

void LLHLSLShader::uniform3f(U32 index, F32 x, F32 y, F32 z)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
	llassert(sCurBoundShaderPtr == this);

	// S24 (2026-08-09, task #138): see uniform2f(U32,...)'s comment just
	// above - same fix, same reasoning. Real callers found this session:
	// glowExtractF.hlsl's lumWeights/warmthWeights (GLOW_LUM_WEIGHTS/
	// GLOW_WARMTH_WEIGHTS).
	if (index < LLShaderMgr::instance()->mReservedUniforms.size())
	{
		const std::string& name = LLShaderMgr::instance()->mReservedUniforms[index];
		const float v[3] = { x, y, z };
		// S24 (2026-08-09, task #149): both stages unconditionally now -
		// see uniform1f(U32,...)'s comment.
		mDXVertexShader.setUniformFloatArray(name, v, 3);
		mDXPixelShader.setUniformFloatArray(name, v, 3);
	}
}

void LLHLSLShader::uniform4f(U32 index, F32 x, F32 y, F32 z, F32 w)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
	llassert(sCurBoundShaderPtr == this);

	// S24 (DXUIBatch plan, phase 3): mirrors uniform4fv(U32,...)'s DX_RENDER
	// branch above - same reasoning (mReservedUniforms is a plain
	// backend-agnostic name table, works identically here). Real caller
	// found this session: LLRender::diffuseColor4ub()/diffuseColor4ubv()
	// push LLShaderMgr::DIFFUSE_COLOR through this exact function whenever
	// the currently-bound shader has no per-vertex COLOR0 (gSolidColorProgram/
	// solidcolorF.hlsl's "uniform vec4 color" - the mechanism solid-color
	// 2D UI fills use instead of vertex color). Previously silently did
	// nothing under DX_RENDER (mProgramObject is always 0 here, so the old
	// unconditional `if (mProgramObject)` guard skipped the whole body) -
	// solid-color fills would have rendered with whatever the constant
	// buffer's default/uninitialized contents were, not a chosen color.
	if (index < LLShaderMgr::instance()->mReservedUniforms.size())
	{
		const std::string& name = LLShaderMgr::instance()->mReservedUniforms[index];
		const F32 v[4] = { x, y, z, w };
		// S24 (2026-08-09, task #149): both stages unconditionally now -
		// see uniform1f(U32,...)'s comment.
		mDXVertexShader.setUniformFloatArray(name, v, 4);
		mDXPixelShader.setUniformFloatArray(name, v, 4);
	}
}

void LLHLSLShader::uniform1iv(U32 index, U32 count, const GLint* v)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
	llassert(sCurBoundShaderPtr == this);

	if (mProgramObject)
	{
		if (mUniform.size() <= index)
		{
			LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << index << LL_ENDL;
			llassert(false);
			return;
		}

		if (mUniform[index] >= 0)
		{
			const auto& iter = mValue.find(mUniform[index]);
			LLVector4 vec((F32)v[0], 0.f, 0.f, 0.f);
			if (iter == mValue.end() || shouldChange(iter->second, vec) || count != 1)
			{
				glUniform1iv(mUniform[index], count, v);
				mValue[mUniform[index]] = vec;
			}
		}
	}
}

void LLHLSLShader::uniform4iv(U32 index, U32 count, const GLint* v)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
	llassert(sCurBoundShaderPtr == this);

	if (mProgramObject)
	{
		if (mUniform.size() <= index)
		{
			LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << index << LL_ENDL;
			llassert(false);
			return;
		}

		if (mUniform[index] >= 0)
		{
			const auto& iter = mValue.find(mUniform[index]);
			LLVector4 vec((F32)v[0], (F32)v[1], (F32)v[2], (F32)v[3]);
			if (iter == mValue.end() || shouldChange(iter->second, vec) || count != 1)
			{
				glUniform1iv(mUniform[index], count, v);
				mValue[mUniform[index]] = vec;
			}
		}
	}
}

void LLHLSLShader::uniform1fv(U32 index, U32 count, const F32* v)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
	llassert(sCurBoundShaderPtr == this);

	// S24 (2026-08-03): had NO DX_RENDER branch at all (unlike uniform4f()/
	// uniform4fv(), fixed earlier) - a complete silent no-op, since the GL
	// body below is gated entirely on `if (mProgramObject)`, always 0 under
	// DX_RENDER. Uses setUniformPaddedArray() (component_count=1) rather
	// than a raw memcpy - HLSL pads every array element, even a plain
	// "float x[N]", to its own 16-byte slot.
	if (index < LLShaderMgr::instance()->mReservedUniforms.size())
	{
		const std::string& name = LLShaderMgr::instance()->mReservedUniforms[index];
		// S24 (2026-08-09, task #149): both stages unconditionally now -
		// see uniform1f(U32,...)'s comment.
		mDXVertexShader.setUniformPaddedArray(name, v, 1, count);
		mDXPixelShader.setUniformPaddedArray(name, v, 1, count);
	}
}

void LLHLSLShader::uniform2fv(U32 index, U32 count, const F32* v)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
	llassert(sCurBoundShaderPtr == this);

	// Mirrors uniform1fv()'s DX_RENDER branch above (component_count=2).
	if (index < LLShaderMgr::instance()->mReservedUniforms.size())
	{
		const std::string& name = LLShaderMgr::instance()->mReservedUniforms[index];
		// S24 (2026-08-09, task #149): both stages unconditionally now -
		// see uniform1f(U32,...)'s comment.
		mDXVertexShader.setUniformPaddedArray(name, v, 2, count);
		mDXPixelShader.setUniformPaddedArray(name, v, 2, count);
	}
}

void LLHLSLShader::uniform3fv(U32 index, U32 count, const F32* v)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
	llassert(sCurBoundShaderPtr == this);

	// Mirrors uniform1fv()'s DX_RENDER branch above (component_count=3) -
	// real confirmed caller: LLDrawPoolTerrain's TERRAIN_EMISSIVE_COLORS
	// upload ("uniform float3 emissiveColors[4]" in pbrterrainF.hlsl),
	// which was silently never uploading under DX_RENDER before this fix.
	if (index < LLShaderMgr::instance()->mReservedUniforms.size())
	{
		const std::string& name = LLShaderMgr::instance()->mReservedUniforms[index];
		// S24 (2026-08-09, task #149): both stages unconditionally now -
		// see uniform1f(U32,...)'s comment.
		mDXVertexShader.setUniformPaddedArray(name, v, 3, count);
		mDXPixelShader.setUniformPaddedArray(name, v, 3, count);
	}
}

void LLHLSLShader::uniform4fv(U32 index, U32 count, const F32* v)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
	llassert(sCurBoundShaderPtr == this);

	// Only real caller today: LLViewerJointMesh::uploadJointMatrices()'s
	// hardware-skinning path, pushing the avatar body's 45-float4 joint
	// palette ("matrixPalette", AVATAR_MATRIX) - a plain top-level HLSL
	// uniform, unlike per-vertex inputs (see injectSkinningInputs()), so no
	// VSInput-injection trick is needed here, just the reflected $Globals
	// constant this shader already declares. mReservedUniforms is a plain
	// backend-agnostic name table (populated once at startup, independent of
	// mProgramObject/GL), so index->name resolution works identically under
	// DX_RENDER.
	if (index < LLShaderMgr::instance()->mReservedUniforms.size())
	{
		const std::string& name = LLShaderMgr::instance()->mReservedUniforms[index];
		// S24 (2026-08-09, task #149): both stages unconditionally now -
		// see uniform1f(U32,...)'s comment.
		mDXVertexShader.setUniformFloatArray(name, v, (size_t)count * 4);
		mDXPixelShader.setUniformFloatArray(name, v, (size_t)count * 4);
	}
}

void LLHLSLShader::uniform4uiv(U32 index, U32 count, const GLuint* v)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
	llassert(sCurBoundShaderPtr == this);

	if (mProgramObject)
	{
		if (mUniform.size() <= index)
		{
			LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << index << LL_ENDL;
			llassert(false);
			return;
		}

		if (mUniform[index] >= 0)
		{
			const auto& iter = mValue.find(mUniform[index]);
			LLVector4 vec((F32)v[0], (F32)v[1], (F32)v[2], (F32)v[3]);
			if (iter == mValue.end() || shouldChange(iter->second, vec) || count != 1)
			{
				LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
				glUniform4uiv(mUniform[index], count, v);
				mValue[mUniform[index]] = vec;
			}
		}
	}
}

void LLHLSLShader::uniformMatrix2fv(U32 index, U32 count, bool transpose, const F32* v)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
	llassert(sCurBoundShaderPtr == this);

	if (mProgramObject)
	{
		if (mUniform.size() <= index)
		{
			LL_WARNS_ONCE("Shader") << "Uniform index out of bounds. Size: " << (S32)mUniform.size() << " index: " << index << LL_ENDL;
			llassert(false);
			return;
		}

		if (mUniform[index] >= 0)
		{
			glUniformMatrix2fv(mUniform[index], count, transpose, v);
		}
	}
}

void LLHLSLShader::uniformMatrix3fv(U32 index, U32 count, bool transpose, const F32* v)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
	llassert(sCurBoundShaderPtr == this);

	// S24 (2026-08-09, task #146): was a complete no-op under DX_RENDER
	// (mProgramObject is always 0 here) - same silent-no-op bug class
	// already fixed for uniform1f/2f/3f/4f earlier this session, just
	// never hit for a matrix uniform until now. Confirmed root cause of a
	// real, reported bug: DEFERRED_ENV_MAT (the 3x3 rotation that orients
	// reflection-probe lookups into environment-map space,
	// pipeline.cpp's bindDeferredShader()/setupSpotLight()) never reached
	// the GPU - env_vec = mul(env_mat, reflect(...)) read a stale/zero
	// matrix, producing a reflection that "rolled like it was rendered
	// onto a sphere" and only coincidentally lined up at specific camera
	// angles. Worst on water's mirror-like surface, but this is a shared
	// chokepoint - affects every reflection-probe consumer (bump/shiny
	// materials, sky).
	//
	// S24 (2026-08-10, task #146 follow-up): count > 1 - no known 3x3 array
	// caller today (unlike uniformMatrix4fv's DEFERRED_SHADOW_MATRIX), but
	// this is the same bug class/same fix shape - handled for real
	// completeness rather than leaving a matching silent-no-op trap for
	// whatever future caller hits it. See uniformMatrix4fv(U32,...)'s own
	// count>1 branch for the full root-cause writeup (shadow cascades).
	if (count == 1 && index < LLShaderMgr::instance()->mReservedUniforms.size())
	{
		const std::string& name = LLShaderMgr::instance()->mReservedUniforms[index];
		float m[9];
		if (transpose)
		{
			// GL's transpose=true means v is supplied row-major (GL
			// transposes it into column-major internally before upload).
			// DXShader::setUniformMatrix3() expects data already in
			// column-major order with no further conversion - do the same
			// row-major -> column-major transpose here instead.
			for (int r = 0; r < 3; ++r)
			{
				for (int c = 0; c < 3; ++c)
				{
					m[c * 3 + r] = v[r * 3 + c];
				}
			}
		}
		else
		{
			memcpy(m, v, sizeof(m));
		}

		// S24 (2026-08-09, task #149): both stages unconditionally now -
		// see uniform1f(U32,...)'s comment for the real bug this exclusive
		// "try VS, else PS" pattern caused elsewhere (cloud_scale).
		mDXVertexShader.setUniformMatrix3(name, m);
		mDXPixelShader.setUniformMatrix3(name, m);
	}
	else if (count > 1 && index < LLShaderMgr::instance()->mReservedUniforms.size())
	{
		const std::string& name = LLShaderMgr::instance()->mReservedUniforms[index];
		std::vector<float> m(static_cast<size_t>(count) * 9);
		if (transpose)
		{
			for (U32 i = 0; i < count; ++i)
			{
				const F32* src = v + (size_t)i * 9;
				float* dst = m.data() + (size_t)i * 9;
				for (int r = 0; r < 3; ++r)
				{
					for (int c = 0; c < 3; ++c)
					{
						dst[c * 3 + r] = src[r * 3 + c];
					}
				}
			}
		}
		else
		{
			memcpy(m.data(), v, m.size() * sizeof(float));
		}

		mDXVertexShader.setUniformFloatArray(name, m.data(), m.size());
		mDXPixelShader.setUniformFloatArray(name, m.data(), m.size());
	}
}

void LLHLSLShader::uniformMatrix3x4fv(U32 index, U32 count, bool transpose, const F32* v)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
	llassert(sCurBoundShaderPtr == this);

	// S24 (2026-08-09, task #157): was a complete no-op - the ONLY caller
	// is LLRenderPass::uploadMatrixPalette() (lldrawpool.cpp), the rigged-
	// mesh skinning matrix palette ("matrixPalette", AVATAR_MATRIX,
	// objectSkinV.hlsl) used by every mesh attachment - mesh bodies,
	// clothing, hair, everything modern SL avatars actually wear (the
	// classic system-avatar-body path is a DIFFERENT uniform/shader,
	// avatarSkinV.hlsl's flat float4[45], already fixed via uniform4fv).
	// With matrixPalette never uploaded, every skinned vertex multiplied
	// by a zero-initialized joint matrix - collapsing every rigged mesh to
	// a single degenerate point, i.e. invisible. objectSkinV.hlsl's
	// matrixPalette was changed to `row_major float3x4` (see that file's
	// own comment) specifically so this can be a straight contiguous copy
	// with no repacking: real callers only ever pass transpose=false, and
	// GL's own default (column-major) mat3x4 layout is byte-identical to
	// HLSL's row_major float3x4 layout here (both end up storing the
	// affine matrix as 3 registers of 4 floats - one per row - despite
	// GLSL's mat3x4 and HLSL's float3x4 naming the transposed shape of
	// each other). transpose=true is not handled - no real caller uses it.
	if (index < LLShaderMgr::instance()->mReservedUniforms.size())
	{
		if (!transpose)
		{
			const std::string& name = LLShaderMgr::instance()->mReservedUniforms[index];
			mDXVertexShader.setUniformFloatArray(name, v, (size_t)count * 12);
			mDXPixelShader.setUniformFloatArray(name, v, (size_t)count * 12);
		}
	}
}

void LLHLSLShader::uniformMatrix4fv(U32 index, U32 count, bool transpose, const F32* v)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
	llassert(sCurBoundShaderPtr == this);

	// S24 (2026-08-09, task #146): mirrors uniformMatrix3fv(U32,...)'s
	// DX_RENDER fix (see its own comment) - was a complete no-op here too.
	// Real callers found via this index-based overload: MODELVIEW_MATRIX
	// (reflection-probe capture view), MODELVIEW_DELTA_MATRIX/
	// INVERSE_MODELVIEW_DELTA_MATRIX (motion vectors), DEFERRED_NORM_MATRIX,
	// PROJECTOR_MATRIX (setupSpotLight() - task #144's spotlight work
	// depends on this reaching the GPU).
	//
	// S24 (2026-08-10, task #146 follow-up): count > 1 - real caller found
	// once the shadow pass actually started running under DX_RENDER this
	// session: pipeline.cpp's bindDeferredShader() uploads
	// DEFERRED_SHADOW_MATRIX (shadowUtil.hlsl's shadow_matrix[6], 4 sun
	// cascades + 2 spot lights) via a single count=6 call. Silently no-op'd
	// before this fix - shadow_matrix[] never reached the GPU at all, so
	// sampleDirectionalShadow()/sampleSpotShadow() sampled whatever stale
	// data happened to already be in that constant-buffer slot: a real,
	// confirmed root cause (not guessed - proven via a live readback showing
	// spatially-incoherent-but-numerically-real shadow values, then traced
	// here) of shadows appearing as a "grainy," camera-relative artifact
	// with no real correlation to current geometry or sun angle. Single
	// matrix (count==1) keeps the original zero-allocation stack-array path
	// untouched below; count>1 builds a flat count*16 buffer (same per-
	// matrix transpose conversion as the single-matrix case, applied once
	// per matrix - transpose is a per-matrix operation, not a whole-array
	// one) and uploads it via setUniformFloatArray(), mirroring
	// uniformMatrix3x4fv(U32,...)'s (task #157) already-working array-
	// upload shape.
	if (count == 1 && index < LLShaderMgr::instance()->mReservedUniforms.size())
	{
		const std::string& name = LLShaderMgr::instance()->mReservedUniforms[index];
		float m[16];
		if (transpose)
		{
			// See uniformMatrix3fv(U32,...)'s comment - same row-major ->
			// column-major conversion, just 4x4 instead of 3x3.
			for (int r = 0; r < 4; ++r)
			{
				for (int c = 0; c < 4; ++c)
				{
					m[c * 4 + r] = v[r * 4 + c];
				}
			}
		}
		else
		{
			memcpy(m, v, sizeof(m));
		}

		// S24 (2026-08-09, task #149): both stages unconditionally now -
		// see uniformMatrix3fv(U32,...)'s comment just above.
		mDXVertexShader.setUniformMatrix4(name, m);
		mDXPixelShader.setUniformMatrix4(name, m);
	}
	else if (count > 1 && index < LLShaderMgr::instance()->mReservedUniforms.size())
	{
		const std::string& name = LLShaderMgr::instance()->mReservedUniforms[index];
		std::vector<float> m(static_cast<size_t>(count) * 16);
		if (transpose)
		{
			for (U32 i = 0; i < count; ++i)
			{
				const F32* src = v + (size_t)i * 16;
				float* dst = m.data() + (size_t)i * 16;
				for (int r = 0; r < 4; ++r)
				{
					for (int c = 0; c < 4; ++c)
					{
						dst[c * 4 + r] = src[r * 4 + c];
					}
				}
			}
		}
		else
		{
			memcpy(m.data(), v, m.size() * sizeof(float));
		}

		mDXVertexShader.setUniformFloatArray(name, m.data(), m.size());
		mDXPixelShader.setUniformFloatArray(name, m.data(), m.size());
	}
}

GLint LLHLSLShader::getUniformLocation(const LLStaticHashedString& uniform)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	GLint ret = -1;
	if (mProgramObject)
	{
		LLStaticStringTable<GLint>::iterator iter = mUniformMap.find(uniform);
		if (iter != mUniformMap.end())
		{
			if (gDebugGL)
			{
				stop_glerror();
				if (iter->second != glGetUniformLocation(mProgramObject, uniform.String().c_str()))
				{
					LL_ERRS() << "Uniform does not match." << LL_ENDL;
				}
				stop_glerror();
			}
			ret = iter->second;
		}
	}

	return ret;
}

GLint LLHLSLShader::getUniformLocation(U32 index)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	GLint ret = -1;
	if (mProgramObject)
	{
		if (index >= mUniform.size())
		{
			LL_WARNS_ONCE("Shader") << "Uniform index " << index << " out of bounds " << (S32)mUniform.size() << LL_ENDL;
			return ret;
		}
		return mUniform[index];
	}

	return ret;
}

GLint LLHLSLShader::getAttribLocation(U32 attrib)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	if (attrib < mAttribute.size())
	{
		return mAttribute[attrib];
	}
	else
	{
		return -1;
	}
}

void LLHLSLShader::uniform1i(const LLStaticHashedString& uniform, GLint v)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	// S24 (2026-08-09, task #147b/#153): this whole LLStaticHashedString-
	// keyed overload family (14 total, see task #153) had zero DX_RENDER
	// handling - getUniformLocation(LLStaticHashedString) always returns
	// -1 (mProgramObject==0), so this was a SAFE no-op, not a crash, but
	// silently dropped every value. Found chasing #147b's setUniforms()
	// (probes_enabled/probe_intensity/etc never reaching the shader would
	// have made reflections read as disabled/zero). Both stages
	// unconditionally, not exclusive if/else - see uniform1f(U32,...)'s
	// own comment (llhlslshader.cpp) for why. Reinterprets the int's bits
	// through a float* rather than converting the value, matching
	// uniform1i(U32,...)'s same reasoning (HLSL cbuffers pack int as raw
	// int32, not GLSL-style implicit float conversion).
	const std::string& name = uniform.String();
	const float* fv = reinterpret_cast<const float*>(&v);
	mDXVertexShader.setUniformFloatArray(name, fv, 1);
	mDXPixelShader.setUniformFloatArray(name, fv, 1);
}

void LLHLSLShader::uniform1iv(const LLStaticHashedString& uniform, U32 count, const GLint* v)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	// S24 (2026-08-09, task #153): see uniform1i(LLStaticHashedString,...)'s
	// comment - same gap, same fix. Int/float share the same 4-byte width
	// in the constant buffer, so a raw bit-reinterpretation (not a value
	// conversion) is correct here, matching uniform1i's own reasoning.
	// setUniformPaddedArray (not setUniformFloatArray) since HLSL pads
	// every array element ("int x[N]") to its own 16-byte slot, same as
	// the float array overloads.
	const std::string& name = uniform.String();
	const float* fv = reinterpret_cast<const float*>(v);
	mDXVertexShader.setUniformPaddedArray(name, fv, 1, count);
	mDXPixelShader.setUniformPaddedArray(name, fv, 1, count);
}

void LLHLSLShader::uniform4iv(const LLStaticHashedString& uniform, U32 count, const GLint* v)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	// S24 (2026-08-09, task #153): see uniform1iv(LLStaticHashedString,...)'s
	// comment just above - same reasoning, component_count=4 (each int4
	// element is already a natural 16-byte slot, no extra padding needed
	// beyond what setUniformPaddedArray already does for that case).
	const std::string& name = uniform.String();
	const float* fv = reinterpret_cast<const float*>(v);
	mDXVertexShader.setUniformPaddedArray(name, fv, 4, count);
	mDXPixelShader.setUniformPaddedArray(name, fv, 4, count);
}

void LLHLSLShader::uniform2i(const LLStaticHashedString& uniform, GLint i, GLint j)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	// S24 (2026-08-09, task #153): see uniform1iv(LLStaticHashedString,...)'s
	// comment above - same bit-reinterpretation reasoning, non-array case.
	const std::string& name = uniform.String();
	const GLint iv[2] = { i, j };
	const float* fv = reinterpret_cast<const float*>(iv);
	mDXVertexShader.setUniformFloatArray(name, fv, 2);
	mDXPixelShader.setUniformFloatArray(name, fv, 2);
}

void LLHLSLShader::uniform1f(const LLStaticHashedString& uniform, F32 v)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	// S24 (2026-08-09, task #147b/#153): see uniform1i(LLStaticHashedString,...)'s
	// comment just above - same gap, same fix.
	const std::string& name = uniform.String();
	const float v_arr[1] = { v };
	mDXVertexShader.setUniformFloatArray(name, v_arr, 1);
	mDXPixelShader.setUniformFloatArray(name, v_arr, 1);
}

void LLHLSLShader::uniform2f(const LLStaticHashedString& uniform, F32 x, F32 y)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	// S24 (2026-08-09, task #153): see uniform1i(LLStaticHashedString,...)'s
	// comment - same gap, same fix.
	const std::string& name = uniform.String();
	const float v_arr[2] = { x, y };
	mDXVertexShader.setUniformFloatArray(name, v_arr, 2);
	mDXPixelShader.setUniformFloatArray(name, v_arr, 2);
}

void LLHLSLShader::uniform3f(const LLStaticHashedString& uniform, F32 x, F32 y, F32 z)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	// S24 (2026-08-09, task #153): see uniform1i(LLStaticHashedString,...)'s
	// comment - same gap, same fix.
	const std::string& name = uniform.String();
	const float v_arr[3] = { x, y, z };
	mDXVertexShader.setUniformFloatArray(name, v_arr, 3);
	mDXPixelShader.setUniformFloatArray(name, v_arr, 3);
}

void LLHLSLShader::uniform4f(const LLStaticHashedString& uniform, F32 x, F32 y, F32 z, F32 w)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	// S24 (2026-08-09, task #153): see uniform1i(LLStaticHashedString,...)'s
	// comment - same gap, same fix.
	const std::string& name = uniform.String();
	const float v_arr[4] = { x, y, z, w };
	mDXVertexShader.setUniformFloatArray(name, v_arr, 4);
	mDXPixelShader.setUniformFloatArray(name, v_arr, 4);
}

void LLHLSLShader::uniform1fv(const LLStaticHashedString& uniform, U32 count, const F32* v)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	// S24 (2026-08-09, task #153): see uniform1fv(U32,...)'s comment
	// (task #107, same file) - setUniformPaddedArray since HLSL pads every
	// array element to its own 16-byte slot, even a plain "float x[N]".
	const std::string& name = uniform.String();
	mDXVertexShader.setUniformPaddedArray(name, v, 1, count);
	mDXPixelShader.setUniformPaddedArray(name, v, 1, count);
}

void LLHLSLShader::uniform2fv(const LLStaticHashedString& uniform, U32 count, const F32* v)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	// S24 (2026-08-09, task #153): see uniform1fv(LLStaticHashedString,...)'s
	// comment just above - same reasoning, component_count=2.
	const std::string& name = uniform.String();
	mDXVertexShader.setUniformPaddedArray(name, v, 2, count);
	mDXPixelShader.setUniformPaddedArray(name, v, 2, count);
}

void LLHLSLShader::uniform3fv(const LLStaticHashedString& uniform, U32 count, const F32* v)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	// S24 (2026-08-09, task #153): see uniform1fv(LLStaticHashedString,...)'s
	// comment above - same reasoning, component_count=3. Real caller:
	// LLShaderUniforms::apply()'s mVector3s loop.
	const std::string& name = uniform.String();
	mDXVertexShader.setUniformPaddedArray(name, v, 3, count);
	mDXPixelShader.setUniformPaddedArray(name, v, 3, count);
}

void LLHLSLShader::uniform4fv(const LLStaticHashedString& uniform, U32 count, const F32* v)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	// S24 (2026-08-09, task #153): see uniform1fv(LLStaticHashedString,...)'s
	// comment above - component_count=4 (each vec4 element is already a
	// natural 16-byte slot). Real caller: LLShaderUniforms::apply()'s
	// mVectors loop.
	const std::string& name = uniform.String();
	mDXVertexShader.setUniformPaddedArray(name, v, 4, count);
	mDXPixelShader.setUniformPaddedArray(name, v, 4, count);
}

void LLHLSLShader::uniform4uiv(const LLStaticHashedString& uniform, U32 count, const GLuint* v)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	// S24 (2026-08-09, task #153): see uniform1iv(LLStaticHashedString,...)'s
	// comment - same bit-reinterpretation reasoning (uint/float share the
	// same 4-byte width), component_count=4.
	const std::string& name = uniform.String();
	const float* fv = reinterpret_cast<const float*>(v);
	mDXVertexShader.setUniformPaddedArray(name, fv, 4, count);
	mDXPixelShader.setUniformPaddedArray(name, fv, 4, count);
}

void LLHLSLShader::uniformMatrix4fv(const LLStaticHashedString& uniform, U32 count, bool transpose, const F32* v)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;

	// S24 (2026-08-09, task #153): the count==1 case (the overwhelming
	// majority of real callers) mirrors uniformMatrix4fv(U32,...)'s own
	// DX_RENDER branch, including the transpose handling (a caller passing
	// transpose=TRUE means v is row-major and needs converting to
	// setUniformMatrix4's expected column-major layout - missed in an
	// earlier pass of this fix, caught by comparing against the U32
	// overload's real implementation). count>1 (an actual array of
	// matrices, e.g. shadow cascade matrices) is NOT handled - that's
	// task #146's own tracked, still-open gap, not invented/solved here to
	// avoid diverging from that task's scope.
	const std::string& name = uniform.String();
	if (count == 1)
	{
		float m[16];
		if (transpose)
		{
			for (int r = 0; r < 4; ++r)
			{
				for (int c = 0; c < 4; ++c)
				{
					m[c * 4 + r] = v[r * 4 + c];
				}
			}
		}
		else
		{
			memcpy(m, v, sizeof(m));
		}

		mDXVertexShader.setUniformMatrix4(name, m);
		mDXPixelShader.setUniformMatrix4(name, m);
	}
}

void LLHLSLShader::vertexAttrib4f(U32 index, F32 x, F32 y, F32 z, F32 w)
{
	if (mAttribute[index] > 0)
	{
		glVertexAttrib4f(mAttribute[index], x, y, z, w);
	}
}

void LLHLSLShader::vertexAttrib4fv(U32 index, F32* v)
{
	if (mAttribute[index] > 0)
	{
		glVertexAttrib4fv(mAttribute[index], v);
	}
}

void LLHLSLShader::setMinimumAlpha(F32 minimum)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
	gDX.flush();
	uniform1f(LLShaderMgr::MINIMUM_ALPHA, minimum);
}

void LLShaderUniforms::apply(LLHLSLShader* shader)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER;
	for (auto& uniform : mIntegers)
	{
		shader->uniform1i(uniform.mUniform, uniform.mValue);
	}

	for (auto& uniform : mFloats)
	{
		shader->uniform1f(uniform.mUniform, uniform.mValue);
	}

	for (auto& uniform : mVectors)
	{
		shader->uniform4fv(uniform.mUniform, 1, uniform.mValue.mV);
	}

	for (auto& uniform : mVector3s)
	{
		shader->uniform3fv(uniform.mUniform, 1, uniform.mValue.mV);
	}
}

LLUUID LLHLSLShader::hash()
{
	HBXXH128 hash_obj;
	hash_obj.update(mName);
	hash_obj.update(&mShaderGroup, sizeof(mShaderGroup));
	hash_obj.update(&mShaderLevel, sizeof(mShaderLevel));
	for (const auto& shdr_pair : mShaderFiles)
	{
		hash_obj.update(shdr_pair.first);
		hash_obj.update(&shdr_pair.second, sizeof(GLenum));
	}
	for (const auto& define_pair : mDefines)
	{
		hash_obj.update(define_pair.first);
		hash_obj.update(define_pair.second);

	}
	for (const auto& define_pair : LLHLSLShader::sGlobalDefines)
	{
		hash_obj.update(define_pair.first);
		hash_obj.update(define_pair.second);

	}
	hash_obj.update(&mFeatures, sizeof(LLShaderFeatures));
	hash_obj.update(gGLManager.mGLVendor);
	hash_obj.update(gGLManager.mGLRenderer);
	hash_obj.update(gGLManager.mGLVersionString);
	return hash_obj.digest();
}

#if LL_PROFILER_ENABLE_RENDER_DOC
void LLHLSLShader::setLabel(const char* label) {
	LL_LABEL_OBJECT_GL(GL_PROGRAM, mProgramObject, strlen(label), label);
}
#endif