#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <vector>
#include <string>
#include <memory>

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include "Mesh.h"

// ============================================================================
// 地形参数
// ============================================================================
struct TerrainNewParams
{
    int gridWidth = 256;              // 网格宽度（顶点数）
    int gridHeight = 256;             // 网格高度（顶点数）
    float worldSizeX = 1024.0f;       // 世界空间X大小
    float worldSizeZ = 1024.0f;       // 世界空间Z大小
    float heightScale = 100.0f;       // 高度缩放因子
    float heightOffset = 0.0f;        // 高度偏移
    
    // Chunk参数
    int chunkSize = 64;               // 每个chunk的网格大小（顶点数-1）
    int maxLODLevels = 4;             // 最大LOD级别数
    float lodDistances[4] = {50.0f, 150.0f, 400.0f, 1000.0f};  // 每个LOD级别的距离阈值
    
    // Morphing参数
    float morphStartRatio = 0.66f;   // Morphing开始距离比例（66%处开始）
};

// ============================================================================
// Chunk结构 - 表示地形的一个区块
// ============================================================================
struct TerrainChunk
{
    int chunkX;                       // Chunk在X方向的索引
    int chunkZ;                       // Chunk在Z方向的索引
    int lodLevel;                     // 当前LOD级别
    float morphFactor;                // Morphing因子 (0-1)
    
    float minX, minZ, maxX, maxZ;    // 世界空间边界
    float minY, maxY;                 // 高度范围（用于剔除）
    
    // GPU资源（每个LOD级别都有独立的缓冲区）
    std::vector<Microsoft::WRL::ComPtr<ID3D11Buffer>> vertexBuffers;  // 每个LOD的顶点缓冲区
    std::vector<Microsoft::WRL::ComPtr<ID3D11Buffer>> indexBuffers;   // 每个LOD的索引缓冲区
    std::vector<UINT> indexCounts;                                    // 每个LOD的索引数量
    
    // 计算到相机的距离
    float GetDistanceToCamera(float camX, float camY, float camZ) const
    {
        float centerX = (minX + maxX) * 0.5f;
        float centerY = (minY + maxY) * 0.5f;
        float centerZ = (minZ + maxZ) * 0.5f;
        
        float dx = centerX - camX;
        float dy = centerY - camY;
        float dz = centerZ - camZ;
        
        return sqrtf(dx * dx + dy * dy + dz * dz);
    }
    
    // 获取AABB用于剔除（简化版，不使用DirectXCollision）
    void GetBoundingBox(float& outMinX, float& outMinY, float& outMinZ, 
                        float& outMaxX, float& outMaxY, float& outMaxZ) const
    {
        outMinX = minX; outMinY = minY; outMinZ = minZ;
        outMaxX = maxX; outMaxY = maxY; outMaxZ = maxZ;
    }
};

// ============================================================================
// 四叉树节点
// ============================================================================
struct QuadTreeNode
{
    float minX, minZ, maxX, maxZ;    // 世界空间边界
    float minY, maxY;                 // 高度范围
    
    int lodLevel;                     // LOD级别
    int chunkX, chunkZ;                // Chunk索引
    
    int childIndices[4];              // 子节点索引 [TopLeft, TopRight, BottomLeft, BottomRight]
    bool hasChildren;
    
    QuadTreeNode()
        : minX(0), minZ(0), maxX(0), maxZ(0)
        , minY(0), maxY(0)
        , lodLevel(0), chunkX(0), chunkZ(0)
        , hasChildren(false)
    {
        childIndices[0] = childIndices[1] = childIndices[2] = childIndices[3] = -1;
    }
};

// ============================================================================
// 新地形类 - 带Chunk和LOD的地形系统
// ============================================================================
class TerrainNew
{
public:
    TerrainNew();
    ~TerrainNew();

    // 从高度图创建地形
    bool CreateFromHeightmap(ID3D11Device* device, const std::wstring& heightmapPath,
                             const TerrainNewParams& params);

    // 程序化生成地形（随机算法）
    bool CreateProcedural(ID3D11Device* device, const TerrainNewParams& params);

    // 渲染地形（需要相机位置）
    void Render(ID3D11DeviceContext* context, const DirectX::XMFLOAT3& cameraPosition);

    // 查询指定世界坐标的高度
    float GetHeightAt(float worldX, float worldZ) const;

    // 访问器
    const TerrainNewParams& GetParams() const { return m_params; }
    
    // 获取统计信息
    struct RenderStats
    {
        int visibleChunks = 0;
        int culledChunks = 0;
        int lodDistribution[8] = {0};
    };
    const RenderStats& GetStats() const { return m_renderStats; }

private:
    // 加载高度图
    bool LoadHeightmap(const std::wstring& path);

    // 生成程序化高度数据（随机算法）
    void GenerateProceduralHeight();

    // 平滑高度图（减少突变）
    void SmoothHeightmap(int width, int height);

    // 生成所有chunk的网格（所有LOD级别）
    bool GenerateChunks(ID3D11Device* device);

    // 生成单个chunk的网格（指定LOD级别）
    void GenerateChunkMesh(int chunkX, int chunkZ, int lodLevel,
                           std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);

    // 创建chunk的GPU缓冲区
    bool CreateChunkBuffers(ID3D11Device* device, TerrainChunk& chunk,
                            const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices,
                            int lodLevel);

    // 构建四叉树
    void BuildQuadTree();

    // 选择要渲染的chunk（基于距离和LOD）
    void SelectChunks(const DirectX::XMFLOAT3& cameraPosition, std::vector<TerrainChunk*>& outChunks);

    // 计算chunk的LOD级别（基于距离）
    int CalculateLODLevel(float distance) const;

    // 计算morphing因子（基于距离和LOD级别）
    float CalculateMorphFactor(float distance, int lodLevel) const;

    // 计算chunk的高度范围
    void CalculateChunkHeightRange(TerrainChunk& chunk);

private:
    TerrainNewParams m_params;

    // 高度数据（归一化到0-1范围）
    std::vector<float> m_heightData;
    int m_heightmapWidth;
    int m_heightmapHeight;

    // Chunk数据
    std::vector<TerrainChunk> m_chunks;
    int m_chunkCountX;
    int m_chunkCountZ;

    // 四叉树
    std::vector<QuadTreeNode> m_quadTree;
    std::vector<int> m_rootNodeIndices;

    // 渲染统计
    RenderStats m_renderStats;
    std::vector<TerrainChunk*> m_selectedChunks;
    
    // Chunk常量缓冲区（用于传递morphing参数）
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_chunkConstantBuffer;
};
