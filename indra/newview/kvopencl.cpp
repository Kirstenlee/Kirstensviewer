/**
 * @file kvopencl.cpp
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

#include "llviewerprecompiledheaders.h"

#include "kvopencl.h"
#include "kveffects.h"
#include <fstream>
#include <iostream>

// ============================================================
//  Helper: load file into string (currently unused, kept for future)
// ============================================================
static std::string loadFile(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return "";
    return std::string((std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());
}

// ============================================================
//  Constructor / Destructor
// ============================================================
KVOpenCL::KVOpenCL()
    : platform(nullptr),
    device(nullptr),
    context(nullptr),
    queue(nullptr),
    initialized(false)
{
}

KVOpenCL::~KVOpenCL()
{
    // Release cached kernels
    for (auto& kv : kernelCache)
    {
        if (kv.second)
            clReleaseKernel(kv.second);
    }
    kernelCache.clear();

    if (queue)   clReleaseCommandQueue(queue);
    if (context) clReleaseContext(context);
}

// ============================================================
//  Internal helpers
// ============================================================
bool KVOpenCL::ensureInit()
{
    if (!initialized)
        return init();
    return true;
}

void KVOpenCL::release(cl_mem& m)
{
    if (m)
    {
        clReleaseMemObject(m);
        m = nullptr;
    }
}

// ============================================================
//  Init OpenCL
// ============================================================
bool KVOpenCL::init()
{
    if (initialized) return true;

    cl_int err;

    // Platform
    err = clGetPlatformIDs(1, &platform, nullptr);
    if (err != CL_SUCCESS) return false;

    // Device
    err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, nullptr);
    if (err != CL_SUCCESS) return false;

    // Context with GL sharing
       cl_context_properties props[] = {
        CL_GL_CONTEXT_KHR, (cl_context_properties)wglGetCurrentContext(),
        CL_WGL_HDC_KHR, (cl_context_properties)wglGetCurrentDC(),
        CL_CONTEXT_PLATFORM, (cl_context_properties)platform,
        0
    };
    
    context = clCreateContext(props, 1, &device, nullptr, nullptr, &err);
    if (!context || err != CL_SUCCESS) return false;

    // Queue
    queue = clCreateCommandQueue(context, device, 0, &err);
    if (!queue || err != CL_SUCCESS) return false;

    initialized = true;
    return true;
}

// ============================================================
//  Kernel cache + loader
// ============================================================
cl_kernel KVOpenCL::getKernel(const std::string& name, const char* source)
{
    auto it = kernelCache.find(name);
    if (it != kernelCache.end())
        return it->second;

    cl_program prog = buildProgram(source);
    if (!prog)
        return nullptr;

    cl_kernel k = createKernel(prog, name.c_str());
    clReleaseProgram(prog);

    if (!k)
        return nullptr;

    kernelCache[name] = k;
    return k;
}

// ============================================================
//  Build program from source
// ============================================================
cl_program KVOpenCL::buildProgram(const std::string& source)
{
    cl_int err;
    const char* src = source.c_str();
    size_t len = source.size();

    cl_program program = clCreateProgramWithSource(context, 1, &src, &len, &err);
    if (!program || err != CL_SUCCESS) return nullptr;

    err = clBuildProgram(program, 1, &device, nullptr, nullptr, nullptr);
    if (err != CL_SUCCESS)
    {
        char log[4096];
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG,
            sizeof(log), log, nullptr);
        std::cout << "OpenCL build error:\n" << log << "\n";
        clReleaseProgram(program);
        return nullptr;
    }

    return program;
}

// ============================================================
//  Create kernel
// ============================================================
cl_kernel KVOpenCL::createKernel(cl_program program, const char* name)
{
    cl_int err;
    cl_kernel kernel = clCreateKernel(program, name, &err);
    if (!kernel || err != CL_SUCCESS) return nullptr;
    return kernel;
}

// ============================================================
//  Buffer helpers
// ============================================================
cl_mem KVOpenCL::createBuffer(size_t size, cl_mem_flags flags)
{
    if (!ensureInit()) return nullptr;

    cl_int err;
    cl_mem buf = clCreateBuffer(context, flags, size, nullptr, &err);
    if (!buf || err != CL_SUCCESS) return nullptr;
    return buf;
}

bool KVOpenCL::writeBuffer(cl_mem buffer, const void* data, size_t size)
{
    if (!ensureInit()) return false;

    return clEnqueueWriteBuffer(queue, buffer, CL_TRUE, 0, size, data,
        0, nullptr, nullptr) == CL_SUCCESS;
}

bool KVOpenCL::readBuffer(cl_mem buffer, void* data, size_t size)
{
    if (!ensureInit()) return false;

    return clEnqueueReadBuffer(queue, buffer, CL_TRUE, 0, size, data,
        0, nullptr, nullptr) == CL_SUCCESS;
}

// ============================================================
//  Work group size optimization helpers
// ============================================================
size_t KVOpenCL::getOptimalWorkGroupSize1D(cl_kernel kernel)
{
    size_t maxSize;
    cl_int err = clGetKernelWorkGroupInfo(kernel, device, CL_KERNEL_WORK_GROUP_SIZE,
        sizeof(size_t), &maxSize, nullptr);

    if (err != CL_SUCCESS) return 256; // Conservative fallback

    // Use conservative 256 (safe for all modern GPUs)
    // If GPU doesn't support 256, fall back to 128
    return (maxSize >= 256) ? 256 : 128;
}

bool KVOpenCL::enqueueKernel1D(cl_kernel kernel, size_t global)
{
    if (!ensureInit()) return false;

    // Get optimal work group size for this kernel
    size_t localSize = getOptimalWorkGroupSize1D(kernel);

    // Round global size up to multiple of local size
    size_t roundedGlobal = ((global + localSize - 1) / localSize) * localSize;

    return clEnqueueNDRangeKernel(queue, kernel, 1, nullptr,
        &roundedGlobal, &localSize,
        0, nullptr, nullptr) == CL_SUCCESS;
}

bool KVOpenCL::enqueueKernel2D(cl_kernel kernel, int width, int height)
{
    if (!ensureInit()) return false;

    // Optimal 2D work group: 16×16 = 256 threads (GPU sweet spot)
    size_t local[2] = {16, 16};

    // Round global size up to multiple of 16
    size_t global[2] = {
        (size_t)(((width + 15) / 16) * 16),
        (size_t)(((height + 15) / 16) * 16)
    };

    return clEnqueueNDRangeKernel(queue, kernel, 2, nullptr,
        global, local, 0, nullptr, nullptr) == CL_SUCCESS;
}

// ============================================================
//  Run grayscale (buffer-based, uchar4)
// ============================================================
bool KVOpenCL::runGrayscale(cl_mem input,
    cl_mem output,
    int width,
    int height)
{
    if (!ensureInit()) return false;

    const int pixelCount = width * height;

    cl_kernel grayKernel = getKernel("grayscale", kCLGrayKernel);
    if (!grayKernel) return false;

    cl_int err = 0;
    err |= clSetKernelArg(grayKernel, 0, sizeof(cl_mem), &input);
    err |= clSetKernelArg(grayKernel, 1, sizeof(cl_mem), &output);
    if (err != CL_SUCCESS) return false;

    size_t global = (size_t)pixelCount;
    return enqueueKernel1D(grayKernel, global);
}

// ============================================================
//  Run contrast / brightness / invert
// ============================================================
bool KVOpenCL::runContrast(cl_mem input, cl_mem output,
    int pixelCount, float factor)
{
    if (!ensureInit()) return false;

    cl_kernel k = getKernel("contrast", kCLContrast);
    if (!k) return false;

    clSetKernelArg(k, 0, sizeof(cl_mem), &input);
    clSetKernelArg(k, 1, sizeof(cl_mem), &output);
    clSetKernelArg(k, 2, sizeof(float), &factor);

    return enqueueKernel1D(k, pixelCount);
}

bool KVOpenCL::runBrightness(cl_mem input, cl_mem output,
    int pixelCount, float amount)
{
    if (!ensureInit()) return false;

    cl_kernel k = getKernel("brightness", kCLBrightness);
    if (!k) return false;

    clSetKernelArg(k, 0, sizeof(cl_mem), &input);
    clSetKernelArg(k, 1, sizeof(cl_mem), &output);
    clSetKernelArg(k, 2, sizeof(float), &amount);

    return enqueueKernel1D(k, pixelCount);
}

bool KVOpenCL::runInvert(cl_mem input, cl_mem output, int pixelCount)
{
    if (!ensureInit()) return false;

    cl_kernel k = getKernel("invert", kCLInvert);
    if (!k) return false;

    clSetKernelArg(k, 0, sizeof(cl_mem), &input);
    clSetKernelArg(k, 1, sizeof(cl_mem), &output);

    return enqueueKernel1D(k, pixelCount);
}

bool KVOpenCL::runRGBControl(
    cl_mem input,
    cl_mem output,
    int pixelCount,
    float redControl,
    float greenControl,
    float blueControl,
    float boostMultiplier,
    float hueRotation,
    float saturationControl,
    float contrastMultiplier,
    float gammaCorrection)
{
    if (!ensureInit()) return false;

    cl_kernel k = getKernel("rgbControl", kCLRGBControl);
    if (!k) return false;

    cl_int err = 0;
    err |= clSetKernelArg(k, 0, sizeof(cl_mem), &input);
    err |= clSetKernelArg(k, 1, sizeof(cl_mem), &output);
    err |= clSetKernelArg(k, 2, sizeof(float), &redControl);
    err |= clSetKernelArg(k, 3, sizeof(float), &greenControl);
    err |= clSetKernelArg(k, 4, sizeof(float), &blueControl);
    err |= clSetKernelArg(k, 5, sizeof(float), &boostMultiplier);
    err |= clSetKernelArg(k, 6, sizeof(float), &hueRotation);
    err |= clSetKernelArg(k, 7, sizeof(float), &saturationControl);
    err |= clSetKernelArg(k, 8, sizeof(float), &contrastMultiplier);
    err |= clSetKernelArg(k, 9, sizeof(float), &gammaCorrection);

    if (err != CL_SUCCESS) return false;

    return enqueueKernel1D(k, pixelCount);
}

bool KVOpenCL::runCelShade(cl_mem input, cl_mem output, int width, int height, int shadeInterval, float lowVarianceThreshold)
{
    if (!ensureInit()) return false;

    cl_kernel k = getKernel("celShade", kCLCelShade);
    if (!k) return false;

    cl_int err = 0;
    err |= clSetKernelArg(k, 0, sizeof(cl_mem), &input);
    err |= clSetKernelArg(k, 1, sizeof(cl_mem), &output);
    err |= clSetKernelArg(k, 2, sizeof(int), &width);
    err |= clSetKernelArg(k, 3, sizeof(int), &height);
    err |= clSetKernelArg(k, 4, sizeof(int), &shadeInterval);
    err |= clSetKernelArg(k, 5, sizeof(float), &lowVarianceThreshold);

    if (err != CL_SUCCESS) return false;

    return enqueueKernel2D(k, width, height);
}

bool KVOpenCL::runVignette(cl_mem input, cl_mem output, int width, int height, float intensity)
{
    if (!ensureInit()) return false;

    cl_kernel k = getKernel("vignette", kCLVignette);
    if (!k) return false;

    cl_int err = 0;
    err |= clSetKernelArg(k, 0, sizeof(cl_mem), &input);
    err |= clSetKernelArg(k, 1, sizeof(cl_mem), &output);
    err |= clSetKernelArg(k, 2, sizeof(int), &width);
    err |= clSetKernelArg(k, 3, sizeof(int), &height);
    err |= clSetKernelArg(k, 4, sizeof(float), &intensity);

    if (err != CL_SUCCESS) return false;

    return enqueueKernel2D(k, width, height);
}

bool KVOpenCL::runNightVision(cl_mem input, cl_mem output, int width, int height, int pixelCount, float greenIntensity, float brightnessBoost, float dimFactor, unsigned int seed, int noiseScale)
{
    if (!ensureInit()) return false;

    cl_kernel k = getKernel("nightVision", kCLNightVision);
    if (!k) return false;

    cl_int err = 0;
    err |= clSetKernelArg(k, 0, sizeof(cl_mem), &input);
    err |= clSetKernelArg(k, 1, sizeof(cl_mem), &output);
    err |= clSetKernelArg(k, 2, sizeof(int), &width);
    err |= clSetKernelArg(k, 3, sizeof(int), &height);
    err |= clSetKernelArg(k, 4, sizeof(float), &greenIntensity);
    err |= clSetKernelArg(k, 5, sizeof(float), &brightnessBoost);
    err |= clSetKernelArg(k, 6, sizeof(float), &dimFactor);
    err |= clSetKernelArg(k, 7, sizeof(unsigned int), &seed);
    err |= clSetKernelArg(k, 8, sizeof(int), &noiseScale);

    if (err != CL_SUCCESS) return false;

    return enqueueKernel1D(k, pixelCount);
}

bool KVOpenCL::runEdgeGlow(cl_mem input, cl_mem output, int width, int height, float edgeSensitivity, float glowIntensity, int blurRadius)
{
    if (!ensureInit()) return false;

    cl_kernel k = getKernel("edgeGlow", kCLEdgeGlow);
    if (!k) return false;

    cl_int err = 0;
    err |= clSetKernelArg(k, 0, sizeof(cl_mem), &input);
    err |= clSetKernelArg(k, 1, sizeof(cl_mem), &output);
    err |= clSetKernelArg(k, 2, sizeof(int), &width);
    err |= clSetKernelArg(k, 3, sizeof(int), &height);
    err |= clSetKernelArg(k, 4, sizeof(float), &edgeSensitivity);
    err |= clSetKernelArg(k, 5, sizeof(float), &glowIntensity);
    err |= clSetKernelArg(k, 6, sizeof(int), &blurRadius);

    if (err != CL_SUCCESS) return false;

    return enqueueKernel2D(k, width, height);
}

bool KVOpenCL::runMotionBlur(cl_mem input, cl_mem output, int width, int height, float blendFactor, cl_mem historyBuffer)
{
    if (!ensureInit()) return false;

    cl_kernel k = getKernel("motionBlur", kCLMotionBlur);
    if (!k) return false;

    const int pixelCount = width * height;

    cl_int err = 0;
    err |= clSetKernelArg(k, 0, sizeof(cl_mem), &input);
    err |= clSetKernelArg(k, 1, sizeof(cl_mem), &historyBuffer);
    err |= clSetKernelArg(k, 2, sizeof(cl_mem), &output);
    err |= clSetKernelArg(k, 3, sizeof(float), &blendFactor);

    if (err != CL_SUCCESS) return false;

    return enqueueKernel1D(k, pixelCount);
}

cl_mem KVOpenCL::createFromGLTexture(GLuint texID, int width, int height)
{
    if (!ensureInit()) return nullptr;

    cl_int err;
    cl_mem clImage = clCreateFromGLTexture(
        context,
        CL_MEM_READ_WRITE,
        GL_TEXTURE_2D,
        0,  // miplevel
        texID,
        &err);

    if (err != CL_SUCCESS) return nullptr;
    return clImage;
}

bool KVOpenCL::acquireGLObject(cl_mem clImage)
{
    return clEnqueueAcquireGLObjects(queue, 1, &clImage, 0, nullptr, nullptr) == CL_SUCCESS;
}

bool KVOpenCL::releaseGLObject(cl_mem clImage)
{
    return clEnqueueReleaseGLObjects(queue, 1, &clImage, 0, nullptr, nullptr) == CL_SUCCESS;
}

// ============================================================
//  OpenCL Kernel Source Code
//  All kernels operate on uchar4 (RGBA) buffer format
//  Kernels are compiled just-in-time and cached for performance
// ============================================================

// ------------------------------------------------------------
// Basic Image Processing Kernels
// ------------------------------------------------------------

// Grayscale conversion using luminance weights (0.3R + 0.59G + 0.11B)
const char* KVOpenCL::kCLGrayKernel = R"CLC(
__kernel void grayscale(
    __global const uchar4* input,
    __global uchar4* output)
{
    int i = get_global_id(0);

    uchar4 px = input[i];

    float r = (float)px.x;
    float g = (float)px.y;
    float b = (float)px.z;

    float gray = r * 0.3f + g * 0.59f + b * 0.11f;

    output[i] = (uchar4)(gray, gray, gray, 255);
}
)CLC";

// Color inversion (255 - RGB)
const char* KVOpenCL::kCLInvert = R"CLC(
__kernel void invert(__global const uchar4* input,
                     __global uchar4* output)
{
    int i = get_global_id(0);
    uchar4 px = input[i];
    output[i] = (uchar4)(255 - px.x, 255 - px.y, 255 - px.z, px.w);
}
)CLC";

// Brightness adjustment (additive)
const char* KVOpenCL::kCLBrightness = R"CLC(
__kernel void brightness(__global const uchar4* input,
                         __global uchar4* output,
                         float amount)
{
    int i = get_global_id(0);
    uchar4 px = input[i];

    float r = clamp((float)px.x + amount, 0.0f, 255.0f);
    float g = clamp((float)px.y + amount, 0.0f, 255.0f);
    float b = clamp((float)px.z + amount, 0.0f, 255.0f);

    output[i] = (uchar4)(r, g, b, px.w);
}
)CLC";

// Contrast adjustment (multiplicative around midpoint)
const char* KVOpenCL::kCLContrast = R"CLC(
__kernel void contrast(__global const uchar4* input,
                       __global uchar4* output,
                       float factor)
{
    int i = get_global_id(0);
    uchar4 px = input[i];

    float r = clamp(((float)px.x - 128.0f) * factor + 128.0f, 0.0f, 255.0f);
    float g = clamp(((float)px.y - 128.0f) * factor + 128.0f, 0.0f, 255.0f);
    float b = clamp(((float)px.z - 128.0f) * factor + 128.0f, 0.0f, 255.0f);

    output[i] = (uchar4)(r, g, b, px.w);
}
)CLC";

// ------------------------------------------------------------
// Advanced Color Grading Kernels
// ------------------------------------------------------------

// RGB Control with HSL manipulation, gamma correction, per-channel boost
// Manually inlined HSL conversion (OpenCL C doesn't support nested functions)
const char* KVOpenCL::kCLRGBControl = R"CLC(
__kernel void rgbControl(
    __global const uchar4* input,
    __global uchar4* output,
    float redControl,
    float greenControl,
    float blueControl,
    float boostMultiplier,
    float hueRotation,
    float saturationControl,
    float contrastMultiplier,
    float gammaCorrection)
{
    int i = get_global_id(0);
    uchar4 px = input[i];

    // Convert to float [0-255]
    float r = (float)px.x;
    float g = (float)px.y;
    float b = (float)px.z;

    // Apply contrast: (value - 128)*contrastMultiplier + 128
    r = (r - 128.0f) * contrastMultiplier + 128.0f;
    g = (g - 128.0f) * contrastMultiplier + 128.0f;
    b = (b - 128.0f) * contrastMultiplier + 128.0f;

    // Normalize to [0,1]
    r = clamp(r / 255.0f, 0.0f, 1.0f);
    g = clamp(g / 255.0f, 0.0f, 1.0f);
    b = clamp(b / 255.0f, 0.0f, 1.0f);

    // RGB to HSL
    float maxVal = fmax(fmax(r, g), b);
    float minVal = fmin(fmin(r, g), b);
    float d = maxVal - minVal;
    float l = (maxVal + minVal) * 0.5f;
    float h = 0.0f, s = 0.0f;

    if (d != 0.0f) {
        s = (l > 0.5f) ? d / (2.0f - maxVal - minVal) : d / (maxVal + minVal);

        // Hue calculation
        if (maxVal == r) {
            h = ((g - b) / d) + (g < b ? 6.0f : 0.0f);
        } else if (maxVal == g) {
            h = ((b - r) / d) + 2.0f;
        } else {
            h = ((r - g) / d) + 4.0f;
        }
        h *= 60.0f;
    }

    // Apply hue rotation and saturation adjustment
    h = fmod(h + hueRotation, 360.0f);
    if (h < 0.0f) h += 360.0f;
    s = clamp(s * saturationControl, 0.0f, 1.0f);

    // HSL to RGB
    float q = (l < 0.5f) ? (l * (1.0f + s)) : (l + s - l * s);
    float p = 2.0f * l - q;

    // Manually inlined hue-to-RGB conversion (OpenCL doesn't support nested functions)
    float hNorm = h / 360.0f;

    // Calculate R channel (t = hNorm + 1/3)
    {
        float t = hNorm + 1.0f / 3.0f;
        if (t < 0.0f) t += 1.0f;
        if (t > 1.0f) t -= 1.0f;
        if (t < 1.0f / 6.0f) 
            r = p + (q - p) * 6.0f * t;
        else if (t < 0.5f) 
            r = q;
        else if (t < 2.0f / 3.0f) 
            r = p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
        else 
            r = p;
    }

    // Calculate G channel (t = hNorm)
    {
        float t = hNorm;
        if (t < 0.0f) t += 1.0f;
        if (t > 1.0f) t -= 1.0f;
        if (t < 1.0f / 6.0f) 
            g = p + (q - p) * 6.0f * t;
        else if (t < 0.5f) 
            g = q;
        else if (t < 2.0f / 3.0f) 
            g = p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
        else 
            g = p;
    }

    // Calculate B channel (t = hNorm - 1/3)
    {
        float t = hNorm - 1.0f / 3.0f;
        if (t < 0.0f) t += 1.0f;
        if (t > 1.0f) t -= 1.0f;
        if (t < 1.0f / 6.0f) 
            b = p + (q - p) * 6.0f * t;
        else if (t < 0.5f) 
            b = q;
        else if (t < 2.0f / 3.0f) 
            b = p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
        else 
            b = p;
    }

    // Convert back to [0-255]
    r *= 255.0f;
    g *= 255.0f;
    b *= 255.0f;

    // Gamma correction
    r = 255.0f * pow(r / 255.0f, gammaCorrection);
    g = 255.0f * pow(g / 255.0f, gammaCorrection);
    b = 255.0f * pow(b / 255.0f, gammaCorrection);

    // Clamp after gamma
    r = clamp(r, 0.0f, 255.0f);
    g = clamp(g, 0.0f, 255.0f);
    b = clamp(b, 0.0f, 255.0f);

    // Apply per-channel boost multipliers
    r *= (redControl * boostMultiplier);
    g *= (greenControl * boostMultiplier);
    b *= (blueControl * boostMultiplier);

    // Final clamp
    r = clamp(r, 0.0f, 255.0f);
    g = clamp(g, 0.0f, 255.0f);
    b = clamp(b, 0.0f, 255.0f);

    // Write result
    output[i] = (uchar4)((uchar)r, (uchar)g, (uchar)b, px.w);
}
)CLC";

// ============================================================
//  Image2D kernel runners (for GL-CL interop)
// ============================================================
bool KVOpenCL::runGrayscaleImage(cl_mem imageIn, cl_mem imageOut, int width, int height)
{
    if (!ensureInit()) return false;

    cl_kernel k = getKernel("grayscale_image", kCLGrayImage);
    if (!k) return false;

    clSetKernelArg(k, 0, sizeof(cl_mem), &imageIn);
    clSetKernelArg(k, 1, sizeof(cl_mem), &imageOut);

    size_t global[2] = { (size_t)width, (size_t)height };
    return clEnqueueNDRangeKernel(queue, k, 2, nullptr,
        global, nullptr, 0, nullptr, nullptr) == CL_SUCCESS;
}

bool KVOpenCL::runInvertImage(cl_mem imageIn, cl_mem imageOut, int width, int height)
{
    if (!ensureInit()) return false;

    cl_kernel k = getKernel("invert_image", kCLInvertImage);
    if (!k) return false;

    clSetKernelArg(k, 0, sizeof(cl_mem), &imageIn);
    clSetKernelArg(k, 1, sizeof(cl_mem), &imageOut);

    size_t global[2] = { (size_t)width, (size_t)height };
    return clEnqueueNDRangeKernel(queue, k, 2, nullptr,
        global, nullptr, 0, nullptr, nullptr) == CL_SUCCESS;
}

// ------------------------------------------------------------
// Image2D Kernels (Deprecated - kept for reference)
// These use image2d_t instead of buffer-based approach
// ------------------------------------------------------------

// Grayscale using Image2D format
const char* KVOpenCL::kCLGrayImage = R"CLC(
__kernel void grayscale_image(__read_only image2d_t input, __write_only image2d_t output)
{
    const sampler_t sampler = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_NEAREST;
    
    int x = get_global_id(0);
    int y = get_global_id(1);
    int2 coord = (int2)(x, y);
    
    float4 px = read_imagef(input, sampler, coord);
    
    float gray = 0.3f * px.x + 0.59f * px.y + 0.11f * px.z;
    
    float4 result = (float4)(gray, gray, gray, px.w);
    write_imagef(output, coord, result);
}
)CLC";

// Color inversion using Image2D format
const char* KVOpenCL::kCLInvertImage = R"CLC(
__kernel void invert_image(__read_only image2d_t input, __write_only image2d_t output)
{
    const sampler_t sampler = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_NEAREST;

    int x = get_global_id(0);
    int y = get_global_id(1);
    int2 coord = (int2)(x, y);

    float4 px = read_imagef(input, sampler, coord);
    float4 result = (float4)(1.0f - px.x, 1.0f - px.y, 1.0f - px.z, px.w);

    write_imagef(output, coord, result);
}
)CLC";

// ------------------------------------------------------------
// Stylized Effect Kernels
// ------------------------------------------------------------

// Cel Shading: Sobel edge detection + posterization + variance-based filtering
const char* KVOpenCL::kCLCelShade = R"CLC(
__kernel void celShade(
    __global const uchar4* input,
    __global uchar4* output,
    int width,
    int height,
    int shadeInterval,
    float lowVarianceThreshold)
{
    int x = get_global_id(0);
    int y = get_global_id(1);

    if (x >= width || y >= height) return;

    int idx = y * width + x;
    uchar4 px = input[idx];

    // Border pixels - output unchanged
    if (x < 1 || y < 1 || x >= width - 1 || y >= height - 1)
    {
        output[idx] = px;
        return;
    }

    // Sobel edge detection - use unrolled version for better performance
    int gx = 0, gy = 0;

    // Top row
    gx += -1 * input[idx - 1 - width].x + 0 * input[idx - width].x + 1 * input[idx + 1 - width].x;
    gy += -1 * input[idx - 1 - width].x + -2 * input[idx - width].x + -1 * input[idx + 1 - width].x;

    // Middle row
    gx += -2 * input[idx - 1].x + 0 * input[idx].x + 2 * input[idx + 1].x;
    gy += 0 * input[idx - 1].x + 0 * input[idx].x + 0 * input[idx + 1].x;

    // Bottom row
    gx += -1 * input[idx - 1 + width].x + 0 * input[idx + width].x + 1 * input[idx + 1 + width].x;
    gy += 1 * input[idx - 1 + width].x + 2 * input[idx + width].x + 1 * input[idx + 1 + width].x;

    // Edge intensity with higher sensitivity
    float edgeIntensity = sqrt((float)(gx * gx + gy * gy));
    edgeIntensity = min(255.0f, edgeIntensity * 1.5f); // Amplify edge detection

    // Dynamic brightness scaling
    int avgBrightness = (px.x + px.y + px.z) / 3;
    float brightnessScale = 1.0f + ((128.0f - avgBrightness) / 128.0f);
    int adjustedEdgeIntensity = (int)clamp(edgeIntensity * brightnessScale, 20.0f, 255.0f);

    // Variance check for posterization
    float variance = 0.0f;
    int varCount = 0;
    for (int ky = -1; ky <= 1; ++ky)
    {
        for (int kx = -1; kx <= 1; ++kx)
        {
            int nIdx = (y + ky) * width + (x + kx);
            uchar4 neighbor = input[nIdx];
            float intensity = (neighbor.x + neighbor.y + neighbor.z) / 3.0f;
            variance += intensity * intensity;
            varCount++;
        }
    }
    variance = sqrt(variance / (float)varCount);

    // Apply stronger posterization if variance is high enough
    uchar4 result = px;
    if (variance >= lowVarianceThreshold && shadeInterval > 1)
    {
        // Posterize with more aggressive quantization
        result.x = ((px.x + shadeInterval/2) / shadeInterval) * shadeInterval;
        result.y = ((px.y + shadeInterval/2) / shadeInterval) * shadeInterval;
        result.z = ((px.z + shadeInterval/2) / shadeInterval) * shadeInterval;

        // Clamp to valid range
        result.x = min(255, (int)result.x);
        result.y = min(255, (int)result.y);
        result.z = min(255, (int)result.z);
    }

    // Blend edge detection (darkening edges) - more aggressive
    int edgeValue = 255 - adjustedEdgeIntensity;
    result.x = (result.x * edgeValue) / 255;
    result.y = (result.y * edgeValue) / 255;
    result.z = (result.z * edgeValue) / 255;

    output[idx] = result;
}
)CLC";

// Vignette: Radial darkening from center with exponential falloff
const char* KVOpenCL::kCLVignette = R"CLC(
__kernel void vignette(
    __global const uchar4* input,
    __global uchar4* output,
    int width,
    int height,
    float intensity)
{
    int x = get_global_id(0);
    int y = get_global_id(1);

    if (x >= width || y >= height) return;

    int idx = y * width + x;
    uchar4 px = input[idx];

    // Calculate distance from center
    float centerX = width * 0.5f;
    float centerY = height * 0.5f;
    float dx = x - centerX;
    float dy = y - centerY;
    float distance = sqrt(dx * dx + dy * dy);

    // Maximum distance (corner to center)
    float maxDistance = sqrt(centerX * centerX + centerY * centerY);

    // Vignette factor with exponential falloff
    float vignetteFactor = 1.0f - (distance / maxDistance);
    vignetteFactor = vignetteFactor * vignetteFactor; // Exponential falloff

    // Intensity adjustment
    float intensityFactor = 1.0f - intensity;
    vignetteFactor = intensityFactor + (vignetteFactor * intensity);

    // Apply vignette
    uchar4 result;
    result.x = (uchar)(px.x * vignetteFactor);
    result.y = (uchar)(px.y * vignetteFactor);
    result.z = (uchar)(px.z * vignetteFactor);
    result.w = px.w;

    output[idx] = result;
}
)CLC";

// Night Vision: Green monochrome with seamless block-based noise
const char* KVOpenCL::kCLNightVision = R"CLC(
__kernel void nightVision(
    __global const uchar4* input,
    __global uchar4* output,
    int width,
    int height,
    float greenIntensity,
    float brightnessBoost,
    float dimFactor,
    unsigned int seed,
    int noiseScale)
{
    int i = get_global_id(0);

    // Calculate x,y coordinates from linear index
    int x = i % width;
    int y = i / width;

    uchar4 px = input[i];

    // Compute grayscale value
    uchar grayscale = (uchar)(0.3f * px.x + 0.59f * px.y + 0.11f * px.z);

    // Enhance brightness and apply green tint
    uchar greenTint = (uchar)clamp(greenIntensity * grayscale + brightnessBoost, 0.0f, 255.0f);

    // Apply night vision effect
    float r = dimFactor * greenTint;
    float g = greenTint;
    float b = dimFactor * greenTint;

    // Block-based noise: compute block coordinates with seamless wrapping
    // Use modulo 256 to create a seamless repeating noise pattern
    int blockX = (x / noiseScale) & 255;  // Wrap at 256 blocks (bitwise AND is faster than modulo)
    int blockY = (y / noiseScale) & 255;

    // Generate noise using better spatial hash (avoids circular patterns)
    // Mix X, Y, and seed independently for better randomness
    unsigned int hash = (unsigned int)blockX;
    hash = hash * 1103515245u + (unsigned int)blockY;
    hash = hash * 48271u + seed;
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    hash *= 0x846ca68bu;
    hash ^= hash >> 16;

    // Much larger noise range for visibility: -50 to +50
    int noise = ((int)(hash % 101)) - 50;

    // Add noise and clamp
    r = clamp(r + noise, 0.0f, 255.0f);
    g = clamp(g + noise, 0.0f, 255.0f);
    b = clamp(b + noise, 0.0f, 255.0f);

    uchar4 result;
    result.x = (uchar)r;
    result.y = (uchar)g;
    result.z = (uchar)b;
    result.w = px.w;

    output[i] = result;
}
)CLC";

// Edge Glow: Sobel detection + variable box blur + additive blending
const char* KVOpenCL::kCLEdgeGlow = R"CLC(
__kernel void edgeGlow(
    __global const uchar4* input,
    __global uchar4* output,
    int width,
    int height,
    float edgeSensitivity,
    float glowIntensity,
    int blurRadius)
{
    int x = get_global_id(0);
    int y = get_global_id(1);

    if (x >= width || y >= height) return;

    int idx = y * width + x;
    uchar4 px = input[idx];

    // Clamp blur radius to reasonable range
    int maxBlur = min(10, blurRadius);
    int borderSize = maxBlur + 1;

    // Border pixels - no edge detection
    if (x < borderSize || y < borderSize || x >= width - borderSize || y >= height - borderSize)
    {
        output[idx] = px;
        return;
    }

    // Sobel edge detection on red channel (at current pixel)
    int gx = 0, gy = 0;

    gx = -1 * input[idx - 1 - width].x + 1 * input[idx + 1 - width].x +
         -2 * input[idx - 1].x + 2 * input[idx + 1].x +
         -1 * input[idx - 1 + width].x + 1 * input[idx + 1 + width].x;

    gy = -1 * input[idx - 1 - width].x + -2 * input[idx - width].x + -1 * input[idx + 1 - width].x +
         1 * input[idx - 1 + width].x + 2 * input[idx + width].x + 1 * input[idx + 1 + width].x;

    int magnitude = (int)sqrt((float)(gx * gx + gy * gy));
    int centerEdge = min(255, (int)(edgeSensitivity * magnitude));

    // Dynamic box blur on edge values based on blurRadius parameter
    int blurredEdge = centerEdge;
    int blurCount = 1;

    // If blur radius is 0, skip blur
    if (maxBlur > 0)
    {
        blurredEdge = 0;
        blurCount = 0;

        // Sample square neighborhood of size (2*maxBlur+1)
        for (int dy = -maxBlur; dy <= maxBlur; dy++)
        {
            for (int dx = -maxBlur; dx <= maxBlur; dx++)
            {
                int nIdx = (y + dy) * width + (x + dx);

                // Compute edge at neighbor
                int ngx = -1 * input[nIdx - 1 - width].x + 1 * input[nIdx + 1 - width].x +
                          -2 * input[nIdx - 1].x + 2 * input[nIdx + 1].x +
                          -1 * input[nIdx - 1 + width].x + 1 * input[nIdx + 1 + width].x;

                int ngy = -1 * input[nIdx - 1 - width].x + -2 * input[nIdx - width].x + -1 * input[nIdx + 1 - width].x +
                          1 * input[nIdx - 1 + width].x + 2 * input[nIdx + width].x + 1 * input[nIdx + 1 + width].x;

                int nMag = (int)sqrt((float)(ngx * ngx + ngy * ngy));
                int nEdge = min(255, (int)(edgeSensitivity * nMag));

                blurredEdge += nEdge;
                blurCount++;
            }
        }

        blurredEdge = blurredEdge / blurCount;
    }

    // Apply glow by adding blurred edge to original
    int glowAmount = (int)(glowIntensity * blurredEdge);

    uchar4 result;
    result.x = min(255, (int)px.x + glowAmount);
    result.y = min(255, (int)px.y + glowAmount);
    result.z = min(255, (int)px.z + glowAmount);
    result.w = px.w;

    output[idx] = result;
}
)CLC";

// ------------------------------------------------------------
// Motion Blur Kernel
// ------------------------------------------------------------
const char* KVOpenCL::kCLMotionBlur = R"CLC(
__kernel void motionBlur(
    __global const uchar4* current,
    __global const uchar4* history,
    __global uchar4* output,
    float blendFactor)
{
    int i = get_global_id(0);

    uchar4 curr = current[i];
    uchar4 hist = history[i];

    // Blend: output = current * blendFactor + history * (1 - blendFactor)
    // blendFactor 0.0 = full trail (all history)
    // blendFactor 1.0 = no blur (all current)
    float invBlend = 1.0f - blendFactor;

    uchar4 result;
    result.x = (uchar)clamp(curr.x * blendFactor + hist.x * invBlend, 0.0f, 255.0f);
    result.y = (uchar)clamp(curr.y * blendFactor + hist.y * invBlend, 0.0f, 255.0f);
    result.z = (uchar)clamp(curr.z * blendFactor + hist.z * invBlend, 0.0f, 255.0f);
    result.w = curr.w; // Preserve alpha

    output[i] = result;
}
)CLC";


