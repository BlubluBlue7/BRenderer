#include "MeshGPU.h"

bool MeshGPU::UploadToGPU(ID3D11Device* device, ID3D11DeviceContext* context, const Mesh& mesh)
{
    const auto& verts = mesh.GetVertices();
    if (verts.empty()) return false;

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.Usage = D3D11_USAGE_DEFAULT;
    vbDesc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * verts.size());
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = verts.data();

    HRESULT hr = device->CreateBuffer(&vbDesc, &initData, vertexBuffer.GetAddressOf());
    if (FAILED(hr)) return false;

    const auto& idx = mesh.GetIndices();
    if (!idx.empty())
    {
        D3D11_BUFFER_DESC ibDesc = {};
        ibDesc.Usage = D3D11_USAGE_DEFAULT;
        ibDesc.ByteWidth = static_cast<UINT>(sizeof(uint32_t) * idx.size());
        ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

        D3D11_SUBRESOURCE_DATA ibData = {};
        ibData.pSysMem = idx.data();

        hr = device->CreateBuffer(&ibDesc, &ibData, indexBuffer.GetAddressOf());
        if (FAILED(hr)) return false;

        indexCount = static_cast<UINT>(idx.size());
        hasIndices = true;
    }
    else
    {
        hasIndices = false;
    }

    vertexCount = (UINT)mesh.GetVertices().size();
    return true;
}

void MeshGPU::Bind(ID3D11DeviceContext* context)
{
    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &vertexStride, &offset);
    if (hasIndices)
        context->IASetIndexBuffer(indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void MeshGPU::Draw(ID3D11DeviceContext* context)
{
    if (hasIndices)
        context->DrawIndexed(indexCount, 0, 0);
    else
        context->Draw(vertexCount, 0);
}
