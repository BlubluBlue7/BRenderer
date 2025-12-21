#include "Renderer.h"
#include "MeshMgr.h"
#include "Camera.h"
#include "ModelLoader.h"
#include "MeshGPU.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <fstream>
#include <vector>
#include <cstring>
#include <memory>
#include <wincodec.h>  // WIC (Windows Imaging Component)
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "windowscodecs.lib")  // WIC库

using namespace DirectX;

// 常量缓冲区结构体（必须按 16 字节对齐）
// 注意：使用 XMFLOAT4X4 而不是 XMMATRIX，因为 XMMATRIX 是 SIMD 类型，不能直接放在结构体中
struct ConstantBuffer
{
    XMFLOAT4X4 world;        // 世界变换矩阵
    XMFLOAT4X4 view;         // 视图变换矩阵
    XMFLOAT4X4 projection;   // 投影变换矩阵
    XMFLOAT4X4 worldViewProj; // 组合矩阵
};

// PBR 光照常量缓冲区结构体（必须对齐到16字节边界，大小必须是16字节的倍数）
struct alignas(16) LightBuffer
{
    XMFLOAT3 lightDirection;  // 光源方向 (12 bytes)
    float lightIntensity;      // 光源强度 (4 bytes) -> 16 bytes total
    
    XMFLOAT3 lightColor;       // 光源颜色 (12 bytes)
    float padding1;            // 对齐填充 (4 bytes) -> 16 bytes total
    
    XMFLOAT3 cameraPosition;   // 相机位置 (12 bytes)
    float padding2;            // 对齐填充 (4 bytes) -> 16 bytes total
    
    // PBR 材质参数
    XMFLOAT3 albedo;           // 反照率（基础颜色）(12 bytes)
    float metallic;            // 金属度 (4 bytes) -> 16 bytes total
    
    float roughness;           // 粗糙度 (4 bytes)
    float padding3a;           // 对齐填充 (4 bytes)
    XMFLOAT3 ambientColor;     // 环境光颜色 (12 bytes)
    float padding3b;           // 对齐填充 (4 bytes) -> 16 bytes total
    // 总共: 16 * 6 = 96 bytes (16字节的倍数)
};

// ============================================================================
// 初始化渲染器
// ============================================================================
bool Renderer::Initialize(HWND hwnd, int width, int height)
{
    // 清除之前的错误信息
    m_lastError.clear();
    
    // 保存窗口尺寸
    m_width = width;
    m_height = height;
    
    // 获取exe路径（在函数开始处定义，供后续使用）
    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    // ========================================================================
    // 步骤 1: 配置交换链描述符
    // 交换链用于管理前后缓冲区，实现双缓冲渲染
    // ========================================================================
    DXGI_SWAP_CHAIN_DESC scDesc = {};
    scDesc.BufferCount = 1;                              // 后缓冲区数量（单缓冲）
    scDesc.BufferDesc.Width = width;                     // 缓冲区宽度
    scDesc.BufferDesc.Height = height;                    // 缓冲区高度
    scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // 32位RGBA格式（每通道8位，归一化到0-1）
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 用作渲染目标
    scDesc.OutputWindow = hwnd;                           // 输出窗口句柄
    scDesc.SampleDesc.Count = 1;                         // 多重采样数量（1表示不启用）
    scDesc.SampleDesc.Quality = 0;                       // 多重采样质量
    scDesc.Windowed = TRUE;                              // 窗口模式
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;        // 交换后丢弃旧内容

    // ========================================================================
    // 步骤 2: 创建 D3D11 设备和交换链
    // 这是整个渲染系统的核心，创建了设备、设备上下文和交换链
    // ========================================================================
    D3D_FEATURE_LEVEL featureLevel;  // 返回的 D3D 功能级别
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,                                    // 使用默认适配器（主显卡）
        D3D_DRIVER_TYPE_HARDWARE,                   // 使用硬件加速
        nullptr,                                    // 不使用软件驱动
        0,                                          // 标志位（0表示无特殊标志）
        nullptr,                                    // 功能级别数组（null表示使用默认）
        0,                                          // 功能级别数组大小
        D3D11_SDK_VERSION,                          // SDK 版本
        &scDesc,                                    // 交换链描述符
        m_swapChain.GetAddressOf(),                 // 输出的交换链指针
        m_device.GetAddressOf(),                    // 输出的设备指针
        &featureLevel,                              // 返回的功能级别
        m_context.GetAddressOf()                    // 输出的设备上下文指针
    );

    if (FAILED(hr))
    {
        m_lastError = L"Failed to create D3D11 device and swap chain. HRESULT: 0x";
        wchar_t hrStr[16];
        swprintf_s(hrStr, L"%08X", hr);
        m_lastError += hrStr;
        return false;
    }

    // ========================================================================
    // 步骤 3: 获取后缓冲区并创建渲染目标视图（RTV）
    // RTV 用于告诉 GPU 将渲染结果输出到哪里
    // ========================================================================
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    // 从交换链获取后缓冲区纹理
    hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr))
        return false;

    // 创建渲染目标视图，将纹理绑定为渲染目标
    hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_rtv.GetAddressOf());
    if (FAILED(hr))
        return false;

    // ========================================================================
    // 步骤 4: 编译和创建 Shader
    // Shader 定义了如何渲染顶点和像素
    // ========================================================================
    if (!CreateShaders())
    {
        if (m_lastError.empty())
            m_lastError = L"Failed to create shaders. Check if Shader files exist in the correct path.";
        return false;
    }

    // ========================================================================
    // 步骤 5: 创建常量缓冲区
    // 常量缓冲区用于在 CPU 和 GPU 之间传递数据（矩阵、光照参数等）
    // ========================================================================
    if (!CreateConstantBuffers())
        return false;

    // ========================================================================
    // 步骤 6: 创建输入布局
    // 输入布局描述了顶点数据的格式，告诉 GPU 如何解析顶点缓冲区
    // ========================================================================
    if (!CreateInputLayout())
        return false;

    // ========================================================================
    // 步骤 7: 创建纹理采样器
    // ========================================================================
    if (!CreateSamplerState())
        return false;

    // ========================================================================
    // 步骤 8: 加载纹理（如果存在）
    // ========================================================================
    // 尝试加载纹理，如果失败则创建默认白色纹理
    bool textureLoaded = false;
    if (exePath[0] != 0)
    {
        char exePathA[MAX_PATH];
        WideCharToMultiByte(CP_ACP, 0, exePath, -1, exePathA, MAX_PATH, nullptr, nullptr);
        std::string exeDir = exePathA;
        size_t lastSlash = exeDir.find_last_of("\\/");
        if (lastSlash != std::string::npos)
        {
            exeDir = exeDir.substr(0, lastSlash + 1);
            
            std::string projectRoot = exeDir;
            for (int i = 0; i < 2; ++i)
            {
                size_t slash = projectRoot.find_last_of("\\/", projectRoot.length() - 2);
                if (slash != std::string::npos)
                    projectRoot = projectRoot.substr(0, slash + 1);
            }
            
            // 尝试加载纹理文件（按优先级顺序尝试）
            // 优先加载BaseColor（基础颜色）纹理
            std::vector<std::wstring> texturePaths = {
                // 优先尝试MI_Manny_01的基础颜色纹理
                std::wstring(projectRoot.begin(), projectRoot.end()) + L"Res/MI_Manny_01_New_BaseColor_0.png",
                std::wstring(projectRoot.begin(), projectRoot.end()) + L"Res/MI_Manny_01_New_BaseColor_0.jpg",
                std::wstring(projectRoot.begin(), projectRoot.end()) + L"Res/MI_Manny_01_New_BaseColor_0.tga",
                // 然后尝试MI_Manny_02的基础颜色纹理
                std::wstring(projectRoot.begin(), projectRoot.end()) + L"Res/MI_Manny_02_New_BaseColor_1.png",
                std::wstring(projectRoot.begin(), projectRoot.end()) + L"Res/MI_Manny_02_New_BaseColor_1.jpg",
                std::wstring(projectRoot.begin(), projectRoot.end()) + L"Res/MI_Manny_02_New_BaseColor_1.tga",
                // 备用路径（exe目录下）
                std::wstring(exeDir.begin(), exeDir.end()) + L"Res/MI_Manny_01_New_BaseColor_0.png",
                std::wstring(exeDir.begin(), exeDir.end()) + L"Res/MI_Manny_02_New_BaseColor_1.png",
                // 通用纹理文件名
                std::wstring(projectRoot.begin(), projectRoot.end()) + L"Res/texture.png",
                std::wstring(projectRoot.begin(), projectRoot.end()) + L"Res/texture.jpg",
                std::wstring(exeDir.begin(), exeDir.end()) + L"Res/texture.png"
            };
            
            for (const auto& texPath : texturePaths)
            {
                if (LoadTexture(texPath))
                {
                    textureLoaded = true;
                    // 输出调试信息：纹理加载成功
                    OutputDebugStringW(L"Texture loaded successfully: ");
                    OutputDebugStringW(texPath.c_str());
                    OutputDebugStringW(L"\n");
                    break;
                }
            }
        }
    }
    
    // 如果纹理加载失败，创建默认白色纹理
    if (!textureLoaded)
    {
        OutputDebugStringW(L"Texture not found, using default white texture.\n");
        if (!CreateDefaultTexture())
            return false;
    }

    // ========================================================================
    // 步骤 9: 初始化网格管理器
    // MeshMgr 负责管理所有网格资源
    // ========================================================================
    m_meshMgr = new MeshMgr(m_device.Get(), m_context.Get());

    // ========================================================================
    // 步骤 10: 加载模型或创建默认三角形
    // ========================================================================
    std::vector<Vertex> verts;
    std::vector<uint32_t> indices;
    
    // 尝试加载模型文件（如果存在）
    // 支持从多个路径加载：Res目录、exe目录、项目根目录
    bool modelLoaded = false;
    
    // 使用之前获取的exe路径
    if (exePath[0] != 0)
    {
        char exePathA[MAX_PATH];
        WideCharToMultiByte(CP_ACP, 0, exePath, -1, exePathA, MAX_PATH, nullptr, nullptr);
        std::string exeDir = exePathA;
        size_t lastSlash = exeDir.find_last_of("\\/");
        if (lastSlash != std::string::npos)
        {
            exeDir = exeDir.substr(0, lastSlash + 1);
            
            // 向上查找项目根目录
            std::string projectRoot = exeDir;
            for (int i = 0; i < 2; ++i)
            {
                size_t slash = projectRoot.find_last_of("\\/", projectRoot.length() - 2);
                if (slash != std::string::npos)
                    projectRoot = projectRoot.substr(0, slash + 1);
            }
            
            // 尝试多个可能的路径（优先尝试FBX，然后OBJ）
            std::vector<std::string> pathsToTry = {
                projectRoot + "Res/SKM_Manny_Simple.FBX",  // 项目根目录下的FBX文件
                projectRoot + "Res/SKM_Manny_Simple.fbx",  // 小写扩展名
                projectRoot + "Res/SKM_Manny_Simple.obj",  // 项目根目录下的OBJ文件
                exeDir + "Res/SKM_Manny_Simple.FBX",       // exe目录下的Res文件夹中的FBX
                exeDir + "Res/SKM_Manny_Simple.fbx",
                exeDir + "Res/SKM_Manny_Simple.obj",       // exe目录下的Res文件夹中的OBJ
                "Res/SKM_Manny_Simple.FBX",                // 相对路径FBX
                "Res/SKM_Manny_Simple.fbx",
                "Res/SKM_Manny_Simple.obj",                // 相对路径OBJ
                projectRoot + "Res/model.FBX",             // 通用模型名FBX
                projectRoot + "Res/model.obj",             // 通用模型名OBJ
                exeDir + "Res/model.FBX",
                exeDir + "Res/model.obj",
                "model.obj",                               // exe目录下的模型
                exeDir + "model.obj"
            };
            
            // 尝试加载模型
            for (const auto& path : pathsToTry)
            {
                if (ModelLoader::LoadFromFile(path, verts, indices))
                {
                    modelLoaded = true;
                    m_meshMgr->CreateMesh("Model", verts, indices);
                    break;
                }
            }
        }
    }
    
    if (!modelLoaded)
    {
        // 加载失败，使用默认三角形
        verts = {
            // 顶部顶点 - 红色材质
            {{0.0f,  0.5f, 0.0f},  {0.0f, 0.0f, 1.0f},  {1.0f, 0.0f, 0.0f},  {0.5f, 0.0f}},
            // 右下顶点 - 绿色材质
            {{0.5f, -0.5f, 0.0f},  {0.0f, 0.0f, 1.0f},  {0.0f, 1.0f, 0.0f},  {1.0f, 1.0f}},
            // 左下顶点 - 蓝色材质
            {{-0.5f,-0.5f, 0.0f},  {0.0f, 0.0f, 1.0f},  {0.0f, 0.0f, 1.0f},  {0.0f, 1.0f}}
        };
        m_meshMgr->CreateMesh("Triangle", verts);
    }

    return true;
}

// ============================================================================
// 渲染一帧
// ============================================================================
void Renderer::RenderFrame(float deltaTime)
{
    // 安全检查：确保设备上下文和渲染目标视图已创建
    if (!m_context || !m_rtv) return;

    // ========================================================================
    // 步骤 1: 设置渲染目标
    // 告诉 GPU 将渲染结果输出到哪个渲染目标（这里是后缓冲区）
    // ========================================================================
    m_context->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);
    // 参数说明：
    // - 1: 渲染目标数量
    // - m_rtv.GetAddressOf(): 渲染目标视图数组
    // - nullptr: 深度模板缓冲区（这里不使用）

    // ========================================================================
    // 步骤 2: 清空渲染目标
    // 用指定颜色填充整个渲染目标，清除上一帧的内容
    // ========================================================================
    float clearColor[4] = { 0.2f, 0.3f, 0.6f, 1.0f }; // RGBA: 深蓝色背景
    m_context->ClearRenderTargetView(m_rtv.Get(), clearColor);

    // ========================================================================
    // 步骤 3: 设置视口
    // 视口定义了渲染区域在窗口中的位置和大小
    // ========================================================================
    D3D11_VIEWPORT vp{};
    vp.Width = (float)m_width;    // 视口宽度（像素）
    vp.Height = (float)m_height;  // 视口高度（像素）
    vp.MinDepth = 0.0f;           // 最小深度值（用于深度测试）
    vp.MaxDepth = 1.0f;           // 最大深度值（用于深度测试）
    vp.TopLeftX = 0.0f;           // 视口左上角 X 坐标
    vp.TopLeftY = 0.0f;          // 视口左上角 Y 坐标
    m_context->RSSetViewports(1, &vp);

    // ========================================================================
    // 步骤 4: 更新常量缓冲区
    // 将变换矩阵和光照参数传递给 Shader
    // ========================================================================
    UpdateConstantBuffers();

    // ========================================================================
    // 步骤 5: 设置 Shader 和输入布局
    // 将编译好的 Shader 和输入布局绑定到渲染管线
    // ========================================================================
    // 设置输入布局：告诉 GPU 如何解析顶点数据
    m_context->IASetInputLayout(m_inputLayout.Get());
    // 设置顶点着色器：处理每个顶点的变换
    m_context->VSSetShader(m_vs.Get(), nullptr, 0);
    // 设置像素着色器：处理每个像素的颜色
    m_context->PSSetShader(m_ps.Get(), nullptr, 0);
    
    // 绑定常量缓冲区到顶点着色器（register b0）
    m_context->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    // 绑定光照常量缓冲区到像素着色器（register b1）
    m_context->PSSetConstantBuffers(1, 1, m_lightBuffer.GetAddressOf());
    
    // 绑定纹理和采样器到像素着色器
    if (m_textureSRV)
    {
        m_context->PSSetShaderResources(0, 1, m_textureSRV.GetAddressOf());
    }
    if (m_samplerState)
    {
        m_context->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());
    }

    // ========================================================================
    // 步骤 6: 渲染网格
    // 绑定网格资源并执行绘制命令
    // ========================================================================
    // 尝试渲染模型，如果不存在则渲染三角形
    std::shared_ptr<MeshGPU> modelGPU = m_meshMgr->GetMeshGPU("Model");
    if (!modelGPU)
        modelGPU = m_meshMgr->GetMeshGPU("Triangle");
    
    if (modelGPU)
    {
        // 绑定顶点缓冲区和索引缓冲区到输入装配阶段
        modelGPU->Bind(m_context.Get());
        // 执行绘制命令，GPU 开始渲染
        modelGPU->Draw(m_context.Get());
    }

    // ========================================================================
    // 步骤 7: 呈现到屏幕
    // 将后缓冲区的内容交换到前缓冲区，显示在屏幕上
    // ========================================================================
    // 参数说明：
    // - 1: 垂直同步间隔（1表示等待垂直同步，实现60fps）
    // - 0: 标志位（0表示无特殊标志）
    m_swapChain->Present(1, 0);
}

// ============================================================================
// 清理资源
// ============================================================================
void Renderer::Cleanup()
{
    // 释放网格管理器
    if (m_meshMgr)
    {
        delete m_meshMgr;
        m_meshMgr = nullptr;
    }

    // 释放所有 D3D11 资源（按创建顺序的逆序释放）
    m_lightBuffer.Reset();   // 光照常量缓冲区
    m_constantBuffer.Reset(); // 常量缓冲区
    m_inputLayout.Reset();   // 输入布局
    m_ps.Reset();            // 像素着色器
    m_vs.Reset();            // 顶点着色器
    m_vsBlob.Reset();        // 顶点着色器编译后的二进制数据
    m_textureSRV.Reset();    // 纹理资源视图
    m_samplerState.Reset();  // 采样器状态
    m_rtv.Reset();           // 渲染目标视图
    m_swapChain.Reset();     // 交换链
    m_context.Reset();       // 设备上下文
    m_device.Reset();        // 设备
}

// ============================================================================
// 创建采样器状态
// ============================================================================
bool Renderer::CreateSamplerState()
{
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;  // 线性过滤
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;      // U方向包裹
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;      // V方向包裹
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;      // W方向包裹
    samplerDesc.MipLODBias = 0.0f;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.BorderColor[0] = 0.0f;
    samplerDesc.BorderColor[1] = 0.0f;
    samplerDesc.BorderColor[2] = 0.0f;
    samplerDesc.BorderColor[3] = 0.0f;
    samplerDesc.MinLOD = 0.0f;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    HRESULT hr = m_device->CreateSamplerState(&samplerDesc, m_samplerState.GetAddressOf());
    return SUCCEEDED(hr);
}

// ============================================================================
// 创建默认纹理（白色纹理）
// ============================================================================
bool Renderer::CreateDefaultTexture()
{
    // 创建1x1白色纹理
    const UINT width = 1;
    const UINT height = 1;
    UINT8 pixels[4] = { 255, 255, 255, 255 };  // RGBA: 白色

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = 0;
    texDesc.MiscFlags = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = pixels;
    initData.SysMemPitch = width * 4;  // 每行字节数（RGBA = 4字节）
    initData.SysMemSlicePitch = 0;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    HRESULT hr = m_device->CreateTexture2D(&texDesc, &initData, texture.GetAddressOf());
    if (FAILED(hr))
        return false;

    // 创建着色器资源视图
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;

    hr = m_device->CreateShaderResourceView(texture.Get(), &srvDesc, m_textureSRV.GetAddressOf());
    return SUCCEEDED(hr);
}

// ============================================================================
// 加载纹理（使用WIC加载图片文件）
// 注意：完整实现需要使用WIC或DirectXTex库来加载图片文件
// ============================================================================
bool Renderer::LoadTexture(const std::wstring& filename)
{
    // TODO: 实现WIC图片加载
    // 暂时返回false，使用默认纹理
    // 需要使用Windows Imaging Component (WIC) 或 DirectXTex 库来加载 PNG/JPG 等格式
    return false;
}

// ============================================================================
// 从文件编译 Shader
// ============================================================================
bool Renderer::CompileShaderFromFile(const wchar_t* filename, const char* entryPoint, const char* target, ID3DBlob** blob)
{
    if (!filename || !entryPoint || !target || !blob)
        return false;

    // 尝试多个可能的路径
    std::vector<std::wstring> pathsToTry;
    pathsToTry.push_back(filename);  // 原始路径（相对于当前工作目录）
    
    // 尝试相对于exe目录的路径（向上两级：x64/Debug -> 项目根目录）
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0)
    {
        std::wstring exeDir = exePath;
        size_t lastSlash = exeDir.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos)
        {
            exeDir = exeDir.substr(0, lastSlash + 1);  // exe目录
            pathsToTry.push_back(exeDir + filename);    // x64/Debug/Shaders/...
            
            // 尝试项目根目录 - 向上两级：x64/Debug -> 项目根
            std::wstring projectRoot = exeDir;
            size_t pos = projectRoot.length() - 1;
            int levelsUp = 0;
            while (pos > 0 && levelsUp < 2)
            {
                pos = projectRoot.find_last_of(L"\\/", pos - 1);
                if (pos != std::wstring::npos && pos > 0)
                {
                    projectRoot = projectRoot.substr(0, pos + 1);
                    levelsUp++;
                }
                else
                    break;
            }
            if (levelsUp == 2)
            {
                pathsToTry.push_back(projectRoot + filename);  // 项目根/Shaders/...
            }
        }
    }

    // 尝试打开文件 - 使用Windows API以确保正确处理Unicode路径
    HANDLE hFile = INVALID_HANDLE_VALUE;
    std::wstring actualPath;
    std::wstring triedPaths;
    
    for (const auto& path : pathsToTry)
    {
        hFile = CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        
        if (hFile != INVALID_HANDLE_VALUE)
        {
            actualPath = path;
            break;
        }
        else
        {
            if (!triedPaths.empty()) triedPaths += L"\n";
            triedPaths += path;
        }
    }

    if (hFile == INVALID_HANDLE_VALUE)
    {
        // 文件未找到，返回false让CreateShaders设置错误信息
        return false;
    }

    // 获取文件大小
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize))
    {
        CloseHandle(hFile);
        return false;
    }

    // 读取文件内容
    std::vector<char> shaderCode(fileSize.QuadPart + 1);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, shaderCode.data(), (DWORD)fileSize.QuadPart, &bytesRead, nullptr))
    {
        CloseHandle(hFile);
        return false;
    }
    shaderCode[bytesRead] = '\0';
    CloseHandle(hFile);
    
    // 将实际使用的路径转换为多字节字符串用于编译错误信息
    char actualPathA[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, actualPath.c_str(), -1, actualPathA, MAX_PATH, nullptr, nullptr);

    // 编译 Shader
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompile(
        shaderCode.data(),                           // Shader 源代码
        bytesRead,                                   // 源代码长度
        actualPathA,                                 // 源文件名（用于错误报告）
        nullptr,                                     // 宏定义数组
        nullptr,                                     // Include 处理器
        entryPoint,                                  // 入口点函数名
        target,                                      // Shader 模型版本（如 "vs_5_0", "ps_5_0"）
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, // 编译标志
        0,                                           // 效果标志（已废弃）
        blob,                                        // 输出的编译后二进制数据
        errorBlob.GetAddressOf()                     // 错误信息输出
    );

    // 如果编译失败，输出错误信息
    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        return false;
    }

    return true;
}

// ============================================================================
// 从字符串编译 Shader（备用方法）
// ============================================================================
bool Renderer::CompileShader(const char* shaderCode, const char* entryPoint, const char* target, ID3DBlob** blob)
{
    if (!shaderCode || !entryPoint || !target || !blob)
        return false;

    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompile(
        shaderCode,
        strlen(shaderCode),
        nullptr,
        nullptr,
        nullptr,
        entryPoint,
        target,
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
        0,
        blob,
        errorBlob.GetAddressOf()
    );

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        return false;
    }

    return true;
}

// ============================================================================
// 创建所有 Shader
// ============================================================================
bool Renderer::CreateShaders()
{
    // ========================================================================
    // 编译顶点着色器
    // ========================================================================
    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    // 从文件加载并编译顶点着色器
    if (!CompileShaderFromFile(L"Shaders/VertexShader.hlsl", "VS", "vs_5_0", vsBlob.GetAddressOf()))
    {
        wchar_t exePath[MAX_PATH];
        std::wstring errorMsg = L"Failed to compile VertexShader.hlsl.\nTried paths:\n";
        errorMsg += L"  - Shaders/VertexShader.hlsl (relative to current directory)\n";
        if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0)
        {
            std::wstring exeDir = exePath;
            size_t lastSlash = exeDir.find_last_of(L"\\/");
            if (lastSlash != std::wstring::npos)
            {
                exeDir = exeDir.substr(0, lastSlash + 1);
                errorMsg += L"  - " + exeDir + L"Shaders/VertexShader.hlsl\n";
                
                std::wstring projectRoot = exeDir;
                size_t pos = projectRoot.length() - 1;
                int levelsUp = 0;
                while (pos > 0 && levelsUp < 2)
                {
                    pos = projectRoot.find_last_of(L"\\/", pos - 1);
                    if (pos != std::wstring::npos && pos > 0)
                    {
                        projectRoot = projectRoot.substr(0, pos + 1);
                        levelsUp++;
                    }
                    else
                        break;
                }
                if (levelsUp == 2)
                {
                    errorMsg += L"  - " + projectRoot + L"Shaders/VertexShader.hlsl";
                }
            }
        }
        m_lastError = errorMsg;
        return false;
    }

    // 创建顶点着色器对象
    HRESULT hr = m_device->CreateVertexShader(
        vsBlob->GetBufferPointer(),  // 编译后的二进制数据指针
        vsBlob->GetBufferSize(),     // 二进制数据大小
        nullptr,                     // 类链接（用于高级特性，这里不使用）
        m_vs.GetAddressOf()          // 输出的顶点着色器对象
    );
    if (FAILED(hr))
    {
        m_lastError = L"Failed to create vertex shader object.";
        return false;
    }

    // ========================================================================
    // 编译像素着色器
    // ========================================================================
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    // 从文件加载并编译像素着色器
    if (!CompileShaderFromFile(L"Shaders/PixelShader.hlsl", "PS", "ps_5_0", psBlob.GetAddressOf()))
    {
        wchar_t exePath[MAX_PATH];
        std::wstring errorMsg = L"Failed to compile PixelShader.hlsl.\nTried paths:\n";
        errorMsg += L"  - Shaders/PixelShader.hlsl (relative to current directory)\n";
        if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0)
        {
            std::wstring exeDir = exePath;
            size_t lastSlash = exeDir.find_last_of(L"\\/");
            if (lastSlash != std::wstring::npos)
            {
                exeDir = exeDir.substr(0, lastSlash + 1);
                errorMsg += L"  - " + exeDir + L"Shaders/PixelShader.hlsl\n";
                
                std::wstring projectRoot = exeDir;
                size_t pos = projectRoot.length() - 1;
                int levelsUp = 0;
                while (pos > 0 && levelsUp < 2)
                {
                    pos = projectRoot.find_last_of(L"\\/", pos - 1);
                    if (pos != std::wstring::npos && pos > 0)
                    {
                        projectRoot = projectRoot.substr(0, pos + 1);
                        levelsUp++;
                    }
                    else
                        break;
                }
                if (levelsUp == 2)
                {
                    errorMsg += L"  - " + projectRoot + L"Shaders/PixelShader.hlsl";
                }
            }
        }
        m_lastError = errorMsg;
        return false;
    }

    // 创建像素着色器对象
    hr = m_device->CreatePixelShader(
        psBlob->GetBufferPointer(),  // 编译后的二进制数据指针
        psBlob->GetBufferSize(),     // 二进制数据大小
        nullptr,                     // 类链接（用于高级特性，这里不使用）
        m_ps.GetAddressOf()          // 输出的像素着色器对象
    );
    if (FAILED(hr))
        return false;

    // 保存顶点着色器的编译数据，用于创建输入布局
    // 注意：输入布局需要知道顶点着色器期望的输入格式
    m_vsBlob = vsBlob;
    return true;
}

// ============================================================================
// 创建输入布局
// 输入布局描述了顶点数据的格式，告诉 GPU 如何从顶点缓冲区中读取数据
// ============================================================================
bool Renderer::CreateInputLayout()
{
    // 必须先用顶点着色器编译数据创建输入布局
    if (!m_vsBlob)
        return false;

    // ========================================================================
    // 定义输入元素描述数组
    // 每个元素描述顶点数据中的一个字段（位置、法线、颜色等）
    // ========================================================================
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        // 位置字段
        {
            "POSITION",                          // 语义名（对应 Shader 中的 : POSITION）
            0,                                   // 语义索引（如果有多个同名语义）
            DXGI_FORMAT_R32G32B32_FLOAT,        // 数据格式（3个32位浮点数）
            0,                                   // 输入槽索引（使用哪个顶点缓冲区）
            0,                                   // 偏移量（从顶点数据开始处的字节偏移）
            D3D11_INPUT_PER_VERTEX_DATA,        // 输入分类（每顶点数据）
            0                                    // 实例数据步进率（非实例化时为0）
        },
        // 法线字段
        {
            "NORMAL",                            // 语义名（对应 Shader 中的 : NORMAL）
            0,                                   // 语义索引
            DXGI_FORMAT_R32G32B32_FLOAT,        // 数据格式（3个32位浮点数）
            0,                                   // 输入槽索引
            12,                                  // 偏移量（位置占12字节：3 * 4）
            D3D11_INPUT_PER_VERTEX_DATA,        // 输入分类（每顶点数据）
            0                                    // 实例数据步进率
        },
        // 颜色字段
        {
            "COLOR",                             // 语义名（对应 Shader 中的 : COLOR）
            0,                                   // 语义索引
            DXGI_FORMAT_R32G32B32_FLOAT,        // 数据格式（3个32位浮点数）
            0,                                   // 输入槽索引
            24,                                  // 偏移量（位置12字节 + 法线12字节 = 24字节）
            D3D11_INPUT_PER_VERTEX_DATA,        // 输入分类（每顶点数据）
            0                                    // 实例数据步进率
        },
        // 纹理坐标字段
        {
            "TEXCOORD",                          // 语义名（对应 Shader 中的 : TEXCOORD）
            0,                                   // 语义索引
            DXGI_FORMAT_R32G32_FLOAT,           // 数据格式（2个32位浮点数）
            0,                                   // 输入槽索引
            36,                                  // 偏移量（位置12 + 法线12 + 颜色12 = 36字节）
            D3D11_INPUT_PER_VERTEX_DATA,        // 输入分类（每顶点数据）
            0                                    // 实例数据步进率
        }
    };

    // 创建输入布局对象
    HRESULT hr = m_device->CreateInputLayout(
        layout,                          // 输入元素描述数组
        ARRAYSIZE(layout),               // 元素数量
        m_vsBlob->GetBufferPointer(),    // 顶点着色器编译数据（用于验证格式匹配）
        m_vsBlob->GetBufferSize(),      // 编译数据大小
        m_inputLayout.GetAddressOf()    // 输出的输入布局对象
    );

    return SUCCEEDED(hr);
}

// ============================================================================
// 创建常量缓冲区
// ============================================================================
bool Renderer::CreateConstantBuffers()
{
    // ========================================================================
    // 创建变换矩阵常量缓冲区
    // ========================================================================
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;              // 动态缓冲区（CPU 可更新）
    cbDesc.ByteWidth = sizeof(ConstantBuffer);       // 缓冲区大小
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;   // 绑定为常量缓冲区
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;  // CPU 可写入
    cbDesc.MiscFlags = 0;
    cbDesc.StructureByteStride = 0;

    HRESULT hr = m_device->CreateBuffer(&cbDesc, nullptr, m_constantBuffer.GetAddressOf());
    if (FAILED(hr))
        return false;

    // ========================================================================
    // 创建光照参数常量缓冲区
    // 注意：常量缓冲区的大小必须是16字节的倍数
    // ========================================================================
    D3D11_BUFFER_DESC lightDesc = {};
    lightDesc.Usage = D3D11_USAGE_DYNAMIC;
    // 确保大小是16字节的倍数（向上取整到最近的16字节倍数）
    UINT lightBufferSize = sizeof(LightBuffer);
    lightDesc.ByteWidth = (lightBufferSize + 15) & ~15;  // 向上对齐到16字节边界
    lightDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    lightDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    lightDesc.MiscFlags = 0;
    lightDesc.StructureByteStride = 0;

    hr = m_device->CreateBuffer(&lightDesc, nullptr, m_lightBuffer.GetAddressOf());
    if (FAILED(hr))
    {
        // 添加详细错误信息
        wchar_t errorMsg[256];
        swprintf_s(errorMsg, L"Failed to create light constant buffer. HRESULT: 0x%08X, Size: %u bytes", hr, lightBufferSize);
        m_lastError = errorMsg;
        return false;
    }

    return true;
}

// ============================================================================
// 更新常量缓冲区
// 每帧调用，更新变换矩阵和光照参数
// ============================================================================
void Renderer::UpdateConstantBuffers()
{
    // ========================================================================
    // 更新变换矩阵常量缓冲区
    // ========================================================================
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = m_context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr))
    {
        ConstantBuffer* cb = (ConstantBuffer*)mapped.pData;
        
        // 创建变换矩阵（使用 XMMATRIX 进行计算）
        // 世界矩阵：物体在世界空间中的位置和方向
        // 应用0.2倍缩放，使模型变小
        XMMATRIX scale = XMMatrixScaling(0.2f, 0.2f, 0.2f);
        XMMATRIX world = scale;  // 应用缩放
        
        // 视图矩阵和投影矩阵：从相机获取
        XMMATRIX view;
        XMMATRIX projection;
        if (m_camera)
        {
            // 使用相机的视图矩阵和投影矩阵
            view = m_camera->GetViewMatrix();
            float aspect = (float)m_width / (float)m_height;
            projection = m_camera->GetProjectionMatrix(aspect);
        }
        else
        {
            // 如果没有相机，使用默认值
            XMVECTOR eye = XMVectorSet(0.0f, 0.0f, -2.0f, 0.0f);
            XMVECTOR at = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
            XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            view = XMMatrixLookAtLH(eye, at, up);
            float aspect = (float)m_width / (float)m_height;
            projection = XMMatrixPerspectiveFovLH(XM_PI / 4.0f, aspect, 0.1f, 100.0f);
        }
        
        // 组合矩阵：世界-视图-投影矩阵
        XMMATRIX worldViewProj = world * view * projection;
        
        // 转置矩阵（HLSL 使用列主序，DirectXMath 使用行主序）
        // 并转换为 XMFLOAT4X4 存储到常量缓冲区
        XMStoreFloat4x4(&cb->world, XMMatrixTranspose(world));
        XMStoreFloat4x4(&cb->view, XMMatrixTranspose(view));
        XMStoreFloat4x4(&cb->projection, XMMatrixTranspose(projection));
        XMStoreFloat4x4(&cb->worldViewProj, XMMatrixTranspose(worldViewProj));
        
        m_context->Unmap(m_constantBuffer.Get(), 0);
    }

    // ========================================================================
    // 更新光照参数常量缓冲区
    // ========================================================================
    hr = m_context->Map(m_lightBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr))
    {
        LightBuffer* lb = (LightBuffer*)mapped.pData;
        
        // ========================================================================
        // 光源参数
        // ========================================================================
        // 光源方向（归一化的方向向量，指向光源）
        // 这里使用从右上角照射的光源
        lb->lightDirection = XMFLOAT3(-0.5f, -0.5f, -0.5f);
        XMVECTOR lightDir = XMVector3Normalize(XMLoadFloat3(&lb->lightDirection));
        XMStoreFloat3(&lb->lightDirection, lightDir);
        
        // 光源颜色和强度
        lb->lightColor = XMFLOAT3(1.0f, 1.0f, 1.0f);  // 白色光
        lb->lightIntensity = 1.0f;                     // 光源强度
        
        // ========================================================================
        // PBR 材质参数
        // ========================================================================
        // 反照率（基础颜色）- 白色(1,1,1)，确保纹理颜色完全显示
        // 如果需要调整整体色调，可以修改这个值
        lb->albedo = XMFLOAT3(1.0f, 1.0f, 1.0f);
        
        // 金属度（0.0 = 非金属，1.0 = 金属）
        // 设置为 0.0 表示非金属材质（如塑料、陶瓷）
        lb->metallic = 0.0f;
        
        // 粗糙度（0.0 = 完全光滑/镜面，1.0 = 完全粗糙/漫反射）
        // 设置为 0.5 表示中等粗糙度
        lb->roughness = 0.5f;
        
        // ========================================================================
        // 环境光参数
        // ========================================================================
        // 环境光颜色（模拟天空光）
        lb->ambientColor = XMFLOAT3(0.1f, 0.1f, 0.2f);
        
        // ========================================================================
        // 相机位置
        // ========================================================================
        if (m_camera)
        {
            lb->cameraPosition = m_camera->GetPosition();
        }
        else
        {
            lb->cameraPosition = XMFLOAT3(0.0f, 0.0f, -2.0f);
        }
        
        m_context->Unmap(m_lightBuffer.Get(), 0);
    }
}
