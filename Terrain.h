#pragma once

// 确保在包含 Windows.h 相关头文件之前定义 NOMINMAX，避免 min/max 宏冲突
#ifndef NOMINMAX
#define NOMINMAX
#endif

// 先包含标准库头文件
#include <vector>
#include <string>

// 然后包含Windows和DirectX头文件
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>
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

// CDLOD常量（必须在结构体定义之前）
#ifndef MAX_LOD_LEVELS
#define MAX_LOD_LEVELS 4  // 最大LOD级别数
#endif

// CDLOD相关结构
struct TerrainPatch
{
    // 世界空间边界框
    float minX, maxX;
    float minZ, maxZ;
    
    // 网格信息（每个LOD级别的索引范围）
    struct LODRange
    {
        UINT indexStart;
        UINT indexCount;
    };
    LODRange lodRanges[MAX_LOD_LEVELS];  // 每个LOD级别的索引范围
    
    int lodLevel;  // 当前选择的LOD级别（0=最高细节）
    
    // 中心点（用于距离计算）
    float centerX, centerZ;
    
    // 块在地形网格中的位置（顶点坐标）
    int patchX, patchZ;  // 块索引
    int startX, startZ;  // 起始顶点坐标
    int endX, endZ;      // 结束顶点坐标
};

// LOD网格数据（每个LOD级别一个）
struct LODMesh
{
    std::vector<uint32_t> indices;
    Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer;
    UINT indexCount;
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
    
    // 渲染地形（使用CDLOD）
    void Render(ID3D11DeviceContext* context, const DirectX::XMFLOAT3& cameraPosition);
    
    // 渲染地形（旧版本，保持兼容性）
    void Render(ID3D11DeviceContext* context);
    
    // 获取世界坐标处的地形高度（用于物体放置等）
    float GetHeightAt(float worldX, float worldZ) const;
    
    // 获取地形参数
    const TerrainParams& GetParams() const { return m_params; }
    
    // 获取顶点缓冲区和索引缓冲区
    ID3D11Buffer* GetVertexBuffer() const { return m_vertexBuffer.Get(); }
    ID3D11Buffer* GetIndexBuffer() const { return m_indexBuffer.Get(); }
    UINT GetIndexCount() const { return m_indexCount; }
    
    // CDLOD控制
    void SetLODLocked(bool locked) { m_lodLocked = locked; }  // 锁定LOD级别（用于调试）
    bool IsLODLocked() const { return m_lodLocked; }
    void SetLockedLODLevel(int level) { if (level >= 0 && level < MAX_LOD_LEVELS) { m_lockedLODLevel = level; m_lodLocked = true; } }

private:
    // 加载高度图文件
    bool LoadHeightmap(const std::wstring& path, std::vector<float>& heightData);
    
    // 从高度数据生成地形网格
    void GenerateTerrainMesh(const std::vector<float>& heightData);
    
    // 计算法线向量
    void CalculateNormals();
    
    // 创建DirectX资源
    bool CreateBuffers(ID3D11Device* device);
    
    // CDLOD相关函数
    void InitializeCDLOD(ID3D11Device* device);
    void GenerateLODMeshes(ID3D11Device* device);
    void GeneratePatches();
    void GeneratePatchIndices(ID3D11Device* device);
    void SelectLODPatches(const DirectX::XMFLOAT3& cameraPosition, std::vector<TerrainPatch>& visiblePatches);
    bool IsPatchVisible(const TerrainPatch& patch, const DirectX::XMFLOAT4X4& viewProjMatrix);

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
    
    // CDLOD相关
    bool m_useCDLOD;
    bool m_lodLocked;                      // 是否锁定LOD级别（用于调试查看网格）
    int m_lockedLODLevel;                  // 锁定的LOD级别
    std::vector<LODMesh> m_lodMeshes;      // 每个LOD级别的索引缓冲区
    std::vector<TerrainPatch> m_patches;   // 地形块列表
    int m_patchSize;                       // 每个块的顶点数（必须是2的幂+1，如17, 33, 65）
    float m_lodDistances[MAX_LOD_LEVELS];  // 每个LOD级别的距离阈值
};

