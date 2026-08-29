
# ⚠️ Kirstens Viewer — README

---

<p align="center">
     <img src="https://img.shields.io/badge/build-stable-green.svg" alt="Build">
     <img src="https://img.shields.io/badge/platform-Windows-blue.svg" alt="Platform">
     <img src="https://img.shields.io/badge/standard-C%2B%2B20-orange.svg" alt="C++20">
     <img src="https://img.shields.io/badge/renderer-DirectX%2011-9cf.svg" alt="DirectX 11">
</p>

---


## 🖥️ About This Viewer

Kirstens Viewer is a maintained hard-fork of the official Second Life Viewer. It prioritizes fidelity, performance, and compatibility with modern Windows systems.

- **Optimized** for modern multi-core CPUs (AVX2); target audience: high-end PCs & technical users
- **Modern C++**: replacing legacy code
- **Unique build system**: modern vcpkg / PowerShell solution
- **Functionality** aligns with official viewer for consistent user experience
- **DirectX 11 renderer** (`DX_RENDER`): a from-scratch native D3D11 backend running alongside the original OpenGL path, complete with its own HLSL shader set — see [DirectX 11 Renderer](#-directx-11-renderer) below

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
<<<<<<< HEAD
| 3535  | HRADR    | S24.8   |
| 3664  | HRADR    | DX_RENDER 0.1 |
| 3693  | HRADR    | DX_RENDER 0.2 |
=======
| 3690  | HRADR    | S24.8   |
>>>>>>> 36aefafa62b450e16a950eb37292c8aa63793a24

**Forked from Viewer Develop / 2026.3**

---

## 🎮 DirectX 11 Renderer

<<<<<<< HEAD
Since build 3535, Kirstens Viewer has been mid-migration from OpenGL to a
native **Direct3D 11** renderer. 
`DX_RENDER` is the actively developed and tested configuration, publicly
released as pre-alpha 0.1 (2026-08-23) and now at release point **0.2**
(2026-08-29, SVN r3693).

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
FXAA/SMAA, real D3D11 occlusion culling, reflection probes — is working.
Known gaps: hero-probe mirrors still render solid black (off by default),
and reflection-probe banding under some lighting conditions is unresolved.

---

## ✨ Change Log

Every entry below is sourced directly from the SVN commit history, one
line per commit, from the first commit after build 3535 (r3536,
2026-07-12 — the start of the DirectX 11 conversion project) through the
0.2 release point (r3694, 2026-08-29). 159 commits, unedited in substance,
grouped by category.

#### 🎨 Graphics & Rendering

- S24: conversion project stage 2
- S24: render move completed HLSL shaders into the working directory prep for stage 3
- S24: Render - Stage 3 - Milestone 0
- S24: Render - Stage 3 - Milestone 1
- S24: Render - Stage 3 - Milestone 2
- S24: Render - Stage 3 - Milestone 3
- S24: Render - Stage 3 - Milestone 4 (post MS3 a few items cropped up to be dealt with first) so this start WIP!
- S24: Render - Stage 3 - Milestone 4 - stubbed out the drawpool
- S24: Render - Stage 3 - Milestone 4 - basics for texture, WIP
- S24: Render - Stage 3/4 - final commit! we are now moving to the per function implementation stage 5!
- S24: DirectX11 - Stage 5 - Phase 5.1
- S24: DirectX11 - Stage 5 - Phase 5.1 complete starting point draw pool simple
- S24: DirectX11 - Stage 5 - Phase 5.2 - draw pool materials
- S24: DirectX11 - Stage 5 - Phase 5.3 - draw pool alpha and bump!
- S24: DirectX11 - Stage 5 - Phase 5.4a / 5.4b terrain and wlsky
- S24: DirectX11 - Stage 5 - Phase 5.4c water checks ok
- S24: Phase 5.5 of the DX11 conversion, first pass on viewerdisplay, WIP
- S24: DirectX11 - Stage 5 - Phase 5.9 plus a clean up of stub files from stage 3
- S24: DirectX11 - Stage 5 - Phase 5.10a avatar mesh and cloth start
- S24: DirectX11 - Stage 5 - Phase 5.10b avatar skinning
- S24: DirectX11 - Stage 5 - Phase 5.10c drawpoolavatar conversion stage
- S24: DirectX11 - Stage 5 - Phase 5.11
- S24: DirectX11 - Stage 5 - Post phase hitlist
- S24: conversion project stage 6 (discovery) phase 1 - shader compile errors on startup
- S24: Stage 6 (discovery) phase 3 - HLSL compiles, well into the weeds now, close out stage 6, move to stage 7 deep dive
- S24: stage 7 - deep dive
- S24: shader modifications, vary TEXCOORD for input
- S24: shader modifications, vary TEXCOORD for input
- S24: core update
- S24: fix VS/PS interpolant register mismatch tree-wide (96 files)
- S24: Stage 8 phase 1 refinement and live render
- S24: stage 8 - first login!
- S24: stage 8 - part 2 - some inworld rendering, pretty messed up, but this is where the real work begins
- S24: stage 8: fix black-world root cause (LLGLState scope-exit blend-state desync), wire PBR/material texture binding, fix uniform1fv/2fv/3fv no-ops and D3D11's alpha-blend-slot rejection of `*_COLOR` enums, build `DXPipeline::renderGeomPostDeferred()` and wire it into per-frame orchestration
- S24: fix HDR Emissive crash: pbrterrainF.hlsl sampler register overflow
- S24: deferred lighting-combine pass (ambient+atmospherics) and convert LLDrawPoolWater
- S24: add local point lights v1 to deferred lighting; fix DX_RENDER GPU detection (was silently masking RenderReflectionsEnabled and other feature table settings)
- S24: fix terrain texture bleed, UV port bug, triplanar HLSL macro errors, and alpha_ramp blend always reading 0
- S24: partial alpha recovery, z-fighting fixed
- S24: Fix Bug #110 - viewport scaling follows menus
- S24: fix materials rendering grey/black/flat - bind normal/specular maps + per-material shading uniforms (was diffuse-only)
- S24: build real glow/bloom post-fx pass - restructure present chain through a real intermediate buffer, wire generateGlow/combineGlow
- S24: fix water tide-rise/seesaw bug - renderGeomPostDeferred() never reset the model matrix between pools
- S24: Windlight atmospherics live - fix clouds never rendering under DX_RENDER
- S24: add reflection probe capture pipeline (task #147) - real D3D11 cubemap array backing, capture pass wiring, per-pixel multi-probe blend
- S24: fix local point/spot lights vanishing near camera (task #161)
- S24: fix GLTF UBO crash + $Globals cbuffer slot-0 collision (task #79)
- S24: fix Transparency+classic Glow color/emissive COLOR0 collision (task #159)
- S24: fix bump/normal-map texture never bound in DXDrawPoolBump (task #162)
- S24: fix sky/cloud rendering - UV-singularity aliasing + missing HDR tonemap curve
- S24: PBR+R gaps and AVATAR explanation - root cause of invisible rigged mesh avatars found and fixed
- S24: fix DXVertexLayout WEIGHT4 bit mapping and guard DXTexture copySubImageFromFrameBuffer against an invalid-region device-removed crash
- S24: fix SSAO noise/depth texture sampling missing GL-to-D3D11 texture-origin flip
- S24: real directional/spot shadow-map rendering working end-to-end (task #158)
- S24: screen flip / origin bugs in shaders, testing WIP
- S24: PBR alpha WIP, attenuation mixed up
- S24: shiny primitives WIP
- S24: shiny primitives / reflections part 2, WIP
- S24: HW light issues WIP
- S24: CTD in pbropaque vary_sign
- S24: missing gaps between GLSL and HLSL in pbralpha
- S24: compressed texture gap fixed
- S24: Post Pipeline - FXAA & KV 64bit DOF wired in
- S24: FEATURE - OpenCL effect VFX coded for DX RENDER
- S24: flip imposters for correct render
- S24: fix degenerate triangle LINE_LOOP visualization in model preview
- S24: atmospherics and water haze as per LL spec
- S24: clear-colour durability fix
- S24: HW light system (custom) V1
- S24: GLTF gaps closed
- S24: fix stale texture-bind dedup causing wrong-texture flashes and redundant flush churn under VRAM-pressure downscaling
- S24: fix CEF content texture colour format issue
- S24: alpha research gaps, small fixes
- S24: REVERT - DX syncLightState TODO
- S24: full FXAA/SMAA HLSL implementation
- S24: FXAA enhanced
- S24: HLSL sampleSpotShadow re-ported
- S24: GLTF PBR material fixes
- S24: shader fixes ported from GLSL to HLSL
- S24: cull face not carried over to DX11 pipeline (fixed)
- S24: occlusion culling wired live - real D3D11 occlusion queries, reflection-probe CTD fix, TRIANGLE_FAN false-occlusion fix
- S24: add DXOcclusionQuery (real D3D11 occlusion query wrapper)
- S24: cubemap deep dive and reflection / SSR, WIP
- S24: glow shader fix, tested OK
- S24: SSAO improvements - blur
- S24: SSAO WIP (fine tuning)
- S24: snapshot system fix crop / format issues and improve upscaler with new HLSL shader
- S24: fix texture format gaps (retain fallback telemetry)
- S24: glPolygonOffset refactor to native DX pattern
- S24: partial alpha issues, edge cases for sculpts and legacy linksets
- S24: remove GL-era hacks for snapshot preview, including the S24 FBO workaround
- S24: Graphics Upgrade 1 - stars (real twinkle, star colour, stardust band, flare/bloom)

#### ⚡ Performance & Optimisation

- S24: split GL and DX cache
- S24: texture / media threading performance tweaks
- S24: DX Vsync method replacing GL version - S24 V1
- S24: proper UI batching mechanism - S24 V1
- S24: GPU eviction code WIP (good initial setup)
- S24: RAM CACHE V10 - real fix for KVRAMCache's disk-write-on-eviction gap
- S24: sTextureBytes proper side tracking mechanism (fallback)
- S24: RAM CACHE - cleanup and wiring proper priority plumbing V11
- S24: VFX cache controls to reduce hot path impact
- S24: flip-model swap chain with tearing support for VSync-off
- S24: startup dialog / splashscreen running on a tiny dedicated thread
- S24: main thread identity
- S24: Stage 9 - remove redundant VBO work queue
- S24: task #257 dedicated DXImageThread, replaces LLImageGLThread under DX_RENDER
- S24: modernise GPU eviction logic & console
- S24: fix reflection-probe uniform buffer redundant rebuild + missing per-frame refresh (task #267)
- S24: shader cache mechanism, WIP
- S24: 0.2 perf pass, texture dedup reverted
- S24: more cached controls, and GL cleanup

#### 🖌️ UI, Themes & Skins

- S24: Stage 6 (discovery) phase 2 - a running DX window, now working on UI visibility issues
- S24: Fix Bug #106 - UI Shader draw with undefined primitive topology
- S24: Fix Bug #129 - real D3D11 scissor-rect support, fixes UI clip overspill (inventory lists, profile divider)
- S24: HUD partial fix - reset LLRenderTarget::sBoundTarget bookkeeping so a HUD's second per-frame render pass no longer obliterates the UI
- S24: clean-up initial window creation when maximised from start
- S24: partial fix on font vertex-buffer code
- S24: nametag fix, viewport offset compensation for chrome
- S24: meaningful user feedback during startup (real shader-compile progress on the splash screen)
- S24: selection beam HUD issues
- S24: voice visualiser fixed
- S24: misc HUD related work
- S24: restore native UI text/image vertex caching
- S24: HUD-space render target fix
- S24: Edit Shape preview icons - mRT swap + viewport/depth-clear fixes (partial, see task #256)
- S24: fix hover-highlight flicker (side effect of DX UI context) - solved
- S24: genuine gap and potential fix for some HUDs displaying black (user feedback)
- S24: fix edit-mode mesh selection outline rendering solid instead of wireframe
- S24: fix font glyph render path - issue #254 from 0.1 alpha
- S24: smooth nametag & voice-dot occlusion-based fading, replacing the depth-blend approach
=======
- Snapshot Build 3690

#### 🎨 Graphics & Rendering

- S24: DX11 conversion project

#### ⚡ Performance & Optimisation

TBA

#### 🖌️ UI, Themes & Skins

UI running in own DX context
>>>>>>> 36aefafa62b450e16a950eb37292c8aa63793a24

#### 🔊 Audio

#### 🎥 Camera & Input

#### 🛠️ Debug & Diagnostics

<<<<<<< HEAD
- S24: stages 5.7/5.8 skipped for now - debug consoles will be handled in a later conversion phase
- S24: clean up temporary glow-investigation diagnostic (task #160)
- S24: native frame stats / FPS metrics
- S24: temp diagnostic leftovers cleanup
- S24: advanced debug toggle for DX RENDER path diagnostics
- S24: clean up stale comment
- S24: KVTweaks debug layer off by default
- S24: lazy-compile Deferred Buffer Visualization shader to unblock an AMD startup lockup
- S24: wire Buffer Visualization dev tool into DXPipeline + fix stale GL-tail comments (task #270)
- S24: user-reported issue (GitHub) - fix crash on first run, before the login screen
- S24: raw-bind audit
- S24: fix resets and folder paths based on build type, GL / DX

=======
>>>>>>> 36aefafa62b450e16a950eb37292c8aa63793a24
#### 🔒 Privacy & Moderation

#### 🔗 Upstream Merges & Bug Fixes

<<<<<<< HEAD
- 5966 Fixed width font's numbers
- 5972 LLUIImage based buffer cache

#### 🏗️ Build System & Infrastructure

- S24: post-boost-conversion cleanup - silence some noise, clean up comments
- S24: clear temp workspace for next stage
- S24: bump the version - and the client core
- S24: Roaming directory changed, separation from the GL client
- S24: pre-alpha 0.1, build 3665 - 2026-08-23
- S24: DX thread infrastructure, for future use
- S24: code cleanup
- S24: prep for 0.2 alpha release
- S24: fix stale Boost baseline fallback
- S24: update packager script
=======
#### 🏗️ Build System & Infrastructure

- S24: vcpkg migration - Boost 1.912- fp:fast AVX2
- S24: Fix include() case mismatches for renamed cmake modules (GLM/ZLIB/APR/CURL/etc.) surfaced by CMake 4.4.0's new case-insensitive module
>>>>>>> 36aefafa62b450e16a950eb37292c8aa63793a24

**Binaries signed:** Codesign Serial: `4e2969400a179e151ba7323da181f8b0`
---

## 📬 Footer

Kirstens Viewer is independently developed. For support, build reports, or contributions, please use the designated contact channels.  
Special thanks to SourceForge for hosting this project and supporting the open source community.

> *"I learned very early the difference between knowing the name of something and knowing something."* — Richard P. Feynman
