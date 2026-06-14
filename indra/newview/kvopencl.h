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
    bool initialized;

    // ============================================================
    //  Kernel Cache (for performance)
    // ============================================================
    std::unordered_map<std::string, cl_kernel> kernelCache;

    // ============================================================
    //  Internal Helpers
    // ============================================================
    bool ensureInit();
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

#endif // KVOPENCL_HPP

