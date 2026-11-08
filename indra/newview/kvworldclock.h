/**
 * @file kvworldclock.h
 * @brief KV World Clock - standalone floater showing a grid of digital
 * clocks for SL Time plus a curated set of real-world cities, fired from
 * the S24 console ("worldclock" / "clock" / "time"). Every tile is built
 * entirely at runtime (same approach as KV Minesweeper's cell grid) since
 * the tile count/layout is just data (see buildZones()), not XUI. Local
 * time + DST per city comes from the Windows time zone database via
 * GetTimeZoneInformationForYear()/SystemTimeToTzSpecificLocalTime() - no
 * bundled tz data, always matches whatever the OS has.
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

#ifndef LL_KVWORLDCLOCK_H
#define LL_KVWORLDCLOCK_H

#include "llfloater.h"
#include "v4color.h"
#include <vector>
#include <string>

class LLPanel;
class LLTextBox;
class LLButton;
struct _SYSTEMTIME;

class KVFloaterWorldClock : public LLFloater
{
public:
    KVFloaterWorldClock(const LLSD& key);
    ~KVFloaterWorldClock();

    bool postBuild() override;
    void draw() override;

private:
    struct ClockZone
    {
        std::string label;      // city/region shown on the tile
        std::wstring tzKeyName; // Windows registry time zone key, e.g. L"Tokyo Standard Time"
        LLColor4 accent;        // tint applied to the shared tile_bg.png gloss panel
        bool featured = false;  // SL Time - gets a brighter gold border instead of the dark neutral one
    };

    struct ClockTile
    {
        LLButton* bg = nullptr;     // non-interactive, tinted tile_bg.png
        LLTextBox* cityText = nullptr;
        LLTextBox* timeText = nullptr;
        LLTextBox* dateText = nullptr;
        LLTextBox* glyphText = nullptr; // sun/moon day-night glyph, Emoji font
    };

    void buildZones();
    void buildTiles();
    void updateClocks();

    // S24: fills outLocal with the current local time for tzKeyName via the
    // Windows tz database (handles DST automatically for the current date).
    // Returns false if the key name isn't recognised by this OS, in which
    // case the caller falls back to showing "--:--:--" rather than a wrong time.
    bool getLocalTimeFor(const std::wstring& tzKeyName, _SYSTEMTIME& outLocal) const;

    std::vector<ClockZone> mZones;
    std::vector<ClockTile> mTiles;
    LLPanel* mGridPanel = nullptr;
    F32 mUpdateAccum = 0.f; // seconds since the last digit refresh - redrawn once/sec, not every frame
};

#endif // LL_KVWORLDCLOCK_H
