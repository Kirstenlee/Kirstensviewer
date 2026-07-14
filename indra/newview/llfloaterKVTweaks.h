/**
 * @file llfloaterKVTweaks.h
 * @brief KVTweaks - S24 comprehensive settings and features panel
 *
 * Copyright (c) 2025 Kirstenlee Cinquetti (Lee Quick)
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef LL_LLFLOATERKVTWEAKS_H
#define LL_LLFLOATERKVTWEAKS_H

#include "llfloater.h"

class LLFloaterKVTweaks : public LLFloater
{
public:
	LLFloaterKVTweaks(const LLSD& key);
	~LLFloaterKVTweaks();
   
    bool postBuild() override;
	
protected:
	void onClickClose();
	void onClickResetToDefaults();  // S24
	void onClickResetWater(); // S24 - Reset only water settings
	void onStreamingEngineChanged(); // S24 - Enable/disable appropriate EQ based on streaming engine
	void onVLCEQPresetChanged(); // S24 - Apply VLC EQ preset

	// S24 - Resets tab handlers
	void onClickPurgeTextureCache();
	void onClickPurgeShaderCache();
	void onClickPurgeCEFCache();
	void onClickPurgeLogs();
	void onClickFactoryReset();
	void onClickPurgeRAMCache();  // S24 - Purge KVRAM Cache

	// S24 - RAM Cache slider callbacks
	void onRAMCacheSettingChanged();  // Unified callback for all RAM cache settings
	void updateRAMCacheBudgetText();
	void updateRAMCacheSoftThresholdText();
	void updateRAMCacheHardThresholdText();
	void updateRAMCacheRetentionTimeText();  // KV:GC→RT Renamed from updateRAMCacheGracePeriodText
	void updateRAMCacheEvictionRateText();

	// S24 - Water preset handlers
	void onWaterPresetSelected();
	void onWaterPresetSave();
	void onWaterPresetDelete();
	void refreshWaterControls(); // Refresh all water tab sliders/controls
	void onWaterPresetListChange(); // Called when preset list changes (save/delete)

	// S24 - Movement tab handlers
	void onClickResetMovement();
	void updateGroundAccelText();
	void updateGroundDecelText();
	void updateFlightAccelText();
	void updateFlightDecelText();
	void updateFlightThresholdText();
	void updateFlightMinThresholdText();
};

#endif  // LL_LLFLOATERKVTWEAKS_H

