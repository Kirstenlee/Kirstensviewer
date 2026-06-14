/**
 * @file llfloatercachestats.cpp
 * @brief Texture Cache Statistics floater - Sleek animated progress bars
 *
 * Copyright (C) 2024, Kirstens S24 Viewer
 */

#include "llviewerprecompiledheaders.h"

#include "llfloatercachestats.h"
#include "lltextbox.h"
#include "llprogressbar.h"
#include "lltexturecache.h"
#include "llappviewer.h"
#include "llviewercontrol.h"

LLFloaterCacheStats::LLFloaterCacheStats(const LLSD& key)
    : LLFloater(key)
    , mEntriesBar(nullptr)
    , mEntriesText(nullptr)
    , mMemoryBar(nullptr)
    , mMemoryText(nullptr)
    , mPressureBar(nullptr)
    , mPressureText(nullptr)
{
}

LLFloaterCacheStats::~LLFloaterCacheStats()
{
}

bool LLFloaterCacheStats::postBuild()
{
    // Get progress bars - with fallback checks
    mEntriesBar = findChild<LLProgressBar>("entries_bar");
    mMemoryBar = findChild<LLProgressBar>("memory_bar");
    mPressureBar = findChild<LLProgressBar>("pressure_bar");

    // Get text labels
    mEntriesText = findChild<LLTextBox>("entries_text");
    mMemoryText = findChild<LLTextBox>("memory_text");
    mPressureText = findChild<LLTextBox>("pressure_text");

    // Validate all widgets loaded
    if (!mEntriesBar || !mMemoryBar || !mPressureBar ||
        !mEntriesText || !mMemoryText || !mPressureText)
    {
        LL_WARNS() << "Failed to load cache stats widgets!" << LL_ENDL;
        return false;
    }

    LL_INFOS() << "Cache stats floater initialized successfully" << LL_ENDL;
    return true;
}

void LLFloaterCacheStats::draw()
{
    updateStats();
    LLFloater::draw();
}

void LLFloaterCacheStats::updateStats()
{
    // Safety check
    if (!mEntriesBar || !mMemoryBar || !mPressureBar ||
        !mEntriesText || !mMemoryText || !mPressureText)
    {
        return; // Widgets not loaded
    }

    static LLCachedControl<bool> texture_ram_enabled(gSavedSettings, "TextureCacheUseRAMBuffer", false);

    // Texture Cache Stats
    if (texture_ram_enabled && LLAppViewer::getTextureCache())
    {
        LLTextureCache* tex_cache = LLAppViewer::getTextureCache();

        /* Check if RAM buffer actually initialized
        if (!tex_cache->isRAMCacheInitialized())
        {
            // Setting enabled but buffer not created - restart required
            mEntriesBar->setValue(LLSD(0.0f));
            mEntriesText->setText(std::string("Restart Required"));

            mMemoryBar->setValue(LLSD(0.0f));
            mMemoryText->setText(std::string("Restart Required"));

            mPressureBar->setValue(LLSD(0.0f));
            mPressureText->setText(std::string("Restart Required"));
            mPressureText->setColor(LLColor4::grey);
        }
        else
        {
            // Buffer exists - show animated stats
            //U32 entries = tex_cache->getRAMCacheEntryCount();
            //S64 memory_used = tex_cache->getRAMCacheMemoryUsage();
            //S64 memory_max = tex_cache->getRAMCacheMaxMemory();
            //F32 pressure = tex_cache->getRAMCacheMemoryPressure();

            // Entry count bar (assume 10k entries = 100% for visualization)
            F32 entry_percent = llclamp((F32)entries / 10000.0f * 100.0f, 0.0f, 100.0f);
            mEntriesBar->setValue(LLSD(entry_percent));
            mEntriesText->setText(llformat("%u entries", entries));

            // Memory bar (actual usage vs max)
            F32 memory_percent = (memory_max > 0) ? llclamp((F32)memory_used / (F32)memory_max * 100.0f, 0.0f, 100.0f) : 0.0f;
            mMemoryBar->setValue(LLSD(memory_percent));
            mMemoryText->setText(llformat("%.1f MB / %.1f MB",
                                         memory_used / (1024.0 * 1024.0),
                                         memory_max / (1024.0 * 1024.0)));

            // Pressure bar
            mPressureBar->setValue(LLSD(pressure * 100.0f));

            // Color code pressure text: Green < 80%, Yellow 80-95%, Red > 95%
            LLColor4 pressure_color;
            if (pressure < 0.80f)
            {
                pressure_color = LLColor4::green;
            }
            else if (pressure < 0.95f)
            {
                pressure_color = LLColor4::yellow;
            }
            else
            {
                pressure_color = LLColor4::red;
            }

            mPressureText->setColor(pressure_color);
            mPressureText->setText(llformat("%.1f%%", pressure * 100.0f));
        }
    }
    else
    {
        // Disabled - show empty bars
        mEntriesBar->setValue(LLSD(0.0f));
        mEntriesText->setText(std::string("Disabled"));

        mMemoryBar->setValue(LLSD(0.0f));
        mMemoryText->setText(std::string("Disabled"));

        mPressureBar->setValue(LLSD(0.0f));
        mPressureText->setText(std::string("Disabled"));
        mPressureText->setColor(LLColor4::grey);
    } */
    }
}