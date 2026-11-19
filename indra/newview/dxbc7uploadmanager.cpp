#include "llviewerprecompiledheaders.h"

#include "dxbc7uploadmanager.h"
#include "dxbc7compressor.h"
#include "llimagedx.h"
#include "llimage.h"
#include "DXTexture.h"
#include "llthreadsafequeue.h"
#include "threadpool.h"
#include "llviewercontrol.h"
#include "llerror.h"
#include "llthread.h"

#include <algorithm>
#include <memory>
#include <unordered_map>

namespace
{
    // One finished background job's worth of work, ready to apply on the
    // main thread. Owns all its compressed mip bytes directly (mipBytes[i]
    // backs mips[i].data, built fresh in DXBC7UploadManager::update() right
    // before the LLImageDX call - see there). An empty mipBytes means the
    // job was abandoned on the worker; update() just releases its entry.
    struct UpgradeResult
    {
        U64 jobId = 0; // S24 : key into getPending() - workers never see an LLImageDX pointer
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

    // S24 : MAIN THREAD ONLY (requestUpgrade() and update() both run there).
    // Maps a job id to the texture it was posted for, so the BC7Compress
    // workers only ever see an integer and plain vectors: an LLPointer copied
    // or destroyed on a worker races the main thread's ref()/unref() and can
    // leave the worker owning the last reference. Leaked on purpose so it is
    // never torn down in static-destruction order at exit.
    std::unordered_map<U64, LLPointer<LLImageDX>>& getPending()
    {
        static auto* sPending = new std::unordered_map<U64, LLPointer<LLImageDX>>();
        return *sPending;
    }
    U64 sNextJobId = 0;

    // Releases entries nothing else owns any more (the texture was dropped
    // while its job was in flight, or the result was lost to a full queue).
    // A late result for a released id is ignored by update().
    void prunePending()
    {
        auto& pending = getPending();
        std::unordered_map<LLImageDX*, S32> held;
        for (const auto& kv : pending)
        {
            ++held[kv.second.get()];
        }
        for (auto it = pending.begin(); it != pending.end();)
        {
            if (it->second->getNumRefs() <= held[it->second.get()])
            {
                it = pending.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}

bool DXBC7UploadManager::wouldAccept(const LLPointer<LLImageDX>& tex, int width, int height)
{
    if (!tex || width <= 0 || height <= 0)
    {
        return false;
    }

    // S24: reuses the legacy GL-era "Enable Texture Compression" settings
    // key (RenderCompressTextures); that old system is dead under
    // DX_RENDER, this is its replacement. Read directly rather than via
    // LLCachedControl - fires at most once per texture upload.
    if (!gSavedSettings.getBOOL("RenderCompressTextures"))
    {
        return false;
    }

    if (!tex->getAllowCompression())
    {
        return false;
    }

    // Padding a texture smaller than this up to a whole 4x4 BC7 block grid
    // can cost as much or more memory than it saves (worst case: a 1x1
    // texture pads to a single 16-byte block vs. 4 bytes uncompressed) -
    // not worth the background-thread/GPU-compute overhead either.
    static const int kMinCompressDim = 8;
    if (width < kMinCompressDim || height < kMinCompressDim)
    {
        return false;
    }

    return true;
}

void DXBC7UploadManager::requestUpgrade(const LLPointer<LLImageDX>& tex, std::vector<uint8_t>&& rgba8, int width, int height)
{
    if (rgba8.size() != (size_t)width * height * 4 || !wouldAccept(tex, width, height))
    {
        return;
    }

    startUpgradeJob(tex, std::move(rgba8), 4, width, height);
}

void DXBC7UploadManager::requestUpgradeFromRaw(const LLPointer<LLImageDX>& tex, std::vector<uint8_t>&& raw, int components, int width, int height)
{
    if (components < 1 || components > 4 || raw.size() != (size_t)width * height * components || !wouldAccept(tex, width, height))
    {
        return;
    }

    startUpgradeJob(tex, std::move(raw), components, width, height);
}

void DXBC7UploadManager::startUpgradeJob(const LLPointer<LLImageDX>& tex, std::vector<uint8_t>&& source, int components, int width, int height)
{
    llassert(on_main_thread());

    const U32 generation = tex->getDXUploadGeneration();

    const U64 jobId = ++sNextJobId;
    getPending()[jobId] = tex;

    bool posted = getPool().getQueue().post(
        [jobId, generation, source = std::move(source), components, width, height]() mutable
        {
            UpgradeResult result;
            result.jobId = jobId;
            result.generation = generation;

            // S24 : RGBA8 pixels for the compressor. Source that is already RGBA is used as it is; anything
            // else is repacked here on the worker, not on the thread that asked for the upgrade.
            std::vector<uint8_t> pixels;
            if (components == 4)
            {
                pixels = std::move(source);
            }
            else if (!DXTexture::repackToRGBA8(source.data(), width, height, components, pixels))
            {
                // nothing to compress: the empty result lets update() release the pending entry
                getResultsQueue().tryPush(std::move(result));
                return;
            }
            source = std::vector<uint8_t>();

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
                    // about (see this class's header comment). The empty
                    // result lets update() release the pending entry.
                    result.mipBytes.clear();
                    result.mipWidths.clear();
                    result.mipHeights.clear();
                    getResultsQueue().tryPush(std::move(result));
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
        getPending().erase(jobId);
        LL_DEBUGS("Texture") << "DXBC7UploadManager: BC7 compression pool closed, skipping" << LL_ENDL;
    }
}

void DXBC7UploadManager::update()
{
    llassert(on_main_thread());

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

        auto pending = getPending().find(result.jobId);
        if (pending == getPending().end())
        {
            continue; // released by prunePending(): nothing else owns the texture
        }
        LLPointer<LLImageDX> tex = std::move(pending->second);
        getPending().erase(pending);

        if (result.mipBytes.empty())
        {
            continue; // abandoned on the worker
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
        tex->upgradeToCompressedMips(mips, DXGI_FORMAT_BC7_UNORM, result.generation);
    }

    // Every 30 frames, release entries whose job result was lost.
    static U32 sFrame = 0;
    if ((++sFrame % 30) == 0)
    {
        prunePending();
    }
}
