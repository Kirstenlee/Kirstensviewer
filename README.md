
# Kirstens Viewer — README

---

<p align="center">
     <img src="https://img.shields.io/badge/build-4706-green.svg" alt="Build">
     <img src="https://img.shields.io/badge/version-Beta%202.0-blueviolet.svg" alt="Beta 2.0">
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
- **DirectX 11 renderer**: a from-scratch native D3D11 backend with its own HLSL shader set. As of Beta 2.0 it is no longer an experiment - it is simply the Kirstens Viewer renderer, and OpenGL is fully retired from the shipped build. See [DirectX 11 Renderer](#-directx-11-renderer).
- **WebView2 web engine** by default (MOAP, H.264/MP4 video, login, search and marketplace pages), with the CEF engine kept as a fallback.
- **Persistent RAM texture cache**: saved at log off and pre-loaded at the next start, with an adaptive, inflow-driven eviction mode.
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
| **4706** | **HRADR** | **Beta 2.0 (current)** |

**Forked from Viewer Develop / 2026.5**

---

## 🎮 DirectX 11 Renderer

Since build 3535, Kirstens Viewer has migrated from OpenGL to a native
**Direct3D 11** renderer. Since r3712 the viewer has shipped only this renderer:
all GLSL shader source and the OpenGL path were removed. As of **Beta 2.0
(build 4706)** the renderer is no longer an experiment: it is the viewer's
renderer, and the "DX" naming has been dropped from the application, its
settings and cache folders, and the window and About text.

Public releases so far: pre-alpha 0.1 (2026-08-23), 0.2, 0.3 and 0.4, then
**Alpha 1.0** (2026-09-17, r3855), **Alpha 1.1** (2026-09-19, r3868),
**Alpha 1.2** (2026-09-27, r4015), **Alpha 1.3** (2026-10-03, r4100),
**Beta 1.0** (2026-10-11, r4168), **Beta 1.1** (2026-10-19, r4255),
**Beta 1.2** (2026-10-30, r4310), **Beta 1.3** (2026-11-08, r4395,
post-release-patched through r4397) and **Beta 2.0** (2026-11-19, r4706), the current release.

**Architecture**: the D3D11 backend lives in `indra/dxrender/`: `core/`
(device, context, state: `DXDevice`, `DXContext`, `DXSwapChain`,
`DXStateCache`) and `resources/` (per-resource wrappers: `DXBuffer`,
`DXTexture`, `DXShader`, `DXSampler`, `DXRenderTarget`, `DXOcclusionQuery`,
and more). `indra/llrender` was retired this cycle and merged entirely into
`dxrender` — render code now lives under a single library. Per-frame
orchestration (`DXPipeline`, the `DXDrawPool*` family) lives in
`indra/newview/`, since it is coupled to `LLPipeline`/`LLDrawPool`.

**Status**: the GLSL→HLSL shader port is complete (237/237 files). The full
deferred renderer is working: opaque, alpha, materials, PBR, avatars,
terrain, water and sky; shadows (sun cascades and spotlights, plus
real-time shadows for local point/omni lights, off by default), local and
spot lights, SSAO, glow and bloom; FXAA, SMAA and Contrast Adaptive
Sharpening; optional AMD FSR1 upscaling; real D3D11 occlusion culling;
reflection probes — with temporal flicker fixed, a corrected ambient/tonemap
balance across sky presets, a same-class champion/runner-up blend that
removes hard cutoffs at probe boundaries, atomic (tear-free) capture, and a
firefly/overbright clamp — alongside the existing per-location probe system
and an optional avatar-centred "Master Reflection Cube" (on by default,
tunable capture radius); anisotropic filtering; real Screen-Space
Reflections; hero-probe mirrors; a real raymarched volumetric-light effect
for the sun; BC7 GPU texture compression (58% VRAM reduction, opt-in); AMD
AGS and NVIDIA NVAPI device-creation parity; and a real Anaglyph 3D
implementation.

The optional VFX effects (including motion blur) read the frame back to the
CPU once per frame for the whole chain, so they carry a performance cost and
are off by default. A hard-edge artifact can still appear at reflection-probe
coverage boundaries on some surfaces under certain lighting; this is still
being investigated.

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

## ✨ Change Log — Beta 2.0 (current release, r4398 → r4706)

Grouped by area. The detailed commit history is in SVN.

#### 🎮 New Features

- **Game Control:** gamepad and controller support ported from the upstream viewer (SDL3).
- **Persistent RAM texture cache:** the KVRAM cache is saved at log off and pre-loaded at the next start, so frequently used textures (your own avatar's especially) stay warm between sessions. A new **Save / Pre-Load Count** sets how many textures are kept (0 turns it off), with separate timeouts for the pre-load and the shutdown save.
- **Adaptive cache eviction:** the RAM cache now paces eviction to how fast textures are arriving and sizes each batch by the cache size, with a pressure valve that drains an overloaded cache quickly, so long flights and heavy scenes no longer choke it. The original count-based mode remains selectable.
- **KV Bench:** a new benchmark floater (Math, RAM, Disk and Graphics, combined into one Kmarks score) in the Extras menu.
- **KV Calculator:** a Second Life centred calculator with live L$ conversion and SL function keys.
- **Floater fade-in:** a smooth fade when a floater opens, tunable in the Interface tab of the S24 Engine panel.
- **Per-tab Reset buttons** across the S24 Engine tuning panel, and two new scheduling toggles on the CPU & Memory tab: **Boost Main Thread (MMCSS High)** and **Low Latency Present**.
- The Solitaire, Reversi and Blackjack console games had their layouts reworked.

#### 🌐 Web, Media & Voice

- **WebView2 is now the default web engine** (MOAP, MP4/H.264 video, audio, login, search and marketplace pages), with the CEF engine kept as a fallback and a Web Engine selector on the Network tab (restart to switch).
- **WebRTC voice** updated to the current upstream library; an opt-in option stops microphone capture while muted (off by default).

#### 🎨 Graphics & Rendering

- **Fixed alpha-glow:** glowing alpha objects now all glow correctly instead of only one lighting up or strobing as the camera moved.
- **Fixed flickering of distant alpha trees:** overlapping far alpha batches no longer swap draw order from frame to frame (new `RenderAlphaSortStickiness` setting).
- **Spot-shadow stability:** fewer shadow slot swaps and a short fade at rest, much improving local lighting in busy scenes.
- **Post-teleport frame drop fixed** (object creation is no longer held back as long after arriving).
- **Edit Shape fix:** a lost texture clamp mode caused a residual blue silhouette artifact.
- **Retuned defaults:** reflection probe content, detail, count and resolution, avatars excluded from reflection captures, Tonemap Everywhere on, shadows in captures off, shadow resolution scale 2.0, Master Reflection Cube radius 32 m, reach-glow 0.15, star-field theme Real Constellations with a subtle glow, jelly-doll ghost values, and the selection beam back to the classic particle style.
- **Skins:** Hyperion, Midnight and LindenX artwork refreshed.
- Smaller shader savings in the sun-shadow, skinned-shadow, volumetric-light and Fresnel paths, and the soften pass skips sky pixels.

#### ⚡ Performance & Frame Pacing

- **A broad frame-pacing pass (over 120 fixes)** across the render path, UI, networking, inventory, textures and threads, including: skipped empty material passes, a single vertex/index upload per flush, UI geometry appended instead of re-uploaded, alpha sorting on small keys, batched avatar culling, shader-constant caching, quadratic loops in the people lists, minimap, inventory sort/teardown and message decode replaced with linear ones, and chat logging moved off the main thread.
- **Swap chain:** flip-model presentation with a waitable frame-latency queue keeps the render-ahead short and input fresh.
- **Threaded UDP receive:** network packets are received and decoded on their own thread; heavy regions (70 avatars in testing) now load objects promptly.
- **Texture pipeline:** off-thread D3D11 texture and mip-chain creation, asynchronous pixel repacking, and a one-time readback for the whole VFX chain.
- **RAM cache fixes:** the disk write-back no longer decodes partial textures (which flooded the texture cache log with failures), and the texture console's Cache reading no longer pulses to zero.
- **Dead-code removal:** roughly 11,900 lines of unused code retired.

#### 🛡️ KRLV

- Closed several ways a restriction could be bypassed: the flycam ignoring `@camunlock`, preview floaters ignoring their view restrictions, locked subfolders being ignored by recursive attach/detach/take-off, and `@setenv` through inventory settings.

#### 🔒 Security & Stability

- A large set of upstream hardening fixes were backported, including a heap use-after-free in draw-info texture matrices, several invalid conversions and casts, parcel overlay edge reads, an external-editor websocket authentication handshake, WebRTC and HTTP shutdown ordering, and particle and model-preview crashes.
- Thread-safety fixes in the network threads, and settings files are now written atomically.
- Hardening and correctness fixes across the common, UI and message libraries (uninitialised members, unterminated buffers, double frees, bounds checks).

#### 🔧 Internals & Build

- **The "DX" suffix is gone:** settings and cache now live in `Kirstens S24` folders, existing `Kirstens S24 DX` profiles are migrated once on first launch, and the window title, About text and device-lost message no longer say DX.
- GL vocabulary retired further (`DXStateEnable`/`DXStateDisable`, a coalesced rasterizer-state guard) and more dead D3D11 plumbing removed.
- Toolchain: vcpkg builds are Release-only, tinygltf, AMD AGS, NVAPI and mikktspace moved onto vcpkg ports, the libwebrtc package refreshed with an installer download cache that checks MD5, and the Store-installed PowerShell location handled.
- The installer and shortcuts now carry the viewer icon.

---

<details>
<summary><b>Beta 1.3 — Graphite skin, KV World Clock, Windows CPU Sets, credential fix (r4311 → r4397)</b></summary>


Grouped by area. The detailed commit history is in SVN.

#### 🎨 Skins & Visual Identity

- **New Graphite skin** — a dark, neutral, pro-tool theme replacing the legacy Flat skin. Built in small, individually-verified layers after an earlier full-rebuild attempt hit unresolved rendering issues: accent-coloured sliders, checkboxes and progress bar, flat underline-style tabs, a coral close button, and the old theme's leftover teal fully purged.
- **Default skin (Aero24) visual pass:** Vista-Pro glass buttons with a matching translucent titlebar, glossy colour toolbar icons with a semantic hue per icon, wireframe inventory folder icons (gold for client folders, blue for system/library), and a centred slider thumb.
- **New Kirstens Viewer login logo** replaces the stock SL hand mark across all five skins, each hue-matched to its own accent colour.
- **Fixed:** menu search-match text was rendering nearly invisible against black backdrops on several skins.

#### 🎮 New Features

- **KV World Clock** floater: SL time plus 11 real-world cities, live DST-aware local time from the Windows timezone database.
- **New Extras menu** consolidating Media Player and S24 Console, plus four new console games — Blackjack, Reversi, Solitaire and Minesweeper.
- **Tonemap Everywhere:** a toggle to enable tonemap/exposure on legacy, pre-HDR sky environments.

#### 🛡️ KRLV — now v1.2.0

- Outfit changes no longer silently drop or detach items locked by `@detach`/`@remattach`/`@remoutfit`.

#### 🔒 Privacy

- Unsolicited ad-hoc group conversation invites from non-friends are now rejected, closing a gap left open by `VoiceCallsFriendsOnly`.

#### ⚡ Performance & Stability

- **Fixed the frame-drop burst on starting to move:** textures for objects that just became visible (turning a corner, leaving occlusion) now refresh immediately instead of waiting up to ~2 seconds on a blind round-robin.
- **Main-loop smoothness pass** fixed several compounding jitter sources: a blocking window-message queue, an unclamped geometry-rebuild budget, audio load bursts, dead-reckoning discontinuities, and async GPU-profiling overhead.
- **Shadow fixes:** directional overshoot on forward-rendered materials, a D3D11 resolution/resize mismatch, point-light crossfade pop, and a spot-shadow matrix mismatch on reflection probes.
- **Local lighting retuned:** reach-glow decoupled from shading intensity, with a longer default reach and a much subtler glow.
- **Windows CPU Sets** core reservation for the main/render/window threads, with AMD X3D cache-aware CCD detection and a manual override.
- **`LLMatrix4::determinant()` was badly broken** — wrong for virtually every matrix, including identity — fixed alongside eight other correctness bugs found in a dedicated math-library audit (quaternion lerp/slerp, ray-box intersection, octree traversal pruning, and more).
- `indra/llrender` retired and merged into `indra/dxrender`; OpenSSL and curl modernized onto vcpkg.

#### 🔧 Internals & Quality

- **A hardening pass** across nine code areas (character animation through plugins/media) fixed four real issues before release: a plugin-shutdown deadlock, a potential UI hang in the multi-slider control, a silently-ignored GPU allocation failure, and an unbounded GPU-query wait.
- `llimage` hardening: fixed a heap overflow and an integer overflow in DXT texture decoding.
- Dead-code sweep: removed a zombie per-frame effects gate, unused avatar counters, and the fully-unused `LLCallStacks` facility.

#### 🩹 Post-release patch (r4396 → r4397)

- **Fixed a severe credential-persistence bug:** on a genuinely fresh profile, saved login/password no longer failed to restore. Root cause traced to the `settings.xml` → `.ini` migration shipped earlier this cycle; that migration has been fully reverted back to the original XML format (every setting value changed since has been preserved).
- Fixed the legacy RC4 credential-decrypt fallback silently failing under OpenSSL 3.x (the "legacy" provider was never loaded).
- Fixed the UI Hue profile dropdown defaulting to the first alphabetical profile instead of the neutral "Default" one on first open.
- Retuned several defaults: reflection-probe content/update-mode/slots/resolution, local light radius/glow, selection beam width, VLC equalizer on by default with the "Full Bass & Treble" preset, and Tonemap Everywhere on by default.

</details>

<details>
<summary><b>Beta 1.2 — point/omni light shadows, real volumetric light, reflection probe polish (r4256 → r4310)</b></summary>

#### 🎨 Graphics & Rendering

- **New, experimental: real-time point/omni light shadows.** Ordinary local lights (prims with "Light" checked, not projectors) can now actually cast shadows instead of shining straight through walls — off by default, with its own toggle under the Lighting tab of the Engine tuning panel. Uses the same proven shadow architecture as sun and spotlight shadows, extended to a 6-face cube capture per light.
- **Volumetric light rewritten.** What used to be a cheap fake "godray" streak is now a real raymarched volumetric-light effect for the sun. Renamed from "Godray Intensity" to just "Intensity" and moved to its own "Volumetric Light" card.
- **Reflection probes refined further:** a same-class champion/runner-up blend removes hard cutoffs where two probes of the same type meet; probe capture is now atomic, removing a source of same-frame tearing; a firefly/overbright clamp was added; and a deep investigation into probe mottling at the lowest detail level confirmed it's an inherent property of that detail tier (not a bug) while catching and fixing two real, separate gaps along the way.
- **Default reflection cube (always-on ambient fallback) overbright fixed** at both high detail tiers and at the lowest detail level.
- **Sculpt prims:** fixed distorted normal-mapped reflections at UV poles.
- **Water:** added dithering, anisotropic filtering and a reflection crossfade/blur pass; a Fresnel floor so grazing-angle reflections never fully vanish; and a horizon haze fade.
- **Profile floater:** Account Type now shows Standard/Special/Internal trust-tier colors.

#### 🛡️ KRLV

- Camera, Clothing/Attachments, Chat/Emotes/IM and the remaining Inventory categories fully closed out: Mouselook-forcing reliability fixed, `@remattach`/`@attachthis`/`@detachthis` reimplemented as real occupancy-aware locks, a cross-cutting fix for every `_sec` command, and a thorough audit that closed several small gaps where a restriction could be silently bypassed.
- KRLV Control floater: a prefix-aware override registry for commands that share a common prefix.
- Fixed a restriction-conflict case where two opposing camera-distance restrictions from different sources could fight each other every frame.

#### 💬 Chat, Messaging & Sign-Up

- **Persistent chat history:** IM and nearby-chat transcripts now survive a restart via a dedicated chat history service, instead of being rebuilt from scratch each session.
- **New in-viewer Join/Sign-Up flow**, plus general-purpose web-modal floaters for showing web content without leaving the viewer.
- "Delete Transcripts" now tells you if it couldn't start (for example, a cleanup already in progress) instead of silently doing nothing.

#### 🖌️ UI & Preferences

- The Colors tab no longer silently resets itself on every login.
- A proper "Reset to Defaults" button was added for the Rendering Advanced tab.

#### 🔧 Internals, Quality & Security

- **A review of every change in this release** found and fixed real issues before shipping: an inventory cache-loading regression that could leave a broken item visible, a double-counted cache diagnostic, the camera-restriction conflict above, and a reflection-probe setting that was quietly paying a real performance cost for no visible benefit at its default value.
- **Security hardening** around sign-in/credential handling in the Join flow.
- Linden Lab upstream backport program closed out again, including an async inventory-cache improvement.
- Large comment-verbosity cleanup across both C++ and shader code: historical narrative and debugging-diary comments trimmed down to short, technical explanations.
- `LLCoordGL` renamed to `LLCoordYUp` tree-wide (~60 files) for clarity — internal only, no user-facing change.

</details>

<details>
<summary><b>Beta 1.1 — reflection probe fixes, Master Reflection Cube, new-user defaults (r4169 → r4255)</b></summary>

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

</details>

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
<summary><b>Alpha releases — KRLV, floater pop-out, the original DirectX 11 conversion project (r3536 → r4105)</b></summary>

Roughly 400 commits across the Alpha series: the full GLSL→HLSL shader port
(237/237 files), the deferred renderer brought up pool by pool, reversed-Z
depth, Screen-Space Reflections, BC7 texture compression, Anaglyph 3D, the
flat HUD-attachment cost fix, the first stages of the GL-vocabulary and
dead-code retirement, the full RLVa command framework (KRLV, 163 commands),
the floater pop-out framework (render-to-texture + DirectComposition), and
the Linden Lab upstream backport program's first close-out (272/272 triaged,
124 commits merged).

</details>

**Beta 2.0 binaries signed:** Authenticode, verified and timestamped, with the same certificate as Beta 1.3.

- Signer: Lee Quick (Open Source Developer), Certum Code Signing 2021 CA
- Certificate serial: `42A6B2AF506CF74D014E99C3C64B2E58`
- Thumbprint (SHA-1): `18DC6635B5F5637EFB33332795F4F652C8047037`
- Valid until: 18 July 2027


---

## 📬 Footer

Kirstens Viewer is independently developed. For support, build reports, or contributions, please use the designated contact channels.  
Special thanks to SourceForge for hosting this project and supporting the open source community.

> *"I learned very early the difference between knowing the name of something and knowing something."* — Richard P. Feynman
