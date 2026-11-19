#include "DXQuery.h"
#include "DXDevice.h"
#include <vector>

namespace
{
    // S24 : retired event queries are kept for reuse instead of being destroyed and created again every frame
    // (an event query can be End()ed again once it is retired). Main thread only, like every caller.
    std::vector<ID3D11Query*> sFreeQueries;
    constexpr size_t MAX_POOLED_QUERIES = 16;
}

ID3D11Query* DXQuery::issue()
{
    ID3D11Query* query = nullptr;
    if (!sFreeQueries.empty())
    {
        query = sFreeQueries.back();
        sFreeQueries.pop_back();
    }
    else
    {
        D3D11_QUERY_DESC desc = {};
        desc.Query = D3D11_QUERY_EVENT;

        HRESULT hr = gDXDevice.getDevice()->CreateQuery(&desc, &query);
        if (FAILED(hr) || !query)
        {
            return nullptr;
        }
    }

    gDXDevice.getContext()->End(query);
    return query;
}

bool DXQuery::isComplete(ID3D11Query* query)
{
    if (!query)
    {
        return false;
    }

    BOOL data = FALSE;
    // 0 flags = non-blocking: returns S_FALSE (not S_OK) while the GPU
    // hasn't reached this query yet, rather than stalling the CPU.
    HRESULT hr = gDXDevice.getContext()->GetData(query, &data, sizeof(data), 0);
    return (hr == S_OK) && (data != FALSE);
}

void DXQuery::release(ID3D11Query* query)
{
    if (!query)
    {
        return;
    }
    if (sFreeQueries.size() < MAX_POOLED_QUERIES && gDXDevice.getDevice())
    {
        sFreeQueries.push_back(query);
    }
    else
    {
        query->Release();
    }
}

void DXQuery::clearPool()
{
    for (ID3D11Query* query : sFreeQueries)
    {
        query->Release();
    }
    sFreeQueries.clear();
}
