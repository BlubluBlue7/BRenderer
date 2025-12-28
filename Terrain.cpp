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
{
    m_params.width = 256;
    m_params.height = 256;
    m_params.sizeX = 100.0f;
    m_params.sizeZ = 100.0f;
    m_params.heightScale = 20.0f;
    m_params.heightOffset = 0.0f;
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
    
    // 生成程序化高度数据（简单的噪声）
    std::vector<float> heightData(m_params.width * m_params.height);
    for (int z = 0; z < m_params.height; ++z)
    {
        for (int x = 0; x < m_params.width; ++x)
        {
            float fx = (float)x / (float)(m_params.width - 1);
            float fz = (float)z / (float)(m_params.height - 1);
            
            // 简单的正弦波生成地形
            float height = sinf(fx * XM_PI * 4.0f) * sinf(fz * XM_PI * 4.0f) * 0.5f + 0.5f;
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
            float texScale = 1.0f;  // 可以做成参数
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

