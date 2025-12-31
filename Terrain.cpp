// 确保在包含 Windows.h 相关头文件之前定义 NOMINMAX，避免 min/max 宏冲突
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Terrain.h"
#include "Mesh.h"

#include <fstream>
#include <algorithm>
#include <cmath>

// stb_image 用于加载高度图
#include "stb_image.h"

using namespace DirectX;

// ============================================================================
// 视锥体实现
// ============================================================================
void Frustum::ExtractFromMatrix(const XMMATRIX& viewProj)
{
    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, viewProj);
    
    // 提取6个平面（使用Gribb/Hartmann方法）
    // 左平面
    planes[0].x = m._14 + m._11;
    planes[0].y = m._24 + m._21;
    planes[0].z = m._34 + m._31;
    planes[0].w = m._44 + m._41;
    
    // 右平面
    planes[1].x = m._14 - m._11;
    planes[1].y = m._24 - m._21;
    planes[1].z = m._34 - m._31;
    planes[1].w = m._44 - m._41;
    
    // 上平面
    planes[2].x = m._14 - m._12;
    planes[2].y = m._24 - m._22;
    planes[2].z = m._34 - m._32;
    planes[2].w = m._44 - m._42;
    
    // 下平面
    planes[3].x = m._14 + m._12;
    planes[3].y = m._24 + m._22;
    planes[3].z = m._34 + m._32;
    planes[3].w = m._44 + m._42;
    
    // 近平面
    planes[4].x = m._13;
    planes[4].y = m._23;
    planes[4].z = m._33;
    planes[4].w = m._43;
    
    // 远平面
    planes[5].x = m._14 - m._13;
    planes[5].y = m._24 - m._23;
    planes[5].z = m._34 - m._33;
    planes[5].w = m._44 - m._43;
    
    // 归一化所有平面
    for (int i = 0; i < 6; ++i)
    {
        float length = sqrtf(planes[i].x * planes[i].x + 
                            planes[i].y * planes[i].y + 
                            planes[i].z * planes[i].z);
        if (length > 0.0001f)
        {
            planes[i].x /= length;
            planes[i].y /= length;
            planes[i].z /= length;
            planes[i].w /= length;
        }
    }
}

bool Frustum::ContainsAABB(const BoundingBox& box) const
{
    XMFLOAT3 corners[8];
    box.GetCorners(corners);
    
    for (int i = 0; i < 6; ++i)
    {
        int outsideCount = 0;
        
        for (int j = 0; j < 8; ++j)
        {
            float distance = planes[i].x * corners[j].x +
                           planes[i].y * corners[j].y +
                           planes[i].z * corners[j].z +
                           planes[i].w;
            
            if (distance < 0)
                ++outsideCount;
        }
        
        if (outsideCount == 8)
            return false;
    }
    
    return true;
}

// ============================================================================
// Terrain 构造和析构
// ============================================================================
Terrain::Terrain()
{
    m_params.width = 256;
    m_params.height = 256;
    m_params.sizeX = 400.0f;
    m_params.sizeZ = 400.0f;
    m_params.heightScale = 30.0f;
    m_params.heightOffset = 0.0f;
    
    for (int i = 0; i < MAX_LOD_LEVELS; ++i)
    {
        m_lodDistances[i] = m_cdlodConfig.baseLODDistance * powf(m_cdlodConfig.lodDistanceMultiplier, (float)i);
    }
}

Terrain::~Terrain()
{
}

void Terrain::SetLockedLODLevel(int level)
{
    if (level >= 0 && level < MAX_LOD_LEVELS)
    {
        m_lockedLODLevel = level;
        m_lodLocked = true;
    }
}

// ============================================================================
// 创建地形
// ============================================================================
bool Terrain::CreateFromHeightmap(ID3D11Device* device, const std::wstring& heightmapPath, const TerrainParams& params)
{
    if (!device)
    {
        OutputDebugStringW(L"[TERRAIN ERROR] Device is null\n");
        return false;
    }
    
    m_params = params;
    
    std::vector<float> heightData;
    if (!LoadHeightmap(heightmapPath, heightData))
    {
        OutputDebugStringW(L"[TERRAIN ERROR] Failed to load heightmap\n");
        return false;
    }
    
    GenerateTerrainMesh(heightData);
    
    if (!CreateBuffers(device))
    {
        OutputDebugStringW(L"[TERRAIN ERROR] Failed to create buffers\n");
        return false;
    }
    
    m_heightData = std::move(heightData);
    InitializeCDLOD(device);
    
    return true;
}

bool Terrain::CreateProcedural(ID3D11Device* device, const TerrainParams& params)
{
    if (!device)
    {
        OutputDebugStringW(L"[TERRAIN ERROR] Device is null\n");
        return false;
    }
    
    m_params = params;
    
    std::vector<float> heightData(m_params.width * m_params.height);
    
    auto hash = [](int x, int z) -> float {
        x = ((x << 13) ^ x) * 1274126177;
        z = ((z << 13) ^ z) * 1274126177;
        return ((x * z) & 0x7FFFFFFF) / 2147483647.0f;
    };
    
    auto smooth = [](float t) -> float {
        return t * t * (3.0f - 2.0f * t);
    };
    
    auto noise = [&hash, &smooth](float x, float z) -> float {
        int ix = (int)floorf(x);
        int iz = (int)floorf(z);
        float fx = x - ix;
        float fz = z - iz;
        
        float n00 = hash(ix, iz);
        float n10 = hash(ix + 1, iz);
        float n01 = hash(ix, iz + 1);
        float n11 = hash(ix + 1, iz + 1);
        
        float sx = smooth(fx);
        float sz = smooth(fz);
        
        float nx0 = n00 * (1.0f - sx) + n10 * sx;
        float nx1 = n01 * (1.0f - sx) + n11 * sx;
        
        return nx0 * (1.0f - sz) + nx1 * sz;
    };
    
    for (int z = 0; z < m_params.height; ++z)
    {
        for (int x = 0; x < m_params.width; ++x)
        {
            float fx = (float)x / (float)(m_params.width - 1);
            float fz = (float)z / (float)(m_params.height - 1);
            
            float height = noise(fx * 8.0f, fz * 8.0f) * 0.5f;
            height += noise(fx * 16.0f, fz * 16.0f) * 0.25f;
            height += noise(fx * 32.0f, fz * 32.0f) * 0.125f;
            
            height = height * 0.8f + 0.1f;
            height = fmaxf(0.0f, fminf(1.0f, height));
            
            heightData[z * m_params.width + x] = height;
        }
    }
    
    GenerateTerrainMesh(heightData);
    
    if (!CreateBuffers(device))
    {
        OutputDebugStringW(L"[TERRAIN ERROR] Failed to create buffers\n");
        return false;
    }
    
    m_heightData = std::move(heightData);
    InitializeCDLOD(device);
    
    return true;
}

// ============================================================================
// 高度图加载
// ============================================================================
bool Terrain::LoadHeightmap(const std::wstring& path, std::vector<float>& heightData)
{
    if (path.empty())
        return false;
    
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string pathA(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, &pathA[0], size_needed, nullptr, nullptr);
    
    int width, height, channels;
    unsigned char* data = stbi_load(pathA.c_str(), &width, &height, &channels, 1);
    
    if (!data)
        return false;
    
    if (width != m_params.width || height != m_params.height)
    {
        stbi_image_free(data);
        return false;
    }
    
    heightData.resize(m_params.width * m_params.height);
    for (int i = 0; i < m_params.width * m_params.height; ++i)
    {
        heightData[i] = (float)data[i] / 255.0f;
    }
    
    stbi_image_free(data);
    return true;
}

// ============================================================================
// 网格生成
// ============================================================================
void Terrain::GenerateTerrainMesh(const std::vector<float>& heightData)
{
    m_vertices.clear();
    m_indices.clear();
    
    float stepX = m_params.sizeX / (float)(m_params.width - 1);
    float stepZ = m_params.sizeZ / (float)(m_params.height - 1);
    
    for (int z = 0; z < m_params.height; ++z)
    {
        for (int x = 0; x < m_params.width; ++x)
        {
            Vertex vertex;
            
            float worldX = (float)x * stepX - m_params.sizeX * 0.5f;
            float worldZ = (float)z * stepZ - m_params.sizeZ * 0.5f;
            float worldY = heightData[z * m_params.width + x] * m_params.heightScale + m_params.heightOffset;
            
            vertex.position[0] = worldX;
            vertex.position[1] = worldY;
            vertex.position[2] = worldZ;
            
            vertex.normal[0] = 0.0f;
            vertex.normal[1] = 1.0f;
            vertex.normal[2] = 0.0f;
            
            vertex.color[0] = 1.0f;
            vertex.color[1] = 1.0f;
            vertex.color[2] = 1.0f;
            
            float texScale = 16.0f;
            vertex.texCoord[0] = (float)x / (float)(m_params.width - 1) * texScale;
            vertex.texCoord[1] = (float)z / (float)(m_params.height - 1) * texScale;
            
            m_vertices.push_back(vertex);
        }
    }
    
    for (int z = 0; z < m_params.height - 1; ++z)
    {
        for (int x = 0; x < m_params.width - 1; ++x)
        {
            uint32_t topLeft = z * m_params.width + x;
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = (z + 1) * m_params.width + x;
            uint32_t bottomRight = bottomLeft + 1;
            
            m_indices.push_back(topLeft);
            m_indices.push_back(bottomLeft);
            m_indices.push_back(topRight);
            
            m_indices.push_back(topRight);
            m_indices.push_back(bottomLeft);
            m_indices.push_back(bottomRight);
        }
    }
    
    CalculateNormals();
}

void Terrain::CalculateNormals()
{
    std::vector<XMFLOAT3> normals(m_vertices.size(), XMFLOAT3(0.0f, 0.0f, 0.0f));
    
    for (size_t i = 0; i < m_indices.size(); i += 3)
    {
        uint32_t i0 = m_indices[i];
        uint32_t i1 = m_indices[i + 1];
        uint32_t i2 = m_indices[i + 2];
        
        XMVECTOR v0 = XMVectorSet(m_vertices[i0].position[0], m_vertices[i0].position[1], m_vertices[i0].position[2], 0.0f);
        XMVECTOR v1 = XMVectorSet(m_vertices[i1].position[0], m_vertices[i1].position[1], m_vertices[i1].position[2], 0.0f);
        XMVECTOR v2 = XMVectorSet(m_vertices[i2].position[0], m_vertices[i2].position[1], m_vertices[i2].position[2], 0.0f);
        
        XMVECTOR edge1 = XMVectorSubtract(v1, v0);
        XMVECTOR edge2 = XMVectorSubtract(v2, v0);
        XMVECTOR normal = XMVector3Cross(edge1, edge2);
        normal = XMVector3Normalize(normal);
        
        XMFLOAT3 n;
        XMStoreFloat3(&n, normal);
        normals[i0].x += n.x; normals[i0].y += n.y; normals[i0].z += n.z;
        normals[i1].x += n.x; normals[i1].y += n.y; normals[i1].z += n.z;
        normals[i2].x += n.x; normals[i2].y += n.y; normals[i2].z += n.z;
    }
    
    for (size_t i = 0; i < m_vertices.size(); ++i)
    {
        XMVECTOR n = XMVectorSet(normals[i].x, normals[i].y, normals[i].z, 0.0f);
        n = XMVector3Normalize(n);
        XMFLOAT3 normalized;
        XMStoreFloat3(&normalized, n);
        
        m_vertices[i].normal[0] = normalized.x;
        m_vertices[i].normal[1] = normalized.y;
        m_vertices[i].normal[2] = normalized.z;
    }
}

bool Terrain::CreateBuffers(ID3D11Device* device)
{
    if (!device || m_vertices.empty() || m_indices.empty())
        return false;
    
    D3D11_BUFFER_DESC vbd = {};
    vbd.Usage = D3D11_USAGE_DEFAULT;
    vbd.ByteWidth = (UINT)(sizeof(Vertex) * m_vertices.size());
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    
    D3D11_SUBRESOURCE_DATA vinitData = {};
    vinitData.pSysMem = m_vertices.data();
    
    HRESULT hr = device->CreateBuffer(&vbd, &vinitData, m_vertexBuffer.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    D3D11_BUFFER_DESC ibd = {};
    ibd.Usage = D3D11_USAGE_DEFAULT;
    ibd.ByteWidth = (UINT)(sizeof(uint32_t) * m_indices.size());
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    
    D3D11_SUBRESOURCE_DATA iinitData = {};
    iinitData.pSysMem = m_indices.data();
    
    hr = device->CreateBuffer(&ibd, &iinitData, m_indexBuffer.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    m_indexCount = (UINT)m_indices.size();
    
    return true;
}

// ============================================================================
// CDLOD初始化
// ============================================================================
void Terrain::InitializeCDLOD(ID3D11Device* device)
{
    if (!device)
        return;
    
    m_useCDLOD = true;
    
    int patchSize = m_cdlodConfig.patchSize;
    float patchWorldSizeX = m_params.sizeX * (float)(patchSize - 1) / (float)(m_params.width - 1);
    float patchWorldSizeZ = m_params.sizeZ * (float)(patchSize - 1) / (float)(m_params.height - 1);
    float patchWorldSize = (patchWorldSizeX + patchWorldSizeZ) * 0.5f;
    
    m_cdlodConfig.baseLODDistance = patchWorldSize * 1.5f;
    
    for (int i = 0; i < MAX_LOD_LEVELS; ++i)
    {
        m_lodDistances[i] = m_cdlodConfig.baseLODDistance * 
                          powf(m_cdlodConfig.lodDistanceMultiplier, (float)i);
    }
    
    GeneratePatches();
    CalculatePatchBounds();
    GenerateLODTemplates(device);
    
    wchar_t msg[512];
    swprintf_s(msg, L"[TERRAIN] CDLOD initialized: %d patches (%dx%d), LOD distances: %.1f, %.1f, %.1f, %.1f\n",
               (int)m_patches.size(), m_numPatchesX, m_numPatchesZ,
               m_lodDistances[0], m_lodDistances[1], m_lodDistances[2], m_lodDistances[3]);
    OutputDebugStringW(msg);
}

// ============================================================================
// 生成地形块
// ============================================================================
void Terrain::GeneratePatches()
{
    m_patches.clear();
    
    int patchSize = m_cdlodConfig.patchSize;
    
    m_numPatchesX = (m_params.width - 1) / (patchSize - 1);
    m_numPatchesZ = (m_params.height - 1) / (patchSize - 1);
    
    if ((m_params.width - 1) % (patchSize - 1) != 0)
        m_numPatchesX++;
    if ((m_params.height - 1) % (patchSize - 1) != 0)
        m_numPatchesZ++;
    
    float patchWorldSizeX = m_params.sizeX / (float)m_numPatchesX;
    float patchWorldSizeZ = m_params.sizeZ / (float)m_numPatchesZ;
    
    m_patches.reserve(m_numPatchesX * m_numPatchesZ);
    
    for (int pz = 0; pz < m_numPatchesZ; ++pz)
    {
        for (int px = 0; px < m_numPatchesX; ++px)
        {
            TerrainPatch patch;
            
            patch.patchX = px;
            patch.patchZ = pz;
            
            patch.startX = px * (patchSize - 1);
            patch.startZ = pz * (patchSize - 1);
            patch.endX = std::min(patch.startX + (patchSize - 1), m_params.width - 1);
            patch.endZ = std::min(patch.startZ + (patchSize - 1), m_params.height - 1);
            
            float minX = -m_params.sizeX * 0.5f + px * patchWorldSizeX;
            float maxX = minX + patchWorldSizeX;
            float minZ = -m_params.sizeZ * 0.5f + pz * patchWorldSizeZ;
            float maxZ = minZ + patchWorldSizeZ;
            
            patch.centerX = (minX + maxX) * 0.5f;
            patch.centerZ = (minZ + maxZ) * 0.5f;
            
            patch.minY = 0.0f;
            patch.maxY = m_params.heightScale;
            
            XMFLOAT3 center((minX + maxX) * 0.5f, (patch.minY + patch.maxY) * 0.5f, (minZ + maxZ) * 0.5f);
            XMFLOAT3 extents((maxX - minX) * 0.5f, (patch.maxY - patch.minY) * 0.5f, (maxZ - minZ) * 0.5f);
            patch.boundingBox = BoundingBox(center, extents);
            
            m_patches.push_back(patch);
        }
    }
}

void Terrain::CalculatePatchBounds()
{
    for (auto& patch : m_patches)
    {
        float minY = FLT_MAX;
        float maxY = -FLT_MAX;
        
        for (int z = patch.startZ; z <= patch.endZ; ++z)
        {
            for (int x = patch.startX; x <= patch.endX; ++x)
            {
                if (x >= 0 && x < m_params.width && z >= 0 && z < m_params.height)
                {
                    float height = m_heightData[z * m_params.width + x] * m_params.heightScale + m_params.heightOffset;
                    minY = std::min(minY, height);
                    maxY = std::max(maxY, height);
                }
            }
        }
        
        minY -= 1.0f;
        maxY += 1.0f;
        
        patch.minY = minY;
        patch.maxY = maxY;
        
        float minX = -m_params.sizeX * 0.5f + patch.patchX * (m_params.sizeX / m_numPatchesX);
        float maxX = minX + (m_params.sizeX / m_numPatchesX);
        float minZ = -m_params.sizeZ * 0.5f + patch.patchZ * (m_params.sizeZ / m_numPatchesZ);
        float maxZ = minZ + (m_params.sizeZ / m_numPatchesZ);
        
        XMFLOAT3 center((minX + maxX) * 0.5f, (minY + maxY) * 0.5f, (minZ + maxZ) * 0.5f);
        XMFLOAT3 extents((maxX - minX) * 0.5f, (maxY - minY) * 0.5f, (maxZ - minZ) * 0.5f);
        patch.boundingBox = BoundingBox(center, extents);
    }
}

// ============================================================================
// 生成LOD索引（为每个块的每个LOD级别生成正确的索引）
// ============================================================================
void Terrain::GenerateLODTemplates(ID3D11Device* device)
{
    if (!device || m_vertices.empty() || m_patches.empty())
        return;
    
    // 为每个LOD级别生成所有块的索引
    std::array<std::vector<uint32_t>, MAX_LOD_LEVELS> allLODIndices;
    
    // 存储每个块在每个LOD级别中的索引范围
    struct PatchIndexRange
    {
        UINT indexStart;
        UINT indexCount;
    };
    std::vector<std::array<PatchIndexRange, MAX_LOD_LEVELS>> patchRanges(m_patches.size());
    
    for (size_t patchIdx = 0; patchIdx < m_patches.size(); ++patchIdx)
    {
        const auto& patch = m_patches[patchIdx];
        
        for (int lod = 0; lod < MAX_LOD_LEVELS; ++lod)
        {
            int step = 1 << lod;
            
            // 记录起始位置
            patchRanges[patchIdx][lod].indexStart = (UINT)allLODIndices[lod].size();
            
            // 为这个块生成索引（使用全局顶点索引）
            for (int z = patch.startZ; z < patch.endZ; z += step)
            {
                int nextZ = std::min(z + step, patch.endZ);
                if (nextZ == z) continue;
                
                for (int x = patch.startX; x < patch.endX; x += step)
                {
                    int nextX = std::min(x + step, patch.endX);
                    if (nextX == x) continue;
                    
                    // 计算四个顶点的全局索引
                    uint32_t topLeft = z * m_params.width + x;
                    uint32_t topRight = z * m_params.width + nextX;
                    uint32_t bottomLeft = nextZ * m_params.width + x;
                    uint32_t bottomRight = nextZ * m_params.width + nextX;
                    
                    // 第一个三角形
                    allLODIndices[lod].push_back(topLeft);
                    allLODIndices[lod].push_back(bottomLeft);
                    allLODIndices[lod].push_back(topRight);
                    
                    // 第二个三角形
                    allLODIndices[lod].push_back(topRight);
                    allLODIndices[lod].push_back(bottomLeft);
                    allLODIndices[lod].push_back(bottomRight);
                }
            }
            
            // 记录索引数量
            patchRanges[patchIdx][lod].indexCount = 
                (UINT)allLODIndices[lod].size() - patchRanges[patchIdx][lod].indexStart;
        }
    }
    
    // 为每个LOD级别创建索引缓冲区
    for (int lod = 0; lod < MAX_LOD_LEVELS; ++lod)
    {
        if (allLODIndices[lod].empty())
            continue;
        
        D3D11_BUFFER_DESC ibd = {};
        ibd.Usage = D3D11_USAGE_DEFAULT;
        ibd.ByteWidth = (UINT)(sizeof(uint32_t) * allLODIndices[lod].size());
        ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        
        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = allLODIndices[lod].data();
        
        HRESULT hr = device->CreateBuffer(&ibd, &initData, m_lodTemplates[lod].indexBuffer.GetAddressOf());
        if (SUCCEEDED(hr))
        {
            m_lodTemplates[lod].indices = std::move(allLODIndices[lod]);
            m_lodTemplates[lod].indexCount = (UINT)m_lodTemplates[lod].indices.size();
            m_lodTemplates[lod].triangleCount = m_lodTemplates[lod].indexCount / 3;
            
            wchar_t msg[256];
            swprintf_s(msg, L"[TERRAIN] LOD %d: %d triangles (step=%d)\n", 
                       lod, m_lodTemplates[lod].triangleCount, 1 << lod);
            OutputDebugStringW(msg);
        }
    }
    
    // 将块的索引范围保存到一个单独的结构中（用于渲染时快速查找）
    // 这里我们需要扩展TerrainPatch结构或使用另一种方式存储
    // 为了简化，我们将范围信息编码到一个辅助数组中
    m_patchIndexRanges.resize(m_patches.size());
    for (size_t i = 0; i < m_patches.size(); ++i)
    {
        for (int lod = 0; lod < MAX_LOD_LEVELS; ++lod)
        {
            m_patchIndexRanges[i][lod].first = patchRanges[i][lod].indexStart;
            m_patchIndexRanges[i][lod].second = patchRanges[i][lod].indexCount;
        }
    }
}

// 生成网格索引的辅助函数（用于单个块）- 这个函数现在不再使用，保留作为参考
void Terrain::GenerateGridIndices(std::vector<uint32_t>& indices, int gridSize, int step)
{
    // 不再使用此函数
    indices.clear();
}

// ============================================================================
// LOD选择和更新
// ============================================================================
void Terrain::UpdateLODSelection(const XMFLOAT3& cameraPosition)
{
    m_renderStats = RenderStats();
    
    for (auto& patch : m_patches)
    {
        float dx = patch.centerX - cameraPosition.x;
        float dz = patch.centerZ - cameraPosition.z;
        float patchHeight = (patch.minY + patch.maxY) * 0.5f;
        float dy = patchHeight - cameraPosition.y;
        
        patch.distanceToCamera = sqrtf(dx * dx + dy * dy + dz * dz);
        
        if (m_lodLocked)
        {
            patch.lodLevel = m_lockedLODLevel;
            patch.morphFactor = 0.0f;
        }
        else
        {
            patch.lodLevel = CalculateLODLevel(patch.distanceToCamera);
            
            if (m_cdlodConfig.enableMorphing)
            {
                patch.morphFactor = CalculateMorphFactor(patch.distanceToCamera, patch.lodLevel);
            }
            else
            {
                patch.morphFactor = 0.0f;
            }
        }
        
        patch.isVisible = true;
    }
    
    EnforceLODConstraints();
}

int Terrain::CalculateLODLevel(float distance) const
{
    for (int i = 0; i < MAX_LOD_LEVELS; ++i)
    {
        if (distance <= m_lodDistances[i])
            return i;
    }
    return MAX_LOD_LEVELS - 1;
}

float Terrain::CalculateMorphFactor(float distance, int lodLevel) const
{
    if (lodLevel >= MAX_LOD_LEVELS - 1)
        return 0.0f;
    
    float lodStart = (lodLevel == 0) ? 0.0f : m_lodDistances[lodLevel - 1];
    float lodEnd = m_lodDistances[lodLevel];
    float morphStart = lodStart + (lodEnd - lodStart) * m_cdlodConfig.morphStartRatio;
    
    if (distance <= morphStart)
        return 0.0f;
    if (distance >= lodEnd)
        return 1.0f;
    
    return (distance - morphStart) / (lodEnd - morphStart);
}

void Terrain::EnforceLODConstraints()
{
    bool changed = true;
    int iterations = 0;
    const int maxIterations = 10;
    
    while (changed && iterations < maxIterations)
    {
        changed = false;
        iterations++;
        
        for (int z = 0; z < m_numPatchesZ; ++z)
        {
            for (int x = 0; x < m_numPatchesX; ++x)
            {
                int idx = z * m_numPatchesX + x;
                TerrainPatch& patch = m_patches[idx];
                
                int minNeighborLOD = MAX_LOD_LEVELS;
                
                const int dx[] = {-1, 1, 0, 0};
                const int dz[] = {0, 0, -1, 1};
                
                for (int i = 0; i < 4; ++i)
                {
                    int nx = x + dx[i];
                    int nz = z + dz[i];
                    
                    if (nx >= 0 && nx < m_numPatchesX && nz >= 0 && nz < m_numPatchesZ)
                    {
                        int neighborIdx = nz * m_numPatchesX + nx;
                        minNeighborLOD = std::min(minNeighborLOD, m_patches[neighborIdx].lodLevel);
                    }
                }
                
                if (patch.lodLevel > minNeighborLOD + 1)
                {
                    patch.lodLevel = minNeighborLOD + 1;
                    changed = true;
                }
            }
        }
    }
}

// ============================================================================
// 视锥剔除
// ============================================================================
void Terrain::UpdateFrustum(const XMMATRIX& viewProj)
{
    m_frustum.ExtractFromMatrix(viewProj);
    m_frustumValid = true;
}

void Terrain::PerformFrustumCulling()
{
    if (!m_cdlodConfig.enableFrustumCulling || !m_frustumValid)
    {
        for (auto& patch : m_patches)
        {
            patch.isVisible = true;
        }
        return;
    }
    
    for (auto& patch : m_patches)
    {
        patch.isVisible = m_frustum.ContainsAABB(patch.boundingBox);
        
        if (!patch.isVisible)
        {
            m_renderStats.culledPatches++;
        }
    }
}

// ============================================================================
// 批次渲染准备
// ============================================================================
void Terrain::PrepareRenderBatches()
{
    for (auto& batch : m_renderBatches)
    {
        batch.patches.clear();
        batch.totalTriangles = 0;
    }
    
    for (size_t i = 0; i < m_patches.size(); ++i)
    {
        const auto& patch = m_patches[i];
        if (!patch.isVisible)
            continue;
        
        int lod = patch.lodLevel;
        if (lod >= 0 && lod < MAX_LOD_LEVELS && !m_patchIndexRanges.empty())
        {
            UINT indexCount = m_patchIndexRanges[i][lod].second;
            if (indexCount > 0)
            {
                m_renderBatches[lod].lodLevel = lod;
                m_renderBatches[lod].patches.push_back(&patch);
                m_renderBatches[lod].totalTriangles += indexCount / 3;
                
                m_renderStats.visiblePatches++;
                m_renderStats.totalTriangles += indexCount / 3;
                m_renderStats.lodDistribution[lod]++;
            }
        }
    }
}

// ============================================================================
// 渲染
// ============================================================================
void Terrain::Render(ID3D11DeviceContext* context)
{
    if (!context || !m_vertexBuffer || !m_indexBuffer)
        return;
    
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->DrawIndexed(m_indexCount, 0, 0);
}

void Terrain::Render(ID3D11DeviceContext* context, const XMFLOAT3& cameraPosition)
{
    if (!m_useCDLOD)
    {
        Render(context);
        return;
    }
    
    if (!context || !m_vertexBuffer)
        return;
    
    UpdateLODSelection(cameraPosition);
    
    if (m_frustumValid)
    {
        PerformFrustumCulling();
    }
    
    PrepareRenderBatches();
    RenderBatches(context);
    
    m_frameCount++;
}

void Terrain::Render(ID3D11DeviceContext* context, const XMFLOAT3& cameraPosition,
                     const XMMATRIX& viewMatrix, const XMMATRIX& projMatrix)
{
    if (!m_useCDLOD)
    {
        Render(context);
        return;
    }
    
    if (!context || !m_vertexBuffer)
        return;
    
    XMMATRIX viewProj = XMMatrixMultiply(viewMatrix, projMatrix);
    UpdateFrustum(viewProj);
    
    UpdateLODSelection(cameraPosition);
    PerformFrustumCulling();
    PrepareRenderBatches();
    RenderBatches(context);
    
    m_frameCount++;
    
    if (m_cdlodConfig.debugMode && m_frameCount % 60 == 0)
    {
        wchar_t msg[512];
        swprintf_s(msg, L"[TERRAIN] Visible: %d, Culled: %d, DrawCalls: %d, Triangles: %d, LOD[0-3]: %d,%d,%d,%d\n",
                   m_renderStats.visiblePatches, m_renderStats.culledPatches,
                   m_renderStats.drawCalls, m_renderStats.totalTriangles,
                   m_renderStats.lodDistribution[0], m_renderStats.lodDistribution[1],
                   m_renderStats.lodDistribution[2], m_renderStats.lodDistribution[3]);
        OutputDebugStringW(msg);
    }
}

void Terrain::RenderBatches(ID3D11DeviceContext* context)
{
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    
    // 调试输出（仅第一次）
    static bool firstRender = true;
    if (firstRender)
    {
        wchar_t msg[512];
        swprintf_s(msg, L"[TERRAIN DEBUG] RenderBatches: %d patches, %d LOD templates\n",
                   (int)m_patches.size(), MAX_LOD_LEVELS);
        OutputDebugStringW(msg);
        
        for (int lod = 0; lod < MAX_LOD_LEVELS; ++lod)
        {
            swprintf_s(msg, L"[TERRAIN DEBUG] LOD %d: indexBuffer=%p, indexCount=%d\n",
                       lod, m_lodTemplates[lod].indexBuffer.Get(), m_lodTemplates[lod].indexCount);
            OutputDebugStringW(msg);
        }
        firstRender = false;
    }
    
    for (int lod = 0; lod < MAX_LOD_LEVELS; ++lod)
    {
        const auto& batch = m_renderBatches[lod];
        if (batch.patches.empty() || !m_lodTemplates[lod].indexBuffer)
            continue;
        
        context->IASetIndexBuffer(m_lodTemplates[lod].indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        
        for (const auto* patch : batch.patches)
        {
            int patchIdx = patch->patchZ * m_numPatchesX + patch->patchX;
            if (patchIdx >= 0 && patchIdx < (int)m_patchIndexRanges.size())
            {
                UINT indexStart = m_patchIndexRanges[patchIdx][lod].first;
                UINT indexCount = m_patchIndexRanges[patchIdx][lod].second;
                
                if (indexCount > 0)
                {
                    context->DrawIndexed(indexCount, indexStart, 0);
                    m_renderStats.drawCalls++;
                }
            }
        }
    }
}

void Terrain::RenderPatch(ID3D11DeviceContext* context, const TerrainPatch& patch, int lod)
{
    // 这个函数现在不再使用，渲染在RenderBatches中完成
}

// ============================================================================
// 高度查询
// ============================================================================
float Terrain::GetHeightAt(float worldX, float worldZ) const
{
    if (m_heightData.empty())
        return 0.0f;
    
    float localX = worldX + m_params.sizeX * 0.5f;
    float localZ = worldZ + m_params.sizeZ * 0.5f;
    
    float fx = localX / m_params.sizeX * (m_params.width - 1);
    float fz = localZ / m_params.sizeZ * (m_params.height - 1);
    
    if (fx < 0.0f || fx >= m_params.width - 1 || fz < 0.0f || fz >= m_params.height - 1)
        return 0.0f;
    
    int x0 = (int)fx;
    int z0 = (int)fz;
    int x1 = std::min(x0 + 1, m_params.width - 1);
    int z1 = std::min(z0 + 1, m_params.height - 1);
    
    float fx_frac = fx - x0;
    float fz_frac = fz - z0;
    
    float h00 = m_heightData[z0 * m_params.width + x0];
    float h10 = m_heightData[z0 * m_params.width + x1];
    float h01 = m_heightData[z1 * m_params.width + x0];
    float h11 = m_heightData[z1 * m_params.width + x1];
    
    float h0 = h00 * (1.0f - fx_frac) + h10 * fx_frac;
    float h1 = h01 * (1.0f - fx_frac) + h11 * fx_frac;
    float height = h0 * (1.0f - fz_frac) + h1 * fz_frac;
    
    return height * m_params.heightScale + m_params.heightOffset;
}
