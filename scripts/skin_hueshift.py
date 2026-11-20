#!/usr/bin/env python3
"""
skin_hueshift.py - regenerate the Hyperion and Midnight skin art from the Aero24 default.

Copyright (c) 2026 Kirstenlee Cinquetti (Lee Quick)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.

Only the blue hue band of the default art is rotated toward the skin's accent.
Greys, whites, black glass and alpha are untouched, and so are the semantic
colours (green ticks, red crosses, gold client folders).

Usage:
  python skin_hueshift.py sheet <layer>            contact sheet to the given folder, no skin files touched
  python skin_hueshift.py apply <layer> [skin]     write the shifted art into the skin(s)
Layers: 1 widgets+window chrome+navbar, 2 toolbar icons, 3 inventory folders, 4 new floater art,
        5 blue fall-through art for LindenX
Skins: hyperion, midnight, linden (default: both of the first two; layer 5 is linden only)
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

SKINS_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                          "..", "indra", "newview", "skins"))
DEFAULT = os.path.join(SKINS_DIR, "default", "textures")

# Accent hues measured from the skins' own existing art (default blue is about 203 degrees).
SOURCE_HUE = 203.0
PROFILES = {
    "hyperion": {"folder": "Lysi", "target": 50.0},    # gold
    "midnight": {"folder": "midnight", "target": 273.0},  # violet
    "linden": {"folder": "linden", "target": 162.0},    # LindenX teal-green, measured from its own widgets
}
BAND_FULL = 42.0   # degrees either side of the source hue that rotate fully
BAND_EDGE = 78.0   # rotation fades to nothing by here, so greens and reds are never caught

# Exactly the default textures reworked since the Vista-style pass (r4371 onward); nothing else is touched.
LAYERS = {
    1: [
        "navbar/NavBar_BG.png",
        "widgets/Checkbox_Disabled.png",
        "widgets/Checkbox_Off.png",
        "widgets/Checkbox_On.png",
        "widgets/Checkbox_On_Disabled.png",
        "widgets/Checkbox_On_Over.png",
        "widgets/Checkbox_On_Press.png",
        "widgets/Checkbox_Over.png",
        "widgets/Checkbox_Press.png",
        "widgets/Checkbox_Slim_On.png",
        "widgets/PushButton_Off.png",
        "widgets/SliderThumb_Off.png",
        "windows/Icon_Close_Foreground.png",
        "windows/Icon_Close_Press.png",
        "windows/Window_Background.png",
        "windows/Window_Foreground.png"],
    2: [
        "toolbar_icons/AO.png",
        "toolbar_icons/appearance.png",
        "toolbar_icons/avatars.png",
        "toolbar_icons/build.png",
        "toolbar_icons/caret_bottom.png",
        "toolbar_icons/caret_left.png",
        "toolbar_icons/caret_right.png",
        "toolbar_icons/chat.png",
        "toolbar_icons/destinations.png",
        "toolbar_icons/environments.png",
        "toolbar_icons/facebook.png",
        "toolbar_icons/flickr.png",
        "toolbar_icons/gestures.png",
        "toolbar_icons/grid_status.png",
        "toolbar_icons/highlighting.png",
        "toolbar_icons/highlighting_selected.png",
        "toolbar_icons/howto.png",
        "toolbar_icons/inventory.png",
        "toolbar_icons/land.png",
        "toolbar_icons/map.png",
        "toolbar_icons/marketplace.png",
        "toolbar_icons/mini_cart.png",
        "toolbar_icons/mini_map.png",
        "toolbar_icons/mktlistings.png",
        "toolbar_icons/move.png",
        "toolbar_icons/nearbyvoice.png",
        "toolbar_icons/outbox.png",
        "toolbar_icons/people.png",
        "toolbar_icons/performance.png",
        "toolbar_icons/picks.png",
        "toolbar_icons/places.png",
        "toolbar_icons/preferences.png",
        "toolbar_icons/profile.png",
        "toolbar_icons/report_abuse.png",
        "toolbar_icons/search.png",
        "toolbar_icons/snapshot.png",
        "toolbar_icons/speak.png",
        "toolbar_icons/twitter.png",
        "toolbar_icons/view.png"],
    3: [
        "icons/Inv_FolderClosed.png",
        "icons/Inv_FolderOpen.png",
        "icons/Inv_LinkFolder.png",
        "icons/Inv_LookFolderClosed.png",
        "icons/Inv_LookFolderOpen.png",
        "icons/Inv_LostClosed.png",
        "icons/Inv_LostOpen.png",
        "icons/Inv_StockFolderClosed.png",
        "icons/Inv_StockFolderOpen.png",
        "icons/Inv_SysClosed.png",
        "icons/Inv_SysOpen.png",
        "icons/Inv_TrashClosed.png",
        "icons/Inv_TrashOpen.png",
        "icons/Inv_VersionFolderClosed.png",
        "icons/Inv_VersionFolderOpen.png"],
    4: [
        "bench/gfx_panel.png",
        "bench/score_panel.png",
        "bottomtray/Dpad.png",
        "calculator/key_bezel.png",
        "calculator/key_bezel_pressed.png",
        "calculator/vfd_bg.png",
        "cards/Back.png",
        "minesweeper/covered.png",
        "minesweeper/flag.png",
        "minesweeper/mine.png",
        "minesweeper/revealed.png",
        "reversi/black_disc.png",
        "reversi/board_cell.png",
        "reversi/white_disc.png",
        "worldclock/tile_bg.png"],
}

# LindenX never received the reworked art, so these default textures fell through to Aero24 blue
# next to its teal-green art. Layer 5 regenerates them for LindenX only.
LAYERS[5] = [
        "BottomTray_BG.png",
        "bench/gfx_panel.png",
        "bench/score_panel.png",
        "bottomtray/BottomTray_BG.png",
        "calculator/vfd_bg.png",
        "cards/Back.png",
        "containers/TabLeft_Flat_Selected.png",
        "containers/TabTop_First_Flat_Selected.png",
        "containers/TabTop_Last_Flat_Selected.png",
        "containers/TabTop_Middle_Flat_Selected.png",
        "icons/Inv_Script_Luau.png",
        "navbar/FileMenu_BG.png",
        "navbar/FileMenu_BarSelect.png",
        "taskpanel/TaskPanel_BG.png",
        "widgets/Checkbox_Over.png",
        "widgets/ComboButton_Over.png",
        "widgets/ComboButton_Press.png",
        "widgets/ComboButton_Up_On_Selected.png",
        "widgets/DropDown_Over.png",
        "widgets/DropDown_Selected.png",
        "widgets/PushButton_On_Disabled.png",
        "widgets/PushButton_On_Over.png",
        "widgets/PushButton_Selected_Over.png",
        "widgets/RadioButton_On_Over.png",
        "widgets/SegmentedBtn_Left_On.png",
        "widgets/SegmentedBtn_Left_On_Disabled.png",
        "widgets/SegmentedBtn_Left_On_Over.png",
        "widgets/SegmentedBtn_Left_On_Selected.png",
        "widgets/SegmentedBtn_Middle_On.png",
        "widgets/SegmentedBtn_Middle_On_Over.png",
        "widgets/SegmentedBtn_Middle_On_Press.png",
        "widgets/SegmentedBtn_Middle_Selected_Over.png",
        "widgets/SegmentedBtn_Right_On.png",
        "widgets/SegmentedBtn_Right_On_Over.png",
        "widgets/SegmentedBtn_Right_Selected_Over.png",
        "widgets/SliderThumb_Over.png"]
LAYER_SKINS = {5: ["linden"]}


def expand(names):
    return [p for p in names if os.path.isfile(os.path.join(DEFAULT, p))]


def rgb_to_hsv(rgb):
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    mx = rgb.max(-1)
    mn = rgb.min(-1)
    d = mx - mn
    s = np.where(mx > 0, d / np.maximum(mx, 1e-6), 0.0)
    safe = np.maximum(d, 1e-6)
    rc, gc, bc = (mx - r) / safe, (mx - g) / safe, (mx - b) / safe
    h = np.where(r == mx, bc - gc, np.where(g == mx, 2.0 + rc - bc, 4.0 + gc - rc))
    h = np.where(d > 1e-6, (h / 6.0) % 1.0, 0.0)
    return h * 360.0, s, mx


def hsv_to_rgb(h, s, v):
    h = (h % 360.0) / 60.0
    i = np.floor(h).astype(np.int32) % 6
    f = h - np.floor(h)
    p, q, t = v * (1 - s), v * (1 - s * f), v * (1 - s * (1 - f))
    r = np.choose(i, [v, q, p, p, t, v])
    g = np.choose(i, [t, v, v, q, p, p])
    b = np.choose(i, [p, p, t, v, v, q])
    return np.stack([r, g, b], -1)


def band_weight(h):
    """1 inside the blue band, fading smoothly to 0 outside it."""
    d = np.abs((h - SOURCE_HUE + 180.0) % 360.0 - 180.0)
    t = np.clip((BAND_EDGE - d) / (BAND_EDGE - BAND_FULL), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def shift(img, delta, sat_scale=1.0):
    a = np.asarray(img.convert("RGBA")).astype(np.float32) / 255.0
    rgb, alpha = a[..., :3], a[..., 3]
    h, s, v = rgb_to_hsv(rgb)
    w = band_weight(h) * np.clip(s * 4.0, 0.0, 1.0)  # near-greys keep their exact value
    h2 = h + delta * w
    s2 = s * (1.0 - w * (1.0 - sat_scale))
    out = hsv_to_rgb(h2, np.clip(s2, 0, 1), v)
    res = np.concatenate([np.clip(out, 0, 1), alpha[..., None]], -1)
    return Image.fromarray((res * 255.0 + 0.5).astype(np.uint8), "RGBA")


def convert(rel, skin):
    src = Image.open(os.path.join(DEFAULT, rel))
    delta = PROFILES[skin]["target"] - SOURCE_HUE
    name = os.path.basename(rel)
    sat = 1.0
    # Hyperion's system folders would turn gold like the client folders, so they go steel instead
    if skin == "hyperion" and rel.startswith("icons/Inv_") and name.startswith(
            ("Inv_Sys", "Inv_Stock", "Inv_Lost", "Inv_Version", "Inv_Look", "Inv_Link", "Inv_Trash")):
        delta, sat = 0.0, 0.22
    return shift(src, delta, sat)


def sheet(layer, out_dir):
    files = expand(LAYERS[layer])
    skins = LAYER_SKINS.get(layer, ["hyperion", "midnight"])
    cell, pad = 72, 6
    cols = 1 + len(skins)
    rows = len(files)
    w = cols * (cell + pad) + pad
    h = rows * (cell + pad) + pad
    img = Image.new("RGBA", (w, h), (70, 70, 74, 255))
    draw = ImageDraw.Draw(img)
    for r, rel in enumerate(files):
        variants = [Image.open(os.path.join(DEFAULT, rel)).convert("RGBA")] + [convert(rel, sk) for sk in skins]
        for c, v in enumerate(variants):
            v = v.copy()
            v.thumbnail((cell, cell), Image.LANCZOS)
            x = pad + c * (cell + pad)
            y = pad + r * (cell + pad)
            draw.rectangle([x, y, x + cell - 1, y + cell - 1], fill=(40, 40, 44, 255))
            img.alpha_composite(v, (x + (cell - v.width) // 2, y + (cell - v.height) // 2))
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "layer%d_sheet.png" % layer)
    img.save(path)
    return path, files


def apply(layer, skins):
    files = expand(LAYERS[layer])
    for skin in skins:
        folder = os.path.join(SKINS_DIR, PROFILES[skin]["folder"], "textures")
        for rel in files:
            dst = os.path.join(folder, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            convert(rel, skin).save(dst, optimize=True)
        print("%s: %d files written" % (skin, len(files)))


def main(argv):
    if len(argv) < 3 or argv[1] not in ("sheet", "apply") or not argv[2].isdigit() or int(argv[2]) not in LAYERS:
        print(__doc__)
        return 1
    layer = int(argv[2])
    if argv[1] == "sheet":
        out_dir = argv[3] if len(argv) > 3 else "."
        path, files = sheet(layer, out_dir)
        print(path, len(files), "files")
    else:
        skins = [argv[3]] if len(argv) > 3 else LAYER_SKINS.get(layer, ["hyperion", "midnight"])
        apply(layer, skins)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
