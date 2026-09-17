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
#include "llwindow.h" // S24: LLSplashScreen::update() for startup shader-compile progress
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
LLHLSLShader    gUIHueShiftProgram;
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
	if (sProfileEnabled && sCurBoundShaderPtr)
	{
		sCurBoundShaderPtr->placeProfileQuery();
	}
}

//static
void LLHLSLShader::stopProfile()
{

	if (sProfileEnabled && sCurBoundShaderPtr)
	{
		sCurBoundShaderPtr->unbind();
	}
}

#ifdef DX_RENDER
void LLHLSLShader::DXProfileQueries::reset()
{
	if (disjoint)       { disjoint->Release();       disjoint = nullptr; }
	if (timestampBegin) { timestampBegin->Release(); timestampBegin = nullptr; }
	if (timestampEnd)   { timestampEnd->Release();   timestampEnd = nullptr; }
	if (occlusion)       { occlusion->Release();       occlusion = nullptr; }
	if (pipelineStats)   { pipelineStats->Release();   pipelineStats = nullptr; }
}
#endif

void LLHLSLShader::placeProfileQuery(bool for_runtime)
{
#ifdef DX_RENDER
	// D3D11 has no single "elapsed time" query like GL_TIME_ELAPSED - a disjoint query
	// (frequency + validity) brackets a pair of plain timestamp queries instead (timestamp
	// queries only support End(), never Begin()). occlusion/pipelineStats are the
	// GL_SAMPLES_PASSED/GL_PRIMITIVES_GENERATED analogues, IAPrimitives being the closest
	// D3D11 equivalent of "primitives generated" (input-assembler count, not post-clip).
	if (sProfileEnabled || for_runtime)
	{
		ID3D11Device* device = gDXDevice.getDevice();
		ID3D11DeviceContext* context = gDXDevice.getContext();
		if (!device || !context)
		{
			return;
		}

		if (!mDXProfileQueries.disjoint)
		{
			D3D11_QUERY_DESC qd = {};
			qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
			device->CreateQuery(&qd, &mDXProfileQueries.disjoint);
			qd.Query = D3D11_QUERY_TIMESTAMP;
			device->CreateQuery(&qd, &mDXProfileQueries.timestampBegin);
			device->CreateQuery(&qd, &mDXProfileQueries.timestampEnd);
			if (!for_runtime)
			{
				qd.Query = D3D11_QUERY_OCCLUSION;
				device->CreateQuery(&qd, &mDXProfileQueries.occlusion);
				qd.Query = D3D11_QUERY_PIPELINE_STATISTICS;
				device->CreateQuery(&qd, &mDXProfileQueries.pipelineStats);
			}
		}

		if (!mDXProfileQueries.disjoint || !mDXProfileQueries.timestampBegin)
		{
			return;
		}

		context->Begin(mDXProfileQueries.disjoint);
		context->End(mDXProfileQueries.timestampBegin);

		if (!for_runtime && mDXProfileQueries.occlusion && mDXProfileQueries.pipelineStats)
		{
			context->Begin(mDXProfileQueries.occlusion);
			context->Begin(mDXProfileQueries.pipelineStats);
		}
	}
	return;
#else
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
#ifdef DX_RENDER
	if ((sProfileEnabled || for_runtime) && mDXProfileQueries.disjoint)
	{
		ID3D11DeviceContext* context = gDXDevice.getContext();
		if (!context)
		{
			return false;
		}

		if (!mProfilePending)
		{
			context->End(mDXProfileQueries.timestampEnd);
			if (!for_runtime && mDXProfileQueries.occlusion && mDXProfileQueries.pipelineStats)
			{
				context->End(mDXProfileQueries.occlusion);
				context->End(mDXProfileQueries.pipelineStats);
			}
			context->End(mDXProfileQueries.disjoint);
			mProfilePending = for_runtime;
		}

		// S24: same "poll, don't block" convention as the GL path's GL_QUERY_RESULT_AVAILABLE
		// check - GetData() with a non-zero flag returns S_FALSE (not ready) instead of stalling
		// the pipeline waiting for the GPU, unless force_read demands an immediate blocking read.
		UINT flags = force_read ? 0 : D3D11_ASYNC_GETDATA_DONOTFLUSH;
		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint_data = {};
		HRESULT hr = context->GetData(mDXProfileQueries.disjoint, &disjoint_data, sizeof(disjoint_data), flags);
		if (hr != S_OK)
		{
			if (!force_read)
			{
				return false;
			}
			// force_read: block until the result is actually available rather than giving up.
			while (context->GetData(mDXProfileQueries.disjoint, &disjoint_data, sizeof(disjoint_data), 0) != S_OK)
			{
			}
		}

		UINT64 t0 = 0, t1 = 0;
		context->GetData(mDXProfileQueries.timestampBegin, &t0, sizeof(t0), 0);
		context->GetData(mDXProfileQueries.timestampEnd, &t1, sizeof(t1), 0);

		mProfilePending = false;

		if (!disjoint_data.Disjoint && disjoint_data.Frequency > 0 && t1 > t0)
		{
			U64 time_elapsed = (U64)(((double)(t1 - t0) / (double)disjoint_data.Frequency) * 1.0e9);
			mTimeElapsed += time_elapsed;

			if (!for_runtime && mDXProfileQueries.occlusion && mDXProfileQueries.pipelineStats)
			{
				UINT64 samples_passed = 0;
				context->GetData(mDXProfileQueries.occlusion, &samples_passed, sizeof(samples_passed), 0);

				D3D11_QUERY_DATA_PIPELINE_STATISTICS stats = {};
				context->GetData(mDXProfileQueries.pipelineStats, &stats, sizeof(stats), 0);

				sTotalTimeElapsed += time_elapsed;

				sTotalSamplesDrawn += samples_passed;
				mSamplesDrawn += samples_passed;

				U32 tri_count = (U32)stats.IAPrimitives;

				mTrianglesDrawn += tri_count;
				sTotalTrianglesDrawn += tri_count;

				sTotalBinds++;
				mBinds++;
			}
		}
	}

	return true;
#else
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
#endif
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
	// S24: must be initialized, not left default-constructed - syncMatrices()
	// compares LLRender::mMatHash[mode] against this array to decide whether
	// a real matrix re-upload is needed, and uninitialized garbage could
	// coincidentally match on the first sync, skipping the first upload.
	// UINT32_MAX matches GL's own "force a first sync" sentinel.
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
	mDXProfileQueries.reset();
}

bool LLHLSLShader::createShader()
{

	// S24: isVisible() guards against reopening the splash dialog on a
	// mid-session shader reload - update() unconditionally recreates the
	// splash window if it was already hidden.
	if (LLSplashScreen::isVisible())
	{
		LLSplashScreen::update("Compiling shader: " + mName);
	}

	return createShaderDX();
}

namespace
{
	// True if `identifier` appears as a standalone token anywhere in `text` - a `#define X ...`
	// line has zero effect on the compiled shader unless its name is referenced somewhere in the
	// body, so buildDXShaderHeader() below uses this to skip emitting defines a given shader's own
	// source never looks at. Conservative over-approximation (a comment/string literal containing
	// the same word would count as a "reference" too) rather than exact preprocessor semantics -
	// that only ever causes a harmless unnecessary #define, never a missed real one. No token-
	// pasting (##) or stringizing (#x) macro usage exists anywhere in this shader tree (checked),
	// so a plain word-boundary substring match is exact here, not just an approximation in practice.
	bool referencesIdentifier(const std::string& text, const std::string& identifier)
	{
		size_t pos = 0;
		while ((pos = text.find(identifier, pos)) != std::string::npos)
		{
			bool left_ok = (pos == 0) || !(isalnum((unsigned char)text[pos - 1]) || text[pos - 1] == '_');
			size_t end = pos + identifier.size();
			bool right_ok = (end >= text.size()) || !(isalnum((unsigned char)text[end]) || text[end] == '_');
			if (left_ok && right_ok)
			{
				return true;
			}
			pos = end;
		}
		return false;
	}

	// Mirrors the small set of #define's LLShaderMgr::loadShaderFile() (GL
	// path) unconditionally injects into every file via extra_code_text -
	// real shaders reference these directly (e.g. diffuseF.hlsl's
	// GBUFFER_FLAG_HAS_ATMOS). Built once per stage per program and prepended
	// to the concatenated blob, rather than baked into each cached file's
	// text, so attached utility files don't each carry their own duplicate
	// copy of the same defines.
	//
	// S24: `body` is this shader's own already-concatenated (entry file + attached utility files,
	// post-#include-resolution) source text, BEFORE this header gets prepended to it - used to skip
	// emitting any settings-driven define this particular shader's text never references. Without
	// this filter, every one of these defines gets baked into literally every one of ~150-200
	// shader programs' header regardless of relevance, so toggling ANY setting that feeds one
	// (RenderMirrors/HERO_PROBES, RenderScreenSpaceReflections/SSR, RenderShadowDetail/SUN_SHADOW+
	// SPOT_SHADOW, RenderEnableEmissiveBuffer/HAS_EMISSIVE, ...) changes the exact source text
	// DXShader's on-disk bytecode cache is keyed on for every single shader at once - even the ones
	// that never look at that setting - forcing a full D3DCompile of the entire engine on every such
	// toggle even though the vast majority of shaders' actual compiled output can't have changed.
	// User-reported symptom this fixes: "mirrors specifically are rebuilding shaders... the shaders
	// have not changed" - true for all but reflectionProbeF.hlsl, the only file that reads
	// HERO_PROBES. GBUFFER_FLAG_*/VERTEX_SHADER/FRAGMENT_SHADER stay unconditional - cheap,
	// universally near-ubiquitous, and not the source of this problem.
	std::string buildDXShaderHeader(bool is_fragment, const LLHLSLShader::defines_map_t& defines, const std::string& body)
	{
		std::string out = is_fragment ? "#define FRAGMENT_SHADER 1\n" : "#define VERTEX_SHADER 1\n";

		out += "#define GBUFFER_FLAG_SKIP_ATMOS 0.0\n";
		out += "#define GBUFFER_FLAG_HAS_ATMOS 0.34\n";
		out += "#define GBUFFER_FLAG_HAS_PBR 0.67\n";
		out += "#define GBUFFER_FLAG_HAS_HDRI 1.0\n";
		out += "#define GET_GBUFFER_FLAG(data, flag) (abs(data-flag)< 0.1)\n";

		for (auto& d : defines)
		{
			if (referencesIdentifier(body, d.first))
			{
				out += "#define " + d.first + " " + d.second + "\n";
			}
		}

		// sGlobalDefines (llviewershadermgr.cpp's loadBasicShaders(), e.g.
		// MAX_JOINTS_PER_MESH_OBJECT/SUN_SHADOW/SSR/REFMAP_LEVEL/terrain-PBR
		// settings) was never emitted here - any HLSL file referencing one
		// of these directly (not just inside #if defined(...)) saw it as
		// undeclared. Per-shader defines above take precedence on conflict.
		for (auto& d : LLHLSLShader::sGlobalDefines)
		{
			if (defines.find(d.first) == defines.end() && referencesIdentifier(body, d.first))
			{
				out += "#define " + d.first + " " + d.second + "\n";
			}
		}

		return out;
	}
}

bool LLHLSLShader::buildDXSource()
{

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
		// S24: resolve #include "varying/....hlsli" directives before the
		// other injection passes, so a shared varying struct is defined
		// before this file's own text references it.
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
		mDXVertexSource = buildDXShaderHeader(false, mDefines, mDXVertexSource) + mDXVertexSource;
	}
	if (!mDXPixelSource.empty())
	{
		DXShader::resolveIncludes(mDXPixelSource);
		mDXPixelSource = buildDXShaderHeader(true, mDefines, mDXPixelSource) + mDXPixelSource;
	}

	return true;
}

bool LLHLSLShader::createShaderDX()
{

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
		// S24: DX-native equivalent of mapUniforms()'s GL-only mTexture[]
		// population, which never runs under DX_RENDER. Sources the
		// name->register(tN) mapping via DXShader::getTextureBindPoint()
		// (D3D11 reflection) instead of GL reflection - same shape as
		// reflectConstants() for $Globals uniforms.
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
		glAttachShader(mProgramObject, object);
#if DEBUG_SHADER_INCLUDES
		std::string object_path("???");
		dumpAttachObject("attachObject", mProgramObject, object_path);
#endif // DEBUG_SHADER_INCLUDES
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

// S24: mapAttributes()/mapUniform()/mapUniformTextureChannel()/
// mapUniforms()/link() removed - all GL-only reflection/linking, unreachable
// under DX_RENDER. Real equivalent is createShaderDX()'s
// DXShader::getTextureBindPoint()-based reflection above (uniforms/textures)
// and DXShader's D3DCompile() step (attributes/linking - see
// reflectVertexAttributeMask()). loadCachedProgramBinary()/
// saveCachedProgramBinary() (llshadermgr.cpp), link()'s only other
// dependent, removed alongside.
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

void LLHLSLShader::bind()
{

	// Minimum needed so LLVertexBuffer::setBuffer() (which asserts
	// sCurBoundShaderPtr) is reachable: bind the compiled DX shaders and
	// track "current shader" by pointer identity instead of mProgramObject
	// (always 0 under DX_RENDER - no GL program object exists).
	// Matrix uniforms (modelview/projection/normal/texture0) ARE wired -
	// see LLRender::syncMatrices()'s DX_RENDER branch, called from
	// LLVertexBuffer::drawRange()/drawArrays() same as GL. mAttributeMask-
	// driven client-array setup (setupClientArrays()) still has no DX_RENDER
	// equivalent, deferred to whenever a pool needing it is converted.
	//
	// S24: a compile failure anywhere earlier in llviewershadermgr.cpp's
	// shader-load chain (each entry only compiles `if (success)` from the
	// previous one) silently leaves a LATER shader in this chain fully
	// unbuilt - mDXVertexShader/mDXPixelShader stay default-constructed
	// (nullptr), and nothing stops a caller from binding it anyway. That
	// used to be an llassert_always() here, which doesn't actually prevent
	// the subsequent VSSetShader()/PSSetShader() calls from running in this
	// build's assert configuration - real crash, first observed via the
	// shooting-star shader only reachable at night. Skip the bind entirely
	// instead: one real broken shader now costs a log line and whatever
	// this draw call was doing renders as nothing, not the whole process.
	if (!isComplete())
	{
		if (!mLoggedIncompleteBind)
		{
			mLoggedIncompleteBind = true;
			LL_WARNS("Shader") << "bind() skipped - \"" << mName << "\" never compiled successfully "
				"(see the D3DCompile failure earlier in the log for the real cause)" << LL_ENDL;
		}
		return;
	}

	// S24: no "skip if sCurBoundShaderPtr already == this" dedup - DXUIBatch::
	// drawAndPop() sets VS/PS directly without updating sCurBoundShaderPtr,
	// so that bookkeeping can desync from what's actually bound in D3D11,
	// and a dedup would then wrongly skip a real rebind.
	gDXUIBatch.flushPending();
	gDXDevice.getContext()->VSSetShader(mDXVertexShader.getVS(), nullptr, 0);
	gDXDevice.getContext()->PSSetShader(mDXPixelShader.getPS(), nullptr, 0);
	sCurBoundShaderPtr = this;

	// S24: gated on mUniformsDirty, not called unconditionally - an earlier
	// unconditional version crashed at startup by reaching LLEnvironment
	// during early shader compilation, before the main loop is up.
	// mUniformsDirty is only ever set by LLEnvironment::update()'s per-frame
	// loop, so this can't fire before LLEnvironment has run at least once.
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

	// S24: must flush() BEFORE clearing sCurBoundShaderPtr - LLRender::flush()
	// reads sCurBoundShaderPtr to determine which shader a pending
	// immediate-mode batch belongs to, and finding it null silently drops
	// the batch. Also flush gDXUIBatch's separate pending queue - same
	// missing-flush hazard, different queue.
	gDX.flush();
	gDXUIBatch.flushPending();
	LLVertexBuffer::unbind();
	gDXDevice.getContext()->VSSetShader(nullptr, nullptr, 0);
	gDXDevice.getContext()->PSSetShader(nullptr, nullptr, 0);
	sCurBoundShaderPtr = nullptr;
}

S32 LLHLSLShader::bindTexture(const std::string& uniform, LLTexture* texture, LLTexUnit::eTextureType mode)
{

	S32 channel = 0;
	channel = getUniformLocation(uniform);

	return bindTexture(channel, texture, mode);
}

S32 LLHLSLShader::bindTexture(S32 uniform, LLTexture* texture, LLTexUnit::eTextureType mode)
{

	// S24: mTexture[] is populated by createShaderDX()'s post-compile
	// reflection pass; bindFast() is the real bind call.
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

	// S24: `index` (multi-attachment selection) and `mode` (filter option)
	// aren't threaded through LLTexUnit::bind(LLRenderTarget*, bool) yet -
	// it always binds attachment 0 with a fixed CLAMP+BILINEAR sampler.
	// Fine for water's two single-attachment targets; revisit if a future
	// multi-attachment caller needs it.
	S32 channel = getTextureChannel(uniform);
	if (channel > -1)
	{
		gDX.getTexUnit(channel)->bind(texture, depth);
	}
	return channel;
}

S32 LLHLSLShader::bindTexture(const std::string& uniform, LLRenderTarget* texture, bool depth, LLTexUnit::eTextureFilterOptions mode)
{

	S32 channel = 0;
	channel = getUniformLocation(uniform);

	return bindTexture(channel, texture, depth, mode);
}

S32 LLHLSLShader::unbindTexture(const std::string& uniform, LLTexUnit::eTextureType mode)
{

	S32 channel = 0;
	channel = getUniformLocation(uniform);

	return unbindTexture(channel);
}

S32 LLHLSLShader::unbindTexture(S32 uniform, LLTexUnit::eTextureType mode)
{

	// S24: see bindTexture(S32, LLTexture*, ...) above.
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
	// S24: see bindTexture(S32, LLTexture*, ...) above.
	if (uniform < 0 || uniform >= (S32)mTexture.size())
	{
		return -1;
	}
	return mTexture[uniform];
}

S32 LLHLSLShader::enableTexture(S32 uniform, LLTexUnit::eTextureType mode)
{

	// S24: see bindTexture(S32, LLTexture*, ...) above. activate()/enable() are already
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

	// S24: mCurrTexType stays TT_NONE forever under DX_RENDER (enable()'s
	// DX_RENDER branch never sets it), so the curr_type-gated block below is
	// never reached here - harmless, just resolves and returns the channel.
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

	// S24: call unconditionally, not gated on mCurrTexType (stays TT_NONE
	// forever under DX_RENDER) - that gate previously always evaluated
	// false here, leaving SRVs bound at the GPU level indefinitely.
	tex_unit->disable();

	return index;
}

void LLHLSLShader::uniform1i(U32 index, GLint x)
{
	llassert(sCurBoundShaderPtr == this);

	// S24: reinterprets the int's own bits through a float* rather than
	// converting the VALUE to float - setUniformFloatArray() is a raw memcpy
	// into the constant buffer, and HLSL packs `int` as a plain 4-byte
	// int32, so a float-converted bit pattern would corrupt it.
	if (index < LLShaderMgr::instance()->mReservedUniforms.size())
	{
		const std::string& name = LLShaderMgr::instance()->mReservedUniforms[index];
		const float* v = reinterpret_cast<const float*>(&x);
		// S24: both stages set unconditionally, not "try VS, only try PS if
		// VS failed" - a uniform declared in both stages (e.g. cloud_scale)
		// would otherwise only ever reach whichever was tried first.
		// setUniformFloatArray() safely no-ops if a stage doesn't declare it.
		mDXVertexShader.setUniformFloatArray(name, v, 1);
		mDXPixelShader.setUniformFloatArray(name, v, 1);
	}
}

void LLHLSLShader::uniform1f(U32 index, F32 x)
{
	llassert(sCurBoundShaderPtr == this);

	// S24: both stages must be set unconditionally, not "try VS, only try PS
	// if VS failed" - a uniform declared in both stages (e.g. cloud_scale,
	// used by both cloudsV.hlsl's UV scaling and cloudsF.hlsl's discard
	// gate) would otherwise only ever reach whichever stage is tried first,
	// leaving the other at its zero-initialized default. Safe unconditionally:
	// setUniformFloatArray() no-ops if a stage doesn't declare the constant.
	// Same fix applied to every other uniform-setter overload in this file.
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
	llassert(sCurBoundShaderPtr == this);
	llassert(mProgramObject);
	llassert(mUniform.size() <= index);
	llassert(mUniform[index] >= 0);
	glUniform1f(mUniform[index], x);
}

void LLHLSLShader::uniform2f(U32 index, F32 x, F32 y)
{
	llassert(sCurBoundShaderPtr == this);

	// S24: mirrors uniform1f(U32,...)'s DX_RENDER branch.
	if (index < LLShaderMgr::instance()->mReservedUniforms.size())
	{
		const std::string& name = LLShaderMgr::instance()->mReservedUniforms[index];
		const float v[2] = { x, y };
		// S24: both stages unconditionally - see uniform1f(U32,...)'s comment.
		mDXVertexShader.setUniformFloatArray(name, v, 2);
		mDXPixelShader.setUniformFloatArray(name, v, 2);
	}
}

void LLHLSLShader::uniform3f(U32 index, F32 x, F32 y, F32 z)
{
	llassert(sCurBoundShaderPtr == this);

	// S24: see uniform2f(U32,...) above - same fix, same reasoning.
	if (index < LLShaderMgr::instance()->mReservedUniforms.size())
	{
		const std::string& name = LLShaderMgr::instance()->mReservedUniforms[index];
		const float v[3] = { x, y, z };
		// S24: both stages unconditionally - see uniform1f(U32,...)'s comment.
		mDXVertexShader.setUniformFloatArray(name, v, 3);
		mDXPixelShader.setUniformFloatArray(name, v, 3);
	}
}

void LLHLSLShader::uniform4f(U32 index, F32 x, F32 y, F32 z, F32 w)
{
	llassert(sCurBoundShaderPtr == this);

	// S24: mirrors uniform4fv(U32,...)'s DX_RENDER branch above.
	if (index < LLShaderMgr::instance()->mReservedUniforms.size())
	{
		const std::string& name = LLShaderMgr::instance()->mReservedUniforms[index];
		const F32 v[4] = { x, y, z, w };
		// S24: both stages unconditionally - see uniform1f(U32,...)'s comment.
		mDXVertexShader.setUniformFloatArray(name, v, 4);
		mDXPixelShader.setUniformFloatArray(name, v, 4);
	}
}

void LLHLSLShader::uniform1iv(U32 index, U32 count, const GLint* v)
{
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
	llassert(sCurBoundShaderPtr == this);

	// S24: uses setUniformPaddedArray() (component_count=1), not a raw
	// memcpy - HLSL pads every array element, even "float x[N]", to its own
	// 16-byte slot.
	if (index < LLShaderMgr::instance()->mReservedUniforms.size())
	{
		const std::string& name = LLShaderMgr::instance()->mReservedUniforms[index];
		// S24: both stages unconditionally - see uniform1f(U32,...)'s comment.
		mDXVertexShader.setUniformPaddedArray(name, v, 1, count);
		mDXPixelShader.setUniformPaddedArray(name, v, 1, count);
	}
}

void LLHLSLShader::uniform2fv(U32 index, U32 count, const F32* v)
{
	llassert(sCurBoundShaderPtr == this);

	// Mirrors uniform1fv()'s DX_RENDER branch above (component_count=2).
	if (index < LLShaderMgr::instance()->mReservedUniforms.size())
	{
		const std::string& name = LLShaderMgr::instance()->mReservedUniforms[index];
		// S24: both stages unconditionally - see uniform1f(U32,...)'s comment.
		mDXVertexShader.setUniformPaddedArray(name, v, 2, count);
		mDXPixelShader.setUniformPaddedArray(name, v, 2, count);
	}
}

void LLHLSLShader::uniform3fv(U32 index, U32 count, const F32* v)
{
	llassert(sCurBoundShaderPtr == this);

	// Mirrors uniform1fv()'s DX_RENDER branch above (component_count=3).
	if (index < LLShaderMgr::instance()->mReservedUniforms.size())
	{
		const std::string& name = LLShaderMgr::instance()->mReservedUniforms[index];
		// S24: both stages unconditionally - see uniform1f(U32,...)'s comment.
		mDXVertexShader.setUniformPaddedArray(name, v, 3, count);
		mDXPixelShader.setUniformPaddedArray(name, v, 3, count);
	}
}

void LLHLSLShader::uniform4fv(U32 index, U32 count, const F32* v)
{
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
		// S24: both stages unconditionally - see uniform1f(U32,...)'s comment.
		mDXVertexShader.setUniformFloatArray(name, v, (size_t)count * 4);
		mDXPixelShader.setUniformFloatArray(name, v, (size_t)count * 4);
	}
}

void LLHLSLShader::uniform4uiv(U32 index, U32 count, const GLuint* v)
{
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
				glUniform4uiv(mUniform[index], count, v);
				mValue[mUniform[index]] = vec;
			}
		}
	}
}

void LLHLSLShader::uniformMatrix2fv(U32 index, U32 count, bool transpose, const F32* v)
{
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
	llassert(sCurBoundShaderPtr == this);

	// S24: count > 1 has no known 3x3 array caller today (unlike
	// uniformMatrix4fv's DEFERRED_SHADOW_MATRIX), but handled for
	// completeness rather than leaving a matching no-op trap - see
	// uniformMatrix4fv(U32,...)'s count>1 branch.
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

		// S24: both stages unconditionally - see uniform1f(U32,...)'s comment.
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
	llassert(sCurBoundShaderPtr == this);

	// S24: only real caller is LLRenderPass::uploadMatrixPalette()'s rigged-
	// mesh skinning matrix palette (objectSkinV.hlsl's `row_major float3x4`).
	// GL's column-major mat3x4 layout is byte-identical to that row_major
	// float3x4 layout, so this is a straight contiguous copy with no
	// repacking. transpose=true is not handled - no real caller uses it.
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
	llassert(sCurBoundShaderPtr == this);

	// S24: count > 1 is real - pipeline.cpp's bindDeferredShader() uploads
	// DEFERRED_SHADOW_MATRIX (shadow_matrix[6], 4 sun cascades + 2 spot
	// lights) as a single count=6 call. Single matrix (count==1) keeps the
	// original zero-allocation stack-array path below; count>1 builds a
	// flat count*16 buffer (transpose applied per-matrix) and uploads via
	// setUniformFloatArray(), mirroring uniformMatrix3x4fv(U32,...)'s shape.
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

		// S24: both stages unconditionally - see uniformMatrix3fv(U32,...) above.
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

	GLint ret = -1;
	if (mProgramObject)
	{
		LLStaticStringTable<GLint>::iterator iter = mUniformMap.find(uniform);
		if (iter != mUniformMap.end())
		{
			if (gDebugGL)
			{
				if (iter->second != glGetUniformLocation(mProgramObject, uniform.String().c_str()))
				{
					LL_ERRS() << "Uniform does not match." << LL_ENDL;
				}
			}
			ret = iter->second;
		}
	}

	return ret;
}

GLint LLHLSLShader::getUniformLocation(U32 index)
{

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

	// S24: this whole LLStaticHashedString-keyed overload family had zero
	// DX_RENDER handling - getUniformLocation(LLStaticHashedString) always
	// returns -1, so these were safe no-ops that silently dropped every
	// value. Both stages set unconditionally - see uniform1f(U32,...)'s
	// comment for why. Reinterprets the int's bits through a float* rather
	// than converting the value - HLSL cbuffers pack int as raw int32.
	const std::string& name = uniform.String();
	const float* fv = reinterpret_cast<const float*>(&v);
	mDXVertexShader.setUniformFloatArray(name, fv, 1);
	mDXPixelShader.setUniformFloatArray(name, fv, 1);
}

void LLHLSLShader::uniform1iv(const LLStaticHashedString& uniform, U32 count, const GLint* v)
{

	// S24: see uniform1i(LLStaticHashedString,...) above - same fix. Uses
	// setUniformPaddedArray, not setUniformFloatArray, since HLSL pads
	// every array element to its own 16-byte slot.
	const std::string& name = uniform.String();
	const float* fv = reinterpret_cast<const float*>(v);
	mDXVertexShader.setUniformPaddedArray(name, fv, 1, count);
	mDXPixelShader.setUniformPaddedArray(name, fv, 1, count);
}

void LLHLSLShader::uniform4iv(const LLStaticHashedString& uniform, U32 count, const GLint* v)
{

	// S24: see uniform1iv(LLStaticHashedString,...) above, component_count=4.
	const std::string& name = uniform.String();
	const float* fv = reinterpret_cast<const float*>(v);
	mDXVertexShader.setUniformPaddedArray(name, fv, 4, count);
	mDXPixelShader.setUniformPaddedArray(name, fv, 4, count);
}

void LLHLSLShader::uniform2i(const LLStaticHashedString& uniform, GLint i, GLint j)
{

	// S24: see uniform1iv(LLStaticHashedString,...) above - same
	// bit-reinterpretation reasoning, non-array case.
	const std::string& name = uniform.String();
	const GLint iv[2] = { i, j };
	const float* fv = reinterpret_cast<const float*>(iv);
	mDXVertexShader.setUniformFloatArray(name, fv, 2);
	mDXPixelShader.setUniformFloatArray(name, fv, 2);
}

void LLHLSLShader::uniform1f(const LLStaticHashedString& uniform, F32 v)
{

	// S24: see uniform1i(LLStaticHashedString,...) above - same fix.
	const std::string& name = uniform.String();
	const float v_arr[1] = { v };
	mDXVertexShader.setUniformFloatArray(name, v_arr, 1);
	mDXPixelShader.setUniformFloatArray(name, v_arr, 1);
}

void LLHLSLShader::uniform2f(const LLStaticHashedString& uniform, F32 x, F32 y)
{

	// S24: see uniform1i(LLStaticHashedString,...) above - same fix.
	const std::string& name = uniform.String();
	const float v_arr[2] = { x, y };
	mDXVertexShader.setUniformFloatArray(name, v_arr, 2);
	mDXPixelShader.setUniformFloatArray(name, v_arr, 2);
}

void LLHLSLShader::uniform3f(const LLStaticHashedString& uniform, F32 x, F32 y, F32 z)
{

	// S24: see uniform1i(LLStaticHashedString,...) above - same fix.
	const std::string& name = uniform.String();
	const float v_arr[3] = { x, y, z };
	mDXVertexShader.setUniformFloatArray(name, v_arr, 3);
	mDXPixelShader.setUniformFloatArray(name, v_arr, 3);
}

void LLHLSLShader::uniform4f(const LLStaticHashedString& uniform, F32 x, F32 y, F32 z, F32 w)
{

	// S24: see uniform1i(LLStaticHashedString,...) above - same fix.
	const std::string& name = uniform.String();
	const float v_arr[4] = { x, y, z, w };
	mDXVertexShader.setUniformFloatArray(name, v_arr, 4);
	mDXPixelShader.setUniformFloatArray(name, v_arr, 4);
}

void LLHLSLShader::uniform1fv(const LLStaticHashedString& uniform, U32 count, const F32* v)
{

	// S24: see uniform1fv(U32,...) above.
	const std::string& name = uniform.String();
	mDXVertexShader.setUniformPaddedArray(name, v, 1, count);
	mDXPixelShader.setUniformPaddedArray(name, v, 1, count);
}

void LLHLSLShader::uniform2fv(const LLStaticHashedString& uniform, U32 count, const F32* v)
{

	// S24: see uniform1fv(LLStaticHashedString,...) above, component_count=2.
	const std::string& name = uniform.String();
	mDXVertexShader.setUniformPaddedArray(name, v, 2, count);
	mDXPixelShader.setUniformPaddedArray(name, v, 2, count);
}

void LLHLSLShader::uniform3fv(const LLStaticHashedString& uniform, U32 count, const F32* v)
{

	// S24: see uniform1fv(LLStaticHashedString,...) above, component_count=3.
	const std::string& name = uniform.String();
	mDXVertexShader.setUniformPaddedArray(name, v, 3, count);
	mDXPixelShader.setUniformPaddedArray(name, v, 3, count);
}

void LLHLSLShader::uniform4fv(const LLStaticHashedString& uniform, U32 count, const F32* v)
{

	// S24: see uniform1fv(LLStaticHashedString,...) above, component_count=4.
	const std::string& name = uniform.String();
	mDXVertexShader.setUniformPaddedArray(name, v, 4, count);
	mDXPixelShader.setUniformPaddedArray(name, v, 4, count);
}

void LLHLSLShader::uniform4uiv(const LLStaticHashedString& uniform, U32 count, const GLuint* v)
{

	// S24: see uniform1iv(LLStaticHashedString,...) above, component_count=4.
	const std::string& name = uniform.String();
	const float* fv = reinterpret_cast<const float*>(v);
	mDXVertexShader.setUniformPaddedArray(name, fv, 4, count);
	mDXPixelShader.setUniformPaddedArray(name, fv, 4, count);
}

void LLHLSLShader::uniformMatrix4fv(const LLStaticHashedString& uniform, U32 count, bool transpose, const F32* v)
{

	// S24: count==1 mirrors uniformMatrix4fv(U32,...)'s DX_RENDER branch,
	// including transpose handling (row-major -> column-major). count>1
	// (an array of matrices) is not handled here - still-open gap, same as
	// uniformMatrix4fv(U32,...).
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
	gDX.flush();
	uniform1f(LLShaderMgr::MINIMUM_ALPHA, minimum);
}

void LLShaderUniforms::apply(LLHLSLShader* shader)
{
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
		hash_obj.update(&shdr_pair.second, sizeof(DXenum));
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
}
#endif