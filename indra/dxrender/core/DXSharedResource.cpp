#include "DXSharedResource.h"
#include "llerror.h"
#include <dxgi1_2.h>
#include <d3d11_1.h>
#include <atomic>

namespace
{
    std::atomic<bool> sSharingDisabled{ false };

    void disableSharingPermanently(const char* reason)
    {
        if (!sSharingDisabled.exchange(true))
        {
            LL_WARNS("DXRender") << "DXSharedResource: cross-device sharing disabled for the "
                "rest of this session (" << reason << ") - worker threads fall back to "
                "gDXDevice, matching pre-task-260-fix behavior" << LL_ENDL;
        }
    }
}

namespace DXSharedResource
{

void makeShareable(D3D11_TEXTURE2D_DESC& desc)
{
    llassert(desc.MipLevels == 1);
    desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
}

bool acquireForWrite(ID3D11Texture2D* src_texture, DWORD timeout_ms)
{
    IDXGIKeyedMutex* mutex = nullptr;
    if (FAILED(src_texture->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&mutex)) || !mutex)
    {
        LL_WARNS_ONCE("DXRender") << "DXSharedResource::acquireForWrite: QueryInterface(IDXGIKeyedMutex) failed" << LL_ENDL;
        disableSharingPermanently("acquireForWrite QI failed");
        return false;
    }

    // Key 0: the fresh, just-created state of a keyed-mutex resource is
    // always immediately acquirable at key 0 by any device - this is the
    // producer's first (and only, in this one-shot design) acquire.
    HRESULT hr = mutex->AcquireSync(0, timeout_ms);
    mutex->Release();

    if (hr != S_OK)
    {
        LL_WARNS_ONCE("DXRender") << "DXSharedResource::acquireForWrite: AcquireSync(0) failed/timed out, hr=0x"
            << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        disableSharingPermanently("acquireForWrite timeout/failure");
        return false;
    }

    return true;
}

void releaseAfterWrite(ID3D11Texture2D* src_texture)
{
    IDXGIKeyedMutex* mutex = nullptr;
    if (SUCCEEDED(src_texture->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&mutex)) && mutex)
    {
        // Hand the key to 1 - the consumer side (openOnDevice() below)
        // AcquireSync(1, ...)'s to pick it up.
        mutex->ReleaseSync(1);
        mutex->Release();
    }
}

ID3D11Texture2D* openOnDevice(ID3D11Device* dest_device, ID3D11Texture2D* src_texture, DWORD timeout_ms)
{
    IDXGIResource1* src_resource = nullptr;
    if (FAILED(src_texture->QueryInterface(__uuidof(IDXGIResource1), (void**)&src_resource)) || !src_resource)
    {
        LL_WARNS_ONCE("DXRender") << "DXSharedResource::openOnDevice: QueryInterface(IDXGIResource1) failed" << LL_ENDL;
        disableSharingPermanently("openOnDevice QI(IDXGIResource1) failed");
        return nullptr;
    }

    HANDLE shared_handle = nullptr;
    HRESULT hr = src_resource->CreateSharedHandle(
        nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &shared_handle);
    src_resource->Release();

    if (FAILED(hr) || !shared_handle)
    {
        LL_WARNS_ONCE("DXRender") << "DXSharedResource::openOnDevice: CreateSharedHandle failed, hr=0x"
            << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        disableSharingPermanently("CreateSharedHandle failed");
        return nullptr;
    }

    ID3D11Device1* dest_device1 = nullptr;
    if (FAILED(dest_device->QueryInterface(__uuidof(ID3D11Device1), (void**)&dest_device1)) || !dest_device1)
    {
        LL_WARNS_ONCE("DXRender") << "DXSharedResource::openOnDevice: QueryInterface(ID3D11Device1) failed" << LL_ENDL;
        CloseHandle(shared_handle);
        disableSharingPermanently("openOnDevice QI(ID3D11Device1) failed");
        return nullptr;
    }

    ID3D11Texture2D* opened = nullptr;
    hr = dest_device1->OpenSharedResource1(shared_handle, __uuidof(ID3D11Texture2D), (void**)&opened);
    dest_device1->Release();
    // S24: NT handles (D3D11_RESOURCE_MISC_SHARED_NTHANDLE) are real Win32
    // handles owned by the caller - unlike the legacy GetSharedHandle() KMT
    // pseudo-handle, this one must be closed here; OpenSharedResource1
    // duplicates whatever reference it needs internally.
    CloseHandle(shared_handle);

    if (FAILED(hr) || !opened)
    {
        LL_WARNS_ONCE("DXRender") << "DXSharedResource::openOnDevice: OpenSharedResource1 failed, hr=0x"
            << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        disableSharingPermanently("OpenSharedResource1 failed");
        return nullptr;
    }

    IDXGIKeyedMutex* mutex = nullptr;
    if (FAILED(opened->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&mutex)) || !mutex)
    {
        LL_WARNS_ONCE("DXRender") << "DXSharedResource::openOnDevice: QueryInterface(IDXGIKeyedMutex) on opened texture failed" << LL_ENDL;
        opened->Release();
        disableSharingPermanently("openOnDevice opened-side QI(IDXGIKeyedMutex) failed");
        return nullptr;
    }

    hr = mutex->AcquireSync(1, timeout_ms);
    mutex->Release();

    if (hr != S_OK)
    {
        LL_WARNS_ONCE("DXRender") << "DXSharedResource::openOnDevice: AcquireSync(1) failed/timed out, hr=0x"
            << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        opened->Release();
        disableSharingPermanently("openOnDevice AcquireSync(1) timeout/failure");
        return nullptr;
    }

    return opened;
}

void releaseAfterRead(ID3D11Texture2D* dest_texture)
{
    IDXGIKeyedMutex* mutex = nullptr;
    if (SUCCEEDED(dest_texture->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&mutex)) && mutex)
    {
        mutex->ReleaseSync(0);
        mutex->Release();
    }
}

bool isSharingDisabled()
{
    return sSharingDisabled.load();
}

} // namespace DXSharedResource
