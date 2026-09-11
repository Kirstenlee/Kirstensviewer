#include "llviewerprecompiledheaders.h"

#include "dxbc7compressor.h"
#include "kvopencl.h"
#include "llerror.h"

#include <mutex>
#include <cstring>

// ============================================================
//  BC7 mode 6 kernel - one work-item per 4x4 block.
// ============================================================
//
// Mode 6 (single subset, no partitioning) bit layout, 128 bits/block,
// packed LSB-first starting at byte 0 bit 0 (the standard BC7/DDS block
// convention - matches D3D11's own expected subresource layout with zero
// reordering needed on the C++ side):
//   bits [0..6]    mode (7 bits, unary - mode 6 = six 0 bits then a 1,
//                  i.e. value 0x40 with bit 6 set)
//   bits [7..62]   endpoints: R0,R1,G0,G1,B0,B1,A0,A1, 7 bits each (56 bits)
//   bit  [63]      P0 - shared p-bit for endpoint 0 (all of R0/G0/B0/A0)
//   bit  [64]      P1 - shared p-bit for endpoint 1 (all of R1/G1/B1/A1)
//   bits [65..127] indices - 4 bits/texel x 16 texels, EXCEPT texel 0 (the
//                  "anchor"), which only stores 3 bits (its top bit is
//                  always implicitly 0, a standard BC7 bit-saving rule for
//                  the first index of a subset) - 63 bits total.
// P-bit reconstruction for mode 6: 7 explicit bits + 1 shared p-bit = 8
// bits exactly, so recon8 = (bits7 << 1) | p, with NO further bit
// replication needed (unlike BC7 modes with fewer explicit bits) - the
// simplifying reason mode 6 is a good fit for a fast, single-mode encoder.
const char* DXBC7Compressor::kBC7Mode6KernelSource = R"CLC(
__kernel void bc7_encode_mode6(
    __global const uchar4* src,
    __global uchar* dst,
    int width,
    int height)
{
    int bx = get_global_id(0);
    int by = get_global_id(1);
    int blocksX = width / 4;
    int blocksY = height / 4;
    if (bx >= blocksX || by >= blocksY) return;

    uchar4 px[16];
    int idx = 0;
    for (int y = 0; y < 4; ++y)
    {
        int sy = by * 4 + y;
        for (int x = 0; x < 4; ++x)
        {
            int sx = bx * 4 + x;
            px[idx++] = src[sy * width + sx];
        }
    }

    // Endpoints: the actual block texels with min/max luma - keeps
    // endpoints real observed colors (avoids the hue distortion a
    // per-channel-independent min/max bounding box can introduce), cheap
    // (single O(16) pass), and correct for flat blocks (min==max texel is
    // fine - every index will simply resolve to the same reconstructed
    // color either way).
    //
    // S24 (2026-09-09, offline round-trip test fix): was luminance min/max
    // (R/G/B-weighted only) - degenerated to a single point (both endpoints
    // = the SAME texel) for any block that varies only in alpha, or in a
    // channel combination luminance happens to weight near zero, silently
    // losing that variation entirely (confirmed live: a block with constant
    // RGB and a hard 0/255 alpha split reconstructed with 100% alpha error,
    // every texel, before this fix). Max-RGBA-distance instead - O(16*15/2)
    // pairwise check, still trivial - captures whichever channel(s) actually
    // vary, alpha included, and can't degenerate the same way (the two
    // farthest-apart texels are only equal if the whole block is a single
    // flat color, which is already the harmless/correct case).
    int minIdx = 0, maxIdx = 1, bestDist = -1;
    for (int i = 0; i < 16; ++i)
    {
        for (int j = i + 1; j < 16; ++j)
        {
            int dr = (int)px[i].x - (int)px[j].x;
            int dg = (int)px[i].y - (int)px[j].y;
            int db = (int)px[i].z - (int)px[j].z;
            int da = (int)px[i].w - (int)px[j].w;
            int dist = dr * dr + dg * dg + db * db + da * da;
            if (dist > bestDist) { bestDist = dist; minIdx = i; maxIdx = j; }
        }
    }

    uchar e0c[4] = { px[minIdx].x, px[minIdx].y, px[minIdx].z, px[minIdx].w };
    uchar e1c[4] = { px[maxIdx].x, px[maxIdx].y, px[maxIdx].z, px[maxIdx].w };

    // Shared p-bit selection: mode 6 has exactly ONE p-bit per endpoint,
    // applied to all 4 of that endpoint's channels - try both candidate
    // p-bit values and keep whichever gives lower total squared error
    // across R/G/B/A for that endpoint.
    int e0_7[4], e1_7[4];
    int p0 = 0, p1 = 0;
    {
        int bestErr = 0x7fffffff;
        for (int p = 0; p < 2; ++p)
        {
            int err = 0;
            for (int c = 0; c < 4; ++c)
            {
                int top7 = clamp((int)((e0c[c] - p + 1) / 2), 0, 127);
                int recon = top7 * 2 + p;
                int d = recon - (int)e0c[c];
                err += d * d;
            }
            if (err < bestErr) { bestErr = err; p0 = p; }
        }
        for (int c = 0; c < 4; ++c)
        {
            e0_7[c] = clamp((int)((e0c[c] - p0 + 1) / 2), 0, 127);
        }
    }
    {
        int bestErr = 0x7fffffff;
        for (int p = 0; p < 2; ++p)
        {
            int err = 0;
            for (int c = 0; c < 4; ++c)
            {
                int top7 = clamp((int)((e1c[c] - p + 1) / 2), 0, 127);
                int recon = top7 * 2 + p;
                int d = recon - (int)e1c[c];
                err += d * d;
            }
            if (err < bestErr) { bestErr = err; p1 = p; }
        }
        for (int c = 0; c < 4; ++c)
        {
            e1_7[c] = clamp((int)((e1c[c] - p1 + 1) / 2), 0, 127);
        }
    }

    // Reconstructed 8-bit endpoints - what the real decoder will see.
    // Index selection below MUST use these (not the original texel
    // colors), so the encoder's "closest index" choice matches actual
    // hardware-decoded output.
    int rE0[4], rE1[4];
    for (int c = 0; c < 4; ++c)
    {
        rE0[c] = e0_7[c] * 2 + p0;
        rE1[c] = e1_7[c] * 2 + p1;
    }

    // BC7's official 4-bit index interpolation weight table (aWeight4/64).
    const int W[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

    // Every texel (including the anchor, texel 0) gets its TRUE globally
    // optimal index here from the full 0-15 range - the anchor's 3-bit
    // storage limit is satisfied below by conditionally swapping which
    // endpoint is "0" vs "1", not by restricting this search.
    //
    // S24 (2026-09-09, offline round-trip test fix): this search used to be
    // restricted to k<8 for i==0 ("a small, always-correct way to guarantee
    // bitstream validity" - it IS always bitstream-valid, but the quality
    // reasoning was wrong). Confirmed live: when texel 0's true best index
    // is e.g. 15 (i.e., texel 0 IS very close to endpoint 1), restricting
    // the search to [0,7] doesn't land it "slightly worse than optimal" -
    // it lands near the MIDDLE of the endpoint range instead of at the
    // correct endpoint (W[7]=30 out of 64, roughly halfway), a large,
    // clearly visible error, not a negligible one. Real-world impact: any
    // block containing a hard two-tone edge where texel 0 happens to be the
    // "far" color reconstructed as a blended color instead - not a rare
    // case.
    int indices[16];
    for (int i = 0; i < 16; ++i)
    {
        uchar texel[4] = { px[i].x, px[i].y, px[i].z, px[i].w };
        int bestIdx = 0;
        int bestErr = 0x7fffffff;
        for (int k = 0; k < 16; ++k)
        {
            int w = W[k];
            int err = 0;
            for (int c = 0; c < 4; ++c)
            {
                int interp = (rE0[c] * (64 - w) + rE1[c] * w + 32) / 64;
                int d = interp - (int)texel[c];
                err += d * d;
            }
            if (err < bestErr) { bestErr = err; bestIdx = k; }
        }
        indices[i] = bestIdx;
    }

    // Anchor-index bit-saving rule: texel 0's stored index is always 3 bits
    // (top bit implicitly 0). If its true optimal index is >= 8, swap which
    // endpoint is "0"/"1" instead of degrading texel 0's match - W[]'s
    // symmetry (W[15-k] == 64-W[k], true for the table above) means every
    // other texel's index just mirrors via `15-index` and stays exactly as
    // optimal as it already was, no re-search needed.
    if (indices[0] >= 8)
    {
        int tmp;
        tmp = rE0[0]; rE0[0] = rE1[0]; rE1[0] = tmp;
        tmp = rE0[1]; rE0[1] = rE1[1]; rE1[1] = tmp;
        tmp = rE0[2]; rE0[2] = rE1[2]; rE1[2] = tmp;
        tmp = rE0[3]; rE0[3] = rE1[3]; rE1[3] = tmp;
        tmp = e0_7[0]; e0_7[0] = e1_7[0]; e1_7[0] = tmp;
        tmp = e0_7[1]; e0_7[1] = e1_7[1]; e1_7[1] = tmp;
        tmp = e0_7[2]; e0_7[2] = e1_7[2]; e1_7[2] = tmp;
        tmp = e0_7[3]; e0_7[3] = e1_7[3]; e1_7[3] = tmp;
        tmp = p0; p0 = p1; p1 = tmp;
        for (int i = 0; i < 16; ++i) indices[i] = 15 - indices[i];
    }

    // Bit-pack the 128-bit block as two 64-bit words - see this file's
    // header comment for the exact field layout/bit ranges. Every field
    // lands entirely within lo or entirely within hi (verified: mode(7)+
    // endpoints(56)+P0(1)=64 bits exactly fill lo; P1(1)+indices(63)=64
    // bits exactly fill hi), so no field ever straddles the boundary.
    ulong lo = 0;
    int bitpos = 0;
    lo |= ((ulong)0x40) << bitpos; bitpos += 7;              // mode 6
    lo |= ((ulong)e0_7[0]) << bitpos; bitpos += 7;           // R0
    lo |= ((ulong)e1_7[0]) << bitpos; bitpos += 7;           // R1
    lo |= ((ulong)e0_7[1]) << bitpos; bitpos += 7;           // G0
    lo |= ((ulong)e1_7[1]) << bitpos; bitpos += 7;           // G1
    lo |= ((ulong)e0_7[2]) << bitpos; bitpos += 7;           // B0
    lo |= ((ulong)e1_7[2]) << bitpos; bitpos += 7;           // B1
    lo |= ((ulong)e0_7[3]) << bitpos; bitpos += 7;           // A0
    lo |= ((ulong)e1_7[3]) << bitpos; bitpos += 7;           // A1
    lo |= ((ulong)p0) << bitpos; bitpos += 1;                // P0 (lands at bit 63)

    ulong hi = 0;
    int ipos = 0;
    hi |= ((ulong)p1) << ipos; ipos += 1;                    // P1 (bit 64 overall)
    hi |= ((ulong)(indices[0] & 0x7)) << ipos; ipos += 3;     // anchor: 3 bits
    for (int i = 1; i < 16; ++i)
    {
        hi |= ((ulong)(indices[i] & 0xF)) << ipos; ipos += 4;
    }

    int blockIdx = by * blocksX + bx;
    __global uchar* out = dst + blockIdx * 16;
    for (int b = 0; b < 8; ++b) out[b]     = (uchar)((lo >> (8 * b)) & 0xFF);
    for (int b = 0; b < 8; ++b) out[8 + b] = (uchar)((hi >> (8 * b)) & 0xFF);
}
)CLC";

namespace
{
    // Lazily built, once, on whichever thread first calls encodeMip() -
    // deliberately NOT routed through KVOpenCL::getKernel()/kernelCache
    // (see this class's header comment for why: that cache is unguarded
    // against the main thread's own VFX use racing a background caller -
    // giving this encoder its own dedicated kernel object sidesteps THAT
    // hazard, rather than requiring a redesign of getKernel() for every
    // caller). It does NOT, by itself, make concurrent USE of this
    // dedicated kernel safe - see sKernelMutex below for the real fix for
    // that (a distinct hazard: this class's OWN background thread pool has
    // more than one worker).
    std::once_flag sKernelOnce;
    cl_kernel sKernel = nullptr;
    bool sKernelOk = false;

    // S24 (2026-09-09, task #318 CTD-adjacent fix): real, confirmed bug -
    // DXBC7UploadManager's thread pool runs 2 worker threads, and both can
    // call encodeMip() concurrently for two different textures. clSetKernelArg()
    // is NOT thread-safe for concurrent calls on the SAME cl_kernel object
    // (OpenCL spec) - without this lock, one thread's args could be
    // overwritten by the other's between its own clSetKernelArg() calls and
    // its clEnqueueNDRangeKernel() call, causing a dispatch to run against
    // the WRONG thread's buffers. The victim thread's own (freshly
    // allocated, never actually written) output buffer then reads back as
    // whatever unwritten/zeroed memory the driver gave it - live-confirmed
    // by the user as "random primitives fade in and out of black" (angle1/
    // angle2.png, a texture-resolution test rig - the affected cubes
    // differed frame to frame, exactly matching a scheduling-dependent
    // race, not a deterministic encoding bug). Guards the whole set-args-
    // then-enqueue sequence in encodeMip() as one atomic unit; buffer
    // creation/write/read use per-call-local cl_mem objects and don't need
    // it (OpenCL command-queue enqueue calls themselves ARE safe from
    // multiple threads, per spec - only the shared kernel object's mutable
    // argument state was the problem).
    std::mutex sKernelMutex;
}

bool DXBC7Compressor::ensureKernel()
{
    if (!gCL.ensureInit())
    {
        return false;
    }

    std::call_once(sKernelOnce, []()
    {
        cl_context ctx = gCL.getContext();
        cl_device_id dev = gCL.getDevice();
        if (!ctx || !dev)
        {
            return;
        }

        cl_int err = CL_SUCCESS;
        const char* src = kBC7Mode6KernelSource;
        size_t len = std::strlen(src);
        cl_program program = clCreateProgramWithSource(ctx, 1, &src, &len, &err);
        if (!program || err != CL_SUCCESS)
        {
            return;
        }

        err = clBuildProgram(program, 1, &dev, nullptr, nullptr, nullptr);
        if (err != CL_SUCCESS)
        {
            char log[4096] = {};
            clGetProgramBuildInfo(program, dev, CL_PROGRAM_BUILD_LOG, sizeof(log), log, nullptr);
            LL_WARNS("DXBC7Compressor") << "BC7 kernel build failed:\n" << log << LL_ENDL;
            clReleaseProgram(program);
            return;
        }

        cl_kernel kernel = clCreateKernel(program, "bc7_encode_mode6", &err);
        clReleaseProgram(program);
        if (!kernel || err != CL_SUCCESS)
        {
            return;
        }

        sKernel = kernel;
        sKernelOk = true;
    });

    return sKernelOk;
}

DXBC7Compressor::CompressedMip DXBC7Compressor::encodeMip(const uint8_t* rgba8, int width, int height)
{
    CompressedMip result;
    result.width = width;
    result.height = height;

    if (!rgba8 || width <= 0 || height <= 0)
    {
        return result;
    }

    if (!ensureKernel())
    {
        return result;
    }

    const int blockWidth = ((width + 3) / 4) * 4;
    const int blockHeight = ((height + 3) / 4) * 4;
    result.blockWidth = blockWidth;
    result.blockHeight = blockHeight;

    // Edge-clamp pad up to a whole 4x4 block grid - BC7 has no concept of a
    // partial block, and this project's design choice (per plan) is to pad
    // rather than skip compression for non-block-aligned textures (common
    // for UI art) so they still benefit.
    std::vector<uint8_t> padded;
    const uint8_t* srcForGpu = rgba8;
    if (blockWidth != width || blockHeight != height)
    {
        padded.resize((size_t)blockWidth * blockHeight * 4);
        for (int y = 0; y < blockHeight; ++y)
        {
            int sy = (y < height) ? y : (height - 1);
            for (int x = 0; x < blockWidth; ++x)
            {
                int sx = (x < width) ? x : (width - 1);
                const uint8_t* s = rgba8 + ((size_t)sy * width + sx) * 4;
                uint8_t* d = padded.data() + ((size_t)y * blockWidth + x) * 4;
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
            }
        }
        srcForGpu = padded.data();
    }

    cl_context ctx = gCL.getContext();
    cl_command_queue cq = gCL.getComputeQueue();
    if (!ctx || !cq)
    {
        return result;
    }

    const size_t srcBytes = (size_t)blockWidth * blockHeight * 4;
    const int blocksX = blockWidth / 4;
    const int blocksY = blockHeight / 4;
    const size_t dstBytes = (size_t)blocksX * blocksY * 16;

    cl_int err = CL_SUCCESS;
    cl_mem srcBuf = clCreateBuffer(ctx, CL_MEM_READ_ONLY, srcBytes, nullptr, &err);
    if (!srcBuf || err != CL_SUCCESS)
    {
        return result;
    }
    cl_mem dstBuf = clCreateBuffer(ctx, CL_MEM_WRITE_ONLY, dstBytes, nullptr, &err);
    if (!dstBuf || err != CL_SUCCESS)
    {
        clReleaseMemObject(srcBuf);
        return result;
    }

    bool ok = clEnqueueWriteBuffer(cq, srcBuf, CL_TRUE, 0, srcBytes, srcForGpu, 0, nullptr, nullptr) == CL_SUCCESS;

    if (ok)
    {
        // S24 (2026-09-09, task #318 CTD-adjacent fix): see sKernelMutex's
        // own comment - must cover clSetKernelArg() through
        // clEnqueueNDRangeKernel() as one atomic unit, since this class's
        // background thread pool has more than one worker and all of them
        // share this single dedicated cl_kernel object.
        std::lock_guard<std::mutex> lock(sKernelMutex);

        err = CL_SUCCESS;
        err |= clSetKernelArg(sKernel, 0, sizeof(cl_mem), &srcBuf);
        err |= clSetKernelArg(sKernel, 1, sizeof(cl_mem), &dstBuf);
        err |= clSetKernelArg(sKernel, 2, sizeof(int), &blockWidth);
        err |= clSetKernelArg(sKernel, 3, sizeof(int), &blockHeight);
        ok = (err == CL_SUCCESS);

        if (ok)
        {
            size_t global[2] = { (size_t)blocksX, (size_t)blocksY };
            ok = clEnqueueNDRangeKernel(cq, sKernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr) == CL_SUCCESS;
        }
    }

    if (ok)
    {
        result.bytes.resize(dstBytes);
        ok = clEnqueueReadBuffer(cq, dstBuf, CL_TRUE, 0, dstBytes, result.bytes.data(), 0, nullptr, nullptr) == CL_SUCCESS;
        if (!ok)
        {
            result.bytes.clear();
        }
    }

    clReleaseMemObject(srcBuf);
    clReleaseMemObject(dstBuf);

    return result;
}
