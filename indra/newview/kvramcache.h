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

        F32 ram_retention_time_seconds = 5.0f;   // KV:GC→RT Renamed from ram_grace_period_seconds
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
        // Per-frame counters (reset each frame)
        U32 ram_hits_this_frame = 0;
        U32 ram_misses_this_frame = 0;

        // Lifetime counters
        U64 total_ram_hits = 0;
        U64 total_ram_misses = 0;

        // Quality control
        U64 textures_rejected_incomplete = 0;  // Rejected due to high discard/invalid metadata
        U64 textures_rejected_corrupt = 0;     // Rejected due to size/dimension mismatch

        // Derived metrics
        F32 ram_hit_rate = 0.0f;        // Calculated from total hits/misses
        U64 disk_writes_buffered = 0;   // Number of textures buffered in RAM (delayed disk writes)
    };

public:
    // Initialization and configuration
    void initialize(const CacheConfig& config);
    void shutdown();
    void updateConfig(const CacheConfig& config);
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
    // texture_data points to cache-owned memory - caller must copy if retention needed
    // Returns false if not found or data is invalid
    bool getTexture(const LLUUID& texture_id, 
                    U8*& texture_data, 
                    U32& size_bytes, 
                    S32& discard_level,
                    U32& width,
                    U32& height,
                    S8& components);

    // Legacy simple interface (kept for compatibility)
    void* getTexture(const LLUUID& uuid);

    // ===== CONFIGURATION (Equilibrium Tuning) =====

    void setThresholds(F32 soft, F32 hard);
    void setRetentionTime(F32 seconds);  // KV:GC→RT Renamed from setGracePeriod
    void setMinDeckSize(U32 size) { mMinDeckSize = llclamp(size, 1U, 5000U); }

    F32 getSoftThreshold() const { return mSoftThreshold; }
    F32 getHardThreshold() const { return mHardThreshold; }
    F32 getRetentionTime() const { return mConfig.ram_retention_time_seconds; }  // KV:GC→RT Renamed from getGracePeriod
    U32 getMinDeckSize() const { return mMinDeckSize; }

    // ===== MANUAL CACHE CONTROL =====

    void remove(const LLUUID& uuid);  // Remove from all tiers
    void clearAll();                  // Nuclear option - clear everything

    // Statistics and debugging
    const CacheStats& getStats() const { return mStats; }
    const ExtendedStats& getExtendedStats() const { return mExtendedStats; }
    void removeFromRAMLRU(const LLUUID& uuid);
    F32 getRAMPressure() const;  // Get current RAM pressure (0.0 - 1.0)
    // Eviction pressure multiplier (0.0-2.0) for UI display; shares
    // calculateEvictionPressureMultiplier() with processPassiveEviction() so
    // the UI can't drift from the real eviction curve.
    F32 getEvictionPressureMultiplier() const;
    void resetExtendedStats();
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
        void evictSlice(U32 slice_size, F64 retention_time, F64 now, std::vector<PendingDiskWrite>& out_pending);

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
    U32 mMinDeckSize = 100;  // Minimum textures in deck (range 100-10000 in 100 steps)

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

    // Frame timing
    F32 mTimeSinceLastEvictionCheck = 0.0f;  // KV:GC→RT Renamed from mTimeSinceLastGraceCheck
    static constexpr F32 EVICTION_CHECK_INTERVAL = 1.0f;  // KV:GC→RT Check eviction every second (was GRACE_CHECK_INTERVAL)

    // Worker thread
    KVCacheThread* mWorkerThread = nullptr;
};

#endif // LL_KVRAMCACHE_H
