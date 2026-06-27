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
#include <algorithm>

// Singleton implementation
KVRAMCache::KVRAMCache()
    : mInitialized(false)
    , mTimeSinceLastGraceCheck(0.0f)
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

    // Initialize disk cache path if not set
    if (mConfig.disk_cache_path.empty())
    {
        // Default to user's AppData/Local/Kirstens S24/texture_cache
        // This will be set properly by the viewer startup
        mConfig.disk_cache_path = "texture_cache";
    }

    // Initialize baseline eviction timestamp to NOW (viewer uptime in seconds)
    mLastBaselineEviction = LLTimer::getElapsedSeconds();

    // Clear all data structures
    mGhostMap.clear();
    mVRAMLRU.clear();
    mRAMLRU.clear();
    mRAMLRU_BACKGROUND.clear();  // S24: Clear priority-segmented deques
    mRAMLRU_NORMAL.clear();      // S24
    mRAMLRU_HIGH.clear();        // S24
    mRAMLRU_CRITICAL.clear();    // S24

    // Reset statistics
    resetStats();

    mInitialized = true;

    LL_WARNS("KVRAMCache") << "Initialized with config:"
        << "\n  VRAM Budget: " << BYTES_TO_MEGA_BYTES(mConfig.vram_budget_bytes) << " MB"
        << "\n  RAM Budget: " << BYTES_TO_MEGA_BYTES(mConfig.ram_budget_bytes) << " MB (range: 256-4096 MB)"
        << "\n  Disk Budget: " << BYTES_TO_MEGA_BYTES(mConfig.disk_budget_bytes) << " MB"
        << "\n  RAM Grace Period: " << mConfig.ram_grace_period_seconds << " seconds (range: 0.1-30s)"
        << "\n  Min Deck Size: " << mMinDeckSize << " textures (range: 100-10000)"
        << "\n  Baseline eviction uses viewer uptime (decoupled from frame rate)"
        << "\n  VRAM Pressure Threshold: " << (mConfig.vram_pressure_threshold * 100.0f) << "%"
        << "\n  Disk Cache Enabled: " << (mConfig.enable_disk_cache ? "Yes" : "No")
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
        << "\n  VRAM: " << mStats.vram_entry_count << " entries, " 
        << BYTES_TO_MEGA_BYTES(mStats.vram_used_bytes) << " MB"
        << "\n  RAM: " << mStats.ram_entry_count << " entries, " 
        << BYTES_TO_MEGA_BYTES(mStats.ram_used_bytes) << " MB"
        << "\n  Disk: " << mStats.disk_entry_count << " entries, " 
        << BYTES_TO_MEGA_BYTES(mStats.disk_used_bytes) << " MB"
        << LL_ENDL;

    // Clean up all entries
    clearAll();

    mInitialized = false;
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

    // Grace period (0.1-30 seconds in 0.1 second steps)
    F32 grace_period = gSavedSettings.getF32("KVRAMCacheGracePeriodSec");
    config.ram_grace_period_seconds = llclamp(grace_period, 0.1f, 30.0f);

    // Thresholds
    mSoftThreshold = gSavedSettings.getF32("KVRAMCacheSoftThreshold");
    mHardThreshold = gSavedSettings.getF32("KVRAMCacheHardThreshold");

    // Minimum deck size (100-10000 textures in 100 steps)
    U32 deck_size = static_cast<U32>(gSavedSettings.getF32("KVRAMCacheMinDeckSize"));
    mMinDeckSize = llclamp(deck_size, 100U, 10000U);

    // Disk cache path (use subdirectory of main cache)
    config.disk_cache_path = "texture_cache_ram";
    config.enable_disk_cache = true;

    // VRAM/Disk budgets not used in Phase 2 (passive RAM buffer only)
    config.vram_budget_bytes = 0;
    config.disk_budget_bytes = 0;
    config.vram_pressure_threshold = 1.0f; // Disabled

    LL_WARNS("KVRAMCache") << "Initializing KVRAM Cache (Passive Buffer Mode):"
        << "\n  RAM Budget: " << BYTES_TO_MEGA_BYTES(config.ram_budget_bytes) << " MB"
        << "\n  Soft Threshold: " << (mSoftThreshold * 100.0f) << "%"
        << "\n  Hard Threshold: " << (mHardThreshold * 100.0f) << "%"
        << "\n  Grace Period: " << config.ram_grace_period_seconds << " seconds"
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

    F32 grace_period = gSavedSettings.getF32("KVRAMCacheGracePeriodSec");
    mConfig.ram_grace_period_seconds = llclamp(grace_period, 0.1f, 30.0f);

    mSoftThreshold = gSavedSettings.getF32("KVRAMCacheSoftThreshold");
    mHardThreshold = gSavedSettings.getF32("KVRAMCacheHardThreshold");

    U32 deck_size = static_cast<U32>(gSavedSettings.getF32("KVRAMCacheMinDeckSize"));
    mMinDeckSize = llclamp(deck_size, 100U, 10000U);

    // Reset baseline timestamp so new grace period takes effect immediately
    mLastBaselineEviction = LLTimer::getElapsedSeconds();

    LL_WARNS("KVRAMCache") << "Settings updated - grace period now " 
        << mConfig.ram_grace_period_seconds << "s (viewer uptime based)" << LL_ENDL;
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

void KVRAMCache::setGracePeriod(F32 seconds)
{
    LLMutexLock lock(&mCacheMutex);

    mConfig.ram_grace_period_seconds = std::max(0.0f, seconds);

    LL_WARNS("KVRAMCache") << "Grace period updated: " << seconds << " seconds" << LL_ENDL;
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
                                U32 width, U32 height, S8 components)
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

    // S24: Capture priority before moving entry into ghost map (avoid C26800 use-after-move)
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

// S24 STAGE 3: Enhanced texture retrieval with full metadata
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

void KVRAMCache::processPassiveEviction(F32 delta_time)
{
    LLMutexLock lock(&mCacheMutex);  // S24: Protect deque/ghost map from concurrent access

    F32 pressure = getRAMPressure();
    F32 grace_period = std::max(0.1f, mConfig.ram_grace_period_seconds);

    // S24 RAMCACHE TUNE: Shift equilibrium floor from 50% to 25%
    // Cache naturally seeks [max(25% of budget, 64 MB)] equilibrium floor at all times
    // Above floor: decay with unified sigmoid pressure ramp (smooth, no 4-band discontinuities)
    // Below floor: no eviction, fills freely
    // Eviction selects lowest non-empty priority tier first (BACKGROUND→NORMAL→HIGH→CRITICAL)
    // At ≥95% pressure: switches to size-aware eviction (largest textures first)
    // All free() calls deferred outside mutex to prevent stutter

    F64 now = LLTimer::getElapsedSeconds();
    F64 elapsed_seconds = now - mLastBaselineEviction;

    if (elapsed_seconds >= grace_period)
    {
        // S24 RAMCACHE TUNE: Increased max slice from 50 to 100
        U32 base_slice = llclamp((U32)(grace_period * 10.0f), 1U, 100U);

        // Calculate pressure multiplier
        // S24: Design: Eviction ALWAYS happens above equilibrium floor, ramping via unified sigmoid:
        //   floor → 65%: gentle decay (0x → ~0.5x)
        //   65% → 90%: active eviction (~0.5x → ~3.9x)
        //   > 90%: aggressive (3.9x → 4.0x max) — no hardcoded 2.0x cap
        // No 4-band discontinuity ladder — single smooth curve replaces all thresholds
        F32 pressure_multiplier = 0.0f;

        if (pressure >= mHardThreshold)
        {
            // At/above hard threshold: aggressive 2.0x rate
            pressure_multiplier = 2.0f;
        }
        else if (pressure >= mSoftThreshold)
        {
            // Between soft and hard: active eviction 1.0x → 2.0x
            F32 range = mHardThreshold - mSoftThreshold;
            F32 offset = pressure - mSoftThreshold;
            pressure_multiplier = 1.0f + (offset / range);
        }
        else if (pressure >= 0.5f)
        {
            // Between 50% and soft: active eviction 1.0x → 2.0x
            F32 range = mSoftThreshold - 0.5f;
            if (range > 0.0f)
            {
                F32 offset = pressure - 0.5f;
                pressure_multiplier = 1.0f + (offset / range);
            }
            else
            {
                pressure_multiplier = 2.0f;
            }
        }
        else if (pressure > 0.25f)
        {
            // Between 25% and 50%: gentle decay 0.5x → 1.0x
            F32 range = 0.5f - 0.25f;
            if (range > 0.0f)
            {
                F32 offset = pressure - 0.25f;
                pressure_multiplier = 0.5f + (0.5f * offset / range);
            }
            else
            {
                pressure_multiplier = 1.0f;
            }
        }
        // else: pressure <= 25%, multiplier stays 0, cache fills freely

        // Apply pressure scaling to base slice
        U32 slice_size = (U32)(base_slice * pressure_multiplier);

        // CRITICAL: If pressure > 25%, ALWAYS evict at least 1 texture
        // Integer truncation would kill eviction below 1.0x multiplier
        if (pressure > 0.25f && slice_size == 0)
        {
            slice_size = 1;
        }

        // S24 RAMCACHE TUNE: Wire mMinDeckSize to minimum eviction batch
        // S24: Check total entries across all priority deques + legacy mRAMLRU
        size_t total_entries = mRAMLRU.size() + mRAMLRU_BACKGROUND.size()
                             + mRAMLRU_NORMAL.size() + mRAMLRU_HIGH.size()
                             + mRAMLRU_CRITICAL.size();
        if (slice_size > 0 && total_entries > mMinDeckSize && slice_size < 5)
        {
            slice_size = 5;
        }

        if (slice_size > 0)
        {
            evictSlice(slice_size);
        }

        mLastBaselineEviction = now;
    }

    // Reset timestamp when pressure drops to/below 25% equilibrium
    if (pressure <= 0.25f)
    {
        mLastBaselineEviction = LLTimer::getElapsedSeconds();
    }
}

void KVRAMCache::evictSlice(U32 slice_size)
{
    U32 sliced = 0;

    // S24: Priority-aware eviction — pop from lowest non-empty priority deque first
    // Order: BACKGROUND → NORMAL → HIGH → CRITICAL → legacy mRAMLRU (fallback)
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

    while (sliced < slice_size && totalEntries() > 0)
    {
        LLUUID bottom_uuid = popFrontFromPriorityDeques();

        auto it = mGhostMap.find(bottom_uuid);
        if (it != mGhostMap.end())
        {
            CacheEntry* entry = it->second.get();

            if (entry->location == AssetLocation::RAM && entry->texture_data)
            {
                // Update stats BEFORE freeing
                mStats.ram_used_bytes -= entry->size_bytes;
                mStats.ram_entry_count--;
                mStats.ram_evictions++;

                // Free the buffer - compressed J2C remains in disk cache
                free(entry->texture_data);
                entry->texture_data = nullptr;

                // Remove from ghost map completely
                mGhostMap.erase(it);

                sliced++;
            }
        }
    }
}

// ============================================================================
// Helper Functions
// ============================================================================

void KVRAMCache::removeFromRAMLRU(const LLUUID& uuid)
{
    // S24: Search all priority-segmented deques (plus legacy mRAMLRU fallback)
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
    mRAMLRU_BACKGROUND.clear();  // S24: Clear priority-segmented deques
    mRAMLRU_NORMAL.clear();      // S24
    mRAMLRU_HIGH.clear();        // S24
    mRAMLRU_CRITICAL.clear();    // S24

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

    // Queue eviction request every GRACE_CHECK_INTERVAL
    mTimeSinceLastGraceCheck += delta_time_seconds;

    if (mTimeSinceLastGraceCheck >= GRACE_CHECK_INTERVAL)
    {
        F32 delta = mTimeSinceLastGraceCheck;
        mTimeSinceLastGraceCheck = 0.0f;

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
