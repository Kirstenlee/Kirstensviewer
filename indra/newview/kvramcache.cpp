/**
 * @file kvramcache.cpp
 * @brief RAM-based fast cache for J2C compressed texture data
 *
 * Acts as an intermediate tier between memory and disk cache:
 * - Stores J2C compressed data (same format as disk cache)
 * - Populated when textures are written to disk cache
 * - Checked before disk cache reads to avoid I/O
 * - Naturally decays: old entries drop, disk cache retains data
 *
 * $LicenseInfo:firstyear=2024&license=viewerlgpl$
 * Kirstens S24 Viewer Source Code
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"
#include "kvramcache.h"
#include "kvcachethread.h"
#include "llgl.h"
#include "llviewercontrol.h"
#include "lltexturecache.h"
#include "llappviewer.h"
#include "llimagej2c.h"
#include "llimage.h"
#include "AresWin32.h"
#include "workqueue.h"
#include "lldir.h"
#include "llwindow.h"
#include "llcrc.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_set>
#include <fstream>
#include <cstddef>

namespace
{
    // S24 : bytes handed to the texture cache for writing whose write has not finished yet. Evicted entries
    // are held until the texture cache reports the write done, and that is only processed as fast as the
    // main thread services the texture cache, so this is also memory held outside the RAM cache budget.
    std::atomic<U64> sInFlightDiskWriteBytes{ 0 };
    constexpr U64 MAX_IN_FLIGHT_DISK_WRITE_BYTES = 192ull * 1024 * 1024;

    // S24 : disk write budget for evicted entries (token bucket). The texture cache runs reads and writes
    // on one worker, so a long run of evictions that all went to disk starved the reads that texturing
    // needs. Sustained rate and burst are bounded; entries over the budget are freed without a disk write
    // (they are only a cache and are fetched again if needed).
    constexpr F64 DISK_WRITE_BYTES_PER_SEC = 24.0 * 1024 * 1024;
    constexpr F64 DISK_WRITE_BURST_BYTES = 48.0 * 1024 * 1024;

    // LLTextureCacheWorker stores the raw pointer, not a copy, so this buffer
    // must survive until the async disk write completes. Responder is
    // ref-counted via LLPointer (see LLTextureCacheWorker::mResponder), so it
    // self-destructs once the write completes; frees the buffer exactly once.
    class KVRAMDiskWriteResponder : public LLTextureCache::WriteResponder
    {
    public:
        KVRAMDiskWriteResponder(U8* data, U64 size) : mData(data), mSize(size) { sInFlightDiskWriteBytes += mSize; }
        ~KVRAMDiskWriteResponder() override { free(mData); sInFlightDiskWriteBytes -= mSize; }
        void completed(bool success) override { /* nothing else to do - see destructor */ }
    private:
        U8* mData;
        U64 mSize;
    };

    // S24 disk-snapshot format for KVRAMCache's own startup hot-load / shutdown
    // flush - entirely independent of LLTextureCache's on-disk cache (which
    // this never touches). Raw, still-encoded J2C bytes only: no decode on
    // write, no decode on read, no LLTextureCache write-path involvement -
    // sidesteps the mRawImage handoff race in LLTextureCacheRemoteWorker::
    // doWrite() entirely rather than chasing it. Theoretical max file size is
    // bounded by KVRAMCacheRAMBudgetMB's own 4096MB ceiling.
    constexpr char KV_SNAPSHOT_MAGIC[4] = { 'K', 'V', 'R', 'C' };
    constexpr U32 KV_SNAPSHOT_VERSION = 1;

#if LL_WINDOWS
#pragma pack(push, 1)
#endif
    struct KVSnapshotHeader
    {
        char magic[4];
        U32 version;
        U32 entry_count;
    };
    struct KVSnapshotEntryHeader
    {
        U8 uuid[16];
        U64 size_bytes;
        S32 discard_level;
        U32 width;
        U32 height;
        U8 components;
        U32 crc32; // LLCRC over the size_bytes of raw J2C data that follows
    };
#if LL_WINDOWS
#pragma pack(pop)
#endif

    // Generous ceiling for one entry's data size - rejects a corrupted
    // size_bytes field (e.g. a bit-flipped huge value) before it ever reaches
    // a std::vector<U8> allocation. Real J2C textures are nowhere near this;
    // it exists purely to stop garbage from causing a huge/failed alloc.
    constexpr U64 KV_SNAPSHOT_MAX_ENTRY_BYTES = 64ULL * 1024 * 1024;

    std::string kv_disk_snapshot_path()
    {
        return gDirUtilp->getExpandedFilename(LL_PATH_CACHE, "kvramcache.bin");
    }
}

// Singleton implementation
KVRAMCache::KVRAMCache()
    : mInitialized(false)
    , mTimeSinceLastEvictionCheck(0.0f)  // KV:GC→RT Renamed from mTimeSinceLastGraceCheck
    , mWorkerThread(nullptr)
{
    LL_WARNS("KVRAMCache") << "KVRAMCache instance created" << LL_ENDL;
}

KVRAMCache::~KVRAMCache()
{
    shutdown();
    stopThread();
}

// ============================================================================
// Initialization and Configuration
// ============================================================================

void KVRAMCache::initialize(const CacheConfig& config)
{
    LLMutexLock lock(&mCacheMutex);

    if (mInitialized)
    {
        LL_WARNS("KVRAMCache") << "Already initialized, shutting down first" << LL_ENDL;
        shutdown();
    }

    mConfig = config;

    // KV:RT Removed mLastBaselineEviction — retention time is per-entry now

    // Clear all data structures
    mGhostMap.clear();
    mRAMLRU.clear();
    mRAMLRU_BACKGROUND.clear();
    mRAMLRU_NORMAL.clear();
    mRAMLRU_HIGH.clear();
    mRAMLRU_CRITICAL.clear();

    // Reset statistics
    resetStats();

    mInitialized = true;

    LL_WARNS("KVRAMCache") << "Initialized with config:"
        << "\n  RAM Budget: " << BYTES_TO_MEGA_BYTES(mConfig.ram_budget_bytes) << " MB (range: 256-4096 MB)"
        << "\n  RAM Retention Time: " << mConfig.ram_retention_time_seconds << " seconds (range: 0.1-30s)"  // KV:GC→RT Renamed from ram_grace_period_seconds
        << "\n  Save/Pre-load Count: " << mMinDeckSize << " textures (0 = off, range: 0-15000)"
        << "\n  Baseline eviction uses viewer uptime (decoupled from frame rate)"
        << LL_ENDL;
}

void KVRAMCache::shutdown()
{
    LLMutexLock lock(&mCacheMutex);

    if (!mInitialized)
    {
        return;
    }

    LL_WARNS("KVRAMCache") << "Shutting down - Final stats:"
        << "\n  RAM: " << mStats.ram_entry_count << " entries, "
        << BYTES_TO_MEGA_BYTES(mStats.ram_used_bytes) << " MB"
        << LL_ENDL;

    // S24: clearAll() is deliberately NOT called here any more - it's deferred
    // to finishShutdownFlush(), called much later (right before
    // AresWin32::TerminateSelf()) so the shutdown flush background task has
    // the whole rest of the shutdown sequence to write entries to disk before
    // anything frees the RAM out from under it. See beginShutdownFlush().

    mInitialized = false;
}

// S24: kicks off the real shutdown-time flush - writes KVRAMCache's own
// dedicated disk snapshot (kv_disk_snapshot_path()) directly from mGhostMap,
// raw J2C bytes only, no decode, no LLTextureCache involvement at all. This
// deliberately bypasses evictSlice()/writeEvictedEntryToDisk() (the normal
// passive-eviction-to-LLTextureCache path, still used unchanged elsewhere)
// because that path requires a real pixel decode per texture and funnels
// through LLTextureCacheRemoteWorker::doWrite(), whose INIT-state check hits
// a pre-existing mRawImage handoff race at a 100% failure rate under this
// kind of back-to-back bulk write (confirmed live, 0/5721 succeeded two runs
// running) - see project_kvramcache_hotload_and_shutdown_flush memory. The
// snapshot format sidesteps all of that instead of fixing it.
//
// Idempotent - safe to call from both LLAppViewer::requestQuit() (as early as
// possible) and finishShutdownFlush() (as a fallback for quit paths, like
// forceQuit()/outOfMemorySoftQuit(), that skip requestQuit() entirely).
// KNOWN LIMITATION on that fallback path specifically: finishShutdownFlush()
// runs after gSavedSettings.cleanup() (llappviewer.cpp, "the control table is
// wiped... a control constructed after that point always gets its default
// value"), so if beginShutdownFlush() only ends up running via that late
// fallback, the gSavedSettings.getF32() read just below gets the default
// timeout, not the user's customized one. Acceptable degradation for an
// already-abnormal quit path - never a crash or data loss, just a possibly
// wrong timeout in a rare case.
void KVRAMCache::beginShutdownFlush()
{
    if (mShutdownFlushStarted.exchange(true))
    {
        return;
    }

    if (!mInitialized)
    {
        mShutdownFlushDone.store(true);
        return;
    }

    // S24 : a save count of 0 means no snapshot at all: the viewer behaves like a normal one next start. A snapshot
    // left over from an earlier session is removed so it cannot be loaded later.
    if (mMinDeckSize == 0)
    {
        std::error_code remove_ec;
        std::filesystem::remove(kv_disk_snapshot_path(), remove_ec);
        mShutdownFlushDone.store(true);
        return;
    }

    F32 timeout_sec = llclamp(gSavedSettings.getF32("KVRAMCacheShutdownFlushTimeoutSec"), 1.0f, 30.0f);
    mShutdownFlushDeadline = LLTimer::getElapsedSeconds() + timeout_sec;

    auto queue = LL::WorkQueue::getInstance("DXPool");
    if (!queue)
    {
        // DXPool unavailable (e.g. mid-shutdown already) - nothing we can safely
        // do off-thread; finishShutdownFlush()'s clearAll() is still the safety net.
        mShutdownFlushDone.store(true);
        return;
    }

    F64 deadline = mShutdownFlushDeadline;
    queue->post([this, deadline]()
    {
        // S24 : only the ids of RAM-resident entries are collected under the lock; each
        // entry's bytes are then copied one at a time, so the lock is never held for a
        // whole-cache copy and the deadline is checked between entries.
        struct Snap
        {
            std::vector<U8> data;
            S32 discard_level = -1;
            U32 width = 0;
            U32 height = 0;
            U8 components = 0;
        };
        std::vector<LLUUID> uuids;
        {
            LLMutexLock lock(&mCacheMutex);

            // S24 : save at most mMinDeckSize entries, best first: the highest priority tier, and within a tier
            // the newest (the back of each deque), so the cut-off (the count here, or the timeout while
            // writing) always falls on the least valuable entries, and the next start pre-loads the best ones.
            const size_t save_cap = mMinDeckSize;
            auto saveable = [this](const LLUUID& id) -> bool
            {
                auto it = mGhostMap.find(id);
                if (it == mGhostMap.end())
                {
                    return false;
                }
                const CacheEntry* entry = it->second.get();
                return entry->location == AssetLocation::RAM && entry->texture_data && entry->size_bytes > 0;
            };

            std::unordered_set<LLUUID> seen;
            uuids.reserve(std::min(save_cap, mGhostMap.size()));
            const std::deque<LLUUID>* tiers[5] = { &mRAMLRU_CRITICAL, &mRAMLRU_HIGH, &mRAMLRU_NORMAL, &mRAMLRU_BACKGROUND, &mRAMLRU };
            for (const std::deque<LLUUID>* tier : tiers)
            {
                for (auto rit = tier->rbegin(); rit != tier->rend() && uuids.size() < save_cap; ++rit)
                {
                    if (saveable(*rit) && seen.insert(*rit).second)
                    {
                        uuids.push_back(*rit);
                    }
                }
            }
            // safety net: an entry that is not in any priority list (it should not happen) still counts
            for (auto& pair : mGhostMap)
            {
                if (uuids.size() >= save_cap)
                {
                    break;
                }
                if (saveable(pair.first) && seen.insert(pair.first).second)
                {
                    uuids.push_back(pair.first);
                }
            }
        }

        U32 written = 0;
        std::ofstream ofs(kv_disk_snapshot_path(), std::ios::binary | std::ios::trunc);
        if (ofs.good())
        {
            KVSnapshotHeader header;
            memcpy(header.magic, KV_SNAPSHOT_MAGIC, 4);
            header.version = KV_SNAPSHOT_VERSION;
            header.entry_count = 0; // patched below once the real count is known
            ofs.write(reinterpret_cast<const char*>(&header), sizeof(header));

            LLTimer progress_timer;
            for (const LLUUID& uuid : uuids)
            {
                if (LLTimer::getElapsedSeconds() >= deadline)
                {
                    break;
                }

                Snap s;
                bool have = false;
                {
                    LLMutexLock lock(&mCacheMutex);
                    auto it = mGhostMap.find(uuid);
                    if (it != mGhostMap.end())
                    {
                        CacheEntry* entry = it->second.get();
                        if (entry->location == AssetLocation::RAM && entry->texture_data && entry->size_bytes > 0)
                        {
                            const U8* bytes = static_cast<const U8*>(entry->texture_data);
                            s.data.assign(bytes, bytes + entry->size_bytes);
                            s.discard_level = entry->discard_level;
                            s.width = entry->width;
                            s.height = entry->height;
                            s.components = entry->components;
                            have = true;
                        }
                    }
                }
                if (!have)
                {
                    continue; // evicted since the id list was taken
                }

                LLCRC crc;
                crc.update(s.data.data(), s.data.size());

                KVSnapshotEntryHeader eh;
                memcpy(eh.uuid, uuid.mData, 16);
                eh.size_bytes = (U64)s.data.size();
                eh.discard_level = s.discard_level;
                eh.width = s.width;
                eh.height = s.height;
                eh.components = s.components;
                eh.crc32 = crc.getCRC();

                ofs.write(reinterpret_cast<const char*>(&eh), sizeof(eh));
                ofs.write(reinterpret_cast<const char*>(s.data.data()), (std::streamsize)s.data.size());
                if (!ofs.good())
                {
                    break;
                }
                ++written;

                if (progress_timer.getElapsedTimeF32() > 0.2f)
                {
                    progress_timer.reset();

                    // S24: in-place, not a new scrolling console line per tick
                    // - UpdateAresDialog() would otherwise print a fresh line
                    // roughly every 200ms, scrolling the console for no reason
                    // during what can be a many-second flush. Kept short on
                    // purpose - the console doesn't wrap/resize for this.
                    static const char s_spinner[] = { '|', '/', '-', '\\' };
                    static U32 s_spinIdx = 0;
                    char spin = s_spinner[s_spinIdx++ % 4];
                    AresWin32::UpdateAresDialogInPlace(llformat(
                        "Saving RAMCache [%c] %u textures.", spin, written));
                }
            }

            // Patch the real entry count now that it's known - offsetof keeps
            // this correct regardless of KVSnapshotHeader's field order.
            ofs.seekp(offsetof(KVSnapshotHeader, entry_count));
            ofs.write(reinterpret_cast<const char*>(&written), sizeof(written));
        }

        if (!uuids.empty())
        {
            // Real scrolling line for the final result, so the console's
            // permanent scrollback shows a clean confirmation after the
            // in-place progress bar rather than just vanishing.
            AresWin32::UpdateAresDialog(llformat("  Flushed %u/%u textures to disk.", written, (U32)uuids.size()));
        }

        LL_WARNS("KVRAMCache") << "Shutdown flush: wrote " << written << "/" << uuids.size()
            << " textures to disk snapshot" << LL_ENDL;
        mShutdownFlushDone.store(true);
    });
}

// S24: the actual final step - call this as the LAST thing before
// AresWin32::TerminateSelf(), not from shutdown()'s own call site, so the flush
// gets the benefit of the whole shutdown sequence (including RunFinalSweep()'s
// own idle Sleep() windows) rather than just whatever's elapsed by the time
// shutdown() runs. Bounded by the same deadline beginShutdownFlush() computed -
// never waits longer than that, so a huge RAM cache can't turn logoff into an
// indefinite hang.
void KVRAMCache::finishShutdownFlush()
{
    if (!mShutdownFlushStarted.load())
    {
        beginShutdownFlush(); // forceQuit()-style path that skipped requestQuit()
    }

    while (!mShutdownFlushDone.load() && LLTimer::getElapsedSeconds() < mShutdownFlushDeadline)
    {
        ms_sleep(5);
    }

    clearAll();
}

// S24: loads the snapshot beginShutdownFlush()/finishShutdownFlush() wrote
// last session - plain sequential reads, no decode, no LLTextureCache
// involvement, no async worker/polling at all, unlike the first attempt at
// this feature. Call from LLAppViewer::init() between initCache() and
// initWindow(). No-ops immediately if disabled or no snapshot exists yet
// (first run, or last session never got far enough to flush one).
void KVRAMCache::loadFromDiskSnapshot()
{
    // S24 : the startup snapshot arrives all at once and is not real inflow
    {
        LLMutexLock lock(&mCacheMutex);
        mCountInflow = false;
    }
    struct RestoreInflowCounting
    {
        KVRAMCache* cache;
        ~RestoreInflowCounting()
        {
            LLMutexLock lock(&cache->mCacheMutex);
            cache->mCountInflow = true;
        }
    } restore_inflow_counting{ this };

    if (!gSavedSettings.getBOOL("KVRAMCacheEnabled") || !mInitialized)
    {
        return;
    }

    // S24 : a save count of 0 means no pre-load; remove a leftover snapshot so it is not carried around
    if (mMinDeckSize == 0)
    {
        std::error_code remove_ec;
        std::filesystem::remove(kv_disk_snapshot_path(), remove_ec);
        return;
    }

    std::ifstream ifs(kv_disk_snapshot_path(), std::ios::binary);
    if (!ifs.good())
    {
        return; // no snapshot yet - not an error
    }

    KVSnapshotHeader header;
    ifs.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!ifs.good() || memcmp(header.magic, KV_SNAPSHOT_MAGIC, sizeof(KV_SNAPSHOT_MAGIC)) != 0
        || header.version != KV_SNAPSHOT_VERSION)
    {
        LL_WARNS("KVRAMCache") << "loadFromDiskSnapshot: missing/corrupt header, skipping" << LL_ENDL;
        return;
    }

    if (header.entry_count > 0)
    {
        LLSplashScreen::update("Warming up RAM cache from last session...");
    }

    U32 max_count = getMinDeckSize();
    U32 loaded = 0;
    F32 timeout_sec = llclamp(gSavedSettings.getF32("KVRAMCacheHotLoadTimeoutSec"), 1.0f, 30.0f);
    F64 deadline = LLTimer::getElapsedSeconds() + timeout_sec;

    // S24 : entries are saved best first, so stop once the cache is as full as it is allowed to sit; loading
    // more would only have the eviction start throwing the same textures straight out again
    const F32 preload_limit = std::max(0.0f, mSoftThreshold - 0.10f);

    for (U32 i = 0; i < header.entry_count && loaded < max_count; ++i)
    {
        if (getRAMPressure() >= preload_limit)
        {
            LL_WARNS("KVRAMCache") << "loadFromDiskSnapshot: stopping at " << loaded << " textures, the cache is at its fill target" << LL_ENDL;
            break;
        }
        if (LLTimer::getElapsedSeconds() >= deadline)
        {
            break;
        }

        KVSnapshotEntryHeader eh;
        ifs.read(reinterpret_cast<char*>(&eh), sizeof(eh));
        if (!ifs.good())
        {
            break; // truncated/corrupt tail - stop, keep whatever already loaded
        }

        // A corrupted size_bytes (e.g. one bit flipped into a huge value) must
        // be caught BEFORE allocating or reading - and unlike the other checks
        // below, this one has to abort the whole remaining read (not just skip
        // this entry), since we can no longer know where the next entry's
        // header starts without trusting this size.
        if (eh.size_bytes == 0 || eh.size_bytes > KV_SNAPSHOT_MAX_ENTRY_BYTES)
        {
            LL_WARNS("KVRAMCache") << "loadFromDiskSnapshot: implausible size_bytes ("
                << eh.size_bytes << ") at entry " << i << " - stopping, file likely corrupt" << LL_ENDL;
            break;
        }

        std::vector<U8> data(eh.size_bytes);
        ifs.read(reinterpret_cast<char*>(data.data()), (std::streamsize)eh.size_bytes);
        if (!ifs.good())
        {
            break; // truncated mid-entry - stream position is now unreliable, stop here too
        }

        // Parity check: catches silent bit-level corruption within the data
        // itself that still happens to leave plausible-looking metadata behind.
        // Safe to just skip (not abort) past this one - the stream position
        // is still correctly aligned on the next entry's header either way,
        // since we already consumed exactly size_bytes bytes above.
        LLCRC crc;
        crc.update(data.data(), data.size());
        if (crc.getCRC() != eh.crc32)
        {
            LL_WARNS("KVRAMCache") << "loadFromDiskSnapshot: CRC mismatch at entry " << i
                << ", skipping (data corrupt)" << LL_ENDL;
            continue;
        }

        // Same sanity bounds acceptEviction() itself enforces - cheap early-out
        // before bothering to take the cache mutex for a doomed entry.
        if (eh.width == 0 || eh.height == 0 || eh.width > 8192 || eh.height > 8192
            || eh.components < 1 || eh.components > 4
            || eh.discard_level < 0 || eh.discard_level > 5)
        {
            continue;
        }

        LLUUID uuid;
        memcpy(uuid.mData, eh.uuid, 16);

        if (acceptEviction(uuid, data.data(), eh.size_bytes, eh.discard_level,
                            eh.width, eh.height, (S8)eh.components, AssetPriority::NORMAL))
        {
            ++loaded;
        }
    }

    LL_WARNS("KVRAMCache") << "loadFromDiskSnapshot: loaded " << loaded << "/" << header.entry_count
        << " textures from last session's snapshot" << LL_ENDL;
}

// ============================================================================
// Worker Thread Control
// ============================================================================

void KVRAMCache::startThread()
{
    if (!mWorkerThread)
    {
        mWorkerThread = new KVCacheThread();
        mWorkerThread->start();
        LL_WARNS("KVRAMCache") << "Worker thread started" << LL_ENDL;
    }
}

void KVRAMCache::stopThread()
{
    if (mWorkerThread)
    {
        mWorkerThread->shutdown();
        delete mWorkerThread;
        mWorkerThread = nullptr;
        LL_WARNS("KVRAMCache") << "Worker thread stopped" << LL_ENDL;
    }
}

// ============================================================================
// Settings Integration
// ============================================================================

void KVRAMCache::initializeFromSettings()
{
    if (!gSavedSettings.getBOOL("KVRAMCacheEnabled"))
    {
        LL_WARNS("KVRAMCache") << "KVRAM Cache disabled in settings" << LL_ENDL;
        return;
    }

    CacheConfig config;

    // RAM budget from settings (256-4096 MB in 256 MB steps)
    U32 ram_budget_mb = gSavedSettings.getU32("KVRAMCacheRAMBudgetMB");
    ram_budget_mb = llclamp(ram_budget_mb, 256U, 4096U);
    config.ram_budget_bytes = MEGA_BYTES_TO_BYTES(ram_budget_mb);

    // KV:GC→RT Minimum retention time (0.1-30 seconds in 0.1 second steps) — textures must be in cache this long before evictable
    F32 retention_time = gSavedSettings.getF32("KVRAMCacheRetentionTimeSec");
    config.ram_retention_time_seconds = llclamp(retention_time, 0.1f, 30.0f);

    // Thresholds
    mSoftThreshold = gSavedSettings.getF32("KVRAMCacheSoftThreshold");
    mHardThreshold = gSavedSettings.getF32("KVRAMCacheHardThreshold");
    clampThresholds();

    // Minimum deck size (100-15000 textures in 100 steps)
    U32 deck_size = static_cast<U32>(gSavedSettings.getF32("KVRAMCacheMinDeckSize"));
    mMinDeckSize = llclamp(deck_size, 0U, 15000U);
    mEvictionScheme = llclamp(gSavedSettings.getS32("KVRAMEvictionScheme"), 0, 1);

    LL_WARNS("KVRAMCache") << "Initializing KVRAM Cache (Passive Buffer Mode):"
        << "\n  RAM Budget: " << BYTES_TO_MEGA_BYTES(config.ram_budget_bytes) << " MB"
        << "\n  Soft Threshold: " << (mSoftThreshold * 100.0f) << "%"
        << "\n  Hard Threshold: " << (mHardThreshold * 100.0f) << "%"
        << "\n  Retention Time: " << config.ram_retention_time_seconds << " seconds"  // KV:GC→RT Renamed from ram_grace_period_seconds
        << "\n  Save/Pre-load Count: " << mMinDeckSize << " textures (0 = off)"
        << LL_ENDL;

    initialize(config);

    // Start dedicated worker thread for cache operations
    startThread();
}

// S24 : the sliders (and a hand-edited settings file) allow a hard threshold of 100%, which can never be reached
// because new textures are refused from 95%, and a soft threshold at or above the hard one. Either one switches the
// pressure release valve off, so keep them apart and below the refusal line.
void KVRAMCache::clampThresholds()
{
    mHardThreshold = llclamp(mHardThreshold, 0.60f, 0.94f);
    mSoftThreshold = llclamp(mSoftThreshold, 0.30f, mHardThreshold - 0.05f);
}

void KVRAMCache::updateFromSettings()
{
    LLMutexLock lock(&mCacheMutex);

    // Update runtime settings without reinitializing
    U32 new_budget_mb = gSavedSettings.getU32("KVRAMCacheRAMBudgetMB");
    new_budget_mb = llclamp(new_budget_mb, 256U, 4096U);
    mConfig.ram_budget_bytes = MEGA_BYTES_TO_BYTES(new_budget_mb);

    // KV:GC→RT Read minimum retention time from settings
    F32 retention_time = gSavedSettings.getF32("KVRAMCacheRetentionTimeSec");
    mConfig.ram_retention_time_seconds = llclamp(retention_time, 0.1f, 30.0f);
    mEvictionScheme = llclamp(gSavedSettings.getS32("KVRAMEvictionScheme"), 0, 1);

    mSoftThreshold = gSavedSettings.getF32("KVRAMCacheSoftThreshold");
    mHardThreshold = gSavedSettings.getF32("KVRAMCacheHardThreshold");
    clampThresholds();

    U32 deck_size = static_cast<U32>(gSavedSettings.getF32("KVRAMCacheMinDeckSize"));
    mMinDeckSize = llclamp(deck_size, 0U, 15000U);

    // KV:RT Removed mLastBaselineEviction reset — retention time is per-entry now

    LL_WARNS("KVRAMCache") << "Settings updated - retention time now "  // KV:GC→RT Renamed from grace period
        << mConfig.ram_retention_time_seconds << "s (per-entry minimum age check)" << LL_ENDL;
}

// ============================================================================
// Passive Buffer Interface (GPU-controlled)
// ============================================================================

bool KVRAMCache::hasTexture(const LLUUID& uuid)
{
    LLMutexLock lock(&mCacheMutex);

    auto it = mGhostMap.find(uuid);
    if (it == mGhostMap.end())
    {
        mExtendedStats.total_ram_misses++;
        return false;
    }

    CacheEntry* entry = it->second.get();

    // Only return true if in RAM with valid data
    bool has_it = (entry->location == AssetLocation::RAM && entry->texture_data != nullptr);

    // S24 : a probe is followed by getTexture(), which counts the hit; only a failed probe is a miss here
    if (!has_it)
    {
        mExtendedStats.total_ram_misses++;
    }

    return has_it;
}

bool KVRAMCache::acceptEviction(const LLUUID& uuid, void* texture_data, U64 size_bytes, S32 discard_level,
                                U32 width, U32 height, S8 components,
                                AssetPriority priority)
{
    LLMutexLock lock(&mCacheMutex);

    if (!texture_data || size_bytes == 0)
    {
        return false;
    }

    // CRITICAL: Reject incomplete or potentially corrupt textures
    // Discard levels: -1 = not loaded, 0 = full res, 1-5 = progressive lower resolutions
    // Accept discard 0-5 (even discard 5 is 1/32 resolution, valid for distant/loading textures)
    // Only reject discard < 0 (invalid) or > 5 (suspiciously high)
    if (discard_level < 0 || discard_level > 5)
    {
        mExtendedStats.textures_rejected_incomplete++;
        return false;
    }

    // Sanity check dimensions - reject zero or impossibly large textures
    // Small textures (4x4, 8x8, 16x16) are valid for particles, decals, tiny details
    if (width == 0 || height == 0 || width > 8192 || height > 8192)
    {
        mExtendedStats.textures_rejected_corrupt++;
        return false;
    }

    // Validate components (should be 1, 3, or 4 for grayscale/RGB/RGBA)
    if (components < 1 || components > 4)
    {
        mExtendedStats.textures_rejected_corrupt++;
        return false;
    }

    // Sanity check size vs dimensions - ONLY reject truly impossible/corrupt data
    // J2C compression is EXTREMELY efficient and varies wildly by content:
    // - Photo textures: 10:1 to 50:1 typical
    // - Flat colors/logos: 100:1 to 500:1 possible
    // - Vendor optimizations: Can be very aggressive
    // Only reject if size is less than 1 byte per 10,000 pixels (clearly truncated/corrupt)
    U64 pixel_count = (U64)width * height;
    if (size_bytes < pixel_count / 10000)  // < 0.01% = impossible
    {
        mExtendedStats.textures_rejected_corrupt++;
        return false;
    }

    // Track disk write saved
    mExtendedStats.disk_writes_buffered++;

    // Check if entry already exists FIRST - updates bypass pressure gates
    auto it = mGhostMap.find(uuid);
    bool is_update = (it != mGhostMap.end() && 
                      it->second->location == AssetLocation::RAM && 
                      it->second->texture_data != nullptr);

    // === PRESSURE-BASED ACCEPTANCE GATE (only for NEW entries) ===
    if (!is_update)
    {
        F32 pressure = getRAMPressure();

        // SAFETY NET: At critical pressure (95%+), reject ALL new entries
        // This is the fallthrough for very small caches or brutal inrush
        // Textures naturally route to disk cache instead
        if (pressure >= 0.95f)
        {
            return false;
        }

        // At hard threshold: reject new entries to allow eviction to catch up
        if (pressure >= mHardThreshold)
        {
            return false;
        }
    }

    // Process update or new entry
    if (is_update)
    {
        // UPDATE EXISTING ENTRY - always allowed, bypasses pressure checks
        // This ensures progressive loading (discard 5→3→1→0) works correctly
        CacheEntry* entry = it->second.get();
        U64 old_size = entry->size_bytes;

        // Free old buffer
        free(entry->texture_data);

        // Allocate and copy new data
        entry->texture_data = malloc(size_bytes);
        if (!entry->texture_data)
        {
            entry->location = AssetLocation::NONE;
            mStats.ram_used_bytes -= old_size;
            mStats.ram_entry_count--;
            return false;
        }
        memcpy(entry->texture_data, texture_data, size_bytes);

        entry->size_bytes = size_bytes;
        entry->discard_level = discard_level;
        entry->width = width;
        entry->height = height;
        entry->components = components;
        // Refreshing priority here does not relocate the uuid already sitting in
        // its old-priority deque slot; it self-corrects next time evictSlice()
        // re-inserts it via pushBackToPriorityDeques(), which uses the current
        // entry->priority.
        entry->priority = priority;

        // Update stats - just adjust size difference
        mStats.ram_used_bytes = mStats.ram_used_bytes - old_size + size_bytes;
        if (mCountInflow && size_bytes > old_size)
        {
            mInflowAccumBytes += (F64)(size_bytes - old_size);
        }

        return true;
    }

    // NEW ENTRY - allocate and add to BACK of deck (newest)
    void* new_buffer = malloc(size_bytes);
    if (!new_buffer)
    {
        return false;
    }
    memcpy(new_buffer, texture_data, size_bytes);

    auto entry = std::make_unique<CacheEntry>();
    entry->uuid = uuid;
    entry->texture_data = new_buffer;
    entry->size_bytes = size_bytes;
    entry->location = AssetLocation::RAM;
    entry->discard_level = discard_level;
    entry->width = width;
    entry->height = height;
    entry->components = components;
    entry->mAdmittedAt = LLTimer::getElapsedSeconds();  // KV:RT Record admission time — entry is protected from eviction for retention_time seconds
    entry->priority = priority;  // caller-classified priority

    // Capture priority before std::move(entry) into ghost map — avoids use-after-move (C26800).
    AssetPriority prio = entry->priority;
    mGhostMap[uuid] = std::move(entry);
    switch (prio)
    {
        case AssetPriority::BACKGROUND: mRAMLRU_BACKGROUND.push_back(uuid); break;
        case AssetPriority::NORMAL:     mRAMLRU_NORMAL.push_back(uuid);     break;
        case AssetPriority::HIGH:       mRAMLRU_HIGH.push_back(uuid);       break;
        case AssetPriority::CRITICAL:   mRAMLRU_CRITICAL.push_back(uuid);   break;
        default:                        mRAMLRU_NORMAL.push_back(uuid);     break;
    }

    mStats.ram_used_bytes += size_bytes;
    mStats.ram_entry_count++;
    if (mCountInflow)
    {
        mInflowAccumBytes += (F64)size_bytes;
    }

    return true;
}

bool KVRAMCache::getTexture(const LLUUID& texture_id,
                            U8*& texture_data, 
                            U32& size_bytes, 
                            S32& discard_level,
                            U32& width,
                            U32& height,
                            S8& components)
{
    LLMutexLock lock(&mCacheMutex);

    auto it = mGhostMap.find(texture_id);
    if (it == mGhostMap.end())
    {
        mExtendedStats.total_ram_misses++;
        return false;
    }

    CacheEntry* entry = it->second.get();

    // Only serve textures from RAM
    if (entry->location != AssetLocation::RAM || !entry->texture_data)
    {
        mExtendedStats.total_ram_misses++;
        return false;
    }

    // Validate data integrity
    if (entry->size_bytes == 0 || entry->discard_level < 0)
    {
        mExtendedStats.total_ram_misses++;
        return false;
    }

    // Copy while still holding the lock: a pointer into cache memory would be unprotected once
    // the lock is released and the entry could be evicted underneath the caller
    U8* copy = (U8*)ll_aligned_malloc_16(entry->size_bytes);
    if (!copy)
    {
        LL_WARNS("KVRAMCache") << "Failed to allocate memory for texture " << texture_id << LL_ENDL;
        mExtendedStats.total_ram_misses++;
        return false;
    }
    memcpy(copy, entry->texture_data, entry->size_bytes);
    texture_data = copy;
    size_bytes = static_cast<U32>(entry->size_bytes);
    discard_level = entry->discard_level;
    width = entry->width;
    height = entry->height;
    components = entry->components;

    mExtendedStats.total_ram_hits++;
    return true;
}

// ============================================================================
// Passive Buffer Interface (GPU-controlled)
// ============================================================================

// Shared by processPassiveEviction() and getEvictionPressureMultiplier() — single source
// of truth for the pressure curve; do not reimplement it separately.
F32 KVRAMCache::calculateEvictionPressureMultiplier(F32 pressure) const
{
    F32 fill_target = std::max(0.0f, mSoftThreshold - 0.10f);
    F32 pressure_multiplier = 0.0f;

    if (pressure >= mHardThreshold)
    {
        // At/above hard threshold: aggressive eviction
        pressure_multiplier = 2.0f;
    }
    else if (pressure >= mSoftThreshold)
    {
        // Between soft and hard: active eviction 1.0x -> 2.0x
        F32 range = mHardThreshold - mSoftThreshold;
        if (range > 0.0f)
        {
            F32 offset = pressure - mSoftThreshold;
            pressure_multiplier = 1.0f + (offset / range);
        }
        else
        {
            pressure_multiplier = 2.0f;
        }
    }
    else if (pressure > fill_target)
    {
        // KV:EV Between fill_target and soft: gentle eviction 0x -> 1.0x
        F32 range = mSoftThreshold - fill_target;
        if (range > 0.0f)
        {
            F32 offset = pressure - fill_target;
            pressure_multiplier = (offset / range);
        }
        else
        {
            pressure_multiplier = 1.0f;
        }
    }
    // KV:EV else: pressure <= fill_target, multiplier stays 0, cache fills freely (was hardcoded 25%)

    return pressure_multiplier;
}

F32 KVRAMCache::getEvictionPressureMultiplier() const
{
    // No lock — mirrors getRAMPressure() (read cross-thread unlocked elsewhere);
    // mSoftThreshold/mHardThreshold are plain floats, not worth a mutex for a UI readout.
    return calculateEvictionPressureMultiplier(getRAMPressure());
}

void KVRAMCache::processPassiveEviction(F32 delta_time)
{
    // Entries evicted this pass that still need a disk write: populated under
    // the lock below (cheap, no I/O/decode), drained after it's released.
    // A slice can be up to ~20 textures (base_slice=10 * up to 2.0x pressure
    // multiplier); decoding that many while holding mCacheMutex would block
    // every other thread's hasTexture()/getTexture()/acceptEviction() call.
    std::vector<PendingDiskWrite> pending;

    {
        LLMutexLock lock(&mCacheMutex);

        F32 pressure = getRAMPressure();
        F32 retention_time = std::max(0.1f, mConfig.ram_retention_time_seconds);  // KV:GC->RT Renamed from ram_grace_period_seconds

        F64 now = LLTimer::getElapsedSeconds();

        // S24 : smoothed inflow, in bytes per second: what the world is pushing into the cache.
        {
            const F64 dt = (mInflowLastSample > 0.0) ? std::max(0.01, now - mInflowLastSample) : std::max(0.01, (F64)delta_time);
            const F64 instant = mInflowAccumBytes / dt;
            mInflowAccumBytes = 0.0;
            mInflowLastSample = now;
            const F64 alpha = 1.0 - std::exp(-dt / EVICT_INFLOW_TAU_SECONDS);
            const F64 average = mInflowBytesPerSec.load(std::memory_order_relaxed);
            mInflowBytesPerSec.store(average + alpha * (instant - average), std::memory_order_relaxed);
        }

        // Duplicated from calculateEvictionPressureMultiplier() (pure function of
        // mSoftThreshold) rather than exposed as a member — needed for the
        // always-evict->=1 rule below.
        F32 fill_target = std::max(0.0f, mSoftThreshold - 0.10f);
        F32 pressure_multiplier = calculateEvictionPressureMultiplier(pressure);

        // KV:RT decoupled base_slice from retention_time — fixed at 10 textures/batch
        U32 base_slice = 10;

        // Apply pressure scaling to base slice
        U32 slice_size = (U32)(base_slice * pressure_multiplier);

        // KV:EV If above fill_target, ALWAYS evict at least 1 texture (was hardcoded > 25%)
        if (pressure > fill_target && slice_size == 0)
        {
            slice_size = 1;
        }

        // KV:EV mMinDeckSize acts as a minimum texture reserve — deck must exceed this before batch eviction kicks in
        // Combined with fill_target: pressure-protects up to fill_target, count-protects down to mMinDeckSize
        size_t total_entries = mRAMLRU.size() + mRAMLRU_BACKGROUND.size()
                             + mRAMLRU_NORMAL.size() + mRAMLRU_HIGH.size()
                             + mRAMLRU_CRITICAL.size();
        if (slice_size > 0 && total_entries > mMinDeckSize && slice_size < 5)
        {
            slice_size = 5;
        }

        // S24 : eviction controller (KVRAMEvictionScheme 1). The count-based slice above is 10 textures a
        // second at most: about 2 MB/s whatever the cache size, so a big cache that filled up took minutes
        // to come back down while it refused every new texture. Here the inflow sets how often a pass runs
        // (to keep the level steady, so disk writes can never exceed what the same traffic cost before the
        // RAM cache existed), the cache size sets how big each pass is (a fixed share of the budget), the
        // overage above the fill target raises the rate, and a pressure release valve drains hard when
        // the cache is overwhelmed. A pass is bounded in entries, because it can run on the main thread.
        F32 next_interval = EVICTION_CHECK_INTERVAL;
        bool emergency = false;
        F32 effective_retention = retention_time;
        if (mEvictionScheme.load(std::memory_order_relaxed) != 0)
        {
            slice_size = 0;
            const F64 budget = (F64)mConfig.ram_budget_bytes;
            const F64 used = (F64)mStats.ram_used_bytes;
            const F64 fill_bytes = (F64)fill_target * budget;
            const F64 soft_bytes = (F64)mSoftThreshold * budget;
            const F64 hard_bytes = (F64)mHardThreshold * budget;

            if (used > fill_bytes && mStats.ram_entry_count > 0)
            {
                const F64 over = used - fill_bytes;
                const F64 ratio = std::min(over / std::max(hard_bytes - fill_bytes, 1.0), 2.0);
                F64 rate = mInflowBytesPerSec.load(std::memory_order_relaxed) * (1.0 + EVICT_OVERAGE_GAIN * ratio)
                         + EVICT_FLOOR_FRACTION * budget;

                // Pressure release valve: at the hard threshold new textures are refused, so be back at
                // the soft threshold within VALVE_DRAIN_SECONDS (half that at 95%), whatever the inflow.
                const bool valve = pressure >= mHardThreshold;
                if (valve)
                {
                    // Emergency: the retention time must not be able to hold the cache at the hard threshold
                    // (a long retention time times a fast inflow can protect most of the cache), so it is
                    // capped, and a priority whose oldest entry is still protected is skipped, not waited on.
                    emergency = true;
                    effective_retention = std::min(retention_time, 0.25f);
                    const F64 seconds = (pressure >= 0.95f) ? VALVE_CRITICAL_DRAIN_SECONDS : VALVE_DRAIN_SECONDS;
                    rate = std::max(rate, std::max(0.0, used - soft_bytes) / seconds);
                }

                const F64 avg_entry = std::max(1.0, used / (F64)mStats.ram_entry_count);
                F64 batch_bytes = std::clamp(EVICT_BATCH_FRACTION * budget, EVICT_MIN_BATCH_BYTES, EVICT_MAX_BATCH_BYTES);
                if (valve)
                {
                    batch_bytes *= 4.0;
                }
                const U32 max_entries = valve ? EVICT_VALVE_MAX_ENTRIES : EVICT_MAX_ENTRIES;
                slice_size = (U32)std::clamp(batch_bytes / avg_entry, (F64)EVICT_MIN_ENTRIES, (F64)max_entries);

                next_interval = (F32)std::clamp((F64)slice_size * avg_entry / std::max(rate, 1.0),
                                                (F64)EVICT_MIN_INTERVAL, (F64)EVICTION_CHECK_INTERVAL);
            }
            else if (used > fill_bytes * 0.95)
            {
                next_interval = 0.25f; // close to the fill target: look again soon
            }
        }
        mNextEvictionInterval.store(next_interval, std::memory_order_relaxed);

        if (slice_size > 0)
        {
            evictSlice(slice_size, effective_retention, now, pending, emergency);  // KV:RT Pass retention_time and now for per-entry age check
        }
        // KV:RT Removed mLastBaselineEviction tracking — eviction is now pressure-driven, not interval-driven
    }

    // Lock released above; do the real decode+disk-write work for what
    // evictSlice() collected. See PendingDiskWrite for why this split exists.
    // S24 : refill the disk write budget, then write only what it covers (and at most
    // EVICT_MAX_WRITES_PER_PASS per pass); the rest is freed unwritten.
    static F64 sWriteTokens = DISK_WRITE_BYTES_PER_SEC;
    static F64 sLastRefill = 0.0;
    const F64 refill_now = LLTimer::getElapsedSeconds();
    if (sLastRefill > 0.0)
    {
        sWriteTokens = std::min(DISK_WRITE_BURST_BYTES, sWriteTokens + (refill_now - sLastRefill) * DISK_WRITE_BYTES_PER_SEC);
    }
    sLastRefill = refill_now;

    U32 writes_this_pass = 0;
    for (const PendingDiskWrite& item : pending)
    {
        const F64 cost = (F64)item.size_bytes;
        if (writes_this_pass < EVICT_MAX_WRITES_PER_PASS && sWriteTokens >= cost
            && sInFlightDiskWriteBytes.load(std::memory_order_relaxed) < MAX_IN_FLIGHT_DISK_WRITE_BYTES)
        {
            sWriteTokens -= cost;
            ++writes_this_pass;
            writeEvictedEntryToDisk(item);
        }
        else
        {
            free(item.data);
            mEvictionsDroppedUnwritten.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

// KV:RT Added retention_time/now params — young entries are pushed back instead of evicted
void KVRAMCache::evictSlice(U32 slice_size, F64 retention_time, F64 now, std::vector<PendingDiskWrite>& out_pending, bool emergency)
{
    U32 sliced = 0;

    // Priority-aware eviction: lowest priority first.
    // Order: BACKGROUND -> NORMAL -> HIGH -> CRITICAL -> legacy mRAMLRU (fallback).
    //
    // S24 : each deque is in admission order (oldest at the front), so once the front entry is younger than the
    // retention time everything behind it is too. The old loop popped a young entry and pushed it to the back
    // again, which rotated the lowest priority deque over and over (up to every entry in the cache, all under
    // mCacheMutex) and never reached the next priority at all. Now a young front simply ends that deque for
    // this pass. In an emergency (the pressure release valve) the next priority is used instead of giving up.
    std::deque<LLUUID>* tiers[5] = { &mRAMLRU_BACKGROUND, &mRAMLRU_NORMAL, &mRAMLRU_HIGH, &mRAMLRU_CRITICAL, &mRAMLRU };

    for (std::deque<LLUUID>* dq : tiers)
    {
        bool front_protected = false;
        while (sliced < slice_size && !dq->empty())
        {
            const LLUUID bottom_uuid = dq->front();

            auto it = mGhostMap.find(bottom_uuid);
            if (it == mGhostMap.end())
            {
                dq->pop_front(); // stale id: the entry is already gone
                continue;
            }

            CacheEntry* entry = it->second.get();

            // KV:RT Retention time check - entries that haven't aged enough stay where they are
            if (now - entry->mAdmittedAt < retention_time)
            {
                front_protected = true;
                break;
            }

            dq->pop_front();

            if (entry->location == AssetLocation::RAM && entry->texture_data)
            {
                // Update stats BEFORE freeing
                mStats.ram_used_bytes -= entry->size_bytes;
                mStats.ram_entry_count--;
                mStats.ram_evictions++;

                // Transfer the buffer to out_pending instead of freeing it here;
                // processPassiveEviction() decodes and writes each pending entry
                // to disk after releasing mCacheMutex (lltexturefetch.cpp's
                // acceptEviction() caller skips its own disk write on that promise).
                // Null out entry->texture_data so CacheEntry's destructor can't
                // double-free once erased below.
                PendingDiskWrite pending;
                pending.uuid = bottom_uuid;
                pending.data = static_cast<U8*>(entry->texture_data);
                pending.size_bytes = entry->size_bytes;
                pending.discard_level = entry->discard_level;
                pending.width = entry->width;
                pending.height = entry->height;
                pending.components = entry->components;
                out_pending.push_back(pending);
                entry->texture_data = nullptr;

                // Remove from ghost map completely
                mGhostMap.erase(it);

                sliced++;
            }
        }

        if (sliced >= slice_size)
        {
            break;
        }
        // An empty or used-up priority simply hands over to the next one (as the old pop-from-the-lowest-non-empty
        // loop did). Only a priority whose oldest entry is still protected by the retention time stops a normal
        // pass: it waits for that entry to age rather than evicting a higher priority first. In an emergency it
        // moves on.
        if (front_protected && !emergency)
        {
            break;
        }
    }
}

// Completes the disk write that acceptEviction()'s caller (lltexturefetch.cpp)
// deferred. KVRAMCache only stores compressed J2C bytes, and the texture cache
// can write those straight to disk: the raw image it asks for is only used for
// the fast-cache preview, so none is needed here. S24 : this used to decode
// every evicted entry back to pixels (a full J2C decode, a multi-MB raw
// allocation and a second deep copy per eviction, 5-20 times a second at the
// soft threshold), which showed up as microstutters on the main loop. Always
// frees item.data exactly once, whether or not the write is queued.
void KVRAMCache::writeEvictedEntryToDisk(const PendingDiskWrite& item)
{
    if (!item.data || item.size_bytes == 0)
    {
        free(item.data);
        return;
    }

    LLTextureCache* cache = LLAppViewer::getTextureCache();
    if (!cache)
    {
        free(item.data);
        return;
    }

    // The original file size is not kept with the entry. Anything below full
    // resolution is only a prefix of the file, so report it as incomplete
    // (datasize + 1, the same "not fully loaded" flag lltexturefetch.cpp uses):
    // a later fetch then asks for the remainder instead of treating the prefix
    // as the whole texture and leaving it permanently blurry. Discard 0 data is
    // the full stream. Responder takes ownership of item.data and frees it once
    // the async write completes - see KVRAMDiskWriteResponder's comment for why
    // that can't happen here.
    const S32 data_size = (S32)item.size_bytes;
    const S32 image_size = (item.discard_level > 0) ? data_size + 1 : data_size;
    cache->writeToCache(item.uuid, item.data, data_size, image_size,
        LLPointer<LLImageRaw>(), item.discard_level, new KVRAMDiskWriteResponder(item.data, item.size_bytes));
}

// KV:RT Helper to re-insert a texture at the back of its priority deque when it's too young to evict
void KVRAMCache::pushBackToPriorityDeques(const LLUUID& uuid, AssetPriority priority)
{
    switch (priority)
    {
        case AssetPriority::BACKGROUND: mRAMLRU_BACKGROUND.push_back(uuid); break;
        case AssetPriority::NORMAL:     mRAMLRU_NORMAL.push_back(uuid);     break;
        case AssetPriority::HIGH:       mRAMLRU_HIGH.push_back(uuid);       break;
        case AssetPriority::CRITICAL:   mRAMLRU_CRITICAL.push_back(uuid);   break;
        default:                        mRAMLRU_NORMAL.push_back(uuid);     break;
    }
}

// ============================================================================
// Helper Functions
// ============================================================================

void KVRAMCache::removeFromRAMLRU(const LLUUID& uuid)
{
    auto eraseFromDeque = [&uuid](std::deque<LLUUID>& dq) -> bool
    {
        auto it = std::find(dq.begin(), dq.end(), uuid);
        if (it != dq.end()) { dq.erase(it); return true; }
        return false;
    };
    if (eraseFromDeque(mRAMLRU_BACKGROUND)) return;
    if (eraseFromDeque(mRAMLRU_NORMAL))     return;
    if (eraseFromDeque(mRAMLRU_HIGH))       return;
    if (eraseFromDeque(mRAMLRU_CRITICAL))   return;
    eraseFromDeque(mRAMLRU); // legacy fallback
}
// ============================================================================
// Manual Cache Control
// ============================================================================

void KVRAMCache::remove(const LLUUID& uuid)
{
    LLMutexLock lock(&mCacheMutex);

    auto it = mGhostMap.find(uuid);
    if (it == mGhostMap.end())
    {
        return;
    }

    CacheEntry* entry = it->second.get();

    // Remove from LRU queue
    KVRAMCache::removeFromRAMLRU(uuid);

    // Update statistics
    if (entry->location == AssetLocation::RAM)
    {
        mStats.ram_used_bytes -= entry->size_bytes;
        mStats.ram_entry_count--;
    }

    // Free texture data buffer if allocated (destructor will also handle this)
    if (entry->texture_data)
    {
        free(entry->texture_data);
        entry->texture_data = nullptr;
    }

    mGhostMap.erase(it);
}

void KVRAMCache::clearAll()
{
    LLMutexLock lock(&mCacheMutex);

    // Free all texture data buffers
    for (auto& pair : mGhostMap)
    {
        if (pair.second->texture_data)
        {
            free(pair.second->texture_data);
            pair.second->texture_data = nullptr;
        }
    }

    mGhostMap.clear();
    mRAMLRU.clear();
    mRAMLRU_BACKGROUND.clear();
    mRAMLRU_NORMAL.clear();
    mRAMLRU_HIGH.clear();
    mRAMLRU_CRITICAL.clear();

    resetStats();

    LL_WARNS("KVRAMCache") << "Cleared all cache entries" << LL_ENDL;
}

F32 KVRAMCache::getRAMPressure() const
{
    if (mConfig.ram_budget_bytes == 0)
    {
        return 0.0f;
    }

    // Work in MB to avoid precision issues with large byte counts
    U32 used_mb = (U32)(mStats.ram_used_bytes / 1048576);
    U32 budget_mb = (U32)(mConfig.ram_budget_bytes / 1048576);

    if (budget_mb == 0) return 0.0f;

    return (F32)used_mb / (F32)budget_mb;
}

// ============================================================================
// Frame Update (Main Thread Coordination)
// ============================================================================

void KVRAMCache::updateFrame(F32 delta_time_seconds)
{
    if (!mInitialized || !mWorkerThread)
    {
        return;
    }

    // Update worker thread - processes completed requests (1ms max per frame)
    mWorkerThread->update(1.0f);

    // KV:GC→RT Renamed — eviction check interval (was GRACE_CHECK_INTERVAL)
    mTimeSinceLastEvictionCheck += delta_time_seconds;

    // S24 : with the controller, the last pass worked out when the next one is due
    const F32 check_interval = (mEvictionScheme.load(std::memory_order_relaxed) != 0)
        ? mNextEvictionInterval.load(std::memory_order_relaxed) : EVICTION_CHECK_INTERVAL;
    if (mTimeSinceLastEvictionCheck >= check_interval)
    {
        F32 delta = mTimeSinceLastEvictionCheck;
        mTimeSinceLastEvictionCheck = 0.0f;

        // Queue eviction processing on worker thread
        mWorkerThread->queueProcessEviction(delta);
    }
}

// ============================================================================
// Statistics and Debugging
// ============================================================================

KVRAMCache::ExtendedStats KVRAMCache::getExtendedStats() const
{
    // S24 : copied under the lock; the hit rate is derived on demand, not recomputed every frame
    LLMutexLock lock(&mCacheMutex);
    ExtendedStats stats = mExtendedStats;
    stats.evictions_dropped_unwritten = mEvictionsDroppedUnwritten.load(std::memory_order_relaxed);
    const U64 attempts = stats.total_ram_hits + stats.total_ram_misses;
    stats.ram_hit_rate = attempts > 0 ? (F32)stats.total_ram_hits / (F32)attempts : 0.0f;
    return stats;
}

void KVRAMCache::resetStats()
{
    mStats = CacheStats();
}

void KVRAMCache::dumpState() const
{
    LLMutexLock lock(&mCacheMutex);

    LL_WARNS("KVRAMCache") << "=== KVRAMCache State Dump ==="
        << "\nRAM: " << mStats.ram_entry_count << " entries, "
        << BYTES_TO_MEGA_BYTES(mStats.ram_used_bytes) << " / "
        << BYTES_TO_MEGA_BYTES(mConfig.ram_budget_bytes) << " MB ("
        << (getRAMPressure() * 100.0f) << "%)"
        << "\nCache Hits: RAM=" << mStats.ram_hits
        << "\nNetwork Requests: " << mStats.network_requests
        << "\nEvictions: RAM=" << mStats.ram_evictions
        << "\nTotal Deck Entries: " << (mRAMLRU.size() + mRAMLRU_BACKGROUND.size() + mRAMLRU_NORMAL.size() + mRAMLRU_HIGH.size() + mRAMLRU_CRITICAL.size())
        << LL_ENDL;
}

void KVRAMCache::updateStats()
{
    // Stats are updated incrementally, but this can be called to recalculate
    // from scratch if needed for debugging

    mStats.ram_entry_count = 0;
    mStats.ram_used_bytes = 0;

    for (const auto& pair : mGhostMap)
    {
        const CacheEntry* entry = pair.second.get();

        if (entry->location == AssetLocation::RAM)
        {
            mStats.ram_entry_count++;
            mStats.ram_used_bytes += entry->size_bytes;
        }
    }
}
