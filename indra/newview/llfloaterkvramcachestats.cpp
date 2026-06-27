/**
 * @file llfloaterkvramcachestats.cpp
 * @brief KVRAM Cache statistics display floater
 *
 * $LicenseInfo:firstyear=2024&license=viewerlgpl$
 * Kirstens S24 Viewer Source Code
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llfloaterkvramcachestats.h"
#include "kvramcache.h"
#include "llprogressbar.h"
#include "lltextbox.h"
#include "lluictrlfactory.h"
#include "llviewercontrol.h"

const F32 LLFloaterKVRAMCacheStats::UPDATE_INTERVAL = 0.25f; // Update 4 times per second

LLFloaterKVRAMCacheStats::LLFloaterKVRAMCacheStats(const LLSD& key)
    : LLFloater(key)
    , mRAMUsageBar(nullptr)
    , mHitRateBar(nullptr)
    , mPressureBar(nullptr)
    , mRAMUsageText(nullptr)
    , mTextureCountText(nullptr)
    , mHitRateText(nullptr)
    , mPressureText(nullptr)
    , mUpdateTimer(0.0f)
{
}

LLFloaterKVRAMCacheStats::~LLFloaterKVRAMCacheStats()
{
}

bool LLFloaterKVRAMCacheStats::postBuild()
{
    // Get progress bars
    mRAMUsageBar = getChild<LLProgressBar>("ram_usage_bar");
    mHitRateBar = getChild<LLProgressBar>("hit_rate_bar");
    mPressureBar = getChild<LLProgressBar>("pressure_bar");

    // Get text boxes
    mRAMUsageText = getChild<LLTextBox>("ram_usage_text");
    mTextureCountText = getChild<LLTextBox>("texture_count_text");
    mHitRateText = getChild<LLTextBox>("hit_rate_text");
    mPressureText = getChild<LLTextBox>("pressure_text");

    return true;
}

void LLFloaterKVRAMCacheStats::draw()
{
    // Update stats periodically
    static LLCachedControl<F32> stats_interval(gSavedSettings, "KVRAMCacheStatsUpdateInterval");
    mUpdateTimer += LLFrameTimer::getFrameDeltaTimeF32();
    if (mUpdateTimer >= stats_interval())
    {
        updateStats();
        mUpdateTimer = 0.0f;
    }

    LLFloater::draw();
}

void LLFloaterKVRAMCacheStats::updateStats()
{
    if (!KVRAMCache::instanceExists() || !gSavedSettings.getBOOL("KVRAMCacheEnabled"))
    {
        // Cache not initialized
        if (mRAMUsageText)
        {
            mRAMUsageText->setText(std::string("KVRAM Cache Disabled"));
        }
        return;
    }

    KVRAMCache* cache = KVRAMCache::getInstance();
    auto stats = cache->getStats();
    auto extended_stats = cache->getExtendedStats();
    F32 ram_pressure = cache->getRAMPressure();

    // === BAR 1: RAM USAGE ===
    // S24: Dynamic color: blue → purple → bright red based on RAM pressure
    if (mRAMUsageBar)
    {
        mRAMUsageBar->setValue(ram_pressure * 100.0f);

        // Color transitions based on RAM pressure
        LLColor4 ram_color;
        if (ram_pressure < 0.5f)
        {
            // Low usage (0-50%): Blue → Purple transition
            F32 blend = ram_pressure / 0.5f;  // 0→1
            ram_color = LLColor4(
                0.3f + (0.5f * blend),  // R: 0.3 → 0.8 (blue to purple)
                0.8f - (0.3f * blend),  // G: 0.8 → 0.5 (stay moderate)
                1.0f - (0.2f * blend),  // B: 1.0 → 0.8 (bright blue to purple)
                1.0f
            );
        }
        else
        {
            // High usage (50-100%): Purple → Bright Red transition
            F32 blend = (ram_pressure - 0.5f) / 0.5f;  // 0→1
            ram_color = LLColor4(
                0.8f + (0.2f * blend),  // R: 0.8 → 1.0 (purple to red)
                0.5f - (0.5f * blend),  // G: 0.5 → 0.0 (lose green)
                0.8f - (0.8f * blend),  // B: 0.8 → 0.0 (lose blue completely)
                1.0f
            );
        }
        mRAMUsageBar->setColorBar(ram_color);
    }
    if (mRAMUsageText)
    {
        U32 used_mb = static_cast<U32>(stats.ram_used_bytes / 1048576);
        U32 budget_mb = gSavedSettings.getU32("KVRAMCacheRAMBudgetMB");
        mRAMUsageText->setText(llformat("%u MB / %u MB", used_mb, budget_mb));
    }

    // Texture count + quality control stats
    if (mTextureCountText)
    {
        U64 total_rejected = extended_stats.textures_rejected_incomplete + extended_stats.textures_rejected_corrupt;
        if (total_rejected > 0)
        {
            mTextureCountText->setText(llformat("%u textures in RAM  |  %llu rejected (incomplete/corrupt)", 
                stats.ram_entry_count, total_rejected));
        }
        else
        {
            mTextureCountText->setText(llformat("%u textures in RAM", stats.ram_entry_count));
        }
    }

    // === BAR 2: HIT RATE ===
    // S24: Dynamic color: red → yellow → green based on cache hit rate
    F32 hit_rate = extended_stats.ram_hit_rate;
    if (mHitRateBar)
    {
        mHitRateBar->setValue(hit_rate * 100.0f);

        // Color transitions based on hit rate (inverted - low is bad, high is good)
        LLColor4 hit_color;
        if (hit_rate < 0.5f)
        {
            // Poor hit rate (0-50%): Red → Yellow transition
            F32 blend = hit_rate / 0.5f;  // 0→1
            hit_color = LLColor4(
                1.0f,                   // R: 1.0 (stay red)
                0.2f + (0.8f * blend),  // G: 0.2 → 1.0 (red to yellow)
                0.0f,                   // B: 0.0 (no blue)
                1.0f
            );
        }
        else
        {
            // Good hit rate (50-100%): Yellow → Green transition
            F32 blend = (hit_rate - 0.5f) / 0.5f;  // 0→1
            hit_color = LLColor4(
                1.0f - (0.7f * blend),  // R: 1.0 → 0.3 (yellow to green)
                1.0f,                   // G: 1.0 (stay bright)
                0.0f + (0.3f * blend),  // B: 0.0 → 0.3 (add slight blue for vibrant green)
                1.0f
            );
        }
        mHitRateBar->setColorBar(hit_color);
    }
    if (mHitRateText)
    {
        U64 total_hits = extended_stats.total_ram_hits;
        U64 total_queries = total_hits + extended_stats.total_ram_misses;
        mHitRateText->setText(llformat("%.1f%% (%llu hits / %llu queries)", 
            hit_rate * 100.0f, total_hits, total_queries));
    }

    // === BAR 3: PRESSURE GAUGE (repurposed equilibrium bar) ===
    // Shows current eviction pressure multiplier
    // Eviction starts at 1.0x (soft threshold) and ramps to 2.0x (hard threshold)
    // S24: Dynamic color: green → amber → red based on pressure
    F32 pressure_multiplier = 0.0f;
    F32 soft_threshold = gSavedSettings.getF32("KVRAMCacheSoftThreshold");
    F32 hard_threshold = gSavedSettings.getF32("KVRAMCacheHardThreshold");

    if (ram_pressure >= hard_threshold)
    {
        pressure_multiplier = 2.0f;
    }
    else if (ram_pressure >= soft_threshold)
    {
        // Active eviction zone: 1.0x → 2.0x
        F32 range = hard_threshold - soft_threshold;
        F32 offset = ram_pressure - soft_threshold;
        pressure_multiplier = 1.0f + (offset / range);
    }
    else if (ram_pressure >= 0.5f)
    {
        // Between 50% and soft: active eviction 1.0x → 2.0x
        F32 range = soft_threshold - 0.5f;
        if (range > 0.0f)
        {
            F32 offset = ram_pressure - 0.5f;
            pressure_multiplier = 1.0f + (offset / range);
        }
        else
        {
            pressure_multiplier = 2.0f;
        }
    }
    else if (ram_pressure > 0.25f)
    {
        // Between 25% and 50%: gentle decay 0.5x → 1.0x
        F32 range = 0.5f - 0.25f;
        if (range > 0.0f)
        {
            F32 offset = ram_pressure - 0.25f;
            pressure_multiplier = 0.5f + (0.5f * offset / range);
        }
        else
        {
            pressure_multiplier = 1.0f;
        }
    }
    // else: at/below 25% equilibrium, no eviction

    if (mPressureBar)
    {
        // Scale to 0-100 for bar display (multiplier 0-2 → 0-100%)
        mPressureBar->setValue(pressure_multiplier * 50.0f);

        // S24: Dynamic color based on pressure: green → amber → red
        LLColor4 bar_color;
        if (pressure_multiplier < 1.0f)
        {
            // Low pressure (0-1.0x): Green
            bar_color = LLColor4(0.3f, 1.0f, 0.3f, 1.0f);
        }
        else if (pressure_multiplier < 1.5f)
        {
            // Medium pressure (1.0-1.5x): Green → Amber transition
            F32 blend = (pressure_multiplier - 1.0f) / 0.5f;  // 0→1
            bar_color = LLColor4(
                0.3f + (0.7f * blend),  // R: 0.3 → 1.0 (green to amber)
                1.0f - (0.2f * blend),  // G: 1.0 → 0.8 (stay bright)
                0.3f - (0.3f * blend),  // B: 0.3 → 0.0 (remove blue)
                1.0f
            );
        }
        else
        {
            // High pressure (1.5-2.0x): Amber → Red transition
            F32 blend = (pressure_multiplier - 1.5f) / 0.5f;  // 0→1
            bar_color = LLColor4(
                1.0f,                   // R: 1.0 (stay red)
                0.8f - (0.6f * blend),  // G: 0.8 → 0.2 (amber to red)
                0.0f,                   // B: 0.0 (no blue)
                1.0f
            );
        }
        mPressureBar->setColorBar(bar_color);
    }
    if (mPressureText)
	{   // S24 avoid special characters in text!
        std::string status;
        if (pressure_multiplier < 0.15f) status = "Idle";
        else if (pressure_multiplier < 1.0f) status = "Decay";
        else if (pressure_multiplier < 1.5f) status = "Normal";
        else if (pressure_multiplier < 1.8f) status = "High";
        else status = "Aggressive";

        mPressureText->setText(llformat("%.2fx (%s)", pressure_multiplier, status.c_str()));
    }
}
