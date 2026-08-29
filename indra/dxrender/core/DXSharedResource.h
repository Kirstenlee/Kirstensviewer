#pragma once
#include <d3d11.h>

// S24 (2026-08-26, task #260): generic (NOT DXTexture-specific) cross-device
// texture handoff, built alongside DXWorkerDevice.h (read that file's header
// comment first for the full rationale). Reusable by any future resource
// type built on a DXWorkerDevice - mesh buffers, shadow maps, whatever else
// ends up wanting per-thread device isolation later.
//
// This is a ONE-SHOT "create fully on a worker device, hand off once, never
// touch again from the worker side" pattern only - not a mechanism for a
// continuously-updated shared surface. Every resource passed through here
// must be MipLevels==1 (D3D11_RESOURCE_MISC_SHARED only works on non-
// mipmapped 2D textures per the D3D11 contract) - build any real mip chain
// on the destination device afterward via CopySubresourceRegion +
// GenerateMips (see DXTexture::scaleDown() for the exact shape - this
// codebase already does that sequence for an unrelated reason).
//
// Synchronization: a bare shared handle with no synchronization is NOT safe
// even for a one-shot handoff - ID3D11DeviceContext::Flush() is async and
// does not guarantee the worker device's writes are visible to the main
// device's reads (the two devices maintain independent command streams even
// on the same physical GPU). Every shareable resource here uses
// D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE
// (Microsoft's recommended combination since Windows 8), synchronized via
// IDXGIKeyedMutex::AcquireSync/ReleaseSync with a BOUNDED timeout - never
// INFINITE. This codebase already has a documented bad experience with a
// mutex-based fix causing a hang (task #260 round 4, a plain std::mutex
// serializing CreateTexture2D - different mechanism, same lesson: never
// block a GPU-adjacent wait without a timeout and a fallback).
namespace DXSharedResource
{
    // Sets the flags a worker-device D3D11_TEXTURE2D_DESC needs for this
    // handoff. Caller must ensure desc.MipLevels == 1 before calling -
    // asserted, not silently corrected.
    void makeShareable(D3D11_TEXTURE2D_DESC& desc);

    // Bracket the WORKER-side write to src_texture (an UpdateSubresource
    // call, or the initial pInitialData upload at CreateTexture2D time) -
    // must wrap the actual GPU write. Returns false on timeout/failure -
    // caller must abort (do not write anyway), and should not call
    // releaseAfterWrite() in that case.
    bool acquireForWrite(ID3D11Texture2D* src_texture, DWORD timeout_ms = 2000);
    void releaseAfterWrite(ID3D11Texture2D* src_texture);

    // Opens src_texture (already makeShareable()'d, written, and
    // releaseAfterWrite()'d) on dest_device via CreateSharedHandle +
    // OpenSharedResource1, then AcquireSync's the dest-side keyed mutex
    // before returning - caller must call releaseAfterRead() once its own
    // reads/copies against the returned texture are issued (not necessarily
    // GPU-complete - releaseAfterRead() only needs to happen after the
    // copy/CreateShaderResourceView call is ISSUED on dest_device's
    // context, same as any other same-device D3D11 resource dependency).
    //
    // Returns nullptr on ANY failure (bad HRESULT, or an AcquireSync
    // timeout) - logs once via LL_WARNS_ONCE and latches
    // isSharingDisabled(). Callers must treat nullptr as "this whole
    // create() call failed", matching every other D3D11 failure path
    // already in DXTexture.cpp.
    ID3D11Texture2D* openOnDevice(ID3D11Device* dest_device, ID3D11Texture2D* src_texture, DWORD timeout_ms = 2000);
    void releaseAfterRead(ID3D11Texture2D* dest_texture);

    // Latches true the first time cross-device sharing fails in a way
    // indicating it isn't usable on this system this session - never reset
    // without a restart. DXWorkerDevice::getForCurrentThread() checks this
    // and permanently falls back to gDXDevice-only (today's pre-fix, single-
    // shared-device behavior) for every thread for the rest of the session,
    // rather than repeatedly failing texture creation.
    bool isSharingDisabled();
}
