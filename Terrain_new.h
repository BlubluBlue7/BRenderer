#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <vector>
#include <string>
#include <memory>

#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>  // 用于ID3D11DeviceContext1
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
    
    // 纹理路径（可选）
    std::wstring normalmapPath;       // 法线图路径（如果为空则从高度图计算法线）
};

// ============================================================================
// 共享LOD索引数据 - 所有chunk共享相同LOD级别的索引
// ============================================================================
struct SharedLODIndices
{
    Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer;   // 索引缓冲区
    UINT indexCount;                                    // 索引数量
    int gridSize;                                       // 网格大小（顶点数-1）
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
    
    // 每个chunk的顶点缓冲区（不同LOD级别）
    std::vector<Microsoft::WRL::ComPtr<ID3D11Buffer>> vertexBuffers;
    
    // 计算到相机的距离（使用到AABB最近点的距离，而非中心距离）
    float GetDistanceToCamera(float camX, float camY, float camZ) const
    {
        // 计算相机到AABB最近点的距离
        // 对于每个轴，如果相机在包围盒内部，距离为0
        // 如果在外部，距离为到最近边界的距离
        float dx = 0.0f, dy = 0.0f, dz = 0.0f;
        
        if (camX < minX) dx = minX - camX;
        else if (camX > maxX) dx = camX - maxX;
        
        if (camY < minY) dy = minY - camY;
        else if (camY > maxY) dy = camY - maxY;
        
        if (camZ < minZ) dz = minZ - camZ;
        else if (camZ > maxZ) dz = camZ - maxZ;
        
        return sqrtf(dx * dx + dy * dy + dz * dz);
    }
    
    // 计算到相机的XZ平面距离（忽略高度，用于LOD计算）
    float GetDistanceToCameraXZ(float camX, float camZ) const
    {
        float dx = 0.0f, dz = 0.0f;
        
        if (camX < minX) dx = minX - camX;
        else if (camX > maxX) dx = camX - maxX;
        
        if (camZ < minZ) dz = minZ - camZ;
        else if (camZ > maxZ) dz = camZ - maxZ;
        
        return sqrtf(dx * dx + dz * dz);
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
// 简单的AABB包围盒（用于视锥剔除）
// ============================================================================
struct AABB
{
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
    
    AABB() : minX(0), minY(0), minZ(0), maxX(0), maxY(0), maxZ(0) {}
    
    AABB(float minX_, float minY_, float minZ_, float maxX_, float maxY_, float maxZ_)
        : minX(minX_), minY(minY_), minZ(minZ_)
        , maxX(maxX_), maxY(maxY_), maxZ(maxZ_) {}
    
    // 获取中心点
    void GetCenter(float& x, float& y, float& z) const
    {
        x = (minX + maxX) * 0.5f;
        y = (minY + maxY) * 0.5f;
        z = (minZ + maxZ) * 0.5f;
    }
    
    // 获取半径（用于球体近似）
    float GetRadius() const
    {
        float dx = (maxX - minX) * 0.5f;
        float dy = (maxY - minY) * 0.5f;
        float dz = (maxZ - minZ) * 0.5f;
        return sqrtf(dx * dx + dy * dy + dz * dz);
    }
};

// ============================================================================
// 四叉树节点 - 支持真正的层次结构
// ============================================================================
struct QuadTreeNode
{
    float minX, minZ, maxX, maxZ;    // 世界空间边界（XZ平面）
    float minY, maxY;                 // 高度范围（用于剔除）
    float centerX, centerZ;           // 中心点（用于距离计算）
    float size;                       // 节点大小（边长）
    
    int level;                        // 树的层级（0=根节点，仅用于调试和结构）
    // 注意：移除了lodLevel字段，LOD级别完全基于距离动态计算
    
    // 如果是叶子节点，存储对应的chunk索引
    int chunkStartX, chunkStartZ;     // Chunk起始索引
    int chunkEndX, chunkEndZ;         // Chunk结束索引
    bool isLeaf;                      // 是否是叶子节点
    
    // 如果是分支节点，存储子节点索引
    int childIndices[4];              // 子节点索引 [0]=LeftTop, [1]=RightTop, [2]=LeftBottom, [3]=RightBottom
    bool hasChildren;
    
    QuadTreeNode()
        : minX(0), minZ(0), maxX(0), maxZ(0)
        , minY(0), maxY(0)
        , centerX(0), centerZ(0), size(0)
        , level(0)
        , chunkStartX(0), chunkStartZ(0)
        , chunkEndX(0), chunkEndZ(0)
        , isLeaf(false), hasChildren(false)
    {
        childIndices[0] = childIndices[1] = childIndices[2] = childIndices[3] = -1;
    }
    
    // 计算到相机的距离（使用到AABB最近点的距离）
    float GetDistanceToCamera(float camX, float camY, float camZ) const
    {
        float dx = 0.0f, dy = 0.0f, dz = 0.0f;
        
        if (camX < minX) dx = minX - camX;
        else if (camX > maxX) dx = camX - maxX;
        
        float centerY = (minY + maxY) * 0.5f;
        float halfHeight = (maxY - minY) * 0.5f;
        if (camY < centerY - halfHeight) dy = (centerY - halfHeight) - camY;
        else if (camY > centerY + halfHeight) dy = camY - (centerY + halfHeight);
        
        if (camZ < minZ) dz = minZ - camZ;
        else if (camZ > maxZ) dz = camZ - maxZ;
        
        return sqrtf(dx * dx + dy * dy + dz * dz);
    }
    
    // 计算到相机的XZ平面距离（用于LOD和细分决策）
    float GetDistanceToCameraXZ(float camX, float camZ) const
    {
        float dx = 0.0f, dz = 0.0f;
        
        if (camX < minX) dx = minX - camX;
        else if (camX > maxX) dx = camX - maxX;
        
        if (camZ < minZ) dz = minZ - camZ;
        else if (camZ > maxZ) dz = camZ - maxZ;
        
        return sqrtf(dx * dx + dz * dz);
    }
    
    // 检查点是否在节点范围内
    bool Contains(float x, float z) const
    {
        return x >= minX && x <= maxX && z >= minZ && z <= maxZ;
    }
};

// ============================================================================
// GPU端数据结构（用于Compute Shader）
// ============================================================================
struct alignas(16) TerrainChunkDataGPU
{
    DirectX::XMFLOAT4 bounds;        // minX, minZ, maxX, maxZ
    DirectX::XMFLOAT4 heightRange;   // minY, maxY, unused, unused
    DirectX::XMUINT2 chunkIndex;     // chunkX, chunkZ
    UINT vertexBufferOffset;         // 在统一VB中的偏移（顶点数）
    UINT indexBufferOffset;          // 在统一IB中的偏移（索引数）
};

struct alignas(16) QuadTreeNodeGPU
{
    DirectX::XMFLOAT4 bounds;        // minX, minZ, maxX, maxZ
    DirectX::XMFLOAT4 heightRange;   // minY, maxY, centerX, centerZ
    DirectX::XMUINT4 children;       // child indices [0]=LT, [1]=RT, [2]=LB, [3]=RB
    DirectX::XMUINT2 chunkRange;     // chunkStartX, chunkStartZ (if leaf)
    UINT isLeaf;                     // 1=叶子节点, 0=分支节点
    UINT padding;
};

struct alignas(16) ChunkInstanceDataGPU
{
    DirectX::XMFLOAT4 chunkParams;   // x=chunkDistToCamera, y=lodLevel, z=morphStart, w=morphEnd
    DirectX::XMFLOAT4 chunkBounds;   // x=minX, y=minZ, z=maxX, w=maxZ
    UINT vertexOffset;
    UINT indexOffset;
    UINT indexCount;
    UINT padding;
};

// 间接绘制参数（必须匹配D3D11_DRAW_INDEXED_INSTANCED_INDIRECT_ARGS）
struct DrawIndexedIndirectArgs
{
    UINT IndexCountPerInstance;
    UINT InstanceCount;
    UINT StartIndexLocation;
    INT BaseVertexLocation;
    UINT StartInstanceLocation;
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
    
    // 从高度图和法线图创建地形
    bool CreateFromHeightmapAndNormalmap(ID3D11Device* device, 
                                         const std::wstring& heightmapPath,
                                         const std::wstring& normalmapPath,
                                         const TerrainNewParams& params);

    // 程序化生成地形（随机算法）
    bool CreateProcedural(ID3D11Device* device, const TerrainNewParams& params);

    // 渲染地形（需要相机位置）
    void Render(ID3D11DeviceContext* context, const DirectX::XMFLOAT3& cameraPosition);
    
    // GPU Driven渲染函数（需要view和proj矩阵）
    void RenderGPUDriven(ID3D11DeviceContext* context, const DirectX::XMFLOAT3& cameraPosition,
                         const DirectX::XMFLOAT4X4& viewMatrix, const DirectX::XMFLOAT4X4& projMatrix);

    // 查询指定世界坐标的高度
    float GetHeightAt(float worldX, float worldZ) const;

    // 访问器
    const TerrainNewParams& GetParams() const { return m_params; }
    
    // 启用/禁用GPU Driven模式
    void SetUseGPUDriven(bool enable) { m_useGPUDriven = enable; }
    bool IsUsingGPUDriven() const { return m_useGPUDriven; }
    
    // 切换LOD调试可视化模式
    void ToggleLODDebug() { m_showLODDebug = !m_showLODDebug; }
    void SetLODDebug(bool show) { m_showLODDebug = show; }
    bool IsLODDebugEnabled() const { return m_showLODDebug; }

    // 切换深度调试可视化模式
    void ToggleDepthDebug() { m_showDepthDebug = !m_showDepthDebug; }
    void SetDepthDebug(bool show) { m_showDepthDebug = show; }
    bool IsDepthDebugEnabled() const { return m_showDepthDebug; }

    // 切换阴影调试可视化模式
    void ToggleShadowDebug() { m_showShadowDebug = !m_showShadowDebug; }
    void SetShadowDebug(bool show) { m_showShadowDebug = show; }
    bool IsShadowDebugEnabled() const { return m_showShadowDebug; }
    
    // 获取统计信息
    struct RenderStats
    {
        int visibleChunks = 0;
        int culledChunks = 0;
        int lodDistribution[8] = {0};
    };
    const RenderStats& GetStats() const { return m_renderStats; }

    // 暴露高度图资源（用于水体等系统在shader中采样地形高度）
    ID3D11ShaderResourceView* GetHeightmapSRV() const { return m_heightmapSRV.Get(); }
    ID3D11SamplerState* GetHeightmapSampler() const { return m_heightmapSampler.Get(); }

private:
    // 加载高度图
    bool LoadHeightmap(const std::wstring& path);
    
    // 加载法线图
    bool LoadNormalmap(const std::wstring& path);

    // 生成程序化高度数据（随机算法）
    void GenerateProceduralHeight();

    // 平滑高度图（减少突变）
    void SmoothHeightmap(int width, int height);

    // 生成共享的LOD索引（所有LOD级别）
    bool GenerateSharedLODIndices(ID3D11Device* device);
    
    // 创建高度图GPU纹理（用于shader中采样）
    bool CreateHeightmapTexture(ID3D11Device* device);
    
    // 创建法线图GPU纹理（用于shader中采样）
    bool CreateNormalmapTexture(ID3D11Device* device);
    
    // 生成所有chunk的顶点数据
    bool GenerateChunkVertices(ID3D11Device* device);

    // 生成LOD索引模板
    void GenerateLODIndicesTemplate(int lodLevel, int gridSize, std::vector<uint32_t>& indices);

    // 生成单个chunk特定LOD的顶点
    void GenerateChunkLODVertices(int chunkX, int chunkZ, int lodLevel, std::vector<Vertex>& vertices);

    // 创建LOD索引缓冲区
    bool CreateLODIndexBuffer(ID3D11Device* device, SharedLODIndices& lodIndices,
                              const std::vector<uint32_t>& indices);
    
    // 创建chunk顶点缓冲区
    bool CreateChunkVertexBuffer(ID3D11Device* device, TerrainChunk& chunk, int lodLevel,
                                 const std::vector<Vertex>& vertices);

    // 构建四叉树（递归构建层次结构）
    void BuildQuadTree();
    
    // 递归构建四叉树节点
    int BuildQuadTreeRecursive(float minX, float minZ, float maxX, float maxZ, 
                               int level, int maxDepth);
    
    // 计算节点的高度范围
    void CalculateNodeHeightRange(QuadTreeNode& node);

    // 选择要渲染的chunk（基于四叉树，支持视锥剔除）
    void SelectChunks(const DirectX::XMFLOAT3& cameraPosition, std::vector<TerrainChunk*>& outChunks);
    
    // 递归选择chunk（四叉树遍历）
    void SelectChunksRecursive(int nodeIndex, const DirectX::XMFLOAT3& cameraPosition,
                               float viewDistance, std::vector<TerrainChunk*>& outChunks);
    
    // 判断节点是否应该细分（基于屏幕空间误差）
    bool ShouldSubdivide(const QuadTreeNode& node, const DirectX::XMFLOAT3& cameraPosition,
                         float viewDistance) const;

    // 计算chunk的LOD级别（基于距离）
    int CalculateLODLevel(float distance) const;

    // 计算morphing因子（基于距离和LOD级别）
    float CalculateMorphFactor(float distance, int lodLevel) const;
    
    // 应用邻居LOD约束（确保相邻chunk的LOD差不超过1级）
    void ApplyNeighborLODConstraints(std::vector<TerrainChunk*>& chunks);
    
    // 获取chunk的邻居LOD级别（8个方向）
    int GetNeighborMaxLOD(int chunkX, int chunkZ, const std::vector<int>& lodMap) const;

    // 计算chunk的高度范围
    void CalculateChunkHeightRange(TerrainChunk& chunk);

private:
    TerrainNewParams m_params;

    // 高度数据（归一化到0-1范围）
    std::vector<float> m_heightData;
    int m_heightmapWidth;
    int m_heightmapHeight;

    // 共享的LOD索引数据（所有chunk共享）
    std::vector<SharedLODIndices> m_sharedLODIndices;
    
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
    
    // 地形调试常量缓冲区（用于传递LOD调试标志）
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_terrainDebugBuffer;
    
    // 高度图纹理资源（用于shader中动态采样高度）
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_heightmapTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_heightmapSRV;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_heightmapSampler;
    
    // 法线图纹理资源（用于shader中采样法线，如果提供）
    std::vector<unsigned char> m_normalmapData;  // 法线图数据（RGB，每个通道0-255）
    int m_normalmapWidth;
    int m_normalmapHeight;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_normalmapTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_normalmapSRV;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_normalmapSampler;
    bool m_hasNormalmap;  // 是否已加载法线图
    
    // GPU Driven相关资源
    bool m_useGPUDriven;  // 是否使用GPU Driven模式
    
    // LOD调试可视化
    bool m_showLODDebug;  // 是否显示LOD调试颜色
    bool m_showDepthDebug; // 是否显示深度调试
    bool m_showShadowDebug; // 是否显示阴影调试
    
    // GPU端Structured Buffer
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_chunkDataBuffer;          // Chunk数据（SRV）
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_lodIndexCountsBuffer;     // 每个LOD的索引数量（SRV）
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_visibleChunkBuffer;       // 可见chunk索引列表（UAV）
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_drawCommandsBuffer;       // 间接绘制参数（UAV）
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_chunkInstanceBuffer;      // Chunk实例数据（UAV）
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_visibleCountBuffer;       // 可见chunk计数（UAV）
    
    // UAV视图
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_visibleChunkUAV;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_drawCommandsUAV;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_chunkInstanceUAV;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_visibleCountUAV;
    
    // SRV视图
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_chunkDataSRV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_lodIndexCountsSRV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_chunkInstanceSRV;  // 用于VS读取
    
    // Compute Shader
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_cullComputeShader;
    
    // 统一缓冲区（方案A：合并所有chunk的顶点和索引）
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_unifiedVertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_unifiedIndexBuffer;
    
    // GPU Cull参数常量缓冲区
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_cullParamsBuffer;
    
    // GPU资源创建函数
    bool CreateGPUBuffers(ID3D11Device* device);
    bool CreateComputeShader(ID3D11Device* device);
    void UploadDataToGPU(ID3D11DeviceContext* context);
    void UpdateCullParams(ID3D11DeviceContext* context, const DirectX::XMFLOAT3& cameraPosition,
                          const DirectX::XMFLOAT4X4& viewMatrix, const DirectX::XMFLOAT4X4& projMatrix);
    
    // 创建统一顶点/索引缓冲区
    bool CreateUnifiedBuffers(ID3D11Device* device);
};
