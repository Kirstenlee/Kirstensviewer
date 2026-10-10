#include "DXDevice.h"
#include "llerror.h"
#include <dxgi.h>
#include <vector>
#include <string>
#include <cstdint>
#include <thread>

DXDevice gDXDevice;

// Shared by both initialize() branches: QI's mDevice for IDXGIDevice to find
// which physical adapter it landed on, whether adopted or created here.
static LUID queryAdapterLuid(ID3D11Device* device)
{
    LUID luid = { 0, 0 };
    IDXGIDevice* dxgi_device = nullptr;
    if (SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi_device)) && dxgi_device)
    {
        IDXGIAdapter* adapter = nullptr;
        if (SUCCEEDED(dxgi_device->GetAdapter(&adapter)) && adapter)
        {
            DXGI_ADAPTER_DESC desc;
            if (SUCCEEDED(adapter->GetDesc(&desc)))
            {
                luid = desc.AdapterLuid;
            }
            adapter->Release();
        }
        dxgi_device->Release();
    }
    return luid;
}

// Default true to match the previously-hardcoded always-on behavior.
bool DXDevice::sDebugLayerEnabled = true;

AGSContext* DXDevice::sAgsContext = nullptr;
bool DXDevice::sAdoptedDeviceViaAgs = false;
bool DXDevice::sNvApiAvailable = false;
bool DXDevice::sNvidiaReflexActive = false;
bool DXDevice::sAgsAsyncShaderCompileEnabled = true;
uint32_t DXDevice::sAgsBreadcrumbMarkerCount = 0;
bool DXDevice::sDepthBoundsTestEnabled = true;

AGSDX11ExtensionParams DXDevice::buildAgsExtensionParams()
{
    AGSDX11ExtensionParams params = {};
    params.numBreadcrumbMarkers = sAgsBreadcrumbMarkerCount;
    return params;
}

void DXDevice::applyAgsPostCreateTuning(AGSContext* context)
{
    if (!context || !sAgsAsyncShaderCompileEnabled)
    {
        return;
    }

    // Upper limit, not a demand - "the driver may create fewer threads than
    // allowed by this function" (amd_ags.h). hardware_concurrency() can
    // return 0 if it can't determine the count; fall back to a small sane
    // default rather than passing 0, which disables async compilation
    // entirely per the same header.
    unsigned int thread_count = std::thread::hardware_concurrency();
    if (thread_count == 0)
    {
        thread_count = 4;
    }

    AGSReturnCode ags_hr = agsDriverExtensionsDX11_SetMaxAsyncCompileThreadCount(context, thread_count);
    if (ags_hr != AGS_SUCCESS)
    {
        LL_WARNS("DXRender") << "agsDriverExtensionsDX11_SetMaxAsyncCompileThreadCount failed, code=" << (int)ags_hr << LL_ENDL;
    }

    ags_hr = agsDriverExtensionsDX11_SetDiskShaderCacheEnabled(context, 1);
    if (ags_hr != AGS_SUCCESS)
    {
        // Expected failure case per the header: disabled explicitly via
        // Radeon Settings or an app profile - not worth warning about.
        LL_INFOS("DXRender") << "agsDriverExtensionsDX11_SetDiskShaderCacheEnabled declined, code=" << (int)ags_hr << LL_ENDL;
    }
}

bool DXDevice::initialize(ID3D11Device* existing_device, ID3D11DeviceContext* existing_context)
{
    if (mDevice)
    {
        // already initialized
        return true;
    }

    if (existing_device && existing_context)
    {
        mDevice = existing_device;
        mContext = existing_context;
        mDevice->AddRef();
        mContext->AddRef();
        mFeatureLevel = mDevice->GetFeatureLevel();
        mDevice->QueryInterface(__uuidof(ID3D11InfoQueue), (void**)&mInfoQueue);
        mAdapterLuid = queryAdapterLuid(mDevice);
        mCreatedViaAgs = sAdoptedDeviceViaAgs;
        return true;
    }

    // No device to adopt (e.g. a single-adapter system, where
    // selectHighPerformanceAdapter() skips device creation entirely) -
    // create our own.
    D3D_FEATURE_LEVEL requested_levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    // D3D11_CREATE_DEVICE_DEBUG (gated by sDebugLayerEnabled) requires the
    // Windows "Graphics Tools" optional feature - device creation fails
    // outright if it's missing while the flag is on.
    //
    // D3D11_CREATE_DEVICE_SINGLETHREADED: this codebase never calls into the
    // D3D11 device/context from any thread but the main one, so the driver
    // can skip its internal per-call thread-safety locking entirely. Must
    // match at every D3D11CreateDevice call site (see llwindowwin32.cpp's
    // selectHighPerformanceAdapter()) since only one site actually creates
    // the device this class adopts or owns.
    UINT flags = (sDebugLayerEnabled ? D3D11_CREATE_DEVICE_DEBUG : 0) | D3D11_CREATE_DEVICE_SINGLETHREADED;
    HRESULT hr = E_FAIL;

    // Prefer the AGS-wrapped creation path when AGS is available (AMD GPU) -
    // gives this device access to AMD's DX11 driver extensions (shader
    // intrinsics, UAV overlap, multi-draw indirect, etc.). Falls back to
    // plain D3D11CreateDevice below on any AGS failure or non-AMD GPU.
    if (sAgsContext)
    {
        AGSDX11DeviceCreationParams creation_params = {};
        creation_params.pAdapter = nullptr;
        creation_params.DriverType = D3D_DRIVER_TYPE_HARDWARE;
        creation_params.Flags = flags;
        creation_params.pFeatureLevels = requested_levels;
        creation_params.FeatureLevels = _countof(requested_levels);
        creation_params.SDKVersion = D3D11_SDK_VERSION;

        AGSDX11ExtensionParams extension_params = buildAgsExtensionParams();
        AGSDX11ReturnedParams returned_params = {};
        AGSReturnCode ags_hr = agsDriverExtensionsDX11_CreateDevice(sAgsContext, &creation_params, &extension_params, &returned_params);
        if (ags_hr == AGS_SUCCESS)
        {
            mDevice = returned_params.pDevice;
            mContext = returned_params.pImmediateContext;
            mFeatureLevel = returned_params.featureLevel;
            mCreatedViaAgs = true;
            hr = S_OK;
            applyAgsPostCreateTuning(sAgsContext);
        }
        else
        {
            LL_WARNS("DXRender") << "agsDriverExtensionsDX11_CreateDevice failed, code=" << (int)ags_hr
                << " - falling back to plain D3D11CreateDevice" << LL_ENDL;
        }
    }

    if (FAILED(hr))
    {
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            requested_levels,
            _countof(requested_levels),
            D3D11_SDK_VERSION,
            &mDevice,
            &mFeatureLevel,
            &mContext);
        mCreatedViaAgs = false;
    }

    if (FAILED(hr))
    {
        LL_WARNS("DXRender") << "D3D11CreateDevice failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        mDevice = nullptr;
        mContext = nullptr;
        return false;
    }

    mDevice->QueryInterface(__uuidof(ID3D11InfoQueue), (void**)&mInfoQueue);
    mAdapterLuid = queryAdapterLuid(mDevice);

    return true;
}

void DXDevice::shutdown()
{
    // Permanent, settings-gated tooling, not a leftover diagnostic.
    // logPendingDebugMessages() only logs each distinct message ID once (to
    // avoid per-frame log I/O); this final tally reports the true per-ID
    // occurrence count for the whole session at shutdown instead.
    for (const auto& entry : mSeenMessageIDs)
    {
        // entry.first is now "messageID|context" (see logPendingDebugMessages()'s
        // header comment for why context was added to the dedup key).
        LL_WARNS("DXDebugLayer") << "final tally: message id|context=" << entry.first
            << " occurred " << entry.second << " times this session" << LL_ENDL;
    }

    if (mInfoQueue) { mInfoQueue->Release(); mInfoQueue = nullptr; }

    if (mCreatedViaAgs && sAgsContext && mDevice)
    {
        // Also cleans up the AMD-specific driver extensions allocated by
        // agsDriverExtensionsDX11_CreateDevice - a plain Release() pair
        // alone would leak those.
        unsigned int device_refs = 0, context_refs = 0;
        agsDriverExtensionsDX11_DestroyDevice(sAgsContext, mDevice, &device_refs, mContext, &context_refs);
        mContext = nullptr;
        mDevice = nullptr;
    }

    if (mContext) { mContext->Release(); mContext = nullptr; }
    if (mDevice) { mDevice->Release(); mDevice = nullptr; }
}

int DXDevice::logPendingDebugMessages(const char* context)
{
    if (!mInfoQueue)
    {
        return 0;
    }

    UINT64 num_messages = mInfoQueue->GetNumStoredMessages();
    int newly_logged = 0;
    for (UINT64 i = 0; i < num_messages; ++i)
    {
        SIZE_T msg_len = 0;
        if (FAILED(mInfoQueue->GetMessage(i, nullptr, &msg_len)) || msg_len == 0)
        {
            continue;
        }

        std::vector<uint8_t> buffer(msg_len);
        D3D11_MESSAGE* msg = (D3D11_MESSAGE*)buffer.data();
        if (SUCCEEDED(mInfoQueue->GetMessage(i, msg, &msg_len)))
        {
            // Keyed by (message ID, context) together, not ID alone, so a
            // different shader hitting the same validation rule under a
            // different context still gets reported.
            const std::string key = std::to_string((int)msg->ID) + "|" + (context ? context : "");
            uint64_t& seen_count = mSeenMessageIDs[key];
            ++seen_count;
            // Only the FIRST time this distinct message ID is ever seen this
            // process does it actually hit LL_WARNS (real disk I/O) - every
            // repeat just increments the counter above. See this method's
            // header comment for why logging every single occurrence was a
            // real, self-inflicted performance problem.
            if (seen_count == 1)
            {
                LL_WARNS("DXDebugLayer") << "[D3D11 sev=" << (int)msg->Severity
                    << " id=" << (int)msg->ID << "] (first occurrence, will not repeat per-instance) "
                    << (context ? (std::string("context='") + context + "' ") : std::string())
                    << std::string(msg->pDescription, msg->DescriptionByteLength > 0 ? msg->DescriptionByteLength - 1 : 0)
                    << LL_ENDL;
                ++newly_logged;
            }
        }
    }

    mInfoQueue->ClearStoredMessages();
    return newly_logged;
}
