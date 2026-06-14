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
#include <algorithm>
#include <cmath>

// ============================================================
//  Global OpenCL Instance
// ============================================================
static KVOpenCL gCL;  // Single instance for all GPU operations

// ============================================================
//  GPU-Accelerated Effects (OpenCL with OpenGL Interop)
//  All effects operate directly on GPU textures via zero-copy buffer sharing
//  for maximum performance. Effects are executed in the render pipeline.
// ============================================================

// ------------------------------------------------------------
// Desaturation (Grayscale Conversion)
// Converts color image to grayscale using luminance weights
// ------------------------------------------------------------
void ImageProcessor::desaturateImageGPU(GLuint texID, int width, int height)
{
	if (!gCL.init())
	{
		LL_WARNS() << "OpenCL initialization failed for desaturate" << LL_ENDL;
		return;
	}

	const int pixelCount = width * height;
	const size_t bufSize = pixelCount * 4; // RGBA

	// Create OpenCL buffers
	cl_mem inBuf = gCL.createBuffer(bufSize, CL_MEM_READ_ONLY);
	cl_mem outBuf = gCL.createBuffer(bufSize, CL_MEM_WRITE_ONLY);

	if (!inBuf || !outBuf)
	{
		gCL.release(inBuf);
		gCL.release(outBuf);
		return;
	}

	// Allocate temporary CPU buffer for GL texture read
	std::vector<unsigned char> tempBuffer(bufSize);

	// Read texture from GPU
	glBindTexture(GL_TEXTURE_2D, texID);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, tempBuffer.data());

	// Upload to OpenCL, process, download
	gCL.writeBuffer(inBuf, tempBuffer.data(), bufSize);
	gCL.runGrayscale(inBuf, outBuf, width, height);
	gCL.readBuffer(outBuf, tempBuffer.data(), bufSize);

	// Write back to texture
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, tempBuffer.data());
	glBindTexture(GL_TEXTURE_2D, 0);

	gCL.release(inBuf);
	gCL.release(outBuf);
}

// ------------------------------------------------------------
// Color Inversion
// Inverts all RGB color channels (255 - value)
// ------------------------------------------------------------
void ImageProcessor::invertImageGPU(GLuint texID, int width, int height)
{
	if (!gCL.init())
	{
		LL_WARNS() << "OpenCL initialization failed for invert" << LL_ENDL;
		return;
	}

	const int pixelCount = width * height;
	const size_t bufSize = pixelCount * 4; // RGBA

	// Create OpenCL buffers
	cl_mem inBuf = gCL.createBuffer(bufSize, CL_MEM_READ_ONLY);
	cl_mem outBuf = gCL.createBuffer(bufSize, CL_MEM_WRITE_ONLY);

	if (!inBuf || !outBuf)
	{
		gCL.release(inBuf);
		gCL.release(outBuf);
		return;
	}

	// Allocate temporary CPU buffer for GL texture read
	std::vector<unsigned char> tempBuffer(bufSize);

	// Read texture from GPU
	glBindTexture(GL_TEXTURE_2D, texID);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, tempBuffer.data());

	// Upload to OpenCL, process, download
	gCL.writeBuffer(inBuf, tempBuffer.data(), bufSize);
	gCL.runInvert(inBuf, outBuf, pixelCount);
	gCL.readBuffer(outBuf, tempBuffer.data(), bufSize);

	// Write back to texture
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, tempBuffer.data());
	glBindTexture(GL_TEXTURE_2D, 0);

	gCL.release(inBuf);
	gCL.release(outBuf);
}

// ------------------------------------------------------------
// RGB Color Grading Control
// Advanced color manipulation with HSL conversion, gamma correction,
// per-channel boost, hue rotation, and saturation adjustment
// ------------------------------------------------------------
void ImageProcessor::RGBControlGPU(GLuint texID, int width, int height)
{
	if (!gCL.init())
	{
		LL_WARNS() << "OpenCL initialization failed for RGB control" << LL_ENDL;
		return;
	}

	const int pixelCount = width * height;
	const size_t bufSize = pixelCount * 4; // RGBA

	// Create OpenCL buffers
	cl_mem inBuf = gCL.createBuffer(bufSize, CL_MEM_READ_ONLY);
	cl_mem outBuf = gCL.createBuffer(bufSize, CL_MEM_WRITE_ONLY);

	if (!inBuf || !outBuf)
	{
		gCL.release(inBuf);
		gCL.release(outBuf);
		return;
	}

	// Fetch control parameters
	const float redControl = gSavedSettings.getF32("RedControlKV");
	const float greenControl = gSavedSettings.getF32("GreenControlKV");
	const float blueControl = gSavedSettings.getF32("BlueControlKV");
	const float boostMultiplier = gSavedSettings.getF32("BoostMultiplierKV");
	const float hueRotation = gSavedSettings.getF32("HueRotationKV");
	const float saturationControl = gSavedSettings.getF32("SaturationControlKV");
	const float contrastMultiplier = gSavedSettings.getF32("ContrastMultiplierKV");
	const float gammaCorrection = gSavedSettings.getF32("GammaCorrectionKV");

	// Allocate temporary CPU buffer for GL texture read
	std::vector<unsigned char> tempBuffer(bufSize);

	// Read texture from GPU
	glBindTexture(GL_TEXTURE_2D, texID);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, tempBuffer.data());

	// Upload to OpenCL, process with all parameters, download
	gCL.writeBuffer(inBuf, tempBuffer.data(), bufSize);
	gCL.runRGBControl(inBuf, outBuf, pixelCount,
		redControl, greenControl, blueControl,
		boostMultiplier, hueRotation, saturationControl,
		contrastMultiplier, gammaCorrection);
	gCL.readBuffer(outBuf, tempBuffer.data(), bufSize);

	// Write back to texture
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, tempBuffer.data());
	glBindTexture(GL_TEXTURE_2D, 0);

	gCL.release(inBuf);
	gCL.release(outBuf);
}

// ------------------------------------------------------------
// Cel Shading (Cartoon/Borderlands Style)
// Sobel edge detection with posterization for comic book effect
// ------------------------------------------------------------
void ImageProcessor::celShadeImageGPU(GLuint texID, int width, int height)
{
	if (!gCL.init())
	{
		LL_WARNS() << "OpenCL initialization failed for cel shading" << LL_ENDL;
		return;
	}

	const int pixelCount = width * height;
	const size_t bufSize = pixelCount * 4; // RGBA

	// Create OpenCL buffers
	cl_mem inBuf = gCL.createBuffer(bufSize, CL_MEM_READ_ONLY);
	cl_mem outBuf = gCL.createBuffer(bufSize, CL_MEM_WRITE_ONLY);

	if (!inBuf || !outBuf)
	{
		gCL.release(inBuf);
		gCL.release(outBuf);
		return;
	}

	// Fetch control parameters
	int safeInterval = std::clamp(gSavedSettings.getU32("CelshadeInterval"), 32U, 255U);
	int safeShades = std::clamp(gSavedSettings.getU32("CelnumShades"), 1U, 128U);
	int shadeInterval = std::max(1, safeInterval / std::max(1, safeShades));
	float lowVarianceThreshold = 15.0f;

	// Allocate temporary CPU buffer for GL texture read
	std::vector<unsigned char> tempBuffer(bufSize);

	// Read texture from GPU
	glBindTexture(GL_TEXTURE_2D, texID);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, tempBuffer.data());

	// Upload to OpenCL, process, download
	gCL.writeBuffer(inBuf, tempBuffer.data(), bufSize);
	gCL.runCelShade(inBuf, outBuf, width, height, shadeInterval, lowVarianceThreshold);
	gCL.readBuffer(outBuf, tempBuffer.data(), bufSize);

	// Write back to texture
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, tempBuffer.data());
	glBindTexture(GL_TEXTURE_2D, 0);

	gCL.release(inBuf);
	gCL.release(outBuf);
}

// ------------------------------------------------------------
// Vignette Effect
// Radial darkening from image center with exponential falloff
// ------------------------------------------------------------
void ImageProcessor::vignetteGPU(GLuint texID, int width, int height)
{
	if (!gCL.init())
	{
		LL_WARNS() << "OpenCL initialization failed for vignette" << LL_ENDL;
		return;
	}

	const int pixelCount = width * height;
	const size_t bufSize = pixelCount * 4; // RGBA

	// Create OpenCL buffers
	cl_mem inBuf = gCL.createBuffer(bufSize, CL_MEM_READ_ONLY);
	cl_mem outBuf = gCL.createBuffer(bufSize, CL_MEM_WRITE_ONLY);

	if (!inBuf || !outBuf)
	{
		gCL.release(inBuf);
		gCL.release(outBuf);
		return;
	}

	// Fetch vignette intensity (must match CPU version setting name)
	float intensity = gSavedSettings.getF32("VignetteIntensity");

	// Allocate temporary CPU buffer for GL texture read
	std::vector<unsigned char> tempBuffer(bufSize);

	// Read texture from GPU
	glBindTexture(GL_TEXTURE_2D, texID);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, tempBuffer.data());

	// Upload to OpenCL, process, download
	gCL.writeBuffer(inBuf, tempBuffer.data(), bufSize);
	gCL.runVignette(inBuf, outBuf, width, height, intensity);
	gCL.readBuffer(outBuf, tempBuffer.data(), bufSize);

	// Write back to texture
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, tempBuffer.data());
	glBindTexture(GL_TEXTURE_2D, 0);

	gCL.release(inBuf);
	gCL.release(outBuf);
}

// ------------------------------------------------------------
// Night Vision Effect
// Green monochrome with brightness boost and block-based noise texture
// ------------------------------------------------------------
void ImageProcessor::nightVisionGPU(GLuint texID, int width, int height)
{
	if (!gCL.init())
	{
		LL_WARNS() << "OpenCL initialization failed for night vision" << LL_ENDL;
		return;
	}

	const int pixelCount = width * height;
	const size_t bufSize = pixelCount * 4; // RGBA

	// Create OpenCL buffers
	cl_mem inBuf = gCL.createBuffer(bufSize, CL_MEM_READ_ONLY);
	cl_mem outBuf = gCL.createBuffer(bufSize, CL_MEM_WRITE_ONLY);

	if (!inBuf || !outBuf)
	{
		gCL.release(inBuf);
		gCL.release(outBuf);
		return;
	}

	// Fetch control parameters
	static LLCachedControl<F32> greenIntensity(gSavedSettings, "greenIntensity");
	static LLCachedControl<F32> brightnessBoost(gSavedSettings, "brightnessBoost");
	static LLCachedControl<U32> noiseScaleSetting(gSavedSettings, "noiseScale");
	float dimFactor = 0.2f;
	int noiseScale = std::max(1, (int)noiseScaleSetting); // Get noise scale from settings

	// Generate random seed for noise
	static std::random_device rd;
	static std::mt19937 rng(rd());
	unsigned int seed = rng();

	// Allocate temporary CPU buffer for GL texture read
	std::vector<unsigned char> tempBuffer(bufSize);

	// Read texture from GPU
	glBindTexture(GL_TEXTURE_2D, texID);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, tempBuffer.data());

	// Upload to OpenCL, process, download
	gCL.writeBuffer(inBuf, tempBuffer.data(), bufSize);
	gCL.runNightVision(inBuf, outBuf, width, height, pixelCount, greenIntensity, brightnessBoost, dimFactor, seed, noiseScale);
	gCL.readBuffer(outBuf, tempBuffer.data(), bufSize);

	// Write back to texture
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, tempBuffer.data());
	glBindTexture(GL_TEXTURE_2D, 0);

	gCL.release(inBuf);
	gCL.release(outBuf);
}

// ------------------------------------------------------------
// Edge Glow Effect
// Sobel edge detection with variable blur radius and additive glow
// ------------------------------------------------------------
void ImageProcessor::edgeGlowGPU(GLuint texID, int width, int height)
{
	if (!gCL.init())
	{
		LL_WARNS() << "OpenCL initialization failed for edge glow" << LL_ENDL;
		return;
	}

	const int pixelCount = width * height;
	const size_t bufSize = pixelCount * 4; // RGBA

	// Create OpenCL buffers
	cl_mem inBuf = gCL.createBuffer(bufSize, CL_MEM_READ_ONLY);
	cl_mem outBuf = gCL.createBuffer(bufSize, CL_MEM_WRITE_ONLY);

	if (!inBuf || !outBuf)
	{
		gCL.release(inBuf);
		gCL.release(outBuf);
		return;
	}

	// Fetch control parameters
	static LLCachedControl<F32> edgeSensitivity(gSavedSettings, "edgeSensitivity");
	static LLCachedControl<F32> glowIntensity(gSavedSettings, "edgeGlowIntensity");
	static LLCachedControl<F32> blurRadiusSetting(gSavedSettings, "edgeBlurRadius");
	int blurRadius = std::max(0, (int)blurRadiusSetting); // Convert to int and clamp

	// Allocate temporary CPU buffer for GL texture read
	std::vector<unsigned char> tempBuffer(bufSize);

	// Read texture from GPU
	glBindTexture(GL_TEXTURE_2D, texID);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, tempBuffer.data());

	// Upload to OpenCL, process, download
	gCL.writeBuffer(inBuf, tempBuffer.data(), bufSize);
	gCL.runEdgeGlow(inBuf, outBuf, width, height, edgeSensitivity, glowIntensity, blurRadius);
	gCL.readBuffer(outBuf, tempBuffer.data(), bufSize);

	// Write back to texture
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, tempBuffer.data());
	glBindTexture(GL_TEXTURE_2D, 0);

	gCL.release(inBuf);
	gCL.release(outBuf);
}

// ------------------------------------------------------------
// Motion Blur Effect
// Frame accumulation using persistent history buffer
// ------------------------------------------------------------
void ImageProcessor::motionBlurGPU(GLuint texID, int width, int height)
{
	// Persistent history buffer for frame accumulation
	static cl_mem historyBuf = nullptr;
	static int lastWidth = 0, lastHeight = 0;

	if (!gCL.init())
	{
		LL_WARNS() << "OpenCL initialization failed for motion blur" << LL_ENDL;
		return;
	}

	const int pixelCount = width * height;
	const size_t bufSize = pixelCount * 4; // RGBA

	// Reinitialize history buffer if dimensions changed
	if (historyBuf && (width != lastWidth || height != lastHeight))
	{
		gCL.release(historyBuf);
		historyBuf = nullptr;
	}

	// Create history buffer on first use or after dimension change
	if (!historyBuf)
	{
		historyBuf = gCL.createBuffer(bufSize, CL_MEM_READ_WRITE);
		if (!historyBuf)
		{
			LL_WARNS() << "Failed to create motion blur history buffer" << LL_ENDL;
			return;
		}

		// Initialize with black
		std::vector<unsigned char> blackFrame(bufSize, 0);
		gCL.writeBuffer(historyBuf, blackFrame.data(), bufSize);
		lastWidth = width;
		lastHeight = height;
	}

	// Create temporary buffers for current frame
	cl_mem inBuf = gCL.createBuffer(bufSize, CL_MEM_READ_ONLY);
	cl_mem outBuf = gCL.createBuffer(bufSize, CL_MEM_WRITE_ONLY);

	if (!inBuf || !outBuf)
	{
		gCL.release(inBuf);
		gCL.release(outBuf);
		return;
	}

	// Fetch blend factor (0.0 = full trail, 1.0 = no blur)
	static LLCachedControl<F32> blendFactor(gSavedSettings, "MotionBlurBlend");

	// Allocate temporary CPU buffer for GL texture read
	std::vector<unsigned char> tempBuffer(bufSize);

	// Read current frame from GPU texture
	glBindTexture(GL_TEXTURE_2D, texID);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, tempBuffer.data());

	// Upload current frame to OpenCL
	gCL.writeBuffer(inBuf, tempBuffer.data(), bufSize);

	// Blend current frame with history: out = current * blend + history * (1 - blend)
	gCL.runMotionBlur(inBuf, outBuf, width, height, blendFactor, historyBuf);

	// Download blended result
	gCL.readBuffer(outBuf, tempBuffer.data(), bufSize);

	// Update history buffer for next frame
	gCL.writeBuffer(historyBuf, tempBuffer.data(), bufSize);

	// Write blended result back to texture
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, tempBuffer.data());
	glBindTexture(GL_TEXTURE_2D, 0);

	gCL.release(inBuf);
	gCL.release(outBuf);
}
