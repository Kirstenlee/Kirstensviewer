/**
 * @file llfloatercachestats.h
 * @brief Texture Cache Statistics floater - Real-time monitoring with progress bars
 *
 * Copyright (C) 2024, Kirstens S24 Viewer
 */

#ifndef LL_LLFLOATERCACHESTATS_H
#define LL_LLFLOATERCACHESTATS_H

#include "llfloater.h"

class LLTextBox;
class LLProgressBar;

class LLFloaterCacheStats : public LLFloater
{
public:
    LLFloaterCacheStats(const LLSD& key);
    virtual ~LLFloaterCacheStats();

    /*virtual*/ bool postBuild();
    /*virtual*/ void draw();

private:
    void updateStats();

    // UI elements - Progress bars + text
    LLProgressBar* mEntriesBar;
    LLTextBox* mEntriesText;

    LLProgressBar* mMemoryBar;
    LLTextBox* mMemoryText;

    LLProgressBar* mPressureBar;
    LLTextBox* mPressureText;
};

#endif // LL_LLFLOATERCACHESTATS_H
