#include "GrassSystem.h"
#include <d3dcompiler.h>
#include <fstream>
#include <vector>
#include <algorithm>

#pragma comment(lib, "d3dcompiler.lib")

// 简单的顶点结构（只需要位置）
struct GrassVertex
{
    XMFLOAT3 position;
};

// 常量缓冲区结构（与着色器中的cbuffer匹配）
struct GrassConstantBuffer
{
    XMFLOAT4X4 view;
    XMFLOAT4X4 projection;
    XMFLOAT4X4 viewInverse;  // 视图矩阵的逆矩阵（用于billboard）
    XMFLOAT3 cameraPosition; // 相机位置（世界空间）
    float time;              // 时间（用于动画）
    XMFLOAT3 windDirection;  // 风向
    float windStrength;      // 风力强度
    XMFLOAT3 windFieldCenter; // 风场中心位置
    float windFieldRadius;    // 风场影响半径
    float windFieldStrength;   // 风场强度
    float padding[2];          // 对齐填充
};

// 实例数据结构（与着色器中的InstanceData匹配）
struct GrassInstanceData
{
    XMFLOAT3 position;
    float padding;  // 对齐到16字节
};

// ============================================================================
// 构造函数
// ============================================================================
GrassSystem::GrassSystem()
{
}

// ============================================================================
// 析构函数
// ============================================================================
GrassSystem::~GrassSystem()
{
    Cleanup();
}

// ============================================================================
// 初始化草地系统
// ============================================================================
bool GrassSystem::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    m_device = device;
    m_context = context;
    
    // 创建几何体
    if (!CreateGrassGeometry(device))
    {
        OutputDebugStringW(L"[GrassSystem] Failed to create grass geometry.\n");
        return false;
    }
    
    // 创建着色器
    if (!CreateShaders(device))
    {
        OutputDebugStringW(L"[GrassSystem] Failed to create shaders.\n");
        return false;
    }
    
    // 创建输入布局
    if (!CreateInputLayout(device))
    {
        OutputDebugStringW(L"[GrassSystem] Failed to create input layout.\n");
        return false;
    }
    
    // 创建常量缓冲区
    if (!CreateConstantBuffer(device))
    {
        OutputDebugStringW(L"[GrassSystem] Failed to create constant buffer.\n");
        return false;
    }
    
    // 创建实例缓冲区
    if (!CreateInstanceBuffer(device))
    {
        OutputDebugStringW(L"[GrassSystem] Failed to create instance buffer.\n");
        return false;
    }
    
    OutputDebugStringW(L"[GrassSystem] Initialized successfully.\n");
    return true;
}

// ============================================================================
// 清理资源
// ============================================================================
void GrassSystem::Cleanup()
{
    m_vertexBuffer.Reset();
    m_indexBuffer.Reset();
    m_vertexShader.Reset();
    m_pixelShader.Reset();
    m_inputLayout.Reset();
    m_vsBlob.Reset();
    m_constantBuffer.Reset();
    m_instanceBuffer.Reset();
    m_instanceBufferSRV.Reset();
    m_visibleGrassPositions.clear();
}

// ============================================================================
// 创建草的几何体（2个三角形组成一个面片）
// ============================================================================
bool GrassSystem::CreateGrassGeometry(ID3D11Device* device)
{
    // 创建一个简单的四边形面片（2个三角形）
    // 草的高度设为1.0，宽度设为0.5
    const float grassHeight = 1.0f;
    const float grassWidth = 0.5f;
    
    // 4个顶点，组成一个垂直的面片
    GrassVertex vertices[] = {
        // 左下
        { XMFLOAT3(-grassWidth * 0.5f, 0.0f, 0.0f) },
        // 右下
        { XMFLOAT3(grassWidth * 0.5f, 0.0f, 0.0f) },
        // 左上
        { XMFLOAT3(-grassWidth * 0.5f, grassHeight, 0.0f) },
        // 右上
        { XMFLOAT3(grassWidth * 0.5f, grassHeight, 0.0f) }
    };
    
    // 索引：2个三角形
    uint32_t indices[] = {
        0, 1, 2,  // 第一个三角形
        1, 3, 2   // 第二个三角形
    };
    
    m_indexCount = 6;
    
    // 创建顶点缓冲区
    D3D11_BUFFER_DESC vertexBufferDesc = {};
    vertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    vertexBufferDesc.ByteWidth = sizeof(GrassVertex) * 4;
    vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vertexBufferDesc.CPUAccessFlags = 0;
    
    D3D11_SUBRESOURCE_DATA vertexData = {};
    vertexData.pSysMem = vertices;
    vertexData.SysMemPitch = 0;
    vertexData.SysMemSlicePitch = 0;
    
    HRESULT hr = device->CreateBuffer(&vertexBufferDesc, &vertexData, m_vertexBuffer.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[GrassSystem] Failed to create vertex buffer.\n");
        return false;
    }
    
    // 创建索引缓冲区
    D3D11_BUFFER_DESC indexBufferDesc = {};
    indexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    indexBufferDesc.ByteWidth = sizeof(uint32_t) * m_indexCount;
    indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    indexBufferDesc.CPUAccessFlags = 0;
    
    D3D11_SUBRESOURCE_DATA indexData = {};
    indexData.pSysMem = indices;
    indexData.SysMemPitch = 0;
    indexData.SysMemSlicePitch = 0;
    
    hr = device->CreateBuffer(&indexBufferDesc, &indexData, m_indexBuffer.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[GrassSystem] Failed to create index buffer.\n");
        return false;
    }
    
    return true;
}

// ============================================================================
// 创建着色器
// ============================================================================
bool GrassSystem::CreateShaders(ID3D11Device* device)
{
    // 获取着色器文件路径
    wchar_t exePath[MAX_PATH] = { 0 };
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0)
    {
        OutputDebugStringW(L"[GrassSystem] Failed to get executable path.\n");
        return false;
    }
    
    std::wstring exeDir = exePath;
    size_t lastSlash = exeDir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos)
    {
        exeDir = exeDir.substr(0, lastSlash + 1);
    }
    
    std::wstring projectRoot = exeDir;
    for (int i = 0; i < 2; ++i)
    {
        size_t slash = projectRoot.find_last_of(L"\\/", projectRoot.length() - 2);
        if (slash != std::wstring::npos)
            projectRoot = projectRoot.substr(0, slash + 1);
    }
    
    // 尝试多个路径
    std::vector<std::wstring> shaderPaths = {
        projectRoot + L"Shaders/GrassVertexShader.hlsl",
        exeDir + L"Shaders/GrassVertexShader.hlsl",
        L"Shaders/GrassVertexShader.hlsl"
    };
    
    // 编译顶点着色器
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    bool shaderFound = false;
    
    for (const auto& path : shaderPaths)
    {
        HRESULT hr = D3DCompileFromFile(
            path.c_str(),
            nullptr,
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            "VS",
            "vs_5_0",
            0,
            0,
            m_vsBlob.GetAddressOf(),
            errorBlob.GetAddressOf()
        );
        
        if (SUCCEEDED(hr))
        {
            shaderFound = true;
            break;
        }
    }
    
    if (!shaderFound)
    {
        if (errorBlob)
        {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        OutputDebugStringW(L"[GrassSystem] Failed to compile vertex shader.\n");
        return false;
    }
    
    // 创建顶点着色器
    HRESULT hr = device->CreateVertexShader(
        m_vsBlob->GetBufferPointer(),
        m_vsBlob->GetBufferSize(),
        nullptr,
        m_vertexShader.GetAddressOf()
    );
    
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[GrassSystem] Failed to create vertex shader.\n");
        return false;
    }
    
    // 编译像素着色器
    std::vector<std::wstring> psPaths = {
        projectRoot + L"Shaders/GrassPixelShader.hlsl",
        exeDir + L"Shaders/GrassPixelShader.hlsl",
        L"Shaders/GrassPixelShader.hlsl"
    };
    
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    shaderFound = false;
    
    for (const auto& path : psPaths)
    {
        hr = D3DCompileFromFile(
            path.c_str(),
            nullptr,
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            "PS",
            "ps_5_0",
            0,
            0,
            psBlob.GetAddressOf(),
            errorBlob.GetAddressOf()
        );
        
        if (SUCCEEDED(hr))
        {
            shaderFound = true;
            break;
        }
    }
    
    if (!shaderFound)
    {
        if (errorBlob)
        {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        OutputDebugStringW(L"[GrassSystem] Failed to compile pixel shader.\n");
        return false;
    }
    
    // 创建像素着色器
    hr = device->CreatePixelShader(
        psBlob->GetBufferPointer(),
        psBlob->GetBufferSize(),
        nullptr,
        m_pixelShader.GetAddressOf()
    );
    
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[GrassSystem] Failed to create pixel shader.\n");
        return false;
    }
    
    return true;
}

// ============================================================================
// 创建输入布局
// ============================================================================
bool GrassSystem::CreateInputLayout(ID3D11Device* device)
{
    // 定义输入元素（只有位置）
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    
    UINT numElements = ARRAYSIZE(layout);
    
    HRESULT hr = device->CreateInputLayout(
        layout,
        numElements,
        m_vsBlob->GetBufferPointer(),
        m_vsBlob->GetBufferSize(),
        m_inputLayout.GetAddressOf()
    );
    
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[GrassSystem] Failed to create input layout.\n");
        return false;
    }
    
    return true;
}

// ============================================================================
// 创建常量缓冲区
// ============================================================================
bool GrassSystem::CreateConstantBuffer(ID3D11Device* device)
{
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    // 常量缓冲区大小必须是16字节的倍数
    UINT bufferSize = (sizeof(GrassConstantBuffer) + 15) & ~15;
    bufferDesc.ByteWidth = bufferSize;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    
    HRESULT hr = device->CreateBuffer(&bufferDesc, nullptr, m_constantBuffer.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[GrassSystem] Failed to create constant buffer.\n");
        return false;
    }
    
    return true;
}

// ============================================================================
// 创建实例缓冲区
// ============================================================================
bool GrassSystem::CreateInstanceBuffer(ID3D11Device* device)
{
    // 创建足够大的缓冲区（可以容纳所有草）
    // 实际大小会在UpdateInstanceBuffer中动态调整
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.ByteWidth = sizeof(GrassInstanceData) * 1024 * 1024;  // 最大1M个实例
    bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bufferDesc.StructureByteStride = sizeof(GrassInstanceData);
    
    HRESULT hr = device->CreateBuffer(&bufferDesc, nullptr, m_instanceBuffer.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[GrassSystem] Failed to create instance buffer.\n");
        return false;
    }
    
    // 创建着色器资源视图
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = 1024 * 1024;
    
    hr = device->CreateShaderResourceView(m_instanceBuffer.Get(), &srvDesc, m_instanceBufferSRV.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[GrassSystem] Failed to create instance buffer SRV.\n");
        return false;
    }
    
    return true;
}

// ============================================================================
// 从视图投影矩阵提取视锥体平面
// ============================================================================
void GrassSystem::ExtractFrustumPlanes(const XMFLOAT4X4& viewProj, XMFLOAT4* planes) const
{
    // 直接使用XMFLOAT4X4访问成员（不需要转换为XMMATRIX）
    // 提取6个平面：左、右、下、上、近、远
    // 平面方程：ax + by + cz + d = 0，存储为 (a, b, c, d)
    
    // 左平面
    planes[0].x = viewProj._14 + viewProj._11;
    planes[0].y = viewProj._24 + viewProj._21;
    planes[0].z = viewProj._34 + viewProj._31;
    planes[0].w = viewProj._44 + viewProj._41;
    
    // 右平面
    planes[1].x = viewProj._14 - viewProj._11;
    planes[1].y = viewProj._24 - viewProj._21;
    planes[1].z = viewProj._34 - viewProj._31;
    planes[1].w = viewProj._44 - viewProj._41;
    
    // 下平面
    planes[2].x = viewProj._14 + viewProj._12;
    planes[2].y = viewProj._24 + viewProj._22;
    planes[2].z = viewProj._34 + viewProj._32;
    planes[2].w = viewProj._44 + viewProj._42;
    
    // 上平面
    planes[3].x = viewProj._14 - viewProj._12;
    planes[3].y = viewProj._24 - viewProj._22;
    planes[3].z = viewProj._34 - viewProj._32;
    planes[3].w = viewProj._44 - viewProj._42;
    
    // 近平面
    planes[4].x = viewProj._13;
    planes[4].y = viewProj._23;
    planes[4].z = viewProj._33;
    planes[4].w = viewProj._43;
    
    // 远平面
    planes[5].x = viewProj._14 - viewProj._13;
    planes[5].y = viewProj._24 - viewProj._23;
    planes[5].z = viewProj._34 - viewProj._33;
    planes[5].w = viewProj._44 - viewProj._43;
    
    // 归一化所有平面
    for (int i = 0; i < 6; ++i)
    {
        XMVECTOR plane = XMLoadFloat4(&planes[i]);
        float length = XMVectorGetX(XMVector3Length(plane));
        if (length > 0.0001f)
        {
            plane = XMVectorScale(plane, 1.0f / length);
            XMStoreFloat4(&planes[i], plane);
        }
    }
}

// ============================================================================
// 检查点是否在视锥体内
// ============================================================================
bool GrassSystem::IsPointInFrustum(const XMFLOAT3& point, const XMFLOAT4* planes) const
{
    XMVECTOR p = XMLoadFloat3(&point);
    
    // 检查点是否在所有平面的正面（或平面上）
    for (int i = 0; i < 6; ++i)
    {
        XMVECTOR plane = XMLoadFloat4(&planes[i]);
        float distance = XMVectorGetX(XMPlaneDotCoord(plane, p));
        
        // 如果点在平面背面，则不在视锥体内
        if (distance < 0.0f)
        {
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// 更新实例缓冲区（根据视锥剔除结果）
// ============================================================================
void GrassSystem::UpdateInstanceBuffer(ID3D11DeviceContext* context, const XMFLOAT4X4& view, const XMFLOAT4X4& projection)
{
    if (m_grassPositions.empty())
    {
        m_visibleInstanceCount = 0;
        return;
    }
    
    // 计算视图投影矩阵
    XMMATRIX viewMatrix = XMLoadFloat4x4(&view);
    XMMATRIX projMatrix = XMLoadFloat4x4(&projection);
    XMMATRIX viewProj = viewMatrix * projMatrix;
    
    XMFLOAT4X4 viewProjMatrix;
    XMStoreFloat4x4(&viewProjMatrix, viewProj);
    
    // 提取视锥体平面
    XMFLOAT4 frustumPlanes[6];
    ExtractFrustumPlanes(viewProjMatrix, frustumPlanes);
    
    // 执行视锥剔除
    m_visibleGrassPositions.clear();
    m_visibleGrassPositions.reserve(m_grassPositions.size() / 4);  // 预估约25%可见
    
    for (const auto& pos : m_grassPositions)
    {
        // 检查草的位置是否在视锥体内
        // 由于草是一个小的面片，我们只检查中心点
        // 如果需要更精确的剔除，可以检查草的面片边界
        if (IsPointInFrustum(pos, frustumPlanes))
        {
            m_visibleGrassPositions.push_back(pos);
        }
    }
    
    m_visibleInstanceCount = (UINT)m_visibleGrassPositions.size();
    
    // 更新实例缓冲区
    if (m_visibleInstanceCount > 0)
    {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = context->Map(m_instanceBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        if (SUCCEEDED(hr))
        {
            GrassInstanceData* instanceData = (GrassInstanceData*)mappedResource.pData;
            for (size_t i = 0; i < m_visibleGrassPositions.size(); ++i)
            {
                instanceData[i].position = m_visibleGrassPositions[i];
                instanceData[i].padding = 0.0f;
            }
            context->Unmap(m_instanceBuffer.Get(), 0);
        }
    }
}

// ============================================================================
// 渲染草地（使用实例化渲染、Billboard和顶点动画）
// ============================================================================
void GrassSystem::Render(ID3D11DeviceContext* context, const XMFLOAT4X4& view, const XMFLOAT4X4& projection, float deltaTime)
{
    if (!m_vertexBuffer || !m_indexBuffer || !m_vertexShader || !m_pixelShader || !m_inputLayout)
    {
        return;
    }
    
    // 更新实例缓冲区（执行视锥剔除）
    UpdateInstanceBuffer(context, view, projection);
    
    // 如果没有可见的草，直接返回
    if (m_visibleInstanceCount == 0)
    {
        return;
    }
    
    // 设置输入布局
    context->IASetInputLayout(m_inputLayout.Get());
    
    // 设置图元类型
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    
    // 绑定顶点缓冲区
    UINT stride = sizeof(GrassVertex);
    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    
    // 绑定索引缓冲区
    context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    
    // 设置着色器
    context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    
    // 计算视图矩阵的逆矩阵（用于提取相机位置）
    XMMATRIX viewMatrix = XMLoadFloat4x4(&view);
    XMMATRIX viewInverse = XMMatrixInverse(nullptr, viewMatrix);
    XMMATRIX projMatrix = XMLoadFloat4x4(&projection);
    
    // 从视图矩阵的逆矩阵中提取相机位置（最后一列的前三个元素）
    XMFLOAT4X4 viewInverseMatrix;
    XMStoreFloat4x4(&viewInverseMatrix, viewInverse);
    XMFLOAT3 cameraPos;
    cameraPos.x = viewInverseMatrix._41;
    cameraPos.y = viewInverseMatrix._42;
    cameraPos.z = viewInverseMatrix._43;
    
    // 累积时间（用于动画）
    static float totalTime = 0.0f;
    totalTime += deltaTime;
    
    // 更新常量缓冲区
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (SUCCEEDED(hr))
    {
        GrassConstantBuffer* cb = (GrassConstantBuffer*)mappedResource.pData;
        XMStoreFloat4x4(&cb->view, XMMatrixTranspose(viewMatrix));
        XMStoreFloat4x4(&cb->projection, XMMatrixTranspose(projMatrix));
        XMStoreFloat4x4(&cb->viewInverse, XMMatrixTranspose(viewInverse));
        cb->cameraPosition = cameraPos;
        cb->time = totalTime;
        
        // 全局风向（归一化）
        XMVECTOR windDir = XMVectorSet(1.0f, 0.0f, 0.5f, 0.0f);
        windDir = XMVector3Normalize(windDir);
        XMStoreFloat3(&cb->windDirection, windDir);
        cb->windStrength = 0.3f;  // 全局风力强度
        
        // 风场参数（在地形中心附近创建一个风场）
        cb->windFieldCenter = XMFLOAT3(0.0f, 0.0f, 0.0f);  // 风场中心（地形中心）
        cb->windFieldRadius = 200.0f;  // 风场影响半径（200单位）
        cb->windFieldStrength = 0.5f;   // 风场强度
        
        context->Unmap(m_constantBuffer.Get(), 0);
    }
    
    // 绑定常量缓冲区
    context->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    
    // 绑定实例缓冲区（作为着色器资源）
    context->VSSetShaderResources(0, 1, m_instanceBufferSRV.GetAddressOf());
    
    // 使用实例化渲染绘制所有可见的草
    context->DrawIndexedInstanced(m_indexCount, m_visibleInstanceCount, 0, 0, 0);
}

// ============================================================================
// 生成多个草的位置，铺满整个地形
// ============================================================================
void GrassSystem::GenerateGrassPositions(float terrainSizeX, float terrainSizeZ, float spacing, 
                                        std::function<float(float, float)> getHeightFunc,
                                        std::function<bool(float, float, float)> includeFunc)
{
    m_grassPositions.clear();
    
    // 计算地形范围（从中心开始，地形中心在(0, 0)）
    float halfSizeX = terrainSizeX * 0.5f;
    float halfSizeZ = terrainSizeZ * 0.5f;
    
    // 计算需要创建的草的数量
    int countX = (int)(terrainSizeX / spacing) + 1;
    int countZ = (int)(terrainSizeZ / spacing) + 1;
    
    wchar_t msg[256];
    swprintf_s(msg, L"[GrassSystem] Generating %d x %d = %d grass instances...\n", countX, countZ, countX * countZ);
    OutputDebugStringW(msg);
    
    // 预分配空间以提高性能
    m_grassPositions.reserve(countX * countZ);
    
    // 简单的伪随机数生成器（基于位置）
    auto randomFloat = [](float x, float z) -> float {
        // 使用简单的哈希函数生成伪随机数
        float n = sin(x * 12.9898f + z * 78.233f) * 43758.5453f;
        return n - floor(n);  // 返回0-1之间的值
    };
    
    // 生成每个草的位置
    for (int z = 0; z < countZ; ++z)
    {
        for (int x = 0; x < countX; ++x)
        {
            // 计算基础世界坐标（从 -halfSize 到 +halfSize）
            float baseX = -halfSizeX + x * spacing;
            float baseZ = -halfSizeZ + z * spacing;
            
            // 添加随机偏移（让草的位置更自然）
            // 偏移范围：-spacing*0.3 到 +spacing*0.3
            float offsetX = (randomFloat(baseX, baseZ) - 0.5f) * spacing * 0.6f;
            float offsetZ = (randomFloat(baseZ, baseX) - 0.5f) * spacing * 0.6f;
            
            float worldX = baseX + offsetX;
            float worldZ = baseZ + offsetZ;
            
            // 获取地形高度（如果有高度函数）
            float height = 0.0f;
            if (getHeightFunc)
            {
                height = getHeightFunc(worldX, worldZ);
            }

            // 可选过滤：例如水域不生成草
            if (includeFunc && !includeFunc(worldX, worldZ, height))
            {
                continue;
            }
            
            // 添加草的位置
            m_grassPositions.push_back(XMFLOAT3(worldX, height, worldZ));
        }
        
        // 每100行输出一次进度
        if ((z + 1) % 100 == 0)
        {
            swprintf_s(msg, L"[GrassSystem] Generated %d rows...\n", z + 1);
            OutputDebugStringW(msg);
        }
    }
    
    swprintf_s(msg, L"[GrassSystem] Generated %zu grass positions successfully.\n", m_grassPositions.size());
    OutputDebugStringW(msg);
}

