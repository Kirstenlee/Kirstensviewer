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
#include "llcubemap.h"
#include "llcubemaparray.h"
#include "llglslshader.h"
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
#endif

//#include <algorithm>
extern void APIENTRY gl_debug_callback(GLenum source,
	GLenum type,
	GLuint id,
	GLenum severity,
	GLsizei length,
	const GLchar* message,
	GLvoid* userParam)
	;

thread_local LLRender gGL;

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

static const GLenum sGLTextureType[] =
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

static const GLenum sGLBlendFactor[] =
{
	GL_ONE,
	GL_ZERO,
	GL_DST_COLOR,
	GL_SRC_COLOR,
	GL_ONE_MINUS_DST_COLOR,
	GL_ONE_MINUS_SRC_COLOR,
	GL_DST_ALPHA,
	GL_SRC_ALPHA,
	GL_ONE_MINUS_DST_ALPHA,
	GL_ONE_MINUS_SRC_ALPHA,

	GL_ZERO // 'BF_UNDEF'
};

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
// S24 perf
void LLTexUnit::refreshState(void)
{
#ifdef DX_RENDER
	// S24 (DX_RENDER, 2026-07-25): was completely unguarded raw
	// glActiveTexture()/glBindTexture() - reachable via LLRender::refreshState()
	// (called for every one of mTexUnits, ~32 units) <- LLViewerWindow::
	// checkSettings() whenever mStatesDirty is set (real graphics-settings-
	// change path, not exercised by a plain login-screen test, but a genuine
	// future crash/link landmine). Mirrors activate()/enable()/
	// setTextureAddressMode()/setTextureFilteringOption()'s existing DX_RENDER
	// no-op pattern: mCurrTexture/mCurrTexType aren't meaningfully tracked
	// under DX_RENDER (every DX_RENDER bind path sets the real D3D11 SRV/
	// sampler directly at bind time, nothing "cached" here to re-establish),
	// so there's nothing for this GL-specific "re-bind the last texture"
	// idiom to do.
	return;
#else
	// We set dirty to true so that the tex unit knows to ignore caching
	// and we reset the cached tex unit state

	// Only flush the pipeline if absolutely necessary (profiling can determine this)
	gGL.flush();

	// Activate the texture unit
	glActiveTexture(GL_TEXTURE0 + mIndex);

	// Bind the appropriate texture type or default to 0
	glBindTexture(
		(mCurrTexType != TT_NONE) ? sGLTextureType[mCurrTexType] : GL_TEXTURE_2D,
		mCurrTexture
	);
#endif // DX_RENDER
}

// S24 perf
void LLTexUnit::activate(void)
{
#ifdef DX_RENDER
	// Texture binding (LLTexUnit -> DXTexture/DXSampler + PSSetShaderResources/
	// PSSetSamplers) isn't wired up yet - stubbed as a safe no-op rather than
	// calling glActiveTexture with no GL context. See stage 3 plan memory for
	// the follow-up task; LLDrawPoolSimple's DX_RENDER branch renders with no
	// texture bound until then.
#else
	// Early exit if mIndex is invalid or the state doesn�t need updating
	if (mIndex < 0 || ((S32)gGL.mCurrTextureUnitIndex == mIndex && !gGL.mDirty)) return;

	// Flush pipeline only if necessary (profiling might refine this further)
	gGL.flush();

	// Activate the texture unit and update the current index
	glActiveTexture(GL_TEXTURE0 + mIndex);
	gGL.mCurrTextureUnitIndex = mIndex;
#endif // DX_RENDER
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

// S24 perf
void LLTexUnit::enable(eTextureType type)
{
#ifdef DX_RENDER
	// Mirrors activate()'s DX_RENDER no-op (see its comment) - texture
	// binding under DX_RENDER goes through bindFast()/the now-fixed bind()
	// overloads, which don't need or update mCurrTexType bookkeeping. Left
	// un-set (stays TT_NONE forever, its constructed default) rather than
	// tracked here, since tracking it would require a matching disable()/
	// unbind() DX_RENDER story for no actual benefit - nothing under
	// DX_RENDER reads mCurrTexType except GL-only code paths.
#else
	// Early exit if mIndex is invalid or type is TT_NONE
	if (mIndex < 0 || type == TT_NONE) return;

	// Only proceed if the texture type or dirty state needs updating
	if (mCurrTexType != type || gGL.mDirty)
	{
		activate();

		// Disable previous texture type if necessary
		if (mCurrTexType != TT_NONE && !gGL.mDirty)
		{
			disable();
		}

		// Update the current texture type
		mCurrTexType = type;

		// Flush pipeline if required (review necessity via profiling)
		gGL.flush();
	}
#endif // DX_RENDER
}

// S24 perf
void LLTexUnit::disable(void)
{
#ifdef DX_RENDER
	// mCurrTexType is never set away from its constructed default
	// (TT_NONE) under DX_RENDER - enable()'s DX_RENDER branch is a no-op
	// that never updates it (see its own comment). That means the
	// mCurrTexType == TT_NONE check below would ALWAYS early-return here,
	// so disable() would never actually reach unbind() - silently
	// skipping the white-texture-fallback fix for every one of this
	// function's many direct callers (draw pools calling
	// gGL.getTexUnit(n)->disable() to "turn off" a channel between
	// batches). Bypass the stale bookkeeping and unbind unconditionally.
	unbind(LLTexUnit::TT_TEXTURE);
#else
	// Early exit if mIndex is invalid or the current texture type is already none
	if (mIndex < 0 || mCurrTexType == TT_NONE) return;

	// Unbind the current texture type and reset to none
	unbind(mCurrTexType);
	mCurrTexType = TT_NONE;
#endif // DX_RENDER
}

// S24 perf
void LLTexUnit::bindFast(LLTexture* texture)
{
#ifdef DX_RENDER
	// Binds this LLImageGL's DXTexture (if uploaded - see
	// LLImageGL::setImage()'s DX_RENDER branch) plus a sampler matching its
	// address-mode/filter settings, to pixel-shader slot mIndex - matching
	// the register(t0)/register(s0) convention the ported .hlsl files use
	// (same texture-unit-index numbering GL uses).
	// S24 (2026-07-22): this used to bind a null SRV when not yet uploaded,
	// with a comment claiming that "renders as if nothing were sampled" -
	// that's wrong; HLSL samples a null SRV as (0,0,0,0), not a no-op, so
	// any shader doing `vertex_color * diffuseMap.Sample(...)` (essentially
	// all 2D UI, per uiF.hlsl) would render fully transparent instead of
	// unaffected. Same bug class as the original unbind()/unbindFast() fix -
	// see getWhiteTextureSRV()'s comment. Fixed the same way: fall back to
	// white rather than null.
	LLImageGL* gl_tex = texture->getGLTexture();
	// S24 (task #223): captured BEFORE mCurrBoundImageGL is overwritten below -
	// see this function's flush-condition comment further down for why this
	// is needed in addition to the mCurrDXSRV comparison.
	bool bound_image_changed = (mCurrBoundImageGL != gl_tex);
	mCurrBoundImageGL = gl_tex;

	// S24 (2026-08-06): this whole function used to silently do NOTHING at
	// all when gl_tex was null (e.g. a terrain detail texture still
	// streaming in - a real, expected transient state, not an error) -
	// unlike the inner "srv is null" case a few lines below (already
	// fixed to fall back to white), this OUTER null check left whatever a
	// completely unrelated PREVIOUS draw call last bound at this same
	// slot resident and untouched. Confirmed via the D3D11 debug layer
	// log (context='Deferred Terrain Shader', "Slot 2... no Sampler
	// bound" - meaning bindFast() never even reached PSSetSamplers for
	// that slot this frame) and matching user report: ground terrain
	// showing an identifiable OTHER scene object's texture ("seaweed
	// texture from a nearby object"). Same bug class as the "leaf/dirt
	// texture cycling" PBR chokepoint fix earlier this session, just one
	// layer further out - always bind SOMETHING (real texture or white
	// fallback), never leave a slot's previous binding stale.
	ID3D11ShaderResourceView* srv = gl_tex ? gl_tex->mDXTexture.getSRV() : nullptr;
	if (!srv)
	{
		srv = getWhiteTextureSRV();
	}
	// S24 (2026-07-23): same missing-flush bug as bind(LLImageGL*) -
	// see its comment. Without this, pending batched vertices pushed
	// under a previous texture get silently redrawn with whatever
	// texture this call switches to.
	// S24 (task #223): mCurrDXSRV alone is NOT a safe "did the texture
	// change" signal - it's a raw ID3D11ShaderResourceView* address, and
	// DXTexture::scaleDown() (the real mechanism behind VRAM-pressure
	// downscaling, dxrender/resources/DXTexture.cpp) Releases the old SRV
	// and creates a brand new one every time a resident texture gets
	// downscaled under pressure. A freed COM object's address is eligible
	// for immediate reuse by the very next unrelated CreateShaderResourceView
	// call - if that happens to land on the same address, this comparison
	// would wrongly read "same texture as last bind, skip the flush" for a
	// completely different texture, and any vertices still pending in the
	// batch from the ACTUAL previous texture render with whatever texture
	// ends up bound next instead - the exact "wrong texture on the wrong
	// geometry" bug class already fixed once above for a missing-flush-
	// entirely case (see this function's own comment a few lines up). Under
	// real VRAM-pressure churn (many scaleDown() calls in quick succession,
	// exactly when the user reported "textures blitting/cycling/different
	// colours") this address-reuse coincidence becomes a live risk, not just
	// theoretical. bound_image_changed (captured above, against the stable
	// LLImageGL object identity - scaleDown() never destroys that wrapper,
	// only swaps the DXTexture's internal raw pointers) catches this
	// deterministically regardless of address reuse.
	if (bound_image_changed || mCurrDXSRV != (void*)srv)
	{
		gGL.flush();
		// S24 (2026-08-16): also flush gDXUIBatch's separate pending queue -
		// same missing-flush hazard. See DXUIBatch.h's top comment.
		gDXUIBatch.flushPending();
		mCurrDXSRV = (void*)srv;
	}
	// gl_tex==nullptr has no address-mode/filter-option to read - TAM_WRAP/
	// TFO_BILINEAR are this codebase's established default (matches
	// LLViewerTexture's own default construction elsewhere).
	ID3D11SamplerState* sampler = gl_tex
		? DXSampler::getOrCreate((int)gl_tex->getAddressMode(), (int)gl_tex->getFilteringOption())
		: DXSampler::getOrCreate((int)LLTexUnit::TAM_WRAP, (int)LLTexUnit::TFO_BILINEAR);

	gDXDevice.getContext()->PSSetShaderResources(mIndex, 1, &srv);
	// S24 (2026-08-04): D3D11 caps pixel-shader sampler slots at 16
	// (s0-s15) but SRV/texture slots go up to 128 - a shader with more
	// than 16 distinct textures (e.g. pbrterrainF.hlsl's 4-detail-layer
	// PBR terrain with HDR Emissive enabled) can legitimately bind a
	// texture at mIndex>=16 while having nowhere for a matching sampler
	// register to exist (any HLSL declaring register(s16)+ simply fails
	// to compile). Root cause of a real crash: that compile failure left
	// the shader's mDXVertexShader/mDXPixelShader null, and a later
	// unconditional bind() call hit LLGLSLShader::bind()'s
	// mDXVertexShader.getVS()!=nullptr ASSERT. The HLSL side is fixed
	// (see pbrterrainF.hlsl) by sharing an existing sampler register
	// instead of declaring a 17th one, but this guard is the general,
	// permanent fix - skip the now-meaningless PSSetSamplers call for
	// any texture bound past slot 15 rather than passing an invalid
	// StartSlot to the API.
	if (mIndex < 16)
	{
		gDXDevice.getContext()->PSSetSamplers(mIndex, 1, &sampler);
	}
#else
	LLImageGL* gl_tex = texture->getGLTexture();

	// Ensure texture is active
	texture->setActive();

	// Activate the correct texture unit
	glActiveTexture(GL_TEXTURE0 + mIndex);
	gGL.mCurrTextureUnitIndex = mIndex;

	// Retrieve texture name
	mCurrTexture = gl_tex->getTexName();

	// If texture is missing, handle updates immediately
	if (!mCurrTexture)
	{
		LL_PROFILE_ZONE_NAMED("MISSING TEXTURE");

		texture->forceImmediateUpdate();
		gl_tex->forceUpdateBindStats();
		texture->bindDefaultImage(mIndex);
		mCurrTexture = gl_tex->getTexName(); // Retrieve after regeneration
	}

	// Bind texture to its target
	glBindTexture(sGLTextureType[gl_tex->getTarget()], mCurrTexture);

	// Update mipmap status
	mHasMipMaps = gl_tex->mHasMipMaps;
#endif // DX_RENDER
}

// S24 Perf
bool LLTexUnit::bind(LLTexture* texture, bool for_rendering, bool forceBind)
{
#ifdef DX_RENDER
	// Same chokepoint shape as several stage-5 pool conversions found:
	// enableTexture()-derived channels are already safe here (they come
	// back as -1, and getTexUnit() maps out-of-range indices to a dummy
	// unit whose mIndex is also -1, caught by the check below) - but a
	// *hardcoded* valid unit index (e.g. gGL.getTexUnit(0)->bind(tex),
	// used by several converted pools for their "no per-material channel
	// registration yet" fallback) bypasses that guard and would otherwise
	// reach the raw glBindTexture() call below with no GL context behind
	// it. Delegate to the already-DX-safe bindFast() instead.
	if (mIndex < 0 || !texture) return false;
	bindFast(texture);
	return true;
#else
	if (mIndex < 0 || !texture) return false;

	gGL.flush();

	LLImageGL* gl_tex = texture->getGLTexture();
	if (!gl_tex) return false;

	GLuint tex_name = gl_tex->getTexName();
	if (!tex_name)
	{
		texture->forceImmediateUpdate();
		gl_tex->forceUpdateBindStats();
		return texture->bindDefaultImage(mIndex);
	}

	if (mCurrTexture != tex_name || forceBind)
	{
		activate();
		enable(gl_tex->getTarget());
		mCurrTexture = tex_name;
		glBindTexture(sGLTextureType[gl_tex->getTarget()], tex_name);

		if (gl_tex->updateBindStats())
		{
			texture->setActive();
			texture->updateBindStatsForTester();
		}

		mHasMipMaps = gl_tex->mHasMipMaps;

		if (gl_tex->mTexOptionsDirty)
		{
			gl_tex->mTexOptionsDirty = false;
			setTextureAddressMode(gl_tex->mAddressMode);
			setTextureFilteringOption(gl_tex->mFilterOption);
		}
	}

	return true;
#endif // DX_RENDER
}

//S24 Perf
bool LLTexUnit::bind(LLImageGL* texture, bool for_rendering, bool forceBind, S32 usename)
{
#ifdef DX_RENDER
	// Mirrors bindFast()'s DX_RENDER body (see its comment) - same
	// chokepoint as the LLTexture* overload above, just operating directly
	// on an LLImageGL instead of going through LLTexture::getGLTexture().
	if (mIndex < 0 || !texture) return false;
	// S24 (task #223): see bindFast()'s matching comment - captured before
	// mCurrBoundImageGL is overwritten, used below alongside mCurrDXSRV so a
	// freed-and-reused SRV address can't masquerade as "same texture, skip
	// the flush."
	bool bound_image_changed = (mCurrBoundImageGL != texture);
	mCurrBoundImageGL = texture;
	ID3D11ShaderResourceView* srv = texture->mDXTexture.getSRV();
	// S24 (2026-07-22/23): this had no null check at all, unlike
	// unbind()/unbindFast() (see getWhiteTextureSRV()'s comment above) - a
	// null SRV samples as (0,0,0,0) in HLSL, not a neutral default. A
	// diagnostic confirmed this path never actually hits null in practice
	// (font glyph textures always have a valid SRV by the time they're
	// bound) - the invisible-menu-text bug was elsewhere - but the fallback
	// is kept as a real defensive fix regardless, matching the established
	// unbind()/unbindFast() precedent.
	if (!srv)
	{
		srv = getWhiteTextureSRV();
	}
	// S24 (2026-07-23): the actual invisible-menu-text bug - this branch
	// never flushed pending batched vertices before switching the bound
	// SRV, unlike GL's `if (mCurrTexture != texname) { gGL.flush(); ... }`
	// below. E.g. LLMenuItemGL::draw() draws its highlight rect
	// (gl_rect_2d() -> unbind(), 6 TRIANGLES verts - too few for end() to
	// auto-flush) then immediately calls mFont->render() -> this function:
	// without a flush here, those still-pending highlight verts get drawn
	// later, whenever a flush eventually happens, using the FONT ATLAS
	// texture (the last thing bound), not the white texture they were
	// pushed under - and likewise any pending glyph verts get corrupted by
	// the next unrelated bind(). Mirrors GL's mCurrTexture-gated flush.
	// S24 (task #223): see bindFast()'s matching comment - bound_image_changed
	// closes the SRV-address-reuse hole that mCurrDXSRV alone can't catch
	// (DXTexture::scaleDown() Releases+recreates the SRV on every VRAM-
	// pressure downscale; a coincidentally-reused address would otherwise
	// read as "unchanged" and suppress a flush that's actually needed).
	if (bound_image_changed || mCurrDXSRV != (void*)srv)
	{
		gGL.flush();
		// S24 (2026-08-16): also flush gDXUIBatch's separate pending queue -
		// same missing-flush hazard. See DXUIBatch.h's top comment.
		gDXUIBatch.flushPending();
		mCurrDXSRV = (void*)srv;
	}
	ID3D11SamplerState* sampler = DXSampler::getOrCreate(
		(int)texture->getAddressMode(), (int)texture->getFilteringOption());
	gDXDevice.getContext()->PSSetShaderResources(mIndex, 1, &srv);
	if (mIndex < 16) // see bindFast()'s comment - sampler slots cap at 16, SRV slots don't
	{
		gDXDevice.getContext()->PSSetSamplers(mIndex, 1, &sampler);
	}
	return true;
#else
	if (mIndex < 0 || !texture) return false;

	U32 texname = usename ? usename : texture->getTexName();
	if (!texname)
	{
		LLImageGL* fallback = LLImageGL::sDefaultGLTexture;
		return (fallback && fallback->getTexName()) ? bind(fallback) : false;
	}

	if (mCurrTexture != texname || forceBind)
	{
		gGL.flush();
		activate();
		enable(texture->getTarget());
		mCurrTexture = texname;
		glBindTexture(sGLTextureType[texture->getTarget()], texname);
		texture->updateBindStats();
		mHasMipMaps = texture->mHasMipMaps;

		if (texture->mTexOptionsDirty)
		{
			texture->mTexOptionsDirty = false;
			setTextureAddressMode(texture->mAddressMode);
			setTextureFilteringOption(texture->mFilterOption);
		}
	}

	return true;
#endif // DX_RENDER
}

// S24 Perf
bool LLTexUnit::bind(LLCubeMap* cubeMap)
{
	if (mIndex < 0 || !cubeMap || !LLCubeMap::sUseCubeMaps) return false;

#ifdef DX_RENDER
	// S24 (2026-08-06, task #113): now binds cubeMap->getDXSRV() - a real
	// D3D11 cubemap resource assembled from the 6 individually-uploaded
	// faces (see DXCubeTexture/LLCubeMap::init()'s DX_RENDER branch). Falls
	// back to a neutral white texture (same convention as every other "real
	// resource not ready yet" SRV gap this session) if the cubemap hasn't
	// been assembled - e.g. init() was never called, or failed.
	ID3D11ShaderResourceView* srv = cubeMap->getDXSRV();
	if (!srv)
	{
		srv = getWhiteTextureSRV();
	}
	if (mCurrDXSRV != (void*)srv)
	{
		gGL.flush();
		// S24 (2026-08-16): also flush gDXUIBatch's separate pending queue -
		// same missing-flush hazard. See DXUIBatch.h's top comment.
		gDXUIBatch.flushPending();
		mCurrDXSRV = (void*)srv;
	}
	// CLAMP + TRILINEAR - matches GL's own TAM_CLAMP convention for cubemap
	// faces (avoids seams at face edges) and the full mip chain
	// DXCubeTexture::create() always generates.
	ID3D11SamplerState* sampler = DXSampler::getOrCreate(2, 2);
	gDXDevice.getContext()->PSSetShaderResources(mIndex, 1, &srv);
	if (mIndex < 16) // see bindFast()'s comment - sampler slots cap at 16, SRV slots don't
	{
		gDXDevice.getContext()->PSSetSamplers(mIndex, 1, &sampler);
	}
	return true;
#else
	GLuint texname = cubeMap->mImages[0]->getTexName();
	if (mCurrTexture == texname) return true;

	gGL.flush();
	activate();
	enable(LLTexUnit::TT_CUBE_MAP);
	mCurrTexture = texname;
	glBindTexture(GL_TEXTURE_CUBE_MAP, texname);

	LLImageGL* face = cubeMap->mImages[0];
	mHasMipMaps = face->mHasMipMaps;
	face->updateBindStats();

	if (face->mTexOptionsDirty)
	{
		face->mTexOptionsDirty = false;
		setTextureAddressMode(face->mAddressMode);
		setTextureFilteringOption(face->mFilterOption);
	}

	return true;
#endif // DX_RENDER
}

#ifdef DX_RENDER
// S24 (2026-08-09, task #147 step 4): direct sibling of bind(LLCubeMap*)
// just above - mirrors its DX_RENDER body exactly, just for the array
// resource type (DXCubeArrayTexture) instead of the single-cubemap one
// (DXCubeTexture). LLCubeMapArray has no LLCubeMap::sUseCubeMaps-style
// static gate to check. DX_RENDER-only, unlike bind(LLCubeMap*): GL's own
// LLCubeMapArray::bind() already calls gGL.getTexUnit(stage)->bindManual()
// directly and has no reason to route through here, so there's no GL body
// to keep in sync - this exists purely as the DX_RENDER counterpart
// LLCubeMapArray::bind() calls instead of bindManual() under DX_RENDER.
bool LLTexUnit::bind(LLCubeMapArray* cubeMapArray)
{
	if (mIndex < 0 || !cubeMapArray) return false;

	ID3D11ShaderResourceView* srv = cubeMapArray->getDXSRV();
	// S24 (2026-08-11, task #163 round 8): temp diagnostic here confirmed
	// the SRV is genuinely valid (not falling back to the white 2D
	// texture) - ruled out. Removed.
	if (!srv)
	{
		srv = getWhiteTextureSRV();
	}
	if (mCurrDXSRV != (void*)srv)
	{
		gGL.flush();
		// S24 (2026-08-16): also flush gDXUIBatch's separate pending queue -
		// same missing-flush hazard. See DXUIBatch.h's top comment.
		gDXUIBatch.flushPending();
		mCurrDXSRV = (void*)srv;
	}
	// CLAMP + TRILINEAR - same convention as bind(LLCubeMap*) above (avoids
	// seams at face edges, full mip chain always generated).
	ID3D11SamplerState* sampler = DXSampler::getOrCreate(2, 2);
	gDXDevice.getContext()->PSSetShaderResources(mIndex, 1, &srv);
	if (mIndex < 16) // see bindFast()'s comment - sampler slots cap at 16, SRV slots don't
	{
		gDXDevice.getContext()->PSSetSamplers(mIndex, 1, &sampler);
	}
	return true;
}
#endif // DX_RENDER

// LLRenderTarget is unavailible on the mapserver since it uses FBOs.
bool LLTexUnit::bind(LLRenderTarget* renderTarget, bool bindDepth, bool useComparisonSampler)
{
	if (mIndex < 0 || !renderTarget) return false;

#ifdef DX_RENDER
	// S24 (DX_RENDER, 2026-07-25): this was routing through bindManual(),
	// which under DX_RENDER unconditionally returns false (documented there:
	// "handed a raw GLuint object name with no LLImageGL/LLTexture to look a
	// DX11 resource up from - there's nothing to translate"). That silently
	// broke every caller of this specific overload - and this is THE
	// texture-binding chokepoint for the whole deferred/post-process chain:
	// LLPipeline::bindDeferredShader() (G-buffer sampling for the actual
	// deferred lighting pass), shadow map sampling, generateGlow(), applyCAS(),
	// applyFXAA(), applySMAA(), copyScreenSpaceReflections(), water/hero-probe
	// depth sampling - dozens of call sites in pipeline.cpp alone. Fixed for
	// real now that DXRenderTarget has getColorSRV()/getDepthSRV() (added
	// earlier this session for DXReadback) - pull the SRV directly instead of
	// going through the GLuint-based path at all.
	ID3D11ShaderResourceView* srv = bindDepth ? renderTarget->getDepthSRV() : renderTarget->getColorSRV(0);
	if (!srv)
	{
		return false;
	}
	if (mCurrDXSRV != (void*)srv)
	{
		gGL.flush();
		// S24 (2026-08-16): also flush gDXUIBatch's separate pending queue -
		// same missing-flush hazard. See DXUIBatch.h's top comment.
		gDXUIBatch.flushPending();
		mCurrDXSRV = (void*)srv;
	}
	// CLAMP + BILINEAR: matches the common post-process/screen-space sampling
	// convention (edge clamping avoids wrap-around artifacts on a
	// screen-space texture; render targets aren't mipmapped here).
	// S24 (2026-08-09, task #124): useComparisonSampler routes shadow-map
	// binds (LLPipeline::bindShadowMaps()) to a real comparison sampler
	// instead - see DXSampler::getOrCreateComparison()'s own comment for why
	// a regular sampler here is undefined behavior against
	// SamplerComparisonState registers.
	ID3D11SamplerState* sampler = useComparisonSampler
		? DXSampler::getOrCreateComparison(D3D11_COMPARISON_LESS_EQUAL)
		: DXSampler::getOrCreate(2, 1);
	gDXDevice.getContext()->PSSetShaderResources(mIndex, 1, &srv);
	if (mIndex < 16) // see bindFast()'s comment - sampler slots cap at 16, SRV slots don't
	{
		gDXDevice.getContext()->PSSetSamplers(mIndex, 1, &sampler);
	}
	return true;
#else
	gGL.flush();

	GLuint texname = 0;

	if (bindDepth)
	{
		texname = renderTarget->getDepth(); // returns U32
		llassert(texname); // depth must exist
	}
	else
	{
		texname = renderTarget->getTexture(); // also returns U32
		if (!texname) return false;
	}

	bindManual(renderTarget->getUsage(), texname);
	return true;
#endif
}

// S24 perf
bool LLTexUnit::bindManual(eTextureType type, U32 texture, bool hasMips)
{
	// Early exit if mIndex is invalid
	if (mIndex < 0) return false;

#ifdef DX_RENDER
	// Unlike bind(LLTexture*)/bind(LLImageGL*), this overload is handed a
	// raw GLuint object name with no LLImageGL/LLTexture to look a DX11
	// resource up from - there's nothing to translate. Callers that derive
	// their channel from getTextureChannel()/enableTexture() already return
	// above via the mIndex<0 check; this only guards the hardcoded-unit
	// callers (LLImageGL, LLRenderTarget, LLTexLayer, post-process, edit-tool
	// grid texture, startup noise/SMAA LUTs, etc.) that would otherwise reach
	// the raw glBindTexture() below.
	return false;
#else
	// Only bind texture if it's different from the current one
	if (mCurrTexture != texture)
	{
		gGL.flush(); // Ensure the pipeline is flushed if required (profiling may refine this)

		// Activate and enable the texture unit and type
		activate();
		enable(type);

		// Update current texture and binding
		mCurrTexture = texture;
		glBindTexture(sGLTextureType[type], texture);

		// Update mipmap flag
		mHasMipMaps = hasMips;
	}

	return true;
#endif // DX_RENDER
}

#ifdef DX_RENDER
// S24 (2026-08-03, task #84): DX-native equivalent of bindManual() for
// callers holding a real DXTexture reference directly - see its header
// comment. Mirrors bindFast(LLTexture*)'s SRV+white-fallback+flush-on-
// change logic exactly, just sourcing the SRV from the caller's own
// DXTexture instead of an LLImageGL's.
bool LLTexUnit::bind(DXTexture& tex, eTextureAddressMode address_mode, eTextureFilterOptions filter_option)
{
	if (mIndex < 0) return false;

	ID3D11ShaderResourceView* srv = tex.getSRV();
	if (!srv)
	{
		srv = getWhiteTextureSRV();
	}

	if (mCurrDXSRV != (void*)srv)
	{
		gGL.flush();
		// S24 (2026-08-16): also flush gDXUIBatch's separate pending queue -
		// same missing-flush hazard. See DXUIBatch.h's top comment.
		gDXUIBatch.flushPending();
		mCurrDXSRV = (void*)srv;
	}

	ID3D11SamplerState* sampler = DXSampler::getOrCreate((int)address_mode, (int)filter_option);
	gDXDevice.getContext()->PSSetShaderResources(mIndex, 1, &srv);
	if (mIndex < 16) // see bindFast()'s comment - sampler slots cap at 16, SRV slots don't
	{
		gDXDevice.getContext()->PSSetSamplers(mIndex, 1, &sampler);
	}
	return true;
}
#endif

// S24 UNLOOP
void LLTexUnit::unbind(eTextureType type)
{
#ifdef DX_RENDER
	// Real fix, not a no-op (was one until this was found and fixed) - see
	// getWhiteTextureSRV()'s comment above for why leaving this a no-op
	// (or a null-bind) silently zeroed out every untextured 2D UI element.
	if (mIndex < 0 || type != LLTexUnit::TT_TEXTURE)
	{
		return;
	}
	ID3D11ShaderResourceView* srv = getWhiteTextureSRV();
	// S24 (2026-07-23): same missing-flush bug as bind(LLImageGL*) - see
	// its comment. gl_rect_2d() (solid-color UI fills, e.g. menu hover
	// highlight) calls this before pushing its verts; without the flush,
	// those verts can end up drawn with whatever a LATER bind() switches
	// to instead of this white fallback.
	if (mCurrDXSRV != (void*)srv)
	{
		gGL.flush();
		// S24 (2026-08-16): also flush gDXUIBatch's separate pending queue -
		// same missing-flush hazard. See DXUIBatch.h's top comment.
		gDXUIBatch.flushPending();
		mCurrDXSRV = (void*)srv;
	}
	// S24 (2026-08-17, task #54): see mCurrBoundImageGL's own comment -
	// unbind() means "no texture", so the tracked pointer needs to actually
	// go to null here rather than staying whatever the last real bind() set.
	mCurrBoundImageGL = nullptr;
	ID3D11SamplerState* sampler = DXSampler::getOrCreate(0, 0); // WRAP, POINT - matches a solid white texel regardless
	gDXDevice.getContext()->PSSetShaderResources(mIndex, 1, &srv);
	if (mIndex < 16) // see bindFast()'s comment - sampler slots cap at 16, SRV slots don't
	{
		gDXDevice.getContext()->PSSetSamplers(mIndex, 1, &sampler);
	}
#else
	stop_glerror();

	if (mIndex < 0) return;

	//always flush and activate for consistency
	// Flush pipeline and activate texture unit for consistency
	gGL.flush();
	activate();

	// Only unbind if the current texture type matches
	if (mCurrTexType == type)
	{
		mCurrTexture = 0;

		// Bind the appropriate default texture or null
		glBindTexture(sGLTextureType[type], (type == LLTexUnit::TT_TEXTURE) ? sWhiteTexture : 0);

		stop_glerror();
	}
#endif // DX_RENDER
}

// S24 UNLOOP
void LLTexUnit::unbindFast(eTextureType type)
{
#ifdef DX_RENDER
	// Real fix, not a no-op - see unbind()'s comment/getWhiteTextureSRV().
	if (mIndex < 0 || type != LLTexUnit::TT_TEXTURE)
	{
		return;
	}
	ID3D11ShaderResourceView* srv = getWhiteTextureSRV();
	// S24 (2026-07-23): same missing-flush bug as bind(LLImageGL*)/unbind() -
	// see their comments.
	if (mCurrDXSRV != (void*)srv)
	{
		gGL.flush();
		// S24 (2026-08-16): also flush gDXUIBatch's separate pending queue -
		// same missing-flush hazard. See DXUIBatch.h's top comment.
		gDXUIBatch.flushPending();
		mCurrDXSRV = (void*)srv;
	}
	// S24 (2026-08-17, task #54): see unbind()'s matching fix/mCurrBoundImageGL's comment.
	mCurrBoundImageGL = nullptr;
	ID3D11SamplerState* sampler = DXSampler::getOrCreate(0, 0);
	gDXDevice.getContext()->PSSetShaderResources(mIndex, 1, &srv);
	if (mIndex < 16) // see bindFast()'s comment - sampler slots cap at 16, SRV slots don't
	{
		gDXDevice.getContext()->PSSetSamplers(mIndex, 1, &sampler);
	}
#else
	activate();

	// Disabled caching of binding state.
	if (mCurrTexType == type)
	{
		mCurrTexture = 0;

		// Bind either the white texture or null based on the type
		glBindTexture(sGLTextureType[type], (type == LLTexUnit::TT_TEXTURE) ? sWhiteTexture : 0);
	}
#endif // DX_RENDER
}

void LLTexUnit::setTextureAddressMode(eTextureAddressMode mode)
{
#ifdef DX_RENDER
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
	// crash. Made explicit here instead.
	return;
#else
	if (mIndex < 0 || mCurrTexture == 0) return;

	gGL.flush();

	activate();

	// Set texture address modes for S and T coordinates
	glTexParameteri(sGLTextureType[mCurrTexType], GL_TEXTURE_WRAP_S, sGLAddressMode[mode]);
	glTexParameteri(sGLTextureType[mCurrTexType], GL_TEXTURE_WRAP_T, sGLAddressMode[mode]);

	// Handle R coordinate for cube maps
	if (mCurrTexType == TT_CUBE_MAP)
	{
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, sGLAddressMode[mode]);
	}
#endif // DX_RENDER
}

// S24 UNLOOP
void LLTexUnit::setTextureFilteringOption(LLTexUnit::eTextureFilterOptions option)
{
#ifdef DX_RENDER
	// Mirrors setTextureAddressMode()'s DX_RENDER no-op above (see its
	// comment) - filter option is likewise applied via DXSampler::getOrCreate()
	// at bind time, not through this GL immediate-state-setting path.
	return;
#else
	// Early exit for invalid index, null texture, or multisample texture type
	if (mIndex < 0 || mCurrTexture == 0 || mCurrTexType == LLTexUnit::TT_MULTISAMPLE_TEXTURE) return;

	// Flush the pipeline to ensure consistency
	gGL.flush();

	// Set texture magnification filter
	glTexParameteri(
		sGLTextureType[mCurrTexType],
		GL_TEXTURE_MAG_FILTER,
		(option == TFO_POINT) ? GL_NEAREST : GL_LINEAR
	);

	// Set texture minification filter based on mipmap presence and filtering option
	if (mHasMipMaps)
	{
		glTexParameteri(
			sGLTextureType[mCurrTexType],
			GL_TEXTURE_MIN_FILTER,
			(option >= TFO_TRILINEAR) ? GL_LINEAR_MIPMAP_LINEAR :
			(option >= TFO_BILINEAR) ? GL_LINEAR_MIPMAP_NEAREST :
			GL_NEAREST_MIPMAP_NEAREST
		);
	}
	else
	{
		glTexParameteri(
			sGLTextureType[mCurrTexType],
			GL_TEXTURE_MIN_FILTER,
			(option >= TFO_BILINEAR) ? GL_LINEAR : GL_NEAREST
		);
	}

	/*
	 S24 Anisotropic Filtering Logic:
	  | GL Calls | Branches | Performance
	  | 1        | 0        | Faster, leaner
	*/

	if (gGLManager.mHasAnisotropic)
	{
		glTexParameterf(
			sGLTextureType[mCurrTexType],
			GL_TEXTURE_MAX_ANISOTROPY,
			(LLImageGL::sGlobalUseAnisotropic && option == TFO_ANISOTROPIC) ?
			gGLManager.mMaxAnisotropy : 1.f
		);
	}
#endif // DX_RENDER
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
		++gGL.mLightHash;
		mDiffuse = diffuse;
	}
}

void LLLightState::setDiffuseB(const LLColor4& diffuse)
{
	if (mDiffuseB != diffuse)
	{
		++gGL.mLightHash;
		mDiffuseB = diffuse;
	}
}

void LLLightState::setSunPrimary(bool v)
{
	if (mSunIsPrimary != v)
	{
		++gGL.mLightHash;
		mSunIsPrimary = v;
	}
}

void LLLightState::setSize(F32 v)
{
	if (mSize != v)
	{
		++gGL.mLightHash;
		mSize = v;
	}
}

void LLLightState::setFalloff(F32 v)
{
	if (mFalloff != v)
	{
		++gGL.mLightHash;
		mFalloff = v;
	}
}

void LLLightState::setAmbient(const LLColor4& ambient)
{
	if (mAmbient != ambient)
	{
		++gGL.mLightHash;
		mAmbient = ambient;
	}
}

void LLLightState::setSpecular(const LLColor4& specular)
{
	if (mSpecular != specular)
	{
		++gGL.mLightHash;
		mSpecular = specular;
	}
}

void LLLightState::setPosition(const LLVector4& position)
{
	//always set position because modelview matrix may have changed
	++gGL.mLightHash;
	mPosition = position;
	//transform position by current modelview matrix
	glm::vec4 pos(position);
	pos = gGL.getModelviewMatrix() * pos;
	mPosition.set(glm::value_ptr(pos));
}

void LLLightState::setConstantAttenuation(const F32& atten)
{
	if (mConstantAtten != atten)
	{
		mConstantAtten = atten;
		++gGL.mLightHash;
	}
}

void LLLightState::setLinearAttenuation(const F32& atten)
{
	if (mLinearAtten != atten)
	{
		++gGL.mLightHash;
		mLinearAtten = atten;
	}
}

void LLLightState::setQuadraticAttenuation(const F32& atten)
{
	if (mQuadraticAtten != atten)
	{
		++gGL.mLightHash;
		mQuadraticAtten = atten;
	}
}

void LLLightState::setSpotExponent(const F32& exponent)
{
	if (mSpotExponent != exponent)
	{
		++gGL.mLightHash;
		mSpotExponent = exponent;
	}
}

void LLLightState::setSpotCutoff(const F32& cutoff)
{
	if (mSpotCutoff != cutoff)
	{
		++gGL.mLightHash;
		mSpotCutoff = cutoff;
	}
}

void LLLightState::setSpotDirection(const LLVector3& direction)
{
	//always set direction because modelview matrix may have changed
	++gGL.mLightHash;

	//transform direction by current modelview matrix
	glm::vec3 dir(direction);
	const glm::mat3 mat(gGL.getModelviewMatrix());
	dir = mat * dir;

	mSpotDirection.set(glm::value_ptr(dir));
}

// S24 Optimised
// No hidden defaults � every member still explicitly initialized.
// Minimal branching � loops are tight, no unnecessary temporaries.
// Clarity � constants are named, magic numbers gone.
// Cache friendly � contiguous fills where possible.
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

// S24 Test
bool LLRender::init(bool needs_vertex_buffer)
{
#ifndef DX_RENDER
	// Setup OpenGL debug output if supported and enabled
	if (gGLManager.mHasDebugOutput && gDebugGL)
	{
		//glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_LOW_ARB, 0, NULL, GL_TRUE);
		glDebugMessageCallback((GLDEBUGPROC)gl_debug_callback, NULL);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	}

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
#endif

	gGL.setSceneBlendType(LLRender::BT_ALPHA);
	gGL.setAmbientLightColor(LLColor4::black);

#ifndef DX_RENDER
	glCullFace(GL_BACK);

	// Enable seamless cube maps for reflection maps
	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

	// Check for Vertex Array Object (VAO) support
	if (glGenVertexArrays == nullptr)
	{
		return false;
	}

	// Bind a dummy VAO to comply with core profile requirements
	U32 dummy_vao;
	glGenVertexArrays(1, &dummy_vao);
	glBindVertexArray(dummy_vao);
#endif

	if (needs_vertex_buffer)
	{
		initVertexBuffer();
	}
	return true;
}

void LLRender::initVertexBuffer()
{
	llassert_always(mBuffer.isNull());
	stop_glerror();
	mBuffer = new LLVertexBuffer(immediate_mask);
	mBuffer->allocateBuffer(4096, 0);
	mBuffer->getVertexStrider(mVerticesp);
	mBuffer->getTexCoord0Strider(mTexcoordsp);
	mBuffer->getColorStrider(mColorsp);
	stop_glerror();
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
	setColorMask(mCurrColorMask[0], mCurrColorMask[1], mCurrColorMask[2], mCurrColorMask[3]);

	// Flush the pipeline to ensure all changes are applied
	flush();

	// Reset the dirty flag as the state is now clean
	mDirty = false;
}

void LLRender::syncLightState()
{
	LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;

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
	LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;

#ifdef DX_RENDER
	// Deliberately simple, no hash-based memoization (unlike the GL path
	// below) - correctness first, matching the "always re-upload" choice
	// already made for DXBuffer::upload(); can be optimized later. Only
	// pushes the uniforms the currently-ported base shaders (diffuseV.hlsl)
	// actually declare - modelview/projection/normal/texture0 - not GL's
	// full inverse-matrix/texture1-3 set, since nothing converted so far
	// uses those. mUniformsDirty's actual per-shader constant/uniform
	// binding (LLGLSLShader::bind()'s comment) is still a separate, larger
	// gap - this only wires the matrices syncMatrices() itself is
	// responsible for.
	LLGLSLShader* dx_shader = LLGLSLShader::sCurBoundShaderPtr;
	if (dx_shader)
	{
		DXShader& vs = dx_shader->mDXVertexShader;

		const glm::mat4& mdv = mMatrix[MM_MODELVIEW][mMatIdx[MM_MODELVIEW]];

		// GL-convention projection matrices (glm::frustum()/ortho(), e.g.
		// LLViewerCamera::calcProjection() - GLM_FORCE_DEPTH_ZERO_TO_ONE is
		// not defined anywhere in this project) produce clip-space z in
		// [-w,w], i.e. NDC z in [-1,1] after the divide. D3D11 requires
		// clip-space z in [0,w] (NDC z in [0,1]) and clips away anything
		// outside that range - fed a raw GL-convention matrix, the near
		// half of the intended frustum (NDC z in [-1,0)) gets clipped as
		// "in front of the near plane". Remapped here (z'=0.5*z+0.5*w,
		// applied before the divide) rather than touching the shared
		// GL-convention projection-matrix construction code, which the GL
		// build still depends on unmodified.
		static const glm::mat4 kGLtoDXDepthRemap = []()
		{
			glm::mat4 m(1.0f);
			m[2][2] = 0.5f;
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
		// S24 (2026-08-09, task #157): standalone "projection_matrix" (as
		// opposed to the combined modelview_projection_matrix above) was
		// never pushed here at all - a real, foundational gap this branch
		// has had since it was first scoped to diffuseV.hlsl's uniform set
		// (that shader only ever needed the combined MVP). 33 shader files
		// (every HAS_SKIN/rigged vertex shader: alphaV, materialV,
		// fullbrightV, pbralphaV/pbropaqueV, bumpV, shadows, velocity,
		// occlusion, highlight, preview, avatar) declare this uniform
		// separately and apply it AFTER building an eye-space position via
		// skin+modelview (needed for their own lighting/normal math before
		// projecting) - unlike ordinary static geometry, which just uses
		// modelview_projection_matrix directly in one step and so never
		// exercised this gap. With this constant never uploaded, it sat at
		// its zero-initialized default - mul(zero-matrix, pos) collapses
		// every one of these shaders' clip-space output to (0,0,0,0),
		// which can never rasterize. This is very likely the actual root
		// cause of the whole-session "rigged mesh completely invisible"
		// investigation: real draw calls, real vertex counts, correct skin
		// matrices, yet nothing ever reached the screen. Uses the same
		// D3D11-depth-remapped `proj` (not raw_proj) as
		// modelview_projection_matrix above, since this feeds SV_Position
		// for rasterization and needs D3D11's [0,w] depth convention, not
		// GL's [-w,w] one.
		vs.setUniformMatrix4("projection_matrix", glm::value_ptr(proj));
		vs.setUniformMatrix3("normal_matrix", normal3x3);
		const glm::mat4& tex_mat0 = mMatrix[MM_TEXTURE0][mMatIdx[MM_TEXTURE0]];
		vs.setUniformMatrix4("texture_matrix0", glm::value_ptr(tex_mat0));

		// S24 (2026-08-05): this DX_RENDER branch was scoped, years ago, to
		// exactly the uniforms diffuseV.hlsl (the first ported vertex
		// shader) declared - modelview/projection/normal/texture0 - per the
		// comment above. softenLightF.hlsl (a PIXEL shader) is the first
		// converted shader to actually need "inv_proj" (deferredUtil.hlsl's
		// getPositionWithDepth(), for reconstructing world/eye position from
		// depth) - nothing ever pushed it, so its constant-buffer slot sat
		// at whatever zero-initialized value it started with, and
		// mul(0-matrix, ndc) => pos.w == 0 => pos.xyz/pos.w == NaN at every
		// pixel (confirmed via a direct C++ readback of mRT->screen, not
		// guessed). Uses the UN-remapped GL-convention projection matrix,
		// not `proj` above - getPositionWithDepth() manually converts the
		// D3D11 [0,1] depth back to GL's [-1,1] NDC convention
		// (`2.0*depth-1.0`) before this multiply, so inv_proj must invert
		// that same GL-convention matrix, not the D3D11-remapped one used
		// for rasterization. setUniformMatrix4() no-ops harmlessly on
		// whichever stage doesn't declare "inv_proj" (looked up by name in
		// that stage's own reflected constant map), so pushing to both vs
		// and ps here is safe for every other already-converted shader.
		glm::mat4 inv_proj = glm::inverse(raw_proj);
		vs.setUniformMatrix4("inv_proj", glm::value_ptr(inv_proj));

		// S24 (2026-08-18, task #155): attempted to call syncLightState()
		// here (GL's only call site is inside this function's #else branch -
		// light_position[]/light_direction[]/light_attenuation[]/
		// light_diffuse[]/sun_up_factor were never uploaded under DX_RENDER
		// at all, so local point/spot lights contributed zero illumination
		// to any forward-lit alpha surface). REVERTED (r3648 follow-up,
		// same day): live-tested and caused severe sunrise/sunset "disco"
		// flicker - LLSettingsSky::getIsSunUp() (llinventory/llsettingssky.cpp,
		// sunDir.mV[2] >= 0.0f) is a hard, zero-margin threshold that this
		// exposed to alphaF.hlsl/materialF.hlsl/pbralphaF.hlsl for the first
		// time; a first attempt at a C++-side hold-time debounce changed the
		// symptom's shape (regular ~2s cycling instead of random flicker)
		// without actually fixing it, and the exact mechanism wasn't pinned
		// down with enough confidence to keep iterating live against an
		// actively disruptive visual bug. Reverted to this file's pre-
		// 2026-08-18 state (local-light arrays back to always-zero, exactly
		// as before) to restore known-stable behavior; needs a proper,
		// unhurried investigation before re-attempting - see task #155.

		// S24 (DX_RENDER diagnostic, 2026-07-26): TEMPORARY - uiV.hlsl
		// transforms every incoming UV through this exact matrix
		// (OUT.vary_texcoord0 = mul(texture_matrix0, float4(IN.texcoord0,0,1)).xy)
		// before the pixel shader ever samples anything. A whole-image
		// (0,0)-(1,1) UV test can't detect a non-identity texture matrix -
		// almost any small stray scale/offset still maps most of [0,1] back
		// onto itself, visually. A tiny, precise sub-rect (a single glyph's
		// ~10x10 texel region out of a 512x512 atlas) has no such margin -
		// the same stray transform would shift it straight out of the real
		// glyph pixels into blank canvas. Logging once to check whether
		// this is actually identity at the moment a glyph gets submitted.
		if (dx_shader == LLGLSLShader::sCurBoundShaderPtr && dx_shader->mName == "UI Shader")
		{
			static bool logged_once = false;
			if (!logged_once)
			{
				logged_once = true;
				const float* m = glm::value_ptr(tex_mat0);
				LL_WARNS("Text") << "syncMatrices() texture_matrix0 for UI Shader: ["
					<< m[0] << "," << m[1] << "," << m[2] << "," << m[3] << " | "
					<< m[4] << "," << m[5] << "," << m[6] << "," << m[7] << " | "
					<< m[8] << "," << m[9] << "," << m[10] << "," << m[11] << " | "
					<< m[12] << "," << m[13] << "," << m[14] << "," << m[15] << "]"
					<< LL_ENDL;

				// S24 (DX_RENDER diagnostic, 2026-07-26): TEMPORARY - real
				// values of the matrix that actually places every UI vertex
				// on screen, never directly logged before (only inferred
				// from "some content showed up roughly where expected").
				const float* mv = glm::value_ptr(mvp);
				LL_WARNS("Text") << "syncMatrices() modelview_projection_matrix for UI Shader: ["
					<< mv[0] << "," << mv[1] << "," << mv[2] << "," << mv[3] << " | "
					<< mv[4] << "," << mv[5] << "," << mv[6] << "," << mv[7] << " | "
					<< mv[8] << "," << mv[9] << "," << mv[10] << "," << mv[11] << " | "
					<< mv[12] << "," << mv[13] << "," << mv[14] << "," << mv[15] << "]"
					<< LL_ENDL;
			}
		}

		vs.uploadConstants();

		// S24 (DX_RENDER diagnostic, 2026-07-26): TEMPORARY - VSSetConstantBuffers
		// only happens inside this if() - if getConstantBuffer() were ever
		// null for the UI shader specifically, this silently skips binding,
		// leaving whatever constant buffer a PREVIOUS shader/pass left bound
		// still active at slot b0. Never directly confirmed non-null before.
		ID3D11Buffer* cb = vs.getConstantBuffer();
		if (dx_shader == LLGLSLShader::sCurBoundShaderPtr && dx_shader->mName == "UI Shader")
		{
			static bool logged_once = false;
			if (!logged_once)
			{
				logged_once = true;
				LL_WARNS("Text") << "syncMatrices() constant buffer for UI Shader: "
					<< (void*)cb << (cb ? "" : " [NULL - VSSetConstantBuffers skipped]")
					<< LL_ENDL;

				// S24 (DX_RENDER diagnostic, 2026-07-26): TEMPORARY - the
				// fresh-shader glyph test (no constant buffer at all,
				// transform baked into vertex positions in C++) rendered
				// correctly where gUIProgram (which DOES rely on this
				// constant buffer for its transform) did not. Every prior
				// check of this data only read the C++-side glm::mat4
				// SOURCE values before upload - never what's actually
				// sitting in GPU memory after uploadConstants() ran. Doing
				// a real staging-buffer readback now, the same technique
				// already used for texture readback (DXReadback), applied
				// to a buffer for the first time this session.
				if (cb)
				{
					D3D11_BUFFER_DESC desc = {};
					cb->GetDesc(&desc);
					D3D11_BUFFER_DESC staging_desc = desc;
					staging_desc.Usage = D3D11_USAGE_STAGING;
					staging_desc.BindFlags = 0;
					staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
					staging_desc.MiscFlags = 0;
					ID3D11Buffer* staging = nullptr;
					HRESULT hr = gDXDevice.getDevice()->CreateBuffer(&staging_desc, nullptr, &staging);
					if (SUCCEEDED(hr) && staging)
					{
						gDXDevice.getContext()->CopyResource(staging, cb);
						D3D11_MAPPED_SUBRESOURCE mapped = {};
						if (SUCCEEDED(gDXDevice.getContext()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
						{
							const float* buf = (const float*)mapped.pData;
							U32 float_count = llmin((U32)(desc.ByteWidth / 4), 32u);
							std::string dump = "[";
							for (U32 i = 0; i < float_count; ++i)
							{
								if (i) dump += ",";
								dump += llformat("%g", buf[i]);
							}
							dump += "]";
							LL_WARNS("Text") << "syncMatrices() ACTUAL GPU constant buffer bytes (size="
								<< desc.ByteWidth << "): " << dump << LL_ENDL;
							gDXDevice.getContext()->Unmap(staging, 0);
						}
						staging->Release();
					}
					else
					{
						LL_WARNS("Text") << "syncMatrices(): constant buffer staging readback CreateBuffer failed, hr=0x"
							<< std::hex << (unsigned long)hr << std::dec << LL_ENDL;
					}
				}
			}
		}
		if (cb)
		{
			// S24 (task #79 follow-on): bind to $Globals' REAL reflected
			// slot, not a hardcoded 0 - almost always 0, but not for a
			// shader that explicitly claims b0 for its own named cbuffer
			// (e.g. class1/gltf/pbrmetallicroughnessV.hlsl's GLTFMaterials),
			// which pushes $Globals to b1 instead. See
			// DXShader::getConstantBufferBindPoint()'s own comment.
			gDXDevice.getContext()->VSSetConstantBuffers(vs.getConstantBufferBindPoint(), 1, &cb);
		}

		// S24 (DXUIBatch plan, phase 3): symmetric pixel-shader handling -
		// never needed before this, since every shader converted so far
		// (diffuseV/F.hlsl, uiV/F.hlsl, ...) declared its top-level uniforms
		// only in the vertex stage. solidcolorF.hlsl's "uniform vec4 color"
		// (see LLGLSLShader::uniform4f()'s DX_RENDER branch, which stages
		// this exact value via mDXPixelShader.setUniformFloatArray()) is the
		// first pixel-stage uniform - without this, the staged value would
		// never actually reach the GPU (uploadConstants()/PSSetConstantBuffers
		// were never called for any DXShader's pixel stage anywhere). No-ops
		// for every shader whose pixel stage declares no top-level uniforms
		// (mDXPixelShader.getConstantBuffer() returns nullptr - reflectConstants()
		// never created one).
		DXShader& ps = dx_shader->mDXPixelShader;
		// S24 (2026-08-05): softenLightF.hlsl needs inv_proj in the PIXEL
		// stage (see the vs.setUniformMatrix4("inv_proj", ...) comment
		// above) - push to both stages, harmless no-op wherever a shader's
		// reflected constants don't include it.
		ps.setUniformMatrix4("inv_proj", glm::value_ptr(inv_proj));
		ps.uploadConstants();
		if (ID3D11Buffer* pcb = ps.getConstantBuffer())
		{
			gDXDevice.getContext()->PSSetConstantBuffers(ps.getConstantBufferBindPoint(), 1, &pcb);
		}
	}
#else
	static const U32 name[] =
	{
		LLShaderMgr::MODELVIEW_MATRIX,
		LLShaderMgr::PROJECTION_MATRIX,
		LLShaderMgr::TEXTURE_MATRIX0,
		LLShaderMgr::TEXTURE_MATRIX1,
		LLShaderMgr::TEXTURE_MATRIX2,
		LLShaderMgr::TEXTURE_MATRIX3,
	};

	LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;

	static glm::mat4 cached_mvp;
	static glm::mat4 cached_inv_mdv;
	static U32 cached_mvp_mdv_hash = 0xFFFFFFFF;
	static U32 cached_mvp_proj_hash = 0xFFFFFFFF;

	static glm::mat4 cached_normal;
	static U32 cached_normal_hash = 0xFFFFFFFF;

	if (shader)
	{
		bool mvp_done = false;

		U32 i = MM_MODELVIEW;
		if (mMatHash[MM_MODELVIEW] != shader->mMatHash[MM_MODELVIEW])
		{ //update modelview, normal, and MVP
			const glm::mat4& mat = mMatrix[MM_MODELVIEW][mMatIdx[MM_MODELVIEW]];

			// if MDV has changed, update the cached inverse as well
			if (cached_mvp_mdv_hash != mMatHash[MM_MODELVIEW])
			{
				cached_inv_mdv = glm::inverse(mat);
			}

			shader->uniformMatrix4fv(name[MM_MODELVIEW], 1, GL_FALSE, glm::value_ptr(mat));
			shader->mMatHash[MM_MODELVIEW] = mMatHash[MM_MODELVIEW];

			//update normal matrix
			S32 loc = shader->getUniformLocation(LLShaderMgr::NORMAL_MATRIX);
			if (loc > -1)
			{
				if (cached_normal_hash != mMatHash[i])
				{
					cached_normal = glm::transpose(cached_inv_mdv);
					cached_normal_hash = mMatHash[i];
				}

				auto norm = glm::value_ptr(cached_normal);

				F32 norm_mat[] =
				{
					norm[0], norm[1], norm[2],
					norm[4], norm[5], norm[6],
					norm[8], norm[9], norm[10]
				};

				shader->uniformMatrix3fv(LLShaderMgr::NORMAL_MATRIX, 1, GL_FALSE, norm_mat);
			}

			if (shader->getUniformLocation(LLShaderMgr::INVERSE_MODELVIEW_MATRIX))
			{
				shader->uniformMatrix4fv(LLShaderMgr::INVERSE_MODELVIEW_MATRIX, 1, GL_FALSE, glm::value_ptr(cached_inv_mdv));
			}

			//update MVP matrix
			mvp_done = true;
			loc = shader->getUniformLocation(LLShaderMgr::MODELVIEW_PROJECTION_MATRIX);
			if (loc > -1)
			{
				U32 proj = MM_PROJECTION;

				if (cached_mvp_mdv_hash != mMatHash[i] || cached_mvp_proj_hash != mMatHash[MM_PROJECTION])
				{
					cached_mvp = mat;
					cached_mvp = mMatrix[proj][mMatIdx[proj]] * cached_mvp;
					cached_mvp_mdv_hash = mMatHash[i];
					cached_mvp_proj_hash = mMatHash[MM_PROJECTION];
				}

				shader->uniformMatrix4fv(LLShaderMgr::MODELVIEW_PROJECTION_MATRIX, 1, GL_FALSE, glm::value_ptr(cached_mvp));
			}
		}

		i = MM_PROJECTION;
		if (mMatHash[MM_PROJECTION] != shader->mMatHash[MM_PROJECTION])
		{ //update projection matrix, normal, and MVP
			const glm::mat4& mat = mMatrix[MM_PROJECTION][mMatIdx[MM_PROJECTION]];

			// GZ: This was previously disabled seemingly due to a bug involving the deferred renderer's regular pushing and popping of mats.
			// We're reenabling this and cleaning up the code around that - that would've been the appropriate course initially.
			// Anything beyond the standard proj and inv proj mats are special cases.  Please setup special uniforms accordingly in the future.
			if (shader->getUniformLocation(LLShaderMgr::INVERSE_PROJECTION_MATRIX))
			{
				glm::mat4 inv_proj = glm::inverse(mat);
				shader->uniformMatrix4fv(LLShaderMgr::INVERSE_PROJECTION_MATRIX, 1, false, glm::value_ptr(inv_proj));
			}

			// Used by some full screen effects - such as full screen lights, glow, etc.
			if (shader->getUniformLocation(LLShaderMgr::IDENTITY_MATRIX))
			{
				shader->uniformMatrix4fv(LLShaderMgr::IDENTITY_MATRIX, 1, GL_FALSE, glm::value_ptr(glm::identity<glm::mat4>()));
			}

			shader->uniformMatrix4fv(name[MM_PROJECTION], 1, GL_FALSE, glm::value_ptr(mat));
			shader->mMatHash[MM_PROJECTION] = mMatHash[MM_PROJECTION];

			if (!mvp_done)
			{
				//update MVP matrix
				S32 loc = shader->getUniformLocation(LLShaderMgr::MODELVIEW_PROJECTION_MATRIX);
				if (loc > -1)
				{
					if (cached_mvp_mdv_hash != mMatHash[MM_PROJECTION] || cached_mvp_proj_hash != mMatHash[MM_PROJECTION])
					{
						U32 mdv = MM_MODELVIEW;
						cached_mvp = mat;
						cached_mvp *= mMatrix[mdv][mMatIdx[mdv]];
						cached_mvp_mdv_hash = mMatHash[MM_MODELVIEW];
						cached_mvp_proj_hash = mMatHash[MM_PROJECTION];
					}

					shader->uniformMatrix4fv(LLShaderMgr::MODELVIEW_PROJECTION_MATRIX, 1, GL_FALSE, glm::value_ptr(cached_mvp));
				}
			}
		}

		for (i = MM_TEXTURE0; i < NUM_MATRIX_MODES; ++i)
		{
			if (mMatHash[i] != shader->mMatHash[i])
			{
				shader->uniformMatrix4fv(name[i], 1, GL_FALSE, glm::value_ptr(mMatrix[i][mMatIdx[i]]));
				shader->mMatHash[i] = mMatHash[i];
			}
		}

		if (shader->mFeatures.hasLighting || shader->mFeatures.calculatesLighting || shader->mFeatures.calculatesAtmospherics)
		{ //also sync light state
			syncLightState();
		}
	}
	STOP_GLERROR;
#endif // DX_RENDER
}

void LLRender::translatef(const GLfloat& x, const GLfloat& y, const GLfloat& z)
{
	flush();

	{
		mMatrix[mMatrixMode][mMatIdx[mMatrixMode]] = glm::translate(mMatrix[mMatrixMode][mMatIdx[mMatrixMode]], glm::vec3(x, y, z));
		mMatHash[mMatrixMode]++;
	}
}

void LLRender::scalef(const GLfloat& x, const GLfloat& y, const GLfloat& z)
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

void LLRender::rotatef(const GLfloat& a, const GLfloat& x, const GLfloat& y, const GLfloat& z)
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

void LLRender::loadMatrix(const GLfloat* m)
{
	flush();
	{
		mMatrix[mMatrixMode][mMatIdx[mMatrixMode]] = glm::make_mat4((GLfloat*)m);
		mMatHash[mMatrixMode]++;
	}
}

void LLRender::multMatrix(const GLfloat* m)
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
		U32 tex_index = gGL.getCurrentTexUnitIndex();
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

void LLRender::setColorMask(bool writeColor, bool writeAlpha)
{
	setColorMask(writeColor, writeColor, writeColor, writeAlpha);
}
// S24 Testing
void LLRender::setColorMask(bool writeColorR, bool writeColorG, bool writeColorB, bool writeAlpha)
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

#ifdef DX_RENDER
		applyDXBlendState();
#else
		glColorMask(writeColorR ? GL_TRUE : GL_FALSE,
			writeColorG ? GL_TRUE : GL_FALSE,
			writeColorB ? GL_TRUE : GL_FALSE,
			writeAlpha ? GL_TRUE : GL_FALSE);
#endif
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
#ifdef DX_RENDER
		applyDXBlendState();
#else
		glBlendFunc(sGLBlendFactor[sfactor], sGLBlendFactor[dfactor]);
#endif
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

#ifdef DX_RENDER
		// DXStateCache's blend state only takes one src/dst pair (matching
		// the 2-factor overload above) - separate color/alpha factors are a
		// documented gap, nothing converted so far calls this overload.
		applyDXBlendState();
#else
		glBlendFuncSeparate(sGLBlendFactor[color_sfactor], sGLBlendFactor[color_dfactor],
			sGLBlendFactor[alpha_sfactor], sGLBlendFactor[alpha_dfactor]);
#endif
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

	bool enabled = LLGLState::isEnabled(GL_BLEND);
	D3D11_BLEND src = sDXBlendFactor[mCurrBlendColorSFactor];
	D3D11_BLEND dst = sDXBlendFactor[mCurrBlendColorDFactor];
	// S24 (2026-08-06): now passed through for real instead of being
	// silently dropped - see DXStateCache::getBlendState()'s header comment.
	// The 2-factor blendFunc() overload already sets these equal to
	// src/dst, so this is a no-op change for every caller except the
	// 4-factor blendFuncSeparate() overload (dxdrawpoolalpha.cpp's main
	// callers), which finally gets the alpha-channel factors it actually
	// asked for.
	D3D11_BLEND alpha_src = sDXBlendFactor[mCurrBlendAlphaSFactor];
	D3D11_BLEND alpha_dst = sDXBlendFactor[mCurrBlendAlphaDFactor];

	ID3D11BlendState* bs = DXStateCache::getBlendState(enabled, src, dst, alpha_src, alpha_dst, write_mask);
	gDXDevice.getContext()->OMSetBlendState(bs, nullptr, 0xFFFFFFFF);
}
#endif

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
	LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
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
	// S24 - Guard error checking loop with debug-only compilation: each glGetError() forces GPU->CPU sync stall
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
	llassert(LLGLSLShader::sCurBoundShaderPtr == &gUIProgram);
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
			LL_ERRS() << "gGL.begin() called redundantly." << LL_ENDL;
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
{ // S24 - Possible problem with snapshots so handling asserts better here
	if (mCount == 0)
	{
		return;
	}

	// Fast pointer load
	LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;

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
	// S24 (2026-08-06, task #106): D3D11 has no LINE_LOOP topology (dropped
	// after D3D9, see LLVertexBuffer.cpp's sDXMode[] table) - mapped to
	// D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED there and asserted against in
	// drawArrays()/drawRange(), but that llassert() is a no-op in Release
	// builds, letting an undefined topology reach IASetPrimitiveTopology()
	// silently (confirmed matching the reported "UI Shader Draw: Current
	// Primitive Topology value (0) is not valid" D3D11 debug-layer warning -
	// llhudview.cpp's composition-guide overlays use gGL.begin(LINE_LOOP)
	// directly and were never swept when llrender2dutils.cpp's own fan/loop
	// call sites were converted to DXRender2DUtils earlier this session).
	// A closed line loop is just a line strip with the first vertex
	// duplicated onto the end - same expansion DXRender2DUtils already uses
	// for its own LINE_LOOP call sites, done here instead at the shared
	// immediate-mode chokepoint so every caller benefits, not just the ones
	// already ported. count+1 is safe here - vertex3f()'s own overflow
	// guard already keeps count within the striders' allocated range with
	// room to spare for one more.
	if (mMode == LLRender::LINE_LOOP && count > 0)
	{
		mVerticesp[count] = mVerticesp[0];
		mColorsp[count] = mColorsp[0];
		mTexcoordsp[count] = mTexcoordsp[0];
		++count;
		draw_mode = LLRender::LINE_STRIP;
	}
	// S24 (2026-08-08, task #127): D3D11 has no TRIANGLE_FAN topology either
	// (same D3D9-era removal as LINE_LOOP above) - mapped to
	// D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED in llvertexbuffer.cpp's sDXMode[]
	// table, same silently-reaches-IASetPrimitiveTopology(UNDEFINED) failure
	// class LINE_LOOP had before the fix above. Traced from a user report
	// that the build-tool translate-arrow gizmos looked broken/needed
	// conversion (originally filed as task #131, a stencil-buffer angle that
	// turned out to be dead #if 0 code, never compiled either build) - the
	// real cause is LLCone::render() (llcylinder.cpp, used by
	// LLManipTranslate::renderArrow() for every axis arrowhead) submitting
	// its entire cone geometry as two TRIANGLE_FAN batches. Same broken path
	// also hit by lltracker.cpp's beacon circle and llreflectionmap.cpp's
	// debug rings (grep-confirmed, not guessed).
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
			gGL.getTexUnit(0)->mCurrTexture,
			mMatrix[MM_MODELVIEW][mMatIdx[MM_MODELVIEW]],
			mMatrix[MM_PROJECTION][mMatIdx[MM_PROJECTION]],
			mMatrix[MM_TEXTURE0][mMatIdx[MM_TEXTURE0]]
		);
#ifdef DX_RENDER
		// S24 (task #54): see LLVertexBufferData::mDXImage's comment.
		buffer_data.mDXImage = gGL.getTexUnit(0)->mCurrBoundImageGL;
#endif
	}
	else
	{
		vb = bufferfromCache(attribute_mask, count);
	}

#ifdef DX_RENDER
	// S24 (2026-08-16): the reverse-direction link that was missing -
	// gDXUIBatch's own call sites already flush THIS queue (gGL) before
	// submitting to gDXUIBatch, but nothing previously flushed gDXUIBatch's
	// separate pending queue before THIS draw. Without this, a UI rect/text
	// batched-but-not-yet-drawn in gDXUIBatch could end up rendered AFTER a
	// later gGL immediate-mode draw that logically should come after it,
	// corrupting paint order. See DXUIBatch.h's top comment.
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
		LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("vb cache hash");

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
		LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("vb cache hit");
		// cache hit, just use the cached buffer
		vb = cache->second.vb;
		cache->second.touched = std::chrono::steady_clock::now();
	}
	else
	{
		LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("vb cache miss");
		vb = genBuffer(attribute_mask, count);

		sVBCache[vhash] = { vb , std::chrono::steady_clock::now() };

		static U32 miss_count = 0;
		miss_count++;
		if (miss_count > 1024)
		{
			LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("vb cache clean");
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

	// S24 (2026-07-21/22): a diagnostic here (menu-hover-sliver, later
	// "zero UI" investigations) confirmed submitted vertex geometry is
	// always full-range/plausible, never squashed - the real bugs were
	// elsewhere (white-texture alpha sampling; see the project's
	// open-issues ledger). Removed once it had answered every question it
	// was asked across both investigations.

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

	// S24 (2026-07-22/23): a series of diagnostics here (across the "zero
	// UI" and "no text" investigations) confirmed the immediate-mode draw
	// pipeline - real back buffer, correct viewport, blending, valid
	// shaders, and real-texture draws (font glyphs) all reaching this
	// chokepoint with sane mode/count and a valid, non-white SRV bound - is
	// correctly configured under DX_RENDER. The remaining "invisible text"
	// mystery was traced downstream of this function; see the project's
	// open-issues ledger (stage 6 discovery) for the full history.

	vb->drawArrays(mode, 0, count);
}

void LLRender::resetStriders(S32 count)
{
	mVerticesp[0] = mVerticesp[count];
	mTexcoordsp[0] = mTexcoordsp[count];
	mColorsp[0] = mColorsp[count];

	mCount = 0;
}

void LLRender::vertex3f(const GLfloat& x, const GLfloat& y, const GLfloat& z)
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
	vertex3f((GLfloat)x, (GLfloat)y, 0);
}

void LLRender::vertex2f(const GLfloat& x, const GLfloat& y)
{
	vertex3f(x, y, 0);
}

void LLRender::vertex2fv(const GLfloat* v)
{
	vertex3f(v[0], v[1], 0);
}

void LLRender::vertex3fv(const GLfloat* v)
{
	vertex3f(v[0], v[1], v[2]);
}

void LLRender::texCoord2f(const GLfloat& x, const GLfloat& y)
{
	mTexcoordsp[mCount] = LLVector2(x, y);
}

void LLRender::texCoord2i(const GLint& x, const GLint& y)
{
	texCoord2f((GLfloat)x, (GLfloat)y);
}

void LLRender::texCoord2fv(const GLfloat* tc)
{
	texCoord2f(tc[0], tc[1]);
}

void LLRender::color4ub(const GLubyte& r, const GLubyte& g, const GLubyte& b, const GLubyte& a)
{
	if (!LLGLSLShader::sCurBoundShaderPtr || LLGLSLShader::sCurBoundShaderPtr->mAttributeMask & LLVertexBuffer::MAP_COLOR)
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

void LLRender::color4f(const GLfloat& r, const GLfloat& g, const GLfloat& b, const GLfloat& a)
{
	color4ub((GLubyte)(llclamp(r, 0.f, 1.f) * 255),
		(GLubyte)(llclamp(g, 0.f, 1.f) * 255),
		(GLubyte)(llclamp(b, 0.f, 1.f) * 255),
		(GLubyte)(llclamp(a, 0.f, 1.f) * 255));
}

void LLRender::color4fv(const GLfloat* c)
{
	color4f(c[0], c[1], c[2], c[3]);
}

void LLRender::color3f(const GLfloat& r, const GLfloat& g, const GLfloat& b)
{
	color4f(r, g, b, 1);
}

void LLRender::color3fv(const GLfloat* c)
{
	color4f(c[0], c[1], c[2], 1);
}

void LLRender::diffuseColor3f(F32 r, F32 g, F32 b)
{
	LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;
	llassert(shader != NULL);

	if (shader)
	{
		shader->uniform4f(LLShaderMgr::DIFFUSE_COLOR, r, g, b, 1.f);
	}
}

void LLRender::diffuseColor3fv(const F32* c)
{
	LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;
	llassert(shader != NULL);

	if (shader)
	{
		shader->uniform4f(LLShaderMgr::DIFFUSE_COLOR, c[0], c[1], c[2], 1.f);
	}
}

void LLRender::diffuseColor4f(F32 r, F32 g, F32 b, F32 a)
{
	LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;
	llassert(shader != NULL);

	if (shader)
	{
		shader->uniform4f(LLShaderMgr::DIFFUSE_COLOR, r, g, b, a);
	}
}

void LLRender::diffuseColor4fv(const F32* c)
{
	LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;
	llassert(shader != NULL);

	if (shader)
	{
		shader->uniform4fv(LLShaderMgr::DIFFUSE_COLOR, 1, c);
	}
}

void LLRender::diffuseColor4ubv(const U8* c)
{
	LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;
	llassert(shader != NULL);

	if (shader)
	{
		shader->uniform4f(LLShaderMgr::DIFFUSE_COLOR, c[0] / 255.f, c[1] / 255.f, c[2] / 255.f, c[3] / 255.f);
	}
}

void LLRender::diffuseColor4ub(U8 r, U8 g, U8 b, U8 a)
{
	LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;
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

// S24 Be very careful! this indirectly impacts projected lights/ and some textures this works nice as is!
glm::vec3 mul_mat4_vec3(const glm::mat4& mat, const glm::vec3& vec)
{
	glm::vec4 vec4(vec, 1.0f); // Convert vec3 to vec4 with w = 1.0
	glm::vec4 result = mat * vec4; // Multiply matrix with vec4

	LLVector4a res;
	res.load3(glm::value_ptr(glm::vec3(result / result.w))); // Normalize by w and convert to vec3

	return glm::make_vec3(res.getF32ptr());
}