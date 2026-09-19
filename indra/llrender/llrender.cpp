/**
* @file llrender.cpp
* @brief LLRender implementation
*
* $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#include "llrender.h"

#include "llvertexbuffer.h"
#include "llhlslshader.h"
#include "llimagegl.h"
#include "llrendertarget.h"
#include "lltexture.h"
#include "llshadermgr.h"
#include "hbxxh.h"
#include "llformat.h"
#include "glm/gtc/type_ptr.hpp"

#ifdef DX_RENDER
#include "DXSampler.h"
#include "DXStateCache.h"
#include "DXTexture.h"
#include "DXDevice.h"
#include "DXSwapChain.h"
#include "DXUIBatch.h"
#include "DXCubeMap.h"
#include "DXCubeMapArray.h"
#endif

//#include <algorithm>
// S24: gl_debug_callback() extern decl removed along with its definition
// in llgl.cpp.

thread_local LLRender gDX;

// Handy copies of last good GL matrices
F32 gGLModelView[16];
F32 gGLLastModelView[16];
F32 gGLLastProjection[16];
F32 gGLProjection[16];

// transform from last frame's camera space to this frame's camera space (and inverse)
glm::mat4 gGLDeltaModelView;
glm::mat4 gGLInverseDeltaModelView;

S32 gGLViewport[4];


U32 LLRender::sUICalls = 0;
U32 LLRender::sUIVerts = 0;
U32 LLTexUnit::sWhiteTexture = 0;
bool LLRender::sGLCoreProfile = false;
bool LLRender::sNsightDebugSupport = false;
LLVector2 LLRender::sUIGLScaleFactor = LLVector2(1.f, 1.f);
bool LLRender::sClassicMode = false;

struct LLVBCache
{
	LLPointer<LLVertexBuffer> vb;
	std::chrono::steady_clock::time_point touched;
};

static std::unordered_map<U64, LLVBCache> sVBCache;
static thread_local std::list<LLVertexBufferData>* sBufferDataList = nullptr;

// S24: live under DX_RENDER via LLTexUnit::getInternalType() (unguarded),
// unlike sGLBlendFactor[] below whose only readers are GL-only.
static const DXenum sGLTextureType[] =
{
	GL_TEXTURE_2D,
	GL_TEXTURE_RECTANGLE,
	GL_TEXTURE_CUBE_MAP,
	GL_TEXTURE_CUBE_MAP_ARRAY,
	GL_TEXTURE_2D_MULTISAMPLE,
	GL_TEXTURE_3D
};

static const GLint sGLAddressMode[] =
{
	GL_REPEAT,
	GL_MIRRORED_REPEAT,
	GL_CLAMP_TO_EDGE
};

const U32 immediate_mask = LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_COLOR | LLVertexBuffer::MAP_TEXCOORD0;

// S24: sGLBlendFactor[] removed - every reader was already inside a dead
// #else (GL) branch of LLRender::blendFunc(), unlike sGLTextureType[] above.

#ifdef DX_RENDER
// Same order/indexing as sGLBlendFactor above - LLRender::eBlendFactor maps
// onto D3D11_BLEND 1:1 (see DXStateCache.h).
static const D3D11_BLEND sDXBlendFactor[] =
{
	D3D11_BLEND_ONE,
	D3D11_BLEND_ZERO,
	D3D11_BLEND_DEST_COLOR,
	D3D11_BLEND_SRC_COLOR,
	D3D11_BLEND_INV_DEST_COLOR,
	D3D11_BLEND_INV_SRC_COLOR,
	D3D11_BLEND_DEST_ALPHA,
	D3D11_BLEND_SRC_ALPHA,
	D3D11_BLEND_INV_DEST_ALPHA,
	D3D11_BLEND_INV_SRC_ALPHA,

	D3D11_BLEND_ZERO // 'BF_UNDEF'
};
#endif

LLTexUnit::LLTexUnit(S32 index)
	: mCurrTexType(TT_NONE),
	mCurrTexture(0),
	mHasMipMaps(false),
	mIndex(index)
{
	llassert_always(index < (S32)LL_NUM_TEXTURE_LAYERS);
}

//static
U32 LLTexUnit::getInternalType(eTextureType type)
{
	return sGLTextureType[type];
}
void LLTexUnit::refreshState(void)
{
	// S24: mCurrTexture/mCurrTexType aren't meaningfully tracked under
	// DX_RENDER - every DX_RENDER bind path sets the real D3D11 SRV/sampler
	// directly at bind time, so there's nothing for this GL-specific
	// "re-bind the last texture" idiom to do. GL branch removed - task #300
	// (full GL removal), never compiled in this DX_RENDER-only build.
	return;
}

void LLTexUnit::activate(void)
{
	// Texture binding (LLTexUnit -> DXTexture/DXSampler + PSSetShaderResources/
	// PSSetSamplers) isn't wired up yet - stubbed as a safe no-op rather than
	// calling glActiveTexture with no GL context. See stage 3 plan memory for
	// the follow-up task; LLDrawPoolSimple's DX_RENDER branch renders with no
	// texture bound until then. GL branch removed - task #300 (full GL
	// removal), never compiled in this DX_RENDER-only build.
}

#ifdef DX_RENDER
namespace
{
    // GL's LLTexUnit::unbind()/unbindFast() don't bind "nothing" - they bind
    // a real 1x1 white texture (LLTexUnit::sWhiteTexture), specifically
    // because shaders like interface/uiF.hlsl unconditionally do
    // `vertex_color * diffuseMap.Sample(...)` for every 2D UI draw, textured
    // or not (solid-color rects/borders/highlights included). A null SRV
    // samples as (0,0,0,0) in HLSL, not a neutral/no-op value - so leaving a
    // slot genuinely unbound (or, worse, leaving whatever was bound there by
    // an earlier, unrelated draw, which is what happened before this fix
    // since unbind()/unbindFast() were pure DX_RENDER no-ops) zeroes out or
    // corrupts every untextured 2D UI element instead of leaving it
    // unaffected. This is a real, self-contained (no newview/ dependency)
    // 1x1 white DXTexture, created once and reused, mirroring GL's
    // sWhiteTexture role exactly.
    ID3D11ShaderResourceView* getWhiteTextureSRV()
    {
        static DXTexture sWhiteDXTexture;
        static bool sInitialized = false;
        if (!sInitialized)
        {
            const uint8_t white_rgba[4] = { 255, 255, 255, 255 };
            sInitialized = sWhiteDXTexture.create(white_rgba, 1, 1, 4);
        }
        return sWhiteDXTexture.getSRV();
    }
}
#endif

void LLTexUnit::enable(eTextureType type)
{
	// S24: mirrors activate()'s DX_RENDER no-op - texture binding under
	// DX_RENDER goes through bindFast()/bind(), which don't need
	// mCurrTexType bookkeeping. Left un-set (stays TT_NONE forever) since
	// nothing under DX_RENDER reads it except GL-only code paths. GL branch
	// removed - task #300 (full GL removal), never compiled in this
	// DX_RENDER-only build.
}

void LLTexUnit::disable(void)
{
	// S24: mCurrTexType stays TT_NONE forever under DX_RENDER (enable()'s
	// DX_RENDER branch never sets it), so a check gated on it would always
	// skip unbind() here. Bypass the stale bookkeeping and unbind
	// unconditionally. GL branch removed - task #300 (full GL removal),
	// never compiled in this DX_RENDER-only build.
	unbind(LLTexUnit::TT_TEXTURE);
}

void LLTexUnit::bindFast(LLTexture* texture)
{
	// Binds this LLImageGL's DXTexture (if uploaded) plus a sampler matching
	// its address-mode/filter settings, to pixel-shader slot mIndex.
	//
	// S24: a null SRV samples as (0,0,0,0) in HLSL, not a no-op, so any
	// shader doing `vertex_color * diffuseMap.Sample(...)` would render
	// fully transparent instead of unaffected - fall back to
	// getWhiteTextureSRV() rather than binding null.
	LLImageGL* gl_tex = texture->getGLTexture();
	// S24: captured BEFORE mCurrBoundImageGL is overwritten below - needed
	// in addition to the mCurrDXSRV comparison, see srv_changed below.
	bool bound_image_changed = (mCurrBoundImageGL != gl_tex);

	// S24: gl_tex can legitimately be null (e.g. a texture still streaming
	// in) - always bind SOMETHING (real texture or white fallback) rather
	// than leaving a slot's previous, unrelated binding stale.
	ID3D11ShaderResourceView* srv = gl_tex ? gl_tex->mDXTexture.getSRV() : nullptr;
	if (!srv)
	{
		srv = getWhiteTextureSRV();
	}
	// S24: mCurrDXSRV alone is not a safe "did the texture change" signal -
	// it's a raw SRV address, and DXTexture::scaleDown() (VRAM-pressure
	// downscaling) releases the old SRV and creates a new one, so a freed
	// COM object's address can be reused by an unrelated texture's SRV.
	// bound_image_changed (against the stable LLImageGL identity, which
	// scaleDown() never destroys) catches this deterministically regardless
	// of address reuse.
	//
	// srv_changed and sampler_changed are independent: bind(LLRenderTarget*,
	// ...)'s useComparisonSampler can legitimately want a different sampler
	// for the same SRV (comparison vs regular), so a shared skip condition
	// would wrongly suppress that sampler rebind.
	bool srv_changed = bound_image_changed || mCurrDXSRV != (void*)srv;
	bool generation_stale = mDXSRVGeneration != DXStateCache::getRTVGeneration();
	if (srv_changed)
	{
		// S24: flush BEFORE updating mCurrBoundImageGL/mCurrDXSRV below, or
		// LLRender::flush()'s mDXImage capture tags still-queued vertices
		// from the previous texture with the new one instead.
		gDX.flush();
		// Also flush gDXUIBatch's separate pending queue - same
		// missing-flush hazard, different queue.
		gDXUIBatch.flushPending();
		mCurrDXSRV = (void*)srv;
	}
	mDXSRVGeneration = DXStateCache::getRTVGeneration();
	mCurrBoundImageGL = gl_tex;
	// gl_tex==nullptr has no address-mode/filter-option to read - TAM_WRAP/
	// TFO_BILINEAR are this codebase's established default (matches
	// LLViewerTexture's own default construction elsewhere).
	ID3D11SamplerState* sampler = gl_tex
		? DXSampler::getOrCreate((int)gl_tex->getAddressMode(), (int)gl_tex->getFilteringOption())
		: DXSampler::getOrCreate((int)LLTexUnit::TAM_WRAP, (int)LLTexUnit::TFO_BILINEAR);

	if (srv_changed || generation_stale)
	{
		gDXDevice.getContext()->PSSetShaderResources(mIndex, 1, &srv);
	}
	// S24: D3D11 caps pixel-shader sampler slots at 16 (s0-s15) but SRV/
	// texture slots go up to 128 - skip the now-meaningless PSSetSamplers
	// call for any texture bound past slot 15 rather than passing an
	// invalid StartSlot to the API.
	if (mIndex < 16 && mCurrDXSampler != (void*)sampler)
	{
		gDXDevice.getContext()->PSSetSamplers(mIndex, 1, &sampler);
	}
	mCurrDXSampler = (void*)sampler;
}

bool LLTexUnit::bind(LLTexture* texture, bool for_rendering, bool forceBind)
{
	// Same chokepoint shape as several stage-5 pool conversions found:
	// enableTexture()-derived channels are already safe here (they come
	// back as -1, and getTexUnit() maps out-of-range indices to a dummy
	// unit whose mIndex is also -1, caught by the check below) - but a
	// *hardcoded* valid unit index (e.g. gDX.getTexUnit(0)->bind(tex),
	// used by several converted pools for their "no per-material channel
	// registration yet" fallback) bypasses that guard and would otherwise
	// reach the raw glBindTexture() call below with no GL context behind
	// it. Delegate to the already-DX-safe bindFast() instead.
	if (mIndex < 0 || !texture) return false;
	bindFast(texture);
	return true;
}

bool LLTexUnit::bind(LLImageGL* texture, bool for_rendering, bool forceBind, S32 usename)
{
	// Mirrors bindFast()'s DX_RENDER body (see its comment) - same
	// chokepoint as the LLTexture* overload above, just operating directly
	// on an LLImageGL instead of going through LLTexture::getGLTexture().
	if (mIndex < 0 || !texture) return false;
	// S24: see bindFast()'s matching comments - bound_image_changed catches
	// SRV-address reuse from DXTexture::scaleDown() that mCurrDXSRV alone
	// would miss, and a null SRV falls back to white rather than sampling
	// as (0,0,0,0).
	bool bound_image_changed = (mCurrBoundImageGL != texture);
	ID3D11ShaderResourceView* srv = texture->mDXTexture.getSRV();
	if (!srv)
	{
		srv = getWhiteTextureSRV();
	}
	bool srv_changed = bound_image_changed || mCurrDXSRV != (void*)srv;
	bool generation_stale = mDXSRVGeneration != DXStateCache::getRTVGeneration();
	if (srv_changed)
	{
		// S24: flush BEFORE updating mCurrBoundImageGL/mCurrDXSRV, not after -
		// see bindFast()'s matching comment. Without this ordering,
		// LLRender::flush()'s mDXImage capture tags still-queued vertices
		// (e.g. from LLFontVertexBuffer's beginList()/endList() recording)
		// with the NEW texture instead of the one they were queued under.
		gDX.flush();
		gDXUIBatch.flushPending();
		mCurrDXSRV = (void*)srv;
	}
	mDXSRVGeneration = DXStateCache::getRTVGeneration();
	mCurrBoundImageGL = texture;
	ID3D11SamplerState* sampler = DXSampler::getOrCreate(
		(int)texture->getAddressMode(), (int)texture->getFilteringOption());
	if (srv_changed || generation_stale)
	{
		gDXDevice.getContext()->PSSetShaderResources(mIndex, 1, &srv);
	}
	if (mIndex < 16 && mCurrDXSampler != (void*)sampler) // S24: sampler slots cap at 16, SRV slots don't - see bindFast()
	{
		gDXDevice.getContext()->PSSetSamplers(mIndex, 1, &sampler);
	}
	mCurrDXSampler = (void*)sampler;
	return true;
}

bool LLTexUnit::bind(DXCubeMap* cubeMap)
{
	if (mIndex < 0 || !cubeMap || !DXCubeMap::sUseCubeMaps) return false;

	// S24: binds cubeMap->getDXSRV(), falling back to white if the cubemap
	// hasn't been assembled yet (init() never called, or failed).
	ID3D11ShaderResourceView* srv = cubeMap->getDXSRV();
	if (!srv)
	{
		srv = getWhiteTextureSRV();
	}
	bool srv_changed = mCurrDXSRV != (void*)srv;
	bool generation_stale = mDXSRVGeneration != DXStateCache::getRTVGeneration();
	if (srv_changed)
	{
		gDX.flush();
		gDXUIBatch.flushPending();
		mCurrDXSRV = (void*)srv;
	}
	mDXSRVGeneration = DXStateCache::getRTVGeneration();
	// CLAMP + TRILINEAR - avoids seams at face edges - and the full mip
	// chain DXCubeTexture::create() always generates.
	ID3D11SamplerState* sampler = DXSampler::getOrCreate(2, 2);
	if (srv_changed || generation_stale)
	{
		gDXDevice.getContext()->PSSetShaderResources(mIndex, 1, &srv);
	}
	if (mIndex < 16 && mCurrDXSampler != (void*)sampler) // S24: sampler slots cap at 16, SRV slots don't - see bindFast()
	{
		gDXDevice.getContext()->PSSetSamplers(mIndex, 1, &sampler);
	}
	mCurrDXSampler = (void*)sampler;
	return true;
}

// S24: direct sibling of bind(DXCubeMap*) just above, for the array resource
// type (DXCubeArrayTexture) instead of the single-cubemap one. No
// DXCubeMap::sUseCubeMaps-style static gate to check.
bool LLTexUnit::bind(DXCubeMapArray* cubeMapArray)
{
	if (mIndex < 0 || !cubeMapArray) return false;

	ID3D11ShaderResourceView* srv = cubeMapArray->getDXSRV();
	if (!srv)
	{
		srv = getWhiteTextureSRV();
	}
	bool srv_changed = mCurrDXSRV != (void*)srv;
	bool generation_stale = mDXSRVGeneration != DXStateCache::getRTVGeneration();
	if (srv_changed)
	{
		gDX.flush();
		gDXUIBatch.flushPending();
		mCurrDXSRV = (void*)srv;
	}
	mDXSRVGeneration = DXStateCache::getRTVGeneration();
	// CLAMP + TRILINEAR - same convention as bind(DXCubeMap*) above (avoids
	// seams at face edges, full mip chain always generated).
	ID3D11SamplerState* sampler = DXSampler::getOrCreate(2, 2);
	if (srv_changed || generation_stale)
	{
		gDXDevice.getContext()->PSSetShaderResources(mIndex, 1, &srv);
	}
	if (mIndex < 16 && mCurrDXSampler != (void*)sampler) // S24: sampler slots cap at 16, SRV slots don't - see bindFast()
	{
		gDXDevice.getContext()->PSSetSamplers(mIndex, 1, &sampler);
	}
	mCurrDXSampler = (void*)sampler;
	return true;
}

// LLRenderTarget is unavailible on the mapserver since it uses FBOs.
bool LLTexUnit::bind(LLRenderTarget* renderTarget, bool bindDepth, bool useComparisonSampler)
{
	if (mIndex < 0 || !renderTarget) return false;

	// S24: pulls the SRV directly via DXRenderTarget's getColorSRV()/
	// getDepthSRV() rather than routing through the GLuint-based
	// bindManual() path, which has nothing to translate under DX_RENDER.
	// This is the texture-binding chokepoint for the whole deferred/
	// post-process chain (bindDeferredShader(), shadow maps, glow, CAS,
	// FXAA/SMAA, SSR, water/hero-probe depth sampling).
	ID3D11ShaderResourceView* srv = bindDepth ? renderTarget->getDepthSRV() : renderTarget->getColorSRV(0);
	if (!srv)
	{
		return false;
	}
	// S24: sampler skip is independent of the SRV skip below - useComparisonSampler
	// means the same SRV can legitimately need a different sampler (comparison
	// vs regular) on two different calls, and a shared skip condition would
	// wrongly suppress that sampler rebind. LLRenderTarget::bindTexture()
	// raw-binds the G-buffer channels outside this cache too - see
	// LLTexUnit::syncDXBindState() for how that stays in sync with it.
	bool srv_changed = mCurrDXSRV != (void*)srv;
	bool generation_stale = mDXSRVGeneration != DXStateCache::getRTVGeneration();
	if (srv_changed)
	{
		gDX.flush();
		gDXUIBatch.flushPending();
		mCurrDXSRV = (void*)srv;
	}
	mDXSRVGeneration = DXStateCache::getRTVGeneration();
	// CLAMP + BILINEAR: matches the common post-process/screen-space sampling
	// convention (edge clamping avoids wrap-around artifacts on a
	// screen-space texture; render targets aren't mipmapped here).
	// S24: useComparisonSampler routes shadow-map binds to a real comparison
	// sampler instead - see DXSampler::getOrCreateComparison() for why a
	// regular sampler is undefined behavior against SamplerComparisonState
	// registers. GREATER_EQUAL (not LESS_EQUAL) because the depth buffer
	// stores near=1.0/far=0.0 (reversed-Z, see kGLtoDXDepthRemap above) -
	// this hardcoded comparison enum isn't routed through glDepthFuncToDX()
	// (llgl.cpp), so it needs its own explicit flip.
	ID3D11SamplerState* sampler = useComparisonSampler
		? DXSampler::getOrCreateComparison(D3D11_COMPARISON_GREATER_EQUAL)
		: DXSampler::getOrCreate(2, 1);
	if (srv_changed || generation_stale)
	{
		gDXDevice.getContext()->PSSetShaderResources(mIndex, 1, &srv);
	}
	if (mIndex < 16 && mCurrDXSampler != (void*)sampler) // S24: sampler slots cap at 16, SRV slots don't - see bindFast()
	{
		gDXDevice.getContext()->PSSetSamplers(mIndex, 1, &sampler);
	}
	mCurrDXSampler = (void*)sampler;
	return true;
}

bool LLTexUnit::bindManual(eTextureType type, U32 texture, bool hasMips)
{
	// Early exit if mIndex is invalid
	if (mIndex < 0) return false;

	// Unlike bind(LLTexture*)/bind(LLImageGL*), this overload is handed a
	// raw GLuint object name with no LLImageGL/LLTexture to look a DX11
	// resource up from - there's nothing to translate. Callers that derive
	// their channel from getTextureChannel()/enableTexture() already return
	// above via the mIndex<0 check; this only guards the hardcoded-unit
	// callers (LLImageGL, LLRenderTarget, LLTexLayer, post-process, edit-tool
	// grid texture, startup noise/SMAA LUTs, etc.) that would otherwise reach
	// the raw glBindTexture() below. GL branch removed - task #300 (full GL
	// removal), never compiled in this DX_RENDER-only build.
	return false;
}

#ifdef DX_RENDER
// S24: DX-native equivalent of bindManual() for callers holding a real
// DXTexture reference directly. Mirrors bindFast(LLTexture*)'s SRV+white-
// fallback+flush-on-change logic exactly, just sourcing the SRV from the
// caller's own DXTexture instead of an LLImageGL's.
bool LLTexUnit::bind(DXTexture& tex, eTextureAddressMode address_mode, eTextureFilterOptions filter_option)
{
	if (mIndex < 0) return false;

	ID3D11ShaderResourceView* srv = tex.getSRV();
	if (!srv)
	{
		srv = getWhiteTextureSRV();
	}

	bool srv_changed = mCurrDXSRV != (void*)srv;
	bool generation_stale = mDXSRVGeneration != DXStateCache::getRTVGeneration();
	if (srv_changed)
	{
		gDX.flush();
		gDXUIBatch.flushPending();
		mCurrDXSRV = (void*)srv;
	}
	mDXSRVGeneration = DXStateCache::getRTVGeneration();

	ID3D11SamplerState* sampler = DXSampler::getOrCreate((int)address_mode, (int)filter_option);
	if (srv_changed || generation_stale)
	{
		gDXDevice.getContext()->PSSetShaderResources(mIndex, 1, &srv);
	}
	if (mIndex < 16 && mCurrDXSampler != (void*)sampler) // S24: sampler slots cap at 16, SRV slots don't - see bindFast()
	{
		gDXDevice.getContext()->PSSetSamplers(mIndex, 1, &sampler);
	}
	mCurrDXSampler = (void*)sampler;
	return true;
}
#endif

void LLTexUnit::unbind(eTextureType type)
{
	// Real fix, not a no-op (was one until this was found and fixed) - see
	// getWhiteTextureSRV()'s comment above for why leaving this a no-op
	// (or a null-bind) silently zeroed out every untextured 2D UI element.
	if (mIndex < 0 || type != LLTexUnit::TT_TEXTURE)
	{
		return;
	}
	ID3D11ShaderResourceView* srv = getWhiteTextureSRV();
	// S24: same missing-flush bug as bind(LLImageGL*) - gl_rect_2d() calls
	// this before pushing its verts; without the flush, those verts can end
	// up drawn with whatever a LATER bind() switches to instead of white.
	bool srv_changed = mCurrDXSRV != (void*)srv;
	bool generation_stale = mDXSRVGeneration != DXStateCache::getRTVGeneration();
	if (srv_changed)
	{
		gDX.flush();
		gDXUIBatch.flushPending();
		mCurrDXSRV = (void*)srv;
	}
	mDXSRVGeneration = DXStateCache::getRTVGeneration();
	// S24: unbind() means "no texture" - mCurrBoundImageGL must actually go
	// to null here, not stay whatever the last real bind() set.
	mCurrBoundImageGL = nullptr;
	ID3D11SamplerState* sampler = DXSampler::getOrCreate(0, 0); // WRAP, POINT - matches a solid white texel regardless
	if (srv_changed || generation_stale)
	{
		gDXDevice.getContext()->PSSetShaderResources(mIndex, 1, &srv);
	}
	if (mIndex < 16 && mCurrDXSampler != (void*)sampler) // S24: sampler slots cap at 16, SRV slots don't - see bindFast()
	{
		gDXDevice.getContext()->PSSetSamplers(mIndex, 1, &sampler);
	}
	mCurrDXSampler = (void*)sampler;
}

void LLTexUnit::unbindFast(eTextureType type)
{
	// Real fix, not a no-op - see unbind()'s comment/getWhiteTextureSRV().
	if (mIndex < 0 || type != LLTexUnit::TT_TEXTURE)
	{
		return;
	}
	ID3D11ShaderResourceView* srv = getWhiteTextureSRV();
	// S24: same missing-flush bug as bind(LLImageGL*)/unbind() - see their comments.
	bool srv_changed = mCurrDXSRV != (void*)srv;
	bool generation_stale = mDXSRVGeneration != DXStateCache::getRTVGeneration();
	if (srv_changed)
	{
		gDX.flush();
		gDXUIBatch.flushPending();
		mCurrDXSRV = (void*)srv;
	}
	mDXSRVGeneration = DXStateCache::getRTVGeneration();
	// S24: see unbind()'s matching fix - mCurrBoundImageGL must go to null too.
	mCurrBoundImageGL = nullptr;
	ID3D11SamplerState* sampler = DXSampler::getOrCreate(0, 0);
	if (srv_changed || generation_stale)
	{
		gDXDevice.getContext()->PSSetShaderResources(mIndex, 1, &srv);
	}
	if (mIndex < 16 && mCurrDXSampler != (void*)sampler) // S24: sampler slots cap at 16, SRV slots don't - see bindFast()
	{
		gDXDevice.getContext()->PSSetSamplers(mIndex, 1, &sampler);
	}
	mCurrDXSampler = (void*)sampler;
}

#ifdef DX_RENDER
// S24: see this method's declaration comment in llrender.h - pure
// bookkeeping, no GPU call. Mirrors the mCurrDXSRV/mDXSRVGeneration/
// mCurrDXSampler update every bind()/bindFast() overload above already does.
void LLTexUnit::syncDXBindState(void* srv, void* sampler)
{
	mCurrDXSRV = srv;
	mCurrDXSampler = sampler;
	mDXSRVGeneration = DXStateCache::getRTVGeneration();
}
#endif // DX_RENDER

void LLTexUnit::setTextureAddressMode(eTextureAddressMode mode)
{
	// Real no-op, not an accidental one: under DX_RENDER, sampler state
	// (address mode + filter option together) is built fresh at bind time
	// by DXSampler::getOrCreate() (see bindFast()'s comment), reading
	// mAddressMode/mFilterOption directly off the LLImageGL/LLTexture being
	// bound - this immediate-state-setting GL idiom has no role to play.
	// Before this was made explicit, the only thing stopping this function
	// from reaching the raw glTexParameteri() calls below under DX_RENDER
	// was mCurrTexture happening to stay 0 forever (every DX_RENDER bind
	// path returns before ever assigning it) - a correct but *implicit*
	// invariant, one accidental future edit away from a null-GL-context
	// crash. Made explicit here instead. GL branch removed - task #300 (full
	// GL removal), never compiled in this DX_RENDER-only build.
	return;
}

void LLTexUnit::setTextureFilteringOption(LLTexUnit::eTextureFilterOptions option)
{
	// Mirrors setTextureAddressMode()'s DX_RENDER no-op above (see its
	// comment) - filter option is likewise applied via DXSampler::getOrCreate()
	// at bind time, not through this GL immediate-state-setting path. GL
	// branch removed - task #300 (full GL removal), never compiled in this
	// DX_RENDER-only build.
	return;
}

GLint LLTexUnit::getTextureSource(eTextureBlendSrc src)
{
	switch (src)
	{
		// All four cases should return the same value.
	case TBS_PREV_COLOR:
	case TBS_PREV_ALPHA:
	case TBS_ONE_MINUS_PREV_COLOR:
	case TBS_ONE_MINUS_PREV_ALPHA:
		return GL_PREVIOUS;

		// All four cases should return the same value.
	case TBS_TEX_COLOR:
	case TBS_TEX_ALPHA:
	case TBS_ONE_MINUS_TEX_COLOR:
	case TBS_ONE_MINUS_TEX_ALPHA:
		return GL_TEXTURE;

		// All four cases should return the same value.
	case TBS_VERT_COLOR:
	case TBS_VERT_ALPHA:
	case TBS_ONE_MINUS_VERT_COLOR:
	case TBS_ONE_MINUS_VERT_ALPHA:
		return GL_PRIMARY_COLOR;

		// All four cases should return the same value.
	case TBS_CONST_COLOR:
	case TBS_CONST_ALPHA:
	case TBS_ONE_MINUS_CONST_COLOR:
	case TBS_ONE_MINUS_CONST_ALPHA:
		return GL_CONSTANT;

	default:
		LL_WARNS() << "Unknown eTextureBlendSrc: " << src << ".  Using Vertex Color instead." << LL_ENDL;
		return GL_PRIMARY_COLOR;
	}
}

GLint LLTexUnit::getTextureSourceType(eTextureBlendSrc src, bool isAlpha)
{
	switch (src)
	{
		// Cases returning GL_SRC_COLOR or GL_SRC_ALPHA
	case TBS_PREV_COLOR:
	case TBS_TEX_COLOR:
	case TBS_VERT_COLOR:
	case TBS_CONST_COLOR:
		return (isAlpha) ? GL_SRC_ALPHA : GL_SRC_COLOR;

		// Cases returning GL_SRC_ALPHA
	case TBS_PREV_ALPHA:
	case TBS_TEX_ALPHA:
	case TBS_VERT_ALPHA:
	case TBS_CONST_ALPHA:
		return GL_SRC_ALPHA;

		// Cases returning GL_ONE_MINUS_SRC_COLOR or GL_ONE_MINUS_SRC_ALPHA
	case TBS_ONE_MINUS_PREV_COLOR:
	case TBS_ONE_MINUS_TEX_COLOR:
	case TBS_ONE_MINUS_VERT_COLOR:
	case TBS_ONE_MINUS_CONST_COLOR:
		return (isAlpha) ? GL_ONE_MINUS_SRC_ALPHA : GL_ONE_MINUS_SRC_COLOR;

		// Cases returning GL_ONE_MINUS_SRC_ALPHA
	case TBS_ONE_MINUS_PREV_ALPHA:
	case TBS_ONE_MINUS_TEX_ALPHA:
	case TBS_ONE_MINUS_VERT_ALPHA:
	case TBS_ONE_MINUS_CONST_ALPHA:
		return GL_ONE_MINUS_SRC_ALPHA;

	default:
		LL_WARNS() << "Unknown eTextureBlendSrc: " << src << ".  Using Source Color or Alpha instead." << LL_ENDL;
		return (isAlpha) ? GL_SRC_ALPHA : GL_SRC_COLOR;
	}
}

// Useful for debugging that you've manually assigned a texture operation to the correct
// texture unit based on the currently set active texture in opengl.
void LLTexUnit::debugTextureUnit(void)
{
	if (mIndex < 0) return;

	GLint activeTexture;
	glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
	if ((GL_TEXTURE0 + mIndex) != activeTexture)
	{
		U32 set_unit = (activeTexture - GL_TEXTURE0);
		LL_WARNS() << "Incorrect Texture Unit!  Expected: " << set_unit << " Actual: " << mIndex << LL_ENDL;
	}
}

LLLightState::LLLightState(S32 index)
	: mIndex(index),
	mEnabled(false),
	mConstantAtten(1.f),
	mLinearAtten(0.f),
	mQuadraticAtten(0.f),
	mSpotExponent(0.f),
	mSpotCutoff(180.f)
{
	if (mIndex == 0)
	{
		mDiffuse.set(1, 1, 1, 1);
		mDiffuseB.set(0, 0, 0, 0);
		mSpecular.set(1, 1, 1, 1);
	}

	mSunIsPrimary = true;

	mAmbient.set(0, 0, 0, 1);
	mPosition.set(0, 0, 1, 0);
	mSpotDirection.set(0, 0, -1);
}

void LLLightState::enable()
{
	mEnabled = true;
}

void LLLightState::disable()
{
	mEnabled = false;
}

void LLLightState::setDiffuse(const LLColor4& diffuse)
{
	if (mDiffuse != diffuse)
	{
		++gDX.mLightHash;
		mDiffuse = diffuse;
	}
}

void LLLightState::setDiffuseB(const LLColor4& diffuse)
{
	if (mDiffuseB != diffuse)
	{
		++gDX.mLightHash;
		mDiffuseB = diffuse;
	}
}

void LLLightState::setSunPrimary(bool v)
{
	if (mSunIsPrimary != v)
	{
		++gDX.mLightHash;
		mSunIsPrimary = v;
	}
}

void LLLightState::setSize(F32 v)
{
	if (mSize != v)
	{
		++gDX.mLightHash;
		mSize = v;
	}
}

void LLLightState::setFalloff(F32 v)
{
	if (mFalloff != v)
	{
		++gDX.mLightHash;
		mFalloff = v;
	}
}

void LLLightState::setAmbient(const LLColor4& ambient)
{
	if (mAmbient != ambient)
	{
		++gDX.mLightHash;
		mAmbient = ambient;
	}
}

void LLLightState::setSpecular(const LLColor4& specular)
{
	if (mSpecular != specular)
	{
		++gDX.mLightHash;
		mSpecular = specular;
	}
}

void LLLightState::setPosition(const LLVector4& position)
{
	//always set position because modelview matrix may have changed
	++gDX.mLightHash;
	mPosition = position;
	//transform position by current modelview matrix
	glm::vec4 pos(position);
	pos = gDX.getModelviewMatrix() * pos;
	mPosition.set(glm::value_ptr(pos));
}

void LLLightState::setConstantAttenuation(const F32& atten)
{
	if (mConstantAtten != atten)
	{
		mConstantAtten = atten;
		++gDX.mLightHash;
	}
}

void LLLightState::setLinearAttenuation(const F32& atten)
{
	if (mLinearAtten != atten)
	{
		++gDX.mLightHash;
		mLinearAtten = atten;
	}
}

void LLLightState::setQuadraticAttenuation(const F32& atten)
{
	if (mQuadraticAtten != atten)
	{
		++gDX.mLightHash;
		mQuadraticAtten = atten;
	}
}

void LLLightState::setSpotExponent(const F32& exponent)
{
	if (mSpotExponent != exponent)
	{
		++gDX.mLightHash;
		mSpotExponent = exponent;
	}
}

void LLLightState::setSpotCutoff(const F32& cutoff)
{
	if (mSpotCutoff != cutoff)
	{
		++gDX.mLightHash;
		mSpotCutoff = cutoff;
	}
}

void LLLightState::setSpotDirection(const LLVector3& direction)
{
	//always set direction because modelview matrix may have changed
	++gDX.mLightHash;

	//transform direction by current modelview matrix
	glm::vec3 dir(direction);
	const glm::mat3 mat(gDX.getModelviewMatrix());
	dir = mat * dir;

	mSpotDirection.set(glm::value_ptr(dir));
}

LLRender::LLRender()
	: mDirty(false),
	mCount(0),
	mMode(LLRender::TRIANGLES),
	mCurrTextureUnitIndex(0),
	mLightHash(0),
	mMatrixMode(LLRender::MM_MODELVIEW),
	mCurrBlendColorSFactor(BF_UNDEF),
	mCurrBlendAlphaSFactor(BF_UNDEF),
	mCurrBlendColorDFactor(BF_UNDEF),
	mCurrBlendAlphaDFactor(BF_UNDEF)
{
	// Texture units
	for (U32 i = 0; i < LL_NUM_TEXTURE_LAYERS; ++i)
		mTexUnits[i].mIndex = i;

	// Light states
	for (U32 i = 0; i < LL_NUM_LIGHT_UNITS; ++i)
		mLightState[i].mIndex = i;

	// Color mask (all true)
	std::fill(std::begin(mCurrColorMask), std::end(mCurrColorMask), true);

	// Matrix stack + indices + hashes
	static const glm::mat4 IDENTITY = glm::identity<glm::mat4>();
	for (U32 mode = 0; mode < NUM_MATRIX_MODES; ++mode)
	{
		std::fill(std::begin(mMatrix[mode]), std::end(mMatrix[mode]), IDENTITY);
		mMatIdx[mode] = 0;
		mMatHash[mode] = 0;
		mCurMatHash[mode] = 0xFFFFFFFFu;
	}
}

LLRender::~LLRender()
{
	shutdown();
}

bool LLRender::init(bool needs_vertex_buffer)
{
	gDX.setSceneBlendType(LLRender::BT_ALPHA);
	gDX.setAmbientLightColor(LLColor4::black);

	if (needs_vertex_buffer)
	{
		initVertexBuffer();
	}
	return true;
}

void LLRender::initVertexBuffer()
{
	llassert_always(mBuffer.isNull());
	mBuffer = new LLVertexBuffer(immediate_mask);
	mBuffer->allocateBuffer(4096, 0);
	mBuffer->getVertexStrider(mVerticesp);
	mBuffer->getTexCoord0Strider(mTexcoordsp);
	mBuffer->getColorStrider(mColorsp);
}

void LLRender::resetVertexBuffer()
{
	mBuffer = nullptr;
}

void LLRender::shutdown()
{
	resetVertexBuffer();
}

void LLRender::refreshState(void)
{
	mDirty = true;

	U32 active_unit = mCurrTextureUnitIndex;

	//  S24 alternate - Use std::for_each to refresh all texture units
	// std::for_each(mTexUnits.begin(), mTexUnits.end(), [](auto& texUnit) {
	//    texUnit.refreshState();
	//    });
	for (auto& texUnit : mTexUnits)
	{
		texUnit.refreshState();
	}

	// Reactivate the previously active texture unit
	mTexUnits[active_unit].activate();

	// Update the color mask settings
	setColorWriteMask(mCurrColorMask[0], mCurrColorMask[1], mCurrColorMask[2], mCurrColorMask[3]);

	// Flush the pipeline to ensure all changes are applied
	flush();

	// Reset the dirty flag as the state is now clean
	mDirty = false;
}

void LLRender::syncLightState()
{
	LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr;

	if (!shader)
	{
		return;
	}

	if (shader->mLightHash != mLightHash)
	{
		shader->mLightHash = mLightHash;

		LLVector4 position[LL_NUM_LIGHT_UNITS];
		LLVector3 direction[LL_NUM_LIGHT_UNITS];
		LLVector4 attenuation[LL_NUM_LIGHT_UNITS];
		LLVector3 diffuse[LL_NUM_LIGHT_UNITS];
		LLVector3 diffuse_b[LL_NUM_LIGHT_UNITS];
		bool      sun_primary[LL_NUM_LIGHT_UNITS];
		LLVector2 size[LL_NUM_LIGHT_UNITS];

		for (U32 i = 0; i < LL_NUM_LIGHT_UNITS; i++)
		{
			LLLightState* light = &mLightState[i];

			position[i] = light->mPosition;
			direction[i] = light->mSpotDirection;
			attenuation[i].set(light->mLinearAtten, light->mQuadraticAtten, light->mSpecular.mV[2], light->mSpecular.mV[3]);
			diffuse[i].set(light->mDiffuse.mV);
			diffuse_b[i].set(light->mDiffuseB.mV);
			sun_primary[i] = light->mSunIsPrimary;
			size[i].set(light->mSize, light->mFalloff);
		}

		shader->uniform4fv(LLShaderMgr::LIGHT_POSITION, LL_NUM_LIGHT_UNITS, position[0].mV);
		shader->uniform3fv(LLShaderMgr::LIGHT_DIRECTION, LL_NUM_LIGHT_UNITS, direction[0].mV);
		shader->uniform4fv(LLShaderMgr::LIGHT_ATTENUATION, LL_NUM_LIGHT_UNITS, attenuation[0].mV);
		shader->uniform2fv(LLShaderMgr::LIGHT_DEFERRED_ATTENUATION, LL_NUM_LIGHT_UNITS, size[0].mV);
		shader->uniform3fv(LLShaderMgr::LIGHT_DIFFUSE, LL_NUM_LIGHT_UNITS, diffuse[0].mV);
		shader->uniform3fv(LLShaderMgr::LIGHT_AMBIENT, 1, mAmbientLightColor.mV);
		shader->uniform1i(LLShaderMgr::SUN_UP_FACTOR, sun_primary[0] ? 1 : 0);

		if (sClassicMode)
		{
			shader->uniform3fv(LLShaderMgr::AMBIENT, 1, mAmbientLightColor.mV);
			shader->uniform3fv(LLShaderMgr::SUNLIGHT_COLOR, 1, diffuse[0].mV);
			shader->uniform3fv(LLShaderMgr::MOONLIGHT_COLOR, 1, diffuse_b[0].mV);
		}
	}
}

#ifdef DX_RENDER
namespace
{
	// Extracts the upper-left 3x3 (as 3 columns of 3 floats each) from a
	// column-major mat4 - matches the GL path's own norm_mat[] construction
	// below (glm::value_ptr(mat)[0,1,2],[4,5,6],[8,9,10]).
	void extractMat3(const glm::mat4& mat, float* out3x3)
	{
		const float* m = glm::value_ptr(mat);
		out3x3[0] = m[0]; out3x3[1] = m[1]; out3x3[2] = m[2];
		out3x3[3] = m[4]; out3x3[4] = m[5]; out3x3[5] = m[6];
		out3x3[6] = m[8]; out3x3[7] = m[9]; out3x3[8] = m[10];
	}
}
#endif

void LLRender::syncMatrices()
{
	STOP_GLERROR;

	// Only pushes the uniforms the currently-ported base shaders
	// (diffuseV.hlsl) actually declare - modelview/projection/normal/
	// texture0 - not GL's full inverse-matrix/texture1-3 set, since nothing
	// converted so far uses those. mUniformsDirty's actual per-shader
	// constant/uniform binding (LLHLSLShader::bind()'s comment) is still a
	// separate, larger gap - this only wires the matrices syncMatrices()
	// itself is responsible for.
	LLHLSLShader* dx_shader = LLHLSLShader::sCurBoundShaderPtr;
	if (dx_shader)
	{
		DXShader& vs = dx_shader->mDXVertexShader;
		DXShader& ps = dx_shader->mDXPixelShader;

		// S24: skip the matrix math and setUniformMatrix4/3 calls below when
		// nothing relevant changed since THIS shader's last sync. mMatHash[mode]
		// is a monotonic per-mode counter bumped by every real matrix mutator
		// (loadMatrix/multMatrix/loadIdentity/popMatrix/translatef/etc below),
		// so it cannot miss a real camera switch. vs.uploadConstants()/
		// VSSetConstantBuffers()/ps.uploadConstants()/PSSetConstantBuffers()
		// further down stay unconditional - still needed to flush any other
		// pending uniform write and rebind the buffer after a shader switch.
		bool matrices_changed =
			(mMatHash[MM_MODELVIEW] != dx_shader->mMatHash[MM_MODELVIEW]) ||
			(mMatHash[MM_PROJECTION] != dx_shader->mMatHash[MM_PROJECTION]) ||
			(mMatHash[MM_TEXTURE0] != dx_shader->mMatHash[MM_TEXTURE0]);

		if (matrices_changed)
		{
		const glm::mat4& mdv = mMatrix[MM_MODELVIEW][mMatIdx[MM_MODELVIEW]];

		// GL-convention projection matrices (glm::frustum()/ortho(), e.g.
		// LLViewerCamera::calcProjection() - GLM_FORCE_DEPTH_ZERO_TO_ONE is
		// not defined anywhere in this project) produce clip-space z in
		// [-w,w], i.e. NDC z in [-1,1] after the divide. D3D11 requires
		// clip-space z in [0,w] (NDC z in [0,1]) and clips away anything
		// outside that range - fed a raw GL-convention matrix, the near
		// half of the intended frustum (NDC z in [-1,0)) gets clipped as
		// "in front of the near plane". Remapped here rather than touching
		// the shared GL-convention projection-matrix construction code,
		// which the GL build still depends on unmodified.
		//
		// S24: z'=0.5*w-0.5*z (not the standard z'=0.5*z+0.5*w) - reversed-Z,
		// so stored depth is near=1.0/far=0.0. This matrix is every pass's
		// projection upload chokepoint (shadows included), so this one sign
		// flip is the root of the whole conversion. Every site that turns a
		// stored depth value back into GL NDC z must flip its own
		// `2.0*depth-1.0` to `1.0-2.0*depth` - see deferredUtil.hlsl/
		// aoUtil.hlsl/waterF.hlsl. Comparison funcs (glDepthFuncToDX(),
		// llgl.cpp) and clear values (DXRenderTarget.cpp/DXContext.cpp,
		// 1.0f->0.0f) flip alongside this as part of the same conversion.
		static const glm::mat4 kGLtoDXDepthRemap = []()
		{
			glm::mat4 m(1.0f);
			m[2][2] = -0.5f;
			m[3][2] = 0.5f;
			return m;
		}();
		const glm::mat4& raw_proj = mMatrix[MM_PROJECTION][mMatIdx[MM_PROJECTION]];
		const glm::mat4 proj = kGLtoDXDepthRemap * raw_proj;
		glm::mat4 mvp = proj * mdv;
		float normal3x3[9];
		extractMat3(glm::transpose(glm::inverse(mdv)), normal3x3);

		vs.setUniformMatrix4("modelview_matrix", glm::value_ptr(mdv));
		vs.setUniformMatrix4("modelview_projection_matrix", glm::value_ptr(mvp));
		// S24: standalone "projection_matrix" (not just the combined
		// modelview_projection_matrix above) is needed by every HAS_SKIN/
		// rigged vertex shader, which build an eye-space position via
		// skin+modelview before projecting separately for lighting/normal
		// math. Uses the same D3D11-depth-remapped `proj` (not raw_proj) as
		// modelview_projection_matrix, since it feeds SV_Position.
		vs.setUniformMatrix4("projection_matrix", glm::value_ptr(proj));
		vs.setUniformMatrix3("normal_matrix", normal3x3);
		const glm::mat4& tex_mat0 = mMatrix[MM_TEXTURE0][mMatIdx[MM_TEXTURE0]];
		vs.setUniformMatrix4("texture_matrix0", glm::value_ptr(tex_mat0));

		// S24: inv_proj uses the UN-remapped GL-convention projection matrix,
		// not `proj` above - getPositionWithDepth() (deferredUtil.hlsl)
		// manually converts D3D11 [0,1] depth back to GL's [-1,1] NDC
		// (`2.0*depth-1.0`) before this multiply, so inv_proj must invert
		// that same GL-convention matrix. Pushed to both vs and ps -
		// setUniformMatrix4() no-ops harmlessly wherever a stage doesn't
		// declare "inv_proj".
		glm::mat4 inv_proj = glm::inverse(raw_proj);
		vs.setUniformMatrix4("inv_proj", glm::value_ptr(inv_proj));
		ps.setUniformMatrix4("inv_proj", glm::value_ptr(inv_proj));

		// S24: remember what this shader was just synced with, so the next
		// draw using it can detect "nothing changed" and skip this block.
		dx_shader->mMatHash[MM_MODELVIEW] = mMatHash[MM_MODELVIEW];
		dx_shader->mMatHash[MM_PROJECTION] = mMatHash[MM_PROJECTION];
		dx_shader->mMatHash[MM_TEXTURE0] = mMatHash[MM_TEXTURE0];
		}

		// S24: re-enabled (task #328) - without this, local point/spot lights
		// contribute zero illumination to forward-lit alpha surfaces (avatar
		// hair, alpha-blend/cutout clothing) under DX_RENDER, since
		// light_position[]/light_direction[]/light_attenuation[]/
		// light_diffuse[]/sun_up_factor were never uploaded to those shaders.
		// A prior attempt to wire this in was reverted after live-testing
		// found a sunrise/sunset "disco" flicker elsewhere (suspected
		// LLSettingsSky::getIsSunUp()'s hard sunDir.mV[2]>=0.0f threshold);
		// a C++-side hold-time debounce didn't fix it. syncLightState() itself
		// is unchanged from that attempt.
		//
		// Gated the same way canonical upstream LL gates this exact call
		// (LLRender::syncMatrices(), llrender.cpp) - only for shaders that
		// actually declare lighting/atmospherics, not unconditionally for
		// every shader bind (UI, water, post-process, etc. never needed
		// this). The earlier attempt here may well have been calling this
		// unconditionally too; matching upstream's real gating is a genuine
		// behavioral difference, not just cosmetic. NEEDS LIVE TESTING
		// ACROSS A SUNRISE/SUNSET TRANSITION before this can be considered
		// fully safe.
		if (dx_shader->mFeatures.hasLighting || dx_shader->mFeatures.calculatesLighting || dx_shader->mFeatures.calculatesAtmospherics)
		{
			syncLightState();
		}

		vs.uploadConstants();

		ID3D11Buffer* cb = vs.getConstantBuffer();
		if (cb)
		{
			// S24: bind to $Globals' real reflected slot, not a hardcoded 0 -
			// a shader claiming b0 for its own named cbuffer (e.g.
			// pbrmetallicroughnessV.hlsl's GLTFMaterials) pushes $Globals to
			// b1 instead. See DXShader::getConstantBufferBindPoint().
			gDXDevice.getContext()->VSSetConstantBuffers(vs.getConstantBufferBindPoint(), 1, &cb);
		}

		// S24: symmetric pixel-shader handling - solidcolorF.hlsl's
		// "uniform vec4 color" needs this (see LLHLSLShader::uniform4f()'s
		// DX_RENDER branch); without it, staged pixel-stage uniforms never
		// reach the GPU. No-ops for shaders with no pixel-stage top-level
		// uniforms (mDXPixelShader.getConstantBuffer() returns nullptr).
		ps.uploadConstants();
		if (ID3D11Buffer* pcb = ps.getConstantBuffer())
		{
			gDXDevice.getContext()->PSSetConstantBuffers(ps.getConstantBufferBindPoint(), 1, &pcb);
		}
	}
}

void LLRender::translatef(const F32& x, const F32& y, const F32& z)
{
	flush();

	{
		mMatrix[mMatrixMode][mMatIdx[mMatrixMode]] = glm::translate(mMatrix[mMatrixMode][mMatIdx[mMatrixMode]], glm::vec3(x, y, z));
		mMatHash[mMatrixMode]++;
	}
}

void LLRender::scalef(const F32& x, const F32& y, const F32& z)
{
	flush();

	{
		mMatrix[mMatrixMode][mMatIdx[mMatrixMode]] = glm::scale(mMatrix[mMatrixMode][mMatIdx[mMatrixMode]], glm::vec3(x, y, z));
		mMatHash[mMatrixMode]++;
	}
}

void LLRender::ortho(F32 left, F32 right, F32 bottom, F32 top, F32 zNear, F32 zFar)
{
	flush();

	{
		mMatrix[mMatrixMode][mMatIdx[mMatrixMode]] *= glm::ortho(left, right, bottom, top, zNear, zFar);
		mMatHash[mMatrixMode]++;
	}
}

void LLRender::rotatef(const F32& a, const F32& x, const F32& y, const F32& z)
{
	flush();

	{
		mMatrix[mMatrixMode][mMatIdx[mMatrixMode]] = glm::rotate(mMatrix[mMatrixMode][mMatIdx[mMatrixMode]], glm::radians(a), glm::vec3(x, y, z));
		mMatHash[mMatrixMode]++;
	}
}

void LLRender::pushMatrix()
{
	flush();

	{
		if (mMatIdx[mMatrixMode] < LL_MATRIX_STACK_DEPTH - 1)
		{
			mMatrix[mMatrixMode][mMatIdx[mMatrixMode] + 1] = mMatrix[mMatrixMode][mMatIdx[mMatrixMode]];
			++mMatIdx[mMatrixMode];
		}
		else
		{
			LL_WARNS() << "Matrix stack overflow." << LL_ENDL;
		}
	}
}

void LLRender::popMatrix()
{
	flush();
	{
		if (mMatIdx[mMatrixMode] > 0)
		{
			--mMatIdx[mMatrixMode];
			mMatHash[mMatrixMode]++;
		}
		else
		{
			LL_WARNS() << "Matrix stack underflow." << LL_ENDL;
		}
	}
}

void LLRender::loadMatrix(const F32* m)
{
	flush();
	{
		mMatrix[mMatrixMode][mMatIdx[mMatrixMode]] = glm::make_mat4((F32*)m);
		mMatHash[mMatrixMode]++;
	}
}

void LLRender::multMatrix(const F32* m)
{
	flush();
	{
		mMatrix[mMatrixMode][mMatIdx[mMatrixMode]] *= glm::make_mat4(m);
		mMatHash[mMatrixMode]++;
	}
}

void LLRender::matrixMode(eMatrixMode mode)
{
	if (mode == MM_TEXTURE)
	{
		U32 tex_index = gDX.getCurrentTexUnitIndex();
		// the shaders don't actually reference anything beyond texture_matrix0/1 outside of terrain rendering
		llassert(tex_index <= 3);
		mode = eMatrixMode(MM_TEXTURE0 + tex_index);
		if (mode > MM_TEXTURE3)
		{
			// getCurrentTexUnitIndex() can go as high as 32 (LL_NUM_TEXTURE_LAYERS)
			// Large value will result in a crash at mMatrix
			LL_WARNS_ONCE() << "Attempted to assign matrix mode out of bounds: " << mode << LL_ENDL;
			mode = MM_TEXTURE0;
		}
	}

	mMatrixMode = mode;
}

LLRender::eMatrixMode LLRender::getMatrixMode()
{
	if (mMatrixMode >= MM_TEXTURE0 && mMatrixMode <= MM_TEXTURE3)
	{ //always return MM_TEXTURE if current matrix mode points at any texture matrix
		return MM_TEXTURE;
	}

	return mMatrixMode;
}

void LLRender::loadIdentity()
{
	flush();

	{
		llassert_always(mMatrixMode < NUM_MATRIX_MODES);

		mMatrix[mMatrixMode][mMatIdx[mMatrixMode]] = glm::identity<glm::mat4>();
		mMatHash[mMatrixMode]++;
	}
}

const glm::mat4& LLRender::getModelviewMatrix()
{
	return mMatrix[MM_MODELVIEW][mMatIdx[MM_MODELVIEW]];
}

const glm::mat4& LLRender::getProjectionMatrix()
{
	return mMatrix[MM_PROJECTION][mMatIdx[MM_PROJECTION]];
}

void LLRender::translateUI(F32 x, F32 y, F32 z)
{
	if (mUIOffset.empty())
	{
		LL_ERRS() << "Need to push a UI translation frame before offsetting" << LL_ENDL;
	}

	mUIOffset.back().add(LLVector4a(x, y, z));
}

void LLRender::scaleUI(F32 x, F32 y, F32 z)
{
	if (mUIScale.empty())
	{
		LL_ERRS() << "Need to push a UI transformation frame before scaling." << LL_ENDL;
	}

	mUIScale.back().mul(LLVector4a(x, y, z));
}

void LLRender::pushUIMatrix()
{
	if (mUIOffset.empty())
	{
		mUIOffset.emplace_back(0.f);
	}
	else
	{
		mUIOffset.push_back(mUIOffset.back());
	}

	if (mUIScale.empty())
	{
		mUIScale.emplace_back(1.f);
	}
	else
	{
		mUIScale.push_back(mUIScale.back());
	}
}

void LLRender::popUIMatrix()
{
	if (mUIOffset.empty())
	{
		LL_ERRS() << "UI offset stack blown." << LL_ENDL;
	}
	mUIOffset.pop_back();
	mUIScale.pop_back();
}

LLVector3 LLRender::getUITranslation()
{
	if (mUIOffset.empty())
	{
		return LLVector3::zero;
	}

	return LLVector3(mUIOffset.back().getF32ptr());
}

LLVector3 LLRender::getUIScale()
{
	if (mUIScale.empty())
	{
		return LLVector3::all_one;
	}

	return LLVector3(mUIScale.back().getF32ptr());
}


void LLRender::loadUIIdentity()
{
	if (mUIOffset.empty())
	{
		LL_ERRS() << "Need to push UI translation frame before clearing offset." << LL_ENDL;
	}

	mUIOffset.back().clear();
	mUIScale.back().splat(1);
}

void LLRender::setColorWriteMask(bool writeColor, bool writeAlpha)
{
	setColorWriteMask(writeColor, writeColor, writeColor, writeAlpha);
}
void LLRender::setColorWriteMask(bool writeColorR, bool writeColorG, bool writeColorB, bool writeAlpha)
{
	flush();

	if (mCurrColorMask[0] != writeColorR ||
		mCurrColorMask[1] != writeColorG ||
		mCurrColorMask[2] != writeColorB ||
		mCurrColorMask[3] != writeAlpha)
	{
		mCurrColorMask[0] = writeColorR;
		mCurrColorMask[1] = writeColorG;
		mCurrColorMask[2] = writeColorB;
		mCurrColorMask[3] = writeAlpha;

		applyDXBlendState();
	}
}

void LLRender::setSceneBlendType(eBlendType type)
{
	switch (type)
	{
	case BT_ALPHA:
		blendFunc(BF_SOURCE_ALPHA, BF_ONE_MINUS_SOURCE_ALPHA);
		break;
	case BT_ADD:
		blendFunc(BF_ONE, BF_ONE);
		break;
	case BT_ADD_WITH_ALPHA:
		blendFunc(BF_SOURCE_ALPHA, BF_ONE);
		break;
	case BT_MULT:
		blendFunc(BF_DEST_COLOR, BF_ZERO);
		break;
	case BT_MULT_ALPHA:
		blendFunc(BF_DEST_ALPHA, BF_ZERO);
		break;
	case BT_MULT_X2:
		blendFunc(BF_DEST_COLOR, BF_SOURCE_COLOR);
		break;
	case BT_REPLACE:
		blendFunc(BF_ONE, BF_ZERO);
		break;
	default:
		LL_ERRS() << "Unknown Scene Blend Type: " << type << LL_ENDL;
		break;
	}
}

void LLRender::blendFunc(eBlendFactor sfactor, eBlendFactor dfactor)
{
	llassert(sfactor < BF_UNDEF);
	llassert(dfactor < BF_UNDEF);
	if (mCurrBlendColorSFactor != sfactor || mCurrBlendColorDFactor != dfactor ||
		mCurrBlendAlphaSFactor != sfactor || mCurrBlendAlphaDFactor != dfactor)
	{
		mCurrBlendColorSFactor = sfactor;
		mCurrBlendAlphaSFactor = sfactor;
		mCurrBlendColorDFactor = dfactor;
		mCurrBlendAlphaDFactor = dfactor;
		flush();
		applyDXBlendState();
	}
}

void LLRender::blendFunc(eBlendFactor color_sfactor, eBlendFactor color_dfactor,
	eBlendFactor alpha_sfactor, eBlendFactor alpha_dfactor)
{
	llassert(color_sfactor < BF_UNDEF);
	llassert(color_dfactor < BF_UNDEF);
	llassert(alpha_sfactor < BF_UNDEF);
	llassert(alpha_dfactor < BF_UNDEF);

	if (mCurrBlendColorSFactor != color_sfactor || mCurrBlendColorDFactor != color_dfactor ||
		mCurrBlendAlphaSFactor != alpha_sfactor || mCurrBlendAlphaDFactor != alpha_dfactor)
	{
		mCurrBlendColorSFactor = color_sfactor;
		mCurrBlendAlphaSFactor = alpha_sfactor;
		mCurrBlendColorDFactor = color_dfactor;
		mCurrBlendAlphaDFactor = alpha_dfactor;
		flush();

		// DXStateCache's blend state only takes one src/dst pair (matching
		// the 2-factor overload above) - separate color/alpha factors are a
		// documented gap, nothing converted so far calls this overload.
		applyDXBlendState();
	}
}

#ifdef DX_RENDER
void LLRender::applyDXBlendState()
{
	uint8_t write_mask = 0;
	if (mCurrColorMask[0]) write_mask |= D3D11_COLOR_WRITE_ENABLE_RED;
	if (mCurrColorMask[1]) write_mask |= D3D11_COLOR_WRITE_ENABLE_GREEN;
	if (mCurrColorMask[2]) write_mask |= D3D11_COLOR_WRITE_ENABLE_BLUE;
	if (mCurrColorMask[3]) write_mask |= D3D11_COLOR_WRITE_ENABLE_ALPHA;

	bool enabled = DXState::isEnabled(GL_BLEND);
	D3D11_BLEND src = sDXBlendFactor[mCurrBlendColorSFactor];
	D3D11_BLEND dst = sDXBlendFactor[mCurrBlendColorDFactor];
	// S24: passed through for real, not silently dropped - see
	// DXStateCache::getBlendState(). No-op for the 2-factor blendFunc()
	// overload (sets these equal to src/dst); matters for the 4-factor
	// blendFuncSeparate() overload, which needs its own alpha factors.
	D3D11_BLEND alpha_src = sDXBlendFactor[mCurrBlendAlphaSFactor];
	D3D11_BLEND alpha_dst = sDXBlendFactor[mCurrBlendAlphaDFactor];

	ID3D11BlendState* bs = DXStateCache::getBlendState(enabled, src, dst, alpha_src, alpha_dst, write_mask);
	gDXDevice.getContext()->OMSetBlendState(bs, nullptr, 0xFFFFFFFF);
}

void LLRender::applyDXRasterizerState()
{
	bool offset_enabled = DXState::isEnabled(GL_POLYGON_OFFSET_FILL) || DXState::isEnabled(GL_POLYGON_OFFSET_LINE);
	ID3D11RasterizerState* rs = DXStateCache::getRasterizerState(
		DXState::isEnabled(GL_CULL_FACE),
		DXState::isEnabled(GL_SCISSOR_TEST),
		DXState::isEnabled(GL_DEPTH_CLAMP),
		offset_enabled ? mCurrPolygonOffsetFactor : 0.f,
		offset_enabled ? mCurrPolygonOffsetUnits : 0.f,
		DXStateCache::sWireframeScopeActive,
		DXState::getCullFace() == GL_FRONT);
	gDXDevice.getContext()->RSSetState(rs);
}
#endif

void LLRender::cullFace(LLGLenum face)
{
	DXState::setCullFace(face);
	applyDXRasterizerState();
}

void LLRender::setPolygonOffset(F32 factor, F32 units)
{
	mCurrPolygonOffsetFactor = factor;
	mCurrPolygonOffsetUnits = units;
	applyDXRasterizerState();
}

LLTexUnit* LLRender::getTexUnit(U32 index)
{
	if (index < mTexUnits.size())
	{
		return &mTexUnits[index];
	}
	else
	{
		LL_DEBUGS() << "Non-existing texture unit layer requested: " << index << LL_ENDL;
		return &mDummyTexUnit;
	}
}

LLLightState* LLRender::getLight(U32 index)
{
	if (index < mLightState.size())
	{
		return &mLightState[index];
	}

	return NULL;
}

void LLRender::setAmbientLightColor(const LLColor4& color)
{
	if (color != mAmbientLightColor)
	{
		++mLightHash;
		mAmbientLightColor = color;
	}
}

bool LLRender::verifyTexUnitActive(U32 unitToVerify)
{
	if (mCurrTextureUnitIndex == unitToVerify)
	{
		return true;
	}
	else
	{
		LL_WARNS() << "TexUnit currently active: " << mCurrTextureUnitIndex << " (expecting " << unitToVerify << ")" << LL_ENDL;
		return false;
	}
}

void LLRender::clearErrors()
{
	#if LL_DEBUG_GL
	while (glGetError())
	{
		//loop until no more error flags left
	}
	#endif
}

void LLRender::beginList(std::list<LLVertexBufferData>* list)
{
	if (sBufferDataList)
	{
		LL_ERRS() << "beginList called while another list is open." << LL_ENDL;
	}
	llassert(LLHLSLShader::sCurBoundShaderPtr == &gUIProgram);
	flush();
	sBufferDataList = list;
}

void LLRender::endList()
{
	if (sBufferDataList)
	{
		flush();
		sBufferDataList = nullptr;
	}
	else
	{
		llassert(false); // endList called without an open list
	}
}

bool LLRender::isRecording() const
{
	return sBufferDataList != nullptr;
}

void LLRender::begin(const GLuint& mode)
{
	if (mode != mMode)
	{
		if (mMode == LLRender::LINES ||
			mMode == LLRender::TRIANGLES ||
			mMode == LLRender::POINTS)
		{
			flush();
		}
		else if (mCount != 0)
		{
			LL_ERRS() << "gDX.begin() called redundantly." << LL_ENDL;
		}

		mMode = mode;
	}
}

void LLRender::end()
{
	if (mCount == 0)
	{
		return;
		//IMM_ERRS << "GL begin and end called with no vertices specified." << LL_ENDL;
	}

	if ((mMode != LLRender::LINES &&
		mMode != LLRender::TRIANGLES &&
		mMode != LLRender::POINTS) ||
		mCount > 2048)
	{
		flush();
	}
}

void LLRender::flush()
{
	if (mCount == 0)
	{
		return;
	}

	// Fast pointer load
	LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr;

	// Graceful handling: no assert, no crash
	if (LL_UNLIKELY(shader == nullptr))
	{
		static U32 warn_count = 0;
		if (warn_count < 5)
		{
			LL_WARNS("Render")
				<< "flush() called with no bound shader. Dropping batch. mode="
				<< mMode << " count=" << mCount
				<< LL_ENDL;
			++warn_count;
		}

		// Drop the batch safely
		mCount = 0;
		resetStriders(0);
		return;
	}

	// UI stats
	if (!mUIOffset.empty())
	{
		sUICalls++;
		sUIVerts += mCount;
	}

	// Store locally to avoid re-entrancy issues
	U32 count = mCount;

	// Fix incomplete primitives
	if (mMode == LLRender::TRIANGLES && (count % 3))
	{
		LL_WARNS("Render") << "Incomplete triangle requested." << LL_ENDL;
		count -= (count % 3);
	}
	else if (mMode == LLRender::LINES && (count % 2))
	{
		LL_WARNS("Render") << "Incomplete line requested." << LL_ENDL;
		count -= (count % 2);
	}

	U32 draw_mode = mMode;
#ifdef DX_RENDER
	// S24: D3D11 has no LINE_LOOP topology (mapped to
	// D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED in llvertexbuffer.cpp's sDXMode[]
	// table). A closed line loop is just a line strip with the first vertex
	// duplicated onto the end, done here at the shared immediate-mode
	// chokepoint so every caller benefits. count+1 is safe - vertex3f()'s
	// own overflow guard keeps count within the striders' range with room
	// to spare for one more.
	if (mMode == LLRender::LINE_LOOP && count > 0)
	{
		mVerticesp[count] = mVerticesp[0];
		mColorsp[count] = mColorsp[0];
		mTexcoordsp[count] = mTexcoordsp[0];
		++count;
		draw_mode = LLRender::LINE_STRIP;
	}
	// S24: D3D11 has no TRIANGLE_FAN topology either, same as LINE_LOOP
	// above. Real callers: LLCone::render() (cone sides for translate-arrow
	// gizmos), lltracker.cpp's beacon circle, llreflectionmap.cpp's debug
	// rings.
	//
	// Unlike LINE_LOOP (one extra vertex, appended in place), a fan-to-list
	// expansion reorders and grows the data - triangle (v0, vi, vi+1) for
	// every i in [1, count-2], where v0 is the fan's shared center vertex -
	// so it can't be done in place against the same indices it's reading
	// from. Copy the source fan verts out first, then overwrite the striders
	// from index 0. Capped against the striders' fixed [0,4095] range (see
	// resetStriders()'s comment) - real callers here are all small
	// decorative/gizmo geometry (cone sides, beacon circles), nowhere close
	// to that limit; a fan large enough to hit it drops its batch with a
	// warning instead of overflowing.
	else if (mMode == LLRender::TRIANGLE_FAN && count >= 3)
	{
		U32 expanded_count = (count - 2) * 3;
		if (expanded_count <= 4095)
		{
			std::vector<LLVector4a> src_verts(count);
			std::vector<LLColor4U> src_colors(count);
			std::vector<LLVector2> src_uvs(count);
			for (U32 i = 0; i < count; ++i)
			{
				src_verts[i] = mVerticesp[i];
				src_colors[i] = mColorsp[i];
				src_uvs[i] = mTexcoordsp[i];
			}

			U32 out = 0;
			for (U32 i = 1; i + 1 < count; ++i)
			{
				mVerticesp[out] = src_verts[0];   mColorsp[out] = src_colors[0];   mTexcoordsp[out] = src_uvs[0];   ++out;
				mVerticesp[out] = src_verts[i];   mColorsp[out] = src_colors[i];   mTexcoordsp[out] = src_uvs[i];   ++out;
				mVerticesp[out] = src_verts[i+1]; mColorsp[out] = src_colors[i+1]; mTexcoordsp[out] = src_uvs[i+1]; ++out;
			}
			count = out;
			draw_mode = LLRender::TRIANGLES;
		}
		else
		{
			LL_WARNS("Render") << "TRIANGLE_FAN too large to expand for DX_RENDER (count=" << count << "), dropping batch." << LL_ENDL;
			mCount = 0;
			resetStriders(0);
			return;
		}
	}
#endif

	mCount = 0;

	if (!mBuffer)
	{
		LL_ERRS("Render") << "flush() called outside main rendering thread" << LL_ENDL;
		return;
	}

	// Normal fast path
	LLVertexBuffer* vb;
	U32 attribute_mask = shader->mAttributeMask;

	if (sBufferDataList)
	{
		vb = genBuffer(attribute_mask, count);
		auto& buffer_data = sBufferDataList->emplace_back(
			vb,
			draw_mode,
			count,
			gDX.getTexUnit(0)->mCurrTexture,
			mMatrix[MM_MODELVIEW][mMatIdx[MM_MODELVIEW]],
			mMatrix[MM_PROJECTION][mMatIdx[MM_PROJECTION]],
			mMatrix[MM_TEXTURE0][mMatIdx[MM_TEXTURE0]]
		);
#ifdef DX_RENDER
		// S24: see LLVertexBufferData::mDXImage's comment.
		buffer_data.mDXImage = gDX.getTexUnit(0)->mCurrBoundImageGL;
		// S24: see LLVertexBufferData::mDXShader's comment.
		buffer_data.mDXShader = shader;
#endif
	}
	else
	{
		vb = bufferfromCache(attribute_mask, count);
	}

#ifdef DX_RENDER
	// S24: the reverse-direction link - gDXUIBatch's call sites already
	// flush THIS queue before submitting to it, but nothing previously
	// flushed gDXUIBatch's pending queue before THIS draw, which could
	// corrupt paint order.
	gDXUIBatch.flushPending();
#endif
	drawBuffer(vb, draw_mode, count);
	resetStriders(count);
}

LLVertexBuffer* LLRender::bufferfromCache(U32 attribute_mask, U32 count)
{
	LLVertexBuffer* vb = nullptr;
	HBXXH64 hash;

	{

		hash.update((U8*)mVerticesp.get(), count * sizeof(LLVector4a));
		if (attribute_mask & LLVertexBuffer::MAP_TEXCOORD0)
		{
			hash.update((U8*)mTexcoordsp.get(), count * sizeof(LLVector2));
		}

		if (attribute_mask & LLVertexBuffer::MAP_COLOR)
		{
			hash.update((U8*)mColorsp.get(), count * sizeof(LLColor4U));
		}

		hash.finalize();
	}

	U64 vhash = hash.digest();

	// check the VB cache before making a new vertex buffer
	// This is a giant hack to deal with (mostly) our terrible UI rendering code
	// that was built on top of OpenGL immediate mode.  Huge performance wins
	// can be had by not uploading geometry to VRAM unless absolutely necessary.
	// Most of our usage of the "immediate mode" style draw calls is actually
	// sending the same geometry over and over again.
	// To leverage this, we maintain a running hash of the vertex stream being
	// built up before a flush, and then check that hash against a VB
	// cache just before creating a vertex buffer in VRAM
	std::unordered_map<U64, LLVBCache>::iterator cache = sVBCache.find(vhash);

	if (cache != sVBCache.end())
	{
		// cache hit, just use the cached buffer
		vb = cache->second.vb;
		cache->second.touched = std::chrono::steady_clock::now();
	}
	else
	{
		vb = genBuffer(attribute_mask, count);

		sVBCache[vhash] = { vb , std::chrono::steady_clock::now() };

		static U32 miss_count = 0;
		miss_count++;
		if (miss_count > 1024)
		{
			miss_count = 0;
			auto now = std::chrono::steady_clock::now();

			using namespace std::chrono_literals;
			// every 1024 misses, clean the cache of any VBs that haven't been touched in the last second
			for (std::unordered_map<U64, LLVBCache>::iterator iter = sVBCache.begin(); iter != sVBCache.end(); )
			{
				if (now - iter->second.touched > 1s)
				{
					iter = sVBCache.erase(iter);
				}
				else
				{
					++iter;
				}
			}
		}
	}
	return vb;
}

LLVertexBuffer* LLRender::genBuffer(U32 attribute_mask, S32 count)
{
	LLVertexBuffer* vb = new LLVertexBuffer(attribute_mask);
	vb->allocateBuffer(count, 0);

	vb->setBuffer();

	vb->setPositionData(mVerticesp.get());

	if (attribute_mask & LLVertexBuffer::MAP_TEXCOORD0)
	{
		vb->setTexCoord0Data(mTexcoordsp.get());
	}

	if (attribute_mask & LLVertexBuffer::MAP_COLOR)
	{
		vb->setColorData(mColorsp.get());
	}

#if LL_DARWIN
	vb->unmapBuffer();
#endif
	vb->unbind();

	return vb;
}

void LLRender::drawBuffer(LLVertexBuffer* vb, U32 mode, S32 count)
{
	vb->setBuffer();

	vb->drawArrays(mode, 0, count);
}

void LLRender::resetStriders(S32 count)
{
	mVerticesp[0] = mVerticesp[count];
	mTexcoordsp[0] = mTexcoordsp[count];
	mColorsp[0] = mColorsp[count];

	mCount = 0;
}

void LLRender::vertex3f(const F32& x, const F32& y, const F32& z)
{
	//the range of mVerticesp, mColorsp and mTexcoordsp is [0, 4095]
	if (mCount > 2048)
	{ //break when buffer gets reasonably full to keep GL command buffers happy and avoid overflow below
		switch (mMode)
		{
		case LLRender::POINTS: flush(); break;
		case LLRender::TRIANGLES: if (mCount % 3 == 0) flush(); break;
		case LLRender::LINES: if (mCount % 2 == 0) flush(); break;
		}
	}

	if (mCount > 4094)
	{
		//  LL_WARNS() << "GL immediate mode overflow.  Some geometry not drawn." << LL_ENDL;
		return;
	}

	LLVector4a vert(x, y, z);
	transform(vert);
	mVerticesp[mCount] = vert;

	mCount++;
	mVerticesp[mCount] = mVerticesp[mCount - 1];
	mColorsp[mCount] = mColorsp[mCount - 1];
	mTexcoordsp[mCount] = mTexcoordsp[mCount - 1];
}

void LLRender::transform(LLVector3& vert)
{
	if (!mUIOffset.empty())
	{
		vert += LLVector3(mUIOffset.back().getF32ptr());
		vert *= LLVector3(mUIScale.back().getF32ptr());
	}
}

void LLRender::transform(LLVector4a& vert)
{
	if (!mUIOffset.empty())
	{
		vert.add(mUIOffset.back());
		vert.mul(mUIScale.back());
	}
}

void LLRender::untransform(LLVector3& vert)
{
	if (!mUIOffset.empty())
	{
		vert /= LLVector3(mUIScale.back().getF32ptr());
		vert -= LLVector3(mUIOffset.back().getF32ptr());
	}
}

void LLRender::batchTransform(LLVector4a* verts, U32 vert_count)
{
	if (!mUIOffset.empty())
	{
		const LLVector4a& offset = mUIOffset.back();
		const LLVector4a& scale = mUIScale.back();

		for (U32 i = 0; i < vert_count; ++i)
		{
			verts[i].add(offset);
			verts[i].mul(scale);
		}
	}
}

void LLRender::vertexBatchPreTransformed(const std::vector<LLVector4a>& verts)
{
	vertexBatchPreTransformed(verts.data(), narrow(verts.size()));
}

void LLRender::vertexBatchPreTransformed(const LLVector4a* verts, S32 vert_count)
{
	if (mCount + vert_count > 4094)
	{
		//  LL_WARNS() << "GL immediate mode overflow.  Some geometry not drawn." << LL_ENDL;
		return;
	}

	for (S32 i = 0; i < vert_count; i++)
	{
		mVerticesp[mCount] = verts[i];

		mCount++;
		mTexcoordsp[mCount] = mTexcoordsp[mCount - 1];
		mColorsp[mCount] = mColorsp[mCount - 1];
	}

	if (mCount > 0) // ND: Guard against crashes if mCount is zero, yes it can happen
		mVerticesp[mCount] = mVerticesp[mCount - 1];
}

void LLRender::vertexBatchPreTransformed(const LLVector4a* verts, const LLVector2* uvs, S32 vert_count)
{
	if (mCount + vert_count > 4094)
	{
		//  LL_WARNS() << "GL immediate mode overflow.  Some geometry not drawn." << LL_ENDL;
		return;
	}

	for (S32 i = 0; i < vert_count; i++)
	{
		mVerticesp[mCount] = verts[i];
		mTexcoordsp[mCount] = uvs[i];

		mCount++;
		mColorsp[mCount] = mColorsp[mCount - 1];
	}

	if (mCount > 0)
	{
		mVerticesp[mCount] = mVerticesp[mCount - 1];
		mTexcoordsp[mCount] = mTexcoordsp[mCount - 1];
	}
}

void LLRender::vertexBatchPreTransformed(const LLVector4a* verts, const LLVector2* uvs, const LLColor4U* colors, S32 vert_count)
{
	if (mCount + vert_count > 4094)
	{
		//  LL_WARNS() << "GL immediate mode overflow.  Some geometry not drawn." << LL_ENDL;
		return;
	}

	for (S32 i = 0; i < vert_count; i++)
	{
		mVerticesp[mCount] = verts[i];
		mTexcoordsp[mCount] = uvs[i];
		mColorsp[mCount] = colors[i];

		mCount++;
	}

	if (mCount > 0)
	{
		mVerticesp[mCount] = mVerticesp[mCount - 1];
		mTexcoordsp[mCount] = mTexcoordsp[mCount - 1];
		mColorsp[mCount] = mColorsp[mCount - 1];
	}
}

void LLRender::vertex2i(const GLint& x, const GLint& y)
{
	vertex3f((F32)x, (F32)y, 0);
}

void LLRender::vertex2f(const F32& x, const F32& y)
{
	vertex3f(x, y, 0);
}

void LLRender::vertex2fv(const F32* v)
{
	vertex3f(v[0], v[1], 0);
}

void LLRender::vertex3fv(const F32* v)
{
	vertex3f(v[0], v[1], v[2]);
}

void LLRender::texCoord2f(const F32& x, const F32& y)
{
	mTexcoordsp[mCount] = LLVector2(x, y);
}

void LLRender::texCoord2i(const GLint& x, const GLint& y)
{
	texCoord2f((F32)x, (F32)y);
}

void LLRender::texCoord2fv(const F32* tc)
{
	texCoord2f(tc[0], tc[1]);
}

void LLRender::color4ub(const GLubyte& r, const GLubyte& g, const GLubyte& b, const GLubyte& a)
{
	if (!LLHLSLShader::sCurBoundShaderPtr || LLHLSLShader::sCurBoundShaderPtr->mAttributeMask & LLVertexBuffer::MAP_COLOR)
	{
		mColorsp[mCount] = LLColor4U(r, g, b, a);
	}
	else
	{ //not using shaders or shader reads color from a uniform
		diffuseColor4ub(r, g, b, a);
	}
}
void LLRender::color4ubv(const GLubyte* c)
{
	color4ub(c[0], c[1], c[2], c[3]);
}

void LLRender::color4f(const F32& r, const F32& g, const F32& b, const F32& a)
{
	color4ub((GLubyte)(llclamp(r, 0.f, 1.f) * 255),
		(GLubyte)(llclamp(g, 0.f, 1.f) * 255),
		(GLubyte)(llclamp(b, 0.f, 1.f) * 255),
		(GLubyte)(llclamp(a, 0.f, 1.f) * 255));
}

void LLRender::color4fv(const F32* c)
{
	color4f(c[0], c[1], c[2], c[3]);
}

void LLRender::color3f(const F32& r, const F32& g, const F32& b)
{
	color4f(r, g, b, 1);
}

void LLRender::color3fv(const F32* c)
{
	color4f(c[0], c[1], c[2], 1);
}

void LLRender::diffuseColor3f(F32 r, F32 g, F32 b)
{
	LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr;
	llassert(shader != NULL);

	if (shader)
	{
		shader->uniform4f(LLShaderMgr::DIFFUSE_COLOR, r, g, b, 1.f);
	}
}

void LLRender::diffuseColor3fv(const F32* c)
{
	LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr;
	llassert(shader != NULL);

	if (shader)
	{
		shader->uniform4f(LLShaderMgr::DIFFUSE_COLOR, c[0], c[1], c[2], 1.f);
	}
}

void LLRender::diffuseColor4f(F32 r, F32 g, F32 b, F32 a)
{
	LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr;
	llassert(shader != NULL);

	if (shader)
	{
		shader->uniform4f(LLShaderMgr::DIFFUSE_COLOR, r, g, b, a);
	}
}

void LLRender::diffuseColor4fv(const F32* c)
{
	LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr;
	llassert(shader != NULL);

	if (shader)
	{
		shader->uniform4fv(LLShaderMgr::DIFFUSE_COLOR, 1, c);
	}
}

void LLRender::diffuseColor4ubv(const U8* c)
{
	LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr;
	llassert(shader != NULL);

	if (shader)
	{
		shader->uniform4f(LLShaderMgr::DIFFUSE_COLOR, c[0] / 255.f, c[1] / 255.f, c[2] / 255.f, c[3] / 255.f);
	}
}

void LLRender::diffuseColor4ub(U8 r, U8 g, U8 b, U8 a)
{
	LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr;
	llassert(shader != NULL);

	if (shader)
	{
		shader->uniform4f(LLShaderMgr::DIFFUSE_COLOR, r / 255.f, g / 255.f, b / 255.f, a / 255.f);
	}
}

void LLRender::debugTexUnits(void)
{
	LL_INFOS("TextureUnit") << "Active TexUnit: " << mCurrTextureUnitIndex << LL_ENDL;
	std::string active_enabled = "false";
	for (U32 i = 0; i < mTexUnits.size(); i++)
	{
		if (getTexUnit(i)->mCurrTexType != LLTexUnit::TT_NONE)
		{
			if (i == mCurrTextureUnitIndex) active_enabled = "true";
			LL_INFOS("TextureUnit") << "TexUnit: " << i << " Enabled" << LL_ENDL;
			LL_INFOS("TextureUnit") << "Enabled As: ";
			switch (getTexUnit(i)->mCurrTexType)
			{
			case LLTexUnit::TT_TEXTURE:
				LL_CONT << "Texture 2D";
				break;
			case LLTexUnit::TT_RECT_TEXTURE:
				LL_CONT << "Texture Rectangle";
				break;
			case LLTexUnit::TT_CUBE_MAP:
				LL_CONT << "Cube Map";
				break;
			default:
				LL_CONT << "ARGH!!! NONE!";
				break;
			}
			LL_CONT << ", Texture Bound: " << getTexUnit(i)->mCurrTexture << LL_ENDL;
		}
	}
	LL_INFOS("TextureUnit") << "Active TexUnit Enabled : " << active_enabled << LL_ENDL;
}

glm::mat4 get_current_modelview()
{
	return glm::make_mat4(gGLModelView);
}

glm::mat4 get_current_projection()
{
	return glm::make_mat4(gGLProjection);
}

glm::mat4 get_last_modelview()
{
	return glm::make_mat4(gGLLastModelView);
}

glm::mat4 get_last_projection()
{
	return glm::make_mat4(gGLLastProjection);
}

void copy_matrix(const glm::mat4& src, F32* dst)
{
	auto matp = glm::value_ptr(src);
	for (U32 i = 0; i < 16; i++)
	{
		dst[i] = matp[i];
	}
}

void set_current_modelview(const glm::mat4& mat)
{
	copy_matrix(mat, gGLModelView);
}

void set_current_projection(const glm::mat4& mat)
{
	copy_matrix(mat, gGLProjection);
}

void set_last_modelview(const glm::mat4& mat)
{
	copy_matrix(mat, gGLLastModelView);
}

void set_last_projection(const glm::mat4& mat)
{
	copy_matrix(mat, gGLLastProjection);
}

// S24: be careful changing this - it indirectly affects projected lights
// and some textures; works correctly as-is.
glm::vec3 mul_mat4_vec3(const glm::mat4& mat, const glm::vec3& vec)
{
	glm::vec4 vec4(vec, 1.0f); // Convert vec3 to vec4 with w = 1.0
	glm::vec4 result = mat * vec4; // Multiply matrix with vec4

	LLVector4a res;
	res.load3(glm::value_ptr(glm::vec3(result / result.w))); // Normalize by w and convert to vec3

	return glm::make_vec3(res.getF32ptr());
}