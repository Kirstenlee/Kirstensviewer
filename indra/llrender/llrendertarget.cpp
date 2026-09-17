/**
 * @file llrendertarget.cpp
 * @brief LLRenderTarget implementation
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

#include "llrendertarget.h"
#include "llrender.h"
#include "llgl.h"

#ifdef DX_RENDER
#include "DXSampler.h"
#include "DXDevice.h"
#include "DXSwapChain.h"
#include "DXUIBatch.h"
#endif

#ifdef DX_RENDER
namespace
{
    // Covers every color_fmt this codebase actually passes to allocate()/
    // addColorAttachment() (confirmed by grepping pipeline.cpp's call
    // sites), not a speculative full GL-format table. DXGI has no 3-channel
    // 8-bit or float format, so GL_RGB/GL_RGB16F pad in an unused alpha
    // channel - same reasoning as DXTexture's format repack.
    DXGI_FORMAT glColorFormatToDX(U32 color_fmt)
    {
        switch (color_fmt)
        {
        case GL_RGBA:      return DXGI_FORMAT_R8G8B8A8_UNORM;
        // S24: R16G16B16A16_UNORM (not _FLOAT) matches GL_RGBA16's own
        // semantics - a normalized fixed-point format, same as GL_RGBA's
        // UNORM mapping just wider, not floating-point like GL_RGBA16F below.
        case GL_RGBA16:    return DXGI_FORMAT_R16G16B16A16_UNORM;
        case GL_RGBA16F:   return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case GL_RGB16F:    return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case GL_RGB10_A2:  return DXGI_FORMAT_R10G10B10A2_UNORM;
        case GL_RGB:       return DXGI_FORMAT_R8G8B8A8_UNORM;
        case GL_R8:        return DXGI_FORMAT_R8_UNORM;
        case GL_RG16F:     return DXGI_FORMAT_R16G16_FLOAT;
        case GL_R16F:      return DXGI_FORMAT_R16_FLOAT;
        // S24: GL_RGBA8/GL_RGB8 are distinct enum values from GL_RGBA/GL_RGB
        // above (sized-internal-format tokens) - made explicit rather than
        // relying on the default:/RGBA8 fallback happening to match.
        case GL_RGBA8:     return DXGI_FORMAT_R8G8B8A8_UNORM;
        case GL_RGB8:      return DXGI_FORMAT_R8G8B8A8_UNORM;
        // S24: R11G11B10_FLOAT is GL_R11F_G11F_B10F's exact DXGI equivalent -
        // same packed layout, no alpha channel in either.
        case GL_R11F_G11F_B10F: return DXGI_FORMAT_R11G11B10_FLOAT;
        default:
            LL_WARNS("RenderTarget") << "glColorFormatToDX: unmapped GL format 0x" << std::hex << color_fmt << std::dec << ", defaulting to RGBA8" << LL_ENDL;
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        }
    }
}
#endif

LLRenderTarget* LLRenderTarget::sBoundTarget = NULL;
U32 LLRenderTarget::sBytesAllocated = 0;

void check_framebuffer_status()
{
#if LL_DEBUG && !defined(DX_RENDER)
	// S24: gated on DX_RENDER too, not just LL_DEBUG - glCheckFramebufferStatus
	// is an unresolved symbol once OpenGL is delinked.
	if (gDebugGL)
	{
		GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE)
		{
			// Debug build only: log and fail hard on incomplete framebuffer
			LL_WARNS() << "check_framebuffer_status failed -- " << std::hex << status << LL_ENDL;
			ll_fail("check_framebuffer_status failed");
		}
	}
#endif
	// Release builds, or DX_RENDER: no-op (trust GPU allocation succeeded)
}

bool LLRenderTarget::sUseFBO = false;
U32 LLRenderTarget::sCurFBO = 0;


extern S32 gGLViewport[4];

U32 LLRenderTarget::sCurResX = 0;
U32 LLRenderTarget::sCurResY = 0;

LLRenderTarget::LLRenderTarget() :
	mResX(0),
	mResY(0),
	mFBO(0),
	mDepth(0),
	mUseDepth(false),
	mUsage(LLTexUnit::TT_TEXTURE)
{
}

LLRenderTarget::~LLRenderTarget()
{
	release();
}
void LLRenderTarget::resize(U32 resx, U32 resy)
{
	// Skip if dimensions are unchanged
	if (resx == mResX && resy == mResY)
		return;

#ifdef DX_RENDER
	mDXRenderTarget.resize(resx, resy);
	mResX = resx;
	mResY = resy;
#else
	// Pixel delta for memory accounting
	const S32 pix_diff = static_cast<S32>(resx) * static_cast<S32>(resy)
		- static_cast<S32>(mResX) * static_cast<S32>(mResY);

	mResX = resx;
	mResY = resy;

	llassert(mInternalFormat.size() == mTex.size());

	const U32 internal_type = LLTexUnit::getInternalType(mUsage);

	// Resize color attachments
	for (size_t i = 0; i < mTex.size(); ++i)
	{
		gDX.getTexUnit(0)->bindManual(mUsage, mTex[i]);
		LLImageGL::setManualImage(
			internal_type,
			0,
			mInternalFormat[i],
			mResX,
			mResY,
			GL_RGBA,
			GL_UNSIGNED_BYTE,
			nullptr,
			false
		);
		sBytesAllocated += pix_diff * 4;
	}

	// Resize depth attachment if present
	if (mDepth)
	{
		gDX.getTexUnit(0)->bindManual(mUsage, mDepth);
		LLImageGL::setManualImage(
			internal_type,
			0,
			GL_DEPTH_COMPONENT24,
			mResX,
			mResY,
			GL_DEPTH_COMPONENT,
			GL_UNSIGNED_INT,
			nullptr,
			false
		);
		sBytesAllocated += pix_diff * 4;
	}
#endif // DX_RENDER
}

bool LLRenderTarget::allocate(U32 resx, U32 resy, U32 color_fmt, bool depth, LLTexUnit::eTextureType usage, LLTexUnit::eTextureMipGeneration generateMipMaps)
{
	llassert(usage == LLTexUnit::TT_TEXTURE);
	llassert(!isBoundInStack());

#ifndef DX_RENDER
	// gGLManager.mGLMaxTextureSize defaults to 0 and is only ever populated
	// by real GL init (glGetIntegerv), which never happens under DX_RENDER -
	// clamping against it there would zero out every render target.
	resx = llmin(resx, (U32)gGLManager.mGLMaxTextureSize);
	resy = llmin(resy, (U32)gGLManager.mGLMaxTextureSize);
#endif

	release();

	mResX = resx;
	mResY = resy;

	mUsage = usage;
	mUseDepth = depth;

	mGenerateMipMaps = generateMipMaps;

	if (mGenerateMipMaps != LLTexUnit::TMG_NONE) {
		// Calculate the number of mip levels based upon resolution that we should have.
		mMipLevels = 1 + (U32)floor(log10((float)llmax(mResX, mResY)) / log10(2.0));
	}

#ifdef DX_RENDER
	// S24: color_fmt==0 is an established "no color attachment, depth-only"
	// convention (pipeline.cpp's shadow[i]/mSpotShadow[i] allocations) -
	// route it straight to DXGI_FORMAT_UNKNOWN rather than through
	// glColorFormatToDX(), which has no case for 0. DXRenderTarget::
	// allocate() skips creating a color attachment when it sees
	// DXGI_FORMAT_UNKNOWN.
	return mDXRenderTarget.allocate(resx, resy, color_fmt == 0 ? DXGI_FORMAT_UNKNOWN : glColorFormatToDX(color_fmt), depth);
#else
	if (depth)
	{
		if (!allocateDepth())
		{
			return false;
		}
	}

	glGenFramebuffers(1, (GLuint*)&mFBO);

	if (mDepth)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, mFBO);

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, LLTexUnit::getInternalType(mUsage), mDepth, 0);

		glBindFramebuffer(GL_FRAMEBUFFER, sCurFBO);
	}

	return addColorAttachment(color_fmt);
#endif // DX_RENDER
}

void LLRenderTarget::setColorAttachment(LLImageGL* img, LLGLuint use_name)
{
#ifdef DX_RENDER
	// S24: real gap, not yet closed - DXRenderTarget can't render into an
	// arbitrary externally-owned DXTexture, only its own. Only known caller
	// (LLDrawPoolBump's bump-map to normal-map conversion) already skips
	// this under DX_RENDER. Kept as a loud one-time warning rather than a
	// silent no-op, in case a future caller reaches this without the same care.
	static bool warned = false;
	if (!warned)
	{
		warned = true;
		LL_WARNS("RenderTarget") << "LLRenderTarget::setColorAttachment: not supported under DX_RENDER - caller must skip this render target under DX_RENDER instead." << LL_ENDL;
	}
	return;
#else
	llassert(img != nullptr); // img must not be null
	llassert(sUseFBO); // FBO support must be enabled
	llassert(mDepth == 0); // depth buffers not supported with this mode
	llassert(mTex.empty()); // mTex must be empty with this mode (binding target should be done via LLImageGL)
	llassert(!isBoundInStack());

	if (mFBO == 0)
	{
		glGenFramebuffers(1, (GLuint*)&mFBO);
	}

	mResX = img->getWidth();
	mResY = img->getHeight();
	mUsage = img->getTarget();

	if (use_name == 0)
	{
		use_name = img->getTexName();
	}

	mTex.push_back(use_name);

	glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		LLTexUnit::getInternalType(mUsage), use_name, 0);

	check_framebuffer_status();

	glBindFramebuffer(GL_FRAMEBUFFER, sCurFBO);
#endif // DX_RENDER
}

void LLRenderTarget::releaseColorAttachment()
{
#ifdef DX_RENDER
	// Mirrors setColorAttachment()'s DX_RENDER no-op above - see its comment.
	return;
#else
	// Preconditions: not bound, single color attachment, valid FBO
	llassert(!isBoundInStack());
	llassert(mTex.size() == 1);
	llassert(mFBO != 0);

	if (mTex.empty())
		return; // nothing to release

	const U32 internal_type = LLTexUnit::getInternalType(mUsage);

	glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
	glFramebufferTexture2D(
		GL_FRAMEBUFFER,
		GL_COLOR_ATTACHMENT0,
		internal_type,
		0,
		0
	);
	glBindFramebuffer(GL_FRAMEBUFFER, sCurFBO);

	mTex.clear();
#endif // DX_RENDER
}

bool LLRenderTarget::addColorAttachment(U32 color_fmt)
{
	llassert(!isBoundInStack());

	if (color_fmt == 0)
		return true; // No attachment requested

#ifdef DX_RENDER
	return mDXRenderTarget.addColorAttachment(glColorFormatToDX(color_fmt));
#else
	const U32 offset = static_cast<U32>(mTex.size());
	if (offset >= 4)
	{
		llassert(offset < 4);
		return false;
	}
	if (offset > 0 && mFBO == 0)
	{
		llassert(mFBO != 0);
		return false;
	}

	U32 tex = 0;
	LLImageGL::generateTextures(1, &tex);
	gDX.getTexUnit(0)->bindManual(mUsage, tex);


	const U32 internal_type = LLTexUnit::getInternalType(mUsage);

	// Allocate texture storage
	clear_glerror();
	LLImageGL::setManualImage(
		internal_type,
		0,
		color_fmt,
		mResX,
		mResY,
		GL_RGBA,
		GL_UNSIGNED_BYTE,
		nullptr,
		false
	);
	if (glGetError() != GL_NO_ERROR)
	{
		return false;
	}

	sBytesAllocated += mResX * mResY * 4;

	// Filtering: bilinear for first attachment, point for additional
	gDX.getTexUnit(0)->setTextureFilteringOption(
		offset == 0 ? LLTexUnit::TFO_BILINEAR : LLTexUnit::TFO_POINT
	);

	// Address mode: mirror unless rectangular texture (ATI quirk)
	gDX.getTexUnit(0)->setTextureAddressMode(
		mUsage != LLTexUnit::TT_RECT_TEXTURE
		? LLTexUnit::TAM_MIRROR
		: LLTexUnit::TAM_CLAMP
	);

	// Attach to FBO if available
	if (mFBO)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
		glFramebufferTexture2D(
			GL_FRAMEBUFFER,
			GL_COLOR_ATTACHMENT0 + offset,
			internal_type,
			tex,
			0
		);
		check_framebuffer_status();
		glBindFramebuffer(GL_FRAMEBUFFER, sCurFBO);
	}

	mTex.push_back(tex);
	mInternalFormat.push_back(color_fmt);

	if (gDebugGL)
	{
		bindTarget();
		flush();
	}

	return true;
#endif // DX_RENDER
}

bool LLRenderTarget::allocateDepth()
{

#ifdef DX_RENDER
	if (!mDXRenderTarget.allocateDepth())
	{
		return false;
	}
	mUseDepth = true;
	return true;
#else
	// Generate and bind depth texture
	LLImageGL::generateTextures(1, &mDepth);
	gDX.getTexUnit(0)->bindManual(mUsage, mDepth);

	const U32 internal_type = LLTexUnit::getInternalType(mUsage);

	clear_glerror();

	// Allocate depth storage
	LLImageGL::setManualImage(
		internal_type,
		0,
		GL_DEPTH_COMPONENT24,
		mResX,
		mResY,
		GL_DEPTH_COMPONENT,
		GL_UNSIGNED_INT,
		nullptr,
		false
	);

	gDX.getTexUnit(0)->setTextureFilteringOption(LLTexUnit::TFO_POINT);

	if (glGetError() != GL_NO_ERROR)
	{
		return false;
	}

	// Memory accounting (4 bytes per pixel for depth24 + padding)
	sBytesAllocated += mResX * mResY * 4;

	return true;
#endif // DX_RENDER
}

void LLRenderTarget::shareDepthBuffer(LLRenderTarget& target)
{
	llassert(!isBoundInStack());

#ifdef DX_RENDER
	// mFBO/mDepth stay 0 under DX_RENDER (no GL resources are ever created),
	// so the GL preconditions below would misfire - check the real DX state.
	mDXRenderTarget.shareDepthBuffer(target.mDXRenderTarget);
	target.mUseDepth = mUseDepth;
#else
	// Precondition checks
	if (!mFBO || !target.mFBO)
		LL_ERRS() << "Cannot share depth buffer between non-FBO render targets." << LL_ENDL;

	if (target.mDepth || target.mUseDepth)
		LL_ERRS() << "Target already has a depth buffer. Detach it first." << LL_ENDL;

	// Nothing to share if we don't have a depth texture
	if (!mDepth)
		return;

	const U32 internal_type = LLTexUnit::getInternalType(mUsage);

	glBindFramebuffer(GL_FRAMEBUFFER, target.mFBO);
	glFramebufferTexture2D(
		GL_FRAMEBUFFER,
		GL_DEPTH_ATTACHMENT,
		internal_type,
		mDepth,
		0
	);

	check_framebuffer_status();
	glBindFramebuffer(GL_FRAMEBUFFER, sCurFBO);

	target.mUseDepth = true;
#endif // DX_RENDER
}

void LLRenderTarget::release()
{
	llassert(!isBoundInStack());

#ifdef DX_RENDER
	// S24: must reset mUseDepth here - isComplete() returns
	// `mDXRenderTarget.getNumColorAttachments() > 0 || mUseDepth`, so
	// leaving it stale-true after release() makes isComplete() lie "still
	// complete" even though the color texture is gone, and any caller gated
	// on `if (!isComplete()) allocate(...)` never reallocates again.
	mUseDepth = false;
	mDXRenderTarget.release();
	mTex.clear();
	mInternalFormat.clear();
	mResX = mResY = 0;
#else
	const size_t tex_count = mTex.size();
	const U32 internal_type = LLTexUnit::getInternalType(mUsage);

	// Depth texture teardown
	if (mDepth)
	{
		LLImageGL::deleteTextures(1, &mDepth);
		mDepth = 0;
		sBytesAllocated -= mResX * mResY * 4;
	}
	else if (mFBO && mUseDepth)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, internal_type, 0, 0);
		mUseDepth = false;
		glBindFramebuffer(GL_FRAMEBUFFER, sCurFBO);
	}

	// Extra color attachments teardown
	if (mFBO && tex_count > 1)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
		for (size_t z = tex_count - 1; z >= 1; --z)
		{
			sBytesAllocated -= mResX * mResY * 4;
			glFramebufferTexture2D(GL_FRAMEBUFFER, static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + z), internal_type, 0, 0);
			LLImageGL::deleteTextures(1, &mTex[z]);
		}
		glBindFramebuffer(GL_FRAMEBUFFER, sCurFBO);
	}

	// FBO teardown
	if (mFBO)
	{
		if (mFBO == sCurFBO)
		{
			sCurFBO = 0;
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}
		glDeleteFramebuffers(1, reinterpret_cast<GLuint*>(&mFBO));
		mFBO = 0;
	}

	// Primary color texture teardown
	if (tex_count > 0)
	{
		sBytesAllocated -= mResX * mResY * 4;
		LLImageGL::deleteTextures(1, &mTex[0]);
	}

	mTex.clear();
	mInternalFormat.clear();
	mResX = mResY = 0;
#endif // DX_RENDER
}

void LLRenderTarget::bindTarget(bool bind_depth)
{
	llassert(!isBoundInStack());

#ifdef DX_RENDER
	// mFBO stays 0 under DX_RENDER (no GL FBO is ever created) - the
	// mPreviousRT/sBoundTarget stack bookkeeping below is shared/backend-
	// agnostic (DXRenderTarget deliberately has no bind-stack of its own -
	// see its header comment), so it's still updated here.
	mDXRenderTarget.bindTarget(bind_depth);
	mPreviousRT = sBoundTarget;
	sBoundTarget = this;
#else
	(void)bind_depth; // GL's FBO depth attachment is fixed at allocate()/shareDepthBuffer() time, not per-bind
	llassert(mFBO);

	// Bind only if not already bound
	if (sCurFBO != mFBO)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
		sCurFBO = mFBO;
	}

	static const GLenum drawbuffers[] = {
		GL_COLOR_ATTACHMENT0,
		GL_COLOR_ATTACHMENT1,
		GL_COLOR_ATTACHMENT2,
		GL_COLOR_ATTACHMENT3
	};

	const size_t tex_count = mTex.size();
	if (tex_count == 0)
	{
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
	}
	else
	{
		glDrawBuffers(static_cast<GLsizei>(tex_count), drawbuffers);
		glReadBuffer(GL_COLOR_ATTACHMENT0);
	}

	check_framebuffer_status();

	// Only update viewport if dimensions changed
	if (sCurResX != mResX || sCurResY != mResY)
	{
		glViewport(0, 0, mResX, mResY);
		sCurResX = mResX;
		sCurResY = mResY;
	}

	mPreviousRT = sBoundTarget;
	sBoundTarget = this;
#endif // DX_RENDER
}

void LLRenderTarget::clear(U32 mask_in)
{

#ifdef DX_RENDER
	mDXRenderTarget.clear((mask_in & GL_COLOR_BUFFER_BIT) != 0, mUseDepth && (mask_in & GL_DEPTH_BUFFER_BIT) != 0);
#else
	llassert(mFBO);

	// Build clear mask in one expression
	const U32 mask = GL_COLOR_BUFFER_BIT | (mUseDepth ? GL_DEPTH_BUFFER_BIT : 0);

#if LL_DEBUG  // keep expensive checks only in debug
	check_framebuffer_status();
#endif

	glClear(mask & mask_in);

#if LL_DEBUG
#endif
#endif // DX_RENDER
}

void LLRenderTarget::clearColor(float r, float g, float b, float a)
{

#ifdef DX_RENDER
	mDXRenderTarget.clearColor(r, g, b, a);
#else
	// No GL caller exists yet - nothing on the GL side has hit the gap this
	// exists for (see DXRenderTarget::clearColor()'s comment). Kept as a
	// loud stub rather than silently doing nothing, in case that changes.
	llassert_always(false && "LLRenderTarget::clearColor() has no GL implementation");
#endif // DX_RENDER
}

U32 LLRenderTarget::getTexture(U32 attachment) const
{
	llassert(attachment < mTex.size());
	return (attachment < mTex.size()) ? mTex[attachment] : 0;
}

U32 LLRenderTarget::getNumTextures() const
{
	return static_cast<U32>(mTex.size());
}

void LLRenderTarget::bindTexture(U32 index, S32 channel, LLTexUnit::eTextureFilterOptions filter_options)
{
#ifdef DX_RENDER
	// S24: this is THE chokepoint for binding one specific attachment of a
	// multi-attachment render target (diffuse/specular/normal/emissive via
	// explicit index) as an input texture - LLPipeline::bindDeferredShader()
	// uses this for all of the deferred lighting pass's G-buffer reads.
	// Uses getColorSRV(index), not the raw-GLuint bindManual() path (which
	// has nothing to translate a bare GLuint into under DX_RENDER).
	if (channel < 0)
	{
		return;
	}
	ID3D11ShaderResourceView* srv = getColorSRV(index);
	if (!srv)
	{
		return;
	}
	// Same missing-flush-before-SRV-switch bug class as every other
	// texture-bind chokepoint fixed this session (see LLTexUnit::bind()'s
	// comments) - this function has no LLTexUnit instance of its own to
	// cache "did this channel's SRV actually change" in, so always flush
	// rather than risk leaving pending batched vertices drawn under stale
	// state.
	gDX.flush();
	// S24: this function raw-binds PSSetShaderResources completely outside
	// LLTexUnit's bookkeeping, so without also flushing gDXUIBatch here, a
	// later LLTexUnit::bind() call for the same channel could wrongly think
	// its own cached state is still current and skip a real rebind.
	gDXUIBatch.flushPending();
	// CLAMP address mode baked in here (GL's setTextureAddressMode(TAM_CLAMP)
	// call that normally follows this one is a confirmed no-op under
	// DX_RENDER - sampler state is built fresh at bind time, not via that
	// idiom) - filter_options threaded through for real, unlike
	// LLTexUnit::bind(LLRenderTarget*, bool)'s hardcoded BILINEAR
	// simplification, since G-buffer channels (encoded normals, packed
	// material params) can be genuinely wrong if bilinear-interpolated
	// across texel boundaries, not just softer-looking.
	ID3D11SamplerState* sampler = DXSampler::getOrCreate(2, (int)filter_options);
	gDXDevice.getContext()->PSSetShaderResources(channel, 1, &srv);
	if (channel < 16) // see LLTexUnit::bindFast()'s comment - sampler slots cap at 16, SRV slots don't
	{
		gDXDevice.getContext()->PSSetSamplers(channel, 1, &sampler);
	}
	// S24: tell this channel's LLTexUnit what's really bound now - see the
	// comment above. Pure bookkeeping, changes no GPU state.
	gDX.getTexUnit(channel)->syncDXBindState((void*)srv, (void*)sampler);
#else
	gDX.getTexUnit(channel)->bindManual(mUsage, getTexture(index), filter_options == LLTexUnit::TFO_TRILINEAR || filter_options == LLTexUnit::TFO_ANISOTROPIC);
	gDX.getTexUnit(channel)->setTextureFilteringOption(filter_options);
#endif
}

void LLRenderTarget::flush()
{
	gDX.flush();

#ifdef DX_RENDER
	// Mip generation (mGenerateMipMaps == TMG_AUTO) has no DX_RENDER
	// equivalent yet - matches DXTexture's "no mip chain" scoping (see its
	// comment); not exercised by deferredScreen (allocated with TMG_NONE).
	llassert(sBoundTarget == this);

	if (mPreviousRT)
	{
		// Restore previous render target in stack - shared/backend-agnostic
		// bookkeeping (see bindTarget()'s comment).
		sBoundTarget = mPreviousRT->mPreviousRT;
		mPreviousRT->bindTarget();
	}
	else
	{
		sBoundTarget = nullptr;
		DXRenderTarget::bindSwapChainBackBuffer();

		// S24: bindSwapChainBackBuffer() always sets a viewport covering the
		// full swap chain, with no idea LLViewerWindow::mWorldViewRectRaw
		// carves out a smaller area below the menu/location bar chrome -
		// restore gGLViewport explicitly here, matching the GL #else branch
		// below, or any RT stack unwind to the back buffer mid-frame widens
		// the live D3D11 viewport back to the full window.
		D3D11_VIEWPORT vp = {};
		vp.TopLeftX = (float)gGLViewport[0];
		vp.TopLeftY = (float)(gDXSwapChain.getHeight() - (gGLViewport[1] + gGLViewport[3]));
		vp.Width = (float)gGLViewport[2];
		vp.Height = (float)gGLViewport[3];
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		gDXDevice.getContext()->RSSetViewports(1, &vp);
	}
#else
	llassert(mFBO);
	llassert(sCurFBO == mFBO);
	llassert(sBoundTarget == this);

	if (mGenerateMipMaps == LLTexUnit::TMG_AUTO)
	{
		bindTexture(0, 0, LLTexUnit::TFO_TRILINEAR);
		glGenerateMipmap(GL_TEXTURE_2D);
	}

	if (mPreviousRT)
	{
		// Restore previous render target in stack
		sBoundTarget = mPreviousRT->mPreviousRT;
		mPreviousRT->bindTarget();
	}
	else
	{
		sBoundTarget = nullptr;
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		sCurFBO = 0;
		glViewport(gGLViewport[0], gGLViewport[1], gGLViewport[2], gGLViewport[3]);
		sCurResX = gGLViewport[2];
		sCurResY = gGLViewport[3];
		glReadBuffer(GL_BACK);
		glDrawBuffer(GL_BACK);
	}
#endif // DX_RENDER
}

bool LLRenderTarget::isComplete() const
{
#ifdef DX_RENDER
	// mTex/mDepth stay empty/0 under DX_RENDER (no GL resources are ever
	// created) - check the real DX-side state instead.
	return mDXRenderTarget.getNumColorAttachments() > 0 || mUseDepth;
#else
	return !mTex.empty() || mDepth;
#endif // DX_RENDER
}

void LLRenderTarget::getViewport(S32* viewport)
{
	viewport[0] = 0;
	viewport[1] = 0;
	viewport[2] = mResX;
	viewport[3] = mResY;
}

bool LLRenderTarget::isBoundInStack() const
{
	LLRenderTarget* cur = sBoundTarget;
	while (cur && cur != this)
	{
		cur = cur->mPreviousRT;
	}

	return cur == this;
}

void LLRenderTarget::swapFBORefs(LLRenderTarget& other)
{
	// Preconditions: both valid, unbound, and compatible
	llassert(mFBO && other.mFBO);
	llassert(sCurFBO != mFBO && sCurFBO != other.mFBO);
	llassert(!isBoundInStack() && !other.isBoundInStack());

	llassert(sUseFBO == other.sUseFBO);
	llassert(mResX == other.mResX && mResY == other.mResY);
	llassert(mInternalFormat == other.mInternalFormat);
	llassert(mTex.size() == other.mTex.size());
	llassert(mDepth == other.mDepth);
	llassert(mUseDepth == other.mUseDepth);
	llassert(mGenerateMipMaps == other.mGenerateMipMaps);
	llassert(mMipLevels == other.mMipLevels);
	llassert(mUsage == other.mUsage);

	using std::swap;
	swap(mFBO, other.mFBO);
	swap(mTex, other.mTex);
	// If other members are logically tied to the FBO, swap them here too
}