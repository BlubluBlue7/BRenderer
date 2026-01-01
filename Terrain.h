#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <vector>
#include <string>
#include <array>
#include <memory>

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include "Mesh.h"

// ============================================================================
// CDLOD配置参数 (Continuous Distance-Dependent Level of Detail)
// 基于 Filip Strugar 的论文实现，扩展支持超大规模地形
// ============================================================================
struct CDLODSettings
{
    // 四叉树配置
    int maxLODLevels = 10;                    // 最大LOD级别数 (0 = 最高细节)，支持2^10 = 1024倍缩放
    int gridMeshDimension = 32;               // 网格模板的边长（顶点数-1），必须是2的幂
    
    // 距离配置
    float LOD0Range = 50.0f;                  // LOD 0 的可见距离范围（米）
    float LODDistanceRatio = 2.0f;            // 相邻LOD距离范围的比率（每级别距离翻倍）
    
    // Morphing配置
    float morphStartRatio = 0.66f;            // 开始morphing的距离比例 (0.66 = 在66%处开始)
    bool enableMorphing = true;               // 是否启用morphing
    
    // 渲染配置
    bool enableFrustumCulling = true;         // 是否启用视锥剔除
    bool debugVisualization = false;          // 调试可视化
    bool enableLODColorDebug = false;         // 启用LOD颜色调试（不同LOD显示不同颜色）
    
    // 计算指定LOD级别的范围距离
    float GetLODRange(int lodLevel) const
    {
        return LOD0Range * powf(LODDistanceRatio, static_cast<float>(lodLevel));
    }
    
    // 计算开始morphing的距离
    float GetMorphStart(int lodLevel) const
    {
        float range = GetLODRange(lodLevel);
        return range * morphStartRatio;
    }
    
    // 计算结束morphing的距离（即当前LOD级别的范围）
    float GetMorphEnd(int lodLevel) const
    {
        return GetLODRange(lodLevel);
    }
    
    // 计算最大可见距离（最粗糙LOD的范围）
    float GetMaxVisibleDistance() const
    {
        return GetLODRange(maxLODLevels - 1);
    }
    
    // 打印LOD距离配置（调试用）
    void PrintLODRanges() const
    {
        wchar_t msg[512];
        swprintf_s(msg, L"[CDLOD] LOD Ranges (max distance: %.0fm):\n", GetMaxVisibleDistance());
        OutputDebugStringW(msg);
        
        for (int i = 0; i < maxLODLevels; ++i)
        {
            float range = GetLODRange(i);
            float morphStart = GetMorphStart(i);
            swprintf_s(msg, L"  LOD %d: range=%.0fm, morph starts at %.0fm\n", i, range, morphStart);
            OutputDebugStringW(msg);
        }
    }
};

// ============================================================================
// 地形参数
// ============================================================================
struct TerrainParams
{
    int heightmapWidth = 1025;                // 高度图宽度（像素）
    int heightmapHeight = 1025;               // 高度图高度（像素）
    float worldSizeX = 1024.0f;               // 世界空间X大小
    float worldSizeZ = 1024.0f;               // 世界空间Z大小
    float heightScale = 100.0f;               // 高度缩放因子
    float heightOffset = 0.0f;                // 高度偏移
};

// ============================================================================
// CDLOD四叉树节点
// 表示地形的一个区域，可以有4个子节点
// ============================================================================
struct CDLODNode
{
    // 节点边界（世界空间）
    float minX, minZ;                         // 节点最小坐标
    float maxX, maxZ;                         // 节点最大坐标
    float minY, maxY;                         // 高度范围（用于剔除）
    
    // 节点信息
    int lodLevel;                             // LOD级别 (0 = 最细节)
    int x, z;                                 // 在该LOD级别的网格坐标
    
    // 渲染时计算的数据
    bool isSelected;                          // 是否被选择用于渲染
    float morphFactor;                        // Morphing因子 (0-1)
    float distanceToCamera;                   // 到相机的距离
    
    // 子节点索引（-1表示没有子节点）
    int childIndices[4];                      // [TopLeft, TopRight, BottomLeft, BottomRight]
    
    // 获取节点中心
    float GetCenterX() const { return (minX + maxX) * 0.5f; }
    float GetCenterZ() const { return (minZ + maxZ) * 0.5f; }
    float GetCenterY() const { return (minY + maxY) * 0.5f; }
    
    // 获取节点大小
    float GetSizeX() const { return maxX - minX; }
    float GetSizeZ() const { return maxZ - minZ; }
    
    // 计算到相机的距离（使用节点中心）
    float ComputeDistance(float camX, float camY, float camZ) const
    {
        float dx = GetCenterX() - camX;
        float dy = GetCenterY() - camY;
        float dz = GetCenterZ() - camZ;
        return sqrtf(dx * dx + dy * dy + dz * dz);
    }
    
    // 获取AABB用于剔除
    DirectX::BoundingBox GetBoundingBox() const
    {
        DirectX::XMFLOAT3 center(GetCenterX(), GetCenterY(), GetCenterZ());
        DirectX::XMFLOAT3 extents(GetSizeX() * 0.5f, (maxY - minY) * 0.5f, GetSizeZ() * 0.5f);
        return DirectX::BoundingBox(center, extents);
    }
    
    CDLODNode()
        : minX(0), minZ(0), maxX(0), maxZ(0)
        , minY(0), maxY(0)
        , lodLevel(0), x(0), z(0)
        , isSelected(false), morphFactor(0), distanceToCamera(0)
    {
        childIndices[0] = childIndices[1] = childIndices[2] = childIndices[3] = -1;
    }
};

// ============================================================================
// 渲染节点 - 被选择用于渲染的节点
// ============================================================================
struct CDLODRenderNode
{
    const CDLODNode* node;                    // 指向四叉树节点
    float morphFactor;                        // Morphing因子
    
    // 相邻节点的LOD级别（用于缝合）
    // -1 = 边界（无邻居）, 其他值 = 邻居的LOD级别
    int neighborLOD[4];                       // [Top, Bottom, Left, Right]
    
    CDLODRenderNode() : node(nullptr), morphFactor(0)
    {
        neighborLOD[0] = neighborLOD[1] = neighborLOD[2] = neighborLOD[3] = -1;
    }
};

// ============================================================================
// 视锥体
// ============================================================================
struct CDLODFrustum
{
    DirectX::XMFLOAT4 planes[6];
    
    void ExtractFromMatrix(const DirectX::XMMATRIX& viewProj);
    bool Intersects(const DirectX::BoundingBox& box) const;
};

// ============================================================================
// 渲染常量缓冲区 - 传递给GPU的地形渲染参数
// ============================================================================
struct alignas(16) TerrainCBuffer
{
    DirectX::XMFLOAT4 terrainScale;           // xyz: worldSize, w: heightScale
    DirectX::XMFLOAT4 terrainOffset;          // xyz: worldOffset, w: heightOffset
    DirectX::XMFLOAT4 morphParams;            // x: morphFactor, y: gridDim, z: lodLevel, w: unused
    DirectX::XMFLOAT4 nodeParams;             // xy: nodeOffset, zw: nodeScale
    DirectX::XMFLOAT4 heightmapSize;          // xy: heightmapSize, zw: 1/heightmapSize
};

// ============================================================================
// 渲染统计
// ============================================================================
struct CDLODStats
{
    int totalNodes = 0;                       // 四叉树总节点数
    int selectedNodes = 0;                    // 选择用于渲染的节点数
    int culledNodes = 0;                      // 被剔除的节点数
    int drawCalls = 0;                        // 绘制调用数
    int triangleCount = 0;                    // 三角形数量
    int lodDistribution[8] = {0};             // 每个LOD级别的节点数
};

// ============================================================================
// CDLOD四叉树 - 管理地形的四叉树结构
// ============================================================================
class CDLODQuadTree
{
public:
    CDLODQuadTree();
    ~CDLODQuadTree();
    
    // 初始化四叉树
    bool Initialize(const TerrainParams& params, const CDLODSettings& settings,
                   const std::vector<float>& heightData);
    
    // 选择用于渲染的节点（主要的LOD选择算法）
    void SelectNodes(const DirectX::XMFLOAT3& cameraPos,
                    const CDLODFrustum* frustum,
                    std::vector<CDLODRenderNode>& outRenderNodes);
    
    // 获取节点
    const CDLODNode& GetNode(int index) const { return m_nodes[index]; }
    int GetNodeCount() const { return static_cast<int>(m_nodes.size()); }
    
    // 获取根节点数量
    int GetRootNodeCountX() const { return m_rootNodesX; }
    int GetRootNodeCountZ() const { return m_rootNodesZ; }
    
private:
    // 构建四叉树
    void BuildQuadTree(const std::vector<float>& heightData);
    int CreateNode(int lodLevel, int x, int z, float minX, float minZ, float maxX, float maxZ);
    void ComputeNodeHeightRange(CDLODNode& node, const std::vector<float>& heightData);
    
    // 递归选择节点
    void SelectNode(int nodeIndex, 
                   const DirectX::XMFLOAT3& cameraPos,
                   const CDLODFrustum* frustum,
                   std::vector<CDLODRenderNode>& outRenderNodes);
    
    // 判断节点是否应该细分
    bool ShouldRefine(const CDLODNode& node, float distance) const;
    
    // 计算morphing因子
    float ComputeMorphFactor(float distance, int lodLevel) const;
    
    // 更新邻居LOD信息
    void UpdateNeighborLODs(std::vector<CDLODRenderNode>& renderNodes);
    
private:
    TerrainParams m_terrainParams;
    CDLODSettings m_settings;
    
    std::vector<CDLODNode> m_nodes;           // 所有四叉树节点
    std::vector<int> m_rootNodeIndices;       // 根节点索引列表
    
    int m_rootNodesX;                         // X方向的根节点数量
    int m_rootNodesZ;                         // Z方向的根节点数量
    int m_maxLODLevel;                        // 实际使用的最大LOD级别
};

// ============================================================================
// CDLOD网格模板 - 预生成的网格索引数据
// ============================================================================
class CDLODMeshTemplate
{
public:
    CDLODMeshTemplate();
    ~CDLODMeshTemplate();
    
    // 初始化网格模板
    bool Initialize(ID3D11Device* device, int gridDimension);
    
    // 获取索引缓冲区
    // stitchMask: bit0=Top, bit1=Bottom, bit2=Left, bit3=Right
    // 如果对应位为1，则该边界需要降低分辨率以匹配较粗糙的邻居
    ID3D11Buffer* GetIndexBuffer(int stitchMask) const;
    UINT GetIndexCount(int stitchMask) const;
    
    // 获取顶点缓冲区
    ID3D11Buffer* GetVertexBuffer() const { return m_vertexBuffer.Get(); }
    UINT GetVertexCount() const { return m_vertexCount; }
    
    int GetGridDimension() const { return m_gridDimension; }
    
private:
    // 生成网格顶点（局部坐标0-1范围）
    void GenerateVertices(std::vector<Vertex>& vertices);
    
    // 生成网格索引
    void GenerateIndices(std::vector<uint32_t>& indices, int stitchMask);
    
    // 创建索引缓冲区
    bool CreateIndexBuffer(ID3D11Device* device, int stitchMask);
    
private:
    int m_gridDimension;                      // 网格维度（边长的单元格数）
    
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;
    UINT m_vertexCount;
    
    // 16种缝合配置的索引缓冲区
    std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>, 16> m_indexBuffers;
    std::array<UINT, 16> m_indexCounts;
};

// ============================================================================
// CDLOD地形主类
// ============================================================================
class Terrain
{
public:
    Terrain();
    ~Terrain();
    
    // 从高度图创建地形
    bool CreateFromHeightmap(ID3D11Device* device, const std::wstring& heightmapPath, 
                            const TerrainParams& params);
    
    // 程序化生成地形
    bool CreateProcedural(ID3D11Device* device, const TerrainParams& params);
    
    // 渲染地形
    void Render(ID3D11DeviceContext* context, const DirectX::XMFLOAT3& cameraPosition,
               const DirectX::XMMATRIX& viewMatrix, const DirectX::XMMATRIX& projMatrix);
    
    // 简单渲染（不使用LOD）
    void Render(ID3D11DeviceContext* context);
    
    // 查询指定世界坐标的高度
    float GetHeightAt(float worldX, float worldZ) const;
    
    // 访问器
    const TerrainParams& GetParams() const { return m_params; }
    CDLODSettings& GetSettings() { return m_settings; }
    const CDLODSettings& GetSettings() const { return m_settings; }
    const CDLODStats& GetStats() const { return m_stats; }
    
    // 获取高度图纹理（供着色器采样）
    ID3D11ShaderResourceView* GetHeightmapSRV() const { return m_heightmapSRV.Get(); }
    
    // LOD锁定（调试用）
    void SetLODLocked(bool locked) { m_lodLocked = locked; }
    bool IsLODLocked() const { return m_lodLocked; }
    void SetLockedLODLevel(int level);
    int GetLockedLODLevel() const { return m_lockedLODLevel; }
    
    // 获取缓冲区（兼容旧接口）
    ID3D11Buffer* GetVertexBuffer() const;
    ID3D11Buffer* GetIndexBuffer() const;
    UINT GetIndexCount() const;
    
    // 渲染统计
    struct RenderStats
    {
        int visiblePatches = 0;
        int culledPatches = 0;
        int drawCalls = 0;
        int totalTriangles = 0;
        int lodDistribution[8] = {0};
    };
    const RenderStats& GetRenderStats() const { return m_renderStats; }
    
private:
    // 加载高度图
    bool LoadHeightmap(const std::wstring& path);
    
    // 生成程序化高度数据
    void GenerateProceduralHeight();
    
    // 初始化CDLOD系统
    bool InitializeCDLOD(ID3D11Device* device);
    
    // 创建高度图纹理
    bool CreateHeightmapTexture(ID3D11Device* device);
    
    // 创建地形常量缓冲区
    bool CreateConstantBuffer(ID3D11Device* device);
    
    // 渲染选中的节点
    void RenderSelectedNodes(ID3D11DeviceContext* context,
                            const std::vector<CDLODRenderNode>& renderNodes);
    
    // 更新常量缓冲区
    void UpdateConstantBuffer(ID3D11DeviceContext* context,
                             const CDLODRenderNode& node);
    
private:
    TerrainParams m_params;
    CDLODSettings m_settings;
    
    // 高度数据
    std::vector<float> m_heightData;
    
    // CDLOD组件
    std::unique_ptr<CDLODQuadTree> m_quadTree;
    std::unique_ptr<CDLODMeshTemplate> m_meshTemplate;
    
    // GPU资源
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_terrainCBuffer;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_heightmapTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_heightmapSRV;
    
    // 渲染节点缓存
    std::vector<CDLODRenderNode> m_renderNodes;
    
    // 视锥体
    CDLODFrustum m_frustum;
    bool m_frustumValid = false;
    
    // 统计信息
    CDLODStats m_stats;
    RenderStats m_renderStats;
    
    // 调试
    bool m_lodLocked = false;
    int m_lockedLODLevel = 0;
    int m_frameCount = 0;
};
