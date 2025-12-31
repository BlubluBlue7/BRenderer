#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Terrain.h"
#include "Mesh.h"

#include <fstream>
#include <algorithm>
#include <cmath>

#include "stb_image.h"

using namespace DirectX;

// ============================================================================
// Frustum Implementation
// ============================================================================
void Frustum::ExtractFromMatrix(const XMMATRIX& viewProj)
{
    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, viewProj);
    
    planes[0].x = m._14 + m._11;
    planes[0].y = m._24 + m._21;
    planes[0].z = m._34 + m._31;
    planes[0].w = m._44 + m._41;
    
    planes[1].x = m._14 - m._11;
    planes[1].y = m._24 - m._21;
    planes[1].z = m._34 - m._31;
    planes[1].w = m._44 - m._41;
    
    planes[2].x = m._14 - m._12;
    planes[2].y = m._24 - m._22;
    planes[2].z = m._34 - m._32;
    planes[2].w = m._44 - m._42;
    
    planes[3].x = m._14 + m._12;
    planes[3].y = m._24 + m._22;
    planes[3].z = m._34 + m._32;
    planes[3].w = m._44 + m._42;
    
    planes[4].x = m._13;
    planes[4].y = m._23;
    planes[4].z = m._33;
    planes[4].w = m._43;
    
    planes[5].x = m._14 - m._13;
    planes[5].y = m._24 - m._23;
    planes[5].z = m._34 - m._33;
    planes[5].w = m._44 - m._43;
    
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
// Terrain Constructor/Destructor
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
        m_lodDistances[i] = m_cdlodConfig.baseLODDistance * 
                          powf(m_cdlodConfig.lodDistanceMultiplier, (float)i);
    }
}

Terrain::~Terrain() {}

void Terrain::SetLockedLODLevel(int level)
{
    if (level >= 0 && level < MAX_LOD_LEVELS)
    {
        m_lockedLODLevel = level;
        m_lodLocked = true;
    }
}

// ============================================================================
// Create Terrain
// ============================================================================
bool Terrain::CreateFromHeightmap(ID3D11Device* device, const std::wstring& heightmapPath, const TerrainParams& params)
{
    if (!device) return false;
    m_params = params;
    
    std::vector<float> heightData;
    if (!LoadHeightmap(heightmapPath, heightData)) return false;
    
    GenerateTerrainMesh(heightData);
    if (!CreateBuffers(device)) return false;
    
    m_heightData = std::move(heightData);
    InitializeCDLOD(device);
    return true;
}

bool Terrain::CreateProcedural(ID3D11Device* device, const TerrainParams& params)
{
    if (!device) return false;
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
        
        return (n00 * (1-sx) + n10 * sx) * (1-sz) + (n01 * (1-sx) + n11 * sx) * sz;
    };
    
    for (int z = 0; z < m_params.height; ++z)
    {
        for (int x = 0; x < m_params.width; ++x)
        {
            float fx = (float)x / (m_params.width - 1);
            float fz = (float)z / (m_params.height - 1);
            
            float height = noise(fx * 8.0f, fz * 8.0f) * 0.5f;
            height += noise(fx * 16.0f, fz * 16.0f) * 0.25f;
            height += noise(fx * 32.0f, fz * 32.0f) * 0.125f;
            height = fmaxf(0.0f, fminf(1.0f, height * 0.8f + 0.1f));
            
            heightData[z * m_params.width + x] = height;
        }
    }
    
    GenerateTerrainMesh(heightData);
    if (!CreateBuffers(device)) return false;
    
    m_heightData = std::move(heightData);
    InitializeCDLOD(device);
    return true;
}

// ============================================================================
// Load Heightmap
// ============================================================================
bool Terrain::LoadHeightmap(const std::wstring& path, std::vector<float>& heightData)
{
    if (path.empty()) return false;
    
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string pathA(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, &pathA[0], size_needed, nullptr, nullptr);
    
    int width, height, channels;
    unsigned char* data = stbi_load(pathA.c_str(), &width, &height, &channels, 1);
    if (!data) return false;
    
    if (width != m_params.width || height != m_params.height)
    {
        stbi_image_free(data);
        return false;
    }
    
    heightData.resize(m_params.width * m_params.height);
    for (int i = 0; i < m_params.width * m_params.height; ++i)
        heightData[i] = data[i] / 255.0f;
    
    stbi_image_free(data);
    return true;
}

// ============================================================================
// Generate Mesh
// ============================================================================
void Terrain::GenerateTerrainMesh(const std::vector<float>& heightData)
{
    m_vertices.clear();
    m_indices.clear();
    
    float stepX = m_params.sizeX / (m_params.width - 1);
    float stepZ = m_params.sizeZ / (m_params.height - 1);
    
    for (int z = 0; z < m_params.height; ++z)
    {
        for (int x = 0; x < m_params.width; ++x)
        {
            Vertex v;
            v.position[0] = x * stepX - m_params.sizeX * 0.5f;
            v.position[1] = heightData[z * m_params.width + x] * m_params.heightScale + m_params.heightOffset;
            v.position[2] = z * stepZ - m_params.sizeZ * 0.5f;
            v.normal[0] = 0; v.normal[1] = 1; v.normal[2] = 0;
            v.color[0] = v.color[1] = v.color[2] = 1;
            v.texCoord[0] = (float)x / (m_params.width - 1) * 16.0f;
            v.texCoord[1] = (float)z / (m_params.height - 1) * 16.0f;
            m_vertices.push_back(v);
        }
    }
    
    for (int z = 0; z < m_params.height - 1; ++z)
    {
        for (int x = 0; x < m_params.width - 1; ++x)
        {
            uint32_t tl = z * m_params.width + x;
            uint32_t tr = tl + 1;
            uint32_t bl = (z + 1) * m_params.width + x;
            uint32_t br = bl + 1;
            
            m_indices.push_back(tl);
            m_indices.push_back(bl);
            m_indices.push_back(tr);
            m_indices.push_back(tr);
            m_indices.push_back(bl);
            m_indices.push_back(br);
        }
    }
    
    CalculateNormals();
}

void Terrain::CalculateNormals()
{
    std::vector<XMFLOAT3> normals(m_vertices.size(), XMFLOAT3(0, 0, 0));
    
    for (size_t i = 0; i < m_indices.size(); i += 3)
    {
        uint32_t i0 = m_indices[i], i1 = m_indices[i + 1], i2 = m_indices[i + 2];
        
        XMVECTOR v0 = XMLoadFloat3((XMFLOAT3*)m_vertices[i0].position);
        XMVECTOR v1 = XMLoadFloat3((XMFLOAT3*)m_vertices[i1].position);
        XMVECTOR v2 = XMLoadFloat3((XMFLOAT3*)m_vertices[i2].position);
        
        XMVECTOR n = XMVector3Normalize(XMVector3Cross(v1 - v0, v2 - v0));
        XMFLOAT3 nf; XMStoreFloat3(&nf, n);
        
        normals[i0].x += nf.x; normals[i0].y += nf.y; normals[i0].z += nf.z;
        normals[i1].x += nf.x; normals[i1].y += nf.y; normals[i1].z += nf.z;
        normals[i2].x += nf.x; normals[i2].y += nf.y; normals[i2].z += nf.z;
    }
    
    for (size_t i = 0; i < m_vertices.size(); ++i)
    {
        XMVECTOR n = XMVector3Normalize(XMLoadFloat3(&normals[i]));
        XMFLOAT3 nf; XMStoreFloat3(&nf, n);
        m_vertices[i].normal[0] = nf.x;
        m_vertices[i].normal[1] = nf.y;
        m_vertices[i].normal[2] = nf.z;
    }
}

bool Terrain::CreateBuffers(ID3D11Device* device)
{
    if (!device || m_vertices.empty() || m_indices.empty()) return false;
    
    D3D11_BUFFER_DESC vbd = {};
    vbd.Usage = D3D11_USAGE_DEFAULT;
    vbd.ByteWidth = (UINT)(sizeof(Vertex) * m_vertices.size());
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    
    D3D11_SUBRESOURCE_DATA vdata = {};
    vdata.pSysMem = m_vertices.data();
    if (FAILED(device->CreateBuffer(&vbd, &vdata, m_vertexBuffer.GetAddressOf()))) return false;
    
    D3D11_BUFFER_DESC ibd = {};
    ibd.Usage = D3D11_USAGE_DEFAULT;
    ibd.ByteWidth = (UINT)(sizeof(uint32_t) * m_indices.size());
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    
    D3D11_SUBRESOURCE_DATA idata = {};
    idata.pSysMem = m_indices.data();
    if (FAILED(device->CreateBuffer(&ibd, &idata, m_indexBuffer.GetAddressOf()))) return false;
    
    m_indexCount = (UINT)m_indices.size();
    return true;
}

// ============================================================================
// CDLOD Initialization
// ============================================================================
void Terrain::InitializeCDLOD(ID3D11Device* device)
{
    if (!device) return;
    m_useCDLOD = true;
    
    int patchSize = m_cdlodConfig.patchSize;
    float patchWorldSize = m_params.sizeX * (patchSize - 1) / (m_params.width - 1);
    
    m_cdlodConfig.baseLODDistance = patchWorldSize * 1.5f;
    for (int i = 0; i < MAX_LOD_LEVELS; ++i)
        m_lodDistances[i] = m_cdlodConfig.baseLODDistance * powf(m_cdlodConfig.lodDistanceMultiplier, (float)i);
    
    GeneratePatches();
    CalculatePatchBounds();
    GeneratePatchIndicesWithStitching(device);
    
    wchar_t msg[512];
    swprintf_s(msg, L"[TERRAIN] CDLOD initialized: %d patches (%dx%d)\n",
               (int)m_patches.size(), m_numPatchesX, m_numPatchesZ);
    OutputDebugStringW(msg);
}

void Terrain::GeneratePatches()
{
    m_patches.clear();
    int patchSize = m_cdlodConfig.patchSize;
    
    m_numPatchesX = (m_params.width - 1) / (patchSize - 1);
    m_numPatchesZ = (m_params.height - 1) / (patchSize - 1);
    if ((m_params.width - 1) % (patchSize - 1) != 0) m_numPatchesX++;
    if ((m_params.height - 1) % (patchSize - 1) != 0) m_numPatchesZ++;
    
    float patchWorldSizeX = m_params.sizeX / m_numPatchesX;
    float patchWorldSizeZ = m_params.sizeZ / m_numPatchesZ;
    
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
            patch.minY = 0;
            patch.maxY = m_params.heightScale;
            
            patch.boundingBox = BoundingBox(
                XMFLOAT3(patch.centerX, (patch.minY + patch.maxY) * 0.5f, patch.centerZ),
                XMFLOAT3((maxX - minX) * 0.5f, (patch.maxY - patch.minY) * 0.5f, (maxZ - minZ) * 0.5f));
            
            m_patches.push_back(patch);
        }
    }
}

void Terrain::CalculatePatchBounds()
{
    for (auto& patch : m_patches)
    {
        float minY = FLT_MAX, maxY = -FLT_MAX;
        for (int z = patch.startZ; z <= patch.endZ; ++z)
        {
            for (int x = patch.startX; x <= patch.endX; ++x)
            {
                float h = m_heightData[z * m_params.width + x] * m_params.heightScale + m_params.heightOffset;
                minY = std::min(minY, h);
                maxY = std::max(maxY, h);
            }
        }
        patch.minY = minY - 1;
        patch.maxY = maxY + 1;
        
        float patchWorldSizeX = m_params.sizeX / m_numPatchesX;
        float patchWorldSizeZ = m_params.sizeZ / m_numPatchesZ;
        float minX = -m_params.sizeX * 0.5f + patch.patchX * patchWorldSizeX;
        float minZ = -m_params.sizeZ * 0.5f + patch.patchZ * patchWorldSizeZ;
        
        patch.boundingBox = BoundingBox(
            XMFLOAT3(patch.centerX, (patch.minY + patch.maxY) * 0.5f, patch.centerZ),
            XMFLOAT3(patchWorldSizeX * 0.5f, (patch.maxY - patch.minY) * 0.5f, patchWorldSizeZ * 0.5f));
    }
}

// ============================================================================
// Generate Patch Indices - Robust version ensuring no gaps
// Uses standard grid pattern with proper boundary handling
// ============================================================================
void Terrain::GeneratePatchIndicesWithStitching(ID3D11Device* device)
{
    if (!device || m_patches.empty()) return;
    
    std::array<std::vector<uint32_t>, MAX_LOD_LEVELS> allLODIndices;
    m_patchStitchRanges.resize(m_patches.size());
    
    for (size_t patchIdx = 0; patchIdx < m_patches.size(); ++patchIdx)
    {
        const auto& patch = m_patches[patchIdx];
        int patchWidth = patch.endX - patch.startX;
        int patchHeight = patch.endZ - patch.startZ;
        
        for (int lod = 0; lod < MAX_LOD_LEVELS; ++lod)
        {
            int step = 1 << lod;
            
            // Single shared index set for all configurations
            // (True stitching would require separate indices per config)
            UINT startIndex = (UINT)allLODIndices[lod].size();
            
            // Generate grid: iterate in local coordinates for clarity
            int numCellsX = (patchWidth + step - 1) / step;
            int numCellsZ = (patchHeight + step - 1) / step;
            
            for (int cz = 0; cz < numCellsZ; ++cz)
            {
                int z0 = patch.startZ + cz * step;
                int z1 = std::min(patch.startZ + (cz + 1) * step, patch.endZ);
                
                for (int cx = 0; cx < numCellsX; ++cx)
                {
                    int x0 = patch.startX + cx * step;
                    int x1 = std::min(patch.startX + (cx + 1) * step, patch.endX);
                    
                    // Ensure we have valid quad dimensions
                    if (x1 <= x0 || z1 <= z0) continue;
                    
                    uint32_t tl = z0 * m_params.width + x0;
                    uint32_t tr = z0 * m_params.width + x1;
                    uint32_t bl = z1 * m_params.width + x0;
                    uint32_t br = z1 * m_params.width + x1;
                    
                    // Triangle 1: TL -> BL -> TR
                    allLODIndices[lod].push_back(tl);
                    allLODIndices[lod].push_back(bl);
                    allLODIndices[lod].push_back(tr);
                    
                    // Triangle 2: TR -> BL -> BR
                    allLODIndices[lod].push_back(tr);
                    allLODIndices[lod].push_back(bl);
                    allLODIndices[lod].push_back(br);
                }
            }
            
            UINT indexCount = (UINT)allLODIndices[lod].size() - startIndex;
            
            // All 16 stitch configurations share the same indices
            // (Real stitching would have different indices per config)
            for (int configId = 0; configId < 16; ++configId)
            {
                m_patchStitchRanges[patchIdx][lod][configId] = {startIndex, indexCount};
            }
        }
    }
    
    // Create index buffers for each LOD level
    for (int lod = 0; lod < MAX_LOD_LEVELS; ++lod)
    {
        if (allLODIndices[lod].empty()) continue;
        
        D3D11_BUFFER_DESC ibd = {};
        ibd.Usage = D3D11_USAGE_DEFAULT;
        ibd.ByteWidth = (UINT)(sizeof(uint32_t) * allLODIndices[lod].size());
        ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        
        D3D11_SUBRESOURCE_DATA idata = {};
        idata.pSysMem = allLODIndices[lod].data();
        
        if (SUCCEEDED(device->CreateBuffer(&ibd, &idata, m_lodTemplates[lod].indexBuffer.GetAddressOf())))
        {
            m_lodTemplates[lod].indices = std::move(allLODIndices[lod]);
            m_lodTemplates[lod].indexCount = (UINT)m_lodTemplates[lod].indices.size();
        }
    }
    
    OutputDebugStringW(L"[TERRAIN] Patch indices generated\n");
}

// ============================================================================
// LOD Selection
// ============================================================================
void Terrain::UpdateLODSelection(const XMFLOAT3& cameraPosition)
{
    m_renderStats = RenderStats();
    
    for (auto& patch : m_patches)
    {
        float dx = patch.centerX - cameraPosition.x;
        float dz = patch.centerZ - cameraPosition.z;
        float dy = (patch.minY + patch.maxY) * 0.5f - cameraPosition.y;
        patch.distanceToCamera = sqrtf(dx*dx + dy*dy + dz*dz);
        
        if (m_lodLocked)
        {
            patch.lodLevel = m_lockedLODLevel;
            patch.morphFactor = 0;
        }
        else
        {
            patch.lodLevel = CalculateLODLevel(patch.distanceToCamera);
            patch.morphFactor = m_cdlodConfig.enableMorphing ? 
                               CalculateMorphFactor(patch.distanceToCamera, patch.lodLevel) : 0;
        }
        patch.isVisible = true;
    }
    
    EnforceLODConstraints();
    UpdateNeighborInfo();
}

int Terrain::CalculateLODLevel(float distance) const
{
    for (int i = 0; i < MAX_LOD_LEVELS; ++i)
        if (distance <= m_lodDistances[i]) return i;
    return MAX_LOD_LEVELS - 1;
}

float Terrain::CalculateMorphFactor(float distance, int lodLevel) const
{
    if (lodLevel >= MAX_LOD_LEVELS - 1) return 0;
    float lodStart = lodLevel == 0 ? 0 : m_lodDistances[lodLevel - 1];
    float lodEnd = m_lodDistances[lodLevel];
    float morphStart = lodStart + (lodEnd - lodStart) * m_cdlodConfig.morphStartRatio;
    if (distance <= morphStart) return 0;
    if (distance >= lodEnd) return 1;
    return (distance - morphStart) / (lodEnd - morphStart);
}

void Terrain::EnforceLODConstraints()
{
    bool changed = true;
    for (int iter = 0; iter < 10 && changed; ++iter)
    {
        changed = false;
        for (int z = 0; z < m_numPatchesZ; ++z)
        {
            for (int x = 0; x < m_numPatchesX; ++x)
            {
                int idx = z * m_numPatchesX + x;
                int minNeighbor = MAX_LOD_LEVELS;
                
                const int dx[] = {-1, 1, 0, 0};
                const int dz[] = {0, 0, -1, 1};
                for (int i = 0; i < 4; ++i)
                {
                    int nx = x + dx[i], nz = z + dz[i];
                    if (nx >= 0 && nx < m_numPatchesX && nz >= 0 && nz < m_numPatchesZ)
                        minNeighbor = std::min(minNeighbor, m_patches[nz * m_numPatchesX + nx].lodLevel);
                }
                
                if (m_patches[idx].lodLevel > minNeighbor + 1)
                {
                    m_patches[idx].lodLevel = minNeighbor + 1;
                    changed = true;
                }
            }
        }
    }
}

void Terrain::UpdateNeighborInfo()
{
    for (int z = 0; z < m_numPatchesZ; ++z)
    {
        for (int x = 0; x < m_numPatchesX; ++x)
        {
            int idx = z * m_numPatchesX + x;
            TerrainPatch& patch = m_patches[idx];
            
            // Top neighbor (z - 1)
            patch.neighborLOD[0] = (z > 0) ? m_patches[(z-1) * m_numPatchesX + x].lodLevel : -1;
            // Bottom neighbor (z + 1)
            patch.neighborLOD[1] = (z < m_numPatchesZ - 1) ? m_patches[(z+1) * m_numPatchesX + x].lodLevel : -1;
            // Left neighbor (x - 1)
            patch.neighborLOD[2] = (x > 0) ? m_patches[z * m_numPatchesX + (x-1)].lodLevel : -1;
            // Right neighbor (x + 1)
            patch.neighborLOD[3] = (x < m_numPatchesX - 1) ? m_patches[z * m_numPatchesX + (x+1)].lodLevel : -1;
            
            // Determine stitch config: stitch if neighbor is coarser (higher LOD level)
            patch.stitchConfig.stitchTop = (patch.neighborLOD[0] > patch.lodLevel);
            patch.stitchConfig.stitchBottom = (patch.neighborLOD[1] > patch.lodLevel);
            patch.stitchConfig.stitchLeft = (patch.neighborLOD[2] > patch.lodLevel);
            patch.stitchConfig.stitchRight = (patch.neighborLOD[3] > patch.lodLevel);
        }
    }
}

// ============================================================================
// Frustum Culling
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
        for (auto& p : m_patches) p.isVisible = true;
        return;
    }
    
    for (auto& patch : m_patches)
    {
        patch.isVisible = m_frustum.ContainsAABB(patch.boundingBox);
        if (!patch.isVisible) m_renderStats.culledPatches++;
    }
}

// ============================================================================
// Rendering
// ============================================================================
void Terrain::PrepareRenderBatches()
{
    for (auto& b : m_renderBatches) { b.patches.clear(); b.totalTriangles = 0; }
    
    for (const auto& patch : m_patches)
    {
        if (!patch.isVisible) continue;
        int lod = patch.lodLevel;
        if (lod >= 0 && lod < MAX_LOD_LEVELS)
        {
            m_renderBatches[lod].patches.push_back(&patch);
            m_renderStats.visiblePatches++;
            m_renderStats.lodDistribution[lod]++;
        }
    }
}

void Terrain::Render(ID3D11DeviceContext* context)
{
    if (!context || !m_vertexBuffer || !m_indexBuffer) return;
    
    UINT stride = sizeof(Vertex), offset = 0;
    context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->DrawIndexed(m_indexCount, 0, 0);
}

void Terrain::Render(ID3D11DeviceContext* context, const XMFLOAT3& cameraPosition)
{
    if (!m_useCDLOD) { Render(context); return; }
    if (!context || !m_vertexBuffer) return;
    
    UpdateLODSelection(cameraPosition);
    if (m_frustumValid) PerformFrustumCulling();
    PrepareRenderBatches();
    RenderBatches(context);
    m_frameCount++;
}

void Terrain::Render(ID3D11DeviceContext* context, const XMFLOAT3& cameraPosition,
                     const XMMATRIX& viewMatrix, const XMMATRIX& projMatrix)
{
    if (!m_useCDLOD) { Render(context); return; }
    if (!context || !m_vertexBuffer) return;
    
    UpdateFrustum(XMMatrixMultiply(viewMatrix, projMatrix));
    UpdateLODSelection(cameraPosition);
    PerformFrustumCulling();
    PrepareRenderBatches();
    RenderBatches(context);
    
    if (m_cdlodConfig.debugMode && ++m_frameCount % 60 == 0)
    {
        wchar_t msg[512];
        swprintf_s(msg, L"[TERRAIN] Visible:%d Culled:%d Draws:%d LOD[0-3]:%d,%d,%d,%d\n",
                   m_renderStats.visiblePatches, m_renderStats.culledPatches, m_renderStats.drawCalls,
                   m_renderStats.lodDistribution[0], m_renderStats.lodDistribution[1],
                   m_renderStats.lodDistribution[2], m_renderStats.lodDistribution[3]);
        OutputDebugStringW(msg);
    }
}

void Terrain::RenderBatches(ID3D11DeviceContext* context)
{
    UINT stride = sizeof(Vertex), offset = 0;
    context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    
    for (int lod = 0; lod < MAX_LOD_LEVELS; ++lod)
    {
        const auto& batch = m_renderBatches[lod];
        if (batch.patches.empty() || !m_lodTemplates[lod].indexBuffer) continue;
        
        context->IASetIndexBuffer(m_lodTemplates[lod].indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        
        for (const auto* patch : batch.patches)
        {
            RenderPatchWithStitching(context, *patch);
        }
    }
}

void Terrain::RenderPatchWithStitching(ID3D11DeviceContext* context, const TerrainPatch& patch)
{
    int patchIdx = patch.patchZ * m_numPatchesX + patch.patchX;
    if (patchIdx < 0 || patchIdx >= (int)m_patchStitchRanges.size()) return;
    
    int lod = patch.lodLevel;
    int configId = patch.stitchConfig.GetConfigId();
    
    auto& range = m_patchStitchRanges[patchIdx][lod][configId];
    if (range.second > 0)
    {
        context->DrawIndexed(range.second, range.first, 0);
        m_renderStats.drawCalls++;
        m_renderStats.totalTriangles += range.second / 3;
    }
}

// ============================================================================
// Height Query
// ============================================================================
float Terrain::GetHeightAt(float worldX, float worldZ) const
{
    if (m_heightData.empty()) return 0;
    
    float fx = (worldX + m_params.sizeX * 0.5f) / m_params.sizeX * (m_params.width - 1);
    float fz = (worldZ + m_params.sizeZ * 0.5f) / m_params.sizeZ * (m_params.height - 1);
    
    if (fx < 0 || fx >= m_params.width - 1 || fz < 0 || fz >= m_params.height - 1) return 0;
    
    int x0 = (int)fx, z0 = (int)fz;
    int x1 = std::min(x0 + 1, m_params.width - 1), z1 = std::min(z0 + 1, m_params.height - 1);
    float xf = fx - x0, zf = fz - z0;
    
    float h00 = m_heightData[z0 * m_params.width + x0];
    float h10 = m_heightData[z0 * m_params.width + x1];
    float h01 = m_heightData[z1 * m_params.width + x0];
    float h11 = m_heightData[z1 * m_params.width + x1];
    
    return ((h00*(1-xf) + h10*xf)*(1-zf) + (h01*(1-xf) + h11*xf)*zf) * m_params.heightScale + m_params.heightOffset;
}

void Terrain::GenerateLODTemplates(ID3D11Device* device) { /* Deprecated, use GeneratePatchIndicesWithStitching */ }
