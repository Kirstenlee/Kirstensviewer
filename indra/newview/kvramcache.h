/**
 * @file kvramcache.h
 * @brief Passive RAM buffer cache for decoded texture data (J2C), sitting
 * between the network fetch path and the on-disk texture cache.
 *
 * A texture is admitted here on eviction from the fetch pipeline, held for
 * a minimum retention time, and either re-served from RAM on a cache hit
 * or written through to the real on-disk cache (LLTextureCache) once it
 * ages out. There is no separate VRAM or DISK tier managed by this class -
 * GPU residency is LLViewerTexture's concern, and on-disk storage is
 * LLTextureCache's.
 *
 * $LicenseInfo:firstyear=2024&license=viewerlgpl$
 * Kirstens S24 Viewer Source Code
 * $/LicenseInfo$
 */

#ifndef LL_KVRAMCACHE_H
#define LL_KVRAMCACHE_H

#include "llsingleton.h"
#include "lluuid.h"
#include "llmutex.h"
#include "llunits.h"
#include "lltimer.h"
#include <unordered_map>
#include <deque>
#include <memory>
#include <vector>
#include <atomic>

#ifndef BYTES_TO_MEGA_BYTES
#define BYTES_TO_MEGA_BYTES(x) ((x) >> 20)
#endif
#ifndef MEGA_BYTES_TO_BYTES
#define MEGA_BYTES_TO_BYTES(x) (((U64)(x)) << 20)  // Cast to U64 to avoid overflow for values >4GB
#endif

class LLImageRaw;
class LLImageDX;
class KVCacheThread;

class KVRAMCache : public LLSingleton<KVRAMCache>
{
    LLSINGLETON(KVRAMCache);
    virtual ~KVRAMCache();

public:
    enum class AssetLocation : U8
    {
        NONE = 0,      // Not in cache, needs network fetch
        RAM = 2        // In the passive RAM buffer
    };

    enum class AssetPriority : U8
    {
        BACKGROUND = 0,  // Far objects, occluded
        NORMAL = 1,      // Standard scene content
        HIGH = 2,        // Avatar attachments, close objects
        CRITICAL = 3     // HUD elements, UI, active selection
    };

    struct CacheConfig
    {
        U64 ram_budget_bytes = MEGA_BYTES_TO_BYTES(1024);   // 1GB default RAM cache

        F32 ram_retention_time_seconds = 3.0f;   // KV:GC→RT Renamed from ram_grace_period_seconds
    };

    struct CacheEntry
    {
        LLUUID uuid;
        AssetLocation location = AssetLocation::NONE;

        U64 size_bytes = 0;              // Actual size in current location

        // Location-specific data
        void* texture_data = nullptr;    // Pointer to decoded texture data (RAM only)

        // Metadata
        U32 width = 0;
        U32 height = 0;
        U8 components = 0;
        S8 discard_level = -1;           // -1 = not loaded, 0 = full res

        AssetPriority priority = AssetPriority::NORMAL;

        F64 mAdmittedAt = 0.0;  // KV:RT Viewer uptime (seconds) when this entry was admitted — minimum retention time starts here

        CacheEntry() = default;

        ~CacheEntry()
        {
            // Free allocated texture data when entry is destroyed
            if (texture_data)
            {
                free(texture_data);
                texture_data = nullptr;
            }
        }
    };

    struct CacheStats
    {
        U64 ram_used_bytes = 0;
        U32 ram_entry_count = 0;

        U64 ram_hits = 0;
        U64 network_requests = 0;

        U64 ram_evictions = 0;
    };

    struct ExtendedStats
    {
        // Lifetime counters
        U64 total_ram_hits = 0;
        U64 total_ram_misses = 0;

        // Quality control
        U64 textures_rejected_incomplete = 0;  // Rejected due to high discard/invalid metadata
        U64 textures_rejected_corrupt = 0;     // Rejected due to size/dimension mismatch

        // Derived metrics
        F32 ram_hit_rate = 0.0f;        // Calculated from total hits/misses
        U64 disk_writes_buffered = 0;   // Number of textures buffered in RAM (delayed disk writes)
        U64 evictions_dropped_unwritten = 0;  // S24 : evicted over the disk write budget, freed without a write (filled in by getExtendedStats)
    };

public:
    // Initialization and configuration
    void initialize(const CacheConfig& config);
    void shutdown();

    // S24 shutdown flush / startup hot-load: a dedicated snapshot file
    // (kvramcache.bin, in LL_PATH_CACHE - entirely separate from
    // LLTextureCache's own on-disk cache, which this never touches) holding
    // every RAM-resident entry's raw, still-encoded J2C bytes plus its
    // width/height/components/discard - no decode on write, no decode on
    // read, no LLTextureCache write-path involvement at all. Without this,
    // shutdown() alone just frees the RAM with zero disk write-back, so the
    // next session's hot-load never has anything fresh to find.
    //
    // beginShutdownFlush() is idempotent and starts the real work on DXPool;
    // call it as early as possible (LLAppViewer::requestQuit()) so it runs
    // in parallel with the rest of the quit sequence. finishShutdownFlush()
    // waits (bounded by KVRAMCacheShutdownFlushTimeoutSec) for that work to
    // finish, then does the final clearAll() - call this as LATE as safely
    // possible (right before LLSingletonBase::deleteAll(), not at shutdown()'s
    // call site) so it gets the whole shutdown sequence's worth of time, not
    // just whatever's elapsed by the time shutdown() itself runs.
    void beginShutdownFlush();
    void finishShutdownFlush();

    // S24: loads the snapshot beginShutdownFlush()/finishShutdownFlush() wrote
    // last session, up to getMinDeckSize() entries, newest-first. Call from
    // LLAppViewer::init() between initCache() and initWindow() - no-ops
    // immediately if KVRAMCacheEnabled is false or no snapshot file exists.
    void loadFromDiskSnapshot();

    const CacheConfig& getConfig() const { return mConfig; }

    // Settings integration
    void initializeFromSettings();
    void updateFromSettings();

    // Frame update - called every frame for thread coordination and stats
    void updateFrame(F32 delta_time_seconds);

    // Thread control
    void startThread();
    void stopThread();

    // ===== PASSIVE BUFFER INTERFACE (Fetch Worker Integration) =====

    // Query Interface (Check before disk read)
    // Returns true if texture is in RAM cache
    bool hasTexture(const LLUUID& uuid);

    // Write Interface (Store when writing to disk cache)
    // Stores J2C compressed data along with decode metadata
    // Returns true if accepted (or false if cache full and can't evict)
    // Cache makes internal copy of texture_data - caller retains ownership
    // priority is caller-classified — see lltexturefetch.cpp's classify_kvram_priority().
    bool acceptEviction(const LLUUID& uuid, void* texture_data, U64 size_bytes, S32 discard_level,
                        U32 width, U32 height, S8 components,
                        AssetPriority priority);

    // Read Interface (Retrieve before disk read)
    // Returns J2C compressed data with full metadata
    // S24 : texture_data is a private 16-byte-aligned copy made under the cache lock (ll_aligned_malloc_16);
    // the caller owns it and frees it with ll_aligned_free_16, or hands it to LLImageFormatted::setData()
    // Returns false if not found or data is invalid
    bool getTexture(const LLUUID& texture_id, 
                    U8*& texture_data, 
                    U32& size_bytes, 
                    S32& discard_level,
                    U32& width,
                    U32& height,
                    S8& components);

    // ===== CONFIGURATION (Equilibrium Tuning) =====

    // S24: was clamped to (1,5000) here vs (100,15000) everywhere else that
    // sets mMinDeckSize - pre-existing inconsistency, fixed to match.
    void setMinDeckSize(U32 size) { mMinDeckSize = llclamp(size, 0U, 15000U); }

    F32 getSoftThreshold() const { return mSoftThreshold; }
    F32 getHardThreshold() const { return mHardThreshold; }
    F32 getRetentionTime() const { return mConfig.ram_retention_time_seconds; }  // KV:GC→RT Renamed from getGracePeriod
    U32 getMinDeckSize() const { return mMinDeckSize; }

    // ===== MANUAL CACHE CONTROL =====

    void remove(const LLUUID& uuid);  // Remove from all tiers
    void clearAll();                  // Nuclear option - clear everything

    // Statistics and debugging
    const CacheStats& getStats() const { return mStats; }
    ExtendedStats getExtendedStats() const;  // by value, copied under the cache lock
    void removeFromRAMLRU(const LLUUID& uuid);
    F32 getRAMPressure() const;  // Get current RAM pressure (0.0 - 1.0)
    // Eviction pressure multiplier (0.0-2.0) for UI display; shares
    // calculateEvictionPressureMultiplier() with processPassiveEviction() so
    // the UI can't drift from the real eviction curve.
    F32 getEvictionPressureMultiplier() const;
    void clampThresholds(); // S24 : keeps soft < hard < the 95% refusal line
    F32 getInflowMBPerSec() const { return (F32)(mInflowBytesPerSec.load(std::memory_order_relaxed) / 1048576.0); }
    S32 getEvictionScheme() const { return mEvictionScheme.load(std::memory_order_relaxed); }
    void resetStats();
    void dumpState() const;  // Debug logging

    // An evicted RAM entry still pending its on-disk write. `data` is a
    // malloc'd buffer whose ownership transfers to whoever drains this list —
    // must free() it exactly once, whether or not the write succeeds.
    struct PendingDiskWrite
    {
        LLUUID uuid;
        U8* data = nullptr;
        U64 size_bytes = 0;
        S32 discard_level = -1;
        U32 width = 0;
        U32 height = 0;
        S8 components = 0;
    };

    protected:
        // Passive eviction logic (time/pressure based) - called by worker thread
        void processPassiveEviction(F32 delta_time);
        // KV:RT Added retention_time/now params for minimum age check.
        // out_pending collects entries needing a disk write (see PendingDiskWrite);
        // appending is O(1), so the caller drains it after releasing mCacheMutex
        // without changing evictSlice()'s own locked-section cost.
        void evictSlice(U32 slice_size, F64 retention_time, F64 now, std::vector<PendingDiskWrite>& out_pending, bool emergency = false);

        // Decodes and writes one evicted entry to LLTextureCache::writeToCache().
        // Always frees item.data exactly once (decode failure or success);
        // call with no lock held — does real CPU decode work.
        void writeEvictedEntryToDisk(const PendingDiskWrite& item);

        // Single source of truth for the pressure->multiplier curve; shared by
        // processPassiveEviction (actual eviction) and getEvictionPressureMultiplier (UI display).
        F32 calculateEvictionPressureMultiplier(F32 pressure) const;

        // Utility
        void updateStats();

        void pushBackToPriorityDeques(const LLUUID& uuid, AssetPriority priority);  // KV:RT Re-insert young entries that aren't old enough to evict

        // Thread friendship
        friend class KVCacheThread;

    private:
    CacheConfig mConfig;
    CacheStats mStats;
    ExtendedStats mExtendedStats;

    // Threshold configuration (equilibrium tuning)
    F32 mSoftThreshold = 0.75f;
    F32 mHardThreshold = 0.90f;

    // FIFO configuration
    U32 mMinDeckSize = 5000;  // S24 : textures saved at shutdown and so the most that can be pre-loaded next start (0 = off, 100-15000 in 100 steps)

    // KV:RT Removed mLastBaselineEviction — retention time is per-entry now

    // Stats tracking
    F32 mEvictionRateAccumulator = 0.0f;
    F32 mEvictionRateTimer = 0.0f;

     // Per-frame eviction accumulator for delta_time-scaled smooth eviction.
     // Fractions from time-scaled slice sizes accumulate here; when >=1.0, one
     // extra texture is evicted and the accumulator decremented. This prevents
     // truncation loss at low frame rates.
     F32 mEvictionAccumulator = 0.0f;
 
    // Ghost Entry Hash Map - fast UUID → CacheEntry lookup
    std::unordered_map<LLUUID, std::unique_ptr<CacheEntry>> mGhostMap;

    // LRU queue (oldest at front)
    std::deque<LLUUID> mRAMLRU;
    // Priority-segmented LRU deques — evict from lowest priority first.
    std::deque<LLUUID> mRAMLRU_BACKGROUND;   // Evicted first
    std::deque<LLUUID> mRAMLRU_NORMAL;       // Default tier
    std::deque<LLUUID> mRAMLRU_HIGH;         // Evicted only after NORMAL+ below empty
    std::deque<LLUUID> mRAMLRU_CRITICAL;     // Evicted only as last resort

    // Thread safety
    mutable LLMutex mCacheMutex;

    // Initialization state
    bool mInitialized = false;

    // S24 shutdown flush state (see beginShutdownFlush()/finishShutdownFlush())
    std::atomic<bool> mShutdownFlushStarted{false};
    std::atomic<bool> mShutdownFlushDone{false};
    F64 mShutdownFlushDeadline = 0.0;

    // S24 eviction controller (KVRAMEvictionScheme 1): inflow sets how often a pass runs, the cache size
    // sets how big each pass is, and a pressure release valve drains hard when the cache is overwhelmed.
    static constexpr F64 EVICT_BATCH_FRACTION = 0.004;          // one pass moves this share of the budget
    static constexpr F64 EVICT_MIN_BATCH_BYTES = 1.0 * 1048576.0;
    static constexpr F64 EVICT_MAX_BATCH_BYTES = 32.0 * 1048576.0;
    static constexpr U32 EVICT_MIN_ENTRIES = 4;                 // per pass bounds: a pass can run on the main thread
    static constexpr U32 EVICT_MAX_ENTRIES = 48;
    static constexpr U32 EVICT_VALVE_MAX_ENTRIES = 192;
    static constexpr U32 EVICT_MAX_WRITES_PER_PASS = 24;        // the rest of a pass is freed without a disk write
    static constexpr F32 EVICT_MIN_INTERVAL = 0.1f;
    static constexpr F64 EVICT_INFLOW_TAU_SECONDS = 2.0;        // inflow average smoothing
    static constexpr F64 EVICT_OVERAGE_GAIN = 4.0;              // evict up to (1 + gain) x inflow at the hard threshold
    static constexpr F64 EVICT_FLOOR_FRACTION = 0.005;          // of the budget per second, once above the fill target
    static constexpr F64 VALVE_DRAIN_SECONDS = 10.0;            // at the hard threshold: be back at soft in this long
    static constexpr F64 VALVE_CRITICAL_DRAIN_SECONDS = 5.0;    // at 95%: same, twice as fast
    std::atomic<S32> mEvictionScheme{ 1 };                      // 0 = original count-based slices, 1 = controller
    std::atomic<F32> mNextEvictionInterval{ EVICTION_CHECK_INTERVAL };
    std::atomic<F64> mInflowBytesPerSec{ 0.0 };
    std::atomic<U64> mEvictionsDroppedUnwritten{ 0 };
    F64 mInflowAccumBytes = 0.0;   // under mCacheMutex: bytes accepted since the last pass
    F64 mInflowLastSample = 0.0;   // under mCacheMutex
    bool mCountInflow = true;      // under mCacheMutex: false while the startup snapshot loads

    // Frame timing
    F32 mTimeSinceLastEvictionCheck = 0.0f;  // KV:GC→RT Renamed from mTimeSinceLastGraceCheck
    static constexpr F32 EVICTION_CHECK_INTERVAL = 1.0f;  // KV:GC→RT Check eviction every second (was GRACE_CHECK_INTERVAL)

    // Worker thread
    KVCacheThread* mWorkerThread = nullptr;
};

#endif // LL_KVRAMCACHE_H
