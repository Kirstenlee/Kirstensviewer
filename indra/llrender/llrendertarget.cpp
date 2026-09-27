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
        // S24: DirectComposition's IDCompositionDevice::CreateSurface only
        // accepts a small fixed set of formats (B8G8R8A8_UNORM chief among
        // them) - DXGI_FORMAT_R8G8B8A8_UNORM is rejected outright
        // (E_INVALIDARG), and CopySubresourceRegion can't convert between
        // the two (different typeless families, not just a byte swap).
        // GL_BGRA lets a render target be allocated directly in the
        // composition-surface-compatible channel order, so the copy in
        // DXCompositionSurface::update() is a same-format GPU copy.
        case GL_BGRA:      return DXGI_FORMAT_B8G8R8A8_UNORM;
        default:
            LL_WARNS("RenderTarget") << "glColorFormatToDX: unmapped GL format 0x" << std::hex << color_fmt << std::dec << ", defaulting to RGBA8" << LL_ENDL;
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        }
    }
}
#endif

LLRenderTarget* LLRenderTarget::sBoundTarget = NULL;
U32 LLRenderTarget::sBytesAllocated = 0;

bool LLRenderTarget::sUseFBO = false;
U32 LLRenderTarget::sCurFBO = 0;


extern S32 gDXViewport[4];

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

	mDXRenderTarget.resize(resx, resy);
	mResX = resx;
	mResY = resy;
}

bool LLRenderTarget::allocate(U32 resx, U32 resy, U32 color_fmt, bool depth, LLTexUnit::eTextureType usage, LLTexUnit::eTextureMipGeneration generateMipMaps)
{
	llassert(usage == LLTexUnit::TT_TEXTURE);
	llassert(!isBoundInStack());

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

	// S24: color_fmt==0 is an established "no color attachment, depth-only"
	// convention (pipeline.cpp's shadow[i]/mSpotShadow[i] allocations) -
	// route it straight to DXGI_FORMAT_UNKNOWN rather than through
	// glColorFormatToDX(), which has no case for 0. DXRenderTarget::
	// allocate() skips creating a color attachment when it sees
	// DXGI_FORMAT_UNKNOWN.
	return mDXRenderTarget.allocate(resx, resy, color_fmt == 0 ? DXGI_FORMAT_UNKNOWN : glColorFormatToDX(color_fmt), depth);
}

void LLRenderTarget::setColorAttachment(LLImageDX* img, LLGLuint use_name)
{
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
}

void LLRenderTarget::releaseColorAttachment()
{
	// Mirrors setColorAttachment()'s DX_RENDER no-op above - see its comment.
	return;
}

bool LLRenderTarget::addColorAttachment(U32 color_fmt)
{
	llassert(!isBoundInStack());

	if (color_fmt == 0)
		return true; // No attachment requested

	return mDXRenderTarget.addColorAttachment(glColorFormatToDX(color_fmt));
}

bool LLRenderTarget::allocateDepth()
{
	if (!mDXRenderTarget.allocateDepth())
	{
		return false;
	}
	mUseDepth = true;
	return true;
}

void LLRenderTarget::shareDepthBuffer(LLRenderTarget& target)
{
	llassert(!isBoundInStack());

	// mFBO/mDepth stay 0 under DX_RENDER (no GL resources are ever created),
	// so the GL preconditions below would misfire - check the real DX state.
	mDXRenderTarget.shareDepthBuffer(target.mDXRenderTarget);
	target.mUseDepth = mUseDepth;
}

void LLRenderTarget::release()
{
	llassert(!isBoundInStack());

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
}

void LLRenderTarget::bindTarget(bool bind_depth)
{
	llassert(!isBoundInStack());

	// mFBO stays 0 under DX_RENDER (no GL FBO is ever created) - the
	// mPreviousRT/sBoundTarget stack bookkeeping below is shared/backend-
	// agnostic (DXRenderTarget deliberately has no bind-stack of its own -
	// see its header comment), so it's still updated here.
	mDXRenderTarget.bindTarget(bind_depth);
	mPreviousRT = sBoundTarget;
	sBoundTarget = this;
}

void LLRenderTarget::clear(U32 mask_in)
{
	mDXRenderTarget.clear((mask_in & GL_COLOR_BUFFER_BIT) != 0, mUseDepth && (mask_in & GL_DEPTH_BUFFER_BIT) != 0);
}

void LLRenderTarget::clearColor(float r, float g, float b, float a)
{
	mDXRenderTarget.clearColor(r, g, b, a);
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
}

void LLRenderTarget::flush()
{
	gDX.flush();

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
		// restore gDXViewport explicitly here, or any RT stack unwind to the
		// back buffer mid-frame widens the live D3D11 viewport back to the
		// full window.
		D3D11_VIEWPORT vp = {};
		vp.TopLeftX = (float)gDXViewport[0];
		vp.TopLeftY = (float)(gDXSwapChain.getHeight() - (gDXViewport[1] + gDXViewport[3]));
		vp.Width = (float)gDXViewport[2];
		vp.Height = (float)gDXViewport[3];
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		gDXDevice.getContext()->RSSetViewports(1, &vp);
	}
}

bool LLRenderTarget::isComplete() const
{
	// mTex/mDepth stay empty/0 under DX_RENDER (no GL resources are ever
	// created) - check the real DX-side state instead.
	return mDXRenderTarget.getNumColorAttachments() > 0 || mUseDepth;
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
	llassert(!isBoundInStack() && !other.isBoundInStack());
	llassert(mResX == other.mResX && mResY == other.mResY);
	llassert(mUsage == other.mUsage);

	using std::swap;
	// S24: mFBO/mTex are vestigial GL-era fields, always 0/empty under
	// DX_RENDER (see release()) - swapping only those was a no-op on the
	// real GPU resources. The actual D3D11 attachments live in
	// mDXRenderTarget (real RTV/SRV/DSV pointers, no user-declared copy/move
	// so std::swap exchanges them safely) - callers depending on this
	// actually exchanging content (LLGLTFMaterialPreviewMgr's exposure-map
	// hide/restore and no-AA screen/mPostPingMap swap) need that swapped,
	// not just the dead fields.
	swap(mDXRenderTarget, other.mDXRenderTarget);
	swap(mUseDepth, other.mUseDepth);
	swap(mFBO, other.mFBO);
	swap(mTex, other.mTex);
}