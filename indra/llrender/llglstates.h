/** 
 * @file llglstates.h
 * @brief LLGL states definitions
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

//THIS HEADER SHOULD ONLY BE INCLUDED FROM llgl.h
#ifndef LL_LLGLSTATES_H
#define LL_LLGLSTATES_H

#include "llimagedx.h"

//----------------------------------------------------------------------------

class DXDepthTest
{
	// Enabled by default
public:
	// S24: depth_func never reaches a real GL call under DX_RENDER -
	// glDepthFuncToDX() translates it to a D3D11 comparison enum instead.
	DXDepthTest(bool depth_enabled, bool write_enabled = true, DXenum depth_func = GL_LEQUAL);

	~DXDepthTest();

	void checkState();

	bool mPrevDepthEnabled;
	DXenum mPrevDepthFunc;
	bool mPrevWriteEnabled;
private:
	static bool sDepthEnabled; // defaults to false
	static DXenum sDepthFunc; // defaults to GL_LESS
	static bool sWriteEnabled; // defaults to true
};

//----------------------------------------------------------------------------

class LLGLSDefault
{
protected:
    LLGLDisable mBlend, mCullFace;
public:
	LLGLSDefault()
		:
		// Disable
		mBlend(GL_BLEND), 
		mCullFace(GL_CULL_FACE)
	{ }
};

class LLGLSObjectSelect
{ 
protected:
	LLGLDisable mBlend;
	LLGLEnable mCullFace;
public:
	LLGLSObjectSelect()
		: mBlend(GL_BLEND), 
		  mCullFace(GL_CULL_FACE)
	{ }
};

//----------------------------------------------------------------------------

class DXUIDefault
{
protected:
	// S24: DXState directly, not the deprecated LLGLEnable/LLGLDisable wrappers -
	// both are just DXState(state, ENABLED_STATE/DISABLED_STATE) under the hood.
	DXState mBlend;
	DXState mCullFace;
	DXDepthTest mDepthTest;
public:
	DXUIDefault()
		: mBlend(GL_BLEND, DXState::ENABLED_STATE),
		  mCullFace(GL_CULL_FACE, DXState::DISABLED_STATE),
		  mDepthTest(false, true, GL_LEQUAL)
	{}
};

//----------------------------------------------------------------------------

class LLGLSPipeline
{ 
protected:
	LLGLEnable mCullFace;
	DXDepthTest mDepthTest;
public:
	LLGLSPipeline()
		: mCullFace(GL_CULL_FACE),
		  mDepthTest(true, true, GL_LEQUAL)
	{ }		
};

class LLGLSPipelineAlpha // : public LLGLSPipeline
{ 
protected:
	LLGLEnable mBlend;
public:
	LLGLSPipelineAlpha()
		: mBlend(GL_BLEND)
	{ }
};

class LLGLSPipelineSelection
{ 
protected:
	LLGLDisable mCullFace;
public:
	LLGLSPipelineSelection()
		: mCullFace(GL_CULL_FACE)
	{}
};

class LLGLSPipelineSkyBox
{ 
protected:
    LLGLDisable mCullFace;
    LLGLSquashToFarClip mSquashClip;
public:
	LLGLSPipelineSkyBox();
   ~LLGLSPipelineSkyBox();
};

class LLGLSPipelineDepthTestSkyBox : public LLGLSPipelineSkyBox
{ 
public:
	LLGLSPipelineDepthTestSkyBox(bool depth_test, bool depth_write);

    DXDepthTest mDepth;
};

class LLGLSPipelineBlendSkyBox : public LLGLSPipelineDepthTestSkyBox 
{ 
public:
	LLGLSPipelineBlendSkyBox(bool depth_test, bool depth_write);
    LLGLEnable mBlend;    
};

class LLGLSTracker
{
protected:
	LLGLEnable mCullFace, mBlend;
public:
	LLGLSTracker() :
		mCullFace(GL_CULL_FACE),
		mBlend(GL_BLEND)
	{ }
};

//----------------------------------------------------------------------------

class LLGLSSpecular
{
public:
	F32 mShininess;
	LLGLSSpecular(const LLColor4& color, F32 shininess)
	{
		mShininess = shininess;
	}
	~LLGLSSpecular()
	{
	}
};

//----------------------------------------------------------------------------

#endif
