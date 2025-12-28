#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include "Mesh.h"  // 需要Vertex结构体的完整定义

// 地形参数结构
struct TerrainParams
{
    int width;          // 地形宽度（顶点数）
    int height;         // 地形高度（顶点数）
    float sizeX;        // 世界空间X方向大小
    float sizeZ;        // 世界空间Z方向大小
    float heightScale;  // 高度缩放因子
    float heightOffset; // 高度偏移量
};

class Terrain
{
public:
    Terrain();
    ~Terrain();

    // 从高度图文件创建地形
    bool CreateFromHeightmap(ID3D11Device* device, const std::wstring& heightmapPath, const TerrainParams& params);
    
    // 使用程序化高度数据创建地形（用于测试）
    bool CreateProcedural(ID3D11Device* device, const TerrainParams& params);
    
    // 渲染地形
    void Render(ID3D11DeviceContext* context);
    
    // 获取世界坐标处的地形高度（用于物体放置等）
    float GetHeightAt(float worldX, float worldZ) const;
    
    // 获取地形参数
    const TerrainParams& GetParams() const { return m_params; }
    
    // 获取顶点缓冲区和索引缓冲区
    ID3D11Buffer* GetVertexBuffer() const { return m_vertexBuffer.Get(); }
    ID3D11Buffer* GetIndexBuffer() const { return m_indexBuffer.Get(); }
    UINT GetIndexCount() const { return m_indexCount; }

private:
    // 加载高度图文件
    bool LoadHeightmap(const std::wstring& path, std::vector<float>& heightData);
    
    // 从高度数据生成地形网格
    void GenerateTerrainMesh(const std::vector<float>& heightData);
    
    // 计算法线向量
    void CalculateNormals();
    
    // 创建DirectX资源
    bool CreateBuffers(ID3D11Device* device);

private:
    TerrainParams m_params;
    
    // 顶点数据（使用与Mesh.h相同的Vertex结构）
    // 注意：需要包含Mesh.h来使用Vertex结构
    std::vector<Vertex> m_vertices;
    std::vector<uint32_t> m_indices;
    
    // 高度数据（用于高度查询）
    std::vector<float> m_heightData;
    
    // DirectX资源
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_indexBuffer;
    UINT m_indexCount;
};

