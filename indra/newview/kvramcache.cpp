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
#include <fstream>
#include <cstddef>

namespace
{
    // LLTextureCacheWorker stores the raw pointer, not a copy, so this buffer
    // must survive until the async disk write completes. Responder is
    // ref-counted via LLPointer (see LLTextureCacheWorker::mResponder), so it
    // self-destructs once the write completes; frees the buffer exactly once.
    class KVRAMDiskWriteResponder : public LLTextureCache::WriteResponder
    {
    public:
        explicit KVRAMDiskWriteResponder(U8* data) : mData(data) {}
        ~KVRAMDiskWriteResponder() override { free(mData); }
        void completed(bool success) override { /* nothing else to do - see destructor */ }
    private:
        U8* mData;
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
        << "\n  Min Deck Size: " << mMinDeckSize << " textures (range: 100-10000)"
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
        // Snapshot everything RAM-resident under one lock - plain memcpy's
        // (via vector's range-copy), no decode, no I/O while locked.
        struct Snap
        {
            LLUUID uuid;
            std::vector<U8> data;
            S32 discard_level = -1;
            U32 width = 0;
            U32 height = 0;
            U8 components = 0;
        };
        std::vector<Snap> snapshot;
        {
            LLMutexLock lock(&mCacheMutex);
            snapshot.reserve(mGhostMap.size());
            for (auto& pair : mGhostMap)
            {
                CacheEntry* entry = pair.second.get();
                if (entry->location == AssetLocation::RAM && entry->texture_data && entry->size_bytes > 0)
                {
                    Snap s;
                    s.uuid = pair.first;
                    const U8* bytes = static_cast<const U8*>(entry->texture_data);
                    s.data.assign(bytes, bytes + entry->size_bytes);
                    s.discard_level = entry->discard_level;
                    s.width = entry->width;
                    s.height = entry->height;
                    s.components = entry->components;
                    snapshot.push_back(std::move(s));
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
            for (const Snap& s : snapshot)
            {
                if (LLTimer::getElapsedSeconds() >= deadline)
                {
                    break;
                }

                LLCRC crc;
                crc.update(s.data.data(), s.data.size());

                KVSnapshotEntryHeader eh;
                memcpy(eh.uuid, s.uuid.mData, 16);
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

        if (!snapshot.empty())
        {
            // Real scrolling line for the final result, so the console's
            // permanent scrollback shows a clean confirmation after the
            // in-place progress bar rather than just vanishing.
            AresWin32::UpdateAresDialog(llformat("  Flushed %u/%u textures to disk.", written, (U32)snapshot.size()));
        }

        LL_WARNS("KVRAMCache") << "Shutdown flush: wrote " << written << "/" << snapshot.size()
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
    if (!gSavedSettings.getBOOL("KVRAMCacheEnabled") || !mInitialized)
    {
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

    for (U32 i = 0; i < header.entry_count && loaded < max_count; ++i)
    {
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

void KVRAMCache::updateConfig(const CacheConfig& config)
{
    LLMutexLock lock(&mCacheMutex);

    mConfig = config;

    LL_WARNS("KVRAMCache") << "Configuration updated" << LL_ENDL;
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

    // Minimum deck size (100-15000 textures in 100 steps)
    U32 deck_size = static_cast<U32>(gSavedSettings.getF32("KVRAMCacheMinDeckSize"));
    mMinDeckSize = llclamp(deck_size, 100U, 15000U);

    LL_WARNS("KVRAMCache") << "Initializing KVRAM Cache (Passive Buffer Mode):"
        << "\n  RAM Budget: " << BYTES_TO_MEGA_BYTES(config.ram_budget_bytes) << " MB"
        << "\n  Soft Threshold: " << (mSoftThreshold * 100.0f) << "%"
        << "\n  Hard Threshold: " << (mHardThreshold * 100.0f) << "%"
        << "\n  Retention Time: " << config.ram_retention_time_seconds << " seconds"  // KV:GC→RT Renamed from ram_grace_period_seconds
        << "\n  Min Deck Size: " << mMinDeckSize << " textures"
        << LL_ENDL;

    initialize(config);

    // Start dedicated worker thread for cache operations
    startThread();
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

    mSoftThreshold = gSavedSettings.getF32("KVRAMCacheSoftThreshold");
    mHardThreshold = gSavedSettings.getF32("KVRAMCacheHardThreshold");

    U32 deck_size = static_cast<U32>(gSavedSettings.getF32("KVRAMCacheMinDeckSize"));
    mMinDeckSize = llclamp(deck_size, 100U, 15000U);

    // KV:RT Removed mLastBaselineEviction reset — retention time is per-entry now

    LL_WARNS("KVRAMCache") << "Settings updated - retention time now "  // KV:GC→RT Renamed from grace period
        << mConfig.ram_retention_time_seconds << "s (per-entry minimum age check)" << LL_ENDL;
}

void KVRAMCache::setThresholds(F32 soft, F32 hard)
{
    LLMutexLock lock(&mCacheMutex);

    mSoftThreshold = llclamp(soft, 0.0f, 1.0f);
    mHardThreshold = llclamp(hard, 0.0f, 1.0f);

    // Ensure hard >= soft
    if (mHardThreshold < mSoftThreshold)
    {
        mHardThreshold = mSoftThreshold;
    }

    LL_WARNS("KVRAMCache") << "Thresholds updated: Soft=" << (mSoftThreshold * 100.0f) 
        << "%, Hard=" << (mHardThreshold * 100.0f) << "%" << LL_ENDL;
}

// KV:GC→RT Renamed from setGracePeriod — sets minimum retention time before an entry is evictable
void KVRAMCache::setRetentionTime(F32 seconds)
{
    LLMutexLock lock(&mCacheMutex);

    mConfig.ram_retention_time_seconds = std::max(0.0f, seconds);

    LL_WARNS("KVRAMCache") << "Retention time updated: " << seconds << " seconds (min age before eviction)" << LL_ENDL;
}

void KVRAMCache::resetExtendedStats()
{
    LLMutexLock lock(&mCacheMutex);

    mExtendedStats = ExtendedStats();

    LL_WARNS("KVRAMCache") << "Extended statistics reset" << LL_ENDL;
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
        mExtendedStats.ram_misses_this_frame++;
        mExtendedStats.total_ram_misses++;
        return false;
    }

    CacheEntry* entry = it->second.get();

    // Only return true if in RAM with valid data
    bool has_it = (entry->location == AssetLocation::RAM && entry->texture_data != nullptr);

    if (has_it)
    {
        mExtendedStats.ram_hits_this_frame++;
        mExtendedStats.total_ram_hits++;
    }
    else
    {
        mExtendedStats.ram_misses_this_frame++;
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

    return true;
}

void* KVRAMCache::getTexture(const LLUUID& uuid)
{
    LLMutexLock lock(&mCacheMutex);

    auto it = mGhostMap.find(uuid);
    if (it == mGhostMap.end())
    {
        mExtendedStats.ram_misses_this_frame++;
        mExtendedStats.total_ram_misses++;
        return nullptr;
    }

    CacheEntry* entry = it->second.get();

    if (entry->location != AssetLocation::RAM)
    {
        mExtendedStats.ram_misses_this_frame++;
        mExtendedStats.total_ram_misses++;
        return nullptr;
    }

    mExtendedStats.ram_hits_this_frame++;
    mExtendedStats.total_ram_hits++;
    return entry->texture_data;
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
        mExtendedStats.ram_misses_this_frame++;
        mExtendedStats.total_ram_misses++;
        return false;
    }

    CacheEntry* entry = it->second.get();

    // Only serve textures from RAM
    if (entry->location != AssetLocation::RAM || !entry->texture_data)
    {
        mExtendedStats.ram_misses_this_frame++;
        mExtendedStats.total_ram_misses++;
        return false;
    }

    // Validate data integrity
    if (entry->size_bytes == 0 || entry->discard_level < 0)
    {
        mExtendedStats.ram_misses_this_frame++;
        mExtendedStats.total_ram_misses++;
        return false;
    }

    // Return pointers to cache data (caller must copy if retention needed)
    texture_data = static_cast<U8*>(entry->texture_data);
    size_bytes = static_cast<U32>(entry->size_bytes);
    discard_level = entry->discard_level;
    width = entry->width;
    height = entry->height;
    components = entry->components;

    mExtendedStats.ram_hits_this_frame++;
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

        if (slice_size > 0)
        {
            evictSlice(slice_size, retention_time, now, pending);  // KV:RT Pass retention_time and now for per-entry age check
        }
        // KV:RT Removed mLastBaselineEviction tracking — eviction is now pressure-driven, not interval-driven
    }

    // Lock released above; do the real decode+disk-write work for what
    // evictSlice() collected. See PendingDiskWrite for why this split exists.
    for (const PendingDiskWrite& item : pending)
    {
        writeEvictedEntryToDisk(item);
    }
}

// KV:RT Added retention_time/now params — young entries are pushed back instead of evicted
void KVRAMCache::evictSlice(U32 slice_size, F64 retention_time, F64 now, std::vector<PendingDiskWrite>& out_pending)
{
    U32 sliced = 0;

    // Priority-aware eviction: pop from lowest non-empty priority deque first.
    // Order: BACKGROUND → NORMAL → HIGH → CRITICAL → legacy mRAMLRU (fallback).
    auto popFrontFromPriorityDeques = [this]() -> LLUUID
    {
        if (!mRAMLRU_BACKGROUND.empty()) { LLUUID id = mRAMLRU_BACKGROUND.front(); mRAMLRU_BACKGROUND.pop_front(); return id; }
        if (!mRAMLRU_NORMAL.empty())     { LLUUID id = mRAMLRU_NORMAL.front();     mRAMLRU_NORMAL.pop_front();     return id; }
        if (!mRAMLRU_HIGH.empty())       { LLUUID id = mRAMLRU_HIGH.front();       mRAMLRU_HIGH.pop_front();       return id; }
        if (!mRAMLRU_CRITICAL.empty())   { LLUUID id = mRAMLRU_CRITICAL.front();   mRAMLRU_CRITICAL.pop_front();   return id; }
        // Legacy fallback — entries still in old mRAMLRU during transition
        LLUUID id = mRAMLRU.front();
        mRAMLRU.pop_front();
        return id;
    };

    auto totalEntries = [this]() -> size_t {
        return mRAMLRU.size() + mRAMLRU_BACKGROUND.size()
             + mRAMLRU_NORMAL.size() + mRAMLRU_HIGH.size()
             + mRAMLRU_CRITICAL.size();
    };

    // KV:RT Track scanned count to prevent infinite loop when ALL entries are too young
    U32 scanned = 0;

    while (sliced < slice_size && totalEntries() > 0 && scanned < totalEntries())
    {
        LLUUID bottom_uuid = popFrontFromPriorityDeques();

        auto it = mGhostMap.find(bottom_uuid);
        if (it != mGhostMap.end())
        {
            CacheEntry* entry = it->second.get();

            // KV:RT Retention time check — skip entries that haven't aged enough
            F64 entry_age = now - entry->mAdmittedAt;
            if (entry_age < retention_time)
            {
                // Too young! Re-insert at the back of its priority deque for another chance
                pushBackToPriorityDeques(bottom_uuid, entry->priority);
                scanned++;
                continue;
            }

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
    }
}

// Completes the disk write that acceptEviction()'s caller (lltexturefetch.cpp)
// deferred. KVRAMCache only stores compressed J2C bytes, but
// LLTextureCache::writeToCache() requires a real LLImageRaw (isBufferInvalid()
// dereferences it unconditionally), so this decodes the bytes back to pixels
// first. Real CPU decode work — must be called with no lock held. Always
// frees item.data exactly once, decode success or failure.
void KVRAMCache::writeEvictedEntryToDisk(const PendingDiskWrite& item)
{
    if (!item.data || item.size_bytes == 0)
    {
        free(item.data);
        return;
    }

    // Do not use LLImageFormatted::setData() with item.data: it takes direct
    // ownership and later frees it with ll_aligned_free_16(), but item.data was
    // malloc()'d in acceptEviction() — mismatched allocator/deallocator, heap
    // corruption. allocateData()+memcpy() makes an independent aligned copy
    // instead, leaving item.data itself for the responder below.
    LLPointer<LLImageJ2C> compressed = new LLImageJ2C;
    U8* compressed_copy = compressed->allocateData((S32)item.size_bytes);
    if (!compressed_copy)
    {
        LL_WARNS("KVRAMCache") << "writeEvictedEntryToDisk: allocateData failed for " << item.uuid << LL_ENDL;
        free(item.data);
        return;
    }
    memcpy(compressed_copy, item.data, item.size_bytes);

    LLPointer<LLImageRaw> raw = new LLImageRaw;
    if (!compressed->decode(raw, 0.0f))
    {
        LL_WARNS("KVRAMCache") << "writeEvictedEntryToDisk: failed to decode " << item.uuid
            << " for disk write-back (" << item.width << "x" << item.height
            << ", discard " << item.discard_level << ") - texture will need a full "
            << "network re-fetch instead of a disk hit next time it's needed" << LL_ENDL;
        free(item.data);
        return;
    }

    LLTextureCache* cache = LLAppViewer::getTextureCache();
    if (!cache)
    {
        free(item.data);
        return;
    }

    // datasize and imagesize both = the full compressed size: KVRAMCache
    // only ever holds complete (if possibly lower-discard-level) data, never
    // a partial/truncated in-progress download - matches lltexturefetch.cpp's
    // own mHaveAllData=true convention for mFileSize. Responder takes
    // ownership of item.data and frees it once the async write completes -
    // see KVRAMDiskWriteResponder's comment for why that can't happen here.
    cache->writeToCache(item.uuid, item.data, (S32)item.size_bytes,
        (S32)item.size_bytes, raw, item.discard_level, new KVRAMDiskWriteResponder(item.data));
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

    if (mTimeSinceLastEvictionCheck >= EVICTION_CHECK_INTERVAL)
    {
        F32 delta = mTimeSinceLastEvictionCheck;
        mTimeSinceLastEvictionCheck = 0.0f;

        // Queue eviction processing on worker thread
        mWorkerThread->queueProcessEviction(delta);
    }

    // Update stats (calculate hit rate)
    U64 total_hits = mExtendedStats.total_ram_hits;
    U64 total_attempts = total_hits + mExtendedStats.total_ram_misses;

    if (total_attempts > 0)
    {
        mExtendedStats.ram_hit_rate = (F32)total_hits / (F32)total_attempts;
    }
    else
    {
        mExtendedStats.ram_hit_rate = 0.0f;
    }
}

// ============================================================================
// Statistics and Debugging
// ============================================================================

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
