#pragma once
#include <d3d11.h>

// S24 (2026-08-26, task #260, KEPT as reusable scaffold though task #260
// itself closed not-applicable): general-purpose "one D3D11 device+context
// per thread" mechanism - NOT texture-specific, no current caller. Built
// while investigating a confirmed NVIDIA driver regression (610.88,
// previously fixed at 610.52) in concurrent D3D11 texture creation from
// multiple threads against one shared device - the underlying feature
// (DXImageThread, background texture/media creation) was removed entirely
// after this and two other mitigations all reached the same driver crash via
// different call paths (see memorygraph tags=["task260"]), but this class's
// actual mechanism - each thread gets its OWN device on the same physical
// adapter (via gDXDevice.getAdapterLuid()), the direct D3D11 analogue of how
// this codebase's old OpenGL backend gave each worker thread a real separate
// wglCreateContextAttribsARB context - is sound, reusable infrastructure in
// its own right: a future detached-UI-window thread, or an avatar-mesh/
// shadow/sky worker thread, could still opt in with a single call to
// getForCurrentThread(). See DXSharedResource.h for how a resource created
// on a worker device gets handed back to gDXDevice's main device (deferred
// contexts are a live alternative worth evaluating for a future consumer,
// per user direction when this was parked - see task #260's memory).
class DXWorkerDevice
{
public:
    // Lazily creates (once) and caches this thread's worker device via a
    // thread_local - works for ANY thread, LL::ThreadPool-based or a bare
    // std::thread, with no per-call-site setup/teardown needed; cleans up
    // automatically (device/context Release()d) at thread exit via ordinary
    // thread_local destruction.
    //
    // Returns nullptr - meaning "fall back to gDXDevice, exactly today's
    // single-device behavior" - in three cases:
    //   - called from the main thread (on_main_thread(), llthread.h)
    //   - device creation failed on this thread (cached; never retried for
    //     the rest of this thread's lifetime)
    //   - DXSharedResource::isSharingDisabled() has latched true (cross-
    //     device sharing itself confirmed unusable on this system this
    //     session - see DXSharedResource.h)
    static DXWorkerDevice* getForCurrentThread();

    ID3D11Device* getDevice() const { return mDevice; }
    ID3D11DeviceContext* getContext() const { return mContext; }

    ~DXWorkerDevice();

private:
    bool initialize();

    ID3D11Device* mDevice = nullptr;
    ID3D11DeviceContext* mContext = nullptr;
};
