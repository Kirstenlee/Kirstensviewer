/**
 * @file lluihueshift.h
 * @brief S24: per-category HSL hue rotation of every LLUIColorTable-driven UI color
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

#ifndef LL_LLUIHUESHIFT_H
#define LL_LLUIHUESHIFT_H

#include <string>
#include <vector>

// S24: recolors the whole viewer UI (floater chrome, menus, buttons/scrollbars/sliders, text/
// chat/notifications, inventory/outfits, map/world overlays, script editor syntax highlighting,
// everything else) by rotating each LLUIColorTable entry's hue independently per category, driven
// by 8 KVTweaks sliders. Every recompute starts from a one-time snapshot of the skin-authored
// colors taken at startup - never from the table's current (possibly already-shifted) state - so
// repeated slider nudges never compound/drift. Routes through LLUIColorTable::setColor(), the same
// mechanism the game's own user-color-override/color-picker system already uses, so every widget
// already holding a live LLUIColor reference picks up the change without recreation.
namespace LLUIHueShift
{
    // Call once at startup, right after LLUIColorTable::instance().loadFromSettings() - snapshots
    // every currently-loaded color's resolved value, wires a settings-changed listener per
    // category, and applies whatever shift values are currently saved (0.0 = no-op on first run).
    void init();

    // Re-applies every category's current saved shift to the live LLUIColorTable, recomputed from
    // the original startup snapshot. Safe to call any time; the settings listeners already call
    // this automatically on any RenderUIHueShift* change, so this is exposed mainly for KVTweaks'
    // "reset to default" path (which needs a fresh apply after resetToDefault() zeroes the values).
    void applyAll();

    // S24: named save/load/delete for the 8 category slider values as a whole, so users can keep a
    // handful of favorite combinations rather than re-dialing sliders from scratch. Stored as a
    // single small LLSD file under the viewer's user_settings dir (shared across all accounts on
    // this machine, like the color table itself) - deliberately NOT routed through
    // LLPresetsManager's water/graphic/camera preset system, since a profile here is just the 8
    // floats, not a full settings-file snapshot.
    std::vector<std::string> getProfileNames();
    bool saveProfile(const std::string& name);
    bool loadProfile(const std::string& name);
    bool deleteProfile(const std::string& name);
}

#endif // LL_LLUIHUESHIFT_H
