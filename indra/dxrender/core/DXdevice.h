#pragma once
#include <d3d11.h>
#include <unordered_map>
#include <string>
#include <cstdint>

// Wraps the ID3D11Device/ID3D11DeviceContext used by the DX_RENDER backend.
// initialize() can adopt an already-created device (llwindowwin32.cpp's
// selectHighPerformanceAdapter() creates one for GPU-selection purposes on
// multi-adapter systems) instead of creating a new one, so the DX_RENDER
// path never ends up with two competing devices on the same adapter.
class DXDevice
{
public:
    // S24 (2026-08-16, task perf investigation): runtime toggle for the
    // D3D11 debug/validation layer (D3D11_CREATE_DEVICE_DEBUG), replacing
    // what had been hardcoded on unconditionally at every device-creation
    // call site since 2026-07-26. Set once from S24DXDebugLayerEnabled via
    // settings_to_globals() (llappviewer.cpp) BEFORE initWindow()/device
    // creation runs - same "push a saved setting into a static the lower
    // layer can't read gSavedSettings for" pattern already used by
    // LLVertexBuffer::sVBOWorkQueueEnabled. Real per-draw-call cost: the
    // debug layer runs full shader-binding/state validation on every single
    // Draw()/DrawIndexed() call, which scales with draw-call count -
    // measured as a large, disproportionate cost on shadow rendering
    // specifically (multiple full-scene redraws per frame: one per sun
    // cascade plus one per active spot light). Defaults true (matches prior
    // always-on behavior) so nothing changes until the user opts out via
    // Preferences > KVTweaks > Rendering Adv. Requires a restart to take
    // effect either direction - the D3D11 device is created once at startup
    // and never recreated.
    static bool sDebugLayerEnabled;

    bool initialize(ID3D11Device* existing_device = nullptr, ID3D11DeviceContext* existing_context = nullptr);
    void shutdown();

    ID3D11Device* getDevice() const { return mDevice; }
    ID3D11DeviceContext* getContext() const { return mContext; }
    D3D_FEATURE_LEVEL getFeatureLevel() const { return mFeatureLevel; }

    // S24 (DX_RENDER diagnostic, 2026-07-28): TEMPORARY - drains whatever
    // D3D11 debug-layer validation messages have accumulated since the last
    // call. The build system isn't set up for attached-debugger runs, so
    // DebugView/the VS Output window (the usual way to see these) aren't
    // available either - but D3D11_CREATE_DEVICE_DEBUG (already set at every
    // device-creation call site) makes the runtime store validation messages
    // in an ID3D11InfoQueue regardless of whether anything is attached to
    // read them live. Polling that queue directly and routing it through our
    // own logging sidesteps the need for a debugger entirely.
    // IMPORTANT (2026-07-28, corrected same day): logs each DISTINCT message
    // ID only ONCE per process (tracked in mSeenMessageIDs), with a running
    // occurrence count, rather than logging every single occurrence. The
    // first version of this function logged every occurrence individually -
    // with linkage errors firing on nearly every Draw() call across the
    // whole engine (not just UI), that meant hundreds of LL_WARNS calls (each
    // with real disk I/O) per frame, which cratered the frame rate badly
    // enough to look like a rendering regression (near-total black screen)
    // rather than a logging problem. Still clears the queue every call so it
    // never grows unbounded internally. Returns the number of NEW distinct
    // message IDs logged this call (0 most of the time, once steady-state).
    //
    // S24 (2026-08-02): CORRECTED AGAIN - the fix above already noted
    // "linkage errors firing... across the whole engine (not just UI)" but
    // still deduped by raw D3D11 message ID alone. D3D11 assigns ONE message
    // ID per VALIDATION RULE, not per shader pair - "id=343, VS/PS linkage
    // incompatible" is the SAME ID regardless of WHICH two shaders triggered
    // it. Since gDeferredGenBrdfLutProgram's one-time startup bake always
    // runs first, its occurrence silently absorbed the "first occurrence"
    // slot for id=343 - meaning if "UI Shader" (or anything else) ALSO hit
    // this exact validation failure, it would never be reported at all, not
    // even once. This was found by a real, patient investigation into a
    // rendering bug (uiF.hlsl outputting UV coordinates as color instead of
    // vertex_color*texture) where every other cause had been exhaustively
    // ruled out - the debug layer likely had the answer the whole time,
    // just silenced by this exact dedup key. Now keyed by (message ID,
    // context) together, so a genuinely different shader hitting the same
    // validation rule still gets its own "first occurrence" report.
    int logPendingDebugMessages(const char* context = nullptr);

    // S24 (2026-08-09, task #133): TEMPORARY - logPendingDebugMessages()'s
    // per-(id,context) dedup (see above) means a real, still-happening
    // warning gets silently swallowed for the rest of the process once it's
    // fired once under a given context - e.g. "UI Shader" already saw a "no
    // Render Target View bound to slot 0" warning once at login, so even if
    // that EXACT condition recurs on every single manipulator-gizmo draw
    // call (same context="UI Shader", same message ID), it would never be
    // logged again. Lets an investigation force a fresh "first occurrence"
    // report right around a specific, currently-broken draw call without
    // permanently disabling the dedup (which exists for a real, documented
    // performance reason - see this class's own history). Remove alongside
    // whatever temporary call site uses it once the hurdle clears.
    void resetDebugMessageDedup() { mSeenMessageIDs.clear(); }

private:
    ID3D11Device* mDevice = nullptr;
    ID3D11DeviceContext* mContext = nullptr;
    D3D_FEATURE_LEVEL mFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    ID3D11InfoQueue* mInfoQueue = nullptr;
    std::unordered_map<std::string, uint64_t> mSeenMessageIDs;
};

extern DXDevice gDXDevice;
