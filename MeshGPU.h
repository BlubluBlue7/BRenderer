#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include "Mesh.h"
#include <vector>

class MeshGPU
{
public:
    MeshGPU() = default;

    // 上传 Mesh 数据到 GPU
    bool UploadToGPU(ID3D11Device* device, ID3D11DeviceContext* context, const Mesh& mesh);

    // 绑定并绘制
    void Bind(ID3D11DeviceContext* context);
    void Draw(ID3D11DeviceContext* context);
    
    // 按子网格绘制（用于多材质支持）
    void DrawSubmesh(ID3D11DeviceContext* context, uint32_t submeshIndex);
    
    // 获取子网格数量
    uint32_t GetSubmeshCount() const { return static_cast<uint32_t>(submeshes.size()); }
    
    // 获取子网格信息
    const Submesh& GetSubmesh(uint32_t index) const { return submeshes[index]; }

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer;
    UINT vertexStride = sizeof(Vertex);
    UINT indexCount = 0;
    UINT vertexCount = 0;
    bool hasIndices = false;
    std::vector<Submesh> submeshes;  // 子网格列表
};
