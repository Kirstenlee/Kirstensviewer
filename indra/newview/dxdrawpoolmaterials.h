/**
 * @file dxdrawpoolmaterials.h
 * @brief LLDrawPoolMaterials class definition plus its DX11-native
 * renderDeferred() implementation - lldrawpoolmaterials.h/.cpp retired
 * entirely, 2026-10-13.
 *
 * Copyright (c) 2025 Kirstenlee Cinquetti (Lee Quick)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include "v4coloru.h"
#include "v2math.h"
#include "v3math.h"
#include "llvertexbuffer.h"
#include "dxdrawpool.h"

class LLViewerTexture;
class LLDrawInfo;
class LLHLSLShader;

class LLDrawPoolMaterials : public LLRenderPass
{
	LLHLSLShader *mShader;
public:
	LLDrawPoolMaterials();

	enum
	{
		VERTEX_DATA_MASK =	LLVertexBuffer::MAP_VERTEX |
		LLVertexBuffer::MAP_NORMAL |
		LLVertexBuffer::MAP_TEXCOORD0 |
		LLVertexBuffer::MAP_TEXCOORD1 |
		LLVertexBuffer::MAP_TEXCOORD2 |
		LLVertexBuffer::MAP_COLOR |
		LLVertexBuffer::MAP_TANGENT
	};

	U32 getVertexDataMask() override { return VERTEX_DATA_MASK; }

	void render(S32 pass = 0) override { }
	S32	 getNumPasses() override {return 0;}
	void prerender() override;

	S32 getNumDeferredPasses() override;
	void beginDeferredPass(S32 pass) override;
	void endDeferredPass(S32 pass) override;
	void renderDeferred(S32 pass) override;
};

// Only LLDrawPoolMaterials::renderDeferred() needs real replacement logic
// below - beginDeferredPass()/endDeferredPass() (above) are left completely
// untouched because they're already DX-safe by composition:
// LLPipeline::bindDeferredShader() (which
// they call) is entirely gated behind LLHLSLShader::enableTexture()'s
// `channel > -1` checks and the already-DX-safe uniform setters, so it
// degrades to a real (if inert) shader bind under DX_RENDER with no changes
// needed. Phase 5.2's other 2 real pools in this group
// (LLDrawPoolWaterExclusion, LLDrawPoolTree) turned out to need zero code
// changes at all for the same reason - see the stage 5 hitlist memory.
//
// Normal/specular maps and the per-material shading uniforms (environment
// intensity, emissive brightness, alpha cutoff, specular color) are bound via
// LLHLSLShader::sCurBoundShaderPtr (mShader is private to LLDrawPoolMaterials) -
// see dxdrawpoolmaterials.cpp's renderDeferred() for the GL-body deviations and
// the rigged (pass>=12) matrix-palette upload.
class DXDrawPoolMaterials
{
public:
    static void renderDeferred(LLDrawPoolMaterials& pool, S32 pass);
};
