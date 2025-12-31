#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <vector>
#include <string>
#include <array>

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include "Mesh.h"

// ============================================================================
// CDLOD配置参数
// ============================================================================
struct CDLODConfig
{
    int maxLODLevels = 4;
    int patchSize = 33;
    float lodDistanceMultiplier = 2.0f;
    float baseLODDistance = 50.0f;
    bool enableMorphing = true;
    float morphStartRatio = 0.7f;
    bool enableFrustumCulling = true;
    bool debugMode = false;
};

// 地形参数结构
struct TerrainParams
{
    int width = 256;
    int height = 256;
    float sizeX = 400.0f;
    float sizeZ = 400.0f;
    float heightScale = 30.0f;
    float heightOffset = 0.0f;
};

constexpr int MAX_LOD_LEVELS = 4;

// 边界方向枚举
enum class BorderSide : int
{
    Top = 0,     // -Z方向
    Bottom = 1,  // +Z方向
    Left = 2,    // -X方向
    Right = 3    // +X方向
};

// ============================================================================
// 边界缝合配置（用于确定每条边界使用哪种缝合模式）
// ============================================================================
struct StitchConfig
{
    // 每条边界是否需要缝合（true = 邻居LOD更粗，需要缝合）
    bool stitchTop = false;
    bool stitchBottom = false;
    bool stitchLeft = false;
    bool stitchRight = false;
    
    // 生成一个唯一的配置ID（0-15）
    int GetConfigId() const
    {
        return (stitchTop ? 1 : 0) | 
               (stitchBottom ? 2 : 0) | 
               (stitchLeft ? 4 : 0) | 
               (stitchRight ? 8 : 0);
    }
};

// ============================================================================
// 地形块结构
// ============================================================================
struct TerrainPatch
{
    DirectX::BoundingBox boundingBox;
    
    float centerX, centerZ;
    float minY, maxY;
    
    int patchX, patchZ;
    int startX, startZ;
    int endX, endZ;
    
    int lodLevel = 0;
    float morphFactor = 0.0f;
    float distanceToCamera = 0.0f;
    
    bool isVisible = true;
    
    // 邻居LOD级别（-1表示没有邻居，即边界）
    int neighborLOD[4] = {-1, -1, -1, -1};  // Top, Bottom, Left, Right
    
    // 当前的缝合配置
    StitchConfig stitchConfig;
};

// ============================================================================
// LOD网格模板
// ============================================================================
struct LODMeshTemplate
{
    std::vector<uint32_t> indices;
    Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer;
    UINT indexCount = 0;
    UINT triangleCount = 0;
};

// ============================================================================
// 批次渲染数据
// ============================================================================
struct TerrainBatch
{
    int lodLevel = 0;
    std::vector<const TerrainPatch*> patches;
    UINT totalTriangles = 0;
};

// ============================================================================
// 视锥体结构
// ============================================================================
struct Frustum
{
    DirectX::XMFLOAT4 planes[6];
    
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

    bool CreateFromHeightmap(ID3D11Device* device, const std::wstring& heightmapPath, const TerrainParams& params);
    bool CreateProcedural(ID3D11Device* device, const TerrainParams& params);
    
    void Render(ID3D11DeviceContext* context, const DirectX::XMFLOAT3& cameraPosition);
    void Render(ID3D11DeviceContext* context, const DirectX::XMFLOAT3& cameraPosition, 
                const DirectX::XMMATRIX& viewMatrix, const DirectX::XMMATRIX& projMatrix);
    void Render(ID3D11DeviceContext* context);
    
    float GetHeightAt(float worldX, float worldZ) const;
    
    const TerrainParams& GetParams() const { return m_params; }
    CDLODConfig& GetCDLODConfig() { return m_cdlodConfig; }
    const CDLODConfig& GetCDLODConfig() const { return m_cdlodConfig; }
    
    ID3D11Buffer* GetVertexBuffer() const { return m_vertexBuffer.Get(); }
    ID3D11Buffer* GetIndexBuffer() const { return m_indexBuffer.Get(); }
    UINT GetIndexCount() const { return m_indexCount; }
    
    void SetLODLocked(bool locked) { m_lodLocked = locked; }
    bool IsLODLocked() const { return m_lodLocked; }
    void SetLockedLODLevel(int level);
    int GetLockedLODLevel() const { return m_lockedLODLevel; }
    
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
    bool LoadHeightmap(const std::wstring& path, std::vector<float>& heightData);
    void GenerateTerrainMesh(const std::vector<float>& heightData);
    void CalculateNormals();
    bool CreateBuffers(ID3D11Device* device);
    
    void InitializeCDLOD(ID3D11Device* device);
    void GeneratePatches();
    void CalculatePatchBounds();
    
    // 索引生成
    void GenerateLODTemplates(ID3D11Device* device);
    void GeneratePatchIndicesWithStitching(ID3D11Device* device);
    
    // LOD选择
    void UpdateLODSelection(const DirectX::XMFLOAT3& cameraPosition);
    int CalculateLODLevel(float distance) const;
    float CalculateMorphFactor(float distance, int lodLevel) const;
    void EnforceLODConstraints();
    void UpdateNeighborInfo();  // 更新邻居LOD信息和缝合配置
    
    // 视锥剔除
    void UpdateFrustum(const DirectX::XMMATRIX& viewProj);
    void PerformFrustumCulling();
    
    // 渲染
    void PrepareRenderBatches();
    void RenderBatches(ID3D11DeviceContext* context);
    void RenderPatchWithStitching(ID3D11DeviceContext* context, const TerrainPatch& patch);

private:
    TerrainParams m_params;
    CDLODConfig m_cdlodConfig;
    
    std::vector<Vertex> m_vertices;
    std::vector<uint32_t> m_indices;
    std::vector<float> m_heightData;
    
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_indexBuffer;
    UINT m_indexCount = 0;
    
    bool m_useCDLOD = false;
    bool m_lodLocked = false;
    int m_lockedLODLevel = 0;
    
    // LOD模板
    std::array<LODMeshTemplate, MAX_LOD_LEVELS> m_lodTemplates;
    
    // 地形块
    std::vector<TerrainPatch> m_patches;
    int m_numPatchesX = 0;
    int m_numPatchesZ = 0;
    
    // 每个块在每个LOD级别中的索引范围 [patchIndex][lodLevel] = (startIndex, indexCount)
    std::vector<std::array<std::pair<UINT, UINT>, MAX_LOD_LEVELS>> m_patchIndexRanges;
    
    // 每个块的每种缝合配置的索引范围 [patchIndex][lodLevel][stitchConfigId] = (startIndex, indexCount)
    // stitchConfigId: 0-15，表示4条边界的缝合组合
    std::vector<std::array<std::array<std::pair<UINT, UINT>, 16>, MAX_LOD_LEVELS>> m_patchStitchRanges;
    
    std::array<float, MAX_LOD_LEVELS> m_lodDistances;
    
    Frustum m_frustum;
    bool m_frustumValid = false;
    
    std::array<TerrainBatch, MAX_LOD_LEVELS> m_renderBatches;
    
    RenderStats m_renderStats;
    int m_frameCount = 0;
};
