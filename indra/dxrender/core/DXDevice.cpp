#include "DXDevice.h"
#include "llerror.h"
#include <vector>
#include <string>
#include <cstdint>

DXDevice gDXDevice;

// S24 (2026-08-16): see DXDevice.h's header comment. Default true to match
// the pre-existing always-on behavior for anyone who hasn't touched the new
// setting yet.
bool DXDevice::sDebugLayerEnabled = true;

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
        return true;
    }

    // No device to adopt (e.g. a single-adapter system, where
    // selectHighPerformanceAdapter() skips device creation entirely) -
    // create our own.
    D3D_FEATURE_LEVEL requested_levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    // S24 (2026-08-16): now gated by S24DXDebugLayerEnabled (see
    // sDebugLayerEnabled's header comment) instead of being hardcoded on -
    // enables the runtime validation layer so state/binding hazards surface
    // as real debug-output messages instead of silent wrong pixels, at a
    // real per-draw-call performance cost. Requires the Windows "Graphics
    // Tools" optional feature installed - if missing, this call fails
    // outright with the flag on (turn the setting off if that happens and
    // this isn't the path being investigated).
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        sDebugLayerEnabled ? D3D11_CREATE_DEVICE_DEBUG : 0,
        requested_levels,
        _countof(requested_levels),
        D3D11_SDK_VERSION,
        &mDevice,
        &mFeatureLevel,
        &mContext);

    if (FAILED(hr))
    {
        LL_WARNS("DXRender") << "D3D11CreateDevice failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
        mDevice = nullptr;
        mContext = nullptr;
        return false;
    }

    mDevice->QueryInterface(__uuidof(ID3D11InfoQueue), (void**)&mInfoQueue);

    return true;
}

void DXDevice::shutdown()
{
    // S24 (DX_RENDER diagnostic, 2026-07-28): TEMPORARY - logPendingDebugMessages()
    // only logs each distinct D3D11 message ID once (see its own comment) to
    // avoid the per-frame I/O cost of logging every occurrence - this final
    // tally is the cheap way to still see how often each one actually fired
    // over the whole session, without paying for it at runtime.
    for (const auto& entry : mSeenMessageIDs)
    {
        // entry.first is now "messageID|context" (see logPendingDebugMessages()'s
        // header comment for why context was added to the dedup key).
        LL_WARNS("DXDebugLayer") << "final tally: message id|context=" << entry.first
            << " occurred " << entry.second << " times this session" << LL_ENDL;
    }

    if (mInfoQueue) { mInfoQueue->Release(); mInfoQueue = nullptr; }
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
            // S24 (2026-08-02): keyed by (message ID, context) together, not
            // message ID alone - see this method's header comment. Without
            // this, a genuinely different shader hitting the exact same
            // D3D11 validation rule (same numeric ID) as an earlier-seen
            // one would never be reported at all.
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
