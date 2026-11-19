#include "DXOcclusionQuery.h"
#include "DXDevice.h"
#include "llerror.h"

#include <queue>
#include <vector>

namespace
{
    // S24 : names are dense sequential integers, so the query for a name is sQueries[name] (null when
    // deleted, and slot 0 stays null as the invalid name) instead of a hash lookup per Begin/End/poll
    std::vector<ID3D11Query*> sQueries(1, nullptr);

    inline ID3D11Query* findQuery(unsigned int name)
    {
        return name < sQueries.size() ? sQueries[name] : nullptr;
    }
    // 0 reserved as "invalid" - matches GL's glGenQueries() convention that
    // LLOcclusionCullingGroup's own "if (!mOcclusionQuery[...])" checks rely on.
    unsigned int sNextName = 1;

    std::queue<unsigned int> sFreePool;
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
        sQueries.push_back(query);
        out_names[i] = name;
    }
}

// static
void DXOcclusionQuery::deleteQueries(int count, const unsigned int* names)
{
    for (int i = 0; i < count; ++i)
    {
        if (ID3D11Query* query = findQuery(names[i]))
        {
            query->Release();
            sQueries[names[i]] = nullptr;
        }
    }
}

// static
unsigned int DXOcclusionQuery::acquire()
{
    if (sFreePool.empty())
    {
        unsigned int queries[1024];
        genQueries(1024, queries);
        for (int i = 0; i < 1024; ++i)
        {
            sFreePool.push(queries[i]);
        }
    }

    unsigned int ret = sFreePool.front();
    sFreePool.pop();
    return ret;
}

// static
void DXOcclusionQuery::release(unsigned int name)
{
    if (name != 0)
    {
        sFreePool.push(name);
    }
}

// static
void DXOcclusionQuery::beginQuery(unsigned int name)
{
    if (ID3D11Query* query = findQuery(name))
    {
        gDXDevice.getContext()->Begin(query);
    }
}

// static
void DXOcclusionQuery::endQuery(unsigned int name)
{
    if (ID3D11Query* query = findQuery(name))
    {
        gDXDevice.getContext()->End(query);
    }
}

// static
bool DXOcclusionQuery::isResultAvailable(unsigned int name)
{
    ID3D11Query* query = findQuery(name);
    if (!query)
    {
        return false;
    }

    HRESULT hr = gDXDevice.getContext()->GetData(query, nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);
    return hr == S_OK;
}

// static
bool DXOcclusionQuery::tryGetResult(unsigned int name, unsigned long long& out_samples)
{
    ID3D11Query* query = findQuery(name);
    if (!query)
    {
        return false;
    }

    // One non-blocking GetData() that both tests for completion and fetches the value
    UINT64 result = 0;
    if (gDXDevice.getContext()->GetData(query, &result, sizeof(result), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
    {
        return false;
    }
    out_samples = result;
    return true;
}

// static
unsigned long long DXOcclusionQuery::getResult(unsigned int name)
{
    ID3D11Query* query = findQuery(name);
    if (!query)
    {
        return 0;
    }

    // Bounded retry, not an infinite `while (hr != S_OK)` spin: if a query's
    // Begin() is ever skipped (possible during a startup burst of many
    // spatial-group/probe queries firing at once), GetData() never returns
    // S_OK and an unbounded loop hangs the main thread forever. 100000
    // iterations is a generous margin (GetData() is a cheap poll); treating
    // "still not ready" past that as 0 samples passed (reads as occluded)
    // is a strictly safer failure mode than hanging.
    UINT64 result = 0;
    HRESULT hr = S_FALSE;
    for (int attempt = 0; attempt < 100000 && hr != S_OK; ++attempt)
    {
        hr = gDXDevice.getContext()->GetData(query, &result, sizeof(result), 0);
    }
    if (hr != S_OK)
    {
        LL_WARNS("Occlusion") << "DXOcclusionQuery::getResult: GetData never returned S_OK after 100000 attempts (name=" << name << ", hr=0x" << std::hex << (unsigned long)hr << std::dec << ") - returning 0 (occluded) instead of hanging" << LL_ENDL;
        return 0;
    }
    return result;
}
