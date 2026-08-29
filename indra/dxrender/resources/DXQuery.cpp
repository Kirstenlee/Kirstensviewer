#include "DXQuery.h"
#include "DXDevice.h"

ID3D11Query* DXQuery::issue()
{
    D3D11_QUERY_DESC desc = {};
    desc.Query = D3D11_QUERY_EVENT;

    ID3D11Query* query = nullptr;
    HRESULT hr = gDXDevice.getDevice()->CreateQuery(&desc, &query);
    if (FAILED(hr) || !query)
    {
        return nullptr;
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
    if (query)
    {
        query->Release();
    }
}
