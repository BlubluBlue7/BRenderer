#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Terrain_new.h"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <random>

#include "stb_image.h"

using namespace DirectX;

// ============================================================================
// TerrainNew 实现
// ============================================================================
TerrainNew::TerrainNew()
    : m_heightmapWidth(0)
    , m_heightmapHeight(0)
    , m_chunkCountX(0)
    , m_chunkCountZ(0)
{
}

TerrainNew::~TerrainNew()
{
}

bool TerrainNew::CreateFromHeightmap(ID3D11Device* device, const std::wstring& heightmapPath,
                                     const TerrainNewParams& params)
{
    if (!device)
        return false;

    m_params = params;

    // 如果提供了高度图路径，尝试加载
    if (!heightmapPath.empty())
    {
        if (!LoadHeightmap(heightmapPath))
        {
            OutputDebugStringW(L"[TerrainNew] Failed to load heightmap, using procedural generation instead.\n");
            GenerateProceduralHeight();
        }
    }
    else
    {
        // 没有高度图，使用随机算法生成
        GenerateProceduralHeight();
    }

    // 生成chunk网格
    if (!GenerateChunks(device))
        return false;

    // 构建四叉树
    BuildQuadTree();

    wchar_t msg[256];
    swprintf_s(msg, L"[TerrainNew] Terrain created: %dx%d chunks, %dx%d grid\n",
               m_chunkCountX, m_chunkCountZ, m_params.gridWidth, m_params.gridHeight);
    OutputDebugStringW(msg);

    return true;
}

bool TerrainNew::CreateProcedural(ID3D11Device* device, const TerrainNewParams& params)
{
    if (!device)
        return false;

    m_params = params;

    // 生成程序化高度数据
    GenerateProceduralHeight();

    // 生成chunk网格
    if (!GenerateChunks(device))
        return false;

    // 构建四叉树
    BuildQuadTree();

    wchar_t msg[256];
    swprintf_s(msg, L"[TerrainNew] Procedural terrain created: %dx%d chunks\n",
               m_chunkCountX, m_chunkCountZ);
    OutputDebugStringW(msg);

    return true;
}

bool TerrainNew::LoadHeightmap(const std::wstring& path)
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
        OutputDebugStringW(L"[TerrainNew] Failed to load heightmap image\n");
        return false;
    }

    m_heightmapWidth = width;
    m_heightmapHeight = height;

    m_heightData.resize(width * height);
    for (int i = 0; i < width * height; ++i)
    {
        m_heightData[i] = data[i] / 255.0f;  // 归一化到0-1范围
    }

    stbi_image_free(data);

    wchar_t msg[256];
    swprintf_s(msg, L"[TerrainNew] Heightmap loaded: %dx%d\n", width, height);
    OutputDebugStringW(msg);

    return true;
}

void TerrainNew::GenerateProceduralHeight()
{
    // 使用高度图尺寸或网格尺寸
    int width = m_params.gridWidth;
    int height = m_params.gridHeight;

    // 如果已经加载了高度图，使用高度图的尺寸
    if (m_heightmapWidth > 0 && m_heightmapHeight > 0)
    {
        width = m_heightmapWidth;
        height = m_heightmapHeight;
    }

    m_heightmapWidth = width;
    m_heightmapHeight = height;

    m_heightData.resize(width * height);

    // 使用随机数生成器
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(0.0f, 1.0f);

    // 简单的随机高度生成算法
    // 使用多层噪声叠加来生成更自然的地形
    for (int z = 0; z < height; ++z)
    {
        for (int x = 0; x < width; ++x)
        {
            float fx = static_cast<float>(x) / (width - 1);
            float fz = static_cast<float>(z) / (height - 1);

            // 基础高度 - 使用简单的正弦波叠加
            float h = 0.0f;

            // 大尺度起伏
            h += sinf(fx * 3.14159f * 2.0f) * 0.3f;
            h += sinf(fz * 3.14159f * 2.0f) * 0.3f;
            h += sinf((fx + fz) * 3.14159f * 3.0f) * 0.2f;

            // 中尺度细节
            h += sinf(fx * 3.14159f * 8.0f) * 0.15f;
            h += sinf(fz * 3.14159f * 8.0f) * 0.15f;

            // 小尺度噪声
            h += dis(gen) * 0.1f;

            // 归一化到0-1范围
            h = (h + 1.0f) * 0.5f;
            h = std::max(0.0f, std::min(1.0f, h));

            m_heightData[z * width + x] = h;
        }
    }

    wchar_t msg[256];
    swprintf_s(msg, L"[TerrainNew] Procedural heightmap generated: %dx%d\n", width, height);
    OutputDebugStringW(msg);
}

bool TerrainNew::GenerateChunks(ID3D11Device* device)
{
    if (!device)
        return false;

    // 计算chunk数量
    m_chunkCountX = (m_params.gridWidth - 1 + m_params.chunkSize - 1) / m_params.chunkSize;
    m_chunkCountZ = (m_params.gridHeight - 1 + m_params.chunkSize - 1) / m_params.chunkSize;

    float chunkWorldSizeX = m_params.worldSizeX / m_chunkCountX;
    float chunkWorldSizeZ = m_params.worldSizeZ / m_chunkCountZ;

    m_chunks.clear();
    m_chunks.reserve(m_chunkCountX * m_chunkCountZ);

    // 为每个chunk生成所有LOD级别的网格
    for (int z = 0; z < m_chunkCountZ; ++z)
    {
        for (int x = 0; x < m_chunkCountX; ++x)
        {
            TerrainChunk chunk;
            chunk.chunkX = x;
            chunk.chunkZ = z;
            chunk.lodLevel = 0;

            // 计算世界空间边界
            chunk.minX = -m_params.worldSizeX * 0.5f + x * chunkWorldSizeX;
            chunk.minZ = -m_params.worldSizeZ * 0.5f + z * chunkWorldSizeZ;
            chunk.maxX = chunk.minX + chunkWorldSizeX;
            chunk.maxZ = chunk.minZ + chunkWorldSizeZ;

            // 为每个LOD级别生成网格
            for (int lod = 0; lod < m_params.maxLODLevels; ++lod)
            {
                std::vector<Vertex> vertices;
                std::vector<uint32_t> indices;

                GenerateChunkMesh(x, z, lod, vertices, indices);

                if (!CreateChunkBuffers(device, chunk, vertices, indices, lod))
                {
                    OutputDebugStringW(L"[TerrainNew] Failed to create chunk buffers\n");
                    return false;
                }
            }

            // 计算高度范围
            CalculateChunkHeightRange(chunk);

            m_chunks.push_back(chunk);
        }
    }
    
    return true;
}

void TerrainNew::GenerateChunkMesh(int chunkX, int chunkZ, int lodLevel,
                                    std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
{
    vertices.clear();
    indices.clear();

    // 计算LOD级别的网格分辨率
    int lodDivisor = 1 << lodLevel;  // LOD 0: 1, LOD 1: 2, LOD 2: 4, ...
    int chunkGridSize = m_params.chunkSize / lodDivisor;
    chunkGridSize = std::max(2, chunkGridSize);  // 至少2x2

    // 计算chunk的世界空间大小
    float chunkWorldSizeX = m_params.worldSizeX / m_chunkCountX;
    float chunkWorldSizeZ = m_params.worldSizeZ / m_chunkCountZ;

    // 计算顶点步长
    float stepX = chunkWorldSizeX / chunkGridSize;
    float stepZ = chunkWorldSizeZ / chunkGridSize;

    // 计算高度图采样步长
    float heightStepX = static_cast<float>(m_heightmapWidth - 1) / (m_params.gridWidth - 1);
    float heightStepZ = static_cast<float>(m_heightmapHeight - 1) / (m_params.gridHeight - 1);

    // 生成顶点
    for (int z = 0; z <= chunkGridSize; ++z)
    {
        for (int x = 0; x <= chunkGridSize; ++x)
        {
            Vertex v;

            // 计算在chunk内的局部坐标
            int gridX = chunkX * m_params.chunkSize + (x * lodDivisor);
            int gridZ = chunkZ * m_params.chunkSize + (z * lodDivisor);

            // 限制在有效范围内
            gridX = std::min(gridX, m_params.gridWidth - 1);
            gridZ = std::min(gridZ, m_params.gridHeight - 1);

            // 世界空间位置
            float worldX = -m_params.worldSizeX * 0.5f + gridX * (m_params.worldSizeX / (m_params.gridWidth - 1));
            float worldZ = -m_params.worldSizeZ * 0.5f + gridZ * (m_params.worldSizeZ / (m_params.gridHeight - 1));

            // 采样高度图
            float heightX = gridX * heightStepX;
            float heightZ = gridZ * heightStepZ;

            int hx0 = static_cast<int>(heightX);
            int hz0 = static_cast<int>(heightZ);
            int hx1 = std::min(hx0 + 1, m_heightmapWidth - 1);
            int hz1 = std::min(hz0 + 1, m_heightmapHeight - 1);

            float fx = heightX - hx0;
            float fz = heightZ - hz0;

            // 双线性插值采样高度
            float h00 = m_heightData[hz0 * m_heightmapWidth + hx0];
            float h10 = m_heightData[hz0 * m_heightmapWidth + hx1];
            float h01 = m_heightData[hz1 * m_heightmapWidth + hx0];
            float h11 = m_heightData[hz1 * m_heightmapWidth + hx1];

            float height = (h00 * (1 - fx) + h10 * fx) * (1 - fz) +
                          (h01 * (1 - fx) + h11 * fx) * fz;

            float worldY = height * m_params.heightScale + m_params.heightOffset;

            v.position[0] = worldX;
            v.position[1] = worldY;
            v.position[2] = worldZ;

            // 计算法线（简化版，使用相邻顶点）
            float hLeft = h00;
            float hRight = h10;
            float hDown = h00;
            float hUp = h01;

            if (gridX > 0)
            {
                float hx = (gridX - 1) * heightStepX;
                int hx0_l = static_cast<int>(hx);
                int hx1_l = std::min(hx0_l + 1, m_heightmapWidth - 1);
                float fx_l = hx - hx0_l;
                float h00_l = m_heightData[hz0 * m_heightmapWidth + hx0_l];
                float h10_l = m_heightData[hz0 * m_heightmapWidth + hx1_l];
                hLeft = h00_l * (1 - fx_l) + h10_l * fx_l;
            }
            if (gridX < m_params.gridWidth - 1)
            {
                float hx = (gridX + 1) * heightStepX;
                int hx0_r = static_cast<int>(hx);
                int hx1_r = std::min(hx0_r + 1, m_heightmapWidth - 1);
                float fx_r = hx - hx0_r;
                float h00_r = m_heightData[hz0 * m_heightmapWidth + hx0_r];
                float h10_r = m_heightData[hz0 * m_heightmapWidth + hx1_r];
                hRight = h00_r * (1 - fx_r) + h10_r * fx_r;
            }
            if (gridZ > 0)
            {
                float hz = (gridZ - 1) * heightStepZ;
                int hz0_d = static_cast<int>(hz);
                int hz1_d = std::min(hz0_d + 1, m_heightmapHeight - 1);
                float fz_d = hz - hz0_d;
                float h00_d = m_heightData[hz0_d * m_heightmapWidth + hx0];
                float h01_d = m_heightData[hz1_d * m_heightmapWidth + hx0];
                hDown = h00_d * (1 - fz_d) + h01_d * fz_d;
            }
            if (gridZ < m_params.gridHeight - 1)
            {
                float hz = (gridZ + 1) * heightStepZ;
                int hz0_u = static_cast<int>(hz);
                int hz1_u = std::min(hz0_u + 1, m_heightmapHeight - 1);
                float fz_u = hz - hz0_u;
                float h00_u = m_heightData[hz0_u * m_heightmapWidth + hx0];
                float h01_u = m_heightData[hz1_u * m_heightmapWidth + hx0];
                hUp = h00_u * (1 - fz_u) + h01_u * fz_u;
            }

            // 计算法线向量
            float dx = stepX * 2.0f;
            float dz = stepZ * 2.0f;
            float dhx = (hRight - hLeft) * m_params.heightScale;
            float dhz = (hUp - hDown) * m_params.heightScale;

            DirectX::XMFLOAT3 normal(-dhx / dx, 1.0f, -dhz / dz);
            DirectX::XMVECTOR n = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&normal));

            DirectX::XMStoreFloat3(&normal, n);
            v.normal[0] = normal.x;
            v.normal[1] = normal.y;
            v.normal[2] = normal.z;

            // 颜色
            float colorFactor = height;
            v.color[0] = 0.5f + colorFactor * 0.5f;
            v.color[1] = 0.5f + colorFactor * 0.5f;
            v.color[2] = 0.5f + colorFactor * 0.5f;

            // 纹理坐标
            v.texCoord[0] = static_cast<float>(gridX) / (m_params.gridWidth - 1);
            v.texCoord[1] = static_cast<float>(gridZ) / (m_params.gridHeight - 1);

            vertices.push_back(v);
        }
    }

    // 生成索引
    for (int z = 0; z < chunkGridSize; ++z)
    {
        for (int x = 0; x < chunkGridSize; ++x)
        {
            uint32_t topLeft = z * (chunkGridSize + 1) + x;
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = (z + 1) * (chunkGridSize + 1) + x;
            uint32_t bottomRight = bottomLeft + 1;

            // 第一个三角形
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);

            // 第二个三角形
            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }
}

bool TerrainNew::CreateChunkBuffers(ID3D11Device* device, TerrainChunk& chunk,
                                    const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices,
                                    int lodLevel)
{
    if (!device || vertices.empty() || indices.empty())
        return false;

    // 确保有足够的缓冲区
    if (chunk.vertexBuffers.size() <= static_cast<size_t>(lodLevel))
    {
        chunk.vertexBuffers.resize(lodLevel + 1);
        chunk.indexBuffers.resize(lodLevel + 1);
        chunk.indexCounts.resize(lodLevel + 1);
    }

    // 创建顶点缓冲区
    D3D11_BUFFER_DESC vbd = {};
    vbd.Usage = D3D11_USAGE_DEFAULT;
    vbd.ByteWidth = static_cast<UINT>(sizeof(Vertex) * vertices.size());
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vdata = {};
    vdata.pSysMem = vertices.data();

    HRESULT hr = device->CreateBuffer(&vbd, &vdata, chunk.vertexBuffers[lodLevel].GetAddressOf());
    if (FAILED(hr))
        return false;

    // 创建索引缓冲区
    D3D11_BUFFER_DESC ibd = {};
    ibd.Usage = D3D11_USAGE_DEFAULT;
    ibd.ByteWidth = static_cast<UINT>(sizeof(uint32_t) * indices.size());
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA idata = {};
    idata.pSysMem = indices.data();

    hr = device->CreateBuffer(&ibd, &idata, chunk.indexBuffers[lodLevel].GetAddressOf());
    if (FAILED(hr))
        return false;

    chunk.indexCounts[lodLevel] = static_cast<UINT>(indices.size());

    return true;
}

void TerrainNew::BuildQuadTree()
{
    m_quadTree.clear();
    m_rootNodeIndices.clear();

    // 简化版：每个chunk作为一个节点
    for (size_t i = 0; i < m_chunks.size(); ++i)
    {
        QuadTreeNode node;
        const TerrainChunk& chunk = m_chunks[i];

        node.minX = chunk.minX;
        node.minZ = chunk.minZ;
        node.maxX = chunk.maxX;
        node.maxZ = chunk.maxZ;
        node.minY = chunk.minY;
        node.maxY = chunk.maxY;
        node.chunkX = chunk.chunkX;
        node.chunkZ = chunk.chunkZ;
        node.lodLevel = 0;
        node.hasChildren = false;

        m_quadTree.push_back(node);
        m_rootNodeIndices.push_back(static_cast<int>(i));
    }
}

void TerrainNew::SelectChunks(const DirectX::XMFLOAT3& cameraPosition, std::vector<TerrainChunk*>& outChunks)
{
    outChunks.clear();

    // 简单的视锥剔除（基于高度范围）
    for (auto& chunk : m_chunks)
    {
        // 计算到相机的距离
        float distance = chunk.GetDistanceToCamera(cameraPosition.x, cameraPosition.y, cameraPosition.z);
        
        // 简单的视锥剔除：如果chunk在相机下方太远，跳过
        if (cameraPosition.y < chunk.minY - 200.0f)
            continue;
        
        // 计算LOD级别
        int lodLevel = CalculateLODLevel(distance);
        chunk.lodLevel = lodLevel;
        
        // 确保LOD级别有效
        if (lodLevel >= 0 && lodLevel < static_cast<int>(chunk.vertexBuffers.size()))
        {
            outChunks.push_back(&chunk);
        }
    }
}

int TerrainNew::CalculateLODLevel(float distance) const
{
    for (int i = 0; i < m_params.maxLODLevels; ++i)
    {
        if (distance <= m_params.lodDistances[i])
            return i;
    }
    return m_params.maxLODLevels - 1;
}

void TerrainNew::CalculateChunkHeightRange(TerrainChunk& chunk)
{
    float minY = FLT_MAX;
    float maxY = -FLT_MAX;

    // 计算chunk对应的高度图区域
    float normMinX = (chunk.minX + m_params.worldSizeX * 0.5f) / m_params.worldSizeX;
    float normMaxX = (chunk.maxX + m_params.worldSizeX * 0.5f) / m_params.worldSizeX;
    float normMinZ = (chunk.minZ + m_params.worldSizeZ * 0.5f) / m_params.worldSizeZ;
    float normMaxZ = (chunk.maxZ + m_params.worldSizeZ * 0.5f) / m_params.worldSizeZ;

    int startX = std::max(0, static_cast<int>(normMinX * (m_heightmapWidth - 1)));
    int endX = std::min(m_heightmapWidth - 1, static_cast<int>(normMaxX * (m_heightmapWidth - 1)));
    int startZ = std::max(0, static_cast<int>(normMinZ * (m_heightmapHeight - 1)));
    int endZ = std::min(m_heightmapHeight - 1, static_cast<int>(normMaxZ * (m_heightmapHeight - 1)));

    for (int z = startZ; z <= endZ; ++z)
    {
        for (int x = startX; x <= endX; ++x)
        {
            float h = m_heightData[z * m_heightmapWidth + x] * m_params.heightScale + m_params.heightOffset;
            minY = std::min(minY, h);
            maxY = std::max(maxY, h);
        }
    }

    chunk.minY = minY;
    chunk.maxY = maxY;
}

void TerrainNew::Render(ID3D11DeviceContext* context, const DirectX::XMFLOAT3& cameraPosition)
{
    if (!context || m_chunks.empty())
        return;

    // 选择要渲染的chunk
    SelectChunks(cameraPosition, m_selectedChunks);

    // 重置统计
    m_renderStats = RenderStats();
    m_renderStats.visibleChunks = static_cast<int>(m_selectedChunks.size());

    // 渲染每个选中的chunk
    for (TerrainChunk* chunk : m_selectedChunks)
    {
        int lodLevel = chunk->lodLevel;
        if (lodLevel < 0 || lodLevel >= static_cast<int>(chunk->vertexBuffers.size()))
            continue;

        if (!chunk->vertexBuffers[lodLevel] || !chunk->indexBuffers[lodLevel])
            continue;

        // 设置顶点缓冲区
        UINT stride = sizeof(Vertex);
        UINT offset = 0;
        ID3D11Buffer* vb = chunk->vertexBuffers[lodLevel].Get();
        context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);

        // 设置索引缓冲区
        context->IASetIndexBuffer(chunk->indexBuffers[lodLevel].Get(), DXGI_FORMAT_R32_UINT, 0);

        // 设置图元拓扑（已经在RenderTerrain中设置，但为了确保正确，这里也设置）
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // 绘制
        context->DrawIndexed(chunk->indexCounts[lodLevel], 0, 0);

        // 更新统计
        if (lodLevel < 8)
            m_renderStats.lodDistribution[lodLevel]++;
    }
}

float TerrainNew::GetHeightAt(float worldX, float worldZ) const
{
    if (m_heightData.empty())
        return 0.0f;

    // 转换世界坐标到高度图坐标
    float normX = (worldX + m_params.worldSizeX * 0.5f) / m_params.worldSizeX;
    float normZ = (worldZ + m_params.worldSizeZ * 0.5f) / m_params.worldSizeZ;

    if (normX < 0 || normX > 1 || normZ < 0 || normZ > 1)
        return 0.0f;

    float fx = normX * (m_heightmapWidth - 1);
    float fz = normZ * (m_heightmapHeight - 1);

    int x0 = static_cast<int>(fx);
    int z0 = static_cast<int>(fz);
    int x1 = std::min(x0 + 1, m_heightmapWidth - 1);
    int z1 = std::min(z0 + 1, m_heightmapHeight - 1);

    float xf = fx - x0;
    float zf = fz - z0;

    float h00 = m_heightData[z0 * m_heightmapWidth + x0];
    float h10 = m_heightData[z0 * m_heightmapWidth + x1];
    float h01 = m_heightData[z1 * m_heightmapWidth + x0];
    float h11 = m_heightData[z1 * m_heightmapWidth + x1];

    // 双线性插值
    float h = (h00 * (1 - xf) + h10 * xf) * (1 - zf) + (h01 * (1 - xf) + h11 * xf) * zf;

    return h * m_params.heightScale + m_params.heightOffset;
}
