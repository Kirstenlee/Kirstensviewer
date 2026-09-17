
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

Kirstens Viewer is a maintained fork of the official Second Life Viewer. It prioritizes fidelity, performance, and compatibility with modern Windows systems.

- **Optimized** for modern multi-core CPUs (AVX2); target audience: high-end PCs & technical users
- **Modern C++**: replacing legacy code
- **Unique build system**: modern vcpkg / PowerShell solution
- **Functionality** aligns with official viewer for consistent user experience
- **DirectX 11 renderer** (`DX_RENDER`): a from-scratch native D3D11 backend, complete with its own HLSL shader set — as of build 3712 the sole supported renderer, with OpenGL fully retired from the shipped build — see [DirectX 11 Renderer](#-directx-11-renderer) below

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
| 3664  | HRADR    | DX_RENDER 0.1 |
| 3693  | HRADR    | DX_RENDER 0.2 |
| 3745  | HRADR    | DX_RENDER 0.3 |
| 3783  | HRADR    | DX_RENDER 0.4 |
| 3855  | HRADR    | Alpha 1.0 |

**Forked from Viewer Develop / 2026.3**

---

## 🎮 DirectX 11 Renderer

Since build 3535, Kirstens Viewer has migrated from OpenGL to a native
**Direct3D 11** renderer. As of r3712 the dual-path era ended outright — all
GLSL shader source and the OpenGL rendering path were removed, so
`DX_RENDER` is now the viewer's only renderer. `DX_RENDER` was publicly
released as pre-alpha 0.1 (2026-08-23), reached release point **0.2**
(2026-08-29, SVN r3693), pre-alpha **0.3** (2026-09-05, SVN r3745),
pre-alpha **0.4** (2026-09-11, SVN r3783), and is now **Alpha 1.0**
(2026-09-17, SVN r3855) — the "pre-" qualifier is dropped from this point
on. The longer-term GL-era class-name/scaffolding retirement (task #300)
that ran alongside 0.4 is now substantially complete: `llgl.cpp`,
`llrender.cpp`, `llviewerwindow.cpp`, `llviewerdisplay.cpp`,
`llrender2dutils.cpp`, `llspatialpartition.cpp`, `pipeline.cpp`,
`llshadermgr.cpp`, and `kveffects.cpp/.h` have all had their dead GL
branches purged (several thousand combined dead lines removed), the
tree-wide `glLineWidth`/`glPolygonMode`/`glPointSize` sweep (task #311) is
done, and the confirmed no-op `stop_glerror()` macro is gone along with its
~154 call sites.

**Architecture**: the D3D11 backend lives in its own module,
`indra/dxrender/` — `core/` (device/context/state: `DXDevice`,
`DXContext`, `DXSwapChain`, `DXStateCache`) and `resources/` (thin
per-resource wrappers: `DXBuffer`, `DXTexture`, `DXShader`, `DXSampler`,
`DXRenderTarget`, `DXOcclusionQuery`, and more) — kept deliberately
separate from the ported `newview`/`llrender` code rather than smearing
D3D11 calls throughout the original GL call sites. Per-frame orchestration
(`DXPipeline`, the `DXDrawPool*` family) lives in `indra/newview/`
alongside its GL counterparts, since it's tightly coupled to
`LLPipeline`/`LLDrawPool`. Since 0.4, `LLGLState` itself was refactored into
a real `DXState`, and the legacy `lldrawpoolsimple`/`materials`/`water`/
`bump`/`terrain` GL implementations were retired outright — their DX
counterparts were always the real path under `DX_RENDER`.

**Status**: the GLSL→HLSL shader port is complete (237/237 files
converted). The full deferred renderer — opaque/alpha/materials/PBR/
avatars/terrain/water/sky, shadows, local/spot lights, SSAO, glow/bloom,
FXAA/SMAA, real D3D11 occlusion culling, reflection probes — is working.
Since 0.2, the depth buffer moved to reversed-Z (fixing z-fighting and
distant water shore-fade draining), and the legacy `LLCubeMap`/
`LLCubeMapArray` classes were replaced with real `DXCubeMap`/
`DXCubeMapArray` equivalents (fixing the hero-probe mirrors along the way).
Since 0.3, real Screen-Space Reflections went live, hero-probe mirrors were
fixed for good, box-probe banding was addressed on four separate fronts,
and a real BC7 GPU texture-compression pipeline shipped (58% VRAM
reduction on eligible content, opt-in). Since 0.4, a real Anaglyph 3D
implementation replaced a 90+ site GL colormask hack, water gained
live-wind-driven waves and depth-based color absorption, the night sky and
UI color system both got a second pass (a real dynamic hue-shift system for
UI chrome), and a flat ~70fps cost that hit every scene with anything
HUD-attached was root-caused and fixed (a per-frame render-type-mask toggle
was hiding a full-scene geometry-dirty side effect meant for a one-time
settings change, not internal bookkeeping). Known gaps: reflection quality
is still being tuned by eye rather than fully derived, and avatar GPU-cost
throttling (AutoFPS) remains a known no-op under `DX_RENDER`.

---

## ✨ Change Log

Every entry below is sourced directly from the SVN commit history, from
the first commit after build 3535 (r3536, 2026-07-12 — the start of the
DirectX 11 conversion project) through the current Alpha 1.0 point (r3855,
2026-09-17) — roughly 316 commits total, grouped by category and
compressed where several commits form one continuous piece of work rather
than kept one-line-per-commit. The earliest development-diary-style
entries (per-milestone/phase WIP commits from the initial GLSL→HLSL
conversion) have been merged into a handful of stage-level summaries to
keep the log readable rather than exhaustive.

#### 🎨 Graphics & Rendering

- S24: DX11 conversion stages 2-4 — HLSL shader source migrated into the working tree; draw-pool architecture stubbed out (simple/materials/alpha/bump/terrain/wlsky/water) with first texture-binding basics
- S24: DX11 conversion stage 5 — per-drawpool D3D11 implementation, one phase per pool (simple, materials, alpha/bump, terrain/wlsky, water, viewerdisplay, avatar mesh/skinning/cloth/drawpool), ending with a post-phase cleanup hitlist
- S24: DX11 conversion stages 6-7 — first HLSL compile pass (fixed startup shader-compile errors) then a deep-dive debugging stage; fixed a tree-wide VS/PS interpolant register mismatch (96 files) that was shifting every varying
- S24: DX11 conversion stage 8 — first successful login and in-world render; root-caused the "black world" bug (an `LLGLState` scope-exit blend-state desync), wired PBR/material texture binding, fixed `uniform1fv`/`2fv`/`3fv` no-ops and D3D11's alpha-blend-slot rejection of `*_COLOR` enums, and built `DXPipeline::renderGeomPostDeferred()` into per-frame orchestration
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
- S24: CTD in pbropaque vary_sign
- S24: compressed texture gap fixed
- S24: Post Pipeline - FXAA & KV 64bit DOF wired in
- S24: FEATURE - OpenCL effect VFX coded for DX RENDER
- S24: flip imposters for correct render
- S24: fix degenerate triangle LINE_LOOP visualization in model preview
- S24: atmospherics and water haze as per LL spec
- S24: fix stale texture-bind dedup causing wrong-texture flashes and redundant flush churn under VRAM-pressure downscaling
- S24: fix CEF content texture colour format issue
- S24: full FXAA/SMAA HLSL implementation, then a follow-up enhancement pass
- S24: HLSL sampleSpotShadow re-ported
- S24: GLTF PBR material fixes
- S24: cull face not carried over to DX11 pipeline (fixed)
- S24: occlusion culling wired live - real D3D11 occlusion queries, reflection-probe CTD fix, TRIANGLE_FAN false-occlusion fix
- S24: add DXOcclusionQuery (real D3D11 occlusion query wrapper)
- S24: cubemap deep dive and reflection / SSR, WIP
- S24: glow shader fix, tested OK
- S24: SSAO blur and fine-tuning pass
- S24: snapshot system fix crop / format issues and improve upscaler with new HLSL shader
- S24: fix texture format gaps (retain fallback telemetry)
- S24: glPolygonOffset refactor to native DX pattern
- S24: partial alpha issues, edge cases for sculpts and legacy linksets
- S24: remove GL-era hacks for snapshot preview, including the S24 FBO workaround
- S24: Graphics Upgrade 1 - stars (real twinkle, star colour, stardust band, flare/bloom)
- S24: math/LLVector4a optimisation pass; fixed a crash on Japanese fonts
- DX_RENDER: reversed-Z depth buffer conversion (near=1.0/far=0.0) - fixes z-fighting/surface bleed-through and water shore-fade draining at distance, plus matching fixes for stars/moon/sun draw order, DoF circle-of-confusion reconstruction, and a stale reversed-Z depth-func default in DXUIBatch
- DX_RENDER: adapted AYAstorm's 3-block attachment-order alpha fix for D3D11; general DOF fixes
- DX_RENDER: removed all GLSL shader source - DX-only from here on, no more dual GL/DX rendering support - followed by a GL-era core refactor (LLGLSLShader respecified, dead GL init paths retired from llgl.cpp/h)
- DX_RENDER: replaced the legacy LLCubeMap/LLCubeMapArray classes with real DXCubeMap/DXCubeMapArray backed by a proven closed-form radiance/irradiance formula, fixing the hero-probe mirrors; a follow-up pbropaqueF.hlsl front-face-normal fix fixed them for good
- DX_RENDER: fixed a reflection-probe mip-count off-by-one and a missing max_probe_lod uniform for PBR alpha materials (also mirrored into the hero-probe manager); added a tunable SSR glossiness threshold and fixed box-probe automatic-fallback weight scaling
- DX_RENDER: fixed the legacy reflection env-map producer/bind mismatch (water gets a real legacy fallback) and a linear/sRGB color-space bug in environmentMap sampling for water reflections
- DX_RENDER: stopped eagerly compiling the dead Screen Space Reflection Post shader - was hanging startup on some GPUs
- DX_RENDER: Stars WOW part 2 and a new Sky 2.5D cloud-layer system; nebula/shooting-star/procedural sky elements now fade correctly with daytime
- DX_RENDER: fixed the build-tool X/Y/Z readout's HUD/manipulation-line offset - a chrome-rect mismatch that scaled with menu/favorites-bar height
- DX_RENDER: fixed an avatar GPU-profiling crash in the Performance floater's Nearby tab
- S24: removed deprecated Highlight glow (158 lines); fixed the Film Menu's shader toggles; renamed gGLActive to gDXActive; fixed a font-collection use-after-free/leak and a log-file corruption bug; reverted a failed viewport-to-shader Y-flip experiment; beacon (find/sun-moon) rewrite with real DX11 billboard geometry
- DX_RENDER: retired the dead GL-era `lldrawpoolalpha`/`lldrawpoolwlsky` implementations (Stage 9 GL removal) — `DXDrawPoolAlpha`/`DXDrawPoolWLSky` were always the real path under `DX_RENDER`; ~1,300 combined dead lines removed
- DX_RENDER: added `dxLineWidth()`, a shared D3D11 line-width replacement (real camera-facing billboard geometry, screen-space-constant width) for the selection beam, the find/sun-moon beacon, and parcel-edit boundary posts; also gave the selection beam a real width setting instead of a hardcoded constant
- DX_RENDER: fixed black dots under 2.5D clouds at opacity above 1.0, and nebula/shooting stars fading on moonlight instead of sun elevation
- DX_RENDER: fixed a real green tint in the ACES tonemap (an HLSL/GLSL row-vs-column-major matrix mismatch, same bug class as the earlier TBN fix); added ACES (Fast) and AgX tonemap options; factory default changed from ACES to Khronos Neutral
- DX_RENDER: code-audit pass — straight-lined dead GL `#ifdef`s in `pipeline.cpp`, fixed real uninitialized-read bugs (raycast results, shadow matrices) and a spot-shadow/reflection-probe-capture state cross-contamination bug; GL-era naming/dead-code purge elsewhere (`GLfloat`→`F32`, `GLboolean`→`bool`, `getOpenGLTransform()`→`getDirectXTransform()`, 3 confirmed-dead functions removed)
- DX_RENDER: reflection-probe auto-placement no longer escapes through out-of-frustum walls; removed the hardcoded SSR gloss threshold that structurally excluded glass/PBR-alpha materials from ever reaching reflections
- DX_RENDER: Screen-Space Reflections rebuilt from scratch — fixed a missing perspective-divide guard, a wrong (current- instead of last-frame) reprojection matrix, and a Y-flip double-application; real reflections working for the first time, with a first tuning pass on blend/confidence/black-hit rejection
- DX_RENDER: water/reflection-probe quality pass — fixed visibly-pixellated wave-normal feed into SSR, and four distinct box-probe banding causes (blend-fade falloff, unclamped mip LOD, sun-relative lighting baked into probe captures); added Equalize Faces and Opacity reflection-probe tuning controls
- DX_RENDER: fixed torn/incoherent "hall of mirrors" SSR reflections — the per-sample jitter was locked to a single fixed diagonal instead of being genuinely 2D and centered; also improved march convergence and firefly suppression
- DX_RENDER: fixed a real crash when disabling Screen Space Reflections (a stale, lower-quality cached shader source getting reused by a shader that still needed the higher tier)
- DX_RENDER: fixed hero-probe mirrors working correctly only once per process — a render-target cleanup path never reset an internal depth flag, so the viewer silently stopped reallocating the mirror's render target after the first use
- DX_RENDER: fixed rotated UI icons (e.g. inventory folder disclosure triangles) drawing at the wrong screen position instead of appearing invisible — the rotated-icon path never added the accumulated UI translation into its own vertex positions
- DX_RENDER: fixed SSAO/SSR temporal-reprojection flicker — the HUD camera was overwriting the main view's last-frame matrix every frame
- DX_RENDER: real BC7 GPU texture-compression pipeline (background-thread encode, live resource swap, zero added latency) replaces the long-dead GL-era compression checkbox — 58% real VRAM reduction confirmed live on eligible content, opt-in
- DX_RENDER: fixed PBR terrain never actually rendering on any paint-type permutation — an unused vertex-input field was silently breaking GPU input-layout creation for every permutation
- DX_RENDER: replaced the flat opaque grey "jelly doll" look for too-complex/too-slow avatars with a translucent, rim-glowing ghost silhouette — same cached-impostor performance cost, far less immersion-breaking
- S24: real Anaglyph 3D V12 (HLSL-based) — a dual-render-target + composite shader replaced a 90+ site GL colormask hack; also introduced a GL-vocabulary-free `DXenum` type to adhoc-retire the ad-hoc `LLGLenum` typedef going forward
- S24: retuned the hair/alpha shadow blend cutoff and shadow dither values (confirmed byte-identical to upstream, fixed as a real quality improvement anyway) and re-tuned BC7 compression for fine hair alpha detail
- DX_RENDER: retired the legacy `lldrawpoolsimple`/`materials`/`water`/`bump`/`terrain` GL implementations outright — their DX counterparts were always the real path under `DX_RENDER`; also consolidated SMAA's sampler binds
- DX_RENDER: Night Sky WOW V2
- S24: `LLGLState` refactored into a real `DXState`
- S24: fixed a terrain-paintmap shader chicken-and-egg crash and its remaining GL calls
- DX_RENDER: fixed a real glow regression (a debug-overlay code path was silently wired live, leaking a colorWriteMask override into normal rendering) and a Fullbright+Shiny white-texture bug (a hardcoded texture unit plus a stale shader-level gate on batched textures)
- DX_RENDER: Water WOW V1 — live-wind-driven wave direction/speed and depth-based color absorption, plus a wind-strength cap so storms can't over-drive wave speed
- S24: fixed a hair/skirt cull-direction gap under DX11
- DX_RENDER: task #300 (full GL removal) continued through most of the tree — dead GL branches/landmines purged from `llgl.cpp` (~900 lines), `llrender.cpp` (-477 lines), `llviewerwindow.cpp`, `llrender2dutils.cpp`, `llviewerdisplay.cpp`, `kveffects.cpp/.h`, `llspatialpartition.cpp`, `llshadermgr.cpp`, and a dead shadow-map filtering block in `pipeline.cpp`; finished the tree-wide `glLineWidth`/`glPolygonMode`/`glPointSize` sweep (task #311); removed the confirmed no-op `stop_glerror()` macro and its ~154 call sites
- DX_RENDER: Alpha 1.0 QA pass — fixed a hero-probe glossiness-gating bug that was wasting a cubemap sample on every legacy-shaded pixel during mirror renders, and a byte-identical-to-GL water specular-alpha bug that had always evaluated to zero

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
- DX_RENDER: GPU eviction tuning; added a dedicated DXPool worker-thread pool and parallelized idleUpdate() for non-avatar objects (task #283)
- DX_RENDER: full async shader-compile cache — `D3DCompile()` now prefetches ahead of time on a worker thread instead of blocking the frame it's needed on, and cache coverage went from a staged per-shader allowlist to blanket coverage; also fixed the long-dead "Reload Vertex Shader" developer menu entry
- DX_RENDER: perf tuning pass — hoisted redundant per-plane math out of the camera frustum-clipping hot loop, gated a redundant per-draw uniform upload behind a last-value check, and fused two separate passes over the active-object list into one
- DX_RENDER: removed a leftover per-frame diagnostic log with a real, measurable frame-time cost (including one line firing on every reflection-probe occlusion-query result); avoided redundant per-drawable uniform uploads in the alpha pool
- DX_RENDER: matrix-upload caching for `syncMatrices()` — skips redundant vertex/pixel constant recomputation and re-upload when the camera hasn't actually moved since the shader's last sync
- DX_RENDER: fixed a texture disk-cache write race — thousands of textures per session were silently skipping the on-disk cache due to a shared-pointer race with the main thread; the write path now takes its own private copy before handing off
- DX_RENDER: re-fixed redundant D3D11 texture/sampler rebinds (skips redundant GPU calls when nothing actually changed) — an earlier attempt regressed lit-surface rendering and was reverted; root-caused to a render-target bind path that bypassed the cache entirely, and re-fixed with independent texture/sampler change-tracking
- S24: repurposed a GL-era CPU-bias slider into a real EcoQoS control (0=Eco/1=Normal/2=High) via real Windows process/thread scheduling APIs
- S24: HLSL optimising sweep — an SSR adaptive sample-count `max()` was dead code, the hero-probe was being sampled before being discarded by weight, and a duplicate spotlight `pbrPunctual()` call was computed twice for no reason
- S24: fixed frame-time budget enforcement in the geometry rebuild queues
- S24: shader-reload performance fixes — cached raw shader file text across settings-triggered reloads instead of re-reading from disk, scoped per-shader defines so one program's reload no longer invalidated every other program's bytecode cache entry, and stopped a blanket shader-cache wipe on enabling Mirrors
- DX_RENDER: fixed a flat ~70fps cost on any scene with anything HUD-attached — `render_hud_attachments()`'s per-frame render-type-mask bookkeeping was triggering `markAllGeometryDirty()` (a full octree traversal meant for a one-time settings change) every single frame; also hardened the VO cache and drawable damped-interpolation paths against the same spoofed-camera-ID HUD render pass

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
- DX_RENDER: quieted unwanted log spam (unknown-avatar-animation warnings, voice mPrimary/mute-info messages); removed the dead "Info" Set Logging Level menu entry - a production build only ever has WARNS or none
- S24: trimmed overspill and dropped an orphaned header/verbose debug text from the KVTweaks Rendering Adv tab; added a real S24 Engine entry point to the Preferences menu; renamed the Texture Compression control to match its current system; general World-menu cleanup
- S24: fixed a build-gizmo scaling bug; Engine Guide accuracy fixes; improved shader-recompile feedback during graphical setting changes (real progress on the splash screen instead of an apparent hang)
- DX_RENDER: UI WOW — a real, HLSL-driven dynamic hue-shift system for UI chrome (floaters/menus/controls/text/inventory/map/script/misc, independently rotatable), plus a starter "Malachite" color profile
- DX_RENDER: Alpha 1.0 UI QA pass — root-caused and fixed the Quick Chat bar being completely undraggable (its drag handle is always full-panel-sized regardless of header settings; the compact layout left zero unclaimed pixels for it to ever catch a mousedown) and hardened its toolbar-docking fallback; fixed 6 tab-overspill bugs and assorted stale-comment/dead-setting issues across the KVTweaks floater's 17 tabs

#### 🔊 Audio

- S24: prevented an audio gain boost during the Progressview loading sequence

#### 🎥 Camera & Input

- DX_RENDER: fixed SpaceNavigator and Xbox-style/generic game controllers being completely non-functional — the build flag gating the whole joystick/controller subsystem was never actually being set by the build system, silently compiling it out entirely (predates `DX_RENDER`)

#### 🛠️ Debug & Diagnostics

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
- DX_RENDER: cleaned up 6 leftover investigation diagnostics and a dead counter; fixed 2 shader compile failures and 5 real bugs found during a warnings cleanup pass; fixed a handful of misc runtime issues (wasted shadow-map allocation, a dead shader cache, misleading warnings, a missing emoji asset); fixed a flush_glerror() crash risk; expanded the shader bytecode cache to cover FXAA, SMAA, and the GLTF/PBR shader family
- DX_RENDER: downgraded several non-actionable warnings from WARN to DEBUG (a routine mute-list cache clear, a malformed cached UUID string, a benign avatar-appearance packet-ordering artifact, an animation exceeding an old joint-constraint cap) — none were things a user could act on, just log noise
- S24: codebase-wide comment cleanup (330 files, net -8,465 lines) and retired the entire `LL_PROFILER`/Tracy system (1,088 dead `LL_PROFILE_*` call sites removed, 42 orphaned Fast-Timer handles fixed/deleted)
- S24: background `WorkQueue` task exceptions are now caught and logged instead of taking down the whole process; audited the Experimental features tab for real vs. non-functional connections
- S24: Develop-menu deep dive — rewired Debug/Crash menu settings, wired up render tests and metadata debug overlays, tuned the wireframe background, fixed a debug-camera format bug, and finished a full cleanup pass leaving the Develop menu fully DX_RENDER-clean (freed several Ctrl+Alt+F shortcuts for reuse along the way)
- S24: fixed a `LLViewerRegion` null-deref crash and a real DXPool `join_cv`/`join_mutex` use-after-free race (an atomic decrement outside the mutex let the main thread destroy stack-local sync objects before the last worker's notify ran); fixed a crash in the Extended Profile Info floater
- S24: implemented a real DX11 GPU timer query for avatar profiling
- DX_RENDER: Alpha 1.0 QA pass — fixed a spotlight shadow-priority null-deref crash risk (a sibling code path already had the correct guard, this one didn't), added minimal D3D11 device-loss detection (logs the real `GetDeviceRemovedReason()` and shows an error dialog instead of silently rendering garbage after a driver crash/TDR/eGPU unplug), and removed 2 more confirmed-dead GL-era code blocks

#### 🔒 Privacy & Moderation

#### 🔗 Upstream Merges & Bug Fixes

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
- S24: prepare for / bump version to Pre-Alpha 0.3
- S24: prepare for / bump version to Pre-Alpha 0.4
- S24: bump version, prepare for Alpha 1.0 release

**Binaries signed:** Codesign Serial: `4e2969400a179e151ba7323da181f8b0`
---

## 📬 Footer

Kirstens Viewer is independently developed. For support, build reports, or contributions, please use the designated contact channels.  
Special thanks to SourceForge for hosting this project and supporting the open source community.

> *"I learned very early the difference between knowing the name of something and knowing something."* — Richard P. Feynman
