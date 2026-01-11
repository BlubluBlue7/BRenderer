#include "GrassSystem.h"
#include "Terrain_new.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;

// ============================================================================
// 构造函数和析构函数
// ============================================================================
GrassSystem::GrassSystem()
    : m_terrain(nullptr)
    , m_vertexCount(0)
    , m_indexCount(0)
    , m_instanceCount(0)
    , m_time(0.0f)
    , m_initialized(false)
{
}

GrassSystem::~GrassSystem()
{
    Cleanup();
}

// ============================================================================
// 初始化
// ============================================================================
bool GrassSystem::Initialize(ID3D11Device* device, const GrassParams& params, TerrainNew* terrain)
{
    OutputDebugStringW(L"[GrassSystem] Initialize called\n");
    
    if (!device || !terrain)
    {
        OutputDebugStringW(L"[GrassSystem] Initialize failed: device or terrain is null\n");
        return false;
    }
    
    m_params = params;
    m_terrain = terrain;
    
    OutputDebugStringW(L"[GrassSystem] Starting initialization...\n");
    
    // 1. 创建草的几何数据
    if (!CreateGrassGeometry(device))
    {
        OutputDebugStringW(L"[GrassSystem] Failed to create grass geometry\n");
        return false;
    }
    
    // 2. 生成草地实例
    GenerateInstances(terrain);
    
    // 3. 创建实例缓冲区
    if (!CreateInstanceBuffer(device))
    {
        OutputDebugStringW(L"[GrassSystem] Failed to create instance buffer\n");
        return false;
    }
    
    // 4. 创建着色器
    if (!CreateShaders(device))
    {
        OutputDebugStringW(L"[GrassSystem] Failed to create shaders\n");
        return false;
    }
    
    // 5. 创建纹理和采样器
    if (!CreateTextureAndSampler(device))
    {
        OutputDebugStringW(L"[GrassSystem] Failed to create texture and sampler\n");
        return false;
    }
    
    // 6. 创建常量缓冲区
    // 常量缓冲区结构：worldViewProj(16) + world(16) + view(16) + cameraPosition(4) + windParams(4) + grassParams(4) + renderParams(4) + lightDirection(4) + lightColor(4) + ambientColor(4) = 76 floats
    // 对齐到16字节边界：需要80 floats (320 bytes)
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth = sizeof(float) * 80; // 足够大的缓冲区（对齐到16字节）
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    
    HRESULT hr = device->CreateBuffer(&bd, nullptr, m_constantBuffer.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[GrassSystem] Failed to create constant buffer\n");
        return false;
    }
    
    // 7. 创建混合状态（Alpha blending - 标准alpha混合，用于透明草纹理）
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; // 源颜色乘以源alpha
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; // 目标颜色乘以(1-源alpha)
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD; // 相加
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; // 源alpha直接使用
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; // 目标alpha乘以(1-源alpha)
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    
    hr = device->CreateBlendState(&blendDesc, m_blendState.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[GrassSystem] Failed to create blend state\n");
        return false;
    }
    
    // 8. 创建深度模板状态（启用深度测试和写入，使用LESS_EQUAL以允许草与地形融合）
    D3D11_DEPTH_STENCIL_DESC depthDesc = {};
    depthDesc.DepthEnable = TRUE;
    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; // 启用深度写入，确保草正确遮挡
    depthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL; // 使用LESS_EQUAL，允许草与地形在同一高度时可见
    depthDesc.StencilEnable = FALSE;
    
    hr = device->CreateDepthStencilState(&depthDesc, m_depthStencilState.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[GrassSystem] Failed to create depth stencil state\n");
        return false;
    }
    
    // 9. 创建光栅化状态（实心模式）
    D3D11_RASTERIZER_DESC solidRasterizerDesc = {};
    solidRasterizerDesc.FillMode = D3D11_FILL_SOLID;
    solidRasterizerDesc.CullMode = D3D11_CULL_NONE; // 草是双面的，不剔除
    solidRasterizerDesc.FrontCounterClockwise = false;
    solidRasterizerDesc.DepthBias = 0;
    solidRasterizerDesc.DepthBiasClamp = 0.0f;
    solidRasterizerDesc.SlopeScaledDepthBias = 0.0f;
    solidRasterizerDesc.DepthClipEnable = true;
    solidRasterizerDesc.ScissorEnable = false;
    solidRasterizerDesc.MultisampleEnable = false;
    solidRasterizerDesc.AntialiasedLineEnable = false;
    
    hr = device->CreateRasterizerState(&solidRasterizerDesc, m_solidRasterizerState.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[GrassSystem] Failed to create solid rasterizer state\n");
        return false;
    }
    
    // 10. 创建光栅化状态（线框模式）
    D3D11_RASTERIZER_DESC wireframeRasterizerDesc = {};
    wireframeRasterizerDesc.FillMode = D3D11_FILL_WIREFRAME;
    wireframeRasterizerDesc.CullMode = D3D11_CULL_NONE; // 草是双面的，不剔除
    wireframeRasterizerDesc.FrontCounterClockwise = false;
    wireframeRasterizerDesc.DepthBias = -50; // 深度偏移，让线框稍微靠前
    wireframeRasterizerDesc.DepthBiasClamp = 0.0f;
    wireframeRasterizerDesc.SlopeScaledDepthBias = -0.5f;
    wireframeRasterizerDesc.DepthClipEnable = true;
    wireframeRasterizerDesc.ScissorEnable = false;
    wireframeRasterizerDesc.MultisampleEnable = false;
    wireframeRasterizerDesc.AntialiasedLineEnable = true; // 线条抗锯齿
    
    hr = device->CreateRasterizerState(&wireframeRasterizerDesc, m_wireframeRasterizerState.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[GrassSystem] Failed to create wireframe rasterizer state\n");
        return false;
    }
    
    m_initialized = true;
    
    wchar_t msg[256];
    swprintf_s(msg, L"[GrassSystem] Initialized successfully with %u instances\n", m_instanceCount);
    OutputDebugStringW(msg);
    
    if (m_instanceCount == 0)
    {
        OutputDebugStringW(L"[GrassSystem] WARNING: No instances generated! Grass will not be visible.\n");
    }
    
    return true;
}

// ============================================================================
// 创建草的几何数据（交叉四边形）
// ============================================================================
bool GrassSystem::CreateGrassGeometry(ID3D11Device* device)
{
    // 创建一个交叉的四边形（X形状），从任何角度看都可见
    // 每个四边形由两个三角形组成
    
    struct GrassVertex
    {
        XMFLOAT3 position;
        XMFLOAT3 normal;
        XMFLOAT2 texCoord;
    };
    
    std::vector<GrassVertex> vertices;
    std::vector<UINT> indices;
    
    float halfWidth = m_params.grassWidth * 0.5f;
    float height = m_params.grassHeight;
    
    // 第一个四边形（沿X轴，在YZ平面上）
    // 法线方向：对于YZ平面上的四边形，法线应该是(1,0,0)或(-1,0,0)
    // 我们使用(1,0,0)，因为草是双面的，这个方向并不重要
    // 底部
    vertices.push_back({XMFLOAT3(-halfWidth, 0.0f, 0.0f), XMFLOAT3(1, 0, 0), XMFLOAT2(0.0f, 1.0f)});
    vertices.push_back({XMFLOAT3(halfWidth, 0.0f, 0.0f), XMFLOAT3(1, 0, 0), XMFLOAT2(1.0f, 1.0f)});
    // 顶部
    vertices.push_back({XMFLOAT3(-halfWidth, height, 0.0f), XMFLOAT3(1, 0, 0), XMFLOAT2(0.0f, 0.0f)});
    vertices.push_back({XMFLOAT3(halfWidth, height, 0.0f), XMFLOAT3(1, 0, 0), XMFLOAT2(1.0f, 0.0f)});
    
    // 第一个四边形的索引
    UINT baseIndex = 0;
    indices.push_back(baseIndex + 0);
    indices.push_back(baseIndex + 2);
    indices.push_back(baseIndex + 1);
    indices.push_back(baseIndex + 1);
    indices.push_back(baseIndex + 2);
    indices.push_back(baseIndex + 3);
    
    // 第二个四边形（沿Z轴，在XY平面上）
    // 法线方向：对于XY平面上的四边形，法线应该是(0,0,1)或(0,0,-1)
    // 我们使用(0,0,1)
    baseIndex = 4;
    vertices.push_back({XMFLOAT3(0.0f, 0.0f, -halfWidth), XMFLOAT3(0, 0, 1), XMFLOAT2(0.0f, 1.0f)});
    vertices.push_back({XMFLOAT3(0.0f, 0.0f, halfWidth), XMFLOAT3(0, 0, 1), XMFLOAT2(1.0f, 1.0f)});
    vertices.push_back({XMFLOAT3(0.0f, height, -halfWidth), XMFLOAT3(0, 0, 1), XMFLOAT2(0.0f, 0.0f)});
    vertices.push_back({XMFLOAT3(0.0f, height, halfWidth), XMFLOAT3(0, 0, 1), XMFLOAT2(1.0f, 0.0f)});
    
    // 第二个四边形的索引
    indices.push_back(baseIndex + 0);
    indices.push_back(baseIndex + 2);
    indices.push_back(baseIndex + 1);
    indices.push_back(baseIndex + 1);
    indices.push_back(baseIndex + 2);
    indices.push_back(baseIndex + 3);
    
    m_vertexCount = static_cast<UINT>(vertices.size());
    m_indexCount = static_cast<UINT>(indices.size());
    
    // 创建顶点缓冲区
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(GrassVertex) * m_vertexCount;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = vertices.data();
    
    HRESULT hr = device->CreateBuffer(&bd, &initData, m_vertexBuffer.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    // 创建索引缓冲区
    bd.ByteWidth = sizeof(UINT) * m_indexCount;
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    initData.pSysMem = indices.data();
    
    hr = device->CreateBuffer(&bd, &initData, m_indexBuffer.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    return true;
}

// ============================================================================
// 生成草地实例
// ============================================================================
void GrassSystem::GenerateInstances(TerrainNew* terrain)
{
    if (!terrain)
        return;
    
    m_instances.clear();
    
    // 计算需要的实例数量
    float area = m_params.worldSizeX * m_params.worldSizeZ;
    int targetCount = static_cast<int>(area * m_params.density);
    
    // 生成随机位置
    // 注意：TerrainNew的GetHeightAt期望坐标范围是[-worldSizeX/2, worldSizeX/2]
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> xDist(-m_params.worldSizeX * 0.5f, m_params.worldSizeX * 0.5f);
    std::uniform_real_distribution<float> zDist(-m_params.worldSizeZ * 0.5f, m_params.worldSizeZ * 0.5f);
    std::uniform_real_distribution<float> rotDist(0.0f, 2.0f * 3.14159f);
    std::uniform_real_distribution<float> scaleDist(0.8f, 1.2f);
    std::uniform_real_distribution<float> colorDist(0.9f, 1.0f);
    
    m_instances.reserve(targetCount);
    
    for (int i = 0; i < targetCount; ++i)
    {
        float x = xDist(gen);
        float z = zDist(gen);
        
        // 从地形获取高度
        float height = terrain->GetHeightAt(x, z);
        
        // 检查高度范围
        if (height < m_params.minHeight || height > m_params.maxHeight)
            continue;
        
        GrassInstanceData instance;
        instance.position = XMFLOAT3(x, height, z);
        instance.rotation = rotDist(gen);
        instance.scale = scaleDist(gen);
        instance.heightVariation = scaleDist(gen);
        
        // 轻微的颜色变化（绿色系）
        float colorVar = colorDist(gen);
        instance.color = XMFLOAT4(colorVar, colorVar * 0.95f, colorVar * 0.8f, 1.0f);
        
        m_instances.push_back(instance);
    }
    
    m_instanceCount = static_cast<UINT>(m_instances.size());
    
    // 调试输出
    wchar_t msg[256];
    swprintf_s(msg, L"[GrassSystem] Generated %u instances from %d attempts (area=%.0f, density=%.1f)\n", 
               m_instanceCount, targetCount, area, m_params.density);
    OutputDebugStringW(msg);
    
    if (m_instanceCount == 0)
    {
        OutputDebugStringW(L"[GrassSystem] WARNING: No grass instances generated! Check height range and terrain.\n");
    }
}

// ============================================================================
// 创建实例缓冲区
// ============================================================================
bool GrassSystem::CreateInstanceBuffer(ID3D11Device* device)
{
    if (m_instances.empty())
        return false;
    
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(GrassInstanceData) * m_instanceCount;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = m_instances.data();
    
    HRESULT hr = device->CreateBuffer(&bd, &initData, m_instanceBuffer.GetAddressOf());
    return SUCCEEDED(hr);
}

// ============================================================================
// 创建着色器
// ============================================================================
bool GrassSystem::CreateShaders(ID3D11Device* device)
{
    // 编译顶点着色器
    OutputDebugStringW(L"[GrassSystem] Compiling vertex shader: Shaders/GrassVertexShader.hlsl\n");
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompileFromFile(
        L"Shaders/GrassVertexShader.hlsl",
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VS",
        "vs_5_0",
        0,
        0,
        m_vsBlob.GetAddressOf(),
        errorBlob.GetAddressOf()
    );
    
    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        OutputDebugStringW(L"[GrassSystem] Failed to compile vertex shader\n");
        return false;
    }
    
    hr = device->CreateVertexShader(m_vsBlob->GetBufferPointer(), m_vsBlob->GetBufferSize(),
                                     nullptr, m_vertexShader.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    // 编译像素着色器
    OutputDebugStringW(L"[GrassSystem] Compiling pixel shader: Shaders/GrassPixelShader.hlsl\n");
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    hr = D3DCompileFromFile(
        L"Shaders/GrassPixelShader.hlsl",
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "PS",
        "ps_5_0",
        0,
        0,
        psBlob.GetAddressOf(),
        errorBlob.GetAddressOf()
    );
    
    if (FAILED(hr))
    {
        wchar_t hrMsg[256];
        swprintf_s(hrMsg, L"[GrassSystem] Failed to compile pixel shader, HRESULT: 0x%08X\n", hr);
        OutputDebugStringW(hrMsg);
        if (errorBlob)
        {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        return false;
    }
    OutputDebugStringW(L"[GrassSystem] Pixel shader compiled successfully\n");
    
    hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                   nullptr, m_pixelShader.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    // 创建输入布局
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        // 顶点数据
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        // 实例数据
        {"POSITION", 1, DXGI_FORMAT_R32G32B32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"ROTATION", 0, DXGI_FORMAT_R32_FLOAT, 1, 12, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"SCALE", 0, DXGI_FORMAT_R32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"HEIGHT_VAR", 0, DXGI_FORMAT_R32_FLOAT, 1, 20, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"COLOR", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 24, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };
    
    hr = device->CreateInputLayout(layout, ARRAYSIZE(layout),
                                    m_vsBlob->GetBufferPointer(), m_vsBlob->GetBufferSize(),
                                    m_inputLayout.GetAddressOf());
    return SUCCEEDED(hr);
}

// ============================================================================
// 创建纹理和采样器
// ============================================================================
bool GrassSystem::CreateTextureAndSampler(ID3D11Device* device)
{
    // 创建一个简单的程序化纹理（如果没有外部纹理文件）
    // 这里创建一个1x1的绿色纹理作为占位符
    // 实际使用时应该加载真实的草纹理
    
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = 1;
    texDesc.Height = 1;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    
    // 创建绿色纹理数据
    UINT greenColor = 0xFF4A8B3A; // ARGB格式
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = &greenColor;
    initData.SysMemPitch = 4;
    
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    HRESULT hr = device->CreateTexture2D(&texDesc, &initData, texture.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    
    hr = device->CreateShaderResourceView(texture.Get(), &srvDesc, m_grassTextureSRV.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    // 创建采样器
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    
    hr = device->CreateSamplerState(&samplerDesc, m_samplerState.GetAddressOf());
    return SUCCEEDED(hr);
}

// ============================================================================
// 更新
// ============================================================================
void GrassSystem::Update(float deltaTime)
{
    if (!m_initialized)
        return;
    
    m_time += deltaTime;
}

// ============================================================================
// 更新常量缓冲区
// ============================================================================
void GrassSystem::UpdateConstantBuffer(ID3D11DeviceContext* context,
                                       const DirectX::XMFLOAT4X4& viewMatrix,
                                       const DirectX::XMFLOAT4X4& projMatrix,
                                       const DirectX::XMFLOAT3& cameraPosition,
                                       const DirectX::XMFLOAT4& lightDirection,
                                       const DirectX::XMFLOAT4& lightColor,
                                       const DirectX::XMFLOAT4& ambientColor)
{
    if (!context || !m_constantBuffer)
        return;
    
    // 计算世界-视图-投影矩阵
    XMMATRIX world = XMMatrixIdentity();
    XMMATRIX view = XMLoadFloat4x4(&viewMatrix);
    XMMATRIX proj = XMLoadFloat4x4(&projMatrix);
    XMMATRIX worldViewProj = world * view * proj;
    
    // 归一化风向
    XMVECTOR windDir = XMLoadFloat3(&m_params.windDirection);
    windDir = XMVector3Normalize(windDir);
    
    // 归一化光源方向
    XMVECTOR lightDir = XMLoadFloat4(&lightDirection);
    lightDir = XMVector3Normalize(lightDir);
    XMFLOAT4 lightDirNormalized;
    XMStoreFloat4(&lightDirNormalized, lightDir);
    
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        float* data = static_cast<float*>(mapped.pData);
        
        // worldViewProj (16 floats) - offset 0
        XMStoreFloat4x4((XMFLOAT4X4*)&data[0], XMMatrixTranspose(worldViewProj));
        
        // world (16 floats) - offset 16
        XMStoreFloat4x4((XMFLOAT4X4*)&data[16], XMMatrixTranspose(world));
        
        // view (16 floats) - offset 32
        XMStoreFloat4x4((XMFLOAT4X4*)&data[32], XMMatrixTranspose(view));
        
        // cameraPosition (4 floats) - offset 48
        data[48] = cameraPosition.x;
        data[49] = cameraPosition.y;
        data[50] = cameraPosition.z;
        data[51] = 0.0f;
        
        // windParams (4 floats) - offset 52
        XMFLOAT3 windDirFloat;
        XMStoreFloat3(&windDirFloat, windDir);
        data[52] = windDirFloat.x;
        data[53] = windDirFloat.y;
        data[54] = windDirFloat.z;
        data[55] = m_time;
        
        // grassParams (4 floats) - offset 56
        data[56] = m_params.grassHeight;
        data[57] = m_params.grassWidth;
        data[58] = m_params.windStrength;
        data[59] = m_params.windSpeed;
        
        // renderParams (4 floats) - offset 60
        data[60] = m_params.alphaTestThreshold;
        data[61] = 0.0f;
        data[62] = 0.0f;
        data[63] = 0.0f;
        
        // lightDirection (4 floats) - offset 64
        data[64] = lightDirNormalized.x;
        data[65] = lightDirNormalized.y;
        data[66] = lightDirNormalized.z;
        data[67] = 0.0f;
        
        // lightColor (4 floats) - offset 68
        data[68] = lightColor.x;
        data[69] = lightColor.y;
        data[70] = lightColor.z;
        data[71] = 0.0f;
        
        // ambientColor (4 floats) - offset 72
        data[72] = ambientColor.x;
        data[73] = ambientColor.y;
        data[74] = ambientColor.z;
        data[75] = 0.0f;
        
        // padding to 80 floats (对齐到16字节边界)
        data[76] = 0.0f;
        data[77] = 0.0f;
        data[78] = 0.0f;
        data[79] = 0.0f;
        
        context->Unmap(m_constantBuffer.Get(), 0);
    }
}

// ============================================================================
// 渲染
// ============================================================================
void GrassSystem::Render(ID3D11DeviceContext* context,
                         const DirectX::XMFLOAT4X4& viewMatrix,
                         const DirectX::XMFLOAT4X4& projMatrix,
                         const DirectX::XMFLOAT3& cameraPosition,
                         const DirectX::XMFLOAT4& lightDirection,
                         const DirectX::XMFLOAT4& lightColor,
                         const DirectX::XMFLOAT4& ambientColor)
{
    if (!m_initialized || !context || m_instanceCount == 0)
        return;
    
    // 更新常量缓冲区
    UpdateConstantBuffer(context, viewMatrix, projMatrix, cameraPosition, lightDirection, lightColor, ambientColor);
    
    // 保存当前状态
    ID3D11BlendState* oldBlendState = nullptr;
    FLOAT blendFactor[4] = {0};
    UINT sampleMask = 0;
    context->OMGetBlendState(&oldBlendState, blendFactor, &sampleMask);
    
    ID3D11DepthStencilState* oldDepthState = nullptr;
    UINT stencilRef = 0;
    context->OMGetDepthStencilState(&oldDepthState, &stencilRef);
    
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> oldRasterizerState;
    context->RSGetState(oldRasterizerState.GetAddressOf());
    
    // 检查当前是否是线框模式
    bool isWireframe = false;
    if (oldRasterizerState)
    {
        D3D11_RASTERIZER_DESC desc;
        oldRasterizerState->GetDesc(&desc);
        isWireframe = (desc.FillMode == D3D11_FILL_WIREFRAME);
    }
    
    // 设置光栅化状态（根据当前模式）
    if (isWireframe && m_wireframeRasterizerState)
    {
        context->RSSetState(m_wireframeRasterizerState.Get());
    }
    else if (m_solidRasterizerState)
    {
        context->RSSetState(m_solidRasterizerState.Get());
    }
    
    // 设置混合状态
    context->OMSetBlendState(m_blendState.Get(), nullptr, 0xFFFFFFFF);
    context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);
    
    // 设置着色器
    context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    context->IASetInputLayout(m_inputLayout.Get());
    
    // 设置常量缓冲区
    ID3D11Buffer* cb = m_constantBuffer.Get();
    context->VSSetConstantBuffers(0, 1, &cb);
    context->PSSetConstantBuffers(0, 1, &cb);
    
    // 设置纹理和采样器
    ID3D11ShaderResourceView* srv = m_grassTextureSRV.Get();
    context->PSSetShaderResources(0, 1, &srv);
    ID3D11SamplerState* sampler = m_samplerState.Get();
    context->PSSetSamplers(0, 1, &sampler);
    
    // 设置顶点和索引缓冲区
    UINT strides[] = {sizeof(float) * 8, sizeof(GrassInstanceData)}; // 顶点: pos(3) + normal(3) + tex(2)
    UINT offsets[] = {0, 0};
    ID3D11Buffer* buffers[] = {m_vertexBuffer.Get(), m_instanceBuffer.Get()};
    context->IASetVertexBuffers(0, 2, buffers, strides, offsets);
    context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    
    // 绘制所有实例
    // 注意：完整的视锥剔除和距离剔除需要使用GPU计算着色器进行剔除
    // 然后使用间接绘制（indirect draw），这需要较大的重构
    // 当前实现渲染所有实例，由GPU的视锥剔除和early-Z测试来处理不可见的草
    context->DrawIndexedInstanced(m_indexCount, m_instanceCount, 0, 0, 0);
    
    // 恢复状态
    context->OMSetBlendState(oldBlendState, blendFactor, sampleMask);
    context->OMSetDepthStencilState(oldDepthState, stencilRef);
    if (oldRasterizerState)
        context->RSSetState(oldRasterizerState.Get());
    
    if (oldBlendState) oldBlendState->Release();
    if (oldDepthState) oldDepthState->Release();
}

// ============================================================================
// 清理
// ============================================================================
void GrassSystem::Cleanup()
{
    m_vertexBuffer.Reset();
    m_indexBuffer.Reset();
    m_instanceBuffer.Reset();
    m_vertexShader.Reset();
    m_pixelShader.Reset();
    m_inputLayout.Reset();
    m_vsBlob.Reset();
    m_grassTextureSRV.Reset();
    m_samplerState.Reset();
    m_constantBuffer.Reset();
    m_blendState.Reset();
    m_depthStencilState.Reset();
    
    m_instances.clear();
    m_instanceCount = 0;
    m_initialized = false;
}

