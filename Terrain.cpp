// 确保在包含 Windows.h 相关头文件之前定义 NOMINMAX，避免 min/max 宏冲突
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Terrain.h"
#include "Mesh.h"  // 使用Vertex结构

#include <fstream>
#include <algorithm>
#include <cmath>

// stb_image 用于加载高度图
// 注意：STB_IMAGE_IMPLEMENTATION已经在Renderer.cpp中定义，这里只包含头文件
#include "stb_image.h"

using namespace DirectX;

Terrain::Terrain()
    : m_indexCount(0)
    , m_useCDLOD(false)
    , m_lodLocked(false)
    , m_lockedLODLevel(0)
    , m_patchSize(33)  // 33x33的块（32x32的四边形）
{
    m_params.width = 256;
    m_params.height = 256;
    m_params.sizeX = 400.0f;  // 扩大地形区域：从100增加到400
    m_params.sizeZ = 400.0f;  // 扩大地形区域：从100增加到400
    m_params.heightScale = 30.0f;  // 相应增加高度缩放
    m_params.heightOffset = 0.0f;
    
    // 初始化LOD距离阈值（根据地形大小调整）
    m_lodDistances[0] = 100.0f;   // LOD 0: 最近，最高细节
    m_lodDistances[1] = 200.0f;   // LOD 1
    m_lodDistances[2] = 400.0f;   // LOD 2
    m_lodDistances[3] = 1000.0f;  // LOD 3: 最远，最低细节
}

Terrain::~Terrain()
{
}

// 从高度图文件创建地形
bool Terrain::CreateFromHeightmap(ID3D11Device* device, const std::wstring& heightmapPath, const TerrainParams& params)
{
    if (!device)
        return false;
    
    m_params = params;
    
    // 加载高度图
    std::vector<float> heightData;
    if (!LoadHeightmap(heightmapPath, heightData))
        return false;
    
    // 生成地形网格
    GenerateTerrainMesh(heightData);
    
    // 创建DirectX缓冲区
    if (!CreateBuffers(device))
        return false;
    
    // 保存高度数据用于查询
    m_heightData = std::move(heightData);
    
    return true;
}

// 使用程序化高度数据创建地形（用于测试）
bool Terrain::CreateProcedural(ID3D11Device* device, const TerrainParams& params)
{
    if (!device)
        return false;
    
    m_params = params;
    
    // 生成程序化高度数据（使用多层噪声生成更自然的地形）
    std::vector<float> heightData(m_params.width * m_params.height);
    
    // 简单的伪随机函数（用于生成噪声）
    auto hash = [](int x, int z) -> float {
        // 简单的哈希函数，生成伪随机值
        x = ((x << 13) ^ x) * 1274126177;
        z = ((z << 13) ^ z) * 1274126177;
        return ((x * z) & 0x7FFFFFFF) / 2147483647.0f;
    };
    
    // 平滑插值函数
    auto smooth = [](float t) -> float {
        return t * t * (3.0f - 2.0f * t);
    };
    
    // 简单的噪声函数（基于网格的噪声）
    auto noise = [&hash, &smooth](float x, float z) -> float {
        int ix = (int)floorf(x);
        int iz = (int)floorf(z);
        float fx = x - ix;
        float fz = z - iz;
        
        // 双线性插值
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
            
            // 使用多层噪声（fractal noise）生成更自然的地形
            // 第一层：大尺度地形（低频）
            float height = noise(fx * 8.0f, fz * 8.0f) * 0.5f;
            
            // 第二层：中等尺度细节（中频）
            height += noise(fx * 16.0f, fz * 16.0f) * 0.25f;
            
            // 第三层：小尺度细节（高频）
            height += noise(fx * 32.0f, fz * 32.0f) * 0.125f;
            
            // 归一化到0-1范围
            height = height * 0.8f + 0.1f;  // 稍微调整范围，避免完全平坦
            height = fmaxf(0.0f, fminf(1.0f, height));  // 限制在0-1范围
            
            heightData[z * m_params.width + x] = height;
        }
    }
    
    // 生成地形网格
    GenerateTerrainMesh(heightData);
    
    // 创建DirectX缓冲区
    if (!CreateBuffers(device))
        return false;
    
    // 保存高度数据用于查询
    m_heightData = std::move(heightData);
    
    // 初始化CDLOD系统
    InitializeCDLOD(device);
    
    return true;
}

// 加载高度图文件
bool Terrain::LoadHeightmap(const std::wstring& path, std::vector<float>& heightData)
{
    if (path.empty())
        return false;
    
    // 转换宽字符串为多字节字符串
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string pathA(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, &pathA[0], size_needed, nullptr, nullptr);
    
    // 使用stb_image加载图像（灰度图）
    int width, height, channels;
    unsigned char* data = stbi_load(pathA.c_str(), &width, &height, &channels, 1);  // 只加载一个通道（灰度）
    
    if (!data)
    {
        // 如果加载失败，尝试加载为16位图像
        // stb_image也支持16位，但需要特殊处理
        return false;
    }
    
    // 检查尺寸是否匹配
    if (width != m_params.width || height != m_params.height)
    {
        // 可以在这里实现缩放，暂时要求精确匹配
        stbi_image_free(data);
        return false;
    }
    
    // 将8位数据转换为0.0-1.0范围的浮点数
    heightData.resize(m_params.width * m_params.height);
    for (int i = 0; i < m_params.width * m_params.height; ++i)
    {
        heightData[i] = (float)data[i] / 255.0f;
    }
    
    stbi_image_free(data);
    return true;
}

// 从高度数据生成地形网格
void Terrain::GenerateTerrainMesh(const std::vector<float>& heightData)
{
    m_vertices.clear();
    m_indices.clear();
    
    // 计算每个顶点的步长（世界空间）
    float stepX = m_params.sizeX / (float)(m_params.width - 1);
    float stepZ = m_params.sizeZ / (float)(m_params.height - 1);
    
    // 生成顶点
    for (int z = 0; z < m_params.height; ++z)
    {
        for (int x = 0; x < m_params.width; ++x)
        {
            Vertex vertex;
            
            // 位置（世界空间）
            float worldX = (float)x * stepX - m_params.sizeX * 0.5f;
            float worldZ = (float)z * stepZ - m_params.sizeZ * 0.5f;
            float worldY = heightData[z * m_params.width + x] * m_params.heightScale + m_params.heightOffset;
            
            vertex.position[0] = worldX;
            vertex.position[1] = worldY;
            vertex.position[2] = worldZ;
            
            // 法线（稍后计算）
            vertex.normal[0] = 0.0f;
            vertex.normal[1] = 1.0f;
            vertex.normal[2] = 0.0f;
            
            // 颜色（可以使用高度来着色，暂时使用白色）
            vertex.color[0] = 1.0f;
            vertex.color[1] = 1.0f;
            vertex.color[2] = 1.0f;
            
            // 纹理坐标（根据地形大小和纹理平铺次数计算）
            // 增加纹理重复次数，让纹理在地形上重复多次，看起来更自然
            float texScale = 16.0f;  // 纹理重复16次（可以根据地形大小调整）
            vertex.texCoord[0] = (float)x / (float)(m_params.width - 1) * texScale;
            vertex.texCoord[1] = (float)z / (float)(m_params.height - 1) * texScale;
            
            m_vertices.push_back(vertex);
        }
    }
    
    // 生成索引（两个三角形组成一个四边形）
    for (int z = 0; z < m_params.height - 1; ++z)
    {
        for (int x = 0; x < m_params.width - 1; ++x)
        {
            // 当前四边形的四个顶点索引
            uint32_t topLeft = z * m_params.width + x;
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = (z + 1) * m_params.width + x;
            uint32_t bottomRight = bottomLeft + 1;
            
            // 第一个三角形（逆时针顺序：左上、左下、右上）
            // DirectX默认逆时针为正面，所以需要逆时针顺序
            m_indices.push_back(topLeft);
            m_indices.push_back(bottomLeft);
            m_indices.push_back(topRight);
            
            // 第二个三角形（逆时针顺序：右上、左下、右下）
            m_indices.push_back(topRight);
            m_indices.push_back(bottomLeft);
            m_indices.push_back(bottomRight);
        }
    }
    
    // 计算法线
    CalculateNormals();
}

// 计算法线向量
void Terrain::CalculateNormals()
{
    // 初始化法线为零向量
    std::vector<XMFLOAT3> normals(m_vertices.size(), XMFLOAT3(0.0f, 0.0f, 0.0f));
    
    // 遍历所有三角形，累加每个顶点的法线
    for (size_t i = 0; i < m_indices.size(); i += 3)
    {
        uint32_t i0 = m_indices[i];
        uint32_t i1 = m_indices[i + 1];
        uint32_t i2 = m_indices[i + 2];
        
        // 获取三个顶点
        XMVECTOR v0 = XMVectorSet(m_vertices[i0].position[0], m_vertices[i0].position[1], m_vertices[i0].position[2], 0.0f);
        XMVECTOR v1 = XMVectorSet(m_vertices[i1].position[0], m_vertices[i1].position[1], m_vertices[i1].position[2], 0.0f);
        XMVECTOR v2 = XMVectorSet(m_vertices[i2].position[0], m_vertices[i2].position[1], m_vertices[i2].position[2], 0.0f);
        
        // 计算三角形的两个边向量
        XMVECTOR edge1 = XMVectorSubtract(v1, v0);
        XMVECTOR edge2 = XMVectorSubtract(v2, v0);
        
        // 计算法线（叉积）
        XMVECTOR normal = XMVector3Cross(edge1, edge2);
        normal = XMVector3Normalize(normal);
        
        // 累加到三个顶点的法线
        XMFLOAT3 n;
        XMStoreFloat3(&n, normal);
        normals[i0].x += n.x;
        normals[i0].y += n.y;
        normals[i0].z += n.z;
        normals[i1].x += n.x;
        normals[i1].y += n.y;
        normals[i1].z += n.z;
        normals[i2].x += n.x;
        normals[i2].y += n.y;
        normals[i2].z += n.z;
    }
    
    // 归一化所有法线
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

// 创建DirectX缓冲区
bool Terrain::CreateBuffers(ID3D11Device* device)
{
    if (!device || m_vertices.empty() || m_indices.empty())
        return false;
    
    // 创建顶点缓冲区
    D3D11_BUFFER_DESC vbd = {};
    vbd.Usage = D3D11_USAGE_DEFAULT;
    vbd.ByteWidth = (UINT)(sizeof(Vertex) * m_vertices.size());
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbd.CPUAccessFlags = 0;
    
    D3D11_SUBRESOURCE_DATA vinitData = {};
    vinitData.pSysMem = m_vertices.data();
    
    HRESULT hr = device->CreateBuffer(&vbd, &vinitData, m_vertexBuffer.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    // 创建索引缓冲区
    D3D11_BUFFER_DESC ibd = {};
    ibd.Usage = D3D11_USAGE_DEFAULT;
    ibd.ByteWidth = (UINT)(sizeof(uint32_t) * m_indices.size());
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    ibd.CPUAccessFlags = 0;
    
    D3D11_SUBRESOURCE_DATA iinitData = {};
    iinitData.pSysMem = m_indices.data();
    
    hr = device->CreateBuffer(&ibd, &iinitData, m_indexBuffer.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    m_indexCount = (UINT)m_indices.size();
    
    return true;
}

// 渲染地形
void Terrain::Render(ID3D11DeviceContext* context)
{
    if (!context || !m_vertexBuffer || !m_indexBuffer)
    {
        static bool warned = false;
        if (!warned)
        {
            OutputDebugStringW(L"[TERRAIN DEBUG] Terrain::Render: Missing resources!\n");
            if (!context) OutputDebugStringW(L"  - Context is null\n");
            if (!m_vertexBuffer) OutputDebugStringW(L"  - Vertex buffer is null\n");
            if (!m_indexBuffer) OutputDebugStringW(L"  - Index buffer is null\n");
            warned = true;
        }
        return;
    }
    
    // 设置顶点缓冲区
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    
    // 设置索引缓冲区
    context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    
    // 设置图元类型
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    
    // 调试：确认DrawIndexed被调用
    static bool drawLogged = false;
    if (!drawLogged)
    {
        wchar_t msg[256];
        swprintf_s(msg, L"[TERRAIN DEBUG] Terrain::Render: Calling DrawIndexed(%d, 0, 0)\n", m_indexCount);
        OutputDebugStringW(msg);
        drawLogged = true;
    }
    
    // 绘制
    context->DrawIndexed(m_indexCount, 0, 0);
}

// 获取世界坐标处的地形高度
float Terrain::GetHeightAt(float worldX, float worldZ) const
{
    if (m_heightData.empty())
        return 0.0f;
    
    // 转换世界坐标到高度图坐标
    float localX = worldX + m_params.sizeX * 0.5f;
    float localZ = worldZ + m_params.sizeZ * 0.5f;
    
    float fx = localX / m_params.sizeX * (m_params.width - 1);
    float fz = localZ / m_params.sizeZ * (m_params.height - 1);
    
    // 检查边界
    if (fx < 0.0f || fx >= m_params.width - 1 || fz < 0.0f || fz >= m_params.height - 1)
        return 0.0f;
    
    // 双线性插值
    int x0 = (int)fx;
    int z0 = (int)fz;
    int x1 = x0 + 1;
    int z1 = z0 + 1;
    
    float fx_frac = fx - x0;
    float fz_frac = fz - z0;
    
    // 限制在有效范围内
    x1 = (x1 >= m_params.width) ? m_params.width - 1 : x1;
    z1 = (z1 >= m_params.height) ? m_params.height - 1 : z1;
    
    // 四个角的高度值
    float h00 = m_heightData[z0 * m_params.width + x0];
    float h10 = m_heightData[z0 * m_params.width + x1];
    float h01 = m_heightData[z1 * m_params.width + x0];
    float h11 = m_heightData[z1 * m_params.width + x1];
    
    // 双线性插值
    float h0 = h00 * (1.0f - fx_frac) + h10 * fx_frac;
    float h1 = h01 * (1.0f - fx_frac) + h11 * fx_frac;
    float height = h0 * (1.0f - fz_frac) + h1 * fz_frac;
    
    // 应用缩放和偏移
    return height * m_params.heightScale + m_params.heightOffset;
}

// ============================================================================
// CDLOD系统实现
// ============================================================================

// 初始化CDLOD系统
void Terrain::InitializeCDLOD(ID3D11Device* device)
{
    if (!device)
        return;
    
    m_useCDLOD = true;
    
    // 生成地形块（必须先生成块，才能为每个块生成索引）
    GeneratePatches();
    
    // 为每个块生成多级LOD索引
    GeneratePatchIndices(device);
    
    OutputDebugStringW(L"[TERRAIN DEBUG] CDLOD system initialized.\n");
}

// 为每个块生成多级LOD索引
void Terrain::GeneratePatchIndices(ID3D11Device* device)
{
    if (!device || m_vertices.empty() || m_patches.empty())
        return;
    
    // 为所有块的所有LOD级别生成一个大的索引缓冲区
    std::vector<std::vector<uint32_t>> allLODIndices(MAX_LOD_LEVELS);
    
    // 为每个块生成每个LOD级别的索引
    for (auto& patch : m_patches)
    {
        for (int lod = 0; lod < MAX_LOD_LEVELS; ++lod)
        {
            int step = 1 << lod;  // LOD 0: step=1, LOD 1: step=2, LOD 2: step=4, LOD 3: step=8
            
            // 记录当前LOD级别的起始索引位置
            patch.lodRanges[lod].indexStart = (UINT)allLODIndices[lod].size();
            
            // 为这个块生成索引（只在块内部，不跨越边界）
            int blockStartX = patch.startX;
            int blockEndX = patch.endX;
            int blockStartZ = patch.startZ;
            int blockEndZ = patch.endZ;
            
            // 生成块内的索引（确保不跨越块边界）
            // CDLOD标准做法：边界区域总是使用最细步长（step=1），内部区域使用当前LOD的步长
            // 这样可以确保与任何相邻LOD级别都能无缝连接
            
            // 边界步长：总是使用最细步长，确保与任何LOD级别都能无缝连接
            const int boundaryStep = 1;
            
            // 1. 生成边界区域的三角形（使用step=1）
            // 顶部边界行
            if (blockStartZ < blockEndZ)
            {
                for (int x = blockStartX; x < blockEndX; x += boundaryStep)
                {
                    if (x + boundaryStep > blockEndX)
                        break;
                    
                    int z = blockStartZ;
                    if (z + boundaryStep > blockEndZ)
                        break;
                    
                    uint32_t topLeft = z * m_params.width + x;
                    uint32_t topRight = z * m_params.width + (x + boundaryStep);
                    uint32_t bottomLeft = (z + boundaryStep) * m_params.width + x;
                    uint32_t bottomRight = (z + boundaryStep) * m_params.width + (x + boundaryStep);
                    
                    if (topRight < m_vertices.size() && 
                        bottomLeft < m_vertices.size() && 
                        bottomRight < m_vertices.size())
                    {
                        allLODIndices[lod].push_back(topLeft);
                        allLODIndices[lod].push_back(bottomLeft);
                        allLODIndices[lod].push_back(topRight);
                        
                        allLODIndices[lod].push_back(topRight);
                        allLODIndices[lod].push_back(bottomLeft);
                        allLODIndices[lod].push_back(bottomRight);
                    }
                }
            }
            
            // 底部边界行（最后一行，从blockEndZ-1开始向上）
            // blockEndZ是结束顶点坐标（包含），所以最后一个顶点的索引是blockEndZ
            // 底部边界行应该覆盖从blockEndZ-1到blockEndZ的区域
            if (blockEndZ > blockStartZ + boundaryStep)
            {
                for (int x = blockStartX; x < blockEndX; x += boundaryStep)
                {
                    if (x + boundaryStep > blockEndX)
                        break;
                    
                    // 底部边界：从blockEndZ-1向上，确保不超出范围
                    int z = blockEndZ - boundaryStep;
                    if (z < blockStartZ || z + boundaryStep > blockEndZ)
                        continue;  // 跳过无效的边界
                    
                    uint32_t topLeft = z * m_params.width + x;
                    uint32_t topRight = z * m_params.width + (x + boundaryStep);
                    uint32_t bottomLeft = (z + boundaryStep) * m_params.width + x;
                    uint32_t bottomRight = (z + boundaryStep) * m_params.width + (x + boundaryStep);
                    
                    if (topRight < m_vertices.size() && 
                        bottomLeft < m_vertices.size() && 
                        bottomRight < m_vertices.size())
                    {
                        allLODIndices[lod].push_back(topLeft);
                        allLODIndices[lod].push_back(bottomLeft);
                        allLODIndices[lod].push_back(topRight);
                        
                        allLODIndices[lod].push_back(topRight);
                        allLODIndices[lod].push_back(bottomLeft);
                        allLODIndices[lod].push_back(bottomRight);
                    }
                }
            }
            
            // 左侧边界列（不包括已处理的顶部和底部顶点）
            if (blockStartX < blockEndX)
            {
                for (int z = blockStartZ + boundaryStep; z < blockEndZ - boundaryStep; z += boundaryStep)
                {
                    if (z + boundaryStep > blockEndZ - boundaryStep)
                        break;
                    
                    int x = blockStartX;
                    
                    uint32_t topLeft = z * m_params.width + x;
                    uint32_t topRight = z * m_params.width + (x + boundaryStep);
                    uint32_t bottomLeft = (z + boundaryStep) * m_params.width + x;
                    uint32_t bottomRight = (z + boundaryStep) * m_params.width + (x + boundaryStep);
                    
                    if (topRight < m_vertices.size() && 
                        bottomLeft < m_vertices.size() && 
                        bottomRight < m_vertices.size())
                    {
                        allLODIndices[lod].push_back(topLeft);
                        allLODIndices[lod].push_back(bottomLeft);
                        allLODIndices[lod].push_back(topRight);
                        
                        allLODIndices[lod].push_back(topRight);
                        allLODIndices[lod].push_back(bottomLeft);
                        allLODIndices[lod].push_back(bottomRight);
                    }
                }
            }
            
            // 右侧边界列（不包括已处理的顶部和底部顶点）
            if (blockEndX > blockStartX + boundaryStep)
            {
                for (int z = blockStartZ + boundaryStep; z < blockEndZ - boundaryStep; z += boundaryStep)
                {
                    if (z + boundaryStep > blockEndZ - boundaryStep)
                        break;
                    
                    // 右侧边界：从blockEndX-1向左，确保不超出范围
                    int x = blockEndX - boundaryStep;
                    if (x < blockStartX || x + boundaryStep > blockEndX)
                        continue;  // 跳过无效的边界
                    
                    uint32_t topLeft = z * m_params.width + x;
                    uint32_t topRight = z * m_params.width + (x + boundaryStep);
                    uint32_t bottomLeft = (z + boundaryStep) * m_params.width + x;
                    uint32_t bottomRight = (z + boundaryStep) * m_params.width + (x + boundaryStep);
                    
                    if (topRight < m_vertices.size() && 
                        bottomLeft < m_vertices.size() && 
                        bottomRight < m_vertices.size())
                    {
                        allLODIndices[lod].push_back(topLeft);
                        allLODIndices[lod].push_back(bottomLeft);
                        allLODIndices[lod].push_back(topRight);
                        
                        allLODIndices[lod].push_back(topRight);
                        allLODIndices[lod].push_back(bottomLeft);
                        allLODIndices[lod].push_back(bottomRight);
                    }
                }
            }
            
            // 2. 生成内部区域的三角形（使用当前LOD的步长）
            int innerStartZ = blockStartZ + boundaryStep;
            int innerEndZ = blockEndZ - boundaryStep;
            int innerStartX = blockStartX + boundaryStep;
            int innerEndX = blockEndX - boundaryStep;
            
            // 只有当内部区域足够大时才生成
            if (innerStartZ < innerEndZ && innerStartX < innerEndX)
            {
                for (int z = innerStartZ; z < innerEndZ; z += step)
                {
                    if (z + step > innerEndZ)
                        break;
                        
                    for (int x = innerStartX; x < innerEndX; x += step)
                    {
                        if (x + step > innerEndX)
                            break;
                        
                        uint32_t topLeft = z * m_params.width + x;
                        uint32_t topRight = z * m_params.width + (x + step);
                        uint32_t bottomLeft = (z + step) * m_params.width + x;
                        uint32_t bottomRight = (z + step) * m_params.width + (x + step);
                        
                        if (topRight >= m_vertices.size() || 
                            bottomLeft >= m_vertices.size() || 
                            bottomRight >= m_vertices.size())
                            continue;
                        
                        allLODIndices[lod].push_back(topLeft);
                        allLODIndices[lod].push_back(bottomLeft);
                        allLODIndices[lod].push_back(topRight);
                        
                        allLODIndices[lod].push_back(topRight);
                        allLODIndices[lod].push_back(bottomLeft);
                        allLODIndices[lod].push_back(bottomRight);
                    }
                }
            }
            
            // 记录索引数量
            patch.lodRanges[lod].indexCount = (UINT)allLODIndices[lod].size() - patch.lodRanges[lod].indexStart;
        }
    }
    
    // 为每个LOD级别创建索引缓冲区
    m_lodMeshes.clear();
    m_lodMeshes.resize(MAX_LOD_LEVELS);
    
    for (int lod = 0; lod < MAX_LOD_LEVELS; ++lod)
    {
        if (allLODIndices[lod].empty())
            continue;
        
        D3D11_BUFFER_DESC ibd = {};
        ibd.Usage = D3D11_USAGE_DEFAULT;
        ibd.ByteWidth = (UINT)(sizeof(uint32_t) * allLODIndices[lod].size());
        ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        ibd.CPUAccessFlags = 0;
        
        D3D11_SUBRESOURCE_DATA iinitData = {};
        iinitData.pSysMem = allLODIndices[lod].data();
        
        HRESULT hr = device->CreateBuffer(&ibd, &iinitData, m_lodMeshes[lod].indexBuffer.GetAddressOf());
        if (SUCCEEDED(hr))
        {
            m_lodMeshes[lod].indices = std::move(allLODIndices[lod]);
            m_lodMeshes[lod].indexCount = (UINT)m_lodMeshes[lod].indices.size();
            
            wchar_t msg[256];
            swprintf_s(msg, L"[TERRAIN DEBUG] LOD %d index buffer created: %d indices\n", lod, m_lodMeshes[lod].indexCount);
            OutputDebugStringW(msg);
        }
    }
}

// 生成地形块
void Terrain::GeneratePatches()
{
    m_patches.clear();
    
    // 计算每个块的大小（世界空间）
    float patchWorldSizeX = m_params.sizeX / ((float)(m_params.width - 1) / (float)(m_patchSize - 1));
    float patchWorldSizeZ = m_params.sizeZ / ((float)(m_params.height - 1) / (float)(m_patchSize - 1));
    
    // 计算块的数量
    int numPatchesX = (m_params.width - 1) / (m_patchSize - 1);
    int numPatchesZ = (m_params.height - 1) / (m_patchSize - 1);
    
    // 生成每个块
    for (int pz = 0; pz < numPatchesZ; ++pz)
    {
        for (int px = 0; px < numPatchesX; ++px)
        {
            TerrainPatch patch;
            
            // 计算世界空间边界
            patch.minX = -m_params.sizeX * 0.5f + px * patchWorldSizeX;
            patch.maxX = patch.minX + patchWorldSizeX;
            patch.minZ = -m_params.sizeZ * 0.5f + pz * patchWorldSizeZ;
            patch.maxZ = patch.minZ + patchWorldSizeZ;
            
            // 计算中心点
            patch.centerX = (patch.minX + patch.maxX) * 0.5f;
            patch.centerZ = (patch.minZ + patch.maxZ) * 0.5f;
            
            // 记录块在地形网格中的位置
            patch.patchX = px;
            patch.patchZ = pz;
            patch.startX = px * (m_patchSize - 1);
            patch.startZ = pz * (m_patchSize - 1);
            patch.endX = patch.startX + (m_patchSize - 1);
            patch.endZ = patch.startZ + (m_patchSize - 1);
            
            // 确保不越界
            if (patch.endX >= m_params.width) patch.endX = m_params.width - 1;
            if (patch.endZ >= m_params.height) patch.endZ = m_params.height - 1;
            
            // 初始LOD级别（会在渲染时更新）
            patch.lodLevel = 0;
            
            // 初始化LOD范围
            for (int i = 0; i < MAX_LOD_LEVELS; ++i)
            {
                patch.lodRanges[i].indexStart = 0;
                patch.lodRanges[i].indexCount = 0;
            }
            
            m_patches.push_back(patch);
        }
    }
    
    wchar_t msg[256];
    swprintf_s(msg, L"[TERRAIN DEBUG] Generated %d terrain patches\n", (int)m_patches.size());
    OutputDebugStringW(msg);
}

// 选择可见的LOD块
void Terrain::SelectLODPatches(const DirectX::XMFLOAT3& cameraPosition, std::vector<TerrainPatch>& visiblePatches)
{
    visiblePatches.clear();
    
    for (auto& patch : m_patches)
    {
        int lod;
        
        // 如果LOD被锁定，使用锁定的LOD级别
        if (m_lodLocked)
        {
            lod = m_lockedLODLevel;
        }
        else
        {
            // 计算相机到块中心的距离
            float dx = patch.centerX - cameraPosition.x;
            float dz = patch.centerZ - cameraPosition.z;
            float distance = sqrtf(dx * dx + dz * dz);
            
            // 根据距离选择LOD级别
            lod = 0;
            for (int i = 0; i < MAX_LOD_LEVELS - 1; ++i)
            {
                if (distance > m_lodDistances[i])
                {
                    lod = i + 1;
                }
                else
                {
                    break;
                }
            }
            
            // 限制LOD级别
            if (lod >= MAX_LOD_LEVELS)
                lod = MAX_LOD_LEVELS - 1;
        }
        
        patch.lodLevel = lod;
        
        // 检查这个LOD级别是否有有效的索引范围
        if (lod < MAX_LOD_LEVELS && patch.lodRanges[lod].indexCount > 0)
        {
            visiblePatches.push_back(patch);
        }
    }
}

// 检查块是否在视锥内（简化版本）
bool Terrain::IsPatchVisible(const TerrainPatch& patch, const DirectX::XMFLOAT4X4& viewProjMatrix)
{
    // 简化实现：总是返回true（可以后续实现完整的视锥剔除）
    // TODO: 实现完整的视锥剔除
    return true;
}

// 使用CDLOD渲染地形
void Terrain::Render(ID3D11DeviceContext* context, const DirectX::XMFLOAT3& cameraPosition)
{
    if (!m_useCDLOD)
    {
        // 回退到旧版本渲染
        Render(context);
        return;
    }
    
    if (!context || !m_vertexBuffer)
        return;
    
    // 设置顶点缓冲区
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    
    // 设置图元类型
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    
    // 选择可见的LOD块
    std::vector<TerrainPatch> visiblePatches;
    SelectLODPatches(cameraPosition, visiblePatches);
    
    // 统计信息
    static int frameCount = 0;
    frameCount++;
    if (frameCount % 60 == 0)
    {
        wchar_t msg[256];
        swprintf_s(msg, L"[TERRAIN CDLOD] Rendering %d patches\n", (int)visiblePatches.size());
        OutputDebugStringW(msg);
    }
    
    // 按LOD级别分组渲染（减少状态切换）
    int totalIndices = 0;
    int totalPatches = 0;
    
    for (int lod = 0; lod < MAX_LOD_LEVELS; ++lod)
    {
        if (lod >= (int)m_lodMeshes.size() || !m_lodMeshes[lod].indexBuffer)
            continue;
        
        // 设置当前LOD的索引缓冲区
        context->IASetIndexBuffer(m_lodMeshes[lod].indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        
        // 渲染所有使用此LOD的块（每个块单独绘制）
        for (const auto& patch : visiblePatches)
        {
            if (patch.lodLevel == lod && patch.lodRanges[lod].indexCount > 0)
            {
                // 绘制这个块的索引范围
                // DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation)
                UINT indexCount = patch.lodRanges[lod].indexCount;
                UINT startIndex = patch.lodRanges[lod].indexStart;
                INT baseVertex = 0;
                context->DrawIndexed(indexCount, startIndex, baseVertex);
                totalIndices += patch.lodRanges[lod].indexCount;
                totalPatches++;
            }
        }
    }
    
    // 调试输出
    if (frameCount % 60 == 0)
    {
        wchar_t msg[256];
        swprintf_s(msg, L"[TERRAIN CDLOD] Rendered %d patches, %d total indices\n", totalPatches, totalIndices);
        OutputDebugStringW(msg);
    }
}

