#include "DXOcclusionQuery.h"
#include "DXDevice.h"
#include "llerror.h"

#include <unordered_map>

namespace
{
    std::unordered_map<unsigned int, ID3D11Query*> sQueries;
    // 0 reserved as "invalid" - matches GL's glGenQueries() convention that
    // LLOcclusionCullingGroup's own "if (!mOcclusionQuery[...])" checks rely on.
    unsigned int sNextName = 1;
}

// static
void DXOcclusionQuery::genQueries(int count, unsigned int* out_names)
{
    D3D11_QUERY_DESC desc = {};
    desc.Query = D3D11_QUERY_OCCLUSION;

    for (int i = 0; i < count; ++i)
    {
        ID3D11Query* query = nullptr;
        HRESULT hr = gDXDevice.getDevice()->CreateQuery(&desc, &query);
        if (FAILED(hr) || !query)
        {
            LL_WARNS("Occlusion") << "DXOcclusionQuery::genQueries: CreateQuery failed, hr=0x" << std::hex << (unsigned long)hr << std::dec << LL_ENDL;
            out_names[i] = 0;
            continue;
        }

        unsigned int name = sNextName++;
        sQueries[name] = query;
        out_names[i] = name;
    }
}

// static
void DXOcclusionQuery::deleteQueries(int count, const unsigned int* names)
{
    for (int i = 0; i < count; ++i)
    {
        auto iter = sQueries.find(names[i]);
        if (iter != sQueries.end())
        {
            iter->second->Release();
            sQueries.erase(iter);
        }
    }
}

// static
void DXOcclusionQuery::beginQuery(unsigned int name)
{
    auto iter = sQueries.find(name);
    if (iter != sQueries.end())
    {
        gDXDevice.getContext()->Begin(iter->second);
    }
}

// static
void DXOcclusionQuery::endQuery(unsigned int name)
{
    auto iter = sQueries.find(name);
    if (iter != sQueries.end())
    {
        gDXDevice.getContext()->End(iter->second);
    }
}

// static
bool DXOcclusionQuery::isResultAvailable(unsigned int name)
{
    auto iter = sQueries.find(name);
    if (iter == sQueries.end())
    {
        return false;
    }

    HRESULT hr = gDXDevice.getContext()->GetData(iter->second, nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);
    return hr == S_OK;
}

// static
unsigned long long DXOcclusionQuery::getResult(unsigned int name)
{
    auto iter = sQueries.find(name);
    if (iter == sQueries.end())
    {
        return 0;
    }

    // S24 (2026-08-22, task #156 follow-up): bounded retry - this used to
    // be `while (hr != S_OK) { hr = GetData(...); }` with no escape at all.
    // GL's own glGetQueryObjectuiv(..., GL_QUERY_RESULT, ...) is likewise a
    // blocking call by spec, but only ever blocks on a query that was
    // genuinely Begin()/End()'d - a real, confirmed root cause candidate for
    // "world loading hangs at ~11-12s for an eternity" (user-reported, tied
    // to SSR/mirror startup): if any query's Begin() is ever skipped (a real
    // risk during the chaotic startup burst where hundreds of spatial-group
    // AND reflection/hero-probe queries all fire for the first time at
    // once), GetData() never returns S_OK and this spun the main thread
    // forever, with no way out. 100000 iterations is a generous margin
    // (GetData() is a cheap poll, not a blocking driver call by itself) -
    // treating "still not ready after that many tries" as 0 samples passed
    // (conservative: reads as occluded) rather than hanging is a strictly
    // safer failure mode.
    UINT64 result = 0;
    HRESULT hr = S_FALSE;
    for (int attempt = 0; attempt < 100000 && hr != S_OK; ++attempt)
    {
        hr = gDXDevice.getContext()->GetData(iter->second, &result, sizeof(result), 0);
    }
    if (hr != S_OK)
    {
        LL_WARNS("Occlusion") << "DXOcclusionQuery::getResult: GetData never returned S_OK after 100000 attempts (name=" << name << ", hr=0x" << std::hex << (unsigned long)hr << std::dec << ") - returning 0 (occluded) instead of hanging" << LL_ENDL;
        return 0;
    }
    return result;
}
