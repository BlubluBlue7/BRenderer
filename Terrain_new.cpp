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

    // ========================================================================
    // 平滑噪声生成算法 - 使用改进的Perlin噪声和分形布朗运动
    // ========================================================================
    
    // 哈希函数 - 生成伪随机值
    auto hash = [](int x, int z, int seed = 0) -> float {
        int n = x + z * 57 + seed * 131;
        n = (n << 13) ^ n;
        return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7FFFFFFF) / 1073741824.0f);
    };
    
    // 平滑插值函数 - Quintic曲线（比smoothstep更平滑）
    auto smoothstep = [](float t) -> float {
        t = std::max(0.0f, std::min(1.0f, t));
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    };
    
    // 梯度噪声（类Perlin噪声）- 生成平滑的噪声值
    auto gradientNoise = [&hash, &smoothstep](float x, float z, float scale, int seed = 0) -> float {
        float scaledX = x * scale;
        float scaledZ = z * scale;
        
        int ix = static_cast<int>(floorf(scaledX));
        int iz = static_cast<int>(floorf(scaledZ));
        float fx = scaledX - ix;
        float fz = scaledZ - iz;
        
        // 获取四个角的值
        float n00 = hash(ix, iz, seed);
        float n10 = hash(ix + 1, iz, seed);
        float n01 = hash(ix, iz + 1, seed);
        float n11 = hash(ix + 1, iz + 1, seed);
        
        // 使用平滑插值
        float sx = smoothstep(fx);
        float sz = smoothstep(fz);
        
        // 双线性插值
        float top = n00 * (1.0f - sx) + n10 * sx;
        float bottom = n01 * (1.0f - sx) + n11 * sx;
        return top * (1.0f - sz) + bottom * sz;
    };
    
    // 分形布朗运动 (fBm) - 多层噪声叠加
    auto fbm = [&gradientNoise](float x, float z, int octaves, float scale, float lacunarity, float persistence, int seed = 0) -> float {
        float value = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        float maxValue = 0.0f;
        
        for (int i = 0; i < octaves; ++i)
        {
            value += gradientNoise(x, z, scale * frequency, seed + i) * amplitude;
            maxValue += amplitude;
            amplitude *= persistence;
            frequency *= lacunarity;
        }
        
        // 归一化到 [-1, 1]
        return value / maxValue;
    };
    
    // 生成平滑的地形高度
    float minH = FLT_MAX, maxH = -FLT_MAX;
    
    for (int z = 0; z < height; ++z)
    {
        for (int x = 0; x < width; ++x)
        {
            // 归一化坐标到0-1范围
            float fx = static_cast<float>(x) / (width - 1);
            float fz = static_cast<float>(z) / (height - 1);
            
            // 使用分形布朗运动生成平滑的基础地形
            // 大尺度地形 - 使用较少的octaves和较大的scale
            float baseHeight = fbm(fx, fz, 4, 2.0f, 2.0f, 0.5f, 0);
            baseHeight = (baseHeight + 1.0f) * 0.5f;  // 转换到 [0, 1]
            
            // 中尺度细节 - 添加丘陵和山谷
            float hills = fbm(fx, fz, 3, 6.0f, 2.0f, 0.45f, 100);
            hills = (hills + 1.0f) * 0.5f;
            
            // 小尺度细节 - 添加细微变化
            float detail = fbm(fx, fz, 2, 20.0f, 2.0f, 0.4f, 200);
            detail = (detail + 1.0f) * 0.5f;
            
            // 混合各层 - 使用权重控制
            float h = 0.0f;
            h += baseHeight * 0.5f;      // 基础地形 50%
            h += hills * 0.3f;           // 丘陵 30%
            h += detail * 0.2f;          // 细节 20%
            
            // 边缘衰减 - 让地形边缘平滑过渡到0
            float edgeFade = 1.0f;
            float edgeDist = 0.1f;  // 边缘距离（10%的区域）
            if (fx < edgeDist) edgeFade *= fx / edgeDist;
            if (fz < edgeDist) edgeFade *= fz / edgeDist;
            if (fx > 1.0f - edgeDist) edgeFade *= (1.0f - fx) / edgeDist;
            if (fz > 1.0f - edgeDist) edgeFade *= (1.0f - fz) / edgeDist;
            
            h *= edgeFade;
            
            // 确保在有效范围内
            h = std::max(0.0f, std::min(1.0f, h));
            
            m_heightData[z * width + x] = h;
            
            minH = std::min(minH, h);
            maxH = std::max(maxH, h);
        }
    }
    
    // 可选：对高度图进行平滑滤波（进一步减少突变）
    SmoothHeightmap(width, height);
    
    wchar_t msg[256];
    swprintf_s(msg, L"[TerrainNew] Procedural heightmap generated: %dx%d, height range [%.2f, %.2f]\n", 
               width, height, minH, maxH);
    OutputDebugStringW(msg);
}

void TerrainNew::SmoothHeightmap(int width, int height)
{
    if (m_heightData.empty() || width < 3 || height < 3)
        return;
    
    // 创建临时缓冲区存储平滑后的数据
    std::vector<float> smoothedData(width * height);
    
    // 使用简单的3x3高斯滤波核进行平滑
    // 权重: 中心4, 上下左右2, 四个角1
    for (int z = 1; z < height - 1; ++z)
    {
        for (int x = 1; x < width - 1; ++x)
        {
            float sum = 0.0f;
            float weight = 0.0f;
            
            // 3x3 高斯核
            for (int dz = -1; dz <= 1; ++dz)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    int nx = x + dx;
                    int nz = z + dz;
                    float w = (dx == 0 && dz == 0) ? 4.0f : 
                             ((dx == 0 || dz == 0) ? 2.0f : 1.0f);
                    
                    sum += m_heightData[nz * width + nx] * w;
                    weight += w;
                }
            }
            
            smoothedData[z * width + x] = sum / weight;
        }
    }
    
    // 保留边缘（不处理边界）
    for (int z = 0; z < height; ++z)
    {
        for (int x = 0; x < width; ++x)
        {
            if (x == 0 || x == width - 1 || z == 0 || z == height - 1)
            {
                smoothedData[z * width + x] = m_heightData[z * width + x];
            }
        }
    }
    
    // 应用平滑结果（混合原始和平滑数据，避免过度平滑）
    float blendFactor = 0.7f;  // 70%平滑，30%原始
    for (int i = 0; i < width * height; ++i)
    {
        m_heightData[i] = m_heightData[i] * (1.0f - blendFactor) + smoothedData[i] * blendFactor;
    }
}

bool TerrainNew::GenerateChunks(ID3D11Device* device)
{
    if (!device)
        return false;

    // 创建chunk常量缓冲区
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.ByteWidth = sizeof(float) * 16;  // 4个float4 (chunkParams, chunkBounds, terrainParams, heightParams)
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    
    HRESULT hr = device->CreateBuffer(&cbDesc, nullptr, m_chunkConstantBuffer.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[TerrainNew] Failed to create chunk constant buffer\n");
        return false;
    }

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

            // 纹理坐标：存储全局网格坐标（用于morphing和边界对齐）
            // 使用全局网格坐标确保相邻chunk的边界顶点对齐
            v.texCoord[0] = static_cast<float>(gridX);  // 全局网格X坐标
            v.texCoord[1] = static_cast<float>(gridZ);  // 全局网格Z坐标
            
            // 注意：texCoord存储的是全局网格坐标，不是局部坐标
            // 这样可以确保边界顶点在morphing时移动到相同的全局位置

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
        
        // 计算morphing因子（在切换到下一级LOD之前进行morphing）
        float morphFactor = CalculateMorphFactor(distance, lodLevel);
        
        // 如果morphFactor接近1.0，应该使用下一级LOD（但为了简化，我们仍然使用当前LOD并应用morphing）
        chunk.lodLevel = lodLevel;
        chunk.morphFactor = morphFactor;
        
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

float TerrainNew::CalculateMorphFactor(float distance, int lodLevel) const
{
    // CDLOD morphing：在LOD边界附近平滑过渡
    // morphFactor = 0: 完全使用当前LOD几何
    // morphFactor = 1: 完全morphed到下一个（更粗糙）LOD
    
    if (lodLevel >= m_params.maxLODLevels - 1)
        return 0.0f;  // 最低LOD级别，不需要morphing
    
    // 获取当前LOD的距离阈值
    float currentLODDist = m_params.lodDistances[lodLevel];
    
    // Morphing在距离阈值的指定比例处开始，在阈值处完成
    float morphStart = currentLODDist * m_params.morphStartRatio;
    float morphEnd = currentLODDist;
    
    if (distance <= morphStart)
        return 0.0f;  // 距离很近，不需要morphing
    
    // 如果距离超过morphEnd，应该已经切换到下一级LOD了
    // 但为了安全，我们仍然返回1.0（完全morphed）
    if (distance >= morphEnd)
        return 1.0f;  // 距离较远，完全morphed
    
    // 线性插值计算morphFactor
    float morphFactor = (distance - morphStart) / (morphEnd - morphStart);
    
    // 确保在有效范围内
    return std::max(0.0f, std::min(1.0f, morphFactor));
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

        // 计算当前LOD的网格大小
        int lodDivisor = 1 << lodLevel;
        int chunkGridSize = m_params.chunkSize / lodDivisor;
        chunkGridSize = std::max(2, chunkGridSize);

        // 更新chunk常量缓冲区（传递morphing参数）
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = context->Map(m_chunkConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr))
        {
            float* data = static_cast<float*>(mapped.pData);
            
            // chunkParams: x=morphFactor, y=lodLevel, z=chunkGridSize, w=unused
            data[0] = chunk->morphFactor;
            data[1] = static_cast<float>(lodLevel);
            data[2] = static_cast<float>(chunkGridSize);
            data[3] = 0.0f;
            
            // chunkBounds: x=minX, y=minZ, z=maxX, w=maxZ
            data[4] = chunk->minX;
            data[5] = chunk->minZ;
            data[6] = chunk->maxX;
            data[7] = chunk->maxZ;
            
            // terrainParams: x=worldSizeX, y=worldSizeZ, z=gridWidth-1, w=gridHeight-1
            data[8] = m_params.worldSizeX;
            data[9] = m_params.worldSizeZ;
            data[10] = static_cast<float>(m_params.gridWidth - 1);
            data[11] = static_cast<float>(m_params.gridHeight - 1);
            
            // heightParams: x=heightScale, y=heightOffset, z=1/(gridWidth-1), w=1/(gridHeight-1)
            data[12] = m_params.heightScale;
            data[13] = m_params.heightOffset;
            data[14] = 1.0f / (m_params.gridWidth - 1);
            data[15] = 1.0f / (m_params.gridHeight - 1);
            
            context->Unmap(m_chunkConstantBuffer.Get(), 0);
        }

        // 绑定chunk常量缓冲区到顶点着色器 slot 2
        ID3D11Buffer* cb_ptr = m_chunkConstantBuffer.Get();
        context->VSSetConstantBuffers(2, 1, &cb_ptr);

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
    
    // 调试输出（每300帧输出一次）
    static int frameCount = 0;
    if (++frameCount % 300 == 0)
    {
        float avgMorphFactor = 0.0f;
        int morphingChunks = 0;
        for (TerrainChunk* chunk : m_selectedChunks)
        {
            if (chunk->morphFactor > 0.001f)
            {
                avgMorphFactor += chunk->morphFactor;
                morphingChunks++;
            }
        }
        if (morphingChunks > 0)
        {
            avgMorphFactor /= morphingChunks;
            wchar_t msg[256];
            swprintf_s(msg, L"[TerrainNew] Morphing: %d chunks morphing, avg factor: %.3f\n",
                      morphingChunks, avgMorphFactor);
            OutputDebugStringW(msg);
        }
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
