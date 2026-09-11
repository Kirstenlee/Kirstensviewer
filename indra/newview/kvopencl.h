/**
 * @file kvopencl.h
 * @brief KV OpenCL - GPU acceleration toolbox with OpenGL interop
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

#ifndef KVOPENCL_HPP
#define KVOPENCL_HPP

#pragma once

#define CL_TARGET_OPENCL_VERSION 300
#include <CL/cl.h>
#include <CL/cl_gl.h>  // OpenGL-OpenCL interop

#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>

// ============================================================
//  OpenCL Wrapper Class
//  Manages OpenCL context, kernels, and buffer operations
// ============================================================
class KVOpenCL
{
public:
    KVOpenCL();
    ~KVOpenCL();

    // ============================================================
    //  Initialization
    // ============================================================
    bool init();

    // S24 (2026-09-09, BC7 texture-compression pipeline): was private -
    // this is exactly what an out-of-TU background caller (DXBC7Compressor,
    // dxrender/) needs to safely lazy-init this class the same way every
    // existing run*() method already does internally. Now guarded by
    // mInitMutex (see .cpp) so it's safe to call concurrently with the main
    // thread's own per-frame VFX use - previously an unguarded
    // check-then-set race on `initialized`.
    bool ensureInit();

    // S24 (2026-09-09, BC7 pipeline): accessors for a second, independent
    // caller (DXBC7Compressor) that needs its own dedicated cl_command_queue
    // (so its blocking writeBuffer/readBuffer calls on a background thread
    // never contend with the main thread's per-frame VFX queue) and its own
    // dedicated cl_kernel (so it never touches the unguarded, main-thread-
    // only kernelCache/getKernel() path). context/device/platform are
    // create-once in init() and never reassigned after - safe to read from
    // any thread once ensureInit() has returned true.
    cl_context getContext() const { return context; }
    cl_device_id getDevice() const { return device; }
    cl_command_queue getComputeQueue() const { return computeQueue; }

    // ============================================================
    //  Program & Kernel Management
    // ============================================================
    cl_program buildProgram(const std::string& source);
    cl_kernel createKernel(cl_program program, const char* name);
    cl_mem createFromGLTexture(GLuint texID, int width, int height);

    // ============================================================
    //  Buffer Operations
    // ============================================================
    cl_mem createBuffer(size_t size, cl_mem_flags flags);
    bool writeBuffer(cl_mem buffer, const void* data, size_t size);
    bool readBuffer(cl_mem buffer, void* data, size_t size);
    void release(cl_mem& m);

    // ============================================================
    //  Kernel Execution Helpers
    // ============================================================
    bool enqueueKernel1D(cl_kernel kernel, size_t global);

    // ============================================================
    //  Basic Image Effects (buffer-based, uchar4 RGBA format)
    // ============================================================

    // Grayscale conversion
    bool runGrayscale(cl_mem input, cl_mem output, int width, int height);

    // Color inversion
    bool runInvert(cl_mem input, cl_mem output, int pixelCount);

    // Brightness adjustment
    bool runBrightness(cl_mem input, cl_mem output, int pixelCount, float amount);

    // Contrast adjustment
    bool runContrast(cl_mem input, cl_mem output, int pixelCount, float factor);

    // ============================================================
    //  Advanced Image Effects
    // ============================================================

    // RGB control with HSL manipulation, gamma correction, per-channel boost
    // Parameters: input/output buffers, pixel count, RGB controls (0-2), boost multiplier,
    //             hue rotation (degrees), saturation (0-2), contrast (0-2), gamma (0.1-3)
    bool runRGBControl(cl_mem input, cl_mem output, int pixelCount,
                       float redControl, float greenControl, float blueControl,
                       float boostMultiplier, float hueRotation, float saturationControl,
                       float contrastMultiplier, float gammaCorrection);

    // Cel shading (cartoon effect) with Sobel edge detection and posterization
    // Parameters: input/output buffers, dimensions, shade interval (posterization level),
    //             variance threshold for flat regions
    bool runCelShade(cl_mem input, cl_mem output, int width, int height,
                     int shadeInterval, float lowVarianceThreshold);

    // Vignette (radial darkening from center)
    // Parameters: input/output buffers, dimensions, intensity (0-1)
    bool runVignette(cl_mem input, cl_mem output, int width, int height, float intensity);

    // Night vision with green tint and block-based noise texture
    // Parameters: input/output buffers, dimensions, pixel count, green intensity (0-2),
    //             brightness boost (0-255), dim factor (0-1), random seed, noise block size
    bool runNightVision(cl_mem input, cl_mem output, int width, int height, int pixelCount,
                        float greenIntensity, float brightnessBoost, float dimFactor,
                        unsigned int seed, int noiseScale);

    // Edge glow with Sobel detection and variable blur radius
    // Parameters: input/output buffers, dimensions, edge sensitivity (0-10),
    //             glow intensity (0-10), blur radius (0-10)
    bool runEdgeGlow(cl_mem input, cl_mem output, int width, int height,
                     float edgeSensitivity, float glowIntensity, int blurRadius);

    // Motion blur with frame accumulation and decay
    // Parameters: input/output buffers, dimensions, blend factor (0-1), history buffer
    bool runMotionBlur(cl_mem input, cl_mem output, int width, int height,
                       float blendFactor, cl_mem historyBuffer);

    // ============================================================
    //  OpenGL Interop (for direct texture processing)
    // ============================================================
    bool acquireGLObject(cl_mem clImage);
    bool releaseGLObject(cl_mem clImage);

    // ============================================================
    //  Image2D Effects (deprecated - kept for reference)
    // ============================================================
    bool runGrayscaleImage(cl_mem imageIn, cl_mem imageOut, int width, int height);
    bool runInvertImage(cl_mem imageIn, cl_mem imageOut, int width, int height);

 private:
    // ============================================================
    //  Core OpenCL Objects
    // ============================================================
    cl_platform_id platform;
    cl_device_id device;
    cl_context context;
    cl_command_queue queue;
    // S24 (2026-09-09, BC7 pipeline): second queue, same context/device
    // (OpenCL spec explicitly allows multiple queues sharing one context) -
    // reserved for DXBC7Compressor's background-thread use so it never
    // shares a queue (and therefore never needs to serialize) with the main
    // thread's per-frame VFX enqueue calls on `queue` above.
    cl_command_queue computeQueue;
    // S24 (2026-09-09, BC7 pipeline): was a plain bool - ensureInit()'s
    // fast-path check (the overwhelmingly common case: already initialized,
    // called every frame per VFX effect) reads this with NO lock for speed,
    // so it needs to be an atomic rather than a data race over a plain bool.
    // The actual init-or-not transition is still fully serialized by
    // mInitMutex inside init() itself - this atomic only makes the cheap
    // "are we already done" read safe to do lock-free.
    std::atomic<bool> initialized;

    // S24 (2026-09-09, BC7 pipeline): guards the init()/ensureInit()
    // check-then-set and ensures only one thread ever runs the actual
    // clCreateContext/clCreateCommandQueue sequence. Also guards
    // kernelCache below - getKernel() is otherwise an unguarded
    // unordered_map find+insert, unsafe if ever called from more than one
    // thread (DXBC7Compressor does NOT use it - see getKernel()'s own
    // comment - but this still closes the theoretical gap for any future
    // caller).
    std::mutex mInitMutex;

    // ============================================================
    //  Kernel Cache (for performance)
    // ============================================================
    std::unordered_map<std::string, cl_kernel> kernelCache;

    // ============================================================
    //  Internal Helpers
    // ============================================================
    cl_kernel getKernel(const std::string& name, const char* source);

    // GPU work group optimization helpers
    size_t getOptimalWorkGroupSize1D(cl_kernel kernel);
    bool enqueueKernel2D(cl_kernel kernel, int width, int height);

    // ============================================================
    //  Embedded OpenCL Kernel Sources
    // ============================================================

    // Basic effects
    static const char* kCLGrayKernel;    // Grayscale conversion
    static const char* kCLInvert;        // Color inversion
    static const char* kCLBrightness;    // Brightness adjustment
    static const char* kCLContrast;      // Contrast adjustment

    // Advanced effects
    static const char* kCLRGBControl;    // RGB/HSL color grading
    static const char* kCLCelShade;      // Cel shading with edge detection
    static const char* kCLVignette;      // Radial vignette
    static const char* kCLNightVision;   // Night vision with noise
    static const char* kCLEdgeGlow;      // Edge glow with blur
    static const char* kCLMotionBlur;    // Motion blur with accumulation

    // Image2D kernels (deprecated)
    static const char* kCLGrayImage;     // Image2D grayscale
    static const char* kCLInvertImage;   // Image2D inversion
};

// S24 (2026-09-09, BC7 texture-compression pipeline): the single shared
// KVOpenCL instance (defined non-static now in kveffects.cpp - see its own
// comment there). Declared here rather than in kveffects.h so callers that
// only need the OpenCL wrapper itself (DXBC7Compressor, dxrender/) don't
// need to pull in kveffects.h's VFX-specific API surface at all.
extern KVOpenCL gCL;

#endif // KVOPENCL_HPP

