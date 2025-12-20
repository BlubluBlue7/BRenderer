#pragma once
#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include "Mesh.h"

class MeshGPU
{
public:
    MeshGPU() = default;

    // 上传 Mesh 数据到 GPU
    bool UploadToGPU(ID3D11Device* device, ID3D11DeviceContext* context, const Mesh& mesh);

    // 绑定并绘制
    void Bind(ID3D11DeviceContext* context);
    void Draw(ID3D11DeviceContext* context);

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer;
    UINT vertexStride = sizeof(Vertex);
    UINT indexCount = 0;
    UINT vertexCount = 0;
    bool hasIndices = false;
};
