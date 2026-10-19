/**
 * @file llvertexbuffer.cpp
 * @brief LLVertexBuffer implementation
 *
 * $LicenseInfo:firstyear=2003&license=viewerlgpl$
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

#include "llfasttimer.h"
#include "llsys.h"
#include "llvertexbuffer.h"
#include "llgl.h"

#include "llglheaders.h"
#include "llrender.h"
#include "llvector4a.h"
#include "llshadermgr.h"
#include "llhlslshader.h"
#include "llmemory.h"
#include <glm/gtc/type_ptr.hpp>
#include <future>
#include <memory>

#include "DXDevice.h"
#include "DXStateCache.h"
#include "DXVertexLayout.h"

//Next Highest Power Of Two
//helper function, returns first number > v that is a power of 2, or v if v is already a power of 2
U32 nhpo2(U32 v)
{
	if (v == 0) return 1; // Special case when input is 0

	--v; // Subtract 1 to account for numbers that are already powers of 2
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16; // Works for up to 32-bit integers
	return v + 1;
}

//which power of 2 is i?
//assumes i is a power of 2 > 0
U32 wpo2(U32 i)
{
	llassert(i > 0);
	llassert(nhpo2(i) == i);

	return 31 - _lzcnt_u32(i);   // S24: MSVC x86 intrinsic
}

struct CompareMappedRegion
{
	bool operator()(const LLVertexBuffer::MappedRegion& lhs, const LLVertexBuffer::MappedRegion& rhs)
	{
		return lhs.mStart < rhs.mStart;
	}
};

//============================================================================
// S24: the legacy GL VBO-name pool (LLVBOPool/LLAppleVBOPool/LLDefaultVBOPool,
// gen_buffer()/delete_buffers(), sVBOPool) removed outright - genuinely dead
// under DX_RENDER. LLVertexBuffer::initClass() never instantiates sVBOPool
// (it's a no-op under DX_RENDER, see below), so every caller into this pool
// was unreachable. The real allocation path is LLVertexBuffer::genBuffer()/
// genIndices() going straight to mDXBuffer/mDXIndices (D3D11-native, no
// pooling needed - that was a GL driver-quirk workaround).


void LLVertexBufferData::drawWithMatrix()
{
	if (!mVB)
	{
		llassert(false);
		// Not supposed to happen, check buffer generation
		return;
	}

	// S24: see LLVertexBufferData::mDXImage's comment - replay via a real
	// bind(LLImageDX*) instead of GL's raw-GLuint bindManual(mTexName).
	if (mDXImage)
	{
		gDX.getTexUnit(0)->bind(mDXImage.get());
	}
	else
	{
		gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
	}
	// S24: see LLVertexBufferData::mDXShader's comment - force the shader
	// that was actually bound when this batch was recorded back active
	// before drawing, rather than trusting whatever's ambiently bound now.
	if (mDXShader)
	{
		mDXShader->bind();
	}

	gDX.matrixMode(LLRender::MM_MODELVIEW);
	gDX.pushMatrix();
	gDX.loadMatrix(glm::value_ptr(mModelView));
	gDX.matrixMode(LLRender::MM_PROJECTION);
	gDX.pushMatrix();
	gDX.loadMatrix(glm::value_ptr(mProjection));
	gDX.matrixMode(LLRender::MM_TEXTURE0);
	gDX.pushMatrix();
	gDX.loadMatrix(glm::value_ptr(mTexture0));

	mVB->setBuffer();
	mVB->drawArrays(mMode, 0, mCount);

	gDX.popMatrix();
	gDX.matrixMode(LLRender::MM_PROJECTION);
	gDX.popMatrix();
	gDX.matrixMode(LLRender::MM_MODELVIEW);
	gDX.popMatrix();
}

void LLVertexBufferData::draw()
{
	if (!mVB)
	{
		llassert(false);
		// Not supposed to happen, check buffer generation
		return;
	}

	// S24: see LLVertexBufferData::mDXImage's comment - replay via a real
	// bind(LLImageDX*) instead of GL's raw-GLuint bindManual(mTexName).
	if (mDXImage)
	{
		gDX.getTexUnit(0)->bind(mDXImage.get());
	}
	else
	{
		gDX.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
	}
	// S24: see LLVertexBufferData::mDXShader's comment.
	if (mDXShader)
	{
		mDXShader->bind();
	}

	mVB->setBuffer();
	mVB->drawArrays(mMode, 0, mCount);
}

//============================================================================

//static
U64 LLVertexBuffer::getBytesAllocated()
{
	// S24: no VBO pool under DX_RENDER (see initClass()) to report usage from.
	return 0;
}

//============================================================================
//
//static
thread_local U32 LLVertexBuffer::sGLRenderBuffer = 0;
thread_local U32 LLVertexBuffer::sGLRenderIndices = 0;
thread_local ID3D11Buffer* LLVertexBuffer::sDXRenderBuffer = nullptr;
thread_local ID3D11Buffer* LLVertexBuffer::sDXRenderIndices = nullptr;
thread_local LLHLSLShader* LLVertexBuffer::sDXLastShader = nullptr;

U32 LLVertexBuffer::sLastMask = 0;
U32 LLVertexBuffer::sVertexCount = 0;

//NOTE: each component must be AT LEAST 4 bytes in size to avoid a performance penalty on AMD hardware
const U32 LLVertexBuffer::sTypeSize[LLVertexBuffer::TYPE_MAX] =
{
	sizeof(LLVector4), // TYPE_VERTEX,
	sizeof(LLVector4), // TYPE_NORMAL,
	sizeof(LLVector2), // TYPE_TEXCOORD0,
	sizeof(LLVector2), // TYPE_TEXCOORD1,
	sizeof(LLVector2), // TYPE_TEXCOORD2,
	sizeof(LLVector2), // TYPE_TEXCOORD3,
	sizeof(LLColor4U), // TYPE_COLOR,
	sizeof(LLColor4U), // TYPE_EMISSIVE, only alpha is used currently
	sizeof(LLVector4), // TYPE_TANGENT,
	sizeof(F32),       // TYPE_WEIGHT,
	sizeof(LLVector4), // TYPE_WEIGHT4,
	sizeof(LLVector4), // TYPE_CLOTHWEIGHT,
	sizeof(U64),       // TYPE_JOINT,
	sizeof(LLVector4), // TYPE_TEXTURE_INDEX (actually exists as position.w), no extra data, but stride is 16 bytes
};

static const std::string vb_type_name[] =
{
	"TYPE_VERTEX",
	"TYPE_NORMAL",
	"TYPE_TEXCOORD0",
	"TYPE_TEXCOORD1",
	"TYPE_TEXCOORD2",
	"TYPE_TEXCOORD3",
	"TYPE_COLOR",
	"TYPE_EMISSIVE",
	"TYPE_TANGENT",
	"TYPE_WEIGHT",
	"TYPE_WEIGHT4",
	"TYPE_CLOTHWEIGHT",
	"TYPE_JOINT"
	"TYPE_TEXTURE_INDEX",
	"TYPE_MAX",
	"TYPE_INDEX",
};

const U32 LLVertexBuffer::sGLMode[LLRender::NUM_MODES] =
{
	GL_TRIANGLES,
	GL_TRIANGLE_STRIP,
	GL_TRIANGLE_FAN,
	GL_POINTS,
	GL_LINES,
	GL_LINE_STRIP,
	GL_LINE_LOOP,
};

//static
void LLVertexBuffer::setupClientArrays(U32 data_mask)
{
	if (sLastMask != data_mask)
	{
		// S24: zero callers under DX_RENDER (see llhlslshader.cpp's bind()
		// comment) - the glEnableVertexAttribArray/glDisableVertexAttribArray
		// calls that used to live here are stubbed out rather than deleted,
		// since this function's mask-diffing approach has no real DX_RENDER
		// translation anyway (D3D11's Input Layout declares the active
		// attribute set up front - there's no runtime per-attribute toggle).
		// A real port, if a converted pool ever needs this, means building a
		// DXVertexLayout variant for `data_mask`, not porting these two calls.
		for (U32 i = 0; i < TYPE_MAX; ++i)
		{
			U32 mask = 1 << i;

			if (sLastMask & (1 << i))
			{ //was enabled
				if (!(data_mask & mask))
				{ //needs to be disabled (no-op, see above)
				}
			}
			else
			{   //was disabled
				if (data_mask & mask)
				{ //needs to be enabled (no-op, see above)
				}
			}
		}
	}

	sLastMask = data_mask;
}

//static
void LLVertexBuffer::drawArrays(U32 mode, const std::vector<LLVector3>& pos)
{
	gDX.begin(mode);
	for (auto& v : pos)
	{
		gDX.vertex3fv(v.mV);
	}
	gDX.end();
	gDX.flush();
}

//static
void LLVertexBuffer::drawElements(U32 mode, const LLVector4a* pos, const LLVector2* tc, U32 num_indices, const U16* indicesp)
{
	llassert(LLHLSLShader::sCurBoundShaderPtr != NULL);

	STOP_GLERROR;

	gDX.syncMatrices();

	unbind();

	gDX.begin(mode);

	if (tc != nullptr)
	{
		for (U32 i = 0; i < num_indices; ++i)
		{
			U16 idx = indicesp[i];
			gDX.texCoord2fv(tc[idx].mV);
			gDX.vertex3fv(pos[idx].getF32ptr());
		}
	}
	else
	{
		for (U32 i = 0; i < num_indices; ++i)
		{
			U16 idx = indicesp[i];
			gDX.vertex3fv(pos[idx].getF32ptr());
		}
	}
	gDX.end();
	gDX.flush();
}

bool LLVertexBuffer::validateRange(U32 start, U32 end, U32 count, U32 indices_offset) const
{
	if (!gDebugGL)
	{
		return true;
	}

	llassert(start < mNumVerts);
	llassert(end < mNumVerts);

	if (start >= mNumVerts ||
		end >= mNumVerts)
	{
		LL_ERRS() << "Bad vertex buffer draw range: [" << start << ", " << end << "] vs " << mNumVerts << LL_ENDL;
	}

	if (indices_offset >= mNumIndices ||
		indices_offset + count > mNumIndices)
	{
		LL_ERRS() << "Bad index buffer draw range: [" << indices_offset << ", " << indices_offset + count << "]" << LL_ENDL;
	}

	return true;
}

#if LL_PROFILER_ENABLE_RENDER_DOC
void LLVertexBuffer::setLabel(const char* label) {
}
#endif

void LLVertexBuffer::clone(LLVertexBuffer& target) const
{
	target.mTypeMask = mTypeMask;
	target.mIndicesType = mIndicesType;
	target.mIndicesStride = mIndicesStride;
	if (target.getNumVerts() != getNumVerts() ||
		target.getNumIndices() != getNumIndices())
	{
		target.allocateBuffer(getNumVerts(), getNumIndices());
	}
}

namespace
{
	// D3D11 has no equivalent of GL_TRIANGLE_FAN or GL_LINE_LOOP (dropped
	// after D3D9) - mapped to D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED and asserted
	// against below. Neither is used by the base (unrigged, non-UI) geometry
	// converted so far; revisit if/when a pool that needs one is converted.
	const D3D11_PRIMITIVE_TOPOLOGY sDXMode[LLRender::NUM_MODES] =
	{
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,  // TRIANGLES
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, // TRIANGLE_STRIP
		D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED,     // TRIANGLE_FAN - not yet supported
		D3D11_PRIMITIVE_TOPOLOGY_POINTLIST,     // POINTS
		D3D11_PRIMITIVE_TOPOLOGY_LINELIST,      // LINES
		D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP,     // LINE_STRIP
		D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED,     // LINE_LOOP - not yet supported
	};

	// S24: this is the universal DX_RENDER draw chokepoint (every 3D pool
	// draw, every LLRender::flush() call, every direct drawRange()/
	// drawArrays() caller) - re-asserts VS/PS here in case some raw
	// ctx->VSSetShader()/PSSetShader() call elsewhere didn't update
	// LLHLSLShader::sCurBoundShaderPtr, leaving stale GPU-bound shaders with
	// no warning. Cache-and-compare, not unconditional rebind - D3D11
	// SetShader calls aren't free at this call frequency.
	ID3D11VertexShader* sLastBoundVS = nullptr;
	ID3D11PixelShader* sLastBoundPS = nullptr;

	// If sCurBoundShaderPtr is null, leave the context untouched - this is
	// the deliberate post-unbind() state the two raw-bind sites above already
	// rely on (unbind() nulls both the tracker and the real VS/PS so the
	// NEXT real bind() is forced to do a genuine rebind); asserting a shader
	// here when the tracker says "none" would fight that convention instead
	// of complementing it.
	void assertShaderStagesBound()
	{
		LLHLSLShader* shader = LLHLSLShader::sCurBoundShaderPtr;
		if (!shader)
		{
			return;
		}
		ID3D11VertexShader* vs = shader->mDXVertexShader.getVS();
		ID3D11PixelShader* ps = shader->mDXPixelShader.getPS();
		if (vs != sLastBoundVS || ps != sLastBoundPS)
		{
			gDXDevice.getContext()->VSSetShader(vs, nullptr, 0);
			gDXDevice.getContext()->PSSetShader(ps, nullptr, 0);
			sLastBoundVS = vs;
			sLastBoundPS = ps;
		}
	}
}

void LLVertexBuffer::drawRange(U32 mode, U32 start, U32 end, U32 count, U32 indices_offset) const
{
	llassert(validateRange(start, end, count, indices_offset));

	llassert(mDXBuffer.getBuffer() == sDXRenderBuffer);
	llassert(mDXIndices.getBuffer() == sDXRenderIndices);
	llassert(sDXMode[mode] != D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED);
	gDX.syncMatrices();
	assertShaderStagesBound();
	ID3D11DeviceContext* ctx = gDXDevice.getContext();
	DXStateCache::setPrimitiveTopology(ctx, sDXMode[mode]);
	ctx->DrawIndexed(count, indices_offset, 0);
	// S24: NOT a leftover diagnostic - this is the permanent, settings-gated
	// (S24DXDebugLayerEnabled) mechanism behind the D3D11 debug-layer
	// end-of-session tally and per-draw shader correlation. Do not remove as
	// part of a diagnostic sweep. logPendingDebugMessages() only does real
	// logging/I/O the first time each distinct message ID is seen, so this
	// is cheap even called on every draw.
	gDXDevice.logPendingDebugMessages(LLHLSLShader::sCurBoundShaderPtr ? LLHLSLShader::sCurBoundShaderPtr->mName.c_str() : "?");
	return;
}

void LLVertexBuffer::drawRangeFast(U32 mode, U32 start, U32 end, U32 count, U32 indices_offset) const
{
	// S24: no gDX.syncMatrices() call in this "fast" variant - callers are
	// expected to have already synced matrices themselves - but the
	// shader-stage assertion still applies, see assertShaderStagesBound().
	assertShaderStagesBound();
	ID3D11DeviceContext* ctx = gDXDevice.getContext();
	DXStateCache::setPrimitiveTopology(ctx, sDXMode[mode]);
	ctx->DrawIndexed(count, indices_offset, 0);
	gDXDevice.logPendingDebugMessages(LLHLSLShader::sCurBoundShaderPtr ? LLHLSLShader::sCurBoundShaderPtr->mName.c_str() : "?");
	return;
}

void LLVertexBuffer::draw(U32 mode, U32 count, U32 indices_offset) const
{
	drawRange(mode, 0, mNumVerts - 1, count, indices_offset);
}

void LLVertexBuffer::drawArrays(U32 mode, U32 first, U32 count) const
{
	llassert(first + count <= mNumVerts);

	llassert(mDXBuffer.getBuffer() == sDXRenderBuffer);
	llassert(mDXIndices.getBuffer() == sDXRenderIndices);
	llassert(sDXMode[mode] != D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED);
	gDX.syncMatrices();
	assertShaderStagesBound();
	ID3D11DeviceContext* ctx = gDXDevice.getContext();
	DXStateCache::setPrimitiveTopology(ctx, sDXMode[mode]);
	ctx->Draw(count, first);
	gDXDevice.logPendingDebugMessages(LLHLSLShader::sCurBoundShaderPtr ? LLHLSLShader::sCurBoundShaderPtr->mName.c_str() : "?");
	return;
}

//static
void LLVertexBuffer::initClass(LLWindow* window)
{
	// genBuffer()/genIndices() never used a pool under DX_RENDER - GL's
	// VBO-orphaning pool and worker-thread-based deferred buffer creation are
	// driver-quirk workarounds with no D3D11 equivalent need, so there's
	// nothing to initialize here.
	return;
}

//static
void LLVertexBuffer::unbind()
{
	// No GL bind-state concept under DX_RENDER; IASetVertexBuffers/
	// IASetIndexBuffer are reissued fresh at every setBuffer() call, so
	// there's nothing to explicitly unbind on the GPU side - just reset the
	// bookkeeping fields callers (setBuffer(), drawRange()'s assert) check.
	sDXRenderBuffer = nullptr;
	sDXRenderIndices = nullptr;
}

//static
void LLVertexBuffer::cleanupClass()
{
	unbind();

	DXVertexLayout::clear();
}

//----------------------------------------------------------------------------

LLVertexBuffer::LLVertexBuffer(U32 typemask)
	: LLRefCount(),
	mTypeMask(typemask)
{
	//zero out offsets
	for (U32 i = 0; i < TYPE_MAX; i++)
	{
		mOffsets[i] = 0;
	}
}

// list of mapped buffers
// NOTE: must not be LLPointer<LLVertexBuffer> to avoid breaking non-ref-counted LLVertexBuffer instances
static std::vector<LLVertexBuffer*> sMappedBuffers;

//static
void LLVertexBuffer::flushBuffers()
{
	// must only be called from main thread
	for (auto& buffer : sMappedBuffers)
	{
		buffer->_unmapBuffer();
		buffer->mMapped = false;
	}

	sMappedBuffers.resize(0);
}

//static
U32 LLVertexBuffer::calcOffsets(const U32& typemask, U32* offsets, U32 num_vertices)
{
	U32 offset = 0;
	for (U32 i = 0; i < TYPE_TEXTURE_INDEX; i++)
	{
		U32 mask = 1 << i;
		if (typemask & mask)
		{
			if (offsets && LLVertexBuffer::sTypeSize[i])
			{
				offsets[i] = offset;
				offset += LLVertexBuffer::sTypeSize[i] * num_vertices;
				offset = (offset + 0xF) & ~0xF;
			}
		}
	}

	offsets[TYPE_TEXTURE_INDEX] = offsets[TYPE_VERTEX] + 12;

	return offset;
}

//static
U32 LLVertexBuffer::calcVertexSize(const U32& typemask)
{
	U32 size = 0;
	for (U32 i = 0; i < TYPE_TEXTURE_INDEX; i++)
	{
		U32 mask = 1 << i;
		if (typemask & mask)
		{
			size += LLVertexBuffer::sTypeSize[i];
		}
	}

	return size;
}

// protected, use unref()
//virtual
LLVertexBuffer::~LLVertexBuffer()
{
	if (mMapped)
	{ // is on the mapped buffer list but doesn't need to be flushed
		mMapped = false;
		unmapBuffer();
	}

	destroyGLBuffer();
	destroyGLIndices();

	if (mMappedData)
	{
		LL_ERRS() << "Failed to clear vertex buffer's vertices" << LL_ENDL;
	}
	if (mMappedIndexData)
	{
		LL_ERRS() << "Failed to clear vertex buffer's indices" << LL_ENDL;
	}
};

//----------------------------------------------------------------------------

void LLVertexBuffer::genBuffer(U32 size)
{

	// No VBO-orphaning pool under DX_RENDER (that's a GL driver-quirk
	// workaround with no D3D11 equivalent need) - mMappedData is just a
	// plain CPU shadow buffer; mDXBuffer is the real GPU-side resource,
	// uploaded from it in flush_vbo().
	llassert(mSize == 0);
	llassert(mMappedData == nullptr);

	mSize = size;
	mMappedData = (U8*)ll_aligned_malloc_16(size);
	mDXBuffer.createVertexBuffer(size, nullptr);
}

void LLVertexBuffer::genIndices(U32 size)
{

	llassert(mIndicesSize == 0);
	llassert(mMappedIndexData == nullptr);

	mIndicesSize = size;
	mMappedIndexData = (U8*)ll_aligned_malloc_16(size);
	mDXIndices.createIndexBuffer(size, nullptr);
}

bool LLVertexBuffer::createGLBuffer(U32 size)
{
	if (mGLBuffer || mMappedData)
	{
		destroyGLBuffer();
	}

	if (size == 0)
	{
		return true;
	}

	bool success = true;

	genBuffer(size);

	if (!mMappedData)
	{
		success = false;
	}
	return success;
}

bool LLVertexBuffer::createGLIndices(U32 size)
{
	if (mGLIndices)
	{
		destroyGLIndices();
	}

	if (size == 0)
	{
		return true;
	}

	bool success = true;

	genIndices(size);

	if (!mMappedIndexData)
	{
		success = false;
	}
	return success;
}

void LLVertexBuffer::destroyGLBuffer()
{
	if (mGLBuffer || mMappedData)
	{
		if (mMappedData)
		{
			ll_aligned_free_16(mMappedData);
		}
		mDXBuffer.destroy();

		mSize = 0;
		mGLBuffer = 0;
		mMappedData = nullptr;
	}
}

void LLVertexBuffer::destroyGLIndices()
{
	if (mGLIndices || mMappedIndexData)
	{
		if (mMappedIndexData)
		{
			ll_aligned_free_16(mMappedIndexData);
		}
		mDXIndices.destroy();

		mIndicesSize = 0;
		mGLIndices = 0;
		mMappedIndexData = nullptr;
	}
}

bool LLVertexBuffer::updateNumVerts(U32 nverts)
{
	llassert(nverts >= 0);

	bool success = true;

	U32 needed_size = calcOffsets(mTypeMask, mOffsets, nverts);

	if (needed_size != mSize)
	{
		success &= createGLBuffer(needed_size);
	}

	llassert(mSize == needed_size);
	mNumVerts = nverts;
	return success;
}

bool LLVertexBuffer::updateNumIndices(U32 nindices)
{
	llassert(nindices >= 0);

	bool success = true;

	U32 needed_size = sizeof(U16) * nindices;

	if (needed_size != mIndicesSize)
	{
		success &= createGLIndices(needed_size);
	}

	llassert(mIndicesSize == needed_size);
	mNumIndices = nindices;
	return success;
}

bool LLVertexBuffer::allocateBuffer(U32 nverts, U32 nindices)
{
	if (nverts < 0 || nindices < 0)
	{
		LL_ERRS() << "Bad vertex buffer allocation: " << nverts << " : " << nindices << LL_ENDL;
	}

	bool success = true;

	success &= updateNumVerts(nverts);
	success &= updateNumIndices(nindices);

	return success;
}

//----------------------------------------------------------------------------

// if no gap between region and given range exists, expand region to cover given range and return true
// otherwise return false
bool expand_region(LLVertexBuffer::MappedRegion& region, U32 start, U32 end)
{
	if (end < region.mStart ||
		start > region.mEnd)
	{ //gap exists, do not merge
		return false;
	}

	region.mStart = llmin(region.mStart, start);
	region.mEnd = llmax(region.mEnd, end);

	return true;
}

// Map for data access
U8* LLVertexBuffer::mapVertexBuffer(LLVertexBuffer::AttributeType type, U32 index, S32 count)
{
	_mapBuffer();

	if (count == -1)
	{
		count = mNumVerts - index;
	}

	if (!gGLManager.mIsApple)
	{
		U32 start = mOffsets[type] + sTypeSize[type] * index;
		U32 end = start + sTypeSize[type] * count - 1;

		bool flagged = false;
		// flag region as mapped
		for (U32 i = 0; i < mMappedVertexRegions.size(); ++i)
		{
			MappedRegion& region = mMappedVertexRegions[i];
			if (expand_region(region, start, end))
			{
				flagged = true;
				break;
			}
		}

		if (!flagged)
		{
			//didn't expand an existing region, make a new one
			mMappedVertexRegions.push_back({ start, end });
		}
	}
	return mMappedData + mOffsets[type] + sTypeSize[type] * index;
}

U8* LLVertexBuffer::mapIndexBuffer(U32 index, S32 count)
{
	_mapBuffer();

	if (count == -1)
	{
		count = mNumIndices - index;
	}

	if (!gGLManager.mIsApple)
	{
		U32 start = sizeof(U16) * index;
		U32 end = start + sizeof(U16) * count - 1;

		bool flagged = false;
		// flag region as mapped
		for (U32 i = 0; i < mMappedIndexRegions.size(); ++i)
		{
			MappedRegion& region = mMappedIndexRegions[i];
			if (expand_region(region, start, end))
			{
				flagged = true;
				break;
			}
		}

		if (!flagged)
		{
			//didn't expand an existing region, make a new one
			mMappedIndexRegions.push_back({ start, end });
		}
	}

	return mMappedIndexData + sizeof(U16) * index;
}

// flush the given byte range
//  target -- "target" parameter for glBufferSubData
//  start -- first byte to copy
//  end -- last byte to copy (NOT last byte + 1)
//  data -- data to be flushed
//  dst -- mMappedData or mMappedIndexData
void LLVertexBuffer::flush_vbo(DXenum target, U32 start, U32 end, void* data, U8* dst)
{
	if (gGLManager.mIsApple)
	{
		// on OS X, flush_vbo doesn't actually write to the GL buffer, so be sure to call
		// _mapBuffer to tag the buffer for flushing to GL
		_mapBuffer();
		STOP_GLERROR;
		// copy into mapped buffer
		memcpy(dst + start, data, end - start + 1);
	}
	else
	{
		// D3D11_USAGE_DYNAMIC has no partial-range update - Map(WRITE_DISCARD)
		// invalidates the whole resource, so dst (mMappedData/mMappedIndexData)
		// must hold the complete, authoritative buffer contents before the
		// full-buffer upload. Callers that already write directly into dst
		// (mapVertexBuffer()/getXXXStrider()) pass data == dst + start, so this
		// memcpy is skipped; callers with an external source (setPositionData()
		// and friends) need this copy to keep dst authoritative for the bytes
		// outside their own touched range.
		if (end != 0 && (U8*)data != dst + start)
		{
			memcpy(dst + start, data, end - start + 1);
		}

		DXBuffer& buf = (target == GL_ARRAY_BUFFER) ? mDXBuffer : mDXIndices;
		U32 full_size = (target == GL_ARRAY_BUFFER) ? mSize : mIndicesSize;
		buf.upload(dst, full_size);
	}
}

void LLVertexBuffer::unmapBuffer()
{
	flushBuffers();
}

void LLVertexBuffer::_mapBuffer()
{
	if (!mMapped)
	{
		mMapped = true;
		sMappedBuffers.push_back(this);
	}
}

void LLVertexBuffer::_unmapBuffer()
{
	STOP_GLERROR;
	if (!mMapped)
	{
		return;
	}

	struct SortMappedRegion
	{
		bool operator()(const MappedRegion& lhs, const MappedRegion& rhs)
		{
			return lhs.mStart < rhs.mStart;
		}
	};

	// S24: the gGLManager.mIsApple branch that used to live here (real
	// glBindBuffer/glBufferData/gen_buffer() calls) was dead on Windows and
	// removed outright - this is the only reachable path under DX_RENDER.
	if (!mMappedVertexRegions.empty())
	{

		U32 start = 0;
		U32 end = 0;

		std::sort(mMappedVertexRegions.begin(), mMappedVertexRegions.end(), SortMappedRegion());

		for (U32 i = 0; i < mMappedVertexRegions.size(); ++i)
		{
			const MappedRegion& region = mMappedVertexRegions[i];
			if (region.mStart == end + 1)
			{
				end = region.mEnd;
			}
			else
			{
				flush_vbo(GL_ARRAY_BUFFER, start, end, (U8*)mMappedData + start, mMappedData);
				start = region.mStart;
				end = region.mEnd;
			}
		}

		flush_vbo(GL_ARRAY_BUFFER, start, end, (U8*)mMappedData + start, mMappedData);
		mMappedVertexRegions.clear();
	}

	if (!mMappedIndexRegions.empty())
	{

		U32 start = 0;
		U32 end = 0;

		std::sort(mMappedIndexRegions.begin(), mMappedIndexRegions.end(), SortMappedRegion());

		for (U32 i = 0; i < mMappedIndexRegions.size(); ++i)
		{
			const MappedRegion& region = mMappedIndexRegions[i];
			if (region.mStart == end + 1)
			{
				end = region.mEnd;
			}
			else
			{
				flush_vbo(GL_ELEMENT_ARRAY_BUFFER, start, end, (U8*)mMappedIndexData + start, mMappedIndexData);
				start = region.mStart;
				end = region.mEnd;
			}
		}

		flush_vbo(GL_ELEMENT_ARRAY_BUFFER, start, end, (U8*)mMappedIndexData + start, mMappedIndexData);
		mMappedIndexRegions.clear();
	}
}

//----------------------------------------------------------------------------

template <class T, LLVertexBuffer::AttributeType type> struct VertexBufferStrider
{
	typedef LLStrider<T> strider_t;
	static bool get(LLVertexBuffer& vbo,
		strider_t& strider,
		S32 index, S32 count)
	{
		if (type == LLVertexBuffer::TYPE_INDEX)
		{
			U8* ptr = vbo.mapIndexBuffer(index, count);

			if (ptr == NULL)
			{
				LL_WARNS() << "mapIndexBuffer failed!" << LL_ENDL;
				return false;
			}

			strider = (T*)ptr;
			strider.setStride(0);
			return true;
		}
		else if (vbo.hasDataType(type))
		{
			U32 stride = LLVertexBuffer::sTypeSize[type];

			U8* ptr = vbo.mapVertexBuffer(type, index, count);

			if (ptr == NULL)
			{
				LL_WARNS() << "mapVertexBuffer failed!" << LL_ENDL;
				return false;
			}

			strider = (T*)ptr;
			strider.setStride(stride);
			return true;
		}
		else
		{
			LL_ERRS() << "VertexBufferStrider could not find valid vertex data." << LL_ENDL;
		}
		return false;
	}
};

bool LLVertexBuffer::getVertexStrider(LLStrider<LLVector3>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLVector3, TYPE_VERTEX>::get(*this, strider, index, count);
}
bool LLVertexBuffer::getVertexStrider(LLStrider<LLVector4a>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLVector4a, TYPE_VERTEX>::get(*this, strider, index, count);
}
bool LLVertexBuffer::getIndexStrider(LLStrider<U16>& strider, U32 index, S32 count)
{
	llassert(mIndicesStride == 2); // cannot access 32-bit indices with U16 strider
	llassert(mIndicesType == GL_UNSIGNED_SHORT);
	return VertexBufferStrider<U16, TYPE_INDEX>::get(*this, strider, index, count);
}
bool LLVertexBuffer::getTexCoord0Strider(LLStrider<LLVector2>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLVector2, TYPE_TEXCOORD0>::get(*this, strider, index, count);
}
bool LLVertexBuffer::getTexCoord1Strider(LLStrider<LLVector2>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLVector2, TYPE_TEXCOORD1>::get(*this, strider, index, count);
}
bool LLVertexBuffer::getTexCoord2Strider(LLStrider<LLVector2>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLVector2, TYPE_TEXCOORD2>::get(*this, strider, index, count);
}
bool LLVertexBuffer::getNormalStrider(LLStrider<LLVector3>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLVector3, TYPE_NORMAL>::get(*this, strider, index, count);
}
bool LLVertexBuffer::getNormalStrider(LLStrider<LLVector4a>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLVector4a, TYPE_NORMAL>::get(*this, strider, index, count);
}
bool LLVertexBuffer::getTangentStrider(LLStrider<LLVector3>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLVector3, TYPE_TANGENT>::get(*this, strider, index, count);
}
bool LLVertexBuffer::getTangentStrider(LLStrider<LLVector4a>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLVector4a, TYPE_TANGENT>::get(*this, strider, index, count);
}
bool LLVertexBuffer::getColorStrider(LLStrider<LLColor4U>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLColor4U, TYPE_COLOR>::get(*this, strider, index, count);
}
bool LLVertexBuffer::getEmissiveStrider(LLStrider<LLColor4U>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLColor4U, TYPE_EMISSIVE>::get(*this, strider, index, count);
}
bool LLVertexBuffer::getWeightStrider(LLStrider<F32>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<F32, TYPE_WEIGHT>::get(*this, strider, index, count);
}

bool LLVertexBuffer::getWeight4Strider(LLStrider<LLVector4>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLVector4, TYPE_WEIGHT4>::get(*this, strider, index, count);
}

bool LLVertexBuffer::getClothWeightStrider(LLStrider<LLVector4>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLVector4, TYPE_CLOTHWEIGHT>::get(*this, strider, index, count);
}

//----------------------------------------------------------------------------

// Set for rendering
void LLVertexBuffer::setBuffer()
{
	STOP_GLERROR;

	if (mMapped)
	{
		LL_WARNS_ONCE() << "Missing call to unmapBuffer or flushBuffers" << LL_ENDL;
		_unmapBuffer();
	}

	// no data may be pending
	llassert(mMappedVertexRegions.empty());
	llassert(mMappedIndexRegions.empty());

	// a shader must be bound
	llassert(LLHLSLShader::sCurBoundShaderPtr);

	// Unlike GL's exact data_mask/mTypeMask superset assert, D3D11 input
	// layouts tolerate a vertex buffer providing more attributes than the
	// shader consumes (extra elements are simply unused) - so there's no
	// equivalent check here. setupVertexBuffer() builds the layout from this
	// buffer's own mTypeMask; CreateInputLayout itself fails correctly (see
	// DXVertexLayout::getOrCreate()'s LL_WARNS) if the bound VS needs an
	// attribute this buffer lacks.
	//
	// S24: no "skip setupVertexBuffer() if sDXRenderBuffer/sDXLastShader
	// already match" dedup here - DXUIBatch::drawAndPop() and DXPipeline's
	// fullscreen-blit path both set the input layout/vertex buffers directly
	// without touching sDXRenderBuffer/sDXLastShader, so a dedup here could
	// wrongly skip re-establishing the input layout after one of those ran,
	// leaving the GPU to misinterpret this buffer's bytes under the wrong
	// layout - most visible for anything that redraws the SAME
	// LLVertexBuffer object across many frames (e.g. LLUIImage's
	// display-list cache). setupVertexBuffer() is a handful of cheap
	// state-setting calls, not a Draw() - unconditional is the safe default.
	sDXRenderBuffer = mDXBuffer.getBuffer();
	sDXLastShader = LLHLSLShader::sCurBoundShaderPtr;
	setupVertexBuffer();

	if (mDXIndices.getBuffer() != sDXRenderIndices)
	{
		sDXRenderIndices = mDXIndices.getBuffer();
		gDXDevice.getContext()->IASetIndexBuffer(sDXRenderIndices,
			mIndicesType == GL_UNSIGNED_SHORT ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
	}

	STOP_GLERROR;
	return;
}

// virtual (default)
void LLVertexBuffer::setupVertexBuffer()
{
	STOP_GLERROR;

	// LLVertexBuffer is struct-of-arrays (calcOffsets() gives each attribute
	// its own contiguous block spanning all vertices), not interleaved - so
	// each active attribute gets its own D3D11 input slot, all views into
	// the same mDXBuffer resource at different offsets/strides. Built from
	// this buffer's own mTypeMask (see setBuffer()'s comment on why, unlike
	// GL, there's no need to consult the bound shader's attribute mask here).
	// Enumeration order MUST match DXVertexLayout::getOrCreate()'s InputSlot
	// assignment (bits 0-5, then color/emissive, then tangent, then weight,
	// then clothweight).
	llassert(LLHLSLShader::sCurBoundShaderPtr);
	ID3DBlob* vs_bytecode = LLHLSLShader::sCurBoundShaderPtr->mDXVertexShader.getVSBytecode();
	if (!vs_bytecode)
	{
		return;
	}

	ID3D11Buffer* vb = mDXBuffer.getBuffer();
	// S24: 12, not 11, to fit MAP_WEIGHT4 - see DXVertexLayout.cpp's
	// matching elements[] array comment.
	ID3D11Buffer* buffers[12];
	UINT strides[12];
	UINT offsets[12];
	UINT count = 0;

	static const AttributeType kSimple[6] = { TYPE_VERTEX, TYPE_NORMAL, TYPE_TEXCOORD0, TYPE_TEXCOORD1, TYPE_TEXCOORD2, TYPE_TEXCOORD3 };
	for (AttributeType type : kSimple)
	{
		if (mTypeMask & (1u << type))
		{
			buffers[count] = vb;
			strides[count] = sTypeSize[type];
			offsets[count] = mOffsets[type];
			++count;
		}
	}

	if ((mTypeMask & MAP_COLOR) || (mTypeMask & MAP_EMISSIVE))
	{
		// S24: which data source (color vs emissive) feeds the shared COLOR0
		// slot depends on which one the BOUND SHADER wants, not just which
		// ones this buffer carries - a single VBO can carry both (any
		// alpha-blended face with classic Glow enabled), and the alpha-blend
		// pass (alphaV.hlsl) wants diffuse_color while the glow-accumulation
		// pass (emissiveV.hlsl/pbrglowV.hlsl) wants emissive, both declaring
		// plain "COLOR0" in HLSL so D3D11 reflection alone can't distinguish
		// them. Identify the emissive-accumulation shaders by name instead;
		// falls back to emissive if that's the only data this buffer carries.
		const std::string& bound_name = LLHLSLShader::sCurBoundShaderPtr->mName;
		bool wants_emissive_in_color0 =
			bound_name == "Deferred Emissive Shader" ||
			bound_name == " PBR Glow Shader" ||
			bound_name == "Skinned Deferred Emissive Shader" ||
			bound_name == "Skinned  PBR Glow Shader";
		AttributeType src = ((mTypeMask & MAP_EMISSIVE) && (wants_emissive_in_color0 || !(mTypeMask & MAP_COLOR)))
			? TYPE_EMISSIVE : TYPE_COLOR;
		buffers[count] = vb;
		strides[count] = sTypeSize[TYPE_COLOR];
		offsets[count] = mOffsets[src];
		++count;
	}

	if (mTypeMask & MAP_TANGENT)
	{
		buffers[count] = vb;
		strides[count] = sTypeSize[TYPE_TANGENT];
		offsets[count] = mOffsets[TYPE_TANGENT];
		++count;
	}

	if (mTypeMask & MAP_WEIGHT)
	{
		buffers[count] = vb;
		strides[count] = sTypeSize[TYPE_WEIGHT];
		offsets[count] = mOffsets[TYPE_WEIGHT];
		++count;
	}

	if (mTypeMask & MAP_WEIGHT4)
	{
		// S24: rigged-mesh skinning (objectSkinV.hlsl's "weight4 :
		// BLENDWEIGHT" input) - see DXVertexLayout.cpp's kWeight4 comment.
		buffers[count] = vb;
		strides[count] = sTypeSize[TYPE_WEIGHT4];
		offsets[count] = mOffsets[TYPE_WEIGHT4];
		++count;
	}

	if (mTypeMask & MAP_CLOTHWEIGHT)
	{
		buffers[count] = vb;
		strides[count] = sTypeSize[TYPE_CLOTHWEIGHT];
		offsets[count] = mOffsets[TYPE_CLOTHWEIGHT];
		++count;
	}

	if (mTypeMask & MAP_TEXTURE_INDEX)
	{
		// Same underlying buffer as TYPE_VERTEX, different input slot - see
		// DXVertexLayout.h's header comment: this is packed into position's
		// otherwise-unused W component (mOffsets[TYPE_TEXTURE_INDEX] is
		// already TYPE_VERTEX's offset + 12, computed by the shared,
		// non-DX_RENDER-specific calcOffsets()), not a separate stream.
		buffers[count] = vb;
		strides[count] = sTypeSize[TYPE_VERTEX];
		offsets[count] = mOffsets[TYPE_TEXTURE_INDEX];
		++count;
	}

	ID3D11DeviceContext* ctx = gDXDevice.getContext();
	ctx->IASetVertexBuffers(0, count, buffers, strides, offsets);

	ID3D11InputLayout* layout = DXVertexLayout::getOrCreate(mTypeMask, vs_bytecode->GetBufferPointer(), vs_bytecode->GetBufferSize(),
		LLHLSLShader::sCurBoundShaderPtr->mName.c_str());
	ctx->IASetInputLayout(layout);

	return;
}

void LLVertexBuffer::setPositionData(const LLVector4a* data)
{
	flush_vbo(GL_ARRAY_BUFFER, 0, sizeof(LLVector4a) * getNumVerts() - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setTexCoord0Data(const LLVector2* data)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_TEXCOORD0], mOffsets[TYPE_TEXCOORD0] + sTypeSize[TYPE_TEXCOORD0] * getNumVerts() - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setTexCoord1Data(const LLVector2* data)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_TEXCOORD1], mOffsets[TYPE_TEXCOORD1] + sTypeSize[TYPE_TEXCOORD1] * getNumVerts() - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setColorData(const LLColor4U* data)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_COLOR], mOffsets[TYPE_COLOR] + sTypeSize[TYPE_COLOR] * getNumVerts() - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setNormalData(const LLVector4a* data)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_NORMAL], mOffsets[TYPE_NORMAL] + sTypeSize[TYPE_NORMAL] * getNumVerts() - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setTangentData(const LLVector4a* data)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_TANGENT], mOffsets[TYPE_TANGENT] + sTypeSize[TYPE_TANGENT] * getNumVerts() - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setWeight4Data(const LLVector4a* data)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_WEIGHT4], mOffsets[TYPE_WEIGHT4] + sTypeSize[TYPE_WEIGHT4] * getNumVerts() - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setJointData(const U64* data)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_JOINT], mOffsets[TYPE_JOINT] + sTypeSize[TYPE_JOINT] * getNumVerts() - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setIndexData(const U16* data)
{
	flush_vbo(GL_ELEMENT_ARRAY_BUFFER, 0, sizeof(U16) * getNumIndices() - 1, (U8*)data, mMappedIndexData);
}

void LLVertexBuffer::setIndexData(const U32* data)
{
	if (mIndicesType != GL_UNSIGNED_INT)
	{ // HACK -- vertex buffers are initialized as 16-bit indices, but can be switched to 32-bit indices
		mIndicesType = GL_UNSIGNED_INT;
		mIndicesStride = 4;
		mNumIndices /= 2;
	}
	flush_vbo(GL_ELEMENT_ARRAY_BUFFER, 0, sizeof(U32) * getNumIndices() - 1, (U8*)data, mMappedIndexData);
}

void LLVertexBuffer::setPositionData(const LLVector4a* data, U32 offset, U32 count)
{
	flush_vbo(GL_ARRAY_BUFFER, offset * sizeof(LLVector4a), (offset + count) * sizeof(LLVector4a) - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setNormalData(const LLVector4a* data, U32 offset, U32 count)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_NORMAL] + offset * sTypeSize[TYPE_NORMAL], mOffsets[TYPE_NORMAL] + (offset + count) * sTypeSize[TYPE_NORMAL] - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setTexCoord0Data(const LLVector2* data, U32 offset, U32 count)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_TEXCOORD0] + offset * sTypeSize[TYPE_TEXCOORD0], mOffsets[TYPE_TEXCOORD0] + (offset + count) * sTypeSize[TYPE_TEXCOORD0] - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setTexCoord1Data(const LLVector2* data, U32 offset, U32 count)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_TEXCOORD1] + offset * sTypeSize[TYPE_TEXCOORD1], mOffsets[TYPE_TEXCOORD1] + (offset + count) * sTypeSize[TYPE_TEXCOORD1] - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setColorData(const LLColor4U* data, U32 offset, U32 count)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_COLOR] + offset * sTypeSize[TYPE_COLOR], mOffsets[TYPE_COLOR] + (offset + count) * sTypeSize[TYPE_COLOR] - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setTangentData(const LLVector4a* data, U32 offset, U32 count)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_TANGENT] + offset * sTypeSize[TYPE_TANGENT], mOffsets[TYPE_TANGENT] + (offset + count) * sTypeSize[TYPE_TANGENT] - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setWeight4Data(const LLVector4a* data, U32 offset, U32 count)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_WEIGHT4] + offset * sTypeSize[TYPE_WEIGHT4], mOffsets[TYPE_WEIGHT4] + (offset + count) * sTypeSize[TYPE_WEIGHT4] - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setJointData(const U64* data, U32 offset, U32 count)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_JOINT] + offset * sTypeSize[TYPE_JOINT], mOffsets[TYPE_JOINT] + (offset + count) * sTypeSize[TYPE_JOINT] - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setIndexData(const U16* data, U32 offset, U32 count)
{
	flush_vbo(GL_ELEMENT_ARRAY_BUFFER, offset * sizeof(U16), (offset + count) * sizeof(U16) - 1, (U8*)data, mMappedIndexData);
}

void LLVertexBuffer::setIndexData(const U32* data, U32 offset, U32 count)
{
	if (mIndicesType != GL_UNSIGNED_INT)
	{ // HACK -- vertex buffers are initialized as 16-bit indices, but can be switched to 32-bit indices
		mIndicesType = GL_UNSIGNED_INT;
		mIndicesStride = 4;
		mNumIndices /= 2;
	}
	flush_vbo(GL_ELEMENT_ARRAY_BUFFER, offset * sizeof(U32), (offset + count) * sizeof(U32) - 1, (U8*)data, mMappedIndexData);
}