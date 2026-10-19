/**
 * @file dxdrawpoolsimple.h
 * @brief The 6 "simple" draw pool classes (ordinary diffuse/fullbright/
 * alpha-mask/grass/glow geometry) and their DX11-native render
 * implementations. Previously split across lldrawpoolsimple.h (class
 * declarations + thin per-pool redirects) and this file (the real render
 * bodies) - merged here 2026-10-13, lldrawpoolsimple.h/.cpp retired.
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

#include "dxdrawpool.h"

class LLDrawPoolSimple final : public LLRenderPass
{
public:
	enum
	{
		VERTEX_DATA_MASK =	LLVertexBuffer::MAP_VERTEX |
							LLVertexBuffer::MAP_NORMAL |
							LLVertexBuffer::MAP_TEXCOORD0 |
							LLVertexBuffer::MAP_COLOR
	};
	U32 getVertexDataMask() override { return VERTEX_DATA_MASK; }

	LLDrawPoolSimple();

    S32 getNumDeferredPasses() override;
	void renderDeferred(S32 pass) override;
};

class LLDrawPoolGrass final : public LLRenderPass
{
public:
	enum
	{
		VERTEX_DATA_MASK =	LLVertexBuffer::MAP_VERTEX |
							LLVertexBuffer::MAP_NORMAL |
							LLVertexBuffer::MAP_TEXCOORD0 |
							LLVertexBuffer::MAP_COLOR
	};
	U32 getVertexDataMask() override { return VERTEX_DATA_MASK; }

	LLDrawPoolGrass();

	S32 getNumDeferredPasses() override { return 1; }
	void renderDeferred(S32 pass) override;
};

class LLDrawPoolAlphaMask final : public LLRenderPass
{
public:
	enum
	{
		VERTEX_DATA_MASK =	LLVertexBuffer::MAP_VERTEX |
							LLVertexBuffer::MAP_NORMAL |
							LLVertexBuffer::MAP_TEXCOORD0 |
							LLVertexBuffer::MAP_COLOR
	};
	U32 getVertexDataMask() override { return VERTEX_DATA_MASK; }

	LLDrawPoolAlphaMask();

    S32 getNumDeferredPasses() override { return 1; }
	void renderDeferred(S32 pass) override;
};

class LLDrawPoolFullbrightAlphaMask final : public LLRenderPass
{
public:
	enum
	{
		VERTEX_DATA_MASK =	LLVertexBuffer::MAP_VERTEX |
							LLVertexBuffer::MAP_TEXCOORD0 |
							LLVertexBuffer::MAP_COLOR
	};
	U32 getVertexDataMask() override { return VERTEX_DATA_MASK; }

	LLDrawPoolFullbrightAlphaMask();

	S32 getNumPostDeferredPasses() override { return 1; }
	void renderPostDeferred(S32 pass) override;
};


class LLDrawPoolFullbright final : public LLRenderPass
{
public:
	enum
	{
		VERTEX_DATA_MASK =	LLVertexBuffer::MAP_VERTEX |
							LLVertexBuffer::MAP_TEXCOORD0 |
							LLVertexBuffer::MAP_COLOR
	};
	U32 getVertexDataMask() override { return VERTEX_DATA_MASK; }

	LLDrawPoolFullbright();

	S32 getNumPostDeferredPasses() override { return 1; }
	void renderPostDeferred(S32 pass) override;
};

class LLDrawPoolGlow final : public LLRenderPass
{
public:
	LLDrawPoolGlow(): LLRenderPass(LLDrawPool::POOL_GLOW) { }

	enum
	{
		VERTEX_DATA_MASK =	LLVertexBuffer::MAP_VERTEX |
							LLVertexBuffer::MAP_TEXCOORD0 |
							LLVertexBuffer::MAP_EMISSIVE
	};

	U32 getVertexDataMask() override { return VERTEX_DATA_MASK; }

	S32 getNumPostDeferredPasses() override { return 1; }
	void renderPostDeferred(S32 pass) override;
};

// One consolidated file for all 6 pools per the "small pools may share one
// dx file rather than one-file-per-pool" principle (stage 5 hitlist memory,
// Phase 5.1). Each pool class's override method above is a thin redirect to
// its DXDrawPoolSimple:: static counterpart below. Every pool renders both
// its static and rigged pass except Grass (not skinned geometry).
//
// One standing gap, unrelated to skinning:
// - LL::GLTFSceneManager calls (LLDrawPoolFullbrightAlphaMask only) are
//   skipped: a separate, unconverted rendering subsystem, explicitly
//   deferred (see stage 5 hitlist memory, Phase 5.1 decision).
class DXDrawPoolSimple
{
public:
    static void renderSimpleDeferred(LLDrawPoolSimple& pool, S32 pass);
    static void renderGrassDeferred(LLDrawPoolGrass& pool, S32 pass);
    static void renderAlphaMaskDeferred(LLDrawPoolAlphaMask& pool, S32 pass);
    static void renderFullbrightAlphaMaskPostDeferred(LLDrawPoolFullbrightAlphaMask& pool, S32 pass);
    static void renderFullbrightPostDeferred(LLDrawPoolFullbright& pool, S32 pass);
    static void renderGlowPostDeferred(LLDrawPoolGlow& pool, S32 pass);
};
