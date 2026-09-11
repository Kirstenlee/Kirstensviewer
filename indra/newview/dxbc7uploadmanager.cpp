#include "llviewerprecompiledheaders.h"

#include "dxbc7uploadmanager.h"
#include "dxbc7compressor.h"
#include "llimagegl.h"
#include "llimage.h"
#include "llthreadsafequeue.h"
#include "threadpool.h"
#include "llviewercontrol.h"
#include "llerror.h"

#include <algorithm>
#include <memory>

namespace
{
    // One finished background job's worth of work, ready to apply on the
    // main thread. Owns all its compressed mip bytes directly (mipBytes[i]
    // backs mips[i].data, built fresh in DXBC7UploadManager::update() right
    // before the LLImageGL call - see there).
    struct UpgradeResult
    {
        LLPointer<LLImageGL> tex;
        U32 generation = 0;
        std::vector<std::vector<uint8_t>> mipBytes;
        std::vector<int> mipWidths;
        std::vector<int> mipHeights;
    };

    // Small, dedicated pool - this is a background VRAM-optimization task,
    // not latency-critical, so it should never meaningfully compete with
    // LLImageDecodeThread's own much larger pool for CPU time.
    LL::ThreadPool& getPool()
    {
        static LL::ThreadPool sPool("BC7Compress", 2);
        static bool sStarted = false;
        if (!sStarted)
        {
            sPool.start();
            sStarted = true;
        }
        return sPool;
    }

    // Bounded capacity - a pure optimization feature must never be allowed
    // to grow memory unboundedly if the main thread falls behind draining
    // it; a full queue just means a handful of finished jobs get dropped
    // (the texture simply stays uncompressed, no correctness impact).
    LLThreadSafeQueue<UpgradeResult>& getResultsQueue()
    {
        static LLThreadSafeQueue<UpgradeResult> sQueue(256);
        return sQueue;
    }
}

void DXBC7UploadManager::requestUpgrade(const LLPointer<LLImageGL>& tex, const uint8_t* rgba8, int width, int height)
{
    if (!tex || !rgba8 || width <= 0 || height <= 0)
    {
        return;
    }

    // S24 (task #318): reuses the SAME settings key the legacy GL-era
    // "Enable Texture Compression" checkbox used (RenderCompressTextures) -
    // that old system is dead under DX_RENDER (see project memory), this is
    // its real replacement. Read directly here rather than caching via
    // LLCachedControl - this call site fires at most once per texture
    // upload, not a hot per-frame path.
    if (!gSavedSettings.getBOOL("RenderCompressTextures"))
    {
        return;
    }

    if (!tex->getAllowCompression())
    {
        return;
    }

    // Padding a texture smaller than this up to a whole 4x4 BC7 block grid
    // can cost as much or more memory than it saves (worst case: a 1x1
    // texture pads to a single 16-byte block vs. 4 bytes uncompressed) -
    // not worth the background-thread/GPU-compute overhead either.
    static const int kMinCompressDim = 8;
    if (width < kMinCompressDim || height < kMinCompressDim)
    {
        return;
    }

    const U32 generation = tex->getDXUploadGeneration();
    std::vector<uint8_t> pixels(rgba8, rgba8 + (size_t)width * height * 4);

    bool posted = getPool().getQueue().post(
        [tex, generation, pixels = std::move(pixels), width, height]() mutable
        {
            UpgradeResult result;
            result.tex = tex;
            result.generation = generation;

            int w = width, h = height;
            const uint8_t* curPtr = pixels.data();
            std::vector<uint8_t> curBuf; // owns curPtr's data once past mip 0

            for (int mipIndex = 0; mipIndex < 17; ++mipIndex)
            {
                DXBC7Compressor::CompressedMip cm = DXBC7Compressor::encodeMip(curPtr, w, h);
                if (cm.bytes.empty())
                {
                    // Compression failed (no usable OpenCL device, kernel
                    // build failure, etc.) - abandon the whole job. The
                    // texture already displayed uncompressed at upload time
                    // and stays that way; nothing to apply, nothing to warn
                    // about (see this class's header comment).
                    return;
                }
                result.mipBytes.push_back(std::move(cm.bytes));
                result.mipWidths.push_back(w);
                result.mipHeights.push_back(h);

                // Matches LLImageDXT's own block-compressed mip floor
                // (checkMinWidthHeight(), llimagedxt.cpp) - stop at a 4x4
                // mip, the smallest a BC format can represent natively.
                if (w <= 4 && h <= 4)
                {
                    break;
                }

                const int nw = std::max(4, w / 2);
                const int nh = std::max(4, h / 2);
                std::vector<uint8_t> nextBuf((size_t)nw * nh * 4);

                if (w == nw * 2 && h == nh * 2)
                {
                    // Exact 2x relationship - LLImageBase::generateMip()'s
                    // real operating range (same box filter
                    // LLImageDXT::encodeDXT() already uses for this exact
                    // purpose, llimage/llimage.cpp).
                    LLImageBase::generateMip(curPtr, nextBuf.data(), nw, nh, 4);
                }
                else
                {
                    // Non power-of-two source (uncommon for world/avatar
                    // assets, possible for UI art) - generateMip() isn't
                    // safe here (it assumes an exact 2x source and reads
                    // past the buffer otherwise). Plain nearest-neighbor
                    // resize instead - lower quality than a real box
                    // filter, but this is already a rare path and a mip
                    // level's quality matters far less than level 0's.
                    for (int y = 0; y < nh; ++y)
                    {
                        int sy = std::min(h - 1, y * h / nh);
                        for (int x = 0; x < nw; ++x)
                        {
                            int sx = std::min(w - 1, x * w / nw);
                            const uint8_t* s = curPtr + ((size_t)sy * w + sx) * 4;
                            uint8_t* d = nextBuf.data() + ((size_t)y * nw + x) * 4;
                            d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
                        }
                    }
                }

                curBuf = std::move(nextBuf);
                curPtr = curBuf.data();
                w = nw; h = nh;
            }

            // Non-blocking - if the main thread has fallen far enough
            // behind that the results queue is full, just drop this result
            // (see getResultsQueue()'s own comment).
            getResultsQueue().tryPush(std::move(result));
        });

    if (!posted)
    {
        LL_DEBUGS("Texture") << "DXBC7UploadManager: BC7 compression pool closed, skipping" << LL_ENDL;
    }
}

void DXBC7UploadManager::update()
{
    UpgradeResult result;
    // Bound the per-frame drain so a burst of finished jobs can't cause a
    // single-frame stall - remaining results simply get picked up next
    // frame, same "amortize over multiple frames" idea as
    // LLViewerTextureList::updateImagesCreateTextures()'s own max_time
    // budget.
    for (int i = 0; i < 8; ++i)
    {
        if (!getResultsQueue().tryPop(result))
        {
            break;
        }

        if (!result.tex)
        {
            continue;
        }

        std::vector<DXCompressedMipData> mips(result.mipBytes.size());
        for (size_t m = 0; m < mips.size(); ++m)
        {
            mips[m].data = result.mipBytes[m].data();
            mips[m].width = result.mipWidths[m];
            mips[m].height = result.mipHeights[m];
        }

        // upgradeToCompressedMips() itself checks the generation again and
        // silently no-ops if stale (a newer real upload superseded this
        // pixel data while the job was in flight) - not treated as an
        // error, see its own comment.
        result.tex->upgradeToCompressedMips(mips, DXGI_FORMAT_BC7_UNORM, result.generation);
    }
}
