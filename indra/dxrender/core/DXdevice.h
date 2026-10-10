#pragma once
#include <d3d11.h>
#include <unordered_map>
#include <string>
#include <cstdint>
#include "amdags/amd_ags.h"

// Wraps the ID3D11Device/ID3D11DeviceContext used by the DX_RENDER backend.
// initialize() can adopt an already-created device (llwindowwin32.cpp's
// selectHighPerformanceAdapter() creates one for GPU-selection purposes on
// multi-adapter systems) instead of creating a new one, so the DX_RENDER
// path never ends up with two competing devices on the same adapter.
class DXDevice
{
public:
    // Runtime toggle for the D3D11 debug/validation layer
    // (D3D11_CREATE_DEVICE_DEBUG). Set once from S24DXDebugLayerEnabled via
    // settings_to_globals() (llappviewer.cpp) before initWindow()/device
    // creation runs, since the lower layer can't read gSavedSettings
    // directly. The debug layer validates every Draw()/DrawIndexed() call,
    // a disproportionate cost on shadow rendering (multiple full-scene
    // redraws per frame). Requires a restart either direction - the D3D11
    // device is created once at startup and never recreated.
    static bool sDebugLayerEnabled;

    // AGS context, created once in WinMain (llappviewerwin32.cpp) before any
    // device creation runs - null on non-AMD GPUs or if AGS init failed, in
    // which case every call site below falls back to plain D3D11CreateDevice.
    static AGSContext* sAgsContext;

    // Set by llwindowwin32.cpp's selectHighPerformanceAdapter() when IT is
    // the one that actually creates gD3D11Device/gD3D11Context (multi-adapter
    // systems) via the AGS wrapper, so initialize()'s adopt path below knows
    // whether shutdown() must tear the device down through AGS too.
    static bool sAdoptedDeviceViaAgs;

    // True once NvAPI_Initialize() has succeeded in WinMain (llappviewerwin32.cpp)
    // - an NVIDIA GPU is present and NVAPI is usable. False (the common case
    // on AMD/Intel) is not an error.
    static bool sNvApiAvailable;

    // True once NvAPI_D3D_SetSleepMode() has successfully enabled Reflex -
    // gates the per-frame NvAPI_D3D_Sleep() call in WinMain's main loop.
    static bool sNvidiaReflexActive;

    // Settings pushed down from settings_to_globals() (llappviewer.cpp),
    // same pattern as sDebugLayerEnabled above - dxrender/core can't read
    // gSavedSettings directly. Read by the AGS device-creation call sites
    // (DXDevice.cpp here, and llwindowwin32.cpp's selectHighPerformanceAdapter())
    // to fill in AGSDX11ExtensionParams.
    static bool sAgsAsyncShaderCompileEnabled;    // RenderAgsAsyncShaderCompile
    static uint32_t sAgsBreadcrumbMarkerCount;    // RenderAgsBreadcrumbMarkers - 0 = disabled

    // Read by DXPipeline::renderDeferredLighting() (newview/dxpipeline.cpp,
    // which CAN read gSavedSettings directly - this one doesn't need the
    // push-down treatment, but lives here alongside the other AGS/NVAPI
    // feature flags for discoverability).
    static bool sDepthBoundsTestEnabled;          // RenderDepthBoundsTest

    bool initialize(ID3D11Device* existing_device = nullptr, ID3D11DeviceContext* existing_context = nullptr);
    void shutdown();

    // Shared by every agsDriverExtensionsDX11_CreateDevice call site (here,
    // and llwindowwin32.cpp's selectHighPerformanceAdapter()) so the
    // breadcrumb-marker count only has one place it's filled in.
    static AGSDX11ExtensionParams buildAgsExtensionParams();

    // Shared post-creation AGS tuning, called once right after a successful
    // agsDriverExtensionsDX11_CreateDevice() - must run before any shader is
    // created (both calls document this), i.e. immediately after creation,
    // not later. No-op if sAgsAsyncShaderCompileEnabled is false.
    static void applyAgsPostCreateTuning(AGSContext* context);

    // True if THIS device was actually created via the AGS wrapper (not just
    // "is AGS available" - see sAgsContext's comment above). Hybrid AMD+NVIDIA
    // systems can have both sAgsContext and sNvApiAvailable set while the
    // device actually ends up on whichever adapter selectHighPerformanceAdapter()
    // picked (by VRAM, not vendor) - callers that dispatch per-vendor at
    // runtime (e.g. depth bounds test) must check this, not sAgsContext
    // directly, or they can end up calling an AMD-only driver extension
    // against an NVIDIA-backed device (harmless - AGS just fails cleanly -
    // but silently inert instead of correctly falling through to NVAPI).
    bool wasCreatedViaAgs() const { return mCreatedViaAgs; }

    ID3D11Device* getDevice() const { return mDevice; }
    ID3D11DeviceContext* getContext() const { return mContext; }
    D3D_FEATURE_LEVEL getFeatureLevel() const { return mFeatureLevel; }

    // The physical adapter this device is on, captured once in initialize()
    // regardless of whether the device was adopted or created here.
    // DXWorkerDevice uses this to target the same adapter for a per-thread
    // device, so resources can be shared cross-device via DXSharedResource.
    LUID getAdapterLuid() const { return mAdapterLuid; }

    // Drains D3D11 debug-layer validation messages accumulated since the
    // last call (there's no attached-debugger tooling in this build to view
    // them live via DebugView/VS Output, so they're polled from the
    // ID3D11InfoQueue and routed through our own logging instead). Logs each
    // distinct (message ID, context) pair only once per process, with a
    // running occurrence count in mSeenMessageIDs, rather than every single
    // occurrence - some validation failures fire on nearly every Draw()
    // call, and logging each one individually costs enough real disk I/O to
    // crater the frame rate. Context is included in the dedup key because
    // D3D11 assigns one message ID per validation rule, not per shader pair
    // - without it, an early one-time occurrence (e.g. a startup shader
    // bake) permanently swallows the "first occurrence" slot for that ID,
    // silencing genuinely different later callers hitting the same rule.
    // Clears the queue every call so it never grows unbounded. Returns the
    // number of new distinct message IDs logged this call.
    int logPendingDebugMessages(const char* context = nullptr);

    // Forces a fresh "first occurrence" report from logPendingDebugMessages()
    // for a given context, bypassing its per-(id,context) dedup - useful
    // when investigating a warning that would otherwise already be
    // silenced for the rest of the process.
    void resetDebugMessageDedup() { mSeenMessageIDs.clear(); }

private:
    ID3D11Device* mDevice = nullptr;
    ID3D11DeviceContext* mContext = nullptr;
    D3D_FEATURE_LEVEL mFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    ID3D11InfoQueue* mInfoQueue = nullptr;
    LUID mAdapterLuid = { 0, 0 };
    std::unordered_map<std::string, uint64_t> mSeenMessageIDs;

    // True if mDevice/mContext were created via agsDriverExtensionsDX11_CreateDevice
    // (directly here, or adopted from selectHighPerformanceAdapter()) - shutdown()
    // must tear them down via agsDriverExtensionsDX11_DestroyDevice instead of a
    // plain Release() pair in that case.
    bool mCreatedViaAgs = false;
};

extern DXDevice gDXDevice;
