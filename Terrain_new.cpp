#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Terrain_new.h"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <random>

#include "stb_image.h"
#include <d3dcompiler.h>
#include <d3d11_1.h>  // 用于ID3D11DeviceContext1

#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;

// ============================================================================
// TerrainNew 实现
// ============================================================================
TerrainNew::TerrainNew()
    : m_heightmapWidth(0)
    , m_heightmapHeight(0)
    , m_normalmapWidth(0)
    , m_normalmapHeight(0)
    , m_hasNormalmap(false)
    , m_chunkCountX(0)
    , m_chunkCountZ(0)
    , m_useGPUDriven(false)  // 默认使用CPU Driven，GPU Driven需要手动启用
    , m_showLODDebug(false)  // 默认不显示LOD调试（正常渲染模式）
    , m_showDepthDebug(false) // 默认不显示深度调试（正常渲染模式）
    , m_showShadowDebug(false) // 默认不显示阴影调试（正常渲染模式）
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
    m_hasNormalmap = false;  // 默认没有法线图

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
    
    // 如果提供了法线图路径，尝试加载
    if (!params.normalmapPath.empty())
    {
        if (!LoadNormalmap(params.normalmapPath))
        {
            OutputDebugStringW(L"[TerrainNew] Failed to load normalmap, will compute normals from heightmap.\n");
        }
    }

    // 生成共享的LOD索引
    if (!GenerateSharedLODIndices(device))
        return false;
    
    // 生成所有chunk的顶点数据
    if (!GenerateChunkVertices(device))
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

    // 生成共享的LOD索引
    if (!GenerateSharedLODIndices(device))
        return false;
    
    // 生成所有chunk的顶点数据
    if (!GenerateChunkVertices(device))
        return false;

    // 构建四叉树
    BuildQuadTree();
    
    // 创建GPU Driven资源（如果启用）
    if (m_useGPUDriven)
    {
        if (!CreateComputeShader(device))
        {
            OutputDebugStringW(L"[TerrainNew] Failed to create Compute Shader, GPU Driven disabled\n");
            m_useGPUDriven = false;
        }
        else if (!CreateUnifiedBuffers(device))
        {
            OutputDebugStringW(L"[TerrainNew] Failed to create unified buffers, GPU Driven disabled\n");
            m_useGPUDriven = false;
        }
        else if (!CreateGPUBuffers(device))
        {
            OutputDebugStringW(L"[TerrainNew] Failed to create GPU buffers, GPU Driven disabled\n");
            m_useGPUDriven = false;
        }
        else
        {
            OutputDebugStringW(L"[TerrainNew] GPU Driven resources created successfully\n");
        }
    }

    wchar_t msg[256];
    swprintf_s(msg, L"[TerrainNew] Procedural terrain created: %dx%d chunks, GPU Driven: %s\n",
               m_chunkCountX, m_chunkCountZ, m_useGPUDriven ? L"Enabled" : L"Disabled");
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

bool TerrainNew::LoadNormalmap(const std::wstring& path)
{
    if (path.empty())
        return false;

    // 转换宽字符路径到多字节
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string pathA(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, &pathA[0], size_needed, nullptr, nullptr);

    int width, height, channels;
    unsigned char* data = stbi_load(pathA.c_str(), &width, &height, &channels, 3);  // 加载RGB（3通道）

    if (!data)
    {
        OutputDebugStringW(L"[TerrainNew] Failed to load normalmap image\n");
        return false;
    }

    m_normalmapWidth = width;
    m_normalmapHeight = height;

    // 存储法线图数据（RGB，每个通道0-255）
    m_normalmapData.resize(width * height * 3);
    memcpy(m_normalmapData.data(), data, width * height * 3);

    stbi_image_free(data);

    m_hasNormalmap = true;

    wchar_t msg[256];
    swprintf_s(msg, L"[TerrainNew] Normalmap loaded: %dx%d\n", width, height);
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

bool TerrainNew::CreateHeightmapTexture(ID3D11Device* device)
{
    if (!device || m_heightData.empty())
        return false;
    
    // 创建高度图纹理（R32_FLOAT格式，存储归一化高度值）
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = m_heightmapWidth;
    texDesc.Height = m_heightmapHeight;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R32_FLOAT;  // 32位浮点格式，存储高度值
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = 0;
    
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = m_heightData.data();
    initData.SysMemPitch = m_heightmapWidth * sizeof(float);
    initData.SysMemSlicePitch = 0;
    
    HRESULT hr = device->CreateTexture2D(&texDesc, &initData, m_heightmapTexture.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[TerrainNew] Failed to create heightmap texture\n");
        return false;
    }
    
    // 创建Shader Resource View
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    
    hr = device->CreateShaderResourceView(m_heightmapTexture.Get(), &srvDesc, m_heightmapSRV.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[TerrainNew] Failed to create heightmap SRV\n");
        return false;
    }
    
    // 创建采样器状态（线性插值）
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MipLODBias = 0.0f;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    
    hr = device->CreateSamplerState(&samplerDesc, m_heightmapSampler.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[TerrainNew] Failed to create heightmap sampler\n");
        return false;
    }
    
    wchar_t msg[256];
    swprintf_s(msg, L"[TerrainNew] Heightmap texture created: %dx%d (R32_FLOAT)\n", 
               m_heightmapWidth, m_heightmapHeight);
    OutputDebugStringW(msg);
    
    return true;
}

bool TerrainNew::CreateNormalmapTexture(ID3D11Device* device)
{
    if (!device || m_normalmapData.empty() || !m_hasNormalmap)
        return false;
    
    // 创建法线图纹理（R8G8B8A8_UNORM格式，存储法线向量）
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = m_normalmapWidth;
    texDesc.Height = m_normalmapHeight;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // 8位RGBA格式
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = 0;
    
    // 将RGB数据转换为RGBA（添加Alpha通道，设为255）
    std::vector<unsigned char> rgbaData(m_normalmapWidth * m_normalmapHeight * 4);
    for (int i = 0; i < m_normalmapWidth * m_normalmapHeight; ++i)
    {
        rgbaData[i * 4 + 0] = m_normalmapData[i * 3 + 0];  // R
        rgbaData[i * 4 + 1] = m_normalmapData[i * 3 + 1];  // G
        rgbaData[i * 4 + 2] = m_normalmapData[i * 3 + 2];  // B
        rgbaData[i * 4 + 3] = 255;                          // A
    }
    
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = rgbaData.data();
    initData.SysMemPitch = m_normalmapWidth * 4;  // RGBA = 4字节
    initData.SysMemSlicePitch = 0;
    
    HRESULT hr = device->CreateTexture2D(&texDesc, &initData, m_normalmapTexture.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[TerrainNew] Failed to create normalmap texture\n");
        return false;
    }
    
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    
    hr = device->CreateShaderResourceView(m_normalmapTexture.Get(), &srvDesc, m_normalmapSRV.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[TerrainNew] Failed to create normalmap SRV\n");
        return false;
    }
    
    // 创建采样器（使用线性过滤和包裹模式）
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    
    hr = device->CreateSamplerState(&samplerDesc, m_normalmapSampler.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[TerrainNew] Failed to create normalmap sampler\n");
        return false;
    }
    
    wchar_t msg[256];
    swprintf_s(msg, L"[TerrainNew] Normalmap texture created: %dx%d (R8G8B8A8_UNORM)\n", 
               m_normalmapWidth, m_normalmapHeight);
    OutputDebugStringW(msg);
    
    return true;
}

bool TerrainNew::GenerateSharedLODIndices(ID3D11Device* device)
{
    if (!device)
        return false;

    // 创建chunk常量缓冲区（5个float4 = 20个float）
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.ByteWidth = sizeof(float) * 20;  // chunkParams + chunkBounds + terrainParams + heightParams + cameraParams
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    
    HRESULT hr = device->CreateBuffer(&cbDesc, nullptr, m_chunkConstantBuffer.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[TerrainNew] Failed to create chunk constant buffer\n");
        return false;
    }
    
    // 创建地形调试常量缓冲区（1个float4 = 4个float，对齐到16字节）
    D3D11_BUFFER_DESC debugCbDesc = {};
    debugCbDesc.Usage = D3D11_USAGE_DYNAMIC;
    debugCbDesc.ByteWidth = sizeof(float) * 4;  // showLODDebug + padding
    debugCbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    debugCbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    
    hr = device->CreateBuffer(&debugCbDesc, nullptr, m_terrainDebugBuffer.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[TerrainNew] Failed to create terrain debug constant buffer\n");
        return false;
    }
    
    // 创建高度图纹理（用于shader中动态采样）
    if (!CreateHeightmapTexture(device))
    {
        OutputDebugStringW(L"[TerrainNew] Failed to create heightmap texture\n");
        return false;
    }
    
    // 如果加载了法线图，创建法线图纹理
    if (m_hasNormalmap)
    {
        if (!CreateNormalmapTexture(device))
        {
            OutputDebugStringW(L"[TerrainNew] Failed to create normalmap texture\n");
            m_hasNormalmap = false;  // 标记为未使用法线图
        }
    }

    // 为每个LOD级别生成共享的索引
    m_sharedLODIndices.clear();
    m_sharedLODIndices.resize(m_params.maxLODLevels);

    for (int lod = 0; lod < m_params.maxLODLevels; ++lod)
    {
        int lodDivisor = 1 << lod;
        int gridSize = m_params.chunkSize / lodDivisor;
        gridSize = std::max(2, gridSize);

        std::vector<uint32_t> indices;
        GenerateLODIndicesTemplate(lod, gridSize, indices);

        if (!CreateLODIndexBuffer(device, m_sharedLODIndices[lod], indices))
        {
            OutputDebugStringW(L"[TerrainNew] Failed to create LOD index buffer\n");
            return false;
        }

        m_sharedLODIndices[lod].gridSize = gridSize;

        wchar_t msg[256];
        swprintf_s(msg, L"[TerrainNew] LOD %d indices: %d (gridSize: %d)\n",
                   lod, static_cast<int>(indices.size()), gridSize);
        OutputDebugStringW(msg);
    }
    
    return true;
}

bool TerrainNew::GenerateChunkVertices(ID3D11Device* device)
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

    // 为每个chunk生成所有LOD级别的顶点
    for (int z = 0; z < m_chunkCountZ; ++z)
    {
        for (int x = 0; x < m_chunkCountX; ++x)
        {
            TerrainChunk chunk;
            chunk.chunkX = x;
            chunk.chunkZ = z;
            chunk.lodLevel = 0;
            chunk.morphFactor = 0.0f;

            // 计算世界空间边界
            chunk.minX = -m_params.worldSizeX * 0.5f + x * chunkWorldSizeX;
            chunk.minZ = -m_params.worldSizeZ * 0.5f + z * chunkWorldSizeZ;
            chunk.maxX = chunk.minX + chunkWorldSizeX;
            chunk.maxZ = chunk.minZ + chunkWorldSizeZ;

            // 为每个LOD级别生成顶点
            chunk.vertexBuffers.resize(m_params.maxLODLevels);
            for (int lod = 0; lod < m_params.maxLODLevels; ++lod)
            {
                std::vector<Vertex> vertices;
                GenerateChunkLODVertices(x, z, lod, vertices);

                if (!CreateChunkVertexBuffer(device, chunk, lod, vertices))
                {
                    OutputDebugStringW(L"[TerrainNew] Failed to create chunk vertex buffer\n");
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

void TerrainNew::GenerateLODIndicesTemplate(int lodLevel, int gridSize, std::vector<uint32_t>& indices)
{
    indices.clear();

    // 生成索引（拓扑结构对所有chunk相同）
    for (int z = 0; z < gridSize; ++z)
    {
        for (int x = 0; x < gridSize; ++x)
        {
            uint32_t topLeft = z * (gridSize + 1) + x;
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = (z + 1) * (gridSize + 1) + x;
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

void TerrainNew::GenerateChunkLODVertices(int chunkX, int chunkZ, int lodLevel,
                                           std::vector<Vertex>& vertices)
{
    vertices.clear();

    // 计算LOD级别的网格分辨率
    int lodDivisor = 1 << lodLevel;
    int gridSize = m_params.chunkSize / lodDivisor;
    gridSize = std::max(2, gridSize);

    // 计算chunk的世界空间大小
    float chunkWorldSizeX = m_params.worldSizeX / m_chunkCountX;
    float chunkWorldSizeZ = m_params.worldSizeZ / m_chunkCountZ;

    // 计算顶点步长
    float stepX = chunkWorldSizeX / gridSize;
    float stepZ = chunkWorldSizeZ / gridSize;

    // 计算高度图采样步长
    float heightStepX = static_cast<float>(m_heightmapWidth - 1) / (m_params.gridWidth - 1);
    float heightStepZ = static_cast<float>(m_heightmapHeight - 1) / (m_params.gridHeight - 1);

    // 生成顶点
    for (int z = 0; z <= gridSize; ++z)
    {
        for (int x = 0; x <= gridSize; ++x)
        {
            Vertex v;

            // 计算全局网格坐标
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

            // ================================================================
            // 计算法线（完整精确版本，基于周围三角形的面法线）
            // ================================================================
            // 对于规则网格，每个顶点最多被6个三角形共享：
            //     NW  N
            //      |\ |
            //   W--[●]--E
            //      | \|
            //      S  SE
            // 我们计算所有相邻三角形的面法线，然后平均得到顶点法线
            
            DirectX::XMFLOAT3 accumulatedNormal(0.0f, 0.0f, 0.0f);
            int triangleCount = 0;
            
            // 辅助函数：采样指定网格位置的世界空间坐标
            auto sampleWorldPos = [&](int gx, int gz) -> DirectX::XMFLOAT3 {
                // 限制在有效范围内
                gx = std::max(0, std::min(gx, m_params.gridWidth - 1));
                gz = std::max(0, std::min(gz, m_params.gridHeight - 1));
                
                // 采样高度
                float hx = gx * heightStepX;
                float hz = gz * heightStepZ;
                int hx0 = static_cast<int>(hx);
                int hz0 = static_cast<int>(hz);
                int hx1 = std::min(hx0 + 1, m_heightmapWidth - 1);
                int hz1 = std::min(hz0 + 1, m_heightmapHeight - 1);
                float fx = hx - hx0;
                float fz = hz - hz0;
                
                float h00 = m_heightData[hz0 * m_heightmapWidth + hx0];
                float h10 = m_heightData[hz0 * m_heightmapWidth + hx1];
                float h01 = m_heightData[hz1 * m_heightmapWidth + hx0];
                float h11 = m_heightData[hz1 * m_heightmapWidth + hx1];
                
                float h = (h00 * (1 - fx) + h10 * fx) * (1 - fz) +
                         (h01 * (1 - fx) + h11 * fx) * fz;
                
                float wx = -m_params.worldSizeX * 0.5f + gx * (m_params.worldSizeX / (m_params.gridWidth - 1));
                float wy = h * m_params.heightScale + m_params.heightOffset;
                float wz = -m_params.worldSizeZ * 0.5f + gz * (m_params.worldSizeZ / (m_params.gridHeight - 1));
                
                return DirectX::XMFLOAT3(wx, wy, wz);
            };
            
            // 辅助函数：计算三角形面法线（使用叉积）
            auto computeFaceNormal = [](const DirectX::XMFLOAT3& p0, 
                                       const DirectX::XMFLOAT3& p1, 
                                       const DirectX::XMFLOAT3& p2) -> DirectX::XMFLOAT3 {
                // 计算两条边向量
                DirectX::XMVECTOR v0 = DirectX::XMLoadFloat3(&p0);
                DirectX::XMVECTOR v1 = DirectX::XMLoadFloat3(&p1);
                DirectX::XMVECTOR v2 = DirectX::XMLoadFloat3(&p2);
                
                DirectX::XMVECTOR edge1 = DirectX::XMVectorSubtract(v1, v0);
                DirectX::XMVECTOR edge2 = DirectX::XMVectorSubtract(v2, v0);
                
                // 叉积得到面法线（已经包含了面积权重）
                DirectX::XMVECTOR faceNormal = DirectX::XMVector3Cross(edge1, edge2);
                
                DirectX::XMFLOAT3 result;
                DirectX::XMStoreFloat3(&result, faceNormal);
                return result;
            };
            
            DirectX::XMFLOAT3 centerPos = sampleWorldPos(gridX, gridZ);
            
            // 计算周围8个三角形的面法线并累加
            // 注意：顶点顺序必须是逆时针（CCW），这样法线才会向上（Y+）
            // 在右手坐标系中，Y轴向上，X轴向右，Z轴向前（屏幕外）
            
            // 三角形1: 当前顶点 - 右侧 - 右上 (逆时针)
            if (gridX < m_params.gridWidth - 1 && gridZ < m_params.gridHeight - 1)
            {
                DirectX::XMFLOAT3 right = sampleWorldPos(gridX + 1, gridZ);
                DirectX::XMFLOAT3 rightUp = sampleWorldPos(gridX + 1, gridZ + 1);
                DirectX::XMFLOAT3 fn = computeFaceNormal(centerPos, rightUp, right);  // 修改顺序！
                accumulatedNormal.x += fn.x;
                accumulatedNormal.y += fn.y;
                accumulatedNormal.z += fn.z;
                triangleCount++;
            }
            
            // 三角形2: 当前顶点 - 右上 - 上方 (逆时针)
            if (gridX < m_params.gridWidth - 1 && gridZ < m_params.gridHeight - 1)
            {
                DirectX::XMFLOAT3 rightUp = sampleWorldPos(gridX + 1, gridZ + 1);
                DirectX::XMFLOAT3 up = sampleWorldPos(gridX, gridZ + 1);
                DirectX::XMFLOAT3 fn = computeFaceNormal(centerPos, up, rightUp);  // 修改顺序！
                accumulatedNormal.x += fn.x;
                accumulatedNormal.y += fn.y;
                accumulatedNormal.z += fn.z;
                triangleCount++;
            }
            
            // 三角形3: 当前顶点 - 上方 - 左上 (逆时针)
            if (gridX > 0 && gridZ < m_params.gridHeight - 1)
            {
                DirectX::XMFLOAT3 up = sampleWorldPos(gridX, gridZ + 1);
                DirectX::XMFLOAT3 leftUp = sampleWorldPos(gridX - 1, gridZ + 1);
                DirectX::XMFLOAT3 fn = computeFaceNormal(centerPos, leftUp, up);  // 修改顺序！
                accumulatedNormal.x += fn.x;
                accumulatedNormal.y += fn.y;
                accumulatedNormal.z += fn.z;
                triangleCount++;
            }
            
            // 三角形4: 当前顶点 - 左上 - 左侧 (逆时针)
            if (gridX > 0 && gridZ < m_params.gridHeight - 1)
            {
                DirectX::XMFLOAT3 leftUp = sampleWorldPos(gridX - 1, gridZ + 1);
                DirectX::XMFLOAT3 left = sampleWorldPos(gridX - 1, gridZ);
                DirectX::XMFLOAT3 fn = computeFaceNormal(centerPos, left, leftUp);  // 修改顺序！
                accumulatedNormal.x += fn.x;
                accumulatedNormal.y += fn.y;
                accumulatedNormal.z += fn.z;
                triangleCount++;
            }
            
            // 三角形5: 当前顶点 - 左侧 - 左下 (逆时针)
            if (gridX > 0 && gridZ > 0)
            {
                DirectX::XMFLOAT3 left = sampleWorldPos(gridX - 1, gridZ);
                DirectX::XMFLOAT3 leftDown = sampleWorldPos(gridX - 1, gridZ - 1);
                DirectX::XMFLOAT3 fn = computeFaceNormal(centerPos, leftDown, left);  // 修改顺序！
                accumulatedNormal.x += fn.x;
                accumulatedNormal.y += fn.y;
                accumulatedNormal.z += fn.z;
                triangleCount++;
            }
            
            // 三角形6: 当前顶点 - 左下 - 下方 (逆时针)
            if (gridX > 0 && gridZ > 0)
            {
                DirectX::XMFLOAT3 leftDown = sampleWorldPos(gridX - 1, gridZ - 1);
                DirectX::XMFLOAT3 down = sampleWorldPos(gridX, gridZ - 1);
                DirectX::XMFLOAT3 fn = computeFaceNormal(centerPos, down, leftDown);  // 修改顺序！
                accumulatedNormal.x += fn.x;
                accumulatedNormal.y += fn.y;
                accumulatedNormal.z += fn.z;
                triangleCount++;
            }
            
            // 三角形7: 当前顶点 - 下方 - 右下 (逆时针)
            if (gridX < m_params.gridWidth - 1 && gridZ > 0)
            {
                DirectX::XMFLOAT3 down = sampleWorldPos(gridX, gridZ - 1);
                DirectX::XMFLOAT3 rightDown = sampleWorldPos(gridX + 1, gridZ - 1);
                DirectX::XMFLOAT3 fn = computeFaceNormal(centerPos, rightDown, down);  // 修改顺序！
                accumulatedNormal.x += fn.x;
                accumulatedNormal.y += fn.y;
                accumulatedNormal.z += fn.z;
                triangleCount++;
            }
            
            // 三角形8: 当前顶点 - 右下 - 右侧 (逆时针)
            if (gridX < m_params.gridWidth - 1 && gridZ > 0)
            {
                DirectX::XMFLOAT3 rightDown = sampleWorldPos(gridX + 1, gridZ - 1);
                DirectX::XMFLOAT3 right = sampleWorldPos(gridX + 1, gridZ);
                DirectX::XMFLOAT3 fn = computeFaceNormal(centerPos, right, rightDown);  // 修改顺序！
                accumulatedNormal.x += fn.x;
                accumulatedNormal.y += fn.y;
                accumulatedNormal.z += fn.z;
                triangleCount++;
            }
            
            // 归一化累加的法线（叉积已经包含了面积权重，所以直接平均即可）
            DirectX::XMVECTOR normalVec = DirectX::XMLoadFloat3(&accumulatedNormal);
            normalVec = DirectX::XMVector3Normalize(normalVec);
            
            DirectX::XMFLOAT3 normal;
            DirectX::XMStoreFloat3(&normal, normalVec);
            
            // 存储到顶点
            v.normal[0] = normal.x;
            v.normal[1] = normal.y;
            v.normal[2] = normal.z;

            // 颜色
            // 根据LOD级别设置颜色（用于LOD可视化）
            // LOD 0 = 绿色, LOD 1 = 蓝色, LOD 2 = 黄色, LOD 3 = 红色
            static const float lodColors[4][3] = {
                {0.0f, 1.0f, 0.0f},  // LOD 0: 绿色
                {0.0f, 0.0f, 1.0f},  // LOD 1: 蓝色
                {1.0f, 1.0f, 0.0f},  // LOD 2: 黄色
                {1.0f, 0.0f, 0.0f}   // LOD 3: 红色
            };
            
            int colorLOD = std::min(lodLevel, 3);
            v.color[0] = lodColors[colorLOD][0];
            v.color[1] = lodColors[colorLOD][1];
            v.color[2] = lodColors[colorLOD][2];

            // 纹理坐标：存储全局网格坐标
            v.texCoord[0] = static_cast<float>(gridX);
            v.texCoord[1] = static_cast<float>(gridZ);

            vertices.push_back(v);
        }
    }
}

bool TerrainNew::CreateLODIndexBuffer(ID3D11Device* device, SharedLODIndices& lodIndices,
                                      const std::vector<uint32_t>& indices)
{
    if (!device || indices.empty())
        return false;

    lodIndices.indexCount = static_cast<UINT>(indices.size());

    // 创建索引缓冲区
    D3D11_BUFFER_DESC ibd = {};
    ibd.Usage = D3D11_USAGE_DEFAULT;
    ibd.ByteWidth = static_cast<UINT>(sizeof(uint32_t) * indices.size());
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA idata = {};
    idata.pSysMem = indices.data();

    HRESULT hr = device->CreateBuffer(&ibd, &idata, lodIndices.indexBuffer.GetAddressOf());
    return SUCCEEDED(hr);
}

bool TerrainNew::CreateChunkVertexBuffer(ID3D11Device* device, TerrainChunk& chunk, int lodLevel,
                                         const std::vector<Vertex>& vertices)
{
    if (!device || vertices.empty())
        return false;

    // 创建顶点缓冲区
    D3D11_BUFFER_DESC vbd = {};
    vbd.Usage = D3D11_USAGE_DEFAULT;
    vbd.ByteWidth = static_cast<UINT>(sizeof(Vertex) * vertices.size());
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vdata = {};
    vdata.pSysMem = vertices.data();

    HRESULT hr = device->CreateBuffer(&vbd, &vdata, chunk.vertexBuffers[lodLevel].GetAddressOf());
    return SUCCEEDED(hr);
}

void TerrainNew::BuildQuadTree()
{
    m_quadTree.clear();
    m_rootNodeIndices.clear();

    if (m_chunks.empty())
        return;

    // 计算地形的世界空间范围
    float minX = -m_params.worldSizeX * 0.5f;
    float minZ = -m_params.worldSizeZ * 0.5f;
    float maxX = m_params.worldSizeX * 0.5f;
    float maxZ = m_params.worldSizeZ * 0.5f;

    // 计算四叉树的最大深度
    // 深度应该使叶子节点大致对应1-4个chunk
    int maxDepth = 0;
    int chunkCount = std::max(m_chunkCountX, m_chunkCountZ);
    while ((1 << maxDepth) < chunkCount / 2)
        maxDepth++;
    maxDepth = std::max(2, std::min(maxDepth, 6));  // 限制在2-6层

    // 递归构建四叉树
    int rootIndex = BuildQuadTreeRecursive(minX, minZ, maxX, maxZ, 0, maxDepth);
    if (rootIndex >= 0)
    {
        m_rootNodeIndices.push_back(rootIndex);
    }

    // 统计叶子节点数量
    int leafCount = 0;
    for (const auto& node : m_quadTree)
    {
        if (node.isLeaf)
            leafCount++;
    }

    wchar_t msg[256];
    swprintf_s(msg, L"[TerrainNew] QuadTree built: %d total nodes, %d leaf nodes, max depth: %d\n",
               static_cast<int>(m_quadTree.size()), leafCount, maxDepth);
    OutputDebugStringW(msg);
}

int TerrainNew::BuildQuadTreeRecursive(float minX, float minZ, float maxX, float maxZ,
                                       int level, int maxDepth)
{
    QuadTreeNode node;
    node.minX = minX;
    node.minZ = minZ;
    node.maxX = maxX;
    node.maxZ = maxZ;
    node.centerX = (minX + maxX) * 0.5f;
    node.centerZ = (minZ + maxZ) * 0.5f;
    node.size = maxX - minX;
    node.level = level;
    // 注意：LOD级别不再基于树层级，而是在选择时基于距离动态计算
    node.hasChildren = false;
    node.isLeaf = false;

    // 计算该节点覆盖的chunk范围
    float chunkWorldSizeX = m_params.worldSizeX / m_chunkCountX;
    float chunkWorldSizeZ = m_params.worldSizeZ / m_chunkCountZ;

    node.chunkStartX = static_cast<int>((minX + m_params.worldSizeX * 0.5f) / chunkWorldSizeX);
    node.chunkStartZ = static_cast<int>((minZ + m_params.worldSizeZ * 0.5f) / chunkWorldSizeZ);
    node.chunkEndX = static_cast<int>((maxX + m_params.worldSizeX * 0.5f) / chunkWorldSizeX);
    node.chunkEndZ = static_cast<int>((maxZ + m_params.worldSizeZ * 0.5f) / chunkWorldSizeZ);

    // 限制在有效范围内
    node.chunkStartX = std::max(0, std::min(node.chunkStartX, m_chunkCountX - 1));
    node.chunkStartZ = std::max(0, std::min(node.chunkStartZ, m_chunkCountZ - 1));
    node.chunkEndX = std::max(0, std::min(node.chunkEndX, m_chunkCountX - 1));
    node.chunkEndZ = std::max(0, std::min(node.chunkEndZ, m_chunkCountZ - 1));

    // 计算高度范围
    CalculateNodeHeightRange(node);

    // 当前节点索引
    int currentIndex = static_cast<int>(m_quadTree.size());
    m_quadTree.push_back(node);

    // 判断是否需要继续细分
    bool shouldSubdivide = false;
    if (level < maxDepth)
    {
        int chunkWidth = node.chunkEndX - node.chunkStartX + 1;
        int chunkHeight = node.chunkEndZ - node.chunkStartZ + 1;
        
        // 如果节点覆盖多个chunk，继续细分
        if (chunkWidth > 1 || chunkHeight > 1)
            shouldSubdivide = true;
    }

    if (shouldSubdivide)
    {
        // 细分为4个子节点
        float midX = (minX + maxX) * 0.5f;
        float midZ = (minZ + maxZ) * 0.5f;

        // 子节点顺序：0=LeftTop, 1=RightTop, 2=LeftBottom, 3=RightBottom
        m_quadTree[currentIndex].childIndices[0] = BuildQuadTreeRecursive(minX, minZ, midX, midZ, level + 1, maxDepth);
        m_quadTree[currentIndex].childIndices[1] = BuildQuadTreeRecursive(midX, minZ, maxX, midZ, level + 1, maxDepth);
        m_quadTree[currentIndex].childIndices[2] = BuildQuadTreeRecursive(minX, midZ, midX, maxZ, level + 1, maxDepth);
        m_quadTree[currentIndex].childIndices[3] = BuildQuadTreeRecursive(midX, midZ, maxX, maxZ, level + 1, maxDepth);

        m_quadTree[currentIndex].hasChildren = true;
    }
    else
    {
        // 叶子节点
        m_quadTree[currentIndex].isLeaf = true;
    }

    return currentIndex;
}

void TerrainNew::CalculateNodeHeightRange(QuadTreeNode& node)
{
    node.minY = FLT_MAX;
    node.maxY = -FLT_MAX;

    // 遍历该节点覆盖的所有chunk，计算高度范围
    for (int z = node.chunkStartZ; z <= node.chunkEndZ; ++z)
    {
        for (int x = node.chunkStartX; x <= node.chunkEndX; ++x)
        {
            int chunkIndex = z * m_chunkCountX + x;
            if (chunkIndex >= 0 && chunkIndex < static_cast<int>(m_chunks.size()))
            {
                const TerrainChunk& chunk = m_chunks[chunkIndex];
                node.minY = std::min(node.minY, chunk.minY);
                node.maxY = std::max(node.maxY, chunk.maxY);
            }
        }
    }

    // 如果没有找到有效chunk，使用默认值
    if (node.minY == FLT_MAX)
    {
        node.minY = 0.0f;
        node.maxY = m_params.heightScale;
    }
}

void TerrainNew::SelectChunks(const DirectX::XMFLOAT3& cameraPosition, std::vector<TerrainChunk*>& outChunks)
{
    outChunks.clear();

    if (m_quadTree.empty() || m_rootNodeIndices.empty())
        return;

    // 计算视距（基于最远的LOD距离）
    float viewDistance = m_params.lodDistances[m_params.maxLODLevels - 1] * 1.5f;

    // 第一遍：从根节点开始递归选择，计算初始LOD
    for (int rootIndex : m_rootNodeIndices)
    {
        SelectChunksRecursive(rootIndex, cameraPosition, viewDistance, outChunks);
    }
    
    // 第二遍：应用邻居LOD约束（确保相邻chunk LOD差不超过1级）
    // 这需要多次迭代直到LOD稳定
    ApplyNeighborLODConstraints(outChunks);
    
    // 第三遍：重新计算morphing因子（基于约束后的LOD）
    for (TerrainChunk* chunk : outChunks)
    {
        float chunkDistXZ = chunk->GetDistanceToCameraXZ(cameraPosition.x, cameraPosition.z);
        chunk->morphFactor = CalculateMorphFactor(chunkDistXZ, chunk->lodLevel);
    }
}

void TerrainNew::SelectChunksRecursive(int nodeIndex, const DirectX::XMFLOAT3& cameraPosition,
                                       float viewDistance, std::vector<TerrainChunk*>& outChunks)
{
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_quadTree.size()))
        return;

    QuadTreeNode& node = m_quadTree[nodeIndex];

    // 1. 计算到相机的距离（使用XZ平面距离，更稳定）
    float distanceXZ = node.GetDistanceToCameraXZ(cameraPosition.x, cameraPosition.z);
    float distance3D = node.GetDistanceToCamera(cameraPosition.x, cameraPosition.y, cameraPosition.z);

    // 2. 距离剔除
    if (distanceXZ > viewDistance + node.size * 0.707f)
        return;

    // 3. 高度剔除
    if (cameraPosition.y < node.minY - 200.0f || cameraPosition.y > node.maxY + 500.0f)
        return;

    // 4. 判断是否应该使用子节点（更高细节）
    if (node.hasChildren && ShouldSubdivide(node, cameraPosition, viewDistance))
    {
        // 递归处理子节点
        for (int i = 0; i < 4; ++i)
        {
            if (node.childIndices[i] >= 0)
            {
                SelectChunksRecursive(node.childIndices[i], cameraPosition, viewDistance, outChunks);
            }
        }
    }
    else
    {
        // 5. 使用当前节点的chunk
        // 遍历该节点覆盖的所有chunk
        for (int z = node.chunkStartZ; z <= node.chunkEndZ; ++z)
        {
            for (int x = node.chunkStartX; x <= node.chunkEndX; ++x)
            {
                int chunkIndex = z * m_chunkCountX + x;
                if (chunkIndex >= 0 && chunkIndex < static_cast<int>(m_chunks.size()))
                {
                    TerrainChunk& chunk = m_chunks[chunkIndex];
                    
                    // 计算chunk到相机的XZ平面距离（更一致的LOD计算）
                    float chunkDistXZ = chunk.GetDistanceToCameraXZ(cameraPosition.x, cameraPosition.z);
                    
                    // 基于距离计算初始LOD级别
                    int lodLevel = CalculateLODLevel(chunkDistXZ);
                    
                    // 设置chunk的初始LOD（邻居约束将在后续统一处理）
                    chunk.lodLevel = lodLevel;
                    
                    // 确保LOD级别有效
                    if (lodLevel >= 0 && lodLevel < static_cast<int>(chunk.vertexBuffers.size()))
                    {
                        outChunks.push_back(&chunk);
                    }
                }
            }
        }
    }
}

bool TerrainNew::ShouldSubdivide(const QuadTreeNode& node, const DirectX::XMFLOAT3& cameraPosition,
                                 float viewDistance) const
{
    // 使用XZ平面距离来决定细分（忽略高度差，更稳定）
    float distance = node.GetDistanceToCameraXZ(cameraPosition.x, cameraPosition.z);
    
    // ================================================================
    // 基于屏幕空间误差的细分决策
    // ================================================================
    // 核心思想：
    // 1. 节点的"几何误差"与节点大小成正比
    // 2. 屏幕空间误差 = 几何误差 / 距离 * 屏幕系数
    // 3. 当屏幕空间误差超过阈值时，需要细分
    
    // 几何误差：节点越大，包含的地形细节越多，误差越大
    // 这里使用节点对角线长度的一半作为几何误差的估计
    float geometricError = node.size * 0.707f;  // sqrt(2)/2 ≈ 0.707
    
    // 屏幕空间误差系数（可调整）
    // 较小的值 = 更激进的细分（更高质量）
    // 较大的值 = 更保守的细分（更高性能）
    const float screenErrorThreshold = 50.0f;  // 像素误差阈值
    const float fovFactor = 1000.0f;  // 基于FOV和屏幕分辨率的估计系数
    
    // 计算屏幕空间误差
    // 当距离很近时，避免除零
    float safeDistance = std::max(1.0f, distance);
    float screenError = (geometricError / safeDistance) * fovFactor;
    
    // 方法1：基于屏幕空间误差
    bool subdivideByScreenError = screenError > screenErrorThreshold;
    
    // 方法2：基于LOD距离阈值（确保与chunk LOD计算一致）
    // 如果节点内最近点的LOD级别需要比节点能提供的更精细，则细分
    int requiredLOD = CalculateLODLevel(distance);
    
    // 节点能提供的最粗LOD（基于其覆盖的chunk数量）
    // 如果节点只覆盖1个chunk，不需要进一步细分
    int chunkWidth = node.chunkEndX - node.chunkStartX + 1;
    int chunkHeight = node.chunkEndZ - node.chunkStartZ + 1;
    bool hasMultipleChunks = (chunkWidth > 1 || chunkHeight > 1);
    
    // 组合决策：
    // - 如果节点覆盖多个chunk，且屏幕误差较大或需要高细节LOD，则细分
    // - 如果节点只覆盖1个chunk，不需要细分（已经是叶子）
    if (!hasMultipleChunks)
        return false;
    
    // 结合两种方法：屏幕误差驱动 + LOD距离驱动
    // 使用较保守的策略：只要任一条件满足就细分
    bool subdivideByLOD = (requiredLOD < m_params.maxLODLevels - 1) && 
                          (distance < m_params.lodDistances[std::max(0, requiredLOD)]);
    
    return subdivideByScreenError || subdivideByLOD;
}

int TerrainNew::CalculateLODLevel(float distance) const
{
    // CDLOD核心原则：
    // - 在距离范围 [0, threshold_i] 内使用 LOD i
    // - 但在接近threshold时开始morphing到下一级LOD
    // - morphing完成后（距离达到threshold），切换到下一级LOD
    
    for (int i = 0; i < m_params.maxLODLevels - 1; ++i)
    {
        // 如果距离小于当前LOD的阈值，使用当前LOD
        if (distance < m_params.lodDistances[i])
            return i;
    }
    
    // 距离超过所有阈值，使用最粗糙的LOD
    return m_params.maxLODLevels - 1;
}

float TerrainNew::CalculateMorphFactor(float distance, int lodLevel) const
{
    // CDLOD morphing原理：
    // 当使用LOD N时，在接近LOD N的距离阈值时，
    // 将LOD N中"多余的顶点"（在LOD N+1中不存在的）morph到LOD N+1的位置
    // 
    // morphFactor = 0: 完全使用LOD N的几何（无morphing）
    // morphFactor = 1: 多余顶点完全morphed到LOD N+1的位置（准备切换LOD）
    
    if (lodLevel >= m_params.maxLODLevels - 1)
        return 0.0f;  // 最粗糙的LOD，没有下一级可以morph了
    
    // 当前LOD的有效距离范围
    float lodStart = (lodLevel > 0) ? m_params.lodDistances[lodLevel - 1] : 0.0f;
    float lodEnd = m_params.lodDistances[lodLevel];
    
    // Morphing开始的距离（在LOD有效范围的后半段）
    float morphStart = lodStart + (lodEnd - lodStart) * m_params.morphStartRatio;
    float morphEnd = lodEnd;
    
    // 如果距离还没到morphing开始点，不需要morph
    if (distance <= morphStart)
        return 0.0f;
    
    // 如果距离超过morphing结束点，完全morphed（即将切换LOD）
    if (distance >= morphEnd)
        return 1.0f;
    
    // 在morphing范围内，线性插值
    float morphFactor = (distance - morphStart) / (morphEnd - morphStart);
    
    // 确保在有效范围内
    return std::max(0.0f, std::min(1.0f, morphFactor));
}

int TerrainNew::GetNeighborMaxLOD(int chunkX, int chunkZ, const std::vector<int>& lodMap) const
{
    // 检查8个方向的邻居（包括对角线）
    // 返回邻居中最精细的LOD（最小的数字）
    static const int neighborOffsets[8][2] = {
        { 0, -1},  // 上
        { 0,  1},  // 下
        {-1,  0},  // 左
        { 1,  0},  // 右
        {-1, -1},  // 左上
        { 1, -1},  // 右上
        {-1,  1},  // 左下
        { 1,  1}   // 右下
    };
    
    int minNeighborLOD = m_params.maxLODLevels - 1;  // 初始化为最粗糙的LOD
    
    for (int i = 0; i < 8; ++i)
    {
        int nx = chunkX + neighborOffsets[i][0];
        int nz = chunkZ + neighborOffsets[i][1];
        
        // 检查边界
        if (nx >= 0 && nx < m_chunkCountX && nz >= 0 && nz < m_chunkCountZ)
        {
            int neighborIndex = nz * m_chunkCountX + nx;
            if (neighborIndex >= 0 && neighborIndex < static_cast<int>(lodMap.size()))
            {
                int neighborLOD = lodMap[neighborIndex];
                if (neighborLOD >= 0)  // -1 表示未选中的chunk
                {
                    minNeighborLOD = std::min(minNeighborLOD, neighborLOD);
                }
            }
        }
    }
    
    return minNeighborLOD;
}

void TerrainNew::ApplyNeighborLODConstraints(std::vector<TerrainChunk*>& chunks)
{
    if (chunks.empty())
        return;
    
    // 创建LOD映射表（所有chunk的当前LOD，未选中的标记为-1）
    std::vector<int> lodMap(m_chunkCountX * m_chunkCountZ, -1);
    
    // 初始化LOD映射
    for (TerrainChunk* chunk : chunks)
    {
        int index = chunk->chunkZ * m_chunkCountX + chunk->chunkX;
        if (index >= 0 && index < static_cast<int>(lodMap.size()))
        {
            lodMap[index] = chunk->lodLevel;
        }
    }
    
    // 迭代应用邻居约束，直到LOD稳定
    // 约束规则：当前chunk的LOD不能比任何邻居粗糙超过1级
    // （即如果邻居LOD=1，当前chunk最多为LOD=2）
    const int maxIterations = m_params.maxLODLevels + 2;  // 防止无限循环
    
    for (int iteration = 0; iteration < maxIterations; ++iteration)
    {
        bool changed = false;
        
        for (TerrainChunk* chunk : chunks)
        {
            int index = chunk->chunkZ * m_chunkCountX + chunk->chunkX;
            int currentLOD = lodMap[index];
            
            // 获取邻居中最精细的LOD
            int minNeighborLOD = GetNeighborMaxLOD(chunk->chunkX, chunk->chunkZ, lodMap);
            
            // 约束：当前LOD不能比邻居的最精细LOD粗糙超过1级
            // 即：currentLOD <= minNeighborLOD + 1
            int constrainedLOD = std::min(currentLOD, minNeighborLOD + 1);
            
            if (constrainedLOD != currentLOD)
            {
                lodMap[index] = constrainedLOD;
                chunk->lodLevel = constrainedLOD;
                changed = true;
            }
        }
        
        // 如果没有变化，LOD已经稳定
        if (!changed)
            break;
    }
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
    if (!context || m_chunks.empty() || m_sharedLODIndices.empty())
        return;

    // 选择要渲染的chunk
    SelectChunks(cameraPosition, m_selectedChunks);

    // 重置统计
    m_renderStats = RenderStats();
    m_renderStats.visibleChunks = static_cast<int>(m_selectedChunks.size());
    
    // 绑定高度图纹理到顶点着色器
    if (m_heightmapSRV)
    {
        ID3D11ShaderResourceView* srv = m_heightmapSRV.Get();
        context->VSSetShaderResources(0, 1, &srv);
    }
    
    // 绑定高度图采样器到顶点着色器
    if (m_heightmapSampler)
    {
        ID3D11SamplerState* sampler = m_heightmapSampler.Get();
        context->VSSetSamplers(0, 1, &sampler);
    }
    
    // 绑定法线图（如果存在）
    if (m_hasNormalmap && m_normalmapSRV)
    {
        ID3D11ShaderResourceView* normalmapSRV = m_normalmapSRV.Get();
        context->VSSetShaderResources(1, 1, &normalmapSRV);
        
        if (m_normalmapSampler)
        {
            ID3D11SamplerState* normalmapSampler = m_normalmapSampler.Get();
            context->VSSetSamplers(1, 1, &normalmapSampler);
        }
    }

    // 渲染每个选中的chunk
    for (TerrainChunk* chunk : m_selectedChunks)
    {
        int lodLevel = chunk->lodLevel;
        
        // 检查LOD级别有效性
        if (lodLevel < 0 || lodLevel >= static_cast<int>(m_sharedLODIndices.size()))
            continue;
        
        if (lodLevel >= static_cast<int>(chunk->vertexBuffers.size()))
            continue;

        const SharedLODIndices& lodIndices = m_sharedLODIndices[lodLevel];
        if (!lodIndices.indexBuffer || !chunk->vertexBuffers[lodLevel])
            continue;

        // 计算全局网格起点
        int globalGridStartX = chunk->chunkX * m_params.chunkSize;
        int globalGridStartZ = chunk->chunkZ * m_params.chunkSize;
        
        // 计算chunk到相机的距离（用于统一的morphing计算）
        float chunkDistToCamera = chunk->GetDistanceToCamera(cameraPosition.x, cameraPosition.y, cameraPosition.z);

        // 更新chunk常量缓冲区
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = context->Map(m_chunkConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr))
        {
            float* data = static_cast<float*>(mapped.pData);
            
            // 计算morphing距离范围（基于当前LOD）
            // morphing是从当前LOD向下一级LOD过渡
            float lodStart = (lodLevel > 0) ? m_params.lodDistances[lodLevel - 1] : 0.0f;
            float lodEnd = (lodLevel < m_params.maxLODLevels - 1) ? 
                          m_params.lodDistances[lodLevel] : 
                          m_params.lodDistances[m_params.maxLODLevels - 1];
            
            float morphStart = lodStart + (lodEnd - lodStart) * m_params.morphStartRatio;
            float morphEnd = lodEnd;
            
            // chunkParams: x=chunkDistToCamera, y=lodLevel, z=morphStartDist, w=morphEndDist
            data[0] = chunkDistToCamera;  // 传递chunk到相机的距离（用于shader中计算morphFactor）
            data[1] = static_cast<float>(lodLevel);
            data[2] = morphStart;
            data[3] = morphEnd;
            
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
            
            // cameraParams: x=cameraPosX, y=cameraPosY, z=cameraPosZ, w=unused
            data[16] = cameraPosition.x;
            data[17] = cameraPosition.y;
            data[18] = cameraPosition.z;
            data[19] = 0.0f;
            
            context->Unmap(m_chunkConstantBuffer.Get(), 0);
        }

        // 绑定chunk常量缓冲区到顶点着色器 slot 2
        ID3D11Buffer* cb_ptr = m_chunkConstantBuffer.Get();
        context->VSSetConstantBuffers(2, 1, &cb_ptr);

        // 更新并绑定地形调试常量缓冲区到像素着色器 slot 3
        D3D11_MAPPED_SUBRESOURCE debugMapped;
        if (SUCCEEDED(context->Map(m_terrainDebugBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &debugMapped)))
        {
            float* debugData = static_cast<float*>(debugMapped.pData);
            debugData[0] = m_showLODDebug ? 1.0f : 0.0f;  // showLODDebug
            debugData[1] = m_showDepthDebug ? 1.0f : 0.0f; // showDepthDebug
            debugData[2] = m_showShadowDebug ? 1.0f : 0.0f; // showShadowDebug
            debugData[3] = 0.0f;  // padding
            context->Unmap(m_terrainDebugBuffer.Get(), 0);
        }
        ID3D11Buffer* debugCb_ptr = m_terrainDebugBuffer.Get();
        context->PSSetConstantBuffers(3, 1, &debugCb_ptr);

        // 设置顶点缓冲区（每个chunk独立）
        UINT stride = sizeof(Vertex);
        UINT offset = 0;
        ID3D11Buffer* vb = chunk->vertexBuffers[lodLevel].Get();
        context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);

        // 设置共享的索引缓冲区
        context->IASetIndexBuffer(lodIndices.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

        // 设置图元拓扑
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // 绘制
        context->DrawIndexed(lodIndices.indexCount, 0, 0);

        // 更新统计
        if (lodLevel < 8)
            m_renderStats.lodDistribution[lodLevel]++;
    }
    
    // 调试输出（每300帧输出一次）
    static int frameCount = 0;
    if (++frameCount % 300 == 0)
    {
        // 统计LOD分布
        int lodCounts[8] = {0};
        float avgMorphFactor = 0.0f;
        int morphingChunks = 0;
        
        for (TerrainChunk* chunk : m_selectedChunks)
        {
            if (chunk->lodLevel >= 0 && chunk->lodLevel < 8)
                lodCounts[chunk->lodLevel]++;
            
            if (chunk->morphFactor > 0.001f)
            {
                avgMorphFactor += chunk->morphFactor;
                morphingChunks++;
            }
        }
        
        wchar_t msg[512];
        swprintf_s(msg, L"[TerrainNew] QuadTree: %d visible chunks, LOD: [%d,%d,%d,%d], morphing: %d chunks (avg: %.3f)\n",
                  m_renderStats.visibleChunks,
                  lodCounts[0], lodCounts[1], lodCounts[2], lodCounts[3],
                  morphingChunks, morphingChunks > 0 ? avgMorphFactor / morphingChunks : 0.0f);
        OutputDebugStringW(msg);
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

// ============================================================================
// GPU Driven实现（简化版本）
// 注意：完整实现需要D3D11.1+支持DrawIndexedIndirect
// ============================================================================

bool TerrainNew::CreateComputeShader(ID3D11Device* device)
{
    if (!device)
        return false;
    
    // 尝试多个可能的路径加载Compute Shader
    std::vector<std::wstring> pathsToTry;
    wchar_t exePath[MAX_PATH] = {0};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0)
    {
        std::wstring exeDir = exePath;
        size_t lastSlash = exeDir.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos)
        {
            exeDir = exeDir.substr(0, lastSlash + 1);
            
            // 尝试项目根目录（向上两级）
            std::wstring projectRoot = exeDir;
            for (int i = 0; i < 2; ++i)
            {
                size_t slash = projectRoot.find_last_of(L"\\/", projectRoot.length() - 2);
                if (slash != std::wstring::npos)
                    projectRoot = projectRoot.substr(0, slash + 1);
            }
            
            pathsToTry.push_back(projectRoot + L"Shaders/TerrainCullCompute.hlsl");
            pathsToTry.push_back(exeDir + L"Shaders/TerrainCullCompute.hlsl");
        }
    }
    pathsToTry.push_back(L"Shaders/TerrainCullCompute.hlsl");
    
    Microsoft::WRL::ComPtr<ID3DBlob> csBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = E_FAIL;
    
    for (const auto& path : pathsToTry)
    {
        HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            CloseHandle(hFile);
            
            // 转换路径
            int size_needed = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string pathA(size_needed, 0);
            WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, &pathA[0], size_needed, nullptr, nullptr);
            
            // 读取文件
            hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile != INVALID_HANDLE_VALUE)
            {
                LARGE_INTEGER fileSize;
                if (GetFileSizeEx(hFile, &fileSize))
                {
                    std::vector<char> shaderCode(fileSize.QuadPart + 1);
                    DWORD bytesRead = 0;
                    if (ReadFile(hFile, shaderCode.data(), (DWORD)fileSize.QuadPart, &bytesRead, nullptr))
                    {
                        shaderCode[bytesRead] = '\0';
                        hr = D3DCompile(shaderCode.data(), bytesRead, pathA.c_str(), nullptr, nullptr, "CS", "cs_5_0", 0, 0, csBlob.GetAddressOf(), errorBlob.GetAddressOf());
                        if (SUCCEEDED(hr))
                        {
                            break;
                        }
                    }
                }
                CloseHandle(hFile);
            }
        }
    }
    
    if (FAILED(hr) || !csBlob)
    {
        if (errorBlob)
        {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        OutputDebugStringW(L"[TerrainNew] Failed to compile Compute Shader\n");
        return false;
    }
    
    hr = device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, m_cullComputeShader.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[TerrainNew] Failed to create Compute Shader\n");
        return false;
    }
    
    OutputDebugStringW(L"[TerrainNew] Compute Shader created successfully\n");
    return true;
}

bool TerrainNew::CreateGPUBuffers(ID3D11Device* device)
{
    if (!device || m_chunks.empty())
        return false;
    
    // 先创建统一缓冲区（如果还没创建）
    if (!m_unifiedVertexBuffer || !m_unifiedIndexBuffer)
    {
        if (!CreateUnifiedBuffers(device))
        {
            OutputDebugStringW(L"[TerrainNew] Failed to create unified buffers\n");
            return false;
        }
    }
    
    // 计算每个chunk每个LOD的偏移
    std::vector<UINT> vertexOffsets;
    std::vector<UINT> indexOffsets;
    vertexOffsets.reserve(m_chunks.size() * m_params.maxLODLevels);
    indexOffsets.reserve(m_chunks.size() * m_params.maxLODLevels);
    
    // 计算每个LOD级别的网格大小和顶点数量
    std::vector<int> lodGridSizes;
    std::vector<UINT> lodVertexCounts;
    std::vector<UINT> lodIndexCounts;
    lodGridSizes.reserve(m_params.maxLODLevels);
    lodVertexCounts.reserve(m_params.maxLODLevels);
    lodIndexCounts.reserve(m_params.maxLODLevels);
    
    for (int lod = 0; lod < m_params.maxLODLevels; ++lod)
    {
        int gridSize = m_params.chunkSize / (1 << lod);
        lodGridSizes.push_back(gridSize);
        lodVertexCounts.push_back((gridSize + 1) * (gridSize + 1));
        lodIndexCounts.push_back(m_sharedLODIndices[lod].indexCount);
    }
    
    // 计算每个chunk每个LOD的偏移
    UINT currentVertexOffset = 0;
    UINT currentIndexOffset = 0;
    
    for (size_t chunkIdx = 0; chunkIdx < m_chunks.size(); ++chunkIdx)
    {
        for (int lod = 0; lod < m_params.maxLODLevels; ++lod)
        {
            vertexOffsets.push_back(currentVertexOffset);
            indexOffsets.push_back(currentIndexOffset);
            
            currentVertexOffset += lodVertexCounts[lod];
            currentIndexOffset += lodIndexCounts[lod];
        }
    }
    
    // 创建chunk数据缓冲区（Structured Buffer）
    std::vector<TerrainChunkDataGPU> gpuChunkData;
    gpuChunkData.reserve(m_chunks.size());
    
    for (size_t i = 0; i < m_chunks.size(); ++i)
    {
        const auto& chunk = m_chunks[i];
        TerrainChunkDataGPU gpuChunk;
        gpuChunk.bounds = DirectX::XMFLOAT4(chunk.minX, chunk.minZ, chunk.maxX, chunk.maxZ);
        gpuChunk.heightRange = DirectX::XMFLOAT4(chunk.minY, chunk.maxY, 0.0f, 0.0f);
        gpuChunk.chunkIndex = DirectX::XMUINT2(chunk.chunkX, chunk.chunkZ);
        // 使用LOD 0的偏移（每个chunk有多个LOD，使用第一个LOD的偏移）
        gpuChunk.vertexBufferOffset = vertexOffsets[i * m_params.maxLODLevels];
        gpuChunk.indexBufferOffset = indexOffsets[i * m_params.maxLODLevels];
        gpuChunkData.push_back(gpuChunk);
    }
    
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = static_cast<UINT>(sizeof(TerrainChunkDataGPU) * gpuChunkData.size());
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(TerrainChunkDataGPU);
    
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = gpuChunkData.data();
    
    HRESULT hr = device->CreateBuffer(&bd, &initData, m_chunkDataBuffer.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    // 创建SRV
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
    srvDesc.BufferEx.FirstElement = 0;
    srvDesc.BufferEx.NumElements = static_cast<UINT>(gpuChunkData.size());
    
    hr = device->CreateShaderResourceView(m_chunkDataBuffer.Get(), &srvDesc, m_chunkDataSRV.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    // 创建LOD索引数量缓冲区（重用之前计算的lodIndexCounts）
    // 注意：lodIndexCounts已经在上面计算过了，直接使用
    bd.ByteWidth = static_cast<UINT>(sizeof(UINT) * lodIndexCounts.size());
    bd.StructureByteStride = sizeof(UINT);
    initData.pSysMem = lodIndexCounts.data();
    
    hr = device->CreateBuffer(&bd, &initData, m_lodIndexCountsBuffer.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    srvDesc.BufferEx.NumElements = static_cast<UINT>(lodIndexCounts.size());
    hr = device->CreateShaderResourceView(m_lodIndexCountsBuffer.Get(), &srvDesc, m_lodIndexCountsSRV.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    // 创建可见chunk索引缓冲区（UAV）
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = static_cast<UINT>(sizeof(UINT) * m_chunks.size());
    bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(UINT);
    bd.CPUAccessFlags = 0;
    
    hr = device->CreateBuffer(&bd, nullptr, m_visibleChunkBuffer.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = static_cast<UINT>(m_chunks.size());
    uavDesc.Buffer.Flags = 0;
    
    hr = device->CreateUnorderedAccessView(m_visibleChunkBuffer.Get(), &uavDesc, m_visibleChunkUAV.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    // 创建间接绘制参数缓冲区（UAV，需要支持间接绘制）
    bd.ByteWidth = static_cast<UINT>(sizeof(DrawIndexedIndirectArgs) * m_chunks.size());
    bd.StructureByteStride = sizeof(DrawIndexedIndirectArgs);
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED | D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;  // 添加间接绘制标志
    
    hr = device->CreateBuffer(&bd, nullptr, m_drawCommandsBuffer.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    uavDesc.Buffer.NumElements = static_cast<UINT>(m_chunks.size());
    hr = device->CreateUnorderedAccessView(m_drawCommandsBuffer.Get(), &uavDesc, m_drawCommandsUAV.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    // 创建chunk实例数据缓冲区（UAV，也可作为SRV供VS使用）
    bd.ByteWidth = static_cast<UINT>(sizeof(ChunkInstanceDataGPU) * m_chunks.size());
    bd.StructureByteStride = sizeof(ChunkInstanceDataGPU);
    
    hr = device->CreateBuffer(&bd, nullptr, m_chunkInstanceBuffer.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    uavDesc.Buffer.NumElements = static_cast<UINT>(m_chunks.size());
    hr = device->CreateUnorderedAccessView(m_chunkInstanceBuffer.Get(), &uavDesc, m_chunkInstanceUAV.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    srvDesc.BufferEx.NumElements = static_cast<UINT>(m_chunks.size());
    hr = device->CreateShaderResourceView(m_chunkInstanceBuffer.Get(), &srvDesc, m_chunkInstanceSRV.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    // 创建可见chunk计数缓冲区（UAV，单元素）
    bd.ByteWidth = sizeof(UINT);
    bd.StructureByteStride = sizeof(UINT);
    
    hr = device->CreateBuffer(&bd, nullptr, m_visibleCountBuffer.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    uavDesc.Buffer.NumElements = 1;
    hr = device->CreateUnorderedAccessView(m_visibleCountBuffer.Get(), &uavDesc, m_visibleCountUAV.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    // 创建Cull参数常量缓冲区
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth = (sizeof(DirectX::XMFLOAT4) * 8 + sizeof(float) + sizeof(UINT) * 2 + sizeof(float));  // frustumPlanes[6] + lodDistances + morphStartRatio + maxChunkCount + viewDistance + padding
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.MiscFlags = 0;
    bd.StructureByteStride = 0;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    
    hr = device->CreateBuffer(&bd, nullptr, m_cullParamsBuffer.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    OutputDebugStringW(L"[TerrainNew] GPU buffers created successfully\n");
    return true;
}

bool TerrainNew::CreateUnifiedBuffers(ID3D11Device* device)
{
    if (!device || m_chunks.empty() || m_sharedLODIndices.empty())
        return false;
    
    // 计算每个LOD级别的网格大小
    std::vector<int> lodGridSizes;
    lodGridSizes.reserve(m_params.maxLODLevels);
    for (int lod = 0; lod < m_params.maxLODLevels; ++lod)
    {
        int gridSize = m_params.chunkSize / (1 << lod);  // chunkSize / 2^lod
        lodGridSizes.push_back(gridSize);
    }
    
    // 计算每个LOD级别的顶点数量（每个chunk）
    std::vector<UINT> lodVertexCounts;
    lodVertexCounts.reserve(m_params.maxLODLevels);
    for (int lod = 0; lod < m_params.maxLODLevels; ++lod)
    {
        int gridSize = lodGridSizes[lod];
        UINT vertexCount = (gridSize + 1) * (gridSize + 1);  // (gridSize+1)^2 顶点
        lodVertexCounts.push_back(vertexCount);
    }
    
    // 计算总顶点数量（所有chunk的所有LOD）
    UINT totalVertexCount = 0;
    for (int lod = 0; lod < m_params.maxLODLevels; ++lod)
    {
        totalVertexCount += lodVertexCounts[lod] * static_cast<UINT>(m_chunks.size());
    }
    
    // 收集所有顶点数据
    std::vector<Vertex> allVertices;
    allVertices.reserve(totalVertexCount);
    
    // 计算每个chunk每个LOD在统一缓冲区中的偏移
    std::vector<UINT> vertexOffsets;
    vertexOffsets.reserve(m_chunks.size() * m_params.maxLODLevels);
    
    UINT currentVertexOffset = 0;
    for (const auto& chunk : m_chunks)
    {
        for (int lod = 0; lod < m_params.maxLODLevels; ++lod)
        {
            // 存储偏移
            vertexOffsets.push_back(currentVertexOffset);
            
            // 生成该chunk该LOD的顶点数据
            std::vector<Vertex> vertices;
            GenerateChunkLODVertices(chunk.chunkX, chunk.chunkZ, lod, vertices);
            
            // 添加到统一缓冲区
            allVertices.insert(allVertices.end(), vertices.begin(), vertices.end());
            
            currentVertexOffset += static_cast<UINT>(vertices.size());
        }
    }
    
    // 创建统一顶点缓冲区
    D3D11_BUFFER_DESC vbd = {};
    vbd.Usage = D3D11_USAGE_DEFAULT;
    vbd.ByteWidth = static_cast<UINT>(sizeof(Vertex) * allVertices.size());
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    
    D3D11_SUBRESOURCE_DATA vdata = {};
    vdata.pSysMem = allVertices.data();
    
    HRESULT hr = device->CreateBuffer(&vbd, &vdata, m_unifiedVertexBuffer.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    // 计算总索引数量（所有LOD）
    UINT totalIndexCount = 0;
    for (int lod = 0; lod < m_params.maxLODLevels; ++lod)
    {
        totalIndexCount += m_sharedLODIndices[lod].indexCount * static_cast<UINT>(m_chunks.size());
    }
    
    // 收集所有索引数据（每个chunk使用相同的LOD索引模板，但需要加上顶点偏移）
    std::vector<uint32_t> allIndices;
    allIndices.reserve(totalIndexCount);
    
    std::vector<UINT> indexOffsets;
    indexOffsets.reserve(m_chunks.size() * m_params.maxLODLevels);
    
    UINT currentIndexOffset = 0;
    for (size_t chunkIdx = 0; chunkIdx < m_chunks.size(); ++chunkIdx)
    {
        for (int lod = 0; lod < m_params.maxLODLevels; ++lod)
        {
            // 存储索引偏移
            indexOffsets.push_back(currentIndexOffset);
            
            // 获取该LOD的索引模板
            const SharedLODIndices& lodIndices = m_sharedLODIndices[lod];
            
            // 计算该chunk该LOD的顶点偏移
            UINT chunkLodVertexOffset = vertexOffsets[chunkIdx * m_params.maxLODLevels + lod];
            
            // 复制索引并加上顶点偏移
            std::vector<uint32_t> chunkIndices;
            chunkIndices.reserve(lodIndices.indexCount);
            
            // 需要从GPU读取索引数据，这里我们重新生成
            std::vector<uint32_t> lodIndicesTemplate;
            int gridSize = lodGridSizes[lod];
            GenerateLODIndicesTemplate(lod, gridSize, lodIndicesTemplate);
            
            for (uint32_t idx : lodIndicesTemplate)
            {
                chunkIndices.push_back(idx + chunkLodVertexOffset);
            }
            
            // 添加到统一索引缓冲区
            allIndices.insert(allIndices.end(), chunkIndices.begin(), chunkIndices.end());
            
            currentIndexOffset += static_cast<UINT>(chunkIndices.size());
        }
    }
    
    // 创建统一索引缓冲区
    D3D11_BUFFER_DESC ibd = {};
    ibd.Usage = D3D11_USAGE_DEFAULT;
    ibd.ByteWidth = static_cast<UINT>(sizeof(uint32_t) * allIndices.size());
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    
    D3D11_SUBRESOURCE_DATA idata = {};
    idata.pSysMem = allIndices.data();
    
    hr = device->CreateBuffer(&ibd, &idata, m_unifiedIndexBuffer.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    // 更新GPU chunk数据中的偏移（需要在CreateGPUBuffers之前调用，或者在这里更新）
    // 注意：这里我们只是创建缓冲区，偏移的更新需要在CreateGPUBuffers中处理
    // 或者我们需要在这里重新创建GPU chunk数据缓冲区
    
    wchar_t msg[256];
    swprintf_s(msg, L"[TerrainNew] Unified buffers created: %d vertices, %d indices\n",
               static_cast<int>(allVertices.size()), static_cast<int>(allIndices.size()));
    OutputDebugStringW(msg);
    
    return true;
}

void TerrainNew::UpdateCullParams(ID3D11DeviceContext* context, const DirectX::XMFLOAT3& cameraPosition,
                                   const DirectX::XMFLOAT4X4& viewMatrix, const DirectX::XMFLOAT4X4& projMatrix)
{
    if (!context || !m_cullParamsBuffer)
        return;
    
    // 从view和proj矩阵提取视锥体平面
    XMMATRIX view = XMLoadFloat4x4(&viewMatrix);
    XMMATRIX proj = XMLoadFloat4x4(&projMatrix);
    XMMATRIX viewProj = XMMatrixMultiply(view, proj);
    
    // 提取视锥体平面（使用Gribb-Hartmann方法）
    // 平面方程: ax + by + cz + d = 0，存储为(normal.xyz, d)
    DirectX::XMFLOAT4 frustumPlanes[6];
    
    // Left plane
    frustumPlanes[0].x = viewProj.r[0].m128_f32[3] + viewProj.r[0].m128_f32[0];
    frustumPlanes[0].y = viewProj.r[1].m128_f32[3] + viewProj.r[1].m128_f32[0];
    frustumPlanes[0].z = viewProj.r[2].m128_f32[3] + viewProj.r[2].m128_f32[0];
    frustumPlanes[0].w = viewProj.r[3].m128_f32[3] + viewProj.r[3].m128_f32[0];
    
    // Right plane
    frustumPlanes[1].x = viewProj.r[0].m128_f32[3] - viewProj.r[0].m128_f32[0];
    frustumPlanes[1].y = viewProj.r[1].m128_f32[3] - viewProj.r[1].m128_f32[0];
    frustumPlanes[1].z = viewProj.r[2].m128_f32[3] - viewProj.r[2].m128_f32[0];
    frustumPlanes[1].w = viewProj.r[3].m128_f32[3] - viewProj.r[3].m128_f32[0];
    
    // Bottom plane
    frustumPlanes[2].x = viewProj.r[0].m128_f32[3] + viewProj.r[0].m128_f32[1];
    frustumPlanes[2].y = viewProj.r[1].m128_f32[3] + viewProj.r[1].m128_f32[1];
    frustumPlanes[2].z = viewProj.r[2].m128_f32[3] + viewProj.r[2].m128_f32[1];
    frustumPlanes[2].w = viewProj.r[3].m128_f32[3] + viewProj.r[3].m128_f32[1];
    
    // Top plane
    frustumPlanes[3].x = viewProj.r[0].m128_f32[3] - viewProj.r[0].m128_f32[1];
    frustumPlanes[3].y = viewProj.r[1].m128_f32[3] - viewProj.r[1].m128_f32[1];
    frustumPlanes[3].z = viewProj.r[2].m128_f32[3] - viewProj.r[2].m128_f32[1];
    frustumPlanes[3].w = viewProj.r[3].m128_f32[3] - viewProj.r[3].m128_f32[1];
    
    // Near plane
    frustumPlanes[4].x = viewProj.r[0].m128_f32[3] + viewProj.r[0].m128_f32[2];
    frustumPlanes[4].y = viewProj.r[1].m128_f32[3] + viewProj.r[1].m128_f32[2];
    frustumPlanes[4].z = viewProj.r[2].m128_f32[3] + viewProj.r[2].m128_f32[2];
    frustumPlanes[4].w = viewProj.r[3].m128_f32[3] + viewProj.r[3].m128_f32[2];
    
    // Far plane
    frustumPlanes[5].x = viewProj.r[0].m128_f32[3] - viewProj.r[0].m128_f32[2];
    frustumPlanes[5].y = viewProj.r[1].m128_f32[3] - viewProj.r[1].m128_f32[2];
    frustumPlanes[5].z = viewProj.r[2].m128_f32[3] - viewProj.r[2].m128_f32[2];
    frustumPlanes[5].w = viewProj.r[3].m128_f32[3] - viewProj.r[3].m128_f32[2];
    
    // 归一化所有平面
    for (int i = 0; i < 6; ++i)
    {
        XMVECTOR plane = XMLoadFloat4(&frustumPlanes[i]);
        float length = XMVectorGetX(XMVector3Length(plane));
        if (length > 0.0001f)
        {
            plane = XMVectorScale(plane, 1.0f / length);
            XMStoreFloat4(&frustumPlanes[i], plane);
        }
    }
    
    // 计算相机方向（从view矩阵提取）
    XMMATRIX viewInv = XMMatrixInverse(nullptr, view);
    XMVECTOR cameraDirVec = viewInv.r[2];  // view矩阵的Z轴是相机的向前方向（在view空间是-Z）
    cameraDirVec = XMVectorNegate(cameraDirVec);  // 反转以得到世界空间的方向
    cameraDirVec = XMVector3Normalize(cameraDirVec);
    DirectX::XMFLOAT3 cameraDir;
    XMStoreFloat3(&cameraDir, cameraDirVec);
    
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = context->Map(m_cullParamsBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr))
    {
        // 注意：这个结构需要与Compute Shader中的CullParams匹配
        DirectX::XMFLOAT4* data = static_cast<DirectX::XMFLOAT4*>(mapped.pData);
        
        // cameraPos
        data[0] = DirectX::XMFLOAT4(cameraPosition.x, cameraPosition.y, cameraPosition.z, 0.0f);
        
        // cameraDir
        data[1] = DirectX::XMFLOAT4(cameraDir.x, cameraDir.y, cameraDir.z, 0.0f);
        
        // frustumPlanes[6]
        for (int i = 0; i < 6; ++i)
        {
            data[2 + i] = frustumPlanes[i];
        }
        
        // lodDistances
        data[8] = DirectX::XMFLOAT4(
            m_params.lodDistances[0],
            m_params.lodDistances[1],
            m_params.lodDistances[2],
            m_params.lodDistances[3]
        );
        
        // morphStartRatio, maxChunkCount, viewDistance, chunkSize, padding
        float* floatData = reinterpret_cast<float*>(&data[9]);
        floatData[0] = m_params.morphStartRatio;
        UINT* uintData = reinterpret_cast<UINT*>(&floatData[1]);
        uintData[0] = static_cast<UINT>(m_chunks.size());
        floatData[2] = m_params.lodDistances[m_params.maxLODLevels - 1] * 2.0f;
        uintData[1] = static_cast<UINT>(m_params.chunkSize);  // chunkSize
        uintData[2] = 0;  // padding
        uintData[3] = 0;  // padding
        
        context->Unmap(m_cullParamsBuffer.Get(), 0);
    }
}

void TerrainNew::RenderGPUDriven(ID3D11DeviceContext* context, const DirectX::XMFLOAT3& cameraPosition,
                                  const DirectX::XMFLOAT4X4& viewMatrix, const DirectX::XMFLOAT4X4& projMatrix)
{
    if (!context || !m_cullComputeShader || m_chunks.empty())
    {
        // 如果GPU Driven不可用，回退到CPU Driven
        Render(context, cameraPosition);
        return;
    }
    
    // 更新Cull参数
    UpdateCullParams(context, cameraPosition, viewMatrix, projMatrix);
    
    // ========================================================================
    // 阶段1：Dispatch Compute Shader - GPU端chunk选择和LOD计算
    // ========================================================================
    
    // 清空可见计数（通过Map/Unmap）
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = context->Map(m_visibleCountBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr))
    {
        UINT* count = static_cast<UINT*>(mapped.pData);
        *count = 0;
        context->Unmap(m_visibleCountBuffer.Get(), 0);
    }
    
    // 绑定Compute Shader
    context->CSSetShader(m_cullComputeShader.Get(), nullptr, 0);
    
    // 绑定常量缓冲区
    ID3D11Buffer* cullParamsCB = m_cullParamsBuffer.Get();
    context->CSSetConstantBuffers(0, 1, &cullParamsCB);
    
    // 绑定Structured Buffer作为SRV（输入）
    ID3D11ShaderResourceView* srvs[] = {
        m_chunkDataSRV.Get(),
        m_lodIndexCountsSRV.Get()
    };
    context->CSSetShaderResources(0, 2, srvs);
    
    // 绑定RW Structured Buffer作为UAV（输出）
    ID3D11UnorderedAccessView* uavs[] = {
        m_visibleChunkUAV.Get(),
        m_drawCommandsUAV.Get(),
        m_chunkInstanceUAV.Get(),
        m_visibleCountUAV.Get()
    };
    UINT initialCounts[] = { 0, 0, 0, 0 };
    context->CSSetUnorderedAccessViews(0, 4, uavs, initialCounts);
    
    // Dispatch Compute Shader
    UINT threadGroupCount = (static_cast<UINT>(m_chunks.size()) + 63) / 64;  // 每组64个线程
    context->Dispatch(threadGroupCount, 1, 1);
    
    // 清除UAV绑定（避免资源冲突）
    ID3D11UnorderedAccessView* nullUAVs[] = { nullptr, nullptr, nullptr, nullptr };
    context->CSSetUnorderedAccessViews(0, 4, nullUAVs, nullptr);
    
    ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr };
    context->CSSetShaderResources(0, 2, nullSRVs);
    
    context->CSSetShader(nullptr, nullptr, 0);
    
    // ========================================================================
    // 阶段2：尝试使用D3D11.1的DrawIndexedInstancedIndirect
    // ========================================================================
    
    // 尝试查询D3D11.1接口
    ID3D11DeviceContext1* context1Raw = nullptr;
    hr = context->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&context1Raw);
    Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context1(context1Raw);
    
    if (SUCCEEDED(hr) && context1Raw && m_unifiedVertexBuffer && m_unifiedIndexBuffer)
    {
        // D3D11.1支持，可以使用DrawIndexedInstancedIndirect
        
        // 绑定统一顶点和索引缓冲区
        UINT stride = sizeof(Vertex);
        UINT offset = 0;
        context->IASetVertexBuffers(0, 1, m_unifiedVertexBuffer.GetAddressOf(), &stride, &offset);
        context->IASetIndexBuffer(m_unifiedIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        
        // 绑定chunk实例数据到VS（作为SRV，register t2）
        context->VSSetShaderResources(2, 1, m_chunkInstanceSRV.GetAddressOf());
        
        // 绑定高度图纹理
        if (m_heightmapSRV)
        {
            ID3D11ShaderResourceView* heightmapSRV = m_heightmapSRV.Get();
            context->VSSetShaderResources(0, 1, &heightmapSRV);
        }
        if (m_heightmapSampler)
        {
            ID3D11SamplerState* sampler = m_heightmapSampler.Get();
            context->VSSetSamplers(0, 1, &sampler);
        }
        
        // 绑定法线图（如果存在）
        if (m_hasNormalmap && m_normalmapSRV)
        {
            ID3D11ShaderResourceView* normalmapSRV = m_normalmapSRV.Get();
            context->VSSetShaderResources(1, 1, &normalmapSRV);
            
            if (m_normalmapSampler)
            {
                ID3D11SamplerState* normalmapSampler = m_normalmapSampler.Get();
                context->VSSetSamplers(1, 1, &normalmapSampler);
            }
        }
        
        // 更新并绑定地形调试常量缓冲区到像素着色器 slot 3
        D3D11_MAPPED_SUBRESOURCE debugMapped;
        if (SUCCEEDED(context->Map(m_terrainDebugBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &debugMapped)))
        {
            float* debugData = static_cast<float*>(debugMapped.pData);
            debugData[0] = m_showLODDebug ? 1.0f : 0.0f;  // showLODDebug
            debugData[1] = m_showDepthDebug ? 1.0f : 0.0f; // showDepthDebug
            debugData[2] = m_showShadowDebug ? 1.0f : 0.0f; // showShadowDebug
            debugData[3] = 0.0f;  // padding
            context->Unmap(m_terrainDebugBuffer.Get(), 0);
        }
        ID3D11Buffer* debugCb_ptr = m_terrainDebugBuffer.Get();
        context->PSSetConstantBuffers(3, 1, &debugCb_ptr);

        // 执行间接绘制
        // 注意：DrawIndexedInstancedIndirect会自动从drawCommandsBuffer读取绘制参数
        context1Raw->DrawIndexedInstancedIndirect(m_drawCommandsBuffer.Get(), 0);
        
        // 清除SRV绑定
        ID3D11ShaderResourceView* nullSRVs2[] = { nullptr, nullptr, nullptr };
        context->VSSetShaderResources(0, 3, nullSRVs2);
        
        // 调试输出（每300帧输出一次）
        static int frameCount = 0;
        if (++frameCount % 300 == 0)
        {
            OutputDebugStringW(L"[TerrainNew] GPU Driven rendering using D3D11.1 DrawIndexedInstancedIndirect\n");
        }
    }
    else
    {
        // D3D11.1不支持或资源未准备好，回退到CPU Driven
        static bool warned = false;
        if (!warned)
        {
            if (!context1Raw)
                OutputDebugStringW(L"[TerrainNew] D3D11.1 not available (ID3D11DeviceContext1 query failed), falling back to CPU Driven\n");
            else if (!m_unifiedVertexBuffer || !m_unifiedIndexBuffer)
                OutputDebugStringW(L"[TerrainNew] Unified buffers not created, falling back to CPU Driven\n");
            warned = true;
        }
        Render(context, cameraPosition);
    }
}

void TerrainNew::UploadDataToGPU(ID3D11DeviceContext* context)
{
    // TODO: 上传数据到GPU（如果需要更新）
    // 当前数据在CreateGPUBuffers时已经上传
}
