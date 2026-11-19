/**
 * @file kveffects.cpp
 * @brief KV Effects - GPU-accelerated post-processing effects using OpenCL
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

#include "llviewerprecompiledheaders.h"

#include "kveffects.h"
#include "kvopencl.h"
#include "llviewercontrol.h"
#include "llgl.h"
#include "llerror.h"

#include <omp.h>
#include <random>
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>

// Read/write bookend for the effect functions below - the OpenCL pipeline
// itself is backend-agnostic; only the texture read/write steps needed this.
#include "DXReadback.h"

// ============================================================
//  Global OpenCL Instance
// ============================================================
// External linkage: DXBC7Compressor (dxrender/) reaches this same instance
// (shared platform/device/context, its own queue/kernel via KVOpenCL's
// getComputeQueue()/getContext()) rather than opening a second OpenCL
// context. Declared `extern` in kvopencl.h, not kveffects.h.
KVOpenCL gCL;  // Single instance for all GPU operations

// ============================================================
//  GPU-Accelerated Effects (OpenCL with OpenGL Interop)
//  All effects operate directly on GPU textures via zero-copy buffer sharing
//  for maximum performance. Effects are executed in the render pipeline.
// ============================================================

// ------------------------------------------------------------
// Effect chain
// S24 : the enabled effects used to run one after another, each as its own full round trip (read the frame back to the
// CPU, upload it, run the kernel, download it, write it back to the texture), with fresh OpenCL buffers and a
// zero-filled CPU buffer per call. The chain now reads the frame once, ping-pongs the enabled kernels between two
// persistent OpenCL buffers, and writes the result back once. Every kernel still reads and writes RGBA8 buffers, so each
// stage sees exactly the bytes the previous one produced, as before. The order is unchanged:
// desaturate, invert, RGB control, cel shade, vignette, edge glow, night vision, motion blur.
// ------------------------------------------------------------
namespace
{
	cl_mem sChainBufA = nullptr;
	cl_mem sChainBufB = nullptr;
	size_t sChainBufSize = 0;
	std::unique_ptr<unsigned char[]> sChainCpu;
	size_t sChainCpuSize = 0;

	// motion blur's cross-frame history; it must survive between frames
	cl_mem sMotionHistory = nullptr;
	int sMotionWidth = 0;
	int sMotionHeight = 0;
}

void ImageProcessor::applyEffectsChainGPU(ID3D11Texture2D* tex, int width, int height, int effectsMask)
{
	if (effectsMask == 0 || width <= 0 || height <= 0)
	{
		return;
	}

	if (!gCL.init())
	{
		LL_WARNS() << "OpenCL initialization failed for the effect chain" << LL_ENDL;
		return;
	}

	const int pixelCount = width * height;
	const size_t bufSize = (size_t)pixelCount * 4; // RGBA

	if (sChainBufSize != bufSize)
	{
		gCL.release(sChainBufA);
		gCL.release(sChainBufB);
		sChainBufA = gCL.createBuffer(bufSize, CL_MEM_READ_WRITE);
		sChainBufB = gCL.createBuffer(bufSize, CL_MEM_READ_WRITE);
		sChainBufSize = (sChainBufA && sChainBufB) ? bufSize : 0;
	}
	if (sChainBufSize != bufSize)
	{
		return;
	}
	if (sChainCpuSize != bufSize)
	{
		// every byte is overwritten by the read below, so it is not zero-filled
		sChainCpu = std::make_unique_for_overwrite<unsigned char[]>(bufSize);
		sChainCpuSize = bufSize;
	}

	// motion blur's history buffer is created (black) before anything is changed, so a failure leaves the frame alone
	const bool motionBlur = (effectsMask & MOTION_BLUR) != 0;
	if (motionBlur)
	{
		if (sMotionHistory && (width != sMotionWidth || height != sMotionHeight))
		{
			gCL.release(sMotionHistory);
			sMotionHistory = nullptr;
		}
		if (!sMotionHistory)
		{
			sMotionHistory = gCL.createBuffer(bufSize, CL_MEM_READ_WRITE);
			if (!sMotionHistory)
			{
				LL_WARNS() << "Failed to create motion blur history buffer" << LL_ENDL;
				return;
			}
			std::vector<unsigned char> blackFrame(bufSize, 0);
			gCL.writeBuffer(sMotionHistory, blackFrame.data(), bufSize);
			sMotionWidth = width;
			sMotionHeight = height;
		}
	}

	unsigned char* cpu = sChainCpu.get();
	if (!DXReadback::readPixels(tex, 0, 0, width, height, 4, cpu))
	{
		return;
	}
	gCL.writeBuffer(sChainBufA, cpu, bufSize);

	// `in` always holds the latest result; each stage writes `out`, then they swap
	cl_mem in = sChainBufA;
	cl_mem out = sChainBufB;

	if (effectsMask & DESATURATION)
	{
		gCL.runGrayscale(in, out, width, height);
		std::swap(in, out);
	}
	if (effectsMask & INVERT)
	{
		gCL.runInvert(in, out, pixelCount);
		std::swap(in, out);
	}
	if (effectsMask & RGB_CONTROL)
	{
		const float redControl = gSavedSettings.getF32("RedControlKV");
		const float greenControl = gSavedSettings.getF32("GreenControlKV");
		const float blueControl = gSavedSettings.getF32("BlueControlKV");
		const float boostMultiplier = gSavedSettings.getF32("BoostMultiplierKV");
		const float hueRotation = gSavedSettings.getF32("HueRotationKV");
		const float saturationControl = gSavedSettings.getF32("SaturationControlKV");
		const float contrastMultiplier = gSavedSettings.getF32("ContrastMultiplierKV");
		const float gammaCorrection = gSavedSettings.getF32("GammaCorrectionKV");
		gCL.runRGBControl(in, out, pixelCount,
			redControl, greenControl, blueControl,
			boostMultiplier, hueRotation, saturationControl,
			contrastMultiplier, gammaCorrection);
		std::swap(in, out);
	}
	if (effectsMask & CEL_SHADING)
	{
		int safeInterval = std::clamp(gSavedSettings.getU32("CelshadeInterval"), 32U, 255U);
		int safeShades = std::clamp(gSavedSettings.getU32("CelnumShades"), 1U, 128U);
		int shadeInterval = std::max(1, safeInterval / std::max(1, safeShades));
		float lowVarianceThreshold = 15.0f;
		gCL.runCelShade(in, out, width, height, shadeInterval, lowVarianceThreshold);
		std::swap(in, out);
	}
	if (effectsMask & VIGNETTE)
	{
		float intensity = gSavedSettings.getF32("VignetteIntensity");
		gCL.runVignette(in, out, width, height, intensity);
		std::swap(in, out);
	}
	if (effectsMask & EDGE_GLOW)
	{
		static LLCachedControl<F32> edgeSensitivity(gSavedSettings, "edgeSensitivity");
		static LLCachedControl<F32> glowIntensity(gSavedSettings, "edgeGlowIntensity");
		static LLCachedControl<F32> blurRadiusSetting(gSavedSettings, "edgeBlurRadius");
		int blurRadius = std::max(0, (int)blurRadiusSetting); // Convert to int and clamp
		gCL.runEdgeGlow(in, out, width, height, edgeSensitivity, glowIntensity, blurRadius);
		std::swap(in, out);
	}
	if (effectsMask & NIGHT_VISION)
	{
		static LLCachedControl<F32> greenIntensity(gSavedSettings, "greenIntensity");
		static LLCachedControl<F32> brightnessBoost(gSavedSettings, "brightnessBoost");
		static LLCachedControl<U32> noiseScaleSetting(gSavedSettings, "noiseScale");
		float dimFactor = 0.2f;
		int noiseScale = std::max(1, (int)noiseScaleSetting); // Get noise scale from settings

		static std::random_device rd;
		static std::mt19937 rng(rd());
		unsigned int seed = rng();

		gCL.runNightVision(in, out, width, height, pixelCount, greenIntensity, brightnessBoost, dimFactor, seed, noiseScale);
		std::swap(in, out);
	}
	if (motionBlur)
	{
		static LLCachedControl<F32> blendFactor(gSavedSettings, "MotionBlurBlend");
		gCL.runMotionBlur(in, out, width, height, blendFactor, sMotionHistory);
		std::swap(in, out);
	}

	gCL.readBuffer(in, cpu, bufSize);

	if (motionBlur)
	{
		// the blurred frame becomes next frame's history (motion blur is the last stage)
		gCL.writeBuffer(sMotionHistory, cpu, bufSize);
	}

	DXReadback::writePixels(tex, 0, 0, width, height, 4, cpu);
}
