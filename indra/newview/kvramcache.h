/**
 * @file kvramcache.h
 * @brief One-way eviction pipeline cache for VRAM/RAM/DISK texture management
 *
 * Implements a unidirectional "waterfall" eviction system:
 * VRAM (Hot) → RAM (Warm) → DISK (Cold) → NULL (Deleted)
 * 
 * Tiered read preference searches from fastest to slowest:
 * Check VRAM → Check RAM → Check DISK → Request Network
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

#ifndef BYTES_TO_MEGA_BYTES
#define BYTES_TO_MEGA_BYTES(x) ((x) >> 20)
#endif
#ifndef MEGA_BYTES_TO_BYTES
#define MEGA_BYTES_TO_BYTES(x) (((U64)(x)) << 20)  // Cast to U64 to avoid overflow for values >4GB
#endif

class LLImageRaw;
class LLImageGL;
class KVCacheThread;

class KVRAMCache : public LLSingleton<KVRAMCache>
{
    LLSINGLETON(KVRAMCache);
    virtual ~KVRAMCache();

public:
    enum class AssetLocation : U8
    {
        NONE = 0,      // Not in cache, needs network fetch
        DISK = 1,      // Cold storage - persistent across sessions
        RAM = 2,       // Warm standby - fast promotion to VRAM
        VRAM = 3       // Hot - actively bound to GPU
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
        U64 vram_budget_bytes = MEGA_BYTES_TO_BYTES(2048);  // 2GB default VRAM budget
        U64 ram_budget_bytes = MEGA_BYTES_TO_BYTES(1024);   // 1GB default RAM cache
        U64 disk_budget_bytes = MEGA_BYTES_TO_BYTES(4096);  // 4GB default disk cache

        F32 ram_grace_period_seconds = 30.0f;  // How long RAM cache holds before disk eviction
        F32 vram_pressure_threshold = 0.85f;   // Start eviction at 85% VRAM usage

        bool enable_disk_cache = true;
        std::string disk_cache_path;  // Set from user preferences
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
        U64 vram_used_bytes = 0;
        U64 ram_used_bytes = 0;
        U64 disk_used_bytes = 0;

        U32 vram_entry_count = 0;
        U32 ram_entry_count = 0;
        U32 disk_entry_count = 0;

        U64 vram_hits = 0;
        U64 ram_hits = 0;
        U64 disk_hits = 0;
        U64 network_requests = 0;

        U64 vram_evictions = 0;
        U64 ram_evictions = 0;
        U64 disk_evictions = 0;  // Tracks RAM entries dropped (grace period expired)
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

    // S24 STAGE 3: RAM Cache acts as fast intermediate between memory and disk
    // Stores J2C compressed texture data (same format as disk cache)
    // Natural flow: Memory → RAM Cache (J2C) → Disk Cache (J2C) → Network

    // Query Interface (Check before disk read)
    // Returns true if texture is in RAM cache
    bool hasTexture(const LLUUID& uuid);

    // Write Interface (Store when writing to disk cache)
    // Stores J2C compressed data along with decode metadata
    // Returns true if accepted (or false if cache full and can't evict)
    // Cache makes internal copy of texture_data - caller retains ownership
    bool acceptEviction(const LLUUID& uuid, void* texture_data, U64 size_bytes, S32 discard_level, 
                        U32 width, U32 height, S8 components);

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
    void setGracePeriod(F32 seconds);
    void setMinDeckSize(U32 size) { mMinDeckSize = llclamp(size, 1U, 5000U); }

    F32 getSoftThreshold() const { return mSoftThreshold; }
    F32 getHardThreshold() const { return mHardThreshold; }
    F32 getGracePeriod() const { return mConfig.ram_grace_period_seconds; }
    U32 getMinDeckSize() const { return mMinDeckSize; }

    // ===== MANUAL CACHE CONTROL =====

    void remove(const LLUUID& uuid);  // Remove from all tiers
    void clearAll();                  // Nuclear option - clear everything

    // Statistics and debugging
    const CacheStats& getStats() const { return mStats; }
    const ExtendedStats& getExtendedStats() const { return mExtendedStats; }
    F32 getRAMPressure() const;  // Get current RAM pressure (0.0 - 1.0)
    void resetExtendedStats();
    void resetStats();
    void dumpState() const;  // Debug logging

    protected:
        // Passive eviction logic (time/pressure based) - called by worker thread
        void processPassiveEviction(F32 delta_time);
        void evictSlice(U32 slice_size);  // Helper to evict N textures from front of deck

        // LRU queue management (FIFO deck)
        void removeFromRAMLRU(const LLUUID& uuid);

        // Utility
        void updateStats();

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

    // Unified time-based eviction: viewer uptime timestamp of last eviction (seconds since app start)
    F64 mLastBaselineEviction = 0.0;

    // Stats tracking
    F32 mEvictionRateAccumulator = 0.0f;
    F32 mEvictionRateTimer = 0.0f;

    // Ghost Entry Hash Map - fast UUID → CacheEntry lookup
    std::unordered_map<LLUUID, std::unique_ptr<CacheEntry>> mGhostMap;

    // LRU queues for hot tiers (oldest at front)
    std::deque<LLUUID> mVRAMLRU;
    std::deque<LLUUID> mRAMLRU;

    // Thread safety
    mutable LLMutex mCacheMutex;

    // Initialization state
    bool mInitialized = false;

    // Frame timing
    F32 mTimeSinceLastGraceCheck = 0.0f;
    static constexpr F32 GRACE_CHECK_INTERVAL = 1.0f;  // Check grace periods every second

    // Worker thread
    KVCacheThread* mWorkerThread = nullptr;
};

#endif // LL_KVRAMCACHE_H
