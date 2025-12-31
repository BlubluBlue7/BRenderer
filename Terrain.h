#pragma once

// 确保在包含 Windows.h 相关头文件之前定义 NOMINMAX，避免 min/max 宏冲突
#ifndef NOMINMAX
#define NOMINMAX
#endif

// 先包含标准库头文件
#include <vector>
#include <string>
#include <array>

// 然后包含Windows和DirectX头文件
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>  // 用于视锥剔除
#include "Mesh.h"  // 需要Vertex结构体的完整定义

// ============================================================================
// CDLOD配置参数（可在运行时调整）
// ============================================================================
struct CDLODConfig
{
    int maxLODLevels = 4;           // 最大LOD级别数
    int patchSize = 33;             // 块大小（必须是2^n + 1）
    float lodDistanceMultiplier = 2.0f;  // LOD距离倍增因子
    float baseLODDistance = 50.0f;       // 基础LOD距离
    bool enableMorphing = true;          // 是否启用顶点Morphing
    float morphStartRatio = 0.7f;        // Morphing开始的距离比例（0.7表示在到达下一级LOD的70%距离时开始）
    bool enableFrustumCulling = true;    // 是否启用视锥剔除
    bool debugMode = false;              // 调试模式
};

// 地形参数结构
struct TerrainParams
{
    int width = 256;           // 地形宽度（顶点数）
    int height = 256;          // 地形高度（顶点数）
    float sizeX = 400.0f;      // 世界空间X方向大小
    float sizeZ = 400.0f;      // 世界空间Z方向大小
    float heightScale = 30.0f; // 高度缩放因子
    float heightOffset = 0.0f; // 高度偏移量
};

// CDLOD常量
constexpr int MAX_LOD_LEVELS = 4;

// ============================================================================
// 地形块结构（优化版）
// ============================================================================
struct TerrainPatch
{
    // 世界空间边界框
    DirectX::BoundingBox boundingBox;
    
    // 中心点和范围
    float centerX, centerZ;
    float minY, maxY;  // 高度范围（用于更精确的剔除）
    
    // 块在地形网格中的位置
    int patchX, patchZ;  // 块索引
    int startX, startZ;  // 起始顶点坐标
    int endX, endZ;      // 结束顶点坐标
    
    // LOD信息
    int lodLevel = 0;           // 当前选择的LOD级别
    float morphFactor = 0.0f;   // Morphing因子（0=当前LOD，1=下一级LOD）
    float distanceToCamera = 0.0f;  // 到相机的距离
    
    // 是否可见
    bool isVisible = true;
};

// ============================================================================
// LOD网格模板（预生成的索引数据，每个LOD级别一个）
// ============================================================================
struct LODMeshTemplate
{
    std::vector<uint32_t> indices;
    Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer;
    UINT indexCount = 0;
    UINT triangleCount = 0;
};

// ============================================================================
// 批次渲染数据（用于合并同一LOD级别的多个块）
// ============================================================================
struct TerrainBatch
{
    int lodLevel = 0;
    std::vector<const TerrainPatch*> patches;
    UINT totalTriangles = 0;
};

// ============================================================================
// 用于传递给GPU的块数据（实例渲染时使用）
// ============================================================================
struct TerrainPatchInstanceData
{
    DirectX::XMFLOAT2 patchOffset;  // 块在地形中的偏移
    float morphFactor;               // Morphing因子
    float lodLevel;                  // LOD级别（作为浮点数传递以便着色器使用）
};

// ============================================================================
// 视锥体结构（用于剔除）
// ============================================================================
struct Frustum
{
    DirectX::XMFLOAT4 planes[6];  // 6个平面：左、右、上、下、近、远
    
    void ExtractFromMatrix(const DirectX::XMMATRIX& viewProj);
    bool ContainsAABB(const DirectX::BoundingBox& box) const;
};

// ============================================================================
// 地形类
// ============================================================================
class Terrain
{
public:
    Terrain();
    ~Terrain();

    // 初始化和创建
    bool CreateFromHeightmap(ID3D11Device* device, const std::wstring& heightmapPath, const TerrainParams& params);
    bool CreateProcedural(ID3D11Device* device, const TerrainParams& params);
    
    // 渲染
    void Render(ID3D11DeviceContext* context, const DirectX::XMFLOAT3& cameraPosition);
    void Render(ID3D11DeviceContext* context, const DirectX::XMFLOAT3& cameraPosition, 
                const DirectX::XMMATRIX& viewMatrix, const DirectX::XMMATRIX& projMatrix);
    void Render(ID3D11DeviceContext* context);  // 兼容旧版本
    
    // 高度查询
    float GetHeightAt(float worldX, float worldZ) const;
    
    // 配置访问
    const TerrainParams& GetParams() const { return m_params; }
    CDLODConfig& GetCDLODConfig() { return m_cdlodConfig; }
    const CDLODConfig& GetCDLODConfig() const { return m_cdlodConfig; }
    
    // 缓冲区访问
    ID3D11Buffer* GetVertexBuffer() const { return m_vertexBuffer.Get(); }
    ID3D11Buffer* GetIndexBuffer() const { return m_indexBuffer.Get(); }
    UINT GetIndexCount() const { return m_indexCount; }
    
    // LOD控制
    void SetLODLocked(bool locked) { m_lodLocked = locked; }
    bool IsLODLocked() const { return m_lodLocked; }
    void SetLockedLODLevel(int level);
    int GetLockedLODLevel() const { return m_lockedLODLevel; }
    
    // 统计信息
    struct RenderStats
    {
        int visiblePatches = 0;
        int culledPatches = 0;
        int drawCalls = 0;
        int totalTriangles = 0;
        int lodDistribution[MAX_LOD_LEVELS] = {0};
    };
    const RenderStats& GetRenderStats() const { return m_renderStats; }

private:
    // 初始化函数
    bool LoadHeightmap(const std::wstring& path, std::vector<float>& heightData);
    void GenerateTerrainMesh(const std::vector<float>& heightData);
    void CalculateNormals();
    bool CreateBuffers(ID3D11Device* device);
    
    // CDLOD核心函数
    void InitializeCDLOD(ID3D11Device* device);
    void GeneratePatches();
    void CalculatePatchBounds();
    
    // 索引生成（重构后的简化版本）
    void GenerateLODTemplates(ID3D11Device* device);
    void GenerateGridIndices(std::vector<uint32_t>& indices, int gridSize, int step);
    void GenerateBorderIndices(std::vector<uint32_t>& indices, int gridSize, 
                                int innerStep, int outerStep, int borderSide);
    
    // LOD选择（优化后）
    void UpdateLODSelection(const DirectX::XMFLOAT3& cameraPosition);
    int CalculateLODLevel(float distance) const;
    float CalculateMorphFactor(float distance, int lodLevel) const;
    void EnforceLODConstraints();  // 确保相邻块LOD差异不超过1
    
    // 视锥剔除
    void UpdateFrustum(const DirectX::XMMATRIX& viewProj);
    void PerformFrustumCulling();
    
    // 批次渲染
    void PrepareRenderBatches();
    void RenderBatches(ID3D11DeviceContext* context);
    void RenderPatch(ID3D11DeviceContext* context, const TerrainPatch& patch, int lod);

private:
    // 基础数据
    TerrainParams m_params;
    CDLODConfig m_cdlodConfig;
    
    // 顶点和索引数据
    std::vector<Vertex> m_vertices;
    std::vector<uint32_t> m_indices;
    std::vector<float> m_heightData;
    
    // DirectX资源
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_indexBuffer;
    UINT m_indexCount = 0;
    
    // CDLOD资源
    bool m_useCDLOD = false;
    bool m_lodLocked = false;
    int m_lockedLODLevel = 0;
    
    // LOD模板（每个LOD级别一个预生成的索引集）
    std::array<LODMeshTemplate, MAX_LOD_LEVELS> m_lodTemplates;
    
    // 地形块
    std::vector<TerrainPatch> m_patches;
    int m_numPatchesX = 0;
    int m_numPatchesZ = 0;
    
    // 每个块在每个LOD级别中的索引范围 [patchIndex][lodLevel] = (startIndex, indexCount)
    std::vector<std::array<std::pair<UINT, UINT>, MAX_LOD_LEVELS>> m_patchIndexRanges;
    
    // LOD距离阈值
    std::array<float, MAX_LOD_LEVELS> m_lodDistances;
    
    // 视锥体（用于剔除）
    Frustum m_frustum;
    bool m_frustumValid = false;
    
    // 渲染批次
    std::array<TerrainBatch, MAX_LOD_LEVELS> m_renderBatches;
    
    // 统计信息
    RenderStats m_renderStats;
    
    // 帧计数（用于调试输出）
    int m_frameCount = 0;
};
