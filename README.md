
# Kirstens Viewer — README

---

<p align="center">
     <img src="https://img.shields.io/badge/build-4015-green.svg" alt="Build">
     <img src="https://img.shields.io/badge/version-Alpha%201.2-blueviolet.svg" alt="Alpha 1.2">
     <img src="https://img.shields.io/badge/platform-Windows-blue.svg" alt="Platform">
     <img src="https://img.shields.io/badge/standard-C%2B%2B20-orange.svg" alt="C++20">
     <img src="https://img.shields.io/badge/renderer-DirectX%2011-9cf.svg" alt="DirectX 11">
</p>

---


## 🖥️ About This Viewer

Kirstens Viewer is a maintained fork of the official Second Life Viewer. It prioritizes fidelity, performance, and compatibility with modern Windows systems.

- **Optimized** for modern multi-core CPUs (AVX2); target audience: high-end PCs & technical users
- **Modern C++**: replacing legacy code
- **Unique build system**: modern vcpkg / PowerShell solution
- **Functionality** aligns with official viewer for consistent user experience
- **DirectX 11 renderer** (`DX_RENDER`): a from-scratch native D3D11 backend, complete with its own HLSL shader set — the sole supported renderer since build 3712, with OpenGL fully retired from the shipped build — see [DirectX 11 Renderer](#-directx-11-renderer) below
- **Floater pop-out framework** (new in Alpha 1.2): pop any registered floater (About, Conversations/nearby chat, more to follow) into its own independently movable and resizable desktop window, rendered via render-to-texture + DirectComposition rather than a second swap chain — see the Alpha 1.2 change log below

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
| **4015** | **HRADR** | **Alpha 1.2 (current)** |

**Forked from Viewer Develop / 2026.4**

---

## 🎮 DirectX 11 Renderer

Since build 3535, Kirstens Viewer has migrated from OpenGL to a native
**Direct3D 11** renderer. As of r3712 the dual-path era ended outright — all
GLSL shader source and the OpenGL rendering path were removed, so
`DX_RENDER` is now the viewer's only renderer. It was publicly released as
pre-alpha 0.1 (2026-08-23) and progressed through 0.2, 0.3 and 0.4 to
**Alpha 1.0** (2026-09-17, r3855), then **Alpha 1.1** (2026-09-19, r3868),
and is now **Alpha 1.2** (2026-09-27, r4015) — the current release.

**Architecture**: the D3D11 backend lives in its own module,
`indra/dxrender/` — `core/` (device/context/state: `DXDevice`,
`DXContext`, `DXSwapChain`, `DXStateCache`) and `resources/` (thin
per-resource wrappers: `DXBuffer`, `DXTexture`, `DXShader`, `DXSampler`,
`DXRenderTarget`, `DXOcclusionQuery`, and more) — kept deliberately
separate from the ported `newview`/`llrender` code rather than smearing
D3D11 calls throughout the original GL call sites. Per-frame orchestration
(`DXPipeline`, the `DXDrawPool*` family) lives in `indra/newview/`
alongside its GL counterparts, since it's tightly coupled to
`LLPipeline`/`LLDrawPool`.

**Status**: the GLSL→HLSL shader port is complete (237/237 files
converted). The full deferred renderer — opaque/alpha/materials/PBR/
avatars/terrain/water/sky, shadows, local/spot lights, SSAO, glow/bloom,
FXAA/SMAA, real D3D11 occlusion culling, reflection probes, anisotropic
filtering — is working, with real Screen-Space Reflections, hero-probe
mirrors, BC7 GPU texture compression (58% VRAM reduction, opt-in), and a
real Anaglyph 3D implementation all shipped and live. The GL-era
class-name/vocabulary retirement (task #300) continued through Alpha 1.1/
1.2 — `LLImageGL`→`LLImageDX`, `LLGLTexture`→`LLDXTexture`,
`LLFontGL`→`LLFontDX`, and the dead GL branches these renames exposed have
all been retired. Known gaps: reflection quality is still tuned by eye
rather than fully derived, and avatar GPU-cost throttling (AutoFPS)
remains a known no-op under `DX_RENDER`.

---

## ✨ Change Log — Alpha 1.2 (current release, r3856 → r4015)

Every entry below is sourced directly from the SVN commit history between
Alpha 1.0 (r3855) and the current build (r4015) — 159 commits, grouped by
category and compressed where several commits form one continuous piece
of work. The full Alpha 1.0-and-earlier history (the original DirectX 11
conversion project, r3536–r3855) has been folded into a collapsed section
at the bottom of this log to keep the current release notes focused.

#### 🎨 Graphics & Rendering

- S24: fixed alpha shadows casting as solid — split the shared alpha-blend shadow cutoff into separate world/rigged constants (do-not-re-merge, they diverge for a real reason)
- S24/DX_RENDER: fixed a `DXState::sCullFace` global leak — an RAII guard (`LLGLCullFace`) replaces raw paired `cullFace()` calls
- S24: fixed sunrise/sunset lighting flicker — `setupHWLights()` was only being called once per frame under DX_RENDER against GL's three
- S24: guarded both spot-light draw blocks with `isComplete()` before binding, matching the existing deferred-soften-shader convention (a never-compiled shader no longer silently reuses whatever was previously bound)
- S24: fixed spot shadow depth double-transform (a manual NDC-to-texture bake was stacking with the reversed-Z remap) and a stale spot-light-fade/shadow-slot desync
- S24: re-enabled real local point/spot light contribution to forward-lit avatar and materials shaders — previously contributed zero illumination under `DX_RENDER`
- S24: added a normal-similarity check to the SSAO/shadow-blur bilateral test, fixing thin-surface back-face shadow/AO bleed onto the front face
- S24: simplified `sampleSpotShadow()` — removed a vestigial sun-cascade-style blend copy-pasted from the sun/cascade shadow code that carried a real divide-by-zero risk
- S24: clamped the specular divide in `calcPointLightOrSpotLight()` to match the existing correct handling elsewhere — unclamped input was producing garbage specular at the lit/unlit terminator
- S24: corrected the camera-inside-light-box classification radius to the box's real circumscribing sphere, replacing an arbitrary margin that had drifted further from the reference implementation than the bug it was meant to fix
- DX_RENDER: re-enabled the anisotropic filtering system — a dead checkbox became a real `DXSampler` level control (0–16, 0 = off)
- DX_RENDER: `GLuint` removal and a large dead-code clearing pass; `LLPostProcess` (a circa-2007 colour-filter/night-vision/bloom class, fully dead) removed outright, alongside a general `LLImageGL` cleanup (3418 → 2308 lines)
- DX_RENDER: `LLImageGL` renamed to `LLImageDX`; `isCompressed()`/format checks retired to ask the real `DXTexture` state directly instead of a stale GL enum, fixing a real ~4x VRAM over-reporting bug for BC7-upgraded textures along the way
- DX_RENDER: `gGLViewport` renamed to `gDXViewport`; dead GL branches stripped from `llrendertarget.cpp`; `swapFBORefs()` fixed to actually swap the real `DXRenderTarget`
- DX_RENDER: `llrender2dutils` functions renamed `gl_*` → `dx_*`; fixed an inner/outer radius swap in the world-map tracker ring
- DX_RENDER: `LLHLSLShader` GLSL-vocabulary cleanup, dead `shouldChange()` removed
- DX_RENDER: `LLGLTexture` renamed to `LLDXTexture`; dead `isJustBound()`/`getDontDiscard()` removed
- DX_RENDER: `LLFontGL` renamed to `LLFontDX`
- S24: reverted a camera-focus/centring regression (r3949) after live diagnosis pinned it down precisely

#### 🖌️ UI, Themes & Skins

- **New: floater pop-out-to-desktop framework.** Any registered floater (currently "About" and the Conversations/nearby-chat window) can be popped out into its own real, independently movable and resizable OS window via render-to-texture + DirectComposition compositing — not a second swap chain. Full input fidelity is preserved: dragging, resizing, text selection, click-and-hold sliders/scrollbars, keyboard shortcuts, and right-click/menu-button context menus are all rebuilt as genuine children of the popped-out floater rather than left rendering in the wrong place. Multi-instance from the ground up — any number of registered floaters can be popped out independently and simultaneously, each with its own host window, render target and DirectComposition surface. Hardened across many rounds of live testing: resize now tracks the real window size live instead of freezing mid-drag, the resize-edge cursor changes correctly, the sticky-hover-highlight bug is fixed, and the backing/transparency handling was tuned twice before landing on using the floater's own real background colour (fixing a class of skin widgets — `Black_NN`/`White_NN`-style semi-transparent overlays — that were rendering invisibly against an earlier flat-black backing choice). A dedicated pre-release audit (five parallel review passes) subsequently found and fixed two genuine process-crash bugs (a `std::terminate()` risk on both a first-popout DirectComposition failure and on quitting while a floater was popped out), a masked GPU-allocation-failure that could leave a popped-out window silently frozen, and a real mouse-capture-clearing logic gap.
- S24: fixed the world map's `secondlife://` region-name handler double-un-escaping an already-escaped name (e.g. search results' "Show on Map" link)

#### 🛠️ Debug & Diagnostics

- S24: guarded `DXSwapChain::create()` against being called with an existing swap chain, which previously silently leaked the old chain and its render/depth views
- S24: added flood-protection throttling for texture-cache write failures under heavy churn
- DX_RENDER: removed the watchdog hang-detection system entirely — a deliberate architecture decision (Linden Lab telemetry-serving overhead with no end-user value), roughly 90 call sites removed with no replacement needed
- S24: fixed `NonvisibleObjectsInMemoryTime` so `0` genuinely means unlimited instead of near-zero, and tightened the corresponding KVTweaks spinner range

#### 🔗 Upstream Merges & Bug Fixes

**Task #330 — Linden Lab backport program: 272/272 closed.** The batch
below (124 commits) closes out the program for this release; each was
independently triaged against three questions — is it relevant to S24,
is it compatible with `DX_RENDER`, and is our existing behaviour already
superior — before being merged.

<details>
<summary><b>Stability & crash fixes (18)</b></summary>

- #6148 crash at `LLViewerRegion::addNewObject`
- #6211 crash on `setSelectedEnvironment`
- #6155 crash at `delete_buffers`
- heap-use-after-free in `removeItemsFromAvatar`
- #6172 crash on `recordResultCode`, mutex around `HTTPStats`
- #6125 out-of-bounds write in `writeHighlightSegments`
- #6135 texture-worker cleanup race in `createRequest`/`deleteAllRequests`
- #6320 crash when the containing object is destroyed mid inventory-rename
- #6199 crash in `LLAudioEngine_OpenAL::shutdown()`
- #6113 crash in `LLViewerMedia::updateMedia`
- #6082 model-preview shutdown ordering
- shutdown crash flushing peer-connection stats
- shutdown crash and reduced teardown time for WebRTC
- #6141/#6142 morph-mask cache allocation leaks
- #3814 unhandled `bad_alloc` in `unpackVolumeFaces`
- #6194 tightened SEH exception handling for `mikk::Mikktspace`
- crash loading chat logs/directories containing unicode filenames
- notify waiting objects when mesh-header retries are exhausted

</details>

<details>
<summary><b>Voice & WebRTC (7)</b></summary>

- WebRTC m144 migration, voice lifecycle rework, audio-device-follow behaviour
- timeout + detach on WebRTC shutdown; guard against concurrent stats requests
- fixed an incorrect "voice incompatible with region" error popup
- #6109 defer `OnDevicesUpdated` to a worker thread; guard `StopPlayout`/`ForceStopRecording`
- #6109 reinit the device module on device change (plus two follow-up tuning passes)
- fixed selecting the default device after a region change
- #6167 fixed WebRTC logging level

</details>

<details>
<summary><b>Scripting & the SLua/Luau editor bridge (8)</b></summary>

- full external Lua/Luau script-editor bridge: Lua-aware tokenizer, compile-target plumbing, keyword sync, 1-based Lua line numbering, recompile menu + constant highlighting + script help URLs, shared compiler-dropdown relocation, `LLSyntaxDefCache` refactor, default-to-Lua on Lua-enabled regions, and an external-editor WebSocket bridge (websocketpp modernised for Boost 1.92, including `command.execute`, matching Linden Lab's own design)
- #3731 removed the `HELLO_LSL` fake-success fallback on script load failure
- #5359 skip SLua scripts during a bulk LSL recompile
- fixed `recompile_all` echoing the internal auto-luau target instead of `luau`
- #5574 filter incompatible scripts by metadata
- #6180 preserve script run state when omitted from a save message
- #6192 `object.publish` now sends `can_save_back`
- #6353 missing `name` attribute in `floater_adjust_environment`

</details>

<details>
<summary><b>LEAP API (8)</b></summary>

- #5663 request camera position and orientation
- #5935 objects use a single-line description field (no more newline errors)
- #6188 standardised and implemented missing `system.*` methods
- #6191 implemented `recompile_all` and `reset_all` commands
- #6127 ensured Agent command scope is global
- #6248 closing a minimized search floater no longer reopens the script floater
- #5869 `getSubtree` + #5749 Cut/Copy/Paste/Select All
- #5368 query statistics, wire up `LLStatsListener`, per-frame timer, and logging cleanup

</details>

<details>
<summary><b>Inventory & assets (8)</b></summary>

- #5881 adjusted `llDialog` width/buttons and optimised inventory cache loading
- #6062 fixed duplicate rename AIS requests
- fixed task-inventory metadata parsing (thumbnail/favourite/script runtime)
- #5960 disabled the "new inventory features" popup
- #6094 added bulk folder upload that preserves subfolder structure
- fixed recursive folder pasting
- allow rezzed objects to be saved back to their rezzer's inventory
- #6182 include object permissions for both the root object and linked prims

</details>

<details>
<summary><b>UI & chat (13)</b></summary>

- #6130 fixed a combo box in inventory settings
- #6316 renamed/removed duplicate XUI `name` attributes
- #6117 guarded against stale nearby-chat toast handles
- #2559 fixed emoji splitting link segments and breaking tooltips
- #6106 guarded emoji-picker navigation bounds
- #6203/#6204 fixed chat-history floater page display and up/down arrow behaviour
- #5804/#6070 select and highlight a newly-created ad-hoc chat
- #3703 fixed the disabled HiDPI value in the About floater
- #5907 fixed the flyout button and daycycle-editor Save button using stale textures
- #6248 retain the newest notifications when trimming persisted notifications
- #6243 added double right-mouse-button support
- #1861 five-digit precision for texture-transform spinners
- #6225 fixed tabular-number width inconsistency and font-width checks

</details>

<details>
<summary><b>Miscellaneous (29)</b></summary>

- #5579 ensure own avatar's complexity is kept up to date
- #5732 fixed the Packets Lost statistic underflowing
- #6227 fixed gesture deactivation not working for links
- #6014 shutdown speedup, async inventory-cache compression, misc. perf
- p#692 viewer stats: added VRAM/RAM usage and `cpu_simd` (AVX fixed baseline)
- #6253 smoothed tracked cameras relative to their focus
- #5897 added a menu to open the full profile for People in the Legacy Search
- #6311 fixed `LLWorld` SpaceTime updating
- p#700 fixed an `LLCoordScreen`/`LLCoordWindow` mismatch on screen-change
- fullscreen file dialogs are now interactable (S24-native fix, in the spirit of #4569)
- #6259 avoid throwing while parsing compiler diagnostics
- #6278 max camera distance is now friendly to avatars in motion
- #6322 fixed a truncation issue in `LLBase64::decodeAsString`
- #1841 fixed the viewer attempting to parse a joystick key string into an LLSD
- #5995 fixed Azure translation rejecting large keys
- fixed a SLPlugin error on unloading libcef
- corrected `override` usage in `llimagej2coj`
- fixed an undefined-symbol link error in `llcorehttp`
- #5972 track UI texture changes (S24-native)
- p#662 clarified the attachment-action description in inventory settings
- #5796 added a CEF remote-debugging-port debug setting
- #5368 added logging and general cleanup
- #6352/#6349 reduced jitter inside `gIdleCallbacks`
- #6362 removed unused `LLMessageBuilder::removeLastBlock()`
- replaced `LLFloaterBuyCurrencyHTML` with the new L$ packs web floater (5-commit chain)
- #6071 avatar-preview fix chain; removed the CPU software-skinning fallback
- fixed a timecode debug-overlay crash and `gUIProgram` bind ordering
- #6017 (partial) defer processing `AvatarAppearance` until after the first `ObjectUpdate`
- fixed moving linksets lagging while alt-cammed

</details>

#### 🏗️ Build System & Infrastructure

- S24: bumped to build 3868, Alpha 1.1
- S24: bumped version in preparation for Alpha 1.2
- S24: current release — build 4015, Alpha 1.2

<details>
<summary><b>Alpha 1.0 and earlier — the original DirectX 11 conversion project (r3536–r3855)</b></summary>

Covers roughly 316 commits from the first commit after build 3535
(r3536, 2026-07-12) through Alpha 1.0 (r3855, 2026-09-17): the full
GLSL→HLSL shader port (237/237 files), the deferred renderer brought up
pool-by-pool, reversed-Z depth, real `DXCubeMap`/`DXCubeMapArray`,
Screen-Space Reflections built from scratch, BC7 GPU texture compression,
a real Anaglyph 3D implementation, the night-sky and UI hue-shift
systems, the flat ~70fps HUD-attachment cost fix, and the first several
stages of the tree-wide GL-vocabulary/dead-code retirement (task #300).
Two early upstream merges also landed in this window: a fixed-width
font numeral rendering fix, and an `LLUIImage`-based buffer cache.

The full commit-by-commit detail for this period has been retired from
this README to keep the current release notes focused — see the SVN
history (r3536–r3855) or an earlier revision of this file for the
complete log.

</details>

**Binaries signed:** Codesign Serial: `4e2969400a179e151ba7323da181f8b0`

---

## 📬 Footer

Kirstens Viewer is independently developed. For support, build reports, or contributions, please use the designated contact channels.  
Special thanks to SourceForge for hosting this project and supporting the open source community.

> *"I learned very early the difference between knowing the name of something and knowing something."* — Richard P. Feynman
