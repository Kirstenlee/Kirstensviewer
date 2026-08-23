/**
 * @file llcubemaparray.cpp
 * @brief LLCubeMap class implementation
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Linden Research, Inc.
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

#include "llworkerthread.h"

#include "llcubemaparray.h"

#include "v4coloru.h"
#include "v3math.h"
#include "v3dmath.h"
#include "m3math.h"
#include "m4math.h"

#include "llrender.h"
#include "llglslshader.h"

#include "llglheaders.h"

 //#pragma optimize("", off)

using namespace LLImageGLMemory;

// MUST match order of OpenGL face-layers
GLenum LLCubeMapArray::sTargets[6] =
{
	GL_TEXTURE_CUBE_MAP_POSITIVE_X,
	GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
	GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
	GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
	GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
	GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
};

LLVector3 LLCubeMapArray::sLookVecs[6] =
{
		LLVector3(1, 0, 0),
		LLVector3(-1, 0, 0),
		LLVector3(0, 1, 0),
		LLVector3(0, -1, 0),
		LLVector3(0, 0, 1),
		LLVector3(0, 0, -1)
};

LLVector3 LLCubeMapArray::sUpVecs[6] =
{
	LLVector3(0, -1, 0),
	LLVector3(0, -1, 0),
	LLVector3(0, 0, 1),
	LLVector3(0, 0, -1),
	LLVector3(0, -1, 0),
	LLVector3(0, -1, 0)
};

// S24 (2026-08-13, task #194 follow-up): tried negating [2]/[3] here too
// (a further 180-degree in-plane rotation on top of the
// sClipToCubeUpVecs[2]/[3] fix below) - reverted. User-confirmed this
// made things wrong in a DIFFERENT way, not fixed - and proven by direct
// derivation afterward: for this table's (at, up_input) parameterization
// (LLCoordFrame::lookDir()'s cross-product construction), the row-
// orientation is fully determined once the center direction is fixed -
// there is no remaining independent sign-flip in this table that can fix
// row-orientation without re-breaking center-direction, or vice versa,
// for [2]/[3] specifically.
//
// S24 (2026-08-13, task #194 round 8 follow-up): [0]/[1] (+X/-X) negated
// here instead - a face-ID color test (solid, distinct color per face,
// no content) confirmed +X/-X were the only 2 faces still wrong after
// the earlier radianceGenV.hlsl/irradianceGenV.hlsl y-negation revert (4
// of 6 faces confirmed correct by real content - WOW WEST.png/WOW EAST.png).
// [0]/[1]'s center direction (-sClipToCubeUpVecs[0/1]) was already
// confirmed correct (matches sLookVecs[0]/[1]) and left untouched here -
// only the "at" input changes, which affects row/column orientation via
// LLCoordFrame::lookDir()'s left = up_input x at cross product without
// touching center direction at all (unlike touching up_input, which
// would move both simultaneously - see the [2]/[3] note above for why
// that path is a dead end).
// S24 (2026-08-15, task #194): a table-mismatch theory (this resample
// table's per-slot look/up vectors didn't match llviewerwindow.cpp::
// cubeSnapshot()'s look_dirs/look_upvecs, the table that actually
// determines what got captured into each array slot) looked well-reasoned
// - self-consistent, matched DXCubeMapFaces.h's own stated GL/DX
// look-vector-identical/up-vector-negated derivation - but a real before/
// after comparison (start.png vs MESS.png, sphere reflection) showed it
// made sphere quality VISIBLY WORSE (more blocky/faceted, not less) than
// this table's existing hand-tuned (rounds 8-15) values. Reverted in full.
// Whatever this resample stage's frame.lookAt()->vary_dir construction
// actually needs, it is NOT simply "the same look/up pairs raw capture
// used" - that assumption was concretely disproven by user testing, not
// just unconfirmed. Do not re-attempt this exact fix without new evidence
// explaining why the mismatch that looked like a bug is apparently
// required.
LLVector3 LLCubeMapArray::sClipToCubeLookVecs[6] =
{
		LLVector3(0, 0, 1),
		LLVector3(0, 0, -1),

		LLVector3(1, 0, 0), // GOOD
		LLVector3(1, 0, 0), // GOOD

		LLVector3(1, 0, 0),
		LLVector3(-1, 0, 0),
};

// S24 (2026-08-13, task #194): [2]/[3] (the +Y/-Y destination faces) had
// their sign backwards relative to sClipToCubeLookVecs[2]/[3] - verified
// by direct vector math: with the old signs, the "center" direction this
// pair's lookAt/getOpenGLRotation basis produces (the direction
// radianceGenV.hlsl/irradianceGenV.hlsl's vary_dir resolves to at screen
// center) came out as the exact OPPOSITE of LLCubeMapArray::sLookVecs[2]/[3]
// - i.e. whatever got written into the nominal "+Y face" slot was actually
// sampling -Y content, and vice versa for "-Y". Every other entry (0,1,4,5)
// already produced the correct matching center direction - only these two
// were wrong, despite being marked "GOOD" (that comment predates this
// fix and was not re-verified here). This table is shared with GL (no
// #ifdef DX_RENDER), so this was a real, pre-existing latent bug on both
// backends, not something introduced by the DX_RENDER port.
LLVector3 LLCubeMapArray::sClipToCubeUpVecs[6] =
{
	LLVector3(-1, 0, 0), //GOOD
	LLVector3(1, 0, 0), //GOOD

	LLVector3(0, -1, 0),
	LLVector3(0, 1, 0),

	LLVector3(0, 0, -1),
	LLVector3(0, 0, 1)
};

LLCubeMapArray::LLCubeMapArray()
	: mTextureStage(0)
{}

LLCubeMapArray::LLCubeMapArray(LLCubeMapArray& lhs, U32 width, U32 count) : mTextureStage(0)
{
	mWidth = width;
	mCount = count;

	// Allocate a new cubemap array with the same criteria as the incoming cubemap array
	allocate(mWidth, lhs.mImage->getComponents(), count, lhs.mImage->getUseMipMaps(), lhs.mHDR);

#ifndef DX_RENDER
	// S24 (DX_RENDER, phase 5.11): see allocate()'s comment - no real texture
	// exists on either side of this copy under DX_RENDER, nothing to copy.
	// Copy each cubemap from the incoming array to the new array
	U32 min_count = std::min(count, lhs.mCount);
	for (U32 i = 0; i < min_count * 6; ++i)
	{
		U32 src_resolution = lhs.mWidth;
		U32 dst_resolution = mWidth;
		{
			GLint components = GL_RGB;
			if (mImage->getComponents() == 4)
				components = GL_RGBA;
			GLint format = GL_RGB;

			// Handle different resolutions by scaling the image
			LLPointer<LLImageRaw> src_image = new LLImageRaw(lhs.mWidth, lhs.mWidth, lhs.mImage->getComponents());
			glGetTexImage(GL_TEXTURE_CUBE_MAP_ARRAY, 0, components, GL_UNSIGNED_BYTE, src_image->getData());

			LLPointer<LLImageRaw> scaled_image = src_image->scaled(mWidth, mWidth);
			glTexSubImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, 0, 0, 0, i, mWidth, mWidth, 1, components, GL_UNSIGNED_BYTE, scaled_image->getData());
		}
	}
#endif
}

LLCubeMapArray::~LLCubeMapArray()
{}

void LLCubeMapArray::allocate(U32 resolution, U32 components, U32 count, bool use_mips, bool hdr)
{
	U32 texname = 0;
	mWidth = resolution;
	mCount = count;

	mHDR = hdr;

	// S24 (2026-08-09, task #147 step 3): real DX11-native implementation -
	// previously this whole function skipped the raw GL allocation
	// entirely and did nothing else under DX_RENDER (the "no capture
	// pipeline exists yet, nothing to render into this texture regardless"
	// reasoning that used to live in this comment). That's no longer true:
	// LLReflectionMapManager's capture path is now real (see
	// llreflectionmapmanager.cpp), so this needs a genuine backing
	// resource. mImage is still constructed (texname 0, the existing "no
	// texture" sentinel, unused under DX_RENDER) so callers reading
	// mImage's dimensions keep working unchanged.
	mImage = new LLImageGL(resolution, resolution, components, use_mips);
#ifdef DX_RENDER
	if (!mDXTexture.create((int)resolution, (int)resolution, (int)count, hdr, use_mips))
	{
		LL_WARNS("Texture") << "LLCubeMapArray::allocate: DXCubeArrayTexture::create failed (resolution=" << resolution << " count=" << count << ")" << LL_ENDL;
	}
#else
	LLImageGL::generateTextures(1, &texname);
	mImage->setTexName(texname);
	mImage->setTarget(sTargets[0], LLTexUnit::TT_CUBE_MAP_ARRAY);

	mImage->setUseMipMaps(use_mips);
	mImage->setHasMipMaps(use_mips);

	bind(0);
	free_cur_tex_image();

	U32 format = components == 4 ? GL_RGBA16F : GL_R11F_G11F_B10F;
	if (!hdr)
	{
		format = components == 4 ? GL_RGBA8 : GL_RGB8;
	}
	U32 mip = 0;
	U32 mip_resolution = resolution;
	while (mip_resolution >= 1)
	{
		glTexImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, mip, format, mip_resolution, mip_resolution, count * 6, 0,
			GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

		if (!use_mips)
		{
			break;
		}
		mip_resolution /= 2;
		++mip;
	}

	alloc_tex_image(resolution, resolution, format, count * 6);

	mImage->setAddressMode(LLTexUnit::TAM_CLAMP);

	if (use_mips)
	{
		mImage->setFilteringOption(LLTexUnit::TFO_ANISOTROPIC);
		//glGenerateMipmap(GL_TEXTURE_CUBE_MAP_ARRAY);  // <=== latest AMD drivers do not appreciate this method of allocating mipmaps
	}   //S24 The regression that caused crashes in earlier beta drivers (circa 2022) was resolved in version 22.11.2.
	else
	{
		mImage->setFilteringOption(LLTexUnit::TFO_BILINEAR);
	}

	unbind();
#endif
}

void LLCubeMapArray::bind(S32 stage)
{
	mTextureStage = stage;
#ifdef DX_RENDER
	// S24 (2026-08-09, task #147 step 3): was bindManual() unconditionally,
	// a hardcoded no-op under DX_RENDER (handed a raw GLuint with nothing
	// to translate) - routes through the new LLTexUnit::bind(LLCubeMapArray*)
	// overload instead, which pulls the real D3D11 SRV directly.
	gGL.getTexUnit(stage)->bind(this);
#else
	gGL.getTexUnit(stage)->bindManual(LLTexUnit::TT_CUBE_MAP_ARRAY, getGLName(), mImage->getUseMipMaps());
#endif
}

void LLCubeMapArray::unbind()
{
	// S24 (2026-08-09, task #147 step 3): unbind(TT_CUBE_MAP_ARRAY) is
	// already a safe no-op under DX_RENDER (LLTexUnit::unbind() only acts
	// on type==TT_TEXTURE, confirmed by reading it - everything else
	// early-returns) - no DX_RENDER branch needed here.
	gGL.getTexUnit(mTextureStage)->unbind(LLTexUnit::TT_CUBE_MAP_ARRAY);
	mTextureStage = -1;
}

GLuint LLCubeMapArray::getGLName()
{
	return mImage->getTexName();
}

void LLCubeMapArray::destroyGL()
{
	mImage = NULL;
#ifdef DX_RENDER
	mDXTexture.destroy();
#endif
}