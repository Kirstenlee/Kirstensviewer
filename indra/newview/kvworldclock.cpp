/**
 * @file kvworldclock.cpp
 * @brief KV World Clock - standalone floater implementation.
 *
 * Copyright (c) 2026 Kirstenlee Cinquetti (Lee Quick)
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

#include "kvworldclock.h"

#include "llappviewer.h" // gFrameIntervalSeconds
#include "llbutton.h"
#include "llfontdx.h"
#include "llpanel.h"
#include "lltextbox.h"
#include "lluictrlfactory.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace
{
    const char* kTileBg = "worldclock/tile_bg.png";

    const S32 TILE_W = 160;
    const S32 TILE_H = 100;
    const S32 GAP = 10;
    const S32 COLS = 4;
    const S32 GRID_LEFT = 12;
    const S32 GRID_TOP_OFFSET = 30; // distance from the floater's TOP edge down to the grid panel's top
    const S32 BOTTOM_MARGIN = 12;

    const char* kMonthAbbrev[] = { "", "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };

    // S24: explicit UTF-8 byte escapes rather than a u8"" literal (char8_t
    // in C++20, won't implicitly convert to std::string) or a raw literal
    // emoji character (depends on the source file's interpreted encoding) -
    // this is correct regardless of either. Sun U+1F31E, crescent moon U+1F319.
    const char* kSunGlyph = "\xF0\x9F\x8C\x9E";
    const char* kMoonGlyph = "\xF0\x9F\x8C\x99";

    LLColor4 dim(const LLColor4& c, F32 factor)
    {
        return LLColor4(c.mV[0] * factor, c.mV[1] * factor, c.mV[2] * factor, c.mV[3]);
    }
}

KVFloaterWorldClock::KVFloaterWorldClock(const LLSD& key)
    : LLFloater(key)
{
    buildZones();
}

KVFloaterWorldClock::~KVFloaterWorldClock()
{
}

bool KVFloaterWorldClock::postBuild()
{
    mGridPanel = getChild<LLPanel>("grid_panel");
    buildTiles();
    updateClocks();
    return LLFloater::postBuild();
}

void KVFloaterWorldClock::draw()
{
    mUpdateAccum += gFrameIntervalSeconds;
    if (mUpdateAccum >= 1.0f)
    {
        mUpdateAccum = 0.f;
        updateClocks();
    }
    LLFloater::draw();
}

void KVFloaterWorldClock::buildZones()
{
    // S24: label, Windows time zone registry key name, tile accent colour, featured.
    // "SL Time" and "West Coast" intentionally share the same Pacific key -
    // SL's grid time IS Pacific, shown as its own gold-featured tile so
    // residents don't have to work that out for themselves.
    mZones.clear();
    mZones.push_back({ "UTC",          L"UTC",                            LLColor4(0.72f, 0.74f, 0.78f, 1.f), false });
    mZones.push_back({ "SL Time",      L"Pacific Standard Time",          LLColor4(1.00f, 0.80f, 0.16f, 1.f), true  });
    mZones.push_back({ "West Coast",   L"Pacific Standard Time",          LLColor4(0.30f, 0.56f, 0.90f, 1.f), false });
    mZones.push_back({ "East Coast",   L"Eastern Standard Time",          LLColor4(0.24f, 0.45f, 0.82f, 1.f), false });
    mZones.push_back({ "London",       L"GMT Standard Time",              LLColor4(0.76f, 0.22f, 0.24f, 1.f), false });
    mZones.push_back({ "Paris",        L"Romance Standard Time",          LLColor4(0.22f, 0.36f, 0.78f, 1.f), false });
    mZones.push_back({ "Amsterdam",    L"W. Europe Standard Time",        LLColor4(0.92f, 0.52f, 0.10f, 1.f), false });
    mZones.push_back({ "Moscow",       L"Russian Standard Time",          LLColor4(0.66f, 0.16f, 0.16f, 1.f), false });
    mZones.push_back({ "Tokyo",        L"Tokyo Standard Time",            LLColor4(0.86f, 0.22f, 0.34f, 1.f), false });
    mZones.push_back({ "Beijing",      L"China Standard Time",            LLColor4(0.82f, 0.16f, 0.16f, 1.f), false });
    mZones.push_back({ "Sydney",       L"AUS Eastern Standard Time",      LLColor4(0.15f, 0.58f, 0.46f, 1.f), false });
    mZones.push_back({ "Sao Paulo",    L"E. South America Standard Time", LLColor4(0.20f, 0.66f, 0.30f, 1.f), false });
}

void KVFloaterWorldClock::buildTiles()
{
    if (!mGridPanel)
    {
        return;
    }
    mGridPanel->deleteAllChildren();

    S32 count = (S32)mZones.size();
    S32 rows = (count + COLS - 1) / COLS;
    S32 gridWidth = COLS * TILE_W + (COLS - 1) * GAP;
    S32 gridHeight = rows * TILE_H + (rows - 1) * GAP;

    // S24: resize the floater first (same setShape-not-reshape reasoning as
    // KV Minesweeper's rebuildGrid() - keeps the title bar pinned on screen),
    // then place the grid panel and tiles relative to the new size.
    S32 floaterWidth = gridWidth + 2 * GRID_LEFT;
    S32 floaterHeight = GRID_TOP_OFFSET + gridHeight + BOTTOM_MARGIN;
    LLRect cur = getRect();
    LLRect target(cur.mLeft, cur.mTop, cur.mLeft + floaterWidth, cur.mTop - floaterHeight);
    setShape(target, false);

    LLRect panelRect(GRID_LEFT, floaterHeight - GRID_TOP_OFFSET,
                      GRID_LEFT + gridWidth, floaterHeight - GRID_TOP_OFFSET - gridHeight);
    mGridPanel->setShape(panelRect, false);

    mTiles.clear();
    mTiles.resize(count);

    for (S32 i = 0; i < count; ++i)
    {
        S32 row = i / COLS;
        S32 col = i % COLS;
        S32 localLeft = col * (TILE_W + GAP);
        S32 localRight = localLeft + TILE_W;
        S32 localTop = gridHeight - row * (TILE_H + GAP);
        S32 localBottom = localTop - TILE_H;

        const ClockZone& zone = mZones[i];
        ClockTile& tile = mTiles[i];

        LLButton::Params bg_p;
        bg_p.name(llformat("wc_bg_%d", i));
        bg_p.rect(LLRect(localLeft, localTop, localRight, localBottom));
        bg_p.image_unselected(LLUI::getUIImage(kTileBg));
        bg_p.image_selected(LLUI::getUIImage(kTileBg));
        bg_p.scale_image(true);
        bg_p.tab_stop(false);
        // S24: deliberately NOT disabled - LLButton::draw() substitutes a
        // fixed grey disabled_color for mImageColor whenever the control is
        // disabled, which silently defeats setColor() below. No click
        // callback is bound, so it stays functionally inert either way.
        tile.bg = LLUICtrlFactory::create<LLButton>(bg_p);
        mGridPanel->addChild(tile.bg);

        // S24: white text + soft drop shadow throughout - a fixed dark text
        // colour read fine on the bright "day" tint but went near-invisible
        // on the darkened "night" tint, since the background itself swings
        // from bright to dark. White+shadow holds up on both. Insets are
        // generous on every edge - text was clipping right against the
        // tile_bg.png border at the old tighter margins.
        const LLColor4 kTextColor(1.f, 1.f, 1.f, 1.f);

        LLTextBox::Params city_p;
        city_p.name(llformat("wc_city_%d", i));
        city_p.rect(LLRect(localLeft + 12, localTop - 10, localRight - 12, localTop - 28));
        city_p.font(zone.featured ? LLFontDX::getFontSansSerifBig() : LLFontDX::getFontSansSerif());
        city_p.font_shadow(LLFontDX::DROP_SHADOW_SOFT);
        city_p.initial_value(zone.label);
        tile.cityText = LLUICtrlFactory::create<LLTextBox>(city_p);
        tile.cityText->setColor(kTextColor);
        mGridPanel->addChild(tile.cityText);

        LLTextBox::Params time_p;
        time_p.name(llformat("wc_time_%d", i));
        time_p.rect(LLRect(localLeft + 6, localTop - 32, localRight - 6, localBottom + 28));
        time_p.font(LLFontDX::getFontSansSerifHuge());
        time_p.font_shadow(LLFontDX::DROP_SHADOW_SOFT);
        time_p.initial_value(std::string("--:--:--"));
        tile.timeText = LLUICtrlFactory::create<LLTextBox>(time_p);
        tile.timeText->setHAlign(LLFontDX::HCENTER);
        tile.timeText->setColor(kTextColor);
        mGridPanel->addChild(tile.timeText);

        LLTextBox::Params date_p;
        date_p.name(llformat("wc_date_%d", i));
        date_p.rect(LLRect(localLeft + 12, localBottom + 22, localRight - 30, localBottom + 6));
        date_p.font(LLFontDX::getFontMonospace());
        date_p.font_shadow(LLFontDX::DROP_SHADOW_SOFT);
        date_p.initial_value(std::string(""));
        tile.dateText = LLUICtrlFactory::create<LLTextBox>(date_p);
        tile.dateText->setColor(kTextColor);
        mGridPanel->addChild(tile.dateText);

        LLTextBox::Params glyph_p;
        glyph_p.name(llformat("wc_glyph_%d", i));
        glyph_p.rect(LLRect(localRight - 28, localBottom + 24, localRight - 8, localBottom + 4));
        glyph_p.font(LLFontDX::getFontEmojiSmall());
        glyph_p.font_shadow(LLFontDX::DROP_SHADOW_SOFT);
        glyph_p.initial_value(std::string(""));
        tile.glyphText = LLUICtrlFactory::create<LLTextBox>(glyph_p);
        tile.glyphText->setColor(kTextColor);
        mGridPanel->addChild(tile.glyphText);
    }
}

bool KVFloaterWorldClock::getLocalTimeFor(const std::wstring& tzKeyName, SYSTEMTIME& outLocal) const
{
    SYSTEMTIME utcNow;
    GetSystemTime(&utcNow);

    DYNAMIC_TIME_ZONE_INFORMATION dtzi;
    ZeroMemory(&dtzi, sizeof(dtzi));
    wcsncpy_s(dtzi.TimeZoneKeyName, tzKeyName.c_str(), _TRUNCATE);

    TIME_ZONE_INFORMATION tzi;
    if (!GetTimeZoneInformationForYear(utcNow.wYear, &dtzi, &tzi))
    {
        return false;
    }

    return SystemTimeToTzSpecificLocalTime(&tzi, &utcNow, &outLocal) != 0;
}

void KVFloaterWorldClock::updateClocks()
{
    for (size_t i = 0; i < mZones.size(); ++i)
    {
        const ClockZone& zone = mZones[i];
        ClockTile& tile = mTiles[i];
        if (!tile.timeText || !tile.dateText || !tile.bg)
        {
            continue;
        }

        SYSTEMTIME local;
        if (getLocalTimeFor(zone.tzKeyName, local))
        {
            bool isDay = local.wHour >= 6 && local.wHour < 18;
            tile.bg->setColor(isDay ? zone.accent : dim(zone.accent, 0.5f));

            tile.timeText->setText(LLStringExplicit(
                llformat("%02d:%02d:%02d", local.wHour, local.wMinute, local.wSecond)));
            tile.dateText->setText(LLStringExplicit(
                llformat("%02d %s - %s", local.wDay, kMonthAbbrev[local.wMonth], isDay ? "Day" : "Night")));
            if (tile.glyphText)
            {
                tile.glyphText->setText(LLStringExplicit(std::string(isDay ? kSunGlyph : kMoonGlyph)));
            }
        }
        else
        {
            tile.bg->setColor(dim(zone.accent, 0.35f));
            tile.timeText->setText(LLStringExplicit(std::string("--:--:--")));
            tile.dateText->setText(LLStringExplicit(std::string("zone unavailable")));
            if (tile.glyphText)
            {
                tile.glyphText->setText(LLStringExplicit(std::string()));
            }
        }
    }
}
