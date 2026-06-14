/**
 * @file llfloaterkvramcachestats.h
 * @brief KVRAM Cache statistics display floater
 *
 * $LicenseInfo:firstyear=2024&license=viewerlgpl$
 * Kirstens S24 Viewer Source Code
 * $/LicenseInfo$
 */

#ifndef LL_LLFLOATERKVRAMCACHESTATS_H
#define LL_LLFLOATERKVRAMCACHESTATS_H

#include "llfloater.h"

class LLProgressBar;
class LLTextBox;

class LLFloaterKVRAMCacheStats : public LLFloater
{
public:
    LLFloaterKVRAMCacheStats(const LLSD& key);
    virtual ~LLFloaterKVRAMCacheStats();

    bool postBuild() override;
    void draw() override;

private:
    void updateStats();

    // UI elements - streamlined to 3 essential bars
    LLProgressBar* mRAMUsageBar;     // RAM usage (MB / budget)
    LLProgressBar* mHitRateBar;      // Cache hit rate (%)
    LLProgressBar* mPressureBar;     // Eviction pressure gauge (0-2x multiplier)

    LLTextBox* mRAMUsageText;
    LLTextBox* mTextureCountText;
    LLTextBox* mHitRateText;
    LLTextBox* mPressureText;

    F32 mUpdateTimer;
    static const F32 UPDATE_INTERVAL;
};

#endif // LL_LLFLOATERKVRAMCACHESTATS_H
