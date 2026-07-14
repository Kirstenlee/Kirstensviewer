
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
| 3535  | HRADR    | S24.8   |

**Forked from Viewer Develop / 2026.3**

---

## ✨ Change Log (Concise)

- Snapshot Build 3535

#### 🎨 Graphics & Rendering

- S24: DX11 conversion project stage 2
- S24: Add runtime-toggled VBO work queue (S24VBOWorkQueueEnabled/ThreadCount)


#### ⚡ Performance & Optimisation

- S24: VRAM eviction improvements.
- S24: Add hysteresis to avatar impostor/too-slow thresholds to stop borderline avatars flickering between full render and billboard in busy scenes.
- S24: Live VRAM signal add live DXGI VRAM budget/usage tracking via budget-change notifications, replacing the one-time static query

#### 🖌️ UI, Themes & Skins

- S24: Nearby people add sortable distance and age metrics, with highlights for new users.
- S24: Quick chat V2

#### 🔊 Audio

#### 🎥 Camera & Input

#### 🛠️ Debug & Diagnostics

- S24: Remove the terribly broken and useless QA scene monitor console

#### 🔒 Privacy & Moderation

#### 🔗 Upstream Merges & Bug Fixes

- 5962 Optimize writeWearablesToAvatar
- 5981 Fix viewer scale interpolation not finishing
- New color picker with more slots
- 5968 Fix outfit tabs not showing up without a filter reset
- 5892 Process high priority inventory updates immediately


#### 🏗️ Build System & Infrastructure

- S24: vcpkg migration - Boost 1.91 - fp:fast AVX2
- S24: Fix include() case mismatches for renamed cmake modules (GLM/ZLIB/APR/CURL/etc.) surfaced by CMake 4.4.0's new case-insensitive module

**Binaries signed:** Codesign Serial: `4e2969400a179e151ba7323da181f8b0`
---

## 📬 Footer

Kirstens Viewer is independently developed. For support, build reports, or contributions, please use the designated contact channels.  
Special thanks to SourceForge for hosting this project and supporting the open source community.

> *"I learned very early the difference between knowing the name of something and knowing something."* — Richard P. Feynman