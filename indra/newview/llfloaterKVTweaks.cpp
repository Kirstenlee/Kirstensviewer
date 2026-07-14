/**
 * @file llfloaterKVTweaks.cpp
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

#include "llviewerprecompiledheaders.h"

#include "llfloaterKVTweaks.h"


#include "llviewercontrol.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "lluictrlfactory.h"
#include "llviewerwindow.h"
#include "llviewerdisplay.h"

#include "llui.h"
#include "llrender.h"

#include "llfloaterreg.h"

#include "llnotificationsutil.h"
#include "llselectmgr.h"
#include "llscrolllistctrl.h"
#include "llviewerobject.h"
#include <lltabcontainer.h>
#include "llcolorswatch.h"

// S24 - Includes for cache/directory operations
#include "lldir.h"
#include "llfile.h"
#include "lldiriterator.h"
#include "llappviewer.h"
#include "lltexturecache.h"
#include "llviewershadermgr.h"
#include "llagent.h"
#include "kvramcache.h"

// S24 - Include for water preset support
#include "llpresetsmanager.h"



LLFloaterKVTweaks::LLFloaterKVTweaks(const LLSD& key)
    : LLFloater(key)
{
    // Register the reset button callback
    mCommitCallbackRegistrar.add("ClickResetDefaults", boost::bind(&LLFloaterKVTweaks::onClickResetToDefaults, this));
}

bool LLFloaterKVTweaks::postBuild()
{
    // S24 - GUIDE button now opens S24 Engine Guide (handled in XML via Help.ShowS24EngineGuide)

    // Hook up streaming audio engine combo box to enable/disable correct EQ controls
    LLComboBox* streaming_engine = getChild<LLComboBox>("streaming_audio_engine");
    if (streaming_engine)
    {
        streaming_engine->setCommitCallback(boost::bind(&LLFloaterKVTweaks::onStreamingEngineChanged, this));
        // Initialize the EQ checkbox states based on current setting
        onStreamingEngineChanged();
    }

    // S24 - Hook up VLC EQ preset selector
    LLComboBox* vlc_eq_preset = getChild<LLComboBox>("vlc_eq_preset");
    if (vlc_eq_preset)
    {
        vlc_eq_preset->setCommitCallback(boost::bind(&LLFloaterKVTweaks::onVLCEQPresetChanged, this));
    }

    // S24 - Hook up Resets tab buttons
    childSetAction("reset_kvtweaks_btn", boost::bind(&LLFloaterKVTweaks::onClickResetToDefaults, this));
    childSetAction("purge_texture_cache_btn", boost::bind(&LLFloaterKVTweaks::onClickPurgeTextureCache, this));
    childSetAction("purge_shader_cache_btn", boost::bind(&LLFloaterKVTweaks::onClickPurgeShaderCache, this));
    childSetAction("purge_cef_cache_btn", boost::bind(&LLFloaterKVTweaks::onClickPurgeCEFCache, this));
    childSetAction("purge_logs_btn", boost::bind(&LLFloaterKVTweaks::onClickPurgeLogs, this));
    childSetAction("purge_ram_cache_btn", boost::bind(&LLFloaterKVTweaks::onClickPurgeRAMCache, this));
    childSetAction("factory_reset_btn", boost::bind(&LLFloaterKVTweaks::onClickFactoryReset, this));

    // S24 - Initialize water preset combo box
    LLComboBox* water_preset_combo = getChild<LLComboBox>("WaterPresetCombo");
    if (water_preset_combo)
    {
        // Create default presets and copy shipped presets on first run
        LLPresetsManager::getInstance()->createMissingDefault(PRESETS_WATER);

        // Populate combo box with available presets
        LLPresetsManager::getInstance()->setPresetNamesInComboBox(PRESETS_WATER, water_preset_combo, DEFAULT_TOP);

        // Select the active preset, or default to "Default" if none set
        std::string active_preset = gSavedSettings.getString("PresetWaterActive");
        if (active_preset.empty())
        {
            active_preset = PRESETS_DEFAULT;
            gSavedSettings.setString("PresetWaterActive", active_preset);
        }
        water_preset_combo->setSimple(active_preset);

        // Hook up the selection callback
        water_preset_combo->setCommitCallback(boost::bind(&LLFloaterKVTweaks::onWaterPresetSelected, this));

        // Listen for preset list changes (save/delete operations)
        LLPresetsManager::getInstance()->setPresetListChangeCallback(boost::bind(&LLFloaterKVTweaks::onWaterPresetListChange, this));
    }

    // Hook up water preset buttons
    childSetAction("WaterPresetSave", boost::bind(&LLFloaterKVTweaks::onWaterPresetSave, this));
    childSetAction("WaterPresetDelete", boost::bind(&LLFloaterKVTweaks::onWaterPresetDelete, this));

    // S24 - Hook up RAM Cache slider value text updates
    getChild<LLUICtrl>("kvram_ram_budget")->setCommitCallback(boost::bind(&LLFloaterKVTweaks::onRAMCacheSettingChanged, this));
    getChild<LLUICtrl>("kvram_soft_threshold")->setCommitCallback(boost::bind(&LLFloaterKVTweaks::onRAMCacheSettingChanged, this));
    getChild<LLUICtrl>("kvram_hard_threshold")->setCommitCallback(boost::bind(&LLFloaterKVTweaks::onRAMCacheSettingChanged, this));
    getChild<LLUICtrl>("kvram_retention_time")->setCommitCallback(boost::bind(&LLFloaterKVTweaks::onRAMCacheSettingChanged, this));  // KV:GC→RT Renamed from kvram_grace_period
    getChild<LLUICtrl>("kvram_min_deck_size")->setCommitCallback(boost::bind(&LLFloaterKVTweaks::onRAMCacheSettingChanged, this));

    // Initialize text values
    updateRAMCacheBudgetText();
    updateRAMCacheSoftThresholdText();
    updateRAMCacheHardThresholdText();
    updateRAMCacheRetentionTimeText();  // KV:GC→RT Renamed from updateRAMCacheGracePeriodText
    updateRAMCacheEvictionRateText();

    // S24 - Hook up Movement tab controls
    childSetAction("reset_movement_btn", boost::bind(&LLFloaterKVTweaks::onClickResetMovement, this));

    // Ground movement slider updates
    getChild<LLUICtrl>("smooth_movement_accel_time")->setCommitCallback(boost::bind(&LLFloaterKVTweaks::updateGroundAccelText, this));
    getChild<LLUICtrl>("smooth_movement_decel_time")->setCommitCallback(boost::bind(&LLFloaterKVTweaks::updateGroundDecelText, this));

    // Flight movement slider updates
    getChild<LLUICtrl>("smooth_flight_accel_time")->setCommitCallback(boost::bind(&LLFloaterKVTweaks::updateFlightAccelText, this));
    getChild<LLUICtrl>("smooth_flight_decel_time")->setCommitCallback(boost::bind(&LLFloaterKVTweaks::updateFlightDecelText, this));
    getChild<LLUICtrl>("smooth_flight_fast_threshold")->setCommitCallback(boost::bind(&LLFloaterKVTweaks::updateFlightThresholdText, this));
    getChild<LLUICtrl>("smooth_flight_min_threshold")->setCommitCallback(boost::bind(&LLFloaterKVTweaks::updateFlightMinThresholdText, this));

    // Initialize movement text values
    updateGroundAccelText();
    updateGroundDecelText();
    updateFlightAccelText();
    updateFlightDecelText();
    updateFlightThresholdText();
    updateFlightMinThresholdText();

    // S24 - Beam color is controlled in Preferences → Colors → My Effects (no duplicate here)

    return true;
}


void LLFloaterKVTweaks::onStreamingEngineChanged()
{
    // S24 - Enable the appropriate EQ controls based on streaming engine selection
    // FMOD (value 1) uses 3-band EQ
    // MediaPlugins/VLC (value 0) uses 10-band EQ

    LLComboBox* streaming_engine = getChild<LLComboBox>("streaming_audio_engine");
    LLCheckBoxCtrl* fmod_eq_checkbox = getChild<LLCheckBoxCtrl>("audio_eq_enabled");
    LLCheckBoxCtrl* vlc_eq_checkbox = getChild<LLCheckBoxCtrl>("vlc_audio_eq_enabled");

    if (streaming_engine && fmod_eq_checkbox && vlc_eq_checkbox)
    {
        S32 engine_value = streaming_engine->getCurrentIndex();
        bool is_fmod = (engine_value == 1); // 1 = FMOD, 0 = MediaPlugins/VLC

        // Enable FMOD 3-band EQ only when FMOD is selected
        fmod_eq_checkbox->setEnabled(is_fmod);

        // Enable VLC 10-band EQ only when MediaPlugins/VLC is selected
        vlc_eq_checkbox->setEnabled(!is_fmod);

        // Uncheck the disabled EQ to avoid confusion
        if (is_fmod)
        {
            vlc_eq_checkbox->set(false);
        }
        else
        {
            fmod_eq_checkbox->set(false);
        }
    }
}


void LLFloaterKVTweaks::onVLCEQPresetChanged()
{
    // S24 - Apply VLC 10-band EQ presets
    // Bands: 60Hz, 170Hz, 310Hz, 600Hz, 1kHz, 3kHz, 6kHz, 12kHz, 14kHz, 16kHz

    LLComboBox* preset_combo = getChild<LLComboBox>("vlc_eq_preset");
    if (!preset_combo)
        return;

    S32 preset = preset_combo->getCurrentIndex();
    F32 bands[10] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    F32 preamp = 0.0f;

    switch (preset)
    {
        case 0: // Flat (Manual) - all zeros, user adjusts
            break;

        case 1: // Rock - Enhanced bass and treble, scooped mids
            bands[0] = 5.0f;   // 60Hz
            bands[1] = 3.0f;   // 170Hz
            bands[2] = -3.0f;  // 310Hz
            bands[3] = -4.0f;  // 600Hz
            bands[4] = -2.0f;  // 1kHz
            bands[5] = 2.0f;   // 3kHz
            bands[6] = 4.0f;   // 6kHz
            bands[7] = 5.0f;   // 12kHz
            bands[8] = 5.0f;   // 14kHz
            bands[9] = 5.0f;   // 16kHz
            preamp = 7.0f;     // Boosted from 2 (+5)
            break;

        case 2: // Pop - Vocal presence, smooth bass
            bands[0] = 2.0f;   // 60Hz
            bands[1] = 2.0f;   // 170Hz
            bands[2] = 0.0f;   // 310Hz
            bands[3] = 1.0f;   // 600Hz
            bands[4] = 3.0f;   // 1kHz
            bands[5] = 3.0f;   // 3kHz
            bands[6] = 2.0f;   // 6kHz
            bands[7] = 1.0f;   // 12kHz
            bands[8] = 1.0f;   // 14kHz
            bands[9] = 0.0f;   // 16kHz
            preamp = 8.0f;     // Boosted from 3 (+5)
            break;

        case 3: // Jazz - Warm mids, controlled bass and treble
            bands[0] = 2.0f;   // 60Hz
            bands[1] = 1.0f;   // 170Hz
            bands[2] = 2.0f;   // 310Hz
            bands[3] = 3.0f;   // 600Hz
            bands[4] = 2.0f;   // 1kHz
            bands[5] = 1.0f;   // 3kHz
            bands[6] = 0.0f;   // 6kHz
            bands[7] = 1.0f;   // 12kHz
            bands[8] = 2.0f;   // 14kHz
            bands[9] = 2.0f;   // 16kHz
            preamp = 7.0f;     // Boosted from 3 (+4)
            break;

        case 4: // Classical - Natural, slight mid boost for clarity
            bands[0] = 0.0f;   // 60Hz
            bands[1] = 0.0f;   // 170Hz
            bands[2] = 1.0f;   // 310Hz
            bands[3] = 2.0f;   // 600Hz
            bands[4] = 2.0f;   // 1kHz
            bands[5] = 1.0f;   // 3kHz
            bands[6] = 0.0f;   // 6kHz
            bands[7] = 0.0f;   // 12kHz
            bands[8] = 1.0f;   // 14kHz
            bands[9] = 2.0f;   // 16kHz
            preamp = 7.0f;     // Boosted from 3 (+4)
            break;

        case 5: // Dance - Strong bass, crisp highs
            bands[0] = 6.0f;   // 60Hz
            bands[1] = 5.0f;   // 170Hz
            bands[2] = 3.0f;   // 310Hz
            bands[3] = 0.0f;   // 600Hz
            bands[4] = 0.0f;   // 1kHz
            bands[5] = 1.0f;   // 3kHz
            bands[6] = 3.0f;   // 6kHz
            bands[7] = 4.0f;   // 12kHz
            bands[8] = 4.0f;   // 14kHz
            bands[9] = 4.0f;   // 16kHz
            preamp = 6.0f;     // Boosted from 1 (+5)
            break;

        case 6: // Bass Boost - Sub and low bass emphasis
            bands[0] = 8.0f;   // 60Hz
            bands[1] = 6.0f;   // 170Hz
            bands[2] = 4.0f;   // 310Hz
            bands[3] = 2.0f;   // 600Hz
            bands[4] = 0.0f;   // 1kHz
            bands[5] = 0.0f;   // 3kHz
            bands[6] = 0.0f;   // 6kHz
            bands[7] = 0.0f;   // 12kHz
            bands[8] = 0.0f;   // 14kHz
            bands[9] = 0.0f;   // 16kHz
            preamp = 4.0f;     // Boosted from 0 (+4)
            break;

        case 7: // Vocal Boost - Midrange clarity for voice
            bands[0] = -1.0f;  // 60Hz
            bands[1] = -2.0f;  // 170Hz
            bands[2] = 0.0f;   // 310Hz
            bands[3] = 3.0f;   // 600Hz
            bands[4] = 5.0f;   // 1kHz
            bands[5] = 5.0f;   // 3kHz
            bands[6] = 3.0f;   // 6kHz
            bands[7] = 1.0f;   // 12kHz
            bands[8] = 0.0f;   // 14kHz
            bands[9] = -1.0f;  // 16kHz
            preamp = 9.0f;     // Boosted from 4 (+5)
            break;

        case 8: // Treble Boost - Brightness and air
            bands[0] = 0.0f;   // 60Hz
            bands[1] = 0.0f;   // 170Hz
            bands[2] = 0.0f;   // 310Hz
            bands[3] = 0.0f;   // 600Hz
            bands[4] = 1.0f;   // 1kHz
            bands[5] = 3.0f;   // 3kHz
            bands[6] = 5.0f;   // 6kHz
            bands[7] = 7.0f;   // 12kHz
            bands[8] = 8.0f;   // 14kHz
            bands[9] = 8.0f;   // 16kHz
            preamp = 6.0f;     // Boosted from 1 (+5)
            break;

        case 9: // Full Bass & Treble - V-curve for powerful sound
            bands[0] = 7.0f;   // 60Hz
            bands[1] = 5.0f;   // 170Hz
            bands[2] = 2.0f;   // 310Hz
            bands[3] = 0.0f;   // 600Hz
            bands[4] = -2.0f;  // 1kHz
            bands[5] = 0.0f;   // 3kHz
            bands[6] = 2.0f;   // 6kHz
            bands[7] = 5.0f;   // 12kHz
            bands[8] = 7.0f;   // 14kHz
            bands[9] = 7.0f;   // 16kHz
            preamp = 5.0f;     // Boosted from 0 (+5)
            break;
    }

    // Apply preset to settings
    gSavedSettings.setS32("VLCAudioEQPreset", preset); // Save selected preset
    gSavedSettings.setF32("VLCAudioEQPreamp", preamp);
    for (int i = 0; i < 10; ++i)
    {
        gSavedSettings.setF32(llformat("VLCAudioEQBand%d", i), bands[i]);
    }

    // Visual feedback via slider updates is sufficient - no popup needed
}


void LLFloaterKVTweaks::onClickResetToDefaults()
{
    // List of all control names used in the floater
    const char* control_names[] = {
        // Tab 1: CPU & Memory
        "EmulateCoreCount",
        "KVdefault_cores",
        "KVmin_cores",
        "KVmax_cores",
        "RenderCPUBasis",
        "MaxHeapSize64",
        "RenderMaxVRAMBudget",
        "RenderTextureVRAMDivisor",
        "RenderMinFreeMainMemoryThreshold",
        "NonvisibleObjectsInMemoryTime",
        "MainWorkTime",
        
        // Tab 2: Rendering
        "RenderVolumeLODFactor",
        "RenderAvatarLODFactor",
        "RenderTerrainLODFactor",
        "RenderTreeLODFactor",
        "RenderFlexTimeFactor",
        "RenderFarClip",
        "RenderAutoMuteRenderWeightLimit",
        "RenderAutoMuteSurfaceAreaLimit",
        "AvatarExtentRefreshPeriodBatch",
        "AvatarFeathering",
        "RenderHoverGlowEnable",
        "RenderCompressTextures",
        "RenderFlushOrphanedTexturesOnTeleport",
        "RenderHighlightFadeTime",
        "RenderHighlightBrightness",
        "RenderSSAOScale",
        "RenderSSAOMaxScale",
        "RenderSSAOFactor",
        "RenderSSAOEffect",
        "RenderWaterSSRIterations",
        "RenderWaterSSRRayStep",

        // Tab: Water (S24)
        "RenderWaterMetallic",
        "RenderWaterRoughnessOverride",
        "RenderWaterSpecularIntensity",
        "RenderWaterReflectionIntensity",
        // Tab: Water Advanced (S24 - Phase 1 & 2)
        "RenderWaterColorTintR",
        "RenderWaterColorTintG",
        "RenderWaterColorTintB",
        "RenderWaterColorTintA",
        "RenderWaterFresnelPower",
        "RenderWaterWaveSpeed",
        "RenderWaterShoreFadeDistance",
        "RenderWaterUnderwaterFogMult",
        "RenderWaterReflectionWarmth",

        // Tab 3: SSR
        "RenderScreenSpaceReflections",
        "RenderScreenSpaceReflectionIterations",
        "RenderScreenSpaceReflectionGlossySamples",
        "RenderScreenSpaceReflectionRayStep",
        "RenderScreenSpaceReflectionDistanceBias",
        "RenderScreenSpaceReflectionAdaptiveStepMultiplier",

        // Tab 4: Shadows
        "RenderShadowResolutionScale",
        "RenderShadowBias",
        "RenderShadowOffset",
        "RenderShadowSplitExponent",
        "RenderShadowProjOffset",
        "RenderShadowProjExponent",
        "RenderShadowNoise",
        "RenderShadowBlurSize",
        "RenderShadowBlurSamples",
        "RenderShadowBlurDistFactor",
        "RenderShadowThrottleEnabled",
        "RenderShadowThrottleSettleFrames",
        "RenderShadowBiasError",
        "RenderShadowOffsetError",
        "RenderSpotShadowBias",
        "RenderSpotShadowOffset",
        "RenderDeferredTreeShadowBias",
        "RenderDeferredTreeShadowOffset",

        // Tab 5: Lighting
        "RenderLocalLightCount",
        "RenderAttachedLights",
        "RenderSpotLightsInNondeferred",
        "RenderBakeSunlight",
        "RenderDeferredSpotShadowBias",
        "RenderDeferredSpotShadowOffset",
        "RenderLocalLightSmartCulling",
        "RenderLocalLightPriorityInnerRadius",
        "RenderLocalLightPriorityMidRadius",
        "RenderLocalLightFrustumCulling",
        "RenderLocalLightFrustumCullDistance",

        // Tab 6: Textures & VRAM
        "TextureDiscardLevel",
        "TextureLoadFullRes",
        "RenderMaxTextureResolution",
        "RenderObjectBump",
        "RenderBumpmapMinDistanceSquared",
        "RenderGlow",
        "RenderGlowResolutionPow",
        "S24VBOWorkQueueEnabled",
        "S24VBOWorkQueueThreadCount",
        "TextureFetchConcurrency",
        "TextureNewByteRange",
        "TextureReverseByteRange",
        "RenderHUDTexturesWarning",
        "RenderHUDOversizedTexturesWarning",
        "RenderHUDTexturesMemoryWarning",

        // Tab 7: Network
        "ThrottleBandwidthKBPS",
        "AckCollectTime",
        "PluginUseReadThread",
        "PluginInstancesCPULimit",

        // Tab 7: Network
        "ThrottleBandwidthKBPS",
        "AckCollectTime",
        "PluginUseReadThread",
        "PluginInstancesCPULimit",

        // Tab 8: Advanced
        "GLTFEnabled",
        "RenderMirrors",
        "RenderCubeMap",
        "HoverTextDistance",
        "HoverTextFadeDistance",

        // Tab 9: Reflections (S24)
        "RenderReflectionProbesEnabled",
        "RenderReflectionProbeIntensity",
        "RenderReflectionProbeSaturation",
        "RenderReflectionProbeContrast",
        "RenderReflectionProbeBlurLODBias",
        "RenderReflectionProbeAmbientMultiplier",
        "RenderPurgeShaderCacheOnExit",

        // Tab 10: Movement (S24)
        "SmoothKeyboardMovement",
        "SmoothMovementAccelTime",
        "SmoothMovementDecelTime",
        "SmoothFlightMovement",
        "SmoothFlightAccelTime",
        "SmoothFlightDecelTime",
        "SmoothFlightFastThreshold",
        "SmoothFlightMinThreshold",

        // Tab 11: Experimental (S24 - Selection Beam)
        "SelectionBeamStyle",
        "SelectionBeamLineStyle",
        "SelectionBeamParticleScale"
    };
    
    // Reset all controls to their defaults
    for (size_t i = 0; i < sizeof(control_names) / sizeof(control_names[0]); ++i)
    {
        LLControlVariable* control = gSavedSettings.getControl(control_names[i]);
        if (control)
        {
            control->resetToDefault(true);
        }
    }
    
    // Refresh the floater UI to show the reset values
    refresh();
}

void LLFloaterKVTweaks::onClickResetWater()
{
    // S24 - Reset only water-related controls to defaults
    const char* water_control_names[] = {
        // S24 Material controls
        "RenderWaterMetallic",
        "RenderWaterRoughnessOverride",
        "RenderWaterSpecularIntensity",
        "RenderWaterReflectionIntensity",
        // S24 Advanced artistic controls (Phase 1 & 2)
        "RenderWaterColorTintR",
        "RenderWaterColorTintG",
        "RenderWaterColorTintB",
        "RenderWaterColorTintA",
        "RenderWaterFresnelPower",
        "RenderWaterWaveSpeed",
        "RenderWaterShoreFadeDistance",
        "RenderWaterUnderwaterFogMult",
        "RenderWaterReflectionWarmth",
        // SSR duplicates (already in main reset, but include for completeness)
        "RenderWaterSSRIterations",
        "RenderWaterSSRRayStep"
    };

    // Reset water controls to their defaults
    for (size_t i = 0; i < sizeof(water_control_names) / sizeof(water_control_names[0]); ++i)
    {
        LLControlVariable* control = gSavedSettings.getControl(water_control_names[i]);
        if (control)
        {
            control->resetToDefault(true);
        }
    }

    // Refresh the floater UI to show the reset values
    refresh();
}

void LLFloaterKVTweaks::onClickClose()
{
    closeFloater();
}

// S24 - Resets tab handlers

void LLFloaterKVTweaks::onClickPurgeTextureCache()
{
    // S24 - Purge disk texture cache on next restart (uses standard viewer flag)
    // This is the safest approach - reuses the battle-tested cache purge system
    // used by Preferences > Advanced > Clear Cache button
    // 
    // Safety: RAM cache hot load has multiple protection layers:
    // - Checks if manifest file exists before attempting load
    // - Validates each texture file exists before reading
    // - Skips gracefully if disk cache is missing
    // 
    // When PurgeCacheOnNextStartup is set:
    // 1. On restart, purgeAllTextures(true) deletes ALL texture files AND manifest
    // 2. Empty cache structure is recreated
    // 3. loadSessionManifest() checks for manifest → not found → returns immediately
    // 4. Result: Clean initialization with no hot load attempts

    LLNotificationsUtil::add("S24PurgeTextureCache", LLSD(), LLSD(),
        [](const LLSD& notification, const LLSD& response)
        {
            if (LLNotificationsUtil::getSelectedOption(notification, response) == 0) // YES/OK
            {
                // Set the standard viewer flag for cache purge on next startup
                gSavedSettings.setBOOL("PurgeCacheOnNextStartup", TRUE);

                // Show confirmation that purge is scheduled
                LLNotificationsUtil::add("S24TextureCacheWillPurge");
            }
            return false;
        });
}

void LLFloaterKVTweaks::onClickPurgeShaderCache()
{
    // S24 - Purge shader cache immediately (confirmation dialog first)
    // Shaders automatically rebuild on next viewer launch
    // Uses existing LLViewerShaderMgr::purgeShaderCache() method
    // which safely deletes all files in shader_cache directory

    LLNotificationsUtil::add("S24PurgeShaderCacheConfirm", LLSD(), LLSD(),
        [](const LLSD& notification, const LLSD& response)
        {
            if (LLNotificationsUtil::getSelectedOption(notification, response) == 0) // OK
            {
                LL_WARNS() << "S24: User initiated shader cache purge" << LL_ENDL;

                // Delete shader cache files immediately
                LLViewerShaderMgr::purgeShaderCache();

                // Show confirmation notification with relog recommendation
                LLNotificationsUtil::add("S24ShaderCachePurged");

                LL_WARNS() << "S24: Shader cache purge completed" << LL_ENDL;
            }
            return false;
        });
}

void LLFloaterKVTweaks::onClickPurgeCEFCache()
{
    // S24 - Purge CEF (Chromium Embedded Framework) browser cache immediately
    // CEF cache automatically rebuilds when media plugins are used
    // Deletes entire cef_cache directory (same logic as LLAppViewer::purgeCefStaleCaches)

    LLNotificationsUtil::add("S24PurgeCEFCacheConfirm", LLSD(), LLSD(),
        [](const LLSD& notification, const LLSD& response)
        {
            if (LLNotificationsUtil::getSelectedOption(notification, response) == 0) // OK
            {
                LL_WARNS() << "S24: User initiated CEF cache purge" << LL_ENDL;

                // Get CEF cache directory path
                const std::string browser_cache_dir = gDirUtilp->getExpandedFilename(LL_PATH_CACHE, "cef_cache");

                if (LLFile::isdir(browser_cache_dir))
                {
                    // Delete entire CEF cache directory and contents
                    gDirUtilp->deleteDirAndContents(browser_cache_dir);
                    LL_WARNS() << "S24: Deleted CEF cache directory: " << browser_cache_dir << LL_ENDL;
                }
                else
                {
                    LL_WARNS() << "S24: CEF cache directory not found: " << browser_cache_dir << LL_ENDL;
                }

                // Show confirmation notification with relog recommendation
                LLNotificationsUtil::add("S24CEFCachePurged");

                LL_WARNS() << "S24: CEF cache purge completed" << LL_ENDL;
            }
            return false;
        });
}

void LLFloaterKVTweaks::onClickPurgeLogs()
{
    // S24 - Schedule log files purge on next startup
    // Logs cannot be deleted while viewer is running (active file handles)
    // On startup, before logging begins, all old logs will be deleted
    // This is the safest approach to avoid file locking issues

    LLNotificationsUtil::add("S24PurgeLogsConfirm", LLSD(), LLSD(),
        [](const LLSD& notification, const LLSD& response)
        {
            if (LLNotificationsUtil::getSelectedOption(notification, response) == 0) // OK
            {
                LL_WARNS() << "S24: User scheduled logs purge on next startup" << LL_ENDL;

                // Set flag to purge logs on next startup (before logging system initializes)
                gSavedSettings.setBOOL("PurgeLogsOnNextStartup", TRUE);

                // Show confirmation that purge is scheduled
                LLNotificationsUtil::add("S24LogsWillPurge");

                LL_WARNS() << "S24: Logs purge scheduled for next startup" << LL_ENDL;
            }
            return false;
        });
}

void LLFloaterKVTweaks::onClickFactoryReset()
{
    // S24 FACTORY RESET: The ultimate nuclear option
    // This will DELETE EVERYTHING - all caches, all settings, all logs
    // Executes via AresWin32 sentinel at absolute final shutdown
    // Show FINAL WARNING dialog with OK/Cancel

    LLNotificationsUtil::add("S24FactoryResetConfirm", LLSD(), LLSD(),
        [](const LLSD& notification, const LLSD& response)
        {
            S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
            if (option == 0) // YES, Factory Reset clicked
            {
                LL_WARNS() << "========================================" << LL_ENDL;
                LL_WARNS() << "S24 FACTORY RESET: User confirmed!" << LL_ENDL;
                LL_WARNS() << "S24 FACTORY RESET: Setting exit flag..." << LL_ENDL;
                LL_WARNS() << "========================================" << LL_ENDL;

                // Set flag for Ares to execute factory reset at final shutdown
                gSavedSettings.setBOOL("PerformFactoryResetOnExit", TRUE);

                // Show final notification
                LLNotificationsUtil::add("S24FactoryResetScheduled");

                // Force application quit immediately
                // This triggers the normal shutdown sequence which ends with Ares sentinel
                LL_WARNS() << "S24 FACTORY RESET: Forcing application quit..." << LL_ENDL;

                LLAppViewer::instance()->userQuit();
            }
            return false;
        });
}

void LLFloaterKVTweaks::onClickPurgeRAMCache()
{
    // S24 - Purge KVRAM Cache immediately (confirmation dialog first)
    // This clears the user-configurable texture buffer in RAM

    LLNotificationsUtil::add("S24PurgeRAMCacheConfirm", LLSD(), LLSD(),
        [](const LLSD& notification, const LLSD& response)
        {
            if (LLNotificationsUtil::getSelectedOption(notification, response) == 0) // OK
            {
                LL_WARNS() << "S24: User initiated KVRAM Cache purge" << LL_ENDL;

                // Clear the RAM cache immediately
                if (KVRAMCache::instanceExists())
                {
                    KVRAMCache::getInstance()->clearAll();
                    LLNotificationsUtil::add("S24RAMCachePurged");
                    LL_INFOS() << "S24: KVRAM Cache purged successfully" << LL_ENDL;
                }
                else
                {
                    LL_WARNS() << "S24: KVRAM Cache not initialized" << LL_ENDL;
                }
            }
            return false;
        });
}

// S24 - RAM Cache slider text updates
void LLFloaterKVTweaks::onRAMCacheSettingChanged()
{
    // Update text displays
    updateRAMCacheBudgetText();
    updateRAMCacheSoftThresholdText();
    updateRAMCacheHardThresholdText();
    updateRAMCacheRetentionTimeText();  // KV:GC→RT Renamed from updateRAMCacheGracePeriodText
    updateRAMCacheEvictionRateText();

    // Apply settings to cache immediately
    if (KVRAMCache::instanceExists())
    {
        KVRAMCache::getInstance()->updateFromSettings();
        LL_INFOS("KVTweaks") << "RAM Cache settings applied immediately" << LL_ENDL;
    }
}

void LLFloaterKVTweaks::updateRAMCacheBudgetText()
{
    U32 value = gSavedSettings.getU32("KVRAMCacheRAMBudgetMB");
    getChild<LLTextBox>("kvram_ram_budget_text")->setText(llformat("%u MB", value));
}

void LLFloaterKVTweaks::updateRAMCacheSoftThresholdText()
{
    F32 value = gSavedSettings.getF32("KVRAMCacheSoftThreshold");
    getChild<LLTextBox>("kvram_soft_threshold_text")->setText(llformat("%.0f%%", value * 100.0f));
}

void LLFloaterKVTweaks::updateRAMCacheHardThresholdText()
{
    F32 value = gSavedSettings.getF32("KVRAMCacheHardThreshold");
    getChild<LLTextBox>("kvram_hard_threshold_text")->setText(llformat("%.0f%%", value * 100.0f));
}

// KV:GC→RT Renamed from updateRAMCacheGracePeriodText
void LLFloaterKVTweaks::updateRAMCacheRetentionTimeText()
{
    F32 value = gSavedSettings.getF32("KVRAMCacheRetentionTimeSec");
    getChild<LLTextBox>("kvram_retention_time_text")->setText(llformat("%.0f sec", value));  // KV:GC→RT Renamed from kvram_grace_period_text
}

void LLFloaterKVTweaks::updateRAMCacheEvictionRateText()
{
    F32 value = gSavedSettings.getF32("KVRAMCacheMinDeckSize");
    getChild<LLTextBox>("kvram_min_deck_size_text")->setText(llformat("%.0f textures", value));
}

// S24 - Water preset handlers
void LLFloaterKVTweaks::onWaterPresetSelected()
{
    LLComboBox* combo = getChild<LLComboBox>("WaterPresetCombo");
    if (combo)
    {
        std::string selected = combo->getSimple();
        if (!selected.empty())
        {
            LL_INFOS() << "Loading water preset: " << selected << LL_ENDL;
            LLPresetsManager::getInstance()->loadPreset(PRESETS_WATER, selected);

            // Refresh all water controls to show new values
            refreshWaterControls();
        }
    }
}

void LLFloaterKVTweaks::refreshWaterControls()
{
    // List of all water control names that need refreshing
    const char* water_controls[] = {
        "RenderWaterMetallic",
        "RenderWaterRoughnessOverride",
        "RenderWaterReflectionIntensity",
        "RenderWaterSpecularIntensity",
        "RenderWaterColorTintR",
        "RenderWaterColorTintG",
        "RenderWaterColorTintB",
        "RenderWaterColorTintA",
        "RenderWaterFresnelPower",
        "RenderWaterWaveSpeed",
        "RenderWaterShoreFadeDistance",
        "RenderWaterUnderwaterFogMult",
        "RenderWaterReflectionWarmth",
        "RenderWaterSSRIterations",
        "RenderWaterSSRRayStep"
    };

    // Refresh each control from saved settings
    for (const char* control_name : water_controls)
    {
        LLControlVariable* control = gSavedSettings.getControl(control_name);
        if (control)
        {
            // Find the UI control and update it
            LLUICtrl* ui_ctrl = findChild<LLUICtrl>(control_name);
            if (ui_ctrl)
            {
                ui_ctrl->setValue(control->getValue());
            }
        }
    }

    LL_INFOS() << "Refreshed water controls from settings" << LL_ENDL;
}

void LLFloaterKVTweaks::onWaterPresetSave()
{
    // Open save dialog to get name for new preset
    LLFloaterReg::showInstance("save_pref_preset", PRESETS_WATER);
}

void LLFloaterKVTweaks::onWaterPresetDelete()
{
    LLComboBox* combo = getChild<LLComboBox>("WaterPresetCombo");
    if (combo)
    {
        std::string selected = combo->getSimple();
        if (!selected.empty() && selected != PRESETS_DEFAULT)
        {
            // Open delete confirmation dialog
            LLFloaterReg::showInstance("delete_pref_preset", PRESETS_WATER);
        }
        else if (selected == PRESETS_DEFAULT)
        {
            LLNotificationsUtil::add("CantDeleteDefaultPreset");
        }
    }
}

void LLFloaterKVTweaks::onWaterPresetListChange()
{
    // Refresh the combo box when presets are saved or deleted
    LLComboBox* combo = getChild<LLComboBox>("WaterPresetCombo");
    if (combo)
    {
        std::string current_selection = combo->getSimple();

        // Repopulate the combo box
        LLPresetsManager::getInstance()->setPresetNamesInComboBox(PRESETS_WATER, combo, DEFAULT_TOP);

        // Try to restore the previous selection, but fall back to Default if it no longer exists
        if (!current_selection.empty())
        {
            // Check if the previous selection still exists in the new list
            if (combo->selectByValue(current_selection))
            {
                // Selection was restored successfully
                LL_INFOS() << "Restored water preset selection: " << current_selection << LL_ENDL;
            }
            else
            {
                // Previous selection no longer exists (was deleted), fall back to Default
                combo->setSimple(PRESETS_DEFAULT);
                gSavedSettings.setString("PresetWaterActive", PRESETS_DEFAULT);

                // Load the default preset to reset water settings
                LLPresetsManager::getInstance()->loadPreset(PRESETS_WATER, PRESETS_DEFAULT);
                refreshWaterControls();

                LL_INFOS() << "Previous preset deleted, reverted to Default" << LL_ENDL;
            }
        }
    }
}


// S24 - Movement tab handlers
void LLFloaterKVTweaks::onClickResetMovement()
{
    // S24 - Reset all movement controls to defaults
    const char* movement_control_names[] = {
        "SmoothKeyboardMovement",
        "SmoothMovementAccelTime",
        "SmoothMovementDecelTime",
        "SmoothFlightMovement",
        "SmoothFlightAccelTime",
        "SmoothFlightDecelTime",
        "SmoothFlightFastThreshold",
        "SmoothFlightMinThreshold"
    };

    for (size_t i = 0; i < sizeof(movement_control_names) / sizeof(movement_control_names[0]); ++i)
    {
        LLControlVariable* control = gSavedSettings.getControl(movement_control_names[i]);
        if (control)
        {
            control->resetToDefault(true);
        }
    }

    // Refresh UI
    refresh();
}

void LLFloaterKVTweaks::updateGroundAccelText()
{
    F32 value = gSavedSettings.getF32("SmoothMovementAccelTime");
    getChild<LLTextBox>("ground_accel_value")->setText(llformat("%.2fs", value));
}

void LLFloaterKVTweaks::updateGroundDecelText()
{
    F32 value = gSavedSettings.getF32("SmoothMovementDecelTime");
    getChild<LLTextBox>("ground_decel_value")->setText(llformat("%.2fs", value));
}

void LLFloaterKVTweaks::updateFlightAccelText()
{
    F32 value = gSavedSettings.getF32("SmoothFlightAccelTime");
    getChild<LLTextBox>("flight_accel_value")->setText(llformat("%.2fs", value));
}

void LLFloaterKVTweaks::updateFlightDecelText()
{
    F32 value = gSavedSettings.getF32("SmoothFlightDecelTime");
    getChild<LLTextBox>("flight_decel_value")->setText(llformat("%.2fs", value));
}

void LLFloaterKVTweaks::updateFlightThresholdText()
{
    F32 value = gSavedSettings.getF32("SmoothFlightFastThreshold");
    getChild<LLTextBox>("flight_threshold_value")->setText(llformat("%.0f%%", value * 100.0f));
}

void LLFloaterKVTweaks::updateFlightMinThresholdText()
{
    F32 value = gSavedSettings.getF32("SmoothFlightMinThreshold");
    getChild<LLTextBox>("flight_min_threshold_value")->setText(llformat("%.0f%%", value * 100.0f));
}


LLFloaterKVTweaks::~LLFloaterKVTweaks()
{
    // Clean up resources if needed
}

