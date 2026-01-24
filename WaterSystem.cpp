#include "WaterSystem.h"
#include "Terrain_new.h"
#include <d3dcompiler.h>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cfloat>

#pragma comment(lib, "d3dcompiler.lib")

// 简单的顶点结构
struct WaterVertex
{
    XMFLOAT3 position;
    XMFLOAT3 normal;
    XMFLOAT2 texCoord;
};

// 常量缓冲区结构（与着色器中的cbuffer匹配）
// 注意：D3D11常量缓冲区 ByteWidth 必须是 16 字节的倍数
struct alignas(16) WaterConstantBuffer
{
    XMFLOAT4X4 world;
    XMFLOAT4X4 view;
    XMFLOAT4X4 projection;
    XMFLOAT4X4 worldViewProj;
    XMFLOAT3 cameraPosition;
    float time;              // 时间（用于未来动画）
    float waterLevel;         // 水位高度
    XMFLOAT3 waterColor;     // 水体颜色
    float transparency;      // 透明度
    float waveAmplitude;     // 波浪振幅
    float waveFrequency;     // 波浪频率
    float waveSpeed;         // 波浪速度
    float padding;           // 对齐填充
    XMFLOAT4 waterBounds;    // minX,minZ,maxX,maxZ（用于高度图UV映射）
    XMFLOAT2 terrainHeightParams; // x=heightScale, y=heightOffset
    float padding2[2];       // 额外填充：确保sizeof是16的倍数
};

// ============================================================================
// 构造函数
// ============================================================================
WaterSystem::WaterSystem()
{
}

// ============================================================================
// 析构函数
// ============================================================================
WaterSystem::~WaterSystem()
{
    Cleanup();
}

// ============================================================================
// 初始化水体系统
// ============================================================================
bool WaterSystem::Initialize(ID3D11Device* device, ID3D11DeviceContext* context, TerrainNew* terrain)
{
    m_device = device;
    m_context = context;
    m_terrain = terrain;
    
    // 从地形获取范围（如果地形存在）
    if (m_terrain)
    {
        const TerrainNewParams& params = m_terrain->GetParams();
        m_minX = -params.worldSizeX * 0.5f;
        m_minZ = -params.worldSizeZ * 0.5f;
        m_maxX = params.worldSizeX * 0.5f;
        m_maxZ = params.worldSizeZ * 0.5f;
    }

    // 计算水位高度（如果启用自动计算）
    // 注意：必须在设置好m_minX/m_maxX范围之后再计算，否则采样范围会不匹配地形
    if (m_autoCalculateLevel && m_terrain)
    {
        CalculateWaterLevel();
    }
    
    // 创建几何体
    if (!CreateWaterGeometry(device))
    {
        OutputDebugStringW(L"[WaterSystem] Failed to create water geometry.\n");
        return false;
    }
    
    // 创建着色器
    if (!CreateShaders(device))
    {
        OutputDebugStringW(L"[WaterSystem] Failed to create shaders.\n");
        return false;
    }
    
    // 创建输入布局
    if (!CreateInputLayout(device))
    {
        OutputDebugStringW(L"[WaterSystem] Failed to create input layout.\n");
        return false;
    }
    
    // 创建常量缓冲区
    if (!CreateConstantBuffer(device))
    {
        OutputDebugStringW(L"[WaterSystem] Failed to create constant buffer.\n");
        return false;
    }
    
    // 创建深度状态
    if (!CreateDepthStencilState(device))
    {
        OutputDebugStringW(L"[WaterSystem] Failed to create depth stencil state.\n");
        return false;
    }
    
    // 创建混合状态
    if (!CreateBlendState(device))
    {
        OutputDebugStringW(L"[WaterSystem] Failed to create blend state.\n");
        return false;
    }

    // 创建光栅化状态（双面渲染）
    if (!CreateRasterizerState(device))
    {
        OutputDebugStringW(L"[WaterSystem] Failed to create rasterizer state.\n");
        return false;
    }
    
    OutputDebugStringW(L"[WaterSystem] Initialized successfully.\n");
    return true;
}

// ============================================================================
// 清理资源
// ============================================================================
void WaterSystem::Cleanup()
{
    m_vertexBuffer.Reset();
    m_indexBuffer.Reset();
    m_vertexShader.Reset();
    m_pixelShader.Reset();
    m_inputLayout.Reset();
    m_vsBlob.Reset();
    m_constantBuffer.Reset();
    m_depthStencilState.Reset();
    m_blendState.Reset();
    m_rasterizerState.Reset();
}

// ============================================================================
// 计算水位高度（基于地形最低点）
// ============================================================================
void WaterSystem::CalculateWaterLevel()
{
    if (!m_terrain)
        return;

    // 采样地形高度，找出最低点
    float minHeight = FLT_MAX;
    int sampleCount = 50;  // 采样点数（可以调整）
    
    for (int i = 0; i <= sampleCount; ++i)
    {
        for (int j = 0; j <= sampleCount; ++j)
        {
            float x = m_minX + (m_maxX - m_minX) * (float)i / sampleCount;
            float z = m_minZ + (m_maxZ - m_minZ) * (float)j / sampleCount;
            
            float height = m_terrain->GetHeightAt(x, z);
            if (height < minHeight)
            {
                minHeight = height;
            }
        }
    }
    
    // 设置水位为最低点 + 一个小偏移（例如5米）
    // 稍微抬高一点，避免水面过低导致看不明显/与地形混在一起
    m_waterLevel = minHeight + 20.0f;
    
    wchar_t msg[256];
    swprintf_s(msg, L"[WaterSystem] Calculated water level: %.2f (min terrain height: %.2f)\n", 
               m_waterLevel, minHeight);
    OutputDebugStringW(msg);
}

// ============================================================================
// 创建水体网格几何体
// ============================================================================
bool WaterSystem::CreateWaterGeometry(ID3D11Device* device)
{
    // 创建平面网格
    std::vector<WaterVertex> vertices;
    std::vector<uint32_t> indices;
    
    float width = m_maxX - m_minX;
    float height = m_maxZ - m_minZ;
    
    // 生成顶点
    for (int z = 0; z <= m_gridHeight; ++z)
    {
        for (int x = 0; x <= m_gridWidth; ++x)
        {
            WaterVertex vertex;
            vertex.position.x = m_minX + (width * x / m_gridWidth);
            vertex.position.y = m_waterLevel;  // 使用当前水位高度
            vertex.position.z = m_minZ + (height * z / m_gridHeight);
            
            vertex.normal = XMFLOAT3(0.0f, 1.0f, 0.0f);  // 向上法线
            
            vertex.texCoord.x = (float)x / m_gridWidth;
            vertex.texCoord.y = (float)z / m_gridHeight;
            
            vertices.push_back(vertex);
        }
    }
    
    // 生成索引（三角形）
    for (int z = 0; z < m_gridHeight; ++z)
    {
        for (int x = 0; x < m_gridWidth; ++x)
        {
            int topLeft = z * (m_gridWidth + 1) + x;
            int topRight = topLeft + 1;
            int bottomLeft = (z + 1) * (m_gridWidth + 1) + x;
            int bottomRight = bottomLeft + 1;
            
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
    
    m_vertexCount = (UINT)vertices.size();
    m_indexCount = (UINT)indices.size();
    
    // 创建顶点缓冲区
    D3D11_BUFFER_DESC vertexBufferDesc = {};
    vertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    vertexBufferDesc.ByteWidth = sizeof(WaterVertex) * m_vertexCount;
    vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vertexBufferDesc.CPUAccessFlags = 0;
    
    D3D11_SUBRESOURCE_DATA vertexData = {};
    vertexData.pSysMem = vertices.data();
    
    HRESULT hr = device->CreateBuffer(&vertexBufferDesc, &vertexData, m_vertexBuffer.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[WaterSystem] Failed to create vertex buffer.\n");
        return false;
    }
    
    // 创建索引缓冲区
    D3D11_BUFFER_DESC indexBufferDesc = {};
    indexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    indexBufferDesc.ByteWidth = sizeof(uint32_t) * m_indexCount;
    indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    indexBufferDesc.CPUAccessFlags = 0;
    
    D3D11_SUBRESOURCE_DATA indexData = {};
    indexData.pSysMem = indices.data();
    
    hr = device->CreateBuffer(&indexBufferDesc, &indexData, m_indexBuffer.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[WaterSystem] Failed to create index buffer.\n");
        return false;
    }
    
    return true;
}

// ============================================================================
// 创建着色器
// ============================================================================
bool WaterSystem::CreateShaders(ID3D11Device* device)
{
    // 获取可执行文件路径
    wchar_t exePath[MAX_PATH] = {0};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0)
    {
        OutputDebugStringW(L"[WaterSystem] Failed to get executable path.\n");
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
        projectRoot + L"Shaders/WaterVertexShader.hlsl",
        exeDir + L"Shaders/WaterVertexShader.hlsl",
        L"Shaders/WaterVertexShader.hlsl"
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
        OutputDebugStringW(L"[WaterSystem] Failed to compile vertex shader.\n");
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
        OutputDebugStringW(L"[WaterSystem] Failed to create vertex shader.\n");
        return false;
    }
    
    // 编译像素着色器
    std::vector<std::wstring> psPaths = {
        projectRoot + L"Shaders/WaterPixelShader.hlsl",
        exeDir + L"Shaders/WaterPixelShader.hlsl",
        L"Shaders/WaterPixelShader.hlsl"
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
        OutputDebugStringW(L"[WaterSystem] Failed to compile pixel shader.\n");
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
        OutputDebugStringW(L"[WaterSystem] Failed to create pixel shader.\n");
        return false;
    }
    
    return true;
}

// ============================================================================
// 创建输入布局
// ============================================================================
bool WaterSystem::CreateInputLayout(ID3D11Device* device)
{
    if (!m_vsBlob)
    {
        OutputDebugStringW(L"[WaterSystem] Vertex shader blob is null.\n");
        return false;
    }
    
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    
    HRESULT hr = device->CreateInputLayout(
        layout,
        ARRAYSIZE(layout),
        m_vsBlob->GetBufferPointer(),
        m_vsBlob->GetBufferSize(),
        m_inputLayout.GetAddressOf()
    );
    
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[WaterSystem] Failed to create input layout.\n");
        return false;
    }
    
    return true;
}

// ============================================================================
// 创建常量缓冲区
// ============================================================================
bool WaterSystem::CreateConstantBuffer(ID3D11Device* device)
{
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.ByteWidth = sizeof(WaterConstantBuffer);
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    
    HRESULT hr = device->CreateBuffer(&bufferDesc, nullptr, m_constantBuffer.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[WaterSystem] Failed to create constant buffer.\n");
        return false;
    }
    
    return true;
}

// ============================================================================
// 创建深度状态
// ============================================================================
bool WaterSystem::CreateDepthStencilState(ID3D11Device* device)
{
    D3D11_DEPTH_STENCIL_DESC depthDesc = {};
    depthDesc.DepthEnable = true;
    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;  // 不写入深度（避免与地形冲突）
    // 恢复正常深度测试：让水体被地形正确遮挡（不会盖住高处地形）
    depthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    depthDesc.StencilEnable = false;
    
    HRESULT hr = device->CreateDepthStencilState(&depthDesc, m_depthStencilState.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[WaterSystem] Failed to create depth stencil state.\n");
        return false;
    }
    
    return true;
}

// ============================================================================
// 创建混合状态（半透明）
// ============================================================================
bool WaterSystem::CreateBlendState(ID3D11Device* device)
{
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = true;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    
    HRESULT hr = device->CreateBlendState(&blendDesc, m_blendState.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[WaterSystem] Failed to create blend state.\n");
        return false;
    }
    
    return true;
}

// ============================================================================
// 创建光栅化状态（双面渲染，避免水面被剔除）
// ============================================================================
bool WaterSystem::CreateRasterizerState(ID3D11Device* device)
{
    D3D11_RASTERIZER_DESC desc = {};
    desc.FillMode = D3D11_FILL_SOLID;
    desc.CullMode = D3D11_CULL_NONE; // 关键：禁用剔除
    desc.FrontCounterClockwise = FALSE;
    desc.DepthClipEnable = TRUE;

    HRESULT hr = device->CreateRasterizerState(&desc, m_rasterizerState.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[WaterSystem] Failed to create rasterizer state.\n");
        return false;
    }
    return true;
}

// ============================================================================
// 设置水体范围
// ============================================================================
void WaterSystem::SetWaterBounds(float minX, float minZ, float maxX, float maxZ)
{
    m_minX = minX;
    m_minZ = minZ;
    m_maxX = maxX;
    m_maxZ = maxZ;
}

// ============================================================================
// 渲染水体
// ============================================================================
void WaterSystem::Render(ID3D11DeviceContext* context, 
                         const XMFLOAT4X4& view, 
                         const XMFLOAT4X4& projection,
                         const XMFLOAT3& cameraPosition,
                         float deltaTime)
{
    if (!m_vertexBuffer || !m_indexBuffer || !m_vertexShader || !m_pixelShader)
        return;
    
    // 保存当前状态
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> oldDepthState;
    UINT oldStencilRef;
    context->OMGetDepthStencilState(oldDepthState.GetAddressOf(), &oldStencilRef);
    
    Microsoft::WRL::ComPtr<ID3D11BlendState> oldBlendState;
    FLOAT oldBlendFactor[4];
    UINT oldSampleMask;
    context->OMGetBlendState(oldBlendState.GetAddressOf(), oldBlendFactor, &oldSampleMask);

    // 保存当前光栅化状态
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> oldRasterizerState;
    context->RSGetState(oldRasterizerState.GetAddressOf());
    
    // 设置深度状态
    context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);
    
    // 设置混合状态
    float blendFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xffffffff);

    // 设置光栅化状态（双面渲染）
    if (m_rasterizerState)
    {
        context->RSSetState(m_rasterizerState.Get());
    }
    
    // 设置输入布局
    context->IASetInputLayout(m_inputLayout.Get());
    
    // 设置顶点和索引缓冲区
    UINT stride = sizeof(WaterVertex);
    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    
    // 设置着色器
    context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    
    // 更新常量缓冲区
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    if (SUCCEEDED(context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource)))
    {
        WaterConstantBuffer* data = (WaterConstantBuffer*)mappedResource.pData;
        
        // 重要：与Renderer::UpdateConstantBuffers保持一致
        // HLSL默认列主序，DirectXMath是行主序，这里需要转置后再写入常量缓冲区
        XMMATRIX worldMatrix = XMMatrixIdentity();
        XMMATRIX viewMatrix = XMLoadFloat4x4(&view);
        XMMATRIX projMatrix = XMLoadFloat4x4(&projection);
        XMMATRIX wvpMatrix = worldMatrix * viewMatrix * projMatrix;

        XMStoreFloat4x4(&data->world, XMMatrixTranspose(worldMatrix));
        XMStoreFloat4x4(&data->view, XMMatrixTranspose(viewMatrix));
        XMStoreFloat4x4(&data->projection, XMMatrixTranspose(projMatrix));
        XMStoreFloat4x4(&data->worldViewProj, XMMatrixTranspose(wvpMatrix));
        
        data->cameraPosition = cameraPosition;
        // 累积时间（用于波浪动画）
        m_time += deltaTime;
        data->time = m_time;
        data->waterLevel = m_waterLevel;
        data->waterColor = XMFLOAT3(0.05f, 0.45f, 0.85f);
        data->transparency = 0.7f;

        data->waveAmplitude = m_waveAmplitude;
        data->waveFrequency = m_waveFrequency;
        data->waveSpeed = m_waveSpeed;

        data->waterBounds = XMFLOAT4(m_minX, m_minZ, m_maxX, m_maxZ);
        if (m_terrain)
        {
            const TerrainNewParams& tp = m_terrain->GetParams();
            data->terrainHeightParams = XMFLOAT2(tp.heightScale, tp.heightOffset);
        }
        else
        {
            data->terrainHeightParams = XMFLOAT2(1.0f, 0.0f);
        }
        
        context->Unmap(m_constantBuffer.Get(), 0);
    }
    
    // 绑定常量缓冲区
    context->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    
    // 注意：LightBuffer和Shadow Map需要在Renderer中绑定
    // 这里只绑定WaterConstantBuffer，其他资源由Renderer管理
    
    // 绘制
    context->DrawIndexed(m_indexCount, 0, 0);
    
    // 恢复状态
    context->OMSetDepthStencilState(oldDepthState.Get(), oldStencilRef);
    context->OMSetBlendState(oldBlendState.Get(), oldBlendFactor, oldSampleMask);
    context->RSSetState(oldRasterizerState.Get());
}

