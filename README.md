
# ⚠️ Kirstens Viewer — README

---

<p align="center">
     <img src="https://img.shields.io/badge/build-stable-green.svg" alt="Build">
     <img src="https://img.shields.io/badge/platform-Windows-blue.svg" alt="Platform">
     <img src="https://img.shields.io/badge/standard-C%2B%2B20-orange.svg" alt="C++20">
</p>

---


## 🖥️ About This Viewer

Kirstens Viewer is a maintained fork of the official Second Life Viewer. It prioritizes fidelity, performance, and compatibility with modern Windows systems.

- **Optimized** for modern multi-core CPUs (AVX2); target audience: high-end PCs & technical users
- **Modern C++**: replacing legacy code
- **Unique build system**: modern vcpkg / PowerShell solution
- **Functionality** aligns with official viewer for consistent user experience

---


## ⚖️ Legal & Compliance

> **Not affiliated with Linden Lab.** Users must adhere to the [Second Life Terms of Service](http://secondlife.com/corporate/tos.php).

> *This software is provided "as is" with no warranty. You assume all risk of use and agree that the Kirstens Viewer team is not liable for any loss or disruption caused by use of this software.*

---


## 🔒 Privacy Features

Kirstens Viewer respects user privacy and disables or limits the following components by default:

- 🚫 Background Updater
- 🚫 Excessive Metrics Collection
- 🚫 Crashlogger

---


### 💬 Message from Kirstenlee

> *Thank you for your time and interest.*  
> *Best wishes always,*  
> *Kirstenlee Cinquetti*

---


## 🏗️ Build History

| Build | Codename | Version |
|-------|----------|---------|
| 2182  | OTHAN    | S24.1   |
| 2342  | DRIF     | S24.2   |
| 2527  | FARA     | S24.3   |
| 2700  | SJAANDI  | S24.4   |
| 3075  | VETR     | S24.5   |
| 3322  | LYSI     | S24.6   |
| 3500  | MAGN     | S24.7   |

**Forked from Viewer Develop / 2026.3**

---

## ✨ Change Log (Concise)

- Release Build 3500 !

#### 🎨 Graphics & Rendering

- FIX - Selection beam not hiding for GL line method
- FIX - HUD texture updates not correct on resize
- S24 - Refined DOF V3 (Internal 64bit Precision)
- S24 - Render Pipeline Optimisation Pass 
- S24 - Cube map modernisation - conservative
- LOD Triangle Checker (#5855)
- S24 MEMORY SAFETY: Cap decode queue depth and add raw image emergency scavenger

#### ⚡ Performance & Optimisation

- S24 - Texture cache bias tuning / with continuous decay for ram cache
- S24 - Cache Tuning and addition of multi threading to cache eviction to disk.
- Solve the issue where ram cache is not used and purge was too gentle on the disk cache! - User Feedback
- S24 - Use gFrameDTClamped rather than arbitrary min/max values for frame timing
- Code tidy and some modernising the main loop for better branch prediction
- 5945 cacheOptimize crash handling
- S24 - Furthur Cache tuning and enhanced diskcache behaviours when near full!
- V2math , v3math , Render2dutil and Renderline opimised for best performance

#### 🖌️ UI, Themes & Skins

- S24 - Dynamic colour control for Progress Bars
- FIX - Inventory search not functional
- S24 - Add friend highlight colour to Preferences -  User Request
- 2026.3 voice controls
- Add new artwork for alternate skins
- Add LL Flat UI theme to S24 selection
- Update individual skin widgets to match themes
- S24 - fix chat history properly to be a fully custom selectable colour - User Feedback
- Camera selection fix for S24 custom camera setup - thanks Dawny! for the bug report
- improve the colors.xml file
- S24:FIX Friends always get the friend colour!

#### 🔊 Audio

- S24 - S24 VLC Engine 10-Bar EQ + Preamp and Stream Metadata.
- S24 - EQ Presets for VLC , and switch over to VLC as default , FMOD engine as Back-up (both with same feature set!)

#### 🎥 Camera & Input

- Extend Smooth Movement control to flight controls (S24).
- S24 - Advanced Snapshot System -  interpolate , burst shot , quick formats and guide lines layered ontop of the standard LL feature set, 4K and above upscaling improvements.
- S24 camera controls (finer control) and progressive acceleration for X,Y and Z

#### 🛠️ Debug & Diagnostics

- S24 - VOS update Scan V2
- S24 - VOS better Whois command
- S24 - VOS Enhance TP and Fly commands
- S24 - VOS flight package upgrade

#### 🔒 Privacy & Moderation

- S24 - Super Mute - derenders Avatar and Hides voice dot also.
- S24 - Mute Objects derenders object and functions for linksets and owner objects making it extreamly handy for film use.
- Security - Clear out dead code from corehttp
- S24 - Modernised MachineID to use Modern windows headers saves over 400 lines of legacy code!

#### 🔗 Upstream Merges & Bug Fixes

- S24 - Add drag-n-drop reordering for clothing layers based on patch (#5918)
- 5777 - LLSD formatter exception handling (inventory/name cache shutdown protection)
- 5232 - LLSD thread-safety
- 5569 APPLIED - S24 Style! no verbose login just the fix for retry logic
- S24 - Font kerning Improvements based partly on LL - patch 2023
- 5702 - CEF media priority (S24 hybrid)
- 5928 - Gesture auto-complete
- 5652 - Moderation Status Reliability
- 5702 - Media Priority Patches
- 5442 - Texture streaming channels with per-channel priority control via TextureChannelPriority Vector4 setting (required adding LLVector4/TYPE_VEC4 support to llxml/llcontrol.*)
- 5818 - Fix setCursor crash
- 5804 - Prevent focus steal when removing unselected conversation widget
- 5785 - fix login and world UI overlap
- 3430 Fix incorrectly stated permissions for scripts
- 5784 fix crash at LLViewerRegion::killInvisibleObjects
- 5519 fix picks from moved regions
- 5801 add Mute sound menu option
- 5744 Prefill 'report' floater for objects' IMs
- 5799 update message for no search results and #5806 add option to reset environment after teleport
- 5804 Prevent group chat focus steal when opening adhoc IM
- 5802 Open teleport history with a keystroke
- 5786 Fix a case of unitialized override matrix  && Fix GLTF bind matrix remapping
- Fix display of ad-hoc session type in About and display p2p instead as appropriate
- 4823 WebRTC crashes FIX
- 5858 Crash at LLFontFreetype::renderGlyph - remade for S24!
- 5859 Fix a missed null category check
- 5846 Fix crash on pushBatch
- GLTF scale doesn't match legacy scaling when rotation is applied.
- Notecard Cut/Copy Fix (#3342) & Favorites Bar Text Truncation (#5877)
- Outfit Search Fix (#5798)
- Drop fewer packets on UDP packet flood (#5565)
- Don't initialize inbox views if inbox isn't visible
- 5837 Fix mature group search showing zero results
- 5946 Fix excessive 'allowed to terraform' warning

#### 🏗️ Build System & Infrastructure

- Updated WebRTC Library
- Updated CEF/Dullahan Browser
- Updated VLC library
- Removal of the vivox SLvoice system.
- Alignment triplet for ABI on libs
- Viewer Switched to Fp:Precise and double precision IEEE-754 defines added to math for use when required!
- Sort a pile of floating point issues / warnings

**Binaries signed:** Codesign Serial: `4e2969400a179e151ba7323da181f8b0`

---


## 📬 Footer

Kirstens Viewer is independently developed. For support, build reports, or contributions, please use the designated contact channels.  
Special thanks to SourceForge for hosting this project and supporting the open source community.

> *"I learned very early the difference between knowing the name of something and knowing something."* — Richard P. Feynman