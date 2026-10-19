
# Kirstens Viewer — README

---

<p align="center">
     <img src="https://img.shields.io/badge/build-4255-green.svg" alt="Build">
     <img src="https://img.shields.io/badge/version-Beta%201.1-blueviolet.svg" alt="Beta 1.1">
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
- **DirectX 11 renderer** (`DX_RENDER`): a from-scratch native D3D11 backend with its own HLSL shader set. It is the sole supported renderer; OpenGL is fully retired from the shipped build. See [DirectX 11 Renderer](#-directx-11-renderer).
- **Floater pop-out framework**: pop any registered floater into its own movable, resizable desktop window.
- **KRLV — RLVa support** (off by default): a clean-room implementation of the RLVa command API with a single control surface. See [KRLV](#-krlv--rlva-support).
- **AMD/NVIDIA vendor-API parity**: AGS and NVAPI device creation, Depth Bounds Test, and NVIDIA Reflex on supported hardware.
- **Optional upscaling**: a percentage Render Scale slider with automatic AMD FSR1 (EASU + RCAS) upscaling below 100%.

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
| 4168  | HRADR    | Beta 1.0 |
| **4255** | **HRADR** | **Beta 1.1 (current)** |

**Forked from Viewer Develop / 2026.4**

---

## 🎮 DirectX 11 Renderer

Since build 3535, Kirstens Viewer has migrated from OpenGL to a native
**Direct3D 11** renderer. Since r3712 the viewer has shipped only this renderer:
all GLSL shader source and the OpenGL path were removed.

Public releases so far: pre-alpha 0.1 (2026-08-23), 0.2, 0.3 and 0.4, then
**Alpha 1.0** (2026-09-17, r3855), **Alpha 1.1** (2026-09-19, r3868),
**Alpha 1.2** (2026-09-27, r4015), **Alpha 1.3** (2026-10-03, r4100),
**Beta 1.0** (2026-10-11, r4168) and **Beta 1.1** (2026-10-19, r4255), the
current release.

**Architecture**: the D3D11 backend lives in `indra/dxrender/`: `core/`
(device, context, state: `DXDevice`, `DXContext`, `DXSwapChain`,
`DXStateCache`) and `resources/` (per-resource wrappers: `DXBuffer`,
`DXTexture`, `DXShader`, `DXSampler`, `DXRenderTarget`, `DXOcclusionQuery`,
and more). It is kept separate from the ported `newview`/`llrender` code.
Per-frame orchestration (`DXPipeline`, the `DXDrawPool*` family) lives in
`indra/newview/`, since it is coupled to `LLPipeline`/`LLDrawPool`.

**Status**: the GLSL→HLSL shader port is complete (237/237 files). The full
deferred renderer is working: opaque, alpha, materials, PBR, avatars,
terrain, water and sky; shadows (with cascade/spot-shadow flicker fixed in
Beta 1.0), local and spot lights, SSAO, glow and bloom; FXAA, SMAA and
Contrast Adaptive Sharpening; optional AMD FSR1 upscaling; real D3D11
occlusion culling; reflection probes, with temporal flicker fixed, a
corrected ambient/tonemap balance across sky presets, and an optional
avatar-centred "Master Reflection Cube" (on by default, tunable capture
radius) alongside the existing per-location probe system; anisotropic
filtering; real Screen-Space Reflections; hero-probe mirrors;
BC7 GPU texture compression (58% VRAM reduction, opt-in); AMD AGS and
NVIDIA NVAPI device-creation parity; and a real Anaglyph 3D implementation.

**Known gaps**: AutoFPS (avatar GPU-cost throttling) is a no-op under
`DX_RENDER`. The optional VFX motion blur reads the frame back to the CPU
each frame, which costs performance; it is off by default. SCULPT mesh
objects can show internal faces in some cases. A
hard-edge/circular-boundary artifact can appear at reflection-probe
coverage boundaries on some surfaces; a fix is in progress.

---

## 🛡️ KRLV — RLVa Support

KRLV is the RLVa (Restrained Love Viewer – Advanced) command implementation for
Kirstens Viewer. It is written from the public RLVa API specification, with thanks
to Marine Kelley and Kitty Barnett. Full reference:
[`indra/krlv/README.md`](indra/krlv/README.md).

- **Off by default.** Nothing a scripted object sends has any effect until the resident turns KRLV on in the KRLV Control floater.
- **163 commands in 17 categories**, all registered. A small number have documented scope limits, listed in the KRLV reference. Chat, camera, teleport, inventory, attachments, shared folders, touch, sitting, names, location, group, viewer settings and more.
- **KRLV Control floater:** shown only when the viewer's maturity preference is Adult. It asks for a PIN on every open and re-locks automatically after a set time. The **Restrictions** tab shows each restriction as ACTIVE, DISABLED or SCRIPT.
- **Owners** are told when restrictions change, with flood-limited notices.
- **Safeword:** a three-word phrase that clears every restriction, turns KRLV off and notifies owners. It works in nearby chat, IM and quick chat.
- **Object whitelist and blacklist** (open mode by default), matched on object and owner together.
- **Tamper detection** on the saved state, with a 60-second confirmation so that short clock or file changes do not raise alarms.
- **Signed state:** restrictions, owners and lists are signed with a per-install key and written atomically.
- **Auto-reply** to dropped IMs and chat, with a per-sender cooldown.

---

## ✨ Change Log — Beta 1.1 (current release, r4169 → r4255)

Grouped by area. The detailed commit history is in SVN.

#### 🎨 Graphics & Rendering — Reflections

- **Temporal flicker fixed:** vertical reflective surfaces (shelf backs, pictures, TV-stand fronts) no longer flicker when the camera stops moving. Root cause was several compounding staleness/throttling gaps in the probe-capture scheduler, not a single bug.
- **Night/day ambient and tonemap balance fixed:** indoor rooms and the stock Sunrise/Midday/Sunset/Midnight sky presets now respect the same tonemap curve and ambient settings consistently — a handful of presets were silently falling back to legacy, untonemapped behavior because of a missing data field, not a deliberate "dark at night" choice.
- **New: Master Reflection Cube** (on by default) — an avatar-centred realtime reflection source that fills in where per-location probe selection can't be trusted yet. Capture radius is live-tunable (default 20m) to match the space you're in, with its own controls in the Experimental tab.
- **Materials tab reflection gates** now default to a sensible 0.65 reject/accept cutoff approximating original hardcoded behavior, instead of a no-op 0/0.
- **New-user-friendly defaults:** reflection probe count (128) and content level (manual + terrain/water), draw distance (128m), antialiasing (SMAA, High), and reflection ambient contribution (1.0, full) all retuned as a fairer out-of-box middle ground.

#### 🖌️ UI & Preferences

- S24 Engine (the tuning/preferences panel) QA pass: fixed a text-clipping layout bug on the Movement tab, and added dedicated "Reset to Defaults" buttons for the Alpha, Materials, Cubemap and Master Cube settings groups.

#### 🔧 Internals & Quality

- **LLPipeline → DXPipeline migration continues:** 59 more static members moved over (113 total now ordered by first use), the `LLGLDepthTest`/`LLGLSUIDefault`/`gGL*` matrix-global naming fully retired to `DXDepthTest`/`DXUIDefault`/`gDX*`, and `LLTexture` retired into `DXTextureBase`.
- **Dead-dependency retirement:** the `glext`/`glh_extensions`/`glh_linear` tarball dependencies (a dead WGL loader and an unused VBO pool) removed entirely.
- **Performance:** the hot per-draw matrix-multiply path (`syncMatrices()`) moved to DirectXMath (~16% faster, measured); avatar `updateCharacter()` parallelized; a render-path perf sweep fixed an avatar shader-rebind inefficiency.
- Drawpool naming fully normalized (zero `ll*`-prefixed drawpool files remain); a `dxpipeline.cpp` code audit caught and fixed a real `&=!` vs `&=~` bitwise bug along with several uninitialized-variable issues.

---

<details>
<summary><b>Beta 1.0 — reflections reworked, 360° capture rebuilt, DXVK toggle (r4100 → r4168)</b></summary>

#### 🎨 Graphics & Rendering

- **Reflections reworked:** cube reflections are now separated from ambient lighting, fixing incorrect orientation and visible banding on box-probe reflections.
- **Materials tab:** real gloss/metallic accept-reject gates, plus normal-map, specular-map and projector specular controls.
- **AMD AGS + NVIDIA NVAPI parity:** vendor-specific device creation, Depth Bounds Test on both vendors, AGS breadcrumbs/async shader compile, and NVIDIA Reflex support.
- **AMD FSR1 upscaling:** a percentage Render Scale slider; dropping below 100% automatically engages FSR1 EASU + RCAS upscaling.
- **Shadows:** fixed a shadow cascade / projector light flicker under camera movement.
- **Night sky:** fixed constellations rendering with incorrect orientation.
- **Stability:** a dangling shader-resource-view crash class is now caught and safely skipped instead of crashing the viewer; one remaining unprotected bind site found during pre-release QA has also been fixed.

#### 📸 360° Capture (rebuilt)

- Replaced the old CEF/WebGL-based 360° snapshot system entirely with a native DirectX 11 cubemap capture and equirectangular-projection pipeline — no browser dependency, no JavaScript stitching step.
- New floater: resolution choice, six-face and stitched preview, save to disk.

#### 🌐 Web & Media

- **Fixed:** web sessions (including MFA "remember this device" cookies on sites like the Destination Guide) no longer get forgotten on every restart. The embedded browser's cache was being silently wiped before the browser ever had a chance to use it; it now persists correctly per site.
- **Web Plugin fine control:** a hard cap (default 8) on concurrent background web-browser instances, with least-recently-used eviction, plus a toggle to disable the embedded browser's GPU process for content that doesn't need it. Dullahan/CEF subprocess counts and thread counts are both substantially reduced.
- Fixed harmless but noisy "dropping message: shutdown_plugin" warning spam on every media playback.

#### 🖌️ UI, Floaters & Tools

- **Media Player:** layout polish on the equalizer panel.
- **Experimental: DXVK (Vulkan) backend toggle** — off by default, requires a restart. Lets users opt into running the DX11 renderer through DXVK's Vulkan translation layer without manually placing any files.
- Preferences and tuning panels (S24 Engine) tidied up: clearer spacing and warnings on experimental features, fixed text overflow on the CPU/Memory scheduling card.

#### 🔧 Internals & Quality

- A comprehensive pre-release sanity pass (dead code, hot-path performance, comment clarity, correctness and crash-risk review) was run across everything changed since Alpha 1.3, catching and fixing several real bugs before release — including the dangling-SRV recovery gap above, a silent-failure path in the DXVK toggle, a cache-key case-sensitivity bug, and a per-user cache-separation regression.
- Large comment-verbosity cleanup across the codebase: historical task numbers and narrative commentary stripped down to short, technical explanations.

</details>

<details>
<summary><b>Alpha 1.3 — KRLV hardening, DX_RENDER hot-path cleanup (r4016 → r4105)</b></summary>

- Full RLVa command framework (KRLV) shipped: 163 commands, Control floater, safeword, signed state, object whitelist/blacklist.
- Contrast Adaptive Sharpening wired into the post chain; SMAA blend/flip corrected.
- Fixed a crash on BC7-upgraded textures under VRAM pressure.
- Mondy Cam: camera floater redesigned with an accordion panel, 43% smaller.
- Media Player renamed from Music Player; volume/EQ persistence added.
- Marketplace Received Items stuck "Searching…" bug fixed (three separate causes).
- Upstream Linden Lab backports: UDP flood handling, HTTP thread shutdown, script/Luau fixes, alignment fixes.
- Startup freeze on AMD GPUs compiling the BRDF LUT shader fixed.

</details>

<details>
<summary><b>Alpha 1.2 — floater pop-out, DX_RENDER hardening, LL backports (r3856 → r4015)</b></summary>

- **Floater pop-out framework:** registered floaters (About, nearby chat) pop out to their own desktop window, rendered via render-to-texture and DirectComposition. Multi-instance; input fidelity preserved; hardened over many live-test rounds.
- **Graphics:** alpha shadow cutoff split; spot-shadow depth double-transform fixed; local and spot lights restored in forward-lit shaders; SSAO normal-similarity check; anisotropic filtering as a real level control; `LLImageGL` renamed to `LLImageDX`; BC7 VRAM accounting fixed; dead GL and `LLPostProcess` code removed.
- **Debug:** watchdog hang-detection removed; swap-chain leak guarded; texture-cache write-failure throttling.
- **Upstream:** Linden Lab backport program closed at 272/272 triaged, with 124 commits merged across stability, voice and WebRTC, scripting and the SLua/Luau bridge, LEAP, inventory, UI and chat.
- **Scripting:** full external Lua/Luau script-editor bridge, including `command.execute`.

</details>

<details>
<summary><b>Alpha 1.0 and earlier — the original DirectX 11 conversion project (r3536–r3855)</b></summary>

Roughly 316 commits from the first commit after build 3535 through Alpha 1.0:
the full GLSL→HLSL shader port (237/237 files), the deferred renderer brought up
pool by pool, reversed-Z depth, Screen-Space Reflections, BC7 texture
compression, Anaglyph 3D, the flat HUD-attachment cost fix, and the first stages
of the GL-vocabulary and dead-code retirement.

</details>

**Beta 1.0 binaries signed:** Authenticode, verified and timestamped.

- Signer: Lee Quick (Open Source Developer), Certum Code Signing 2021 CA
- Certificate serial: `42A6B2AF506CF74D014E99C3C64B2E58`
- Thumbprint (SHA-1): `18DC6635B5F5637EFB33332795F4F652C8047037`
- Valid until: 18 July 2027


---

## 📬 Footer

Kirstens Viewer is independently developed. For support, build reports, or contributions, please use the designated contact channels.  
Special thanks to SourceForge for hosting this project and supporting the open source community.

> *"I learned very early the difference between knowing the name of something and knowing something."* — Richard P. Feynman
