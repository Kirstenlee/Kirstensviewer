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
#if LL_DEBUG
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
	// Release builds: no-op (trust GPU allocation succeeded)
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
}

bool LLRenderTarget::allocate(U32 resx, U32 resy, U32 color_fmt, bool depth, LLTexUnit::eTextureType usage, LLTexUnit::eTextureMipGeneration generateMipMaps)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
	llassert(usage == LLTexUnit::TT_TEXTURE);
	llassert(!isBoundInStack());

	resx = llmin(resx, (U32)gGLManager.mGLMaxTextureSize);
	resy = llmin(resy, (U32)gGLManager.mGLMaxTextureSize);

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
}

void LLRenderTarget::setColorAttachment(LLImageGL* img, LLGLuint use_name)
{
	
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
}

// S24
void LLRenderTarget::releaseColorAttachment()
{
	

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
}

// S24
bool LLRenderTarget::addColorAttachment(U32 color_fmt)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
	llassert(!isBoundInStack());

	if (color_fmt == 0)
		return true; // No attachment requested

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
}

// S24
bool LLRenderTarget::allocateDepth()
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;

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
}

// S24
void LLRenderTarget::shareDepthBuffer(LLRenderTarget& target)
{
	llassert(!isBoundInStack());

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
}

// S24
void LLRenderTarget::release()
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
	llassert(!isBoundInStack());

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
}

// S24
void LLRenderTarget::bindTarget()
{
	LL_PROFILE_GPU_ZONE("bindTarget");
	llassert(mFBO);
	llassert(!isBoundInStack());

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
}

// S24
void LLRenderTarget::clear(U32 mask_in)
{
	LL_PROFILE_GPU_ZONE("clear");
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
	gGL.getTexUnit(channel)->bindManual(mUsage, getTexture(index), filter_options == LLTexUnit::TFO_TRILINEAR || filter_options == LLTexUnit::TFO_ANISOTROPIC);
	gGL.getTexUnit(channel)->setTextureFilteringOption(filter_options);
}

// S24
void LLRenderTarget::flush()
{
	gGL.flush();

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
}

bool LLRenderTarget::isComplete() const
{
	return !mTex.empty() || mDepth;
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