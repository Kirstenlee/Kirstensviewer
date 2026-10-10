
# Kirstens Viewer — README

---

<p align="center">
     <img src="https://img.shields.io/badge/build-4100-green.svg" alt="Build">
     <img src="https://img.shields.io/badge/version-Alpha%201.31-blueviolet.svg" alt="Alpha 1.31">
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
- **Floater pop-out framework** (Alpha 1.2): pop any registered floater into its own movable, resizable desktop window.
- **KRLV — RLVa support** (Alpha 1.3, off by default): a clean-room implementation of the RLVa command API with a single control surface. See [KRLV](#-krlv--rlva-support).

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
| 4015  | HRADR    | Alpha 1.2 |
| **4100** | **—** | **Alpha 1.3** |
| **4105** | **—** | **Alpha 1.31 (current)** |

**Forked from Viewer Develop / 2026.4**

---

## 🎮 DirectX 11 Renderer

Since build 3535, Kirstens Viewer has migrated from OpenGL to a native
**Direct3D 11** renderer. Since r3712 the viewer has shipped only this renderer:
all GLSL shader source and the OpenGL path were removed.

Public releases so far: pre-alpha 0.1 (2026-08-23), 0.2, 0.3 and 0.4, then
**Alpha 1.0** (2026-09-17, r3855), **Alpha 1.1** (2026-09-19, r3868),
**Alpha 1.2** (2026-09-27, r4015) and **Alpha 1.3** (2026-10-03, r4100), the
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
terrain, water and sky; shadows, local and spot lights, SSAO, glow and bloom;
FXAA, SMAA and Contrast Adaptive Sharpening; real D3D11 occlusion culling;
reflection probes; anisotropic filtering; real Screen-Space Reflections;
hero-probe mirrors; BC7 GPU texture compression (58% VRAM reduction, opt-in);
and a real Anaglyph 3D implementation.

**Known gaps**: reflection orientation and quality are still being tuned.
Avatar GPU-cost throttling (AutoFPS) is a no-op under `DX_RENDER`. The optional
VFX motion blur reads the frame back to the CPU each frame, which costs
performance; it is off by default.

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

## ✨ Change Log — Alpha 1.3 (current release, r4016 → r4100)

Grouped by area. The detailed commit history is in SVN.

#### 🛡️ KRLV (new)

- Full RLVa command framework and 163 commands, from the staging pass through to the V1.0.0 frontend.
- KRLV Control floater: Control, Auto-reply, Restrictions, Objects, Communicate, Safeword and Security tabs.
- Command log, owner notices, flood limiting, safeword in all three send paths.
- Object whitelist/blacklist; protected debug settings; blacklist of command names (console-managed).
- Release hardening: secrets refused by `@setdebug_` and `@getdebug_`; account state loaded after login; HMAC-only signing; exact command parsing; per-object exceptions and `@notify` subscriptions; single #RLV folder.
- Persisted KRLV Control floater position.

#### 🎨 Graphics & Rendering

- Contrast Adaptive Sharpening wired into the post chain, on by default (sharpness 0.4).
- SMAA anti-aliasing corrected: blend offset and vertical flip fixed, so it anti-aliases like FXAA.
- Fixed a crash (`CTD in SetShaderResources`) on BC7-upgraded textures under VRAM pressure.
- Shadow bias comments clarified.
- Small hot-path cleanup: cached CAS sharpness, persistent effect buffers, unused CAS uniform removed.
- Removed an unused LL motion blur shader that nothing loaded; the live motion blur is the S24 VFX path.

#### 🖌️ UI, Floaters & Tools

- **Mondy Cam:** the camera floater is redesigned with an accordion panel in place of three tabs, cutting its footprint by 43%.
- **Media Player:** renamed from Music Player; volume and 10-band EQ persist across restarts; VLC equalizer hooks.
- **GLTF Asset Editor:** copy and paste of position, scale and rotation now works.
- **Marketplace Received Items:** fixed a stuck "Searching…" or empty list. Three separate gaps were closed together: a 26.4 regression, a build throttle, and a one-shot login race.
- Gesture and emoji helper floaters are hardened against a failed build.
- Test list-view floater removed.

#### 🔗 Upstream Backports

Each backport was checked against three questions: is it relevant to S24, is it
compatible with `DX_RENDER`, and is our existing behaviour already better.

- UDP flood handling: high and low priority inbound queues, immediate ACKs, so fewer packets are dropped under flood.
- HTTP thread: std::thread with a 60-second shutdown wait, replacing a 250 ms wait that leaked.
- `boost::filesystem` removed from `llfilesystem`; `LLFile` RAII class added.
- Script upload compile-target fix; Luau script icon and script-running constants added.
- Lua and Luau extensions in the script-file picker filter.
- Inventory script-runtime fix on deserialisation.
- Alignment fix: `alignas(16)` across `llmath` and `llcharacter`.
- WebRTC log-spam reduced; dead Darwin code removed from the file picker header.
- Undefined shift in `LLVolumeImplFlexible::remapSections` fixed.

#### 🏗️ Build System & Infrastructure

- Dangling `llstatslistener` references removed, so CMake regenerates cleanly.
- Version bumped for Alpha 1.3 (build 4100).
- Fixed websocketpp `C4005` macro-redefinition warnings: the Boost config now defines `_WEBSOCKETPP_NOEXCEPT_TOKEN_` and `_WEBSOCKETPP_CONSTEXPR_TOKEN_` only if they are not already set (r4104).

#### 🛠️ Post-Release Fixes (after build 4100)

- **Startup freeze at "Compiling shader: Brdf Gen Shader":** the BRDF LUT loop in `genbrdflutF.hlsl` is marked `[loop]`, so the driver compiler does not unroll its 1024 iterations. This is a tentative fix for AMD GPUs and needs confirmation on affected hardware (r4102).
- **KRLV settings:** `KRLVOwnersWereSet` is now declared in `settings.xml`. It was read by the tamper check but missing from the settings file, which logged a warning every minute (r4103).
- **Version:** the About floater and this README now report Alpha 1.31 (r4105).

---

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

**Alpha 1.3 binaries signed:** Authenticode, verified and timestamped.

- Signer: Lee Quick (Open Source Developer), Certum Code Signing 2021 CA
- Certificate serial: `42A6B2AF506CF74D014E99C3C64B2E58`
- Thumbprint (SHA-1): `18DC6635B5F5637EFB33332795F4F652C8047037`
- Valid until: 18 July 2027


---

## 📬 Footer

Kirstens Viewer is independently developed. For support, build reports, or contributions, please use the designated contact channels.  
Special thanks to SourceForge for hosting this project and supporting the open source community.

> *"I learned very early the difference between knowing the name of something and knowing something."* — Richard P. Feynman
