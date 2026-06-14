/**
 * @file llcubemap.cpp
 * @brief LLCubeMap class implementation
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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

#include "llworkerthread.h"

#include "llcubemap.h"

#include "v4coloru.h"
#include "v3math.h"
#include "v3dmath.h"
#include "m3math.h"
#include "m4math.h"

#include "llrender.h"
#include "llglslshader.h"

#include "llglheaders.h"

namespace {
	const U16 RESOLUTION = 64;
}

bool LLCubeMap::sUseCubeMaps = true;

LLCubeMap::LLCubeMap(bool init_as_srgb)
	: mTextureStage(0),
	mMatrixStage(0),
	mIssRGB(init_as_srgb)
{
	mTargets[0] = GL_TEXTURE_CUBE_MAP_NEGATIVE_X;
	mTargets[1] = GL_TEXTURE_CUBE_MAP_POSITIVE_X;
	mTargets[2] = GL_TEXTURE_CUBE_MAP_NEGATIVE_Y;
	mTargets[3] = GL_TEXTURE_CUBE_MAP_POSITIVE_Y;
	mTargets[4] = GL_TEXTURE_CUBE_MAP_NEGATIVE_Z;
	mTargets[5] = GL_TEXTURE_CUBE_MAP_POSITIVE_Z;
}

LLCubeMap::~LLCubeMap()
{}

// S24 - V1
void LLCubeMap::initGL()
{
	llassert(gGLManager.mInited);

	if (!LLCubeMap::sUseCubeMaps)
	{
		return; // silently ignore if cube maps are disabled
	}

	// Already initialised?
	if (!mImages[0].isNull())
	{
		disable();
		return;
	}

	U32 texname = 0;
	LLImageGL::generateTextures(1, &texname);

	for (int face = 0; face < 6; ++face)
	{
		// Allocate GL image for this face
		mImages[face] = new LLImageGL(RESOLUTION, RESOLUTION, 4, false);

		// Set cube map face target
		mImages[face]->setTarget(mTargets[face], LLTexUnit::TT_CUBE_MAP);

		// Allocate raw storage for upload
		mRawImages[face] = new LLImageRaw(RESOLUTION, RESOLUTION, 4);

		// Upload empty face texture
		if (!mImages[face]->createGLTexture(0, mRawImages[face], texname))
		{
			// Fail silently — no spammy warnings
			continue;
		}

		// Bind once per face to set sampler state
		gGL.getTexUnit(0)->bindManual(LLTexUnit::TT_CUBE_MAP, texname);
		mImages[face]->setAddressMode(LLTexUnit::TAM_CLAMP);
	}

	// Leave texture unit in a clean state
	gGL.getTexUnit(0)->disable();

	disable();
}

// Yes, I know that this is inefficient! - djs 08/08/02
// S24 hi djs :) - KL made mre efficient by doing all the work in one pass instead of 6 separate passes, and by using memcpy to copy pixels instead of per-channel copying.
void LLCubeMap::initRawData(const std::vector<LLPointer<LLImageRaw>>& rawimages)
{
	static constexpr bool FLIP_X[6] = { false, true,  false, false, true,  false };
	static constexpr bool FLIP_Y[6] = { true,  true,  true,  false, true,  true };
	static constexpr bool TRANSPOSE[6] = { false, false, false, false, true,  true };

	for (int face = 0; face < 6; face++)
	{
		LLImageDataSharedLock lockIn(rawimages[face]);
		LLImageDataLock lockOut(mRawImages[face]);

		const U8* src = rawimages[face]->getData();
		U8* dst = mRawImages[face]->getData();

		const S32 width = rawimages[face]->getWidth();
		const S32 height = rawimages[face]->getHeight();
		const S32 bpp = rawimages[face]->getComponents(); // should be 4

		// Safety: ensure we only handle RGBA 4‑byte pixels
		llassert(bpp == 4);

		for (S32 y = 0; y < height; ++y)
		{
			for (S32 x = 0; x < width; ++x)
			{
				S32 sx = x;
				S32 sy = y;

				if (FLIP_Y[face]) sy = height - 1 - sy;
				if (FLIP_X[face]) sx = width - 1 - sx;

				if (TRANSPOSE[face])
					std::swap(sx, sy);

				const S32 src_index = (sy * width + sx) * bpp;
				const S32 dst_index = (y * width + x) * bpp;

				// Copy RGBA pixel
				memcpy(dst + dst_index, src + src_index, 4);
			}
		}
	}
}

// S24 - V1
void LLCubeMap::initGLData()
{
	constexpr int FACE_COUNT = 6;

	for (int face = 0; face < FACE_COUNT; ++face)
	{
		mImages[face]->setSubImage(
			mRawImages[face],
			0, 0,
			RESOLUTION,
			RESOLUTION
		);
	}
}

void LLCubeMap::init(const std::vector<LLPointer<LLImageRaw> >& rawimages)
{
	if (!gGLManager.mIsDisabled)
	{
		initGL();
		initRawData(rawimages);
		initGLData();
	}
}

void LLCubeMap::initReflectionMap(U32 resolution, U32 components)
{
	U32 texname = 0;

	LLImageGL::generateTextures(1, &texname);

	mImages[0] = new LLImageGL(resolution, resolution, components, true);
	mImages[0]->setTexName(texname);
	mImages[0]->setTarget(mTargets[0], LLTexUnit::TT_CUBE_MAP);
	gGL.getTexUnit(0)->bindManual(LLTexUnit::TT_CUBE_MAP, texname);
	mImages[0]->setAddressMode(LLTexUnit::TAM_CLAMP);
}

// S24 - V1
void LLCubeMap::initEnvironmentMap(const std::vector<LLPointer<LLImageRaw>>& rawimages)
{
	// Must have exactly 6 faces
	if (rawimages.size() != 6)
		return;

	// All faces must match resolution + components
	const U32 resolution = rawimages[0]->getWidth();
	const U32 components = rawimages[0]->getComponents();

	for (int face = 1; face < 6; ++face)
	{
		if (rawimages[face]->getWidth() != resolution ||
			rawimages[face]->getHeight() != resolution ||
			rawimages[face]->getComponents() != components)
		{
			return; // silently abort on mismatch
		}
	}

	// Create a single cube map texture
	U32 texname = 0;
	LLImageGL::generateTextures(1, &texname);

	// Upload each face
	for (int face = 0; face < 6; ++face)
	{
		mImages[face] = new LLImageGL(resolution, resolution, components, true);
		mImages[face]->setTarget(mTargets[face], LLTexUnit::TT_CUBE_MAP);

		// Store raw image pointer
		mRawImages[face] = rawimages[face];

		// Upload face data
		mImages[face]->createGLTexture(0, mRawImages[face], texname);

		// Set sampler state once per face
		gGL.getTexUnit(0)->bindManual(LLTexUnit::TT_CUBE_MAP, texname);
		mImages[face]->setAddressMode(LLTexUnit::TAM_CLAMP);

		// Upload full face
		mImages[face]->setSubImage(mRawImages[face], 0, 0, resolution, resolution);
	}

	// Final cube map setup
	enableTexture(0);
	bind();

	mImages[0]->setFilteringOption(LLTexUnit::TFO_ANISOTROPIC);

	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
	glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

	gGL.getTexUnit(0)->disable();
	disable();
}

void LLCubeMap::generateMipMaps()
{
	mImages[0]->setUseMipMaps(true);
	mImages[0]->setHasMipMaps(true);
	enableTexture(0);
	bind();
	mImages[0]->setFilteringOption(LLTexUnit::TFO_BILINEAR);
	{
		glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
	}
	gGL.getTexUnit(0)->disable();
	disable();
}

GLuint LLCubeMap::getGLName()
{
	return mImages[0]->getTexName();
}

void LLCubeMap::bind()
{
	gGL.getTexUnit(mTextureStage)->bind(this);
}

void LLCubeMap::enable(S32 stage)
{
	enableTexture(stage);
}

void LLCubeMap::enableTexture(S32 stage)
{
	mTextureStage = stage;
	if (stage >= 0 && LLCubeMap::sUseCubeMaps)
	{
		gGL.getTexUnit(stage)->enable(LLTexUnit::TT_CUBE_MAP);
	}
}

void LLCubeMap::disable(void)
{
	disableTexture();
}

void LLCubeMap::disableTexture(void)
{
	if (mTextureStage >= 0 && LLCubeMap::sUseCubeMaps)
	{
		gGL.getTexUnit(mTextureStage)->disable();
		if (mTextureStage == 0)
		{
			gGL.getTexUnit(0)->enable(LLTexUnit::TT_TEXTURE);
		}
	}
}

// S24 - V1
void LLCubeMap::setMatrix(S32 stage)
{
	mMatrixStage = stage;
	if (stage < 0)
		return;

	// Activate cube map texture unit
	gGL.getTexUnit(stage)->activate();

	// Extract rotation from modelview matrix (column-major)
	const F32* mv = gGLModelView;

	LLVector3 x(mv[0], mv[1], mv[2]);
	LLVector3 y(mv[4], mv[5], mv[6]);
	LLVector3 z(mv[8], mv[9], mv[10]);

	LLMatrix3 rot;
	rot.setRows(x, y, z);

	// Convert to 4×4 and transpose for texture matrix usage
	LLMatrix4 texmat(rot);
	texmat.transpose();

	// Load into texture matrix stack
	gGL.matrixMode(LLRender::MM_TEXTURE);
	gGL.pushMatrix();
	gGL.loadMatrix((F32*)texmat.mMatrix);

	// Restore modelview
	gGL.matrixMode(LLRender::MM_MODELVIEW);
}

// s24 - V1
void LLCubeMap::restoreMatrix()
{
	if (mMatrixStage < 0)
		return;

	// Reactivate the cube map texture unit used in setMatrix()
	gGL.getTexUnit(mMatrixStage)->activate();

	// Restore the texture matrix stack
	gGL.matrixMode(LLRender::MM_TEXTURE);
	gGL.popMatrix();

	// Return to modelview mode
	gGL.matrixMode(LLRender::MM_MODELVIEW);
}

// S24 - V1
void LLCubeMap::destroyGL()
{
	for (int i = 0; i < 6; ++i)
	{
		if (mImages[i])
		{
			mImages[i]->destroyGLTexture();
			mImages[i] = nullptr;
		}

		mRawImages[i] = nullptr;
	}

	mMatrixStage = -1;
}