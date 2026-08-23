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
        case GL_RGBA16F:   return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case GL_RGB16F:    return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case GL_RGB10_A2:  return DXGI_FORMAT_R10G10B10A2_UNORM;
        case GL_RGB:       return DXGI_FORMAT_R8G8B8A8_UNORM;
        case GL_R8:        return DXGI_FORMAT_R8_UNORM;
        case GL_RG16F:     return DXGI_FORMAT_R16G16_FLOAT;
        case GL_R16F:      return DXGI_FORMAT_R16_FLOAT;
        default:
            LL_WARNS("RenderTarget") << "glColorFormatToDX: unmapped GL format 0x" << std::hex << color_fmt << std::dec << ", defaulting to RGBA8" << LL_ENDL;
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        }
    }
}
#endif

LLRenderTarget* LLRenderTarget::sBoundTarget = NULL;
U32 LLRenderTarget::sBytesAllocated = 0;

// S24 - Optimized framebuffer status check for GPU allocation hot path
// This function was called 5+ times during EVERY render target allocation (initialization, resize)
// Gains: ~100-300 cycles per allocation by making status check debug-only
// Rationale: Framebuffer validation is expensive GPU roundtrip (glCheckFramebufferStatus).
//            Production code should trust allocation succeeded (failure is catastrophic anyway).
//            User can't fix GPU driver bugs - warning spam provides zero value.
void check_framebuffer_status()
{
#if LL_DEBUG && !defined(DX_RENDER)
	// S24 (DX_RENDER, 2026-07-24): all real call sites are already GL-only
	// branches (see this file's DX_RENDER fixes), so this never runs under
	// DX_RENDER today - but the function's own body was still unconditionally
	// compiled regardless of DX_RENDER, only gated by LL_DEBUG. A debug
	// DX_RENDER=ON build would have tried to link glCheckFramebufferStatus,
	// an unresolved symbol now that OpenGL is deliberately not linked (see
	// newview/CMakeLists.txt). Gated explicitly rather than relying on "no
	// caller reaches it" to stay true forever.
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
// S24 improve
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
		gGL.getTexUnit(0)->bindManual(mUsage, mTex[i]);
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
		gGL.getTexUnit(0)->bindManual(mUsage, mDepth);
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
	LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
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
	return mDXRenderTarget.allocate(resx, resy, glColorFormatToDX(color_fmt), depth);
#else
	if (depth)
	{
		// S24 - Removed "Failed to allocate depth buffer" warning from allocation hot path
		// Gains: ~30-50 cycles per depth allocation failure
		// Rationale: If depth allocation fails, rendering will fail catastrophically anyway.
		//            User can't fix GPU memory exhaustion - reduce video settings instead.
		//            Warning provides no actionable information.
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
	// S24 (DX_RENDER, 2026-07-24): a real gap, not yet closed - this is the
	// "point this render target at a particular LLImageGL" alternate usage
	// mode (see the class comment), distinct from allocate()'s normal
	// self-owned-texture path. DXRenderTarget has no equivalent - it can't
	// render into an arbitrary externally-owned DXTexture, only its own.
	// Only known caller today is LLDrawPoolBump's bump-map to normal-map
	// conversion (lldrawpoolbump.cpp), which is itself unguarded - see the
	// matching fix there, which skips the whole conversion under DX_RENDER
	// rather than call this and leave a half-set-up render target. Kept as
	// a loud one-time warning rather than a silent no-op, in case some
	// future caller reaches this without the same care.
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
	stop_glerror();

	check_framebuffer_status();

	glBindFramebuffer(GL_FRAMEBUFFER, sCurFBO);
#endif // DX_RENDER
}

// S24
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

// S24
bool LLRenderTarget::addColorAttachment(U32 color_fmt)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
	llassert(!isBoundInStack());

	if (color_fmt == 0)
		return true; // No attachment requested

#ifdef DX_RENDER
	return mDXRenderTarget.addColorAttachment(glColorFormatToDX(color_fmt));
#else
	const U32 offset = static_cast<U32>(mTex.size());
	// S24 - Removed "Too many color attachments" warning from hot path
	// Gains: ~30-40 cycles per excessive attachment attempt
	// Rationale: llassert still fires in debug builds, production should trust the limit.
	//            This is a programmer error (exceeded GL_MAX_COLOR_ATTACHMENTS), not user error.
	//            User can't fix it anyway.
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
	gGL.getTexUnit(0)->bindManual(mUsage, tex);

	stop_glerror();

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
	// S24 - Removed "Could not allocate color buffer" warning from allocation hot path
	// Gains: ~40-60 cycles per allocation failure
	// Rationale: GPU memory exhaustion is catastrophic - warning won't help user.
	//            If allocation fails, rendering will break obviously (black screen, artifacts).
	//            User's only recourse: reduce graphics settings, upgrade GPU.
	//            Console spam during VRAM exhaustion doesn't help diagnosis.
	if (glGetError() != GL_NO_ERROR)
	{
		return false;
	}

	sBytesAllocated += mResX * mResY * 4;
	stop_glerror();

	// Filtering: bilinear for first attachment, point for additional
	gGL.getTexUnit(0)->setTextureFilteringOption(
		offset == 0 ? LLTexUnit::TFO_BILINEAR : LLTexUnit::TFO_POINT
	);
	stop_glerror();

	// Address mode: mirror unless rectangular texture (ATI quirk)
	gGL.getTexUnit(0)->setTextureAddressMode(
		mUsage != LLTexUnit::TT_RECT_TEXTURE
		? LLTexUnit::TAM_MIRROR
		: LLTexUnit::TAM_CLAMP
	);
	stop_glerror();

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

// S24
bool LLRenderTarget::allocateDepth()
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;

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
	gGL.getTexUnit(0)->bindManual(mUsage, mDepth);

	const U32 internal_type = LLTexUnit::getInternalType(mUsage);

	stop_glerror();
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

	gGL.getTexUnit(0)->setTextureFilteringOption(LLTexUnit::TFO_POINT);

	// S24 - Correct order: It checks for OpenGL errors before updating memory accounting.
	// S24 - Removed "Unable to allocate depth buffer" warning from allocation hot path
	// Gains: ~40-60 cycles per depth allocation failure
	// Rationale: Same as color buffer above - GPU memory exhaustion is catastrophic.
	//            Depth allocation failure breaks rendering entirely (Z-fighting, no depth test).
	//            User can't fix GPU VRAM exhaustion from console warning.
	if (glGetError() != GL_NO_ERROR)
	{
		return false;
	}

	// Memory accounting (4 bytes per pixel for depth24 + padding)
	sBytesAllocated += mResX * mResY * 4;

	return true;
#endif // DX_RENDER
}

// S24
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

// S24
void LLRenderTarget::release()
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
	llassert(!isBoundInStack());

#ifdef DX_RENDER
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

// S24
void LLRenderTarget::bindTarget(bool bind_depth)
{
	LL_PROFILE_GPU_ZONE("bindTarget");
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

// S24
void LLRenderTarget::clear(U32 mask_in)
{
	LL_PROFILE_GPU_ZONE("clear");

#ifdef DX_RENDER
	mDXRenderTarget.clear((mask_in & GL_COLOR_BUFFER_BIT) != 0, mUseDepth && (mask_in & GL_DEPTH_BUFFER_BIT) != 0);
#else
	llassert(mFBO);

	// Build clear mask in one expression
	const U32 mask = GL_COLOR_BUFFER_BIT | (mUseDepth ? GL_DEPTH_BUFFER_BIT : 0);

#if LL_DEBUG  // keep expensive checks only in debug
	check_framebuffer_status();
	stop_glerror();
#endif

	glClear(mask & mask_in);

#if LL_DEBUG
	stop_glerror();
#endif
#endif // DX_RENDER
}

// S24
void LLRenderTarget::clearColor(float r, float g, float b, float a)
{
	LL_PROFILE_GPU_ZONE("clearColor");

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
	// S24 (2026-08-04): was completely unguarded - routed through
	// bindManual() with a raw GL texture name from getTexture(index),
	// always a no-op under DX_RENDER (see bindManual()'s own comment -
	// nothing to translate a bare GLuint into). This is THE chokepoint for
	// binding one SPECIFIC attachment of a multi-attachment render target
	// (diffuse/specular/normal/emissive - anything via an explicit index,
	// as opposed to LLTexUnit::bind(LLRenderTarget*, bool)'s single-
	// attachment-0-only overload) as an input texture for a later pass -
	// LLPipeline::bindDeferredShader() uses exactly this for all of the
	// deferred lighting pass's G-buffer reads. Root cause of a real bug:
	// softenLightF.hlsl's getGBuffer() read (0,0,0,0) at every sampled
	// point even where the G-buffer itself (confirmed via a completely
	// separate readback path, DXReadback::readPixels() on the raw
	// resource) had real, varied diffuse color - because diffuseRect/
	// specularRect/normalMap were never actually bound to their texture
	// channels at all. This was a known, documented gap - see
	// DXRenderTarget.h's own class comment ("the read side... has no
	// DX_RENDER equivalent yet, since no such pass has been converted so
	// far") - this is that pass. Uses getColorSRV(index) (already exists,
	// already used by DXReadback/presentDeferredScreen), not the raw-
	// GLuint path.
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
	gGL.flush();
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
#else
	gGL.getTexUnit(channel)->bindManual(mUsage, getTexture(index), filter_options == LLTexUnit::TFO_TRILINEAR || filter_options == LLTexUnit::TFO_ANISOTROPIC);
	gGL.getTexUnit(channel)->setTextureFilteringOption(filter_options);
#endif
}

// S24
void LLRenderTarget::flush()
{
	gGL.flush();

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

		// S24 (2026-08-21): bindSwapChainBackBuffer() always sets a viewport
		// covering the FULL swap chain (DXRenderTarget.cpp) - it has no idea
		// LLViewerWindow::mWorldViewRectRaw carves out a smaller area below
		// the menu bar/location bar chrome. GL's own fallback just below
		// (the #else branch) explicitly restores gGLViewport here; this
		// branch never did, so any RT stack unwinding to the back buffer
		// mid-frame (e.g. render_hud_attachments()'s renderGeomPostDeferred()
		// re-triggering doWaterExclusionMask()'s bindTarget()/flush() cycle)
		// silently widened the live D3D11 viewport back to the full window -
		// aspect mismatch crushed HUD text/geometry, and the origin shift
		// (0,0 vs the chrome-adjusted top) read as an offset. gGLViewport
		// itself (used by llviewercamera.cpp's unProject() for manipulator
		// picking) was never touched by bindSwapChainBackBuffer(), so picking
		// silently disagreed with what was actually on screen.
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

// S24
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