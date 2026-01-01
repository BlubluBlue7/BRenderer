#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Terrain.h"
#include "Mesh.h"

#include <algorithm>
#include <cmath>
#include <cfloat>

#include "stb_image.h"

using namespace DirectX;

// ============================================================================
// CDLODFrustum 实现
// ============================================================================
void CDLODFrustum::ExtractFromMatrix(const XMMATRIX& viewProj)
{
    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, viewProj);
    
    // Left plane
    planes[0].x = m._14 + m._11;
    planes[0].y = m._24 + m._21;
    planes[0].z = m._34 + m._31;
    planes[0].w = m._44 + m._41;
    
    // Right plane
    planes[1].x = m._14 - m._11;
    planes[1].y = m._24 - m._21;
    planes[1].z = m._34 - m._31;
    planes[1].w = m._44 - m._41;
    
    // Top plane
    planes[2].x = m._14 - m._12;
    planes[2].y = m._24 - m._22;
    planes[2].z = m._34 - m._32;
    planes[2].w = m._44 - m._42;
    
    // Bottom plane
    planes[3].x = m._14 + m._12;
    planes[3].y = m._24 + m._22;
    planes[3].z = m._34 + m._32;
    planes[3].w = m._44 + m._42;
    
    // Near plane
    planes[4].x = m._13;
    planes[4].y = m._23;
    planes[4].z = m._33;
    planes[4].w = m._43;
    
    // Far plane
    planes[5].x = m._14 - m._13;
    planes[5].y = m._24 - m._23;
    planes[5].z = m._34 - m._33;
    planes[5].w = m._44 - m._43;
    
    // 归一化平面
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

bool CDLODFrustum::Intersects(const BoundingBox& box) const
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
// CDLODQuadTree 实现
// ============================================================================
CDLODQuadTree::CDLODQuadTree()
    : m_rootNodesX(0)
    , m_rootNodesZ(0)
    , m_maxLODLevel(0)
{
}

CDLODQuadTree::~CDLODQuadTree()
{
}

bool CDLODQuadTree::Initialize(const TerrainParams& params, const CDLODSettings& settings,
                               const std::vector<float>& heightData)
{
    m_terrainParams = params;
    m_settings = settings;
    
    // 计算最大LOD级别
    // 每个LOD级别的节点覆盖 gridDimension * 2^lodLevel 个高度图单元
    int maxCellsPerNode = settings.gridMeshDimension;
    int heightmapCells = std::max(params.heightmapWidth - 1, params.heightmapHeight - 1);
    
    m_maxLODLevel = 0;
    while (maxCellsPerNode * (1 << m_maxLODLevel) < heightmapCells && 
           m_maxLODLevel < settings.maxLODLevels - 1)
    {
        m_maxLODLevel++;
    }
    
    // 计算根节点数量
    int rootNodeCells = settings.gridMeshDimension * (1 << m_maxLODLevel);
    m_rootNodesX = (params.heightmapWidth - 1 + rootNodeCells - 1) / rootNodeCells;
    m_rootNodesZ = (params.heightmapHeight - 1 + rootNodeCells - 1) / rootNodeCells;
    
    // 确保至少有1个根节点
    m_rootNodesX = std::max(1, m_rootNodesX);
    m_rootNodesZ = std::max(1, m_rootNodesZ);
    
    BuildQuadTree(heightData);
    
    wchar_t msg[256];
    swprintf_s(msg, L"[CDLOD] QuadTree initialized: %d nodes, %d root nodes (%dx%d), max LOD level: %d\n",
               static_cast<int>(m_nodes.size()), static_cast<int>(m_rootNodeIndices.size()),
               m_rootNodesX, m_rootNodesZ, m_maxLODLevel);
    OutputDebugStringW(msg);
    
    return true;
}

void CDLODQuadTree::BuildQuadTree(const std::vector<float>& heightData)
{
    m_nodes.clear();
    m_rootNodeIndices.clear();
    
    // 创建根节点并递归构建子树
    float worldSizeX = m_terrainParams.worldSizeX;
    float worldSizeZ = m_terrainParams.worldSizeZ;
    
    float rootNodeSizeX = worldSizeX / m_rootNodesX;
    float rootNodeSizeZ = worldSizeZ / m_rootNodesZ;
    
    for (int z = 0; z < m_rootNodesZ; ++z)
    {
        for (int x = 0; x < m_rootNodesX; ++x)
        {
            float minX = -worldSizeX * 0.5f + x * rootNodeSizeX;
            float minZ = -worldSizeZ * 0.5f + z * rootNodeSizeZ;
            float maxX = minX + rootNodeSizeX;
            float maxZ = minZ + rootNodeSizeZ;
            
            int nodeIndex = CreateNode(m_maxLODLevel, x, z, minX, minZ, maxX, maxZ);
            m_rootNodeIndices.push_back(nodeIndex);
            
            // 计算节点高度范围
            ComputeNodeHeightRange(m_nodes[nodeIndex], heightData);
        }
    }
}

int CDLODQuadTree::CreateNode(int lodLevel, int x, int z, 
                              float minX, float minZ, float maxX, float maxZ)
{
    int nodeIndex = static_cast<int>(m_nodes.size());
    m_nodes.push_back(CDLODNode());
    
    CDLODNode& node = m_nodes[nodeIndex];
    node.lodLevel = lodLevel;
    node.x = x;
    node.z = z;
    node.minX = minX;
    node.minZ = minZ;
    node.maxX = maxX;
    node.maxZ = maxZ;
    node.isSelected = false;
    
    // 如果不是最细级别，创建子节点
    if (lodLevel > 0)
    {
        float midX = (minX + maxX) * 0.5f;
        float midZ = (minZ + maxZ) * 0.5f;
        
        int childLOD = lodLevel - 1;
        int childX = x * 2;
        int childZ = z * 2;
        
        // TopLeft (x, z)
        node.childIndices[0] = CreateNode(childLOD, childX, childZ, minX, minZ, midX, midZ);
        // TopRight (x+1, z)
        node.childIndices[1] = CreateNode(childLOD, childX + 1, childZ, midX, minZ, maxX, midZ);
        // BottomLeft (x, z+1)
        node.childIndices[2] = CreateNode(childLOD, childX, childZ + 1, minX, midZ, midX, maxZ);
        // BottomRight (x+1, z+1)
        node.childIndices[3] = CreateNode(childLOD, childX + 1, childZ + 1, midX, midZ, maxX, maxZ);
    }
    
    return nodeIndex;
}

void CDLODQuadTree::ComputeNodeHeightRange(CDLODNode& node, const std::vector<float>& heightData)
{
    // 计算节点对应的高度图区域
    float worldSizeX = m_terrainParams.worldSizeX;
    float worldSizeZ = m_terrainParams.worldSizeZ;
    
    int hmWidth = m_terrainParams.heightmapWidth;
    int hmHeight = m_terrainParams.heightmapHeight;
    
    // 转换世界坐标到高度图坐标
    float normMinX = (node.minX + worldSizeX * 0.5f) / worldSizeX;
    float normMaxX = (node.maxX + worldSizeX * 0.5f) / worldSizeX;
    float normMinZ = (node.minZ + worldSizeZ * 0.5f) / worldSizeZ;
    float normMaxZ = (node.maxZ + worldSizeZ * 0.5f) / worldSizeZ;
    
    int startX = std::max(0, static_cast<int>(normMinX * (hmWidth - 1)));
    int endX = std::min(hmWidth - 1, static_cast<int>(normMaxX * (hmWidth - 1)));
    int startZ = std::max(0, static_cast<int>(normMinZ * (hmHeight - 1)));
    int endZ = std::min(hmHeight - 1, static_cast<int>(normMaxZ * (hmHeight - 1)));
    
    float minY = FLT_MAX;
    float maxY = -FLT_MAX;
    
    for (int z = startZ; z <= endZ; ++z)
    {
        for (int x = startX; x <= endX; ++x)
        {
            float h = heightData[z * hmWidth + x] * m_terrainParams.heightScale + 
                     m_terrainParams.heightOffset;
            minY = std::min(minY, h);
            maxY = std::max(maxY, h);
        }
    }
    
    // 稍微扩大范围以确保安全
    node.minY = minY - 1.0f;
    node.maxY = maxY + 1.0f;
    
    // 递归更新子节点的高度范围并合并到父节点
    if (node.childIndices[0] >= 0)
    {
        for (int i = 0; i < 4; ++i)
        {
            CDLODNode& child = m_nodes[node.childIndices[i]];
            ComputeNodeHeightRange(child, heightData);
            node.minY = std::min(node.minY, child.minY);
            node.maxY = std::max(node.maxY, child.maxY);
        }
    }
}

void CDLODQuadTree::SelectNodes(const XMFLOAT3& cameraPos,
                                const CDLODFrustum* frustum,
                                std::vector<CDLODRenderNode>& outRenderNodes)
{
    outRenderNodes.clear();
    
    // 重置所有节点的选择状态
    for (auto& node : m_nodes)
    {
        node.isSelected = false;
    }
    
    // 从根节点开始递归选择
    for (int rootIndex : m_rootNodeIndices)
    {
        SelectNode(rootIndex, cameraPos, frustum, outRenderNodes);
    }
    
    // 更新邻居LOD信息
    UpdateNeighborLODs(outRenderNodes);
}

void CDLODQuadTree::SelectNode(int nodeIndex, 
                               const XMFLOAT3& cameraPos,
                               const CDLODFrustum* frustum,
                               std::vector<CDLODRenderNode>& outRenderNodes)
{
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodes.size()))
        return;
    
    CDLODNode& node = m_nodes[nodeIndex];
    
    // 视锥剔除
    if (frustum && m_settings.enableFrustumCulling)
    {
        BoundingBox bb = node.GetBoundingBox();
        if (!frustum->Intersects(bb))
            return;
    }
    
    // 计算到相机的距离
    float distance = node.ComputeDistance(cameraPos.x, cameraPos.y, cameraPos.z);
    node.distanceToCamera = distance;
    
    // 判断是否应该细分
    bool shouldRefine = ShouldRefine(node, distance);
    bool hasChildren = (node.childIndices[0] >= 0);
    
    if (shouldRefine && hasChildren)
    {
        // 细分：递归处理子节点
        for (int i = 0; i < 4; ++i)
        {
            SelectNode(node.childIndices[i], cameraPos, frustum, outRenderNodes);
        }
    }
    else
    {
        // 选择此节点用于渲染
        node.isSelected = true;
        node.morphFactor = ComputeMorphFactor(distance, node.lodLevel);
        
        CDLODRenderNode renderNode;
        renderNode.node = &node;
        renderNode.morphFactor = node.morphFactor;
        outRenderNodes.push_back(renderNode);
    }
}

bool CDLODQuadTree::ShouldRefine(const CDLODNode& node, float distance) const
{
    // CDLOD核心算法：
    // 如果距离小于当前LOD级别的范围，则应该细分到更高细节级别
    
    // LOD级别0是最细节的，不能再细分
    if (node.lodLevel <= 0)
        return false;
    
    // 获取当前LOD级别的范围距离
    float lodRange = m_settings.GetLODRange(node.lodLevel);
    
    // 如果距离小于范围，应该细分
    return distance < lodRange;
}

float CDLODQuadTree::ComputeMorphFactor(float distance, int lodLevel) const
{
    if (!m_settings.enableMorphing)
        return 0.0f;
    
    // Morphing发生在LOD边界附近
    // morphFactor = 0: 完全使用当前LOD几何
    // morphFactor = 1: 完全morphed到下一个（更粗糙）LOD
    
    float morphStart = m_settings.GetMorphStart(lodLevel);
    float morphEnd = m_settings.GetMorphEnd(lodLevel);
    
    if (distance <= morphStart)
        return 0.0f;
    if (distance >= morphEnd)
        return 1.0f;
    
    return (distance - morphStart) / (morphEnd - morphStart);
}

void CDLODQuadTree::UpdateNeighborLODs(std::vector<CDLODRenderNode>& renderNodes)
{
    // 构建选中节点的空间哈希表，用于快速查找邻居
    // 简化实现：对每个选中的节点，查找其四个方向的邻居
    
    for (auto& renderNode : renderNodes)
    {
        const CDLODNode* node = renderNode.node;
        
        // 初始化为-1（无邻居/边界）
        for (int i = 0; i < 4; ++i)
            renderNode.neighborLOD[i] = -1;
        
        float nodeSize = node->GetSizeX();
        float epsilon = nodeSize * 0.01f;
        
        // 查找四个方向的邻居
        // Top (minZ方向)
        float topZ = node->minZ - epsilon;
        // Bottom (maxZ方向)
        float bottomZ = node->maxZ + epsilon;
        // Left (minX方向)
        float leftX = node->minX - epsilon;
        // Right (maxX方向)
        float rightX = node->maxX + epsilon;
        
        for (const auto& other : renderNodes)
        {
            if (other.node == node) continue;
            
            const CDLODNode* otherNode = other.node;
            
            // 检查是否是顶部邻居
            if (otherNode->maxZ > node->minZ - epsilon && 
                otherNode->maxZ < node->minZ + epsilon &&
                otherNode->minX < node->maxX && otherNode->maxX > node->minX)
            {
                renderNode.neighborLOD[0] = std::max(renderNode.neighborLOD[0], otherNode->lodLevel);
            }
            
            // 检查是否是底部邻居
            if (otherNode->minZ > node->maxZ - epsilon && 
                otherNode->minZ < node->maxZ + epsilon &&
                otherNode->minX < node->maxX && otherNode->maxX > node->minX)
            {
                renderNode.neighborLOD[1] = std::max(renderNode.neighborLOD[1], otherNode->lodLevel);
            }
            
            // 检查是否是左侧邻居
            if (otherNode->maxX > node->minX - epsilon && 
                otherNode->maxX < node->minX + epsilon &&
                otherNode->minZ < node->maxZ && otherNode->maxZ > node->minZ)
            {
                renderNode.neighborLOD[2] = std::max(renderNode.neighborLOD[2], otherNode->lodLevel);
            }
            
            // 检查是否是右侧邻居
            if (otherNode->minX > node->maxX - epsilon && 
                otherNode->minX < node->maxX + epsilon &&
                otherNode->minZ < node->maxZ && otherNode->maxZ > node->minZ)
            {
                renderNode.neighborLOD[3] = std::max(renderNode.neighborLOD[3], otherNode->lodLevel);
            }
        }
    }
}

// ============================================================================
// CDLODMeshTemplate 实现
// ============================================================================
CDLODMeshTemplate::CDLODMeshTemplate()
    : m_gridDimension(0)
    , m_vertexCount(0)
{
    m_indexCounts.fill(0);
}

CDLODMeshTemplate::~CDLODMeshTemplate()
{
}

bool CDLODMeshTemplate::Initialize(ID3D11Device* device, int gridDimension)
{
    if (!device || gridDimension < 2)
        return false;
    
    m_gridDimension = gridDimension;
    
    // 生成顶点
    std::vector<Vertex> vertices;
    GenerateVertices(vertices);
    
    // 创建顶点缓冲区
    D3D11_BUFFER_DESC vbd = {};
    vbd.Usage = D3D11_USAGE_DEFAULT;
    vbd.ByteWidth = static_cast<UINT>(sizeof(Vertex) * vertices.size());
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    
    D3D11_SUBRESOURCE_DATA vdata = {};
    vdata.pSysMem = vertices.data();
    
    HRESULT hr = device->CreateBuffer(&vbd, &vdata, m_vertexBuffer.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    m_vertexCount = static_cast<UINT>(vertices.size());
    
    // 生成16种缝合配置的索引缓冲区
    for (int stitchMask = 0; stitchMask < 16; ++stitchMask)
    {
        if (!CreateIndexBuffer(device, stitchMask))
            return false;
    }
    
    wchar_t msg[256];
    swprintf_s(msg, L"[CDLOD] MeshTemplate initialized: %dx%d grid, %u vertices\n",
               gridDimension, gridDimension, m_vertexCount);
    OutputDebugStringW(msg);
    
    return true;
}

void CDLODMeshTemplate::GenerateVertices(std::vector<Vertex>& vertices)
{
    vertices.clear();
    
    int vertexDim = m_gridDimension + 1;
    vertices.reserve(vertexDim * vertexDim);
    
    for (int z = 0; z <= m_gridDimension; ++z)
    {
        for (int x = 0; x <= m_gridDimension; ++x)
        {
            Vertex v;
            
            // 位置：使用0-1范围的局部坐标
            // 实际世界位置将在顶点着色器中计算
            v.position[0] = static_cast<float>(x) / m_gridDimension;
            v.position[1] = 0.0f;  // Y坐标将从高度图采样
            v.position[2] = static_cast<float>(z) / m_gridDimension;
            
            // 法线（将在着色器中计算）
            v.normal[0] = 0.0f;
            v.normal[1] = 1.0f;
            v.normal[2] = 0.0f;
            
            // 颜色
            v.color[0] = v.color[1] = v.color[2] = 1.0f;
            
            // 纹理坐标与位置相同
            v.texCoord[0] = v.position[0];
            v.texCoord[1] = v.position[2];
            
            vertices.push_back(v);
        }
    }
}

void CDLODMeshTemplate::GenerateIndices(std::vector<uint32_t>& indices, int stitchMask)
{
    indices.clear();
    
    bool stitchTop = (stitchMask & 1) != 0;
    bool stitchBottom = (stitchMask & 2) != 0;
    bool stitchLeft = (stitchMask & 4) != 0;
    bool stitchRight = (stitchMask & 8) != 0;
    
    int dim = m_gridDimension;
    int vertexDim = dim + 1;
    
    auto V = [vertexDim](int x, int z) -> uint32_t {
        return static_cast<uint32_t>(z * vertexDim + x);
    };
    
    auto addTri = [&indices](uint32_t a, uint32_t b, uint32_t c) {
        indices.push_back(a);
        indices.push_back(b);
        indices.push_back(c);
    };
    
    // 遍历每个单元格
    for (int z = 0; z < dim; ++z)
    {
        for (int x = 0; x < dim; ++x)
        {
            bool isTopEdge = (z == 0);
            bool isBottomEdge = (z == dim - 1);
            bool isLeftEdge = (x == 0);
            bool isRightEdge = (x == dim - 1);
            
            bool needStitchTop = isTopEdge && stitchTop;
            bool needStitchBottom = isBottomEdge && stitchBottom;
            bool needStitchLeft = isLeftEdge && stitchLeft;
            bool needStitchRight = isRightEdge && stitchRight;
            
            uint32_t tl = V(x, z);
            uint32_t tr = V(x + 1, z);
            uint32_t bl = V(x, z + 1);
            uint32_t br = V(x + 1, z + 1);
            
            // 处理边界缝合
            // 缝合的基本思想：在边界处，每隔一个顶点使用一个，以匹配较粗糙邻居的分辨率
            
            if (needStitchTop && (x % 2 == 0) && (x + 1 < dim))
            {
                // 顶部边界缝合：合并两个单元格
                uint32_t tr2 = V(x + 2, z);
                uint32_t br2 = V(x + 2, z + 1);
                
                // 创建连接粗分辨率边界的三角形
                addTri(tl, bl, tr2);    // 大三角形跨越两个单元
                addTri(bl, br, tr2);
                addTri(br, br2, tr2);
                
                x++;  // 跳过下一个单元格
                continue;
            }
            
            if (needStitchBottom && (x % 2 == 0) && (x + 1 < dim))
            {
                uint32_t tr2 = V(x + 2, z);
                uint32_t bl2 = V(x + 2, z + 1);
                
                addTri(tl, bl, tr);
                addTri(tr, bl, bl2);
                addTri(tr, bl2, tr2);
                
                x++;
                continue;
            }
            
            if (needStitchLeft && (z % 2 == 0) && (z + 1 < dim))
            {
                uint32_t bl2 = V(x, z + 2);
                uint32_t br2 = V(x + 1, z + 2);
                
                addTri(tl, bl2, tr);
                addTri(tr, bl2, br);
                addTri(br, bl2, br2);
                
                continue;  // 这个逻辑需要重新考虑
            }
            
            if (needStitchRight && (z % 2 == 0) && (z + 1 < dim))
            {
                uint32_t bl2 = V(x, z + 2);
                uint32_t br2 = V(x + 1, z + 2);
                
                addTri(tl, bl, tr);
                addTri(bl, br2, tr);
                addTri(bl, bl2, br2);
                
                continue;
            }
            
            // 标准双三角形单元格
            addTri(tl, bl, tr);
            addTri(tr, bl, br);
        }
    }
    
    // 注意：上面的缝合逻辑是简化版本
    // 完整的CDLOD缝合需要更复杂的处理
    // 这里我们使用一个更简单但正确的方法：
    // 对于边界上需要缝合的顶点，在顶点着色器中进行snap处理
}

bool CDLODMeshTemplate::CreateIndexBuffer(ID3D11Device* device, int stitchMask)
{
    std::vector<uint32_t> indices;
    
    // 对于缝合，我们使用顶点着色器中的snap方法而不是复杂的索引生成
    // 所以所有配置都使用相同的基本网格索引
    int dim = m_gridDimension;
    int vertexDim = dim + 1;
    
    auto V = [vertexDim](int x, int z) -> uint32_t {
        return static_cast<uint32_t>(z * vertexDim + x);
    };
    
    // 生成标准网格索引
    for (int z = 0; z < dim; ++z)
    {
        for (int x = 0; x < dim; ++x)
        {
            uint32_t tl = V(x, z);
            uint32_t tr = V(x + 1, z);
            uint32_t bl = V(x, z + 1);
            uint32_t br = V(x + 1, z + 1);
            
            // 两个三角形组成一个四边形
            indices.push_back(tl);
            indices.push_back(bl);
            indices.push_back(tr);
            
            indices.push_back(tr);
            indices.push_back(bl);
            indices.push_back(br);
        }
    }
    
    if (indices.empty())
        return false;
    
    D3D11_BUFFER_DESC ibd = {};
    ibd.Usage = D3D11_USAGE_DEFAULT;
    ibd.ByteWidth = static_cast<UINT>(sizeof(uint32_t) * indices.size());
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    
    D3D11_SUBRESOURCE_DATA idata = {};
    idata.pSysMem = indices.data();
    
    HRESULT hr = device->CreateBuffer(&ibd, &idata, m_indexBuffers[stitchMask].GetAddressOf());
    if (FAILED(hr))
        return false;
    
    m_indexCounts[stitchMask] = static_cast<UINT>(indices.size());
    return true;
}

ID3D11Buffer* CDLODMeshTemplate::GetIndexBuffer(int stitchMask) const
{
    if (stitchMask < 0 || stitchMask >= 16)
        return m_indexBuffers[0].Get();
    return m_indexBuffers[stitchMask].Get();
}

UINT CDLODMeshTemplate::GetIndexCount(int stitchMask) const
{
    if (stitchMask < 0 || stitchMask >= 16)
        return m_indexCounts[0];
    return m_indexCounts[stitchMask];
}

// ============================================================================
// Terrain 实现
// ============================================================================
Terrain::Terrain()
    : m_lodLocked(false)
    , m_lockedLODLevel(0)
    , m_frameCount(0)
    , m_frustumValid(false)
{
}

Terrain::~Terrain()
{
}

bool Terrain::CreateFromHeightmap(ID3D11Device* device, const std::wstring& heightmapPath,
                                  const TerrainParams& params)
{
    if (!device)
        return false;
    
    m_params = params;
    
    if (!LoadHeightmap(heightmapPath))
        return false;
    
    return InitializeCDLOD(device);
}

bool Terrain::CreateProcedural(ID3D11Device* device, const TerrainParams& params)
{
    if (!device)
        return false;
    
    m_params = params;
    
    GenerateProceduralHeight();
    
    return InitializeCDLOD(device);
}

bool Terrain::LoadHeightmap(const std::wstring& path)
{
    if (path.empty())
        return false;
    
    // 转换宽字符路径到多字节
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string pathA(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, &pathA[0], size_needed, nullptr, nullptr);
    
    int width, height, channels;
    unsigned char* data = stbi_load(pathA.c_str(), &width, &height, &channels, 1);
    
    if (!data)
    {
        OutputDebugStringW(L"[CDLOD] Failed to load heightmap\n");
        return false;
    }
    
    m_params.heightmapWidth = width;
    m_params.heightmapHeight = height;
    
    m_heightData.resize(width * height);
    for (int i = 0; i < width * height; ++i)
    {
        m_heightData[i] = data[i] / 255.0f;
    }
    
    stbi_image_free(data);
    
    wchar_t msg[256];
    swprintf_s(msg, L"[CDLOD] Heightmap loaded: %dx%d\n", width, height);
    OutputDebugStringW(msg);
    
    return true;
}

void Terrain::GenerateProceduralHeight()
{
    int width = m_params.heightmapWidth;
    int height = m_params.heightmapHeight;
    
    m_heightData.resize(width * height);
    
    // ========================================================================
    // 高级程序化地形生成 - 支持大规模地形
    // 使用多层分形噪声（fBm）和域扭曲（Domain Warping）
    // ========================================================================
    
    // 哈希函数生成伪随机值
    auto hash = [](int x, int z, int seed = 0) -> float {
        int n = x + z * 57 + seed * 131;
        n = (n << 13) ^ n;
        return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7FFFFFFF) / 1073741824.0f);
    };
    
    // 平滑插值
    auto smoothstep = [](float t) -> float {
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);  // Perlin改进的quintic曲线
    };
    
    // 梯度噪声（类Perlin）
    auto gradientNoise = [&hash, &smoothstep](float x, float z, int seed = 0) -> float {
        int ix = static_cast<int>(floorf(x));
        int iz = static_cast<int>(floorf(z));
        float fx = x - ix;
        float fz = z - iz;
        
        float n00 = hash(ix, iz, seed);
        float n10 = hash(ix + 1, iz, seed);
        float n01 = hash(ix, iz + 1, seed);
        float n11 = hash(ix + 1, iz + 1, seed);
        
        float sx = smoothstep(fx);
        float sz = smoothstep(fz);
        
        return (n00 * (1-sx) + n10 * sx) * (1-sz) + (n01 * (1-sx) + n11 * sx) * sz;
    };
    
    // 分形布朗运动 (fBm) - 多层噪声叠加
    auto fbm = [&gradientNoise](float x, float z, int octaves, float lacunarity, float persistence, int seed = 0) -> float {
        float value = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        float maxValue = 0.0f;
        
        for (int i = 0; i < octaves; ++i)
        {
            value += gradientNoise(x * frequency, z * frequency, seed + i) * amplitude;
            maxValue += amplitude;
            amplitude *= persistence;
            frequency *= lacunarity;
        }
        
        return value / maxValue;  // 归一化到 [-1, 1]
    };
    
    // 山脊噪声 - 创建山脉特征
    auto ridgeNoise = [&gradientNoise](float x, float z, int octaves, float lacunarity, float persistence, int seed = 0) -> float {
        float value = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        float maxValue = 0.0f;
        float weight = 1.0f;
        
        for (int i = 0; i < octaves; ++i)
        {
            float n = gradientNoise(x * frequency, z * frequency, seed + i * 17);
            n = 1.0f - fabsf(n);  // 创建山脊效果
            n = n * n * weight;   // 锐化山脊
            weight = std::min(std::max(n * 2.0f, 0.0f), 1.0f);  // 权重影响后续层
            
            value += n * amplitude;
            maxValue += amplitude;
            amplitude *= persistence;
            frequency *= lacunarity;
        }
        
        return value / maxValue;
    };
    
    // 生成地形高度
    float minH = FLT_MAX, maxH = -FLT_MAX;
    
    for (int z = 0; z < height; ++z)
    {
        for (int x = 0; x < width; ++x)
        {
            // 归一化坐标
            float fx = static_cast<float>(x) / (width - 1);
            float fz = static_cast<float>(z) / (height - 1);
            
            // 域扭曲 - 使地形更自然
            float warpStrength = 0.3f;
            float warpX = fbm(fx * 2.0f, fz * 2.0f, 3, 2.0f, 0.5f, 100) * warpStrength;
            float warpZ = fbm(fx * 2.0f + 5.3f, fz * 2.0f + 1.7f, 3, 2.0f, 0.5f, 200) * warpStrength;
            float wfx = fx + warpX;
            float wfz = fz + warpZ;
            
            // 基础地形 - 大尺度起伏（大陆/平原）
            float baseHeight = fbm(wfx * 1.5f, wfz * 1.5f, 4, 2.0f, 0.5f, 0);
            baseHeight = (baseHeight + 1.0f) * 0.5f;  // 转换到 [0, 1]
            
            // 山脉 - 使用山脊噪声
            float mountains = ridgeNoise(wfx * 3.0f, wfz * 3.0f, 5, 2.2f, 0.5f, 300);
            
            // 丘陵 - 中等尺度细节
            float hills = fbm(wfx * 6.0f, wfz * 6.0f, 4, 2.0f, 0.45f, 500);
            hills = (hills + 1.0f) * 0.5f;
            
            // 细节 - 小尺度噪声
            float detail = fbm(wfx * 20.0f, wfz * 20.0f, 3, 2.0f, 0.4f, 700);
            detail = (detail + 1.0f) * 0.5f;
            
            // 混合各层
            // 山脉掩码 - 控制山脉出现的区域
            float mountainMask = fbm(fx * 0.8f, fz * 0.8f, 2, 2.0f, 0.5f, 900);
            mountainMask = std::max(0.0f, (mountainMask + 0.3f) * 1.5f);
            mountainMask = std::min(1.0f, mountainMask);
            
            // 最终高度合成
            float h = 0.0f;
            h += baseHeight * 0.3f;                        // 基础地形 30%
            h += mountains * mountainMask * 0.5f;          // 山脉 50%（受掩码控制）
            h += hills * 0.15f;                            // 丘陵 15%
            h += detail * 0.05f;                           // 细节 5%
            
            // 边缘衰减 - 让地形边缘平滑过渡到0
            float edgeFade = 1.0f;
            float edgeDist = 0.05f;  // 边缘距离
            if (fx < edgeDist) edgeFade *= fx / edgeDist;
            if (fz < edgeDist) edgeFade *= fz / edgeDist;
            if (fx > 1.0f - edgeDist) edgeFade *= (1.0f - fx) / edgeDist;
            if (fz > 1.0f - edgeDist) edgeFade *= (1.0f - fz) / edgeDist;
            
            h *= edgeFade;
            
            m_heightData[z * width + x] = std::max(0.0f, std::min(1.0f, h));
            
            minH = std::min(minH, h);
            maxH = std::max(maxH, h);
        }
    }
    
    wchar_t msg[256];
    swprintf_s(msg, L"[CDLOD] Procedural heightmap generated: %dx%d, height range [%.2f, %.2f]\n",
               width, height, minH, maxH);
    OutputDebugStringW(msg);
}

bool Terrain::InitializeCDLOD(ID3D11Device* device)
{
    if (m_heightData.empty())
        return false;
    
    // 创建四叉树
    m_quadTree = std::make_unique<CDLODQuadTree>();
    if (!m_quadTree->Initialize(m_params, m_settings, m_heightData))
        return false;
    
    // 创建网格模板
    m_meshTemplate = std::make_unique<CDLODMeshTemplate>();
    if (!m_meshTemplate->Initialize(device, m_settings.gridMeshDimension))
        return false;
    
    // 创建高度图纹理
    if (!CreateHeightmapTexture(device))
        return false;
    
    // 创建常量缓冲区
    if (!CreateConstantBuffer(device))
        return false;
    
    OutputDebugStringW(L"[CDLOD] Terrain system initialized successfully\n");
    return true;
}

bool Terrain::CreateHeightmapTexture(ID3D11Device* device)
{
    if (m_heightData.empty())
        return false;
    
    int width = m_params.heightmapWidth;
    int height = m_params.heightmapHeight;
    
    // 创建R32_FLOAT格式的高度图纹理
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R32_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = m_heightData.data();
    initData.SysMemPitch = width * sizeof(float);
    
    HRESULT hr = device->CreateTexture2D(&texDesc, &initData, m_heightmapTexture.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[CDLOD] Failed to create heightmap texture\n");
        return false;
    }
    
    // 创建SRV
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    
    hr = device->CreateShaderResourceView(m_heightmapTexture.Get(), &srvDesc, m_heightmapSRV.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[CDLOD] Failed to create heightmap SRV\n");
        return false;
    }
    
    OutputDebugStringW(L"[CDLOD] Heightmap texture created\n");
    return true;
}

bool Terrain::CreateConstantBuffer(ID3D11Device* device)
{
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.ByteWidth = sizeof(TerrainCBuffer);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    
    HRESULT hr = device->CreateBuffer(&cbDesc, nullptr, m_terrainCBuffer.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[CDLOD] Failed to create terrain constant buffer\n");
        return false;
    }
    
    return true;
}

void Terrain::Render(ID3D11DeviceContext* context, const XMFLOAT3& cameraPosition,
                     const XMMATRIX& viewMatrix, const XMMATRIX& projMatrix)
{
    if (!context || !m_quadTree || !m_meshTemplate)
        return;
    
    // 更新视锥体
    XMMATRIX viewProj = XMMatrixMultiply(viewMatrix, projMatrix);
    m_frustum.ExtractFromMatrix(viewProj);
    m_frustumValid = true;
    
    // 清除上一帧的统计
    m_stats = CDLODStats();
    m_stats.totalNodes = m_quadTree->GetNodeCount();
    
    // 选择要渲染的节点
    m_renderNodes.clear();
    
    if (m_lodLocked)
    {
        // LOD锁定模式：选择指定LOD级别的所有节点
        // 这里简化实现，直接使用正常选择
        m_quadTree->SelectNodes(cameraPosition, 
                               m_settings.enableFrustumCulling ? &m_frustum : nullptr,
                               m_renderNodes);
    }
    else
    {
        m_quadTree->SelectNodes(cameraPosition,
                               m_settings.enableFrustumCulling ? &m_frustum : nullptr,
                               m_renderNodes);
    }
    
    m_stats.selectedNodes = static_cast<int>(m_renderNodes.size());
    
    // 渲染选中的节点
    RenderSelectedNodes(context, m_renderNodes);
    
    // 更新兼容的RenderStats
    m_renderStats.visiblePatches = m_stats.selectedNodes;
    m_renderStats.culledPatches = m_stats.culledNodes;
    m_renderStats.drawCalls = m_stats.drawCalls;
    m_renderStats.totalTriangles = m_stats.triangleCount;
    
    for (int i = 0; i < 8; ++i)
        m_renderStats.lodDistribution[i] = m_stats.lodDistribution[i];
    
    // 调试输出
    if (m_settings.debugVisualization && (++m_frameCount % 60 == 0))
    {
        wchar_t msg[512];
        swprintf_s(msg, L"[CDLOD] Nodes: %d/%d selected, %d draws, %d tris\n",
                   m_stats.selectedNodes, m_stats.totalNodes,
                   m_stats.drawCalls, m_stats.triangleCount);
        OutputDebugStringW(msg);
    }
}

void Terrain::Render(ID3D11DeviceContext* context)
{
    // 简单渲染模式（不使用LOD）- 使用默认相机位置
    XMFLOAT3 defaultCamPos(0, 100, 0);
    XMMATRIX identity = XMMatrixIdentity();
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, 1.0f, 0.1f, 1000.0f);
    
    Render(context, defaultCamPos, identity, proj);
}

void Terrain::RenderSelectedNodes(ID3D11DeviceContext* context,
                                  const std::vector<CDLODRenderNode>& renderNodes)
{
    if (renderNodes.empty() || !m_meshTemplate)
        return;
    
    // 设置顶点缓冲区
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    ID3D11Buffer* vb = m_meshTemplate->GetVertexBuffer();
    context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    
    // 绑定高度图纹理到顶点着色器
    ID3D11ShaderResourceView* heightmapSRV = m_heightmapSRV.Get();
    context->VSSetShaderResources(0, 1, &heightmapSRV);
    
    // 渲染每个节点
    for (const auto& renderNode : renderNodes)
    {
        const CDLODNode* node = renderNode.node;
        if (!node) continue;
        
        // 计算缝合掩码
        int stitchMask = 0;
        // 如果邻居的LOD级别更高（更粗糙），需要缝合
        if (renderNode.neighborLOD[0] > node->lodLevel) stitchMask |= 1;  // Top
        if (renderNode.neighborLOD[1] > node->lodLevel) stitchMask |= 2;  // Bottom
        if (renderNode.neighborLOD[2] > node->lodLevel) stitchMask |= 4;  // Left
        if (renderNode.neighborLOD[3] > node->lodLevel) stitchMask |= 8;  // Right
        
        // 更新常量缓冲区
        UpdateConstantBuffer(context, renderNode);
        
        // 设置索引缓冲区
        ID3D11Buffer* ib = m_meshTemplate->GetIndexBuffer(stitchMask);
        UINT indexCount = m_meshTemplate->GetIndexCount(stitchMask);
        context->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);
        
        // 绘制
        context->DrawIndexed(indexCount, 0, 0);
        
        m_stats.drawCalls++;
        m_stats.triangleCount += indexCount / 3;
        
        if (node->lodLevel < 8)
            m_stats.lodDistribution[node->lodLevel]++;
    }
}

void Terrain::UpdateConstantBuffer(ID3D11DeviceContext* context,
                                   const CDLODRenderNode& renderNode)
{
    if (!m_terrainCBuffer)
        return;
    
    const CDLODNode* node = renderNode.node;
    
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = context->Map(m_terrainCBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr))
        return;
    
    TerrainCBuffer* cb = static_cast<TerrainCBuffer*>(mapped.pData);
    
    // 地形整体缩放和偏移
    cb->terrainScale = XMFLOAT4(m_params.worldSizeX, m_params.worldSizeZ, 
                                m_params.heightScale, 1.0f);
    cb->terrainOffset = XMFLOAT4(-m_params.worldSizeX * 0.5f, 
                                 -m_params.worldSizeZ * 0.5f,
                                 m_params.heightOffset, 0.0f);
    
    // Morphing参数
    cb->morphParams = XMFLOAT4(renderNode.morphFactor,
                               static_cast<float>(m_settings.gridMeshDimension),
                               static_cast<float>(node->lodLevel),
                               0.0f);
    
    // 节点参数：将0-1网格坐标转换到节点世界空间
    float nodeSizeX = node->GetSizeX();
    float nodeSizeZ = node->GetSizeZ();
    cb->nodeParams = XMFLOAT4(node->minX, node->minZ, nodeSizeX, nodeSizeZ);
    
    // 高度图大小
    cb->heightmapSize = XMFLOAT4(
        static_cast<float>(m_params.heightmapWidth),
        static_cast<float>(m_params.heightmapHeight),
        1.0f / m_params.heightmapWidth,
        1.0f / m_params.heightmapHeight);
    
    context->Unmap(m_terrainCBuffer.Get(), 0);
    
    // 绑定常量缓冲区到顶点着色器 slot 2
    ID3D11Buffer* cb_ptr = m_terrainCBuffer.Get();
    context->VSSetConstantBuffers(2, 1, &cb_ptr);
}

float Terrain::GetHeightAt(float worldX, float worldZ) const
{
    if (m_heightData.empty())
        return 0.0f;
    
    // 转换世界坐标到高度图坐标
    float normX = (worldX + m_params.worldSizeX * 0.5f) / m_params.worldSizeX;
    float normZ = (worldZ + m_params.worldSizeZ * 0.5f) / m_params.worldSizeZ;
    
    if (normX < 0 || normX > 1 || normZ < 0 || normZ > 1)
        return 0.0f;
    
    float fx = normX * (m_params.heightmapWidth - 1);
    float fz = normZ * (m_params.heightmapHeight - 1);
    
    int x0 = static_cast<int>(fx);
    int z0 = static_cast<int>(fz);
    int x1 = std::min(x0 + 1, m_params.heightmapWidth - 1);
    int z1 = std::min(z0 + 1, m_params.heightmapHeight - 1);
    
    float xf = fx - x0;
    float zf = fz - z0;
    
    float h00 = m_heightData[z0 * m_params.heightmapWidth + x0];
    float h10 = m_heightData[z0 * m_params.heightmapWidth + x1];
    float h01 = m_heightData[z1 * m_params.heightmapWidth + x0];
    float h11 = m_heightData[z1 * m_params.heightmapWidth + x1];
    
    // 双线性插值
    float h = (h00 * (1-xf) + h10 * xf) * (1-zf) + (h01 * (1-xf) + h11 * xf) * zf;
    
    return h * m_params.heightScale + m_params.heightOffset;
}

void Terrain::SetLockedLODLevel(int level)
{
    if (level >= 0 && level < m_settings.maxLODLevels)
    {
        m_lockedLODLevel = level;
        m_lodLocked = true;
    }
}

ID3D11Buffer* Terrain::GetVertexBuffer() const
{
    return m_meshTemplate ? m_meshTemplate->GetVertexBuffer() : nullptr;
}

ID3D11Buffer* Terrain::GetIndexBuffer() const
{
    return m_meshTemplate ? m_meshTemplate->GetIndexBuffer(0) : nullptr;
}

UINT Terrain::GetIndexCount() const
{
    return m_meshTemplate ? m_meshTemplate->GetIndexCount(0) : 0;
}
