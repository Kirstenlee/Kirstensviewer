/**
 * @file lluihueshift.cpp
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

#include "llviewerprecompiledheaders.h"

#include "lluihueshift.h"

#include "lluicolortable.h"
#include "llviewercontrol.h"
#include "v3color.h"
#include "lldir.h"
#include "llfile.h"
#include "llsdserialize.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace LLUIHueShift
{

namespace
{
    enum Category : U8
    {
        CAT_FLOATERS = 0,
        CAT_MENUS,
        CAT_CONTROLS,
        CAT_TEXT,
        CAT_INVENTORY,
        CAT_MAP,
        CAT_SCRIPT,
        CAT_MISC,
        CAT_COUNT
    };

    struct CategorySetting
    {
        Category    cat;
        const char* setting_name;
    };

    // S24: order matches the Category enum, but that's not load-bearing - each entry is looked up
    // by name, not index.
    const CategorySetting kCategorySettings[CAT_COUNT] = {
        { CAT_FLOATERS,  "RenderUIHueShiftFloaters" },
        { CAT_MENUS,     "RenderUIHueShiftMenus" },
        { CAT_CONTROLS,  "RenderUIHueShiftControls" },
        { CAT_TEXT,      "RenderUIHueShiftText" },
        { CAT_INVENTORY, "RenderUIHueShiftInventory" },
        { CAT_MAP,       "RenderUIHueShiftMap" },
        { CAT_SCRIPT,    "RenderUIHueShiftScript" },
        { CAT_MISC,      "RenderUIHueShiftMisc" },
    };

    // S24: the global effect toggles (llui/llui.cpp's bindUIEffectsShader()) - not per-category
    // hue values, so kept as a separate list, but a saved color profile needs to capture these
    // too or it wouldn't reproduce the look the user actually had on screen when they saved it.
    const char* kGlobalEffectSettings[] = {
        "RenderUIContrast",
        "RenderUIGrayscale",
        "RenderUIShine",
    };

    // S24: snapshot of every UI color's ORIGINAL (skin-authored, resolved) value, captured once at
    // init() before any shift is ever applied - every applyCategory() call recomputes from this,
    // never from the table's current (possibly already-shifted) state, so repeated slider nudges
    // never compound/drift, and resetting a slider to 0 exactly restores the skin's own color.
    std::unordered_map<std::string, LLColor4> sOriginalColors;
    bool sInitialized = false;

    bool starts_with(const std::string& name, const char* prefix)
    {
        size_t len = strlen(prefix);
        return name.size() >= len && name.compare(0, len, prefix) == 0;
    }

    // S24: bucket every LLUIColorTable entry by name prefix into one of 8 broad, user-meaningful
    // categories. Not a hand-curated per-name list (344 entries and growing) - prefix matching
    // against this codebase's actual naming conventions (colors.xml) covers the overwhelming
    // majority correctly, and CAT_MISC is a genuine, honest catch-all for whatever doesn't cleanly
    // fit rather than a forced/wrong bucket. Pure grays/white/black (zero saturation) are naturally
    // hue-invariant regardless of category - no exclusion list needed for those.
    Category categorize(const std::string& name)
    {
        if (starts_with(name, "Floater") || starts_with(name, "Panel") || starts_with(name, "TitleBar") ||
            starts_with(name, "Resizebar") || starts_with(name, "ColorSwatchBorder"))
        {
            return CAT_FLOATERS;
        }
        if (starts_with(name, "Menu") || starts_with(name, "Pie") || starts_with(name, "Toolbar") ||
            starts_with(name, "Chiclet") || starts_with(name, "SysWell"))
        {
            return CAT_MENUS;
        }
        if (starts_with(name, "Button") || starts_with(name, "Slider") || starts_with(name, "MultiSlider") ||
            starts_with(name, "Scroll") || starts_with(name, "Badge") || starts_with(name, "ColorSwatch"))
        {
            return CAT_CONTROLS;
        }
        if (starts_with(name, "Text") || starts_with(name, "Chat") || starts_with(name, "IM") ||
            starts_with(name, "NameTag") || starts_with(name, "Conversation") || starts_with(name, "Notify") ||
            starts_with(name, "Toast") || starts_with(name, "ToolTip") || starts_with(name, "Inspector") ||
            starts_with(name, "Help") || starts_with(name, "Mention") || starts_with(name, "System") ||
            starts_with(name, "Direct") || starts_with(name, "Object") || starts_with(name, "User") ||
            starts_with(name, "Agent") || starts_with(name, "Time") || starts_with(name, "Health") ||
            starts_with(name, "Speaking") || starts_with(name, "Alert"))
        {
            return CAT_TEXT;
        }
        if (starts_with(name, "Inventory") || starts_with(name, "Filter") || starts_with(name, "Outfit") ||
            starts_with(name, "Worn") || starts_with(name, "Selected") || starts_with(name, "Group"))
        {
            return CAT_INVENTORY;
        }
        if (starts_with(name, "Map") || starts_with(name, "NetMap") || starts_with(name, "Silhouette") ||
            starts_with(name, "Highlight") || starts_with(name, "Beacon") || starts_with(name, "Pathfinding") ||
            starts_with(name, "Property") || starts_with(name, "Context") || starts_with(name, "Parcel") ||
            starts_with(name, "Grid"))
        {
            return CAT_MAP;
        }
        if (starts_with(name, "Script") || starts_with(name, "Syntax"))
        {
            return CAT_SCRIPT;
        }
        return CAT_MISC;
    }

    void applyCategory(Category cat, F32 shift_turns)
    {
        // S24: many real UI colors in a dark/minimalist skin are pure grays (zero saturation) -
        // hue rotation alone is a mathematical no-op on those (there's no hue to rotate), which is
        // why an all-gray category like "Floaters & Panels" visibly did nothing. Raise saturation
        // from that near-zero base toward this ceiling as the slider moves away from 0, so gray UI
        // actually picks up the chosen hue instead of staying inert. Uses max(), never lowers an
        // already-saturated color's own saturation - a genuinely colorful entry still just gets its
        // hue rotated as before, unchanged from the original behavior.
        static const F32 kMaxInjectedSaturation = 0.4f;
        F32 injected_saturation = kMaxInjectedSaturation * llmin(fabsf(shift_turns) / 0.5f, 1.0f);

        // S24: saturation alone can't do anything at the lightness extremes (L=0/1 is always
        // black/white in HSL regardless of H or S - no room for chroma) - real floater/panel tints
        // are exactly this (e.g. FloaterDefaultBackgroundColor = pure black at low alpha, live-
        // confirmed during investigation), which is why floaters stayed inert even after the
        // saturation fix above. Nudge only colors AT OR NEAR the extremes toward a floor/ceiling as
        // shift increases, giving the injected saturation something to actually show against -
        // genuinely mid-tone colors (already well clear of the extremes) are untouched. This is a
        // real, visible trade-off for near-black/near-white UI chrome specifically: it reads as
        // slightly less pure-black/pure-white once shifted, in exchange for actually being tintable.
        static const F32 kMaxLightnessInject = 0.15f;
        F32 lightness_range = kMaxLightnessInject * llmin(fabsf(shift_turns) / 0.5f, 1.0f);

        for (const auto& [name, orig] : sOriginalColors)
        {
            if (categorize(name) != cat)
            {
                continue;
            }

            LLColor3 rgb(orig.mV[VRED], orig.mV[VGREEN], orig.mV[VBLUE]);
            F32 h, s, l;
            rgb.calcHSL(&h, &s, &l);

            h = fmodf(h + shift_turns, 1.0f);
            if (h < 0.0f)
            {
                h += 1.0f;
            }
            s = llmax(s, injected_saturation);
            if (l < lightness_range)
            {
                l = lightness_range;
            }
            else if (l > 1.0f - lightness_range)
            {
                l = 1.0f - lightness_range;
            }

            rgb.setHSL(h, s, l);

            LLColor4 new_color(rgb.mV[0], rgb.mV[1], rgb.mV[2], orig.mV[VALPHA]);
            LLUIColorTable::instance().setColor(name, new_color);
        }
    }

    std::string profilesFilePath()
    {
        return gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "ui_hue_profiles.xml");
    }

    // S24: on first run (no per-user profile file yet), seed it from the shipped app_settings
    // copy that ships with the client - the same "copy a shipped file into the user's dir on
    // first run" convention LLPresetsManager already uses for water presets (see its
    // PRESETS_WATER branch, newview/llpresetsmanager.cpp), just for one combined file instead of
    // one-file-per-preset. Never overwrites an existing user file, so a returning user's own
    // saved/edited profiles are always left alone.
    void seedDefaultProfilesIfMissing()
    {
        std::string user_path = profilesFilePath();
        if (LLFile::isfile(user_path))
        {
            return;
        }

        std::string shipped_path = gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, "ui_hue_profiles.xml");
        if (LLFile::isfile(shipped_path))
        {
            LLFile::copy(shipped_path, user_path);
        }
    }

    // S24: returns an empty (undefined) LLSD map if the file doesn't exist yet - same "missing
    // file is not an error" convention as LLSpellChecker's user-dictionary loader.
    LLSD loadProfilesFile()
    {
        LLSD data;
        llifstream file(profilesFilePath().c_str(), std::ios::binary);
        if (file.is_open())
        {
            LLSDSerialize::fromXMLDocument(data, file);
        }
        if (!data.isMap())
        {
            data = LLSD::emptyMap();
        }
        return data;
    }

    bool writeProfilesFile(const LLSD& data)
    {
        llofstream file(profilesFilePath().c_str(), std::ios::trunc);
        if (!file.is_open())
        {
            return false;
        }
        LLSDSerialize::toPrettyXML(data, file);
        return true;
    }
} // namespace

void applyAll()
{
    if (!sInitialized)
    {
        return;
    }

    for (const auto& cs : kCategorySettings)
    {
        // S24: slider is in degrees (KVTweaks convention, -180..180) - hue math here is turns [0,1).
        F32 degrees = gSavedSettings.getF32(cs.setting_name);
        applyCategory(cs.cat, degrees / 360.0f);
    }
}

void init()
{
    if (sInitialized)
    {
        return;
    }

    sOriginalColors.clear();
    for (const auto& color_pair : LLUIColorTable::instance().getLoadedColors())
    {
        sOriginalColors.emplace(color_pair.first, color_pair.second.get());
    }
    sInitialized = true;

    for (const auto& cs : kCategorySettings)
    {
        LLControlVariable* control = gSavedSettings.getControl(cs.setting_name);
        if (control)
        {
            control->getSignal()->connect([](LLControlVariable*, const LLSD&, const LLSD&) { applyAll(); });
        }
    }

    applyAll();

    seedDefaultProfilesIfMissing();
}

std::vector<std::string> getProfileNames()
{
    std::vector<std::string> names;
    LLSD data = loadProfilesFile();
    for (LLSD::map_const_iterator it = data.beginMap(); it != data.endMap(); ++it)
    {
        names.push_back(it->first);
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool saveProfile(const std::string& name)
{
    if (name.empty())
    {
        return false;
    }

    LLSD profile;
    for (const auto& cs : kCategorySettings)
    {
        profile[cs.setting_name] = gSavedSettings.getF32(cs.setting_name);
    }
    for (const char* setting_name : kGlobalEffectSettings)
    {
        LLControlVariable* control = gSavedSettings.getControl(setting_name);
        if (control)
        {
            // S24: getValue() (not getF32()) so this stays correct regardless of the control's
            // real type (RenderUIGrayscale is a Boolean, not F32).
            profile[setting_name] = control->getValue();
        }
    }

    LLSD data = loadProfilesFile();
    data[name] = profile;
    return writeProfilesFile(data);
}

bool loadProfile(const std::string& name)
{
    LLSD data = loadProfilesFile();
    LLSD profile = data[name];
    if (!profile.isMap())
    {
        return false;
    }

    for (const auto& cs : kCategorySettings)
    {
        if (profile.has(cs.setting_name))
        {
            gSavedSettings.setF32(cs.setting_name, (F32)profile[cs.setting_name].asReal());
        }
    }
    for (const char* setting_name : kGlobalEffectSettings)
    {
        if (profile.has(setting_name))
        {
            LLControlVariable* control = gSavedSettings.getControl(setting_name);
            if (control)
            {
                control->setValue(profile[setting_name]);
            }
        }
    }
    return true;
}

bool deleteProfile(const std::string& name)
{
    LLSD data = loadProfilesFile();
    if (!data.has(name))
    {
        return false;
    }
    data.erase(name);
    return writeProfilesFile(data);
}

} // namespace LLUIHueShift
