#include "Renderer.h"
#include "MeshMgr.h"
#include "Camera.h"
#include "ModelLoader.h"
#include "MeshGPU.h"
#include "Terrain.h"

// 在包含 Windows.h 之前定义 NOMINMAX，避免 min/max 宏冲突
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <wincodec.h>  // WIC (Windows Imaging Component)
#include <DirectXMath.h>
#include <fstream>
#include <vector>
#include <cstring>
#include <memory>
#include <algorithm>  // for std::max, std::transform

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
    // 注意：在HLSL常量缓冲区中，float3会被对齐到16字节（相当于float4）
    // 所以C++结构必须匹配HLSL的对齐方式
    // 使用XMFLOAT4而不是XMFLOAT3，以确保对齐正确
    
    // 注意：在HLSL中，float3会被对齐到16字节（相当于float4）
    // 为了完全匹配，我们在C++和HLSL中都使用float4/XMFLOAT4
    
    XMFLOAT4 lightDirection;  // 光源方向 (16 bytes) - 只使用xyz分量
    float lightIntensity;      // 光源强度 (4 bytes)
    float padding1a;           // 对齐填充 (4 bytes)
    float padding1b;           // 对齐填充 (4 bytes)
    float padding1c;           // 对齐填充 (4 bytes) -> 总共32字节（float需要对齐到16字节边界）
    
    XMFLOAT4 lightColor;       // 光源颜色 (16 bytes) - 只使用xyz分量，float4本身已对齐，无需额外padding
    
    XMFLOAT4 cameraPosition;   // 相机位置 (16 bytes) - 只使用xyz分量，float4本身已对齐，无需额外padding
    
    // PBR 材质参数
    XMFLOAT4 albedo;           // 反照率（基础颜色）(16 bytes) - 只使用xyz分量，float4本身已对齐，无需额外padding
    float metallic;            // 金属度 (4 bytes)
    float padding2d;           // 对齐填充 (4 bytes)
    float padding2e;           // 对齐填充 (4 bytes)
    float padding2f;           // 对齐填充 (4 bytes) -> 总共32字节（float需要对齐到16字节边界）
    
    float roughness;           // 粗糙度 (4 bytes)
    float padding3a;           // 对齐填充 (4 bytes)
    float padding3b;           // 对齐填充 (4 bytes)
    float padding3c;           // 对齐填充 (4 bytes) -> 16 bytes total（float需要对齐到16字节边界）
    
    XMFLOAT4 ambientColor;     // 环境光颜色 (16 bytes) - 只使用xyz分量，float4本身已对齐，无需额外padding
    // 总共: 32 + 16 + 16 + 32 + 16 + 16 = 128 bytes (16字节的倍数)
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
    // 步骤 7: 创建深度模板缓冲区
    // ========================================================================
    if (!CreateDepthStencil())
        return false;

    // ========================================================================
    // 步骤 8: 创建纹理采样器
    // ========================================================================
    if (!CreateSamplerState())
        return false;
    
    // ========================================================================
    // 步骤 9: 创建IBL采样器
    // ========================================================================
    if (!CreateIBLSamplerState())
        return false;
    
    // ========================================================================
    // 步骤 10: 生成BRDF LUT
    // ========================================================================
    if (!GenerateBRDFLUT())
    {
        OutputDebugStringW(L"Warning: Failed to generate BRDF LUT. IBL may not work correctly.\n");
    }
    
    // ========================================================================
    // 步骤 11: 创建天空盒
    // ========================================================================
    if (!CreateSkyboxShaders())
    {
        OutputDebugStringW(L"Warning: Failed to create skybox shaders.\n");
        return false;
    }
    if (!CreateSkyboxGeometry())
    {
        OutputDebugStringW(L"Warning: Failed to create skybox geometry.\n");
        return false;
    }
    if (!CreateSkyboxDepthState())
    {
        OutputDebugStringW(L"Warning: Failed to create skybox depth state.\n");
        return false;
    }
    OutputDebugStringW(L"Skybox resources created successfully.\n");
    
    // ========================================================================
    // 步骤 12: 创建地形shader
    // ========================================================================
    if (!CreateTerrainShaders())
    {
        OutputDebugStringW(L"Warning: Failed to create terrain shaders.\n");
    }
    
    // ========================================================================
    // 步骤 12.5: 创建地形光栅化状态
    // ========================================================================
    if (!CreateTerrainRasterizerStates())
    {
        OutputDebugStringW(L"Warning: Failed to create terrain rasterizer states.\n");
    }
    
    // ========================================================================
    // 步骤 13: 初始化地形
    // ========================================================================
    if (!InitializeTerrain())
    {
        OutputDebugStringW(L"Warning: Failed to initialize terrain.\n");
    }
    
    // ========================================================================
    // 步骤 13: 加载环境贴图
    // ========================================================================
    // 尝试加载 .exr 或 .hdr 环境贴图，如果失败则使用默认环境贴图
    wchar_t exePathEnv[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exePathEnv, MAX_PATH);
    std::wstring exeDir = exePathEnv;
    size_t lastSlash = exeDir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos)
    {
        exeDir = exeDir.substr(0, lastSlash + 1);
        
        std::wstring projectRoot = exeDir;
        for (int i = 0; i < 2; ++i)
        {
            size_t slash = projectRoot.find_last_of(L"\\/", projectRoot.length() - 2);
            if (slash != std::wstring::npos)
                projectRoot = projectRoot.substr(0, slash + 1);
        }
        
        // 尝试加载环境贴图文件（按优先级顺序）
        std::vector<std::wstring> envMapPaths = {
            projectRoot + L"Res/environment.hdr",
            projectRoot + L"environment.hdr"
        };
        
        bool envMapLoaded = false;
        for (const auto& envPath : envMapPaths)
        {
            OutputDebugStringW(L"Trying to load environment map from: ");
            OutputDebugStringW(envPath.c_str());
            OutputDebugStringW(L"\n");
            
            if (LoadEnvironmentMap(envPath))
            {
                envMapLoaded = true;
                OutputDebugStringW(L"Successfully loaded environment map!\n");
                break;
            }
        }
        
        if (!envMapLoaded)
        {
            // 如果所有路径都失败，使用默认环境贴图
            OutputDebugStringW(L"Failed to load environment map from any path, using default (sky blue gradient).\n");
            if (!LoadEnvironmentMap(L""))
            {
                OutputDebugStringW(L"Warning: Failed to load default environment map. IBL may not work correctly.\n");
            }
            else
            {
                OutputDebugStringW(L"Using default environment map (sky blue gradient).\n");
            }
        }
    }
    else
    {
        // 如果无法确定路径，使用默认环境贴图
        if (!LoadEnvironmentMap(L""))
        {
            OutputDebugStringW(L"Warning: Failed to load default environment map. IBL may not work correctly.\n");
        }
    }

    // ========================================================================
    // 步骤 8: 加载纹理（如果存在）
    // ========================================================================
    // 尝试加载纹理，如果失败则创建默认白色纹理
    bool textureLoaded = false;
    if (exePath[0] != 0)
    {
        OutputDebugStringW(L"exePath: ");
        OutputDebugStringW(exePath);
        OutputDebugStringW(L"\n");
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
    std::vector<Submesh> submeshes;
    
    // 尝试加载模型文件（如果存在）
    // 支持从多个路径加载：Res目录、exe目录、项目根目录
    bool modelLoaded = false;
    std::string loadedProjectRoot;
    
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
            
            // 尝试加载模型（使用支持子网格的加载函数）
            for (const auto& path : pathsToTry)
            {
                if (ModelLoader::LoadFromFileWithSubmeshes(path, verts, indices, submeshes))
                {
                    modelLoaded = true;
                    loadedProjectRoot = projectRoot;
                    
                    // 创建Mesh并设置子网格信息
                    auto mesh = m_meshMgr->CreateMesh("Model", verts, indices, submeshes);
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
    else if (!submeshes.empty())
    {
        // 如果模型加载成功且有子网格，加载对应的纹理
        std::vector<std::wstring> materialNames;
        for (const auto& submesh : submeshes)
        {
            materialNames.push_back(std::wstring(submesh.materialName.begin(), submesh.materialName.end()));
        }
        LoadTextures(materialNames, loadedProjectRoot);
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
    m_context->OMSetRenderTargets(1, m_rtv.GetAddressOf(), m_dsv.Get());
    // 参数说明：
    // - 1: 渲染目标数量
    // - m_rtv.GetAddressOf(): 渲染目标视图数组
    // - m_dsv.Get(): 深度模板视图（用于深度测试）

    // ========================================================================
    // 步骤 2: 清空渲染目标和深度缓冲区
    // 用指定颜色填充整个渲染目标，清除上一帧的内容
    // 注意：由于会渲染天空盒，可以清除为黑色（天空盒会覆盖）
    // ========================================================================
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f }; // RGBA: 黑色背景（天空盒会覆盖）
    m_context->ClearRenderTargetView(m_rtv.Get(), clearColor);
    
    // 清空深度缓冲区（设置为1.0，表示最远距离）
    if (m_dsv)
    {
        m_context->ClearDepthStencilView(m_dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    }

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
    // 累积光源旋转时间（如果未暂停）
    if (!m_lightRotationPaused)
    {
        m_lightRotationTime += deltaTime;
    }
    
    UpdateConstantBuffers(deltaTime);

    // ========================================================================
    // 步骤 5: 渲染天空盒（在场景之前渲染）
    // ========================================================================
    RenderSkybox();

    // ========================================================================
    // 步骤 6: 设置深度状态
    // ========================================================================
    // 设置深度状态（天空盒可能改变了深度状态）
    m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);
    
    // ========================================================================
    // 步骤 6.5: 渲染地形（在场景之前渲染，在天空盒之后）
    // 地形使用专用的shader，会在RenderTerrain内部设置
    // ========================================================================
    RenderTerrain();
    
    // ========================================================================
    // 步骤 7: 设置模型的 Shader 和输入布局
    // 地形渲染后，需要设置模型的shader
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
    
    // 地形渲染后，需要重新更新常量缓冲区（因为地形修改了world矩阵）
    // 确保模型的world矩阵是正确的
    UpdateConstantBuffers(deltaTime);

    // ========================================================================
    // 步骤 8: 渲染网格
    // 绑定网格资源并执行绘制命令
    // ========================================================================
    
    // 调试：检查模型是否遮挡了地形
    // 模型在Y=5，地形在Y=[0,30]，如果相机从上方看，模型可能会遮挡地形
    // 但模型很小（0.2倍缩放），应该不会完全遮挡地形
    
    // 尝试渲染模型，如果不存在则渲染三角形
    std::shared_ptr<MeshGPU> modelGPU = m_meshMgr->GetMeshGPU("Model");
    if (!modelGPU)
    {
        OutputDebugStringW(L"Model not found, trying Triangle...\n");
        modelGPU = m_meshMgr->GetMeshGPU("Triangle");
    }
    else
    {
        static bool modelFoundLogged = false;
        if (!modelFoundLogged)
        {
            OutputDebugStringW(L"Model found!\n");
            modelFoundLogged = true;
        }
    }
    
    if (modelGPU)
    {
        // 调试输出
        static bool modelRenderLogged = false;
        if (!modelRenderLogged)
        {
            wchar_t msg[256];
            swprintf_s(msg, L"Rendering model: %d submeshes\n", modelGPU->GetSubmeshCount());
            OutputDebugStringW(msg);
            modelRenderLogged = true;
        }
        
        // 绑定顶点缓冲区和索引缓冲区到输入装配阶段
        modelGPU->Bind(m_context.Get());
        
        // 检查是否有子网格（多材质支持）
        if (modelGPU->GetSubmeshCount() > 0)
        {
            // 按子网格绘制，每个子网格使用不同的纹理
            for (uint32_t i = 0; i < modelGPU->GetSubmeshCount(); ++i)
            {
                const Submesh& submesh = modelGPU->GetSubmesh(i);
                
                // 查找对应的材质纹理
                ID3D11ShaderResourceView* srvs[3] = {
                    m_textureSRV.Get(),  // BaseColor（默认纹理）
                    m_textureSRV.Get(),  // Normal（默认纹理）
                    m_textureSRV.Get()   // MRA（默认纹理）
                };
                
                if (!m_materialTextures.empty())
                {
                    auto it = m_materialTextures.find(submesh.materialName);
                    if (it != m_materialTextures.end())
                    {
                        const MaterialTextures& matTex = it->second;
                        
                        // 绑定多个纹理到像素着色器
                        // t0 = BaseColor, t1 = Normal, t2 = MRA
                        srvs[0] = matTex.baseColorSRV.Get();
                        srvs[1] = matTex.normalSRV.Get();
                        srvs[2] = matTex.mraSRV.Get();
                        
                        // 确保所有纹理都存在，如果不存在则使用默认纹理
                        if (!srvs[0]) srvs[0] = m_textureSRV.Get();
                        if (!srvs[1]) srvs[1] = m_textureSRV.Get();
                        if (!srvs[2]) srvs[2] = m_textureSRV.Get();
                    }
                }
                
                m_context->PSSetShaderResources(0, 3, srvs);
                
                // 绑定IBL纹理（t3 = 环境贴图, t4 = BRDF LUT）
                ID3D11ShaderResourceView* iblSRVs[2] = {
                    m_environmentMapSRV.Get(),
                    m_brdfLutSRV.Get()
                };
                m_context->PSSetShaderResources(3, 2, iblSRVs);
                
                // 绑定采样器（s0用于普通纹理，s1用于IBL纹理）
                if (m_samplerState)
                {
                    m_context->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());
                }
                if (m_iblSamplerState)
                {
                    m_context->PSSetSamplers(1, 1, m_iblSamplerState.GetAddressOf());
                }
                
                // 绘制该子网格
                static bool drawSubmeshLogged = false;
                if (!drawSubmeshLogged && i == 0)
                {
                    wchar_t msg[256];
                    swprintf_s(msg, L"Drawing submesh %d\n", i);
                    OutputDebugStringW(msg);
                    drawSubmeshLogged = true;
                }
                modelGPU->DrawSubmesh(m_context.Get(), i);
            }
        }
        else
        {
            // 没有子网格，使用传统方式绘制（向后兼容）
            // 绑定纹理（如果有）
            ID3D11ShaderResourceView* srvs[3] = {
                m_textureSRV.Get(),
                m_textureSRV.Get(),
                m_textureSRV.Get()
            };
            
            if (m_textureSRV)
            {
                srvs[0] = m_textureSRV.Get();
                srvs[1] = m_textureSRV.Get();
                srvs[2] = m_textureSRV.Get();
            }
            
            m_context->PSSetShaderResources(0, 3, srvs);
            
            // 绑定IBL纹理（t3 = 环境贴图, t4 = BRDF LUT）
            ID3D11ShaderResourceView* iblSRVs[2] = {
                m_environmentMapSRV.Get(),
                m_brdfLutSRV.Get()
            };
            m_context->PSSetShaderResources(3, 2, iblSRVs);
            
            // 绑定采样器（s0用于普通纹理，s1用于IBL纹理）
            if (m_samplerState)
            {
                m_context->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());
            }
            if (m_iblSamplerState)
            {
                m_context->PSSetSamplers(1, 1, m_iblSamplerState.GetAddressOf());
            }
            
            // 执行绘制命令，GPU 开始渲染
            modelGPU->Draw(m_context.Get());
        }
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
    // 释放地形
    if (m_terrain)
    {
        delete m_terrain;
        m_terrain = nullptr;
    }
    
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
    m_depthStencilState.Reset();  // 深度模板状态
    m_dsv.Reset();           // 深度模板视图
    m_rtv.Reset();           // 渲染目标视图
    m_swapChain.Reset();     // 交换链
    m_context.Reset();       // 设备上下文
    m_device.Reset();        // 设备
}

// ============================================================================
// 创建深度模板缓冲区和状态
// ============================================================================
bool Renderer::CreateDepthStencil()
{
    // 创建深度模板纹理
    D3D11_TEXTURE2D_DESC depthStencilDesc = {};
    depthStencilDesc.Width = m_width;
    depthStencilDesc.Height = m_height;
    depthStencilDesc.MipLevels = 1;
    depthStencilDesc.ArraySize = 1;
    depthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;  // 24位深度 + 8位模板
    depthStencilDesc.SampleDesc.Count = 1;
    depthStencilDesc.SampleDesc.Quality = 0;
    depthStencilDesc.Usage = D3D11_USAGE_DEFAULT;
    depthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    depthStencilDesc.CPUAccessFlags = 0;
    depthStencilDesc.MiscFlags = 0;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> depthStencilTexture;
    HRESULT hr = m_device->CreateTexture2D(&depthStencilDesc, nullptr, depthStencilTexture.GetAddressOf());
    if (FAILED(hr))
        return false;

    // 创建深度模板视图
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = depthStencilDesc.Format;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;

    hr = m_device->CreateDepthStencilView(depthStencilTexture.Get(), &dsvDesc, m_dsv.GetAddressOf());
    if (FAILED(hr))
        return false;

    // 创建深度模板状态（启用深度测试）
    D3D11_DEPTH_STENCIL_DESC depthStencilStateDesc = {};
    depthStencilStateDesc.DepthEnable = TRUE;                    // 启用深度测试
    depthStencilStateDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;  // 允许写入深度值
    depthStencilStateDesc.DepthFunc = D3D11_COMPARISON_LESS;     // 小于时通过（近的物体遮挡远的）
    depthStencilStateDesc.StencilEnable = FALSE;                 // 不使用模板测试
    depthStencilStateDesc.StencilReadMask = 0xFF;
    depthStencilStateDesc.StencilWriteMask = 0xFF;

    hr = m_device->CreateDepthStencilState(&depthStencilStateDesc, m_depthStencilState.GetAddressOf());
    if (FAILED(hr))
        return false;

    // 启用深度模板状态
    m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);

    return true;
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
// 支持 PNG、JPG、BMP、TGA 等格式
// ============================================================================
bool Renderer::LoadTexture(const std::wstring& filename)
{
    if (filename.empty())
    {
        m_lastError = L"LoadTexture: filename is empty";
        return false;
    }

    // 检查文件是否存在
    HANDLE hFile = CreateFileW(
        filename.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE)
    {
        m_lastError = L"LoadTexture: File not found: " + filename;
        return false;
    }
    CloseHandle(hFile);

    // 创建 WIC 工厂
    Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(wicFactory.GetAddressOf())
    );
    if (FAILED(hr))
    {
        m_lastError = L"LoadTexture: Failed to create WIC factory. HRESULT: " + std::to_wstring(hr);
        return false;
    }

    // 创建解码器
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    hr = wicFactory->CreateDecoderFromFilename(
        filename.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,
        decoder.GetAddressOf()
    );
    if (FAILED(hr))
    {
        m_lastError = L"LoadTexture: Failed to create decoder. HRESULT: " + std::to_wstring(hr);
        return false;
    }

    // 获取第一帧（大多数图片只有一帧）
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(hr))
    {
        m_lastError = L"LoadTexture: Failed to get frame. HRESULT: " + std::to_wstring(hr);
        return false;
    }

    // 获取图片尺寸
    UINT width = 0, height = 0;
    hr = frame->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0)
    {
        m_lastError = L"LoadTexture: Invalid image dimensions";
        return false;
    }

    // 创建格式转换器
    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    hr = wicFactory->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr))
    {
        m_lastError = L"LoadTexture: Failed to create format converter. HRESULT: " + std::to_wstring(hr);
        return false;
    }

    // 转换为 RGBA 格式（32位，每通道8位）
    hr = converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0f,
        WICBitmapPaletteTypeCustom
    );
    if (FAILED(hr))
    {
        m_lastError = L"LoadTexture: Failed to initialize format converter. HRESULT: " + std::to_wstring(hr);
        return false;
    }

    // 计算行字节数（RGBA = 4字节，对齐到4字节边界）
    UINT stride = (width * 4 + 3) & ~3;  // 对齐到4字节
    UINT imageSize = stride * height;

    // 分配内存存储像素数据
    std::vector<BYTE> pixels(imageSize);
    hr = converter->CopyPixels(nullptr, stride, imageSize, pixels.data());
    if (FAILED(hr))
    {
        m_lastError = L"LoadTexture: Failed to copy pixels. HRESULT: " + std::to_wstring(hr);
        return false;
    }

    // 创建 D3D11 纹理
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
    initData.pSysMem = pixels.data();
    initData.SysMemPitch = stride;
    initData.SysMemSlicePitch = 0;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    hr = m_device->CreateTexture2D(&texDesc, &initData, texture.GetAddressOf());
    if (FAILED(hr))
    {
        m_lastError = L"LoadTexture: Failed to create texture. HRESULT: " + std::to_wstring(hr);
        return false;
    }

    // 创建着色器资源视图
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;

    hr = m_device->CreateShaderResourceView(texture.Get(), &srvDesc, m_textureSRV.GetAddressOf());
    if (FAILED(hr))
    {
        m_lastError = L"LoadTexture: Failed to create shader resource view. HRESULT: " + std::to_wstring(hr);
        return false;
    }

    // 成功加载纹理
    return true;
}

// ============================================================================
// 加载纹理文件（内部辅助函数，返回SRV）
// ============================================================================
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> Renderer::LoadTextureFile(const std::wstring& filename, bool isBaseColor)
{
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> result;
    
    if (filename.empty())
        return result;
    
    // 检查文件是否存在
    HANDLE hFile = CreateFileW(
        filename.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE)
        return result;
    CloseHandle(hFile);
    
    // 创建 WIC 工厂
    Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(wicFactory.GetAddressOf())
    );
    if (FAILED(hr))
        return result;
    
    // 创建解码器
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    hr = wicFactory->CreateDecoderFromFilename(
        filename.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,
        decoder.GetAddressOf()
    );
    if (FAILED(hr))
        return result;
    
    // 获取第一帧
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(hr))
        return result;
    
    // 获取图片尺寸
    UINT width = 0, height = 0;
    hr = frame->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0)
        return result;
    
    // 创建格式转换器
    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    hr = wicFactory->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr))
        return result;
    
    // 转换为 RGBA 格式
    hr = converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0f,
        WICBitmapPaletteTypeCustom
    );
    if (FAILED(hr))
        return result;
    
    // 计算行字节数
    UINT stride = (width * 4 + 3) & ~3;
    UINT imageSize = stride * height;
    
    // 分配内存
    std::vector<BYTE> pixels(imageSize);
    hr = converter->CopyPixels(nullptr, stride, imageSize, pixels.data());
    if (FAILED(hr))
        return result;
    
    // 创建 D3D11 纹理
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
    initData.pSysMem = pixels.data();
    initData.SysMemPitch = stride;
    initData.SysMemSlicePitch = 0;
    
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    hr = m_device->CreateTexture2D(&texDesc, &initData, texture.GetAddressOf());
    if (FAILED(hr))
        return result;
    
    // 创建着色器资源视图
    // BaseColor贴图使用SRGB格式，让DX11自动进行sRGB到线性的转换
    // Normal和MRA贴图必须使用UNORM格式（线性空间）
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    
    if (isBaseColor)
    {
        // 注意：DirectX 11 不允许为 UNORM 格式的纹理创建 SRGB 格式的 SRV
        // 因为纹理本身是用 UNORM 格式创建的，SRV 格式必须与纹理格式兼容
        // 如果要用 SRGB，应该在创建纹理时就使用 SRGB 格式
        // 这里直接使用 UNORM 格式，PBR shader 可以在 shader 中手动进行 gamma 校正
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        hr = m_device->CreateShaderResourceView(texture.Get(), &srvDesc, result.GetAddressOf());
    }
    else
    {
        // Normal和MRA贴图使用UNORM格式
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        hr = m_device->CreateShaderResourceView(texture.Get(), &srvDesc, result.GetAddressOf());
    }
    
    if (FAILED(hr))
    {
        std::wstring errorMsg = L"Error: Failed to create SRV. HRESULT: 0x";
        wchar_t hrStr[16];
        swprintf_s(hrStr, L"%08X", hr);
        errorMsg += hrStr;
        errorMsg += L"\n";
        OutputDebugStringW(errorMsg.c_str());
        result.Reset();
    }
    
    return result;
}

// ============================================================================
// 加载多个纹理（根据材质名称列表）
// ============================================================================
bool Renderer::LoadTextures(const std::vector<std::wstring>& materialNames, const std::string& projectRoot)
{
    m_materialTextures.clear();
    
    std::wstring resPath = std::wstring(projectRoot.begin(), projectRoot.end()) + L"Res/";
    
    // 为每个材质加载所有纹理
    for (const auto& materialName : materialNames)
    {
        MaterialTextures matTex;
        
        // 提取纹理名称（从MI_Manny_01转换为Manny_01，用于T_Manny_01格式）
        std::wstring textureName = materialName;
        if (textureName.length() > 3 && textureName.substr(0, 3) == L"MI_")
        {
            textureName = textureName.substr(3);  // 移除"MI_"前缀
        }
        
        // ========================================================================
        // 1. 加载BaseColor纹理
        // ========================================================================
        // 尝试多种可能的文件名格式
        std::vector<std::wstring> baseColorPaths = {
            resPath + materialName + L"_BaseColor.png",     // MI_Manny_01_New_BaseColor_0.png
        };
        
        bool baseColorLoaded = false;
        for (const auto& path : baseColorPaths)
        {
            matTex.baseColorSRV = LoadTextureFile(path, true);  // BaseColor使用SRGB格式
            if (matTex.baseColorSRV)
            {
                OutputDebugStringW((L"Loaded BaseColor: " + path + L"\n").c_str());
                baseColorLoaded = true;
                break;
            }
        }
        
        if (!baseColorLoaded)
        {
            std::wstring debugMsg = L"Debug: Tried to load BaseColor for material: ";
            debugMsg += materialName;
            debugMsg += L" (textureName: ";
            debugMsg += textureName;
            debugMsg += L"), tried paths:\n";
            for (const auto& path : baseColorPaths)
            {
                debugMsg += L"  - ";
                debugMsg += path;
                debugMsg += L"\n";
            }
            OutputDebugStringW(debugMsg.c_str());
        }
        
        // 如果BaseColor加载失败，使用默认纹理（SRGB格式）
        if (!matTex.baseColorSRV)
        {
            std::wstring warnMsg = L"Warning: Failed to load BaseColor texture for material: ";
            warnMsg += materialName;
            warnMsg += L", using default white texture (SRGB)\n";
            OutputDebugStringW(warnMsg.c_str());
            
            // 创建SRGB格式的默认BaseColor纹理
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
            initData.SysMemPitch = width * 4;
            initData.SysMemSlicePitch = 0;
            
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            HRESULT hr = m_device->CreateTexture2D(&texDesc, &initData, texture.GetAddressOf());
            if (SUCCEEDED(hr))
            {
                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels = 1;
                srvDesc.Texture2D.MostDetailedMip = 0;
                
                // 注意：DirectX 11 不允许为 UNORM 格式的纹理创建 SRGB 格式的 SRV
                // 直接使用 UNORM 格式，PBR shader 可以在 shader 中手动进行 gamma 校正
                srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                hr = m_device->CreateShaderResourceView(texture.Get(), &srvDesc, matTex.baseColorSRV.GetAddressOf());
            }
        }
        
        // ========================================================================
        // 2. 加载法线贴图（BN = BaseNormal）
        // ========================================================================
        std::vector<std::wstring> normalPaths = {
            resPath + L"T_" + textureName + L"_BN.png",      // T_Manny_01_BN.png
        };
        
        for (const auto& path : normalPaths)
        {
            matTex.normalSRV = LoadTextureFile(path);
            if (matTex.normalSRV)
            {
                OutputDebugStringW((L"Loaded Normal: " + path + L"\n").c_str());
                break;
            }
        }
        
        // 如果法线贴图加载失败，使用默认蓝色纹理（表示无法线偏移）
        if (!matTex.normalSRV)
        {
            if (CreateDefaultTexture())
                matTex.normalSRV = m_textureSRV;
        }
        
        // ========================================================================
        // 3. 加载MRA贴图（Metallic-Roughness-AO）
        // ========================================================================
        std::vector<std::wstring> mraPaths = {
            resPath + L"T_" + textureName + L"_MRA.png",     // T_Manny_01_MRA.png
        };
        
        for (const auto& path : mraPaths)
        {
            matTex.mraSRV = LoadTextureFile(path);
            if (matTex.mraSRV)
            {
                OutputDebugStringW((L"Loaded MRA: " + path + L"\n").c_str());
                break;
            }
        }
        
        // 如果MRA贴图加载失败，创建默认MRA纹理（金属度=0, 粗糙度=0.5, AO=1.0）
        // 而不是使用白色纹理，这样可以与真实MRA贴图区分
        if (!matTex.mraSRV)
        {
            std::wstring warnMsg = L"Warning: Failed to load MRA texture for material: ";
            warnMsg += materialName;
            warnMsg += L", using default MRA values\n";
            OutputDebugStringW(warnMsg.c_str());
            
            // 创建默认MRA纹理：R=0(非金属), G=0.5(中等粗糙度), B=1.0(无AO)
            // 这样在着色器中可以判断：如果金属度=0且粗糙度=0.5且AO=1.0，可能是默认值
            const UINT width = 1;
            const UINT height = 1;
            UINT8 pixels[4] = { 0, 128, 255, 255 };  // RGBA: R=0(金属度), G=128(粗糙度0.5), B=255(AO=1.0)
            
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
            initData.SysMemPitch = width * 4;
            initData.SysMemSlicePitch = 0;
            
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            HRESULT hr = m_device->CreateTexture2D(&texDesc, &initData, texture.GetAddressOf());
            if (SUCCEEDED(hr))
            {
                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format = texDesc.Format;
                srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels = 1;
                srvDesc.Texture2D.MostDetailedMip = 0;
                
                hr = m_device->CreateShaderResourceView(texture.Get(), &srvDesc, matTex.mraSRV.GetAddressOf());
            }
        }
        
        // 存储材质纹理
        std::string materialNameA(materialName.begin(), materialName.end());
        m_materialTextures[materialNameA] = matTex;
    }
    
    return !m_materialTextures.empty();
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
void Renderer::UpdateConstantBuffers(float deltaTime)
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
        // 旋转模型摆正：从头顶看向脚底 -> 正常视角
        // 绕X轴旋转-90度（顺时针90度），让模型从躺着的状态变成站着的状态
        XMMATRIX rotation = XMMatrixRotationX(-XM_PI / 2.0f);  // -90度 = -π/2弧度
        // 将模型向上移动，确保在地形上方（地形高度范围大约是0-30，模型中心在y=0，所以需要向上移动）
        XMMATRIX translation = XMMatrixTranslation(0.0f, 5.0f, 0.0f);  // 向上移动5个单位
        // 组合变换：先缩放，再旋转，最后平移
        XMMATRIX world = scale * rotation * translation;
        
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
        // 让光源绕着角色缓慢旋转（在XZ平面上绕Y轴旋转）
        // 光源高度保持一定（从上方照射），旋转速度：每15秒转一圈
        float rotationSpeed = 2.0f * XM_PI / 15.0f;  // 每15秒转一圈（2π弧度），缓慢旋转
        float angle = m_lightRotationTime * rotationSpeed;
        
        // 计算光源位置（在XZ平面上的圆形轨道，Y轴保持一定高度）
        // 光源距离角色的距离（在XZ平面上的半径）
        float lightRadius = 1.0f;
        // 光源高度（从上方照射，负值表示在Y轴上方）
        float lightHeight = -0.5f;  // 从上方约30度角照射（更接近垂直，光照更强）
        
        // 计算光源在世界空间的位置（XZ平面上的圆形轨道）
        float lightX = cosf(angle) * lightRadius;
        float lightZ = sinf(angle) * lightRadius;
        float lightY = lightHeight;
        
        // 光源方向（从角色位置(0,0,0)指向光源位置，然后归一化）
        // 注意：lightDirection在shader中会被取反，所以这里存储的是从表面指向光源的方向
        XMVECTOR lightPos = XMVectorSet(lightX, lightY, lightZ, 0.0f);
        XMVECTOR lightDir = XMVector3Normalize(lightPos);  // 归一化
        
        // 转换为XMFLOAT4并存储（匹配HLSL的float3对齐）
        XMFLOAT4 lightDirFloat4;
        XMStoreFloat4(&lightDirFloat4, XMVectorSetW(lightDir, 0.0f));
        lb->lightDirection = lightDirFloat4;
        
        // 光源颜色和强度
        lb->lightColor = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.0f);  // 白色光
        lb->lightIntensity = 2.0f;                     // 光源强度（提高亮度）
        
        // ========================================================================
        // PBR 材质参数
        // ========================================================================
        // 反照率（基础颜色）- 白色(1,1,1)，确保纹理颜色完全显示
        // 如果需要调整整体色调，可以修改这个值
        lb->albedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.0f);
        
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
        lb->ambientColor = XMFLOAT4(0.1f, 0.1f, 0.15f, 0.0f);
        
        // 确保padding值被初始化（避免未定义值）
        lb->padding3a = 0.0f;
        lb->padding3b = 0.0f;
        lb->padding3c = 0.0f;
        
        // ========================================================================
        // 相机位置
        // ========================================================================
        if (m_camera)
        {
            XMFLOAT3 camPos = m_camera->GetPosition();
            lb->cameraPosition = XMFLOAT4(camPos.x, camPos.y, camPos.z, 0.0f);
        }
        else
        {
            lb->cameraPosition = XMFLOAT4(0.0f, 0.0f, -2.0f, 0.0f);
        }
        
        m_context->Unmap(m_lightBuffer.Get(), 0);
    }
}

// ============================================================================
// 创建IBL采样器状态（支持mipmap和clamp）
// ============================================================================
bool Renderer::CreateIBLSamplerState()
{
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;  // 线性过滤，支持mipmap
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;     // Clamp模式（环境贴图）
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MipLODBias = 0.0f;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.BorderColor[0] = 0.0f;
    samplerDesc.BorderColor[1] = 0.0f;
    samplerDesc.BorderColor[2] = 0.0f;
    samplerDesc.BorderColor[3] = 0.0f;
    samplerDesc.MinLOD = 0.0f;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;  // 支持所有mip级别

    HRESULT hr = m_device->CreateSamplerState(&samplerDesc, m_iblSamplerState.GetAddressOf());
    return SUCCEEDED(hr);
}

// ============================================================================
// 生成BRDF查找表（LUT）
// BRDF LUT用于镜面反射IBL的Split-Sum Approximation
// ============================================================================
bool Renderer::GenerateBRDFLUT()
{
    const UINT lutSize = 512;  // LUT分辨率（512x512）
    
    // 分配内存存储LUT数据（RG16F格式：2个float16通道）
    std::vector<float> lutData(lutSize * lutSize * 2);  // R和G通道
    
    // 生成BRDF LUT
    // 基于UE5的实现：IntegrateBRDF(NdotV, roughness)
    for (UINT y = 0; y < lutSize; ++y)
    {
        for (UINT x = 0; x < lutSize; ++x)
        {
            float NdotV = (x + 0.5f) / lutSize;  // [0, 1]
            float roughness = (y + 0.5f) / lutSize;  // [0, 1]
            
            // 避免除零（使用括号避免Windows宏冲突）
            NdotV = (std::max)(NdotV, 0.0001f);
            roughness = (std::max)(roughness, 0.0001f);
            
            XMVECTOR V = XMVectorSet(sqrtf(1.0f - NdotV * NdotV), 0.0f, NdotV, 0.0f);
            XMVECTOR N = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
            
            float A = 0.0f;
            float B = 0.0f;
            
            // 数值积分（Monte Carlo方法）
            const UINT numSamples = 1024;
            for (UINT i = 0; i < numSamples; ++i)
            {
                // 生成随机方向（Hammersley序列）
                float Xi_x = (float)i / (float)numSamples;
                float Xi_y = float(i % 2) * 0.5f + float((i / 2) % 2) * 0.25f + float((i / 4) % 2) * 0.125f;
                
                // 重要性采样（GGX分布）
                float a = roughness * roughness;
                float a2 = a * a;
                float phi = 2.0f * XM_PI * Xi_x;
                float cosTheta = sqrtf((1.0f - Xi_y) / (1.0f + (a2 - 1.0f) * Xi_y));
                float sinTheta = sqrtf(1.0f - cosTheta * cosTheta);
                
                XMVECTOR H = XMVectorSet(cosf(phi) * sinTheta, sinf(phi) * sinTheta, cosTheta, 0.0f);
                XMVECTOR V_dot_H = XMVector3Dot(V, H);
                float vDotH = XMVectorGetX(V_dot_H);
                XMVECTOR L = XMVectorSubtract(XMVectorScale(H, 2.0f * vDotH), V);
                L = XMVector3Normalize(L);
                
                float NdotL = (std::max)(XMVectorGetZ(L), 0.0f);
                float NdotH = (std::max)(XMVectorGetZ(H), 0.0f);
                float VdotH = (std::max)(vDotH, 0.0f);
                
                if (NdotL > 0.0f)
                {
                    // 简化的几何函数计算
                    float NdotV_val = NdotV;
                    float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
                    float G1_V = NdotV_val / (NdotV_val * (1.0f - k) + k);
                    float G1_L = NdotL / (NdotL * (1.0f - k) + k);
                    float G = G1_V * G1_L;
                    
                    float G_Vis = (G * VdotH) / (NdotH * NdotV + 0.0001f);
                    float Fc = powf(1.0f - VdotH, 5.0f);
                    
                    A += (1.0f - Fc) * G_Vis;
                    B += Fc * G_Vis;
                }
            }
            
            A /= float(numSamples);
            B /= float(numSamples);
            
            // 存储到LUT（R通道 = A, G通道 = B）
            UINT index = (y * lutSize + x) * 2;
            lutData[index] = A;
            lutData[index + 1] = B;
        }
    }
    
    // 创建纹理（使用R32G32_FLOAT格式，因为数据是float32）
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = lutSize;
    texDesc.Height = lutSize;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R32G32_FLOAT;  // 2个float32通道（匹配数据格式）
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = 0;
    texDesc.MiscFlags = 0;
    
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = lutData.data();
    initData.SysMemPitch = lutSize * 2 * sizeof(float);  // 每行2个float32，共8字节
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
    
    hr = m_device->CreateShaderResourceView(texture.Get(), &srvDesc, m_brdfLutSRV.GetAddressOf());
    return SUCCEEDED(hr);
}

// stb_image 用于加载 HDR/EXR 格式
// 注意：必须在所有系统头文件之后包含，避免宏定义冲突
// 下载地址：https://github.com/nothings/stb/blob/master/stb_image.h
// 保存可能被破坏的宏定义
#pragma push_macro("setjmp")
#pragma push_macro("longjmp")
#pragma push_macro("jmp_buf")

#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8   // 可选（Windows 路径）
#include "stb_image.h"

// 恢复宏定义
#pragma pop_macro("jmp_buf")
#pragma pop_macro("longjmp")
#pragma pop_macro("setjmp")

// ============================================================================
// 加载环境贴图（HDR环境贴图，转换为立方体贴图）
// 注意：简化实现，这里先创建一个简单的默认环境贴图
// 完整实现需要支持HDR文件加载和立方体贴图生成
// ============================================================================
bool Renderer::LoadEnvironmentMap(const std::wstring& filename)
{
    // 如果提供了文件名，尝试加载 .exr 或 .hdr 文件
    if (!filename.empty())
    {
        // 检查文件扩展名
        std::wstring ext = filename;
        size_t dotPos = ext.find_last_of(L".");
        if (dotPos != std::wstring::npos)
        {
            ext = ext.substr(dotPos + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
            
            // 注意：stb_image 只支持 HDR 格式，不支持 EXR 格式
            // 如果文件是 EXR 格式，需要转换为 HDR 或使用支持 EXR 的库（如 tinyexr）
            if (ext == L"exr")
            {
                OutputDebugStringW(L"Warning: EXR format is not supported by stb_image. Skipping: ");
                OutputDebugStringW(filename.c_str());
                OutputDebugStringW(L"\n");
                OutputDebugStringW(L"Please convert EXR to HDR format or use a library that supports EXR (such as tinyexr).\n");
                return false;  // 跳过这个文件，尝试下一个
            }
            
            if (ext == L"hdr")
            {
                // 转换宽字符串为多字节字符串
                int size_needed = WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), -1, nullptr, 0, nullptr, nullptr);
                std::string filenameA(size_needed, 0);
                WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), -1, &filenameA[0], size_needed, nullptr, nullptr);
                
                // 使用 stb_image 加载 HDR 文件
                // 注意：stb_image 只支持 HDR 格式，不支持 EXR 格式
                // 如果文件是 EXR 格式，stbi_loadf 会返回 null，需要使用其他库（如 tinyexr）
                int width, height, channels;
                float* hdrData = stbi_loadf(filenameA.c_str(), &width, &height, &channels, 4);  // 强制4通道（RGBA）
                
                if (hdrData && width > 0 && height > 0)
                {
                    // 调试：输出HDR文件信息
                    char debugMsg[256];
                    sprintf_s(debugMsg, "HDR file loaded: width=%d, height=%d, channels=%d\n", width, height, channels);
                    OutputDebugStringA(debugMsg);
                    
                    // 调试：检查前几个像素的值
                    float sampleR = hdrData[0];
                    float sampleG = hdrData[1];
                    float sampleB = hdrData[2];
                    sprintf_s(debugMsg, "First pixel (top-left): R=%.3f, G=%.3f, B=%.3f\n", sampleR, sampleG, sampleB);
                    OutputDebugStringA(debugMsg);
                    
                    // 将 HDR 图像转换为立方体贴图
                    // 假设输入是等距柱状投影（Equirectangular）格式
                    const UINT cubeSize = 1024;  // 立方体贴图每面大小（提高分辨率以减少接缝）
                    
                    // 创建立方体贴图
                    D3D11_TEXTURE2D_DESC texDesc = {};
                    texDesc.Width = cubeSize;
                    texDesc.Height = cubeSize;
                    texDesc.MipLevels = 1;
                    texDesc.ArraySize = 6;  // 立方体贴图有6个面
                    texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;  // HDR格式
                    texDesc.SampleDesc.Count = 1;
                    texDesc.SampleDesc.Quality = 0;
                    texDesc.Usage = D3D11_USAGE_DEFAULT;
                    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                    texDesc.CPUAccessFlags = 0;
                    texDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;  // 标记为立方体贴图
                    
                    // 为每个面生成数据（从等距柱状投影转换为立方体贴图）
                    std::vector<D3D11_SUBRESOURCE_DATA> subresourceData(6);
                    std::vector<std::vector<float>> faceData(6);
                    
                    // 立方体贴图的6个面方向（标准DirectX立方体贴图顺序：+X, -X, +Y, -Y, +Z, -Z）
                    // 每个面的定义：[forward方向, up方向]
                    // 注意：right = up × forward（叉积），方向向量dir = forward + u*right + v*up
                    XMVECTOR faceDirections[6][2] = {
                        { XMVectorSet(-1.0f, 0.0f, 0.0f, 0.0f), XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f) },  // +X: forward=(-1,0,0), up=(0,-1,0), right=(0,0,-1) - 翻转forward
                        { XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f) }, // -X: forward=(1,0,0), up=(0,-1,0), right=(0,0,1) - 翻转forward
                        { XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f) }, // +Y: forward=(0,1,0), up=(0,0,1), right=(-1,0,0)
                        { XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f), XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f) }, // -Y: forward=(0,-1,0), up=(0,0,-1), right=(1,0,0)
                        { XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f) },  // +Z: forward=(0,0,1), up=(0,-1,0), right=(1,0,0) - 翻转up
                        { XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f), XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f) }  // -Z: forward=(0,0,-1), up=(0,-1,0), right=(-1,0,0) - 翻转up
                    };
                    
                    for (UINT face = 0; face < 6; ++face)
                    {
                        faceData[face].resize(cubeSize * cubeSize * 4);  // RGBA，每个float
                        
                        XMVECTOR forward = faceDirections[face][0];  // 面的法线方向（向前）
                        XMVECTOR up = faceDirections[face][1];       // 面的上方向
                        XMVECTOR right = XMVector3Cross(up, forward);  // right = up × forward（右手坐标系）
                        
                        for (UINT y = 0; y < cubeSize; ++y)
                        {
                            for (UINT x = 0; x < cubeSize; ++x)
                            {
                                // 将立方体贴图坐标转换为方向向量
                                float u = (float(x) + 0.5f) / float(cubeSize) * 2.0f - 1.0f;
                                float v = (float(y) + 0.5f) / float(cubeSize) * 2.0f - 1.0f;
                                
                                // 对于上下两个面（+Y和-Y），可能需要翻转UV以确保正确的方向
                                // 但这取决于具体的立方体贴图标准，先不翻转试试
                                
                                XMVECTOR dir = XMVector3Normalize(
                                    XMVectorAdd(
                                        XMVectorAdd(XMVectorScale(right, u), XMVectorScale(up, v)),
                                        forward
                                    )
                                );
                                
                                // 将方向向量转换为等距柱状投影坐标
                                // 等距柱状投影：phi[0,2π]对应X[0,width], theta[0,π]对应Y[0,height]
                                // theta=0是顶部（+Y），theta=π是底部（-Y）
                                float dirX = XMVectorGetX(dir);
                                float dirY = XMVectorGetY(dir);
                                float dirZ = XMVectorGetZ(dir);
                                
                                float phi = atan2f(dirX, dirZ) + XM_PI;  // [0, 2π]
                                float theta = acosf(dirY);  // [0, π]，y=1时theta=0（顶部），y=-1时theta=π（底部）
                                
                                // 采样 HDR 图像（使用双线性过滤以减少接缝）
                                float hdrU = phi / (2.0f * XM_PI) * width;
                                float hdrV = theta / XM_PI * height;
                                
                                // 双线性过滤
                                int hdrX0 = (int)floorf(hdrU);
                                int hdrY0 = (int)floorf(hdrV);
                                int hdrX1 = hdrX0 + 1;
                                int hdrY1 = hdrY0 + 1;
                                
                                float fx = hdrU - hdrX0;
                                float fy = hdrV - hdrY0;
                                
                                // 处理边界（X方向需要包裹，因为等距柱状投影在phi=0和2π处是连续的）
                                // Y方向需要限制，因为theta在0和π处有边界
                                hdrX0 = hdrX0 % width;
                                if (hdrX0 < 0) hdrX0 += width;
                                hdrX1 = hdrX1 % width;
                                if (hdrX1 < 0) hdrX1 += width;
                                
                                hdrY0 = (hdrY0 < 0) ? 0 : ((hdrY0 >= height) ? height - 1 : hdrY0);
                                hdrY1 = (hdrY1 < 0) ? 0 : ((hdrY1 >= height) ? height - 1 : hdrY1);
                                
                                // 采样4个像素
                                int idx00 = (hdrY0 * width + hdrX0) * 4;
                                int idx10 = (hdrY0 * width + hdrX1) * 4;
                                int idx01 = (hdrY1 * width + hdrX0) * 4;
                                int idx11 = (hdrY1 * width + hdrX1) * 4;
                                
                                // 双线性插值
                                float r = (1.0f - fx) * (1.0f - fy) * hdrData[idx00] +
                                          fx * (1.0f - fy) * hdrData[idx10] +
                                          (1.0f - fx) * fy * hdrData[idx01] +
                                          fx * fy * hdrData[idx11];
                                
                                float g = (1.0f - fx) * (1.0f - fy) * hdrData[idx00 + 1] +
                                          fx * (1.0f - fy) * hdrData[idx10 + 1] +
                                          (1.0f - fx) * fy * hdrData[idx01 + 1] +
                                          fx * fy * hdrData[idx11 + 1];
                                
                                float b = (1.0f - fx) * (1.0f - fy) * hdrData[idx00 + 2] +
                                          fx * (1.0f - fy) * hdrData[idx10 + 2] +
                                          (1.0f - fx) * fy * hdrData[idx01 + 2] +
                                          fx * fy * hdrData[idx11 + 2];
                                
                                UINT index = (y * cubeSize + x) * 4;
                                faceData[face][index] = r;      // R
                                faceData[face][index + 1] = g;  // G
                                faceData[face][index + 2] = b;  // B
                                faceData[face][index + 3] = 1.0f;  // A
                            }
                        }
                        
                        subresourceData[face].pSysMem = faceData[face].data();
                        subresourceData[face].SysMemPitch = cubeSize * 4 * sizeof(float);
                        subresourceData[face].SysMemSlicePitch = 0;
                    }
                    
                    stbi_image_free(hdrData);
                    
                    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
                    HRESULT hr = m_device->CreateTexture2D(&texDesc, subresourceData.data(), texture.GetAddressOf());
                    if (SUCCEEDED(hr))
                    {
                        // 创建立方体贴图的着色器资源视图
                        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                        srvDesc.Format = texDesc.Format;
                        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
                        srvDesc.TextureCube.MipLevels = 1;
                        srvDesc.TextureCube.MostDetailedMip = 0;
                        
                        hr = m_device->CreateShaderResourceView(texture.Get(), &srvDesc, m_environmentMapSRV.GetAddressOf());
                        if (SUCCEEDED(hr))
                        {
                            OutputDebugStringW(L"Successfully loaded environment map: ");
                            OutputDebugStringW(filename.c_str());
                            OutputDebugStringW(L"\n");
                            return true;
                        }
                    }
                }
                else
                {
                    if (ext == L"exr")
                    {
                        OutputDebugStringW(L"Warning: EXR format is not supported by stb_image. Please convert to HDR format or use a library that supports EXR (such as tinyexr).\n");
                        OutputDebugStringW(L"Failed to load EXR file: ");
                    }
                    else
                    {
                        OutputDebugStringW(L"Failed to load HDR file: ");
                    }
                    OutputDebugStringW(filename.c_str());
                    OutputDebugStringW(L"\n");
                }
            }
        }
    }
    
    // 如果加载失败或没有提供文件名，创建默认环境贴图（天空蓝色渐变）
    OutputDebugStringW(L"Creating default environment map (sky blue gradient)...\n");
    const UINT cubeSize = 512;  // 立方体贴图每面大小
    
    // 创建立方体贴图
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = cubeSize;
    texDesc.Height = cubeSize;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 6;  // 立方体贴图有6个面
    texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;  // HDR格式
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = 0;
    texDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;  // 标记为立方体贴图
    
    // 为每个面生成默认颜色（天空蓝色渐变）
    std::vector<D3D11_SUBRESOURCE_DATA> subresourceData(6);
    std::vector<std::vector<float>> faceData(6);
    
    for (UINT face = 0; face < 6; ++face)
    {
        faceData[face].resize(cubeSize * cubeSize * 4);  // RGBA，每个float
        
        for (UINT y = 0; y < cubeSize; ++y)
        {
            for (UINT x = 0; x < cubeSize; ++x)
            {
                // 简单的天空蓝色渐变
                float normalizedY = float(y) / float(cubeSize);
                float t = normalizedY;
                float r = 0.5f * (1.0f - t) + 0.1f * t;
                float g = 0.7f * (1.0f - t) + 0.1f * t;
                float b = 1.0f * (1.0f - t) + 0.2f * t;
                
                UINT index = (y * cubeSize + x) * 4;
                faceData[face][index] = r;      // R
                faceData[face][index + 1] = g;  // G
                faceData[face][index + 2] = b;  // B
                faceData[face][index + 3] = 1.0f;     // A
            }
        }
        
        subresourceData[face].pSysMem = faceData[face].data();
        subresourceData[face].SysMemPitch = cubeSize * 4 * sizeof(float);
        subresourceData[face].SysMemSlicePitch = 0;
    }
    
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    HRESULT hr = m_device->CreateTexture2D(&texDesc, subresourceData.data(), texture.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    // 创建立方体贴图的着色器资源视图
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MipLevels = 1;
    srvDesc.TextureCube.MostDetailedMip = 0;
    
    hr = m_device->CreateShaderResourceView(texture.Get(), &srvDesc, m_environmentMapSRV.GetAddressOf());
    return SUCCEEDED(hr);
}

// ============================================================================
// 创建天空盒Shader
// ============================================================================
bool Renderer::CreateSkyboxShaders()
{
    // 编译天空盒顶点着色器
    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    if (!CompileShaderFromFile(L"Shaders/SkyboxShader.hlsl", "VS", "vs_5_0", vsBlob.GetAddressOf()))
    {
        m_lastError = L"Failed to compile SkyboxShader.hlsl (VS).";
        return false;
    }
    
    HRESULT hr = m_device->CreateVertexShader(
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        nullptr,
        m_skyboxVS.GetAddressOf()
    );
    if (FAILED(hr))
    {
        m_lastError = L"Failed to create skybox vertex shader.";
        return false;
    }
    
    // 编译天空盒像素着色器
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    if (!CompileShaderFromFile(L"Shaders/SkyboxShader.hlsl", "PS", "ps_5_0", psBlob.GetAddressOf()))
    {
        m_lastError = L"Failed to compile SkyboxShader.hlsl (PS).";
        return false;
    }
    
    hr = m_device->CreatePixelShader(
        psBlob->GetBufferPointer(),
        psBlob->GetBufferSize(),
        nullptr,
        m_skyboxPS.GetAddressOf()
    );
    if (FAILED(hr))
    {
        m_lastError = L"Failed to create skybox pixel shader.";
        return false;
    }
    
    // 创建天空盒输入布局（只需要位置，不需要其他属性）
    D3D11_INPUT_ELEMENT_DESC skyboxLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    
    hr = m_device->CreateInputLayout(
        skyboxLayout,
        ARRAYSIZE(skyboxLayout),
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        m_skyboxInputLayout.GetAddressOf()
    );
    if (FAILED(hr))
    {
        m_lastError = L"Failed to create skybox input layout.";
        return false;
    }
    
    return true;
}

// ============================================================================
// 创建地形shader
// ============================================================================
bool Renderer::CreateTerrainShaders()
{
    // 编译地形顶点着色器
    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    if (!CompileShaderFromFile(L"Shaders/TerrainVertexShader.hlsl", "VS", "vs_5_0", vsBlob.GetAddressOf()))
    {
        m_lastError = L"Failed to compile TerrainVertexShader.hlsl (VS).";
        OutputDebugStringW(L"[TERRAIN DEBUG] Failed to compile TerrainVertexShader.hlsl\n");
        return false;
    }
    
    HRESULT hr = m_device->CreateVertexShader(
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        nullptr,
        m_terrainVS.GetAddressOf()
    );
    if (FAILED(hr))
    {
        m_lastError = L"Failed to create terrain vertex shader.";
        OutputDebugStringW(L"[TERRAIN DEBUG] Failed to create terrain vertex shader\n");
        return false;
    }
    
    // 编译地形像素着色器
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    if (!CompileShaderFromFile(L"Shaders/TerrainPixelShader.hlsl", "PS", "ps_5_0", psBlob.GetAddressOf()))
    {
        m_lastError = L"Failed to compile TerrainPixelShader.hlsl (PS).";
        OutputDebugStringW(L"[TERRAIN DEBUG] Failed to compile TerrainPixelShader.hlsl\n");
        return false;
    }
    
    hr = m_device->CreatePixelShader(
        psBlob->GetBufferPointer(),
        psBlob->GetBufferSize(),
        nullptr,
        m_terrainPS.GetAddressOf()
    );
    if (FAILED(hr))
    {
        m_lastError = L"Failed to create terrain pixel shader.";
        OutputDebugStringW(L"[TERRAIN DEBUG] Failed to create terrain pixel shader\n");
        return false;
    }
    
    // 创建地形输入布局（与通用shader相同，因为使用相同的Vertex结构）
    D3D11_INPUT_ELEMENT_DESC terrainLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    
    hr = m_device->CreateInputLayout(
        terrainLayout,
        ARRAYSIZE(terrainLayout),
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        m_terrainInputLayout.GetAddressOf()
    );
    if (FAILED(hr))
    {
        m_lastError = L"Failed to create terrain input layout.";
        OutputDebugStringW(L"[TERRAIN DEBUG] Failed to create terrain input layout\n");
        return false;
    }
    
    // 保存顶点着色器的编译数据
    m_terrainVSBlob = vsBlob;
    
    OutputDebugStringW(L"[TERRAIN DEBUG] Terrain shaders created successfully.\n");
    return true;
}

// ============================================================================
// 创建地形光栅化状态（填充和线框）
// ============================================================================
bool Renderer::CreateTerrainRasterizerStates()
{
    // 创建线框光栅化状态
    D3D11_RASTERIZER_DESC wireframeDesc = {};
    wireframeDesc.FillMode = D3D11_FILL_WIREFRAME;  // 线框模式
    wireframeDesc.CullMode = D3D11_CULL_BACK;       // 背面剔除
    wireframeDesc.FrontCounterClockwise = false;    // 逆时针为正面
    wireframeDesc.DepthBias = 0;
    wireframeDesc.DepthBiasClamp = 0.0f;
    wireframeDesc.SlopeScaledDepthBias = 0.0f;
    wireframeDesc.DepthClipEnable = true;
    wireframeDesc.ScissorEnable = false;
    wireframeDesc.MultisampleEnable = false;
    wireframeDesc.AntialiasedLineEnable = false;
    
    HRESULT hr = m_device->CreateRasterizerState(&wireframeDesc, m_terrainWireframeRasterizerState.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[TERRAIN DEBUG] Failed to create terrain wireframe rasterizer state.\n");
        return false;
    }
    
    OutputDebugStringW(L"[TERRAIN DEBUG] Terrain rasterizer states created successfully.\n");
    return true;
}

// ============================================================================
// 创建天空盒几何体（立方体）
// ============================================================================
bool Renderer::CreateSkyboxGeometry()
{
    // 创建立方体的8个顶点（局部空间，中心在原点，大小为2x2x2）
    // 注意：立方体的面是向内的（因为从内部看）
    struct SkyboxVertex
    {
        float x, y, z;
    };
    
    SkyboxVertex vertices[] = {
        // 前面（+Z）
        { -1.0f, -1.0f,  1.0f },
        {  1.0f, -1.0f,  1.0f },
        {  1.0f,  1.0f,  1.0f },
        { -1.0f,  1.0f,  1.0f },
        // 后面（-Z）
        {  1.0f, -1.0f, -1.0f },
        { -1.0f, -1.0f, -1.0f },
        { -1.0f,  1.0f, -1.0f },
        {  1.0f,  1.0f, -1.0f },
        // 右面（+X）
        {  1.0f, -1.0f,  1.0f },
        {  1.0f, -1.0f, -1.0f },
        {  1.0f,  1.0f, -1.0f },
        {  1.0f,  1.0f,  1.0f },
        // 左面（-X）
        { -1.0f, -1.0f, -1.0f },
        { -1.0f, -1.0f,  1.0f },
        { -1.0f,  1.0f,  1.0f },
        { -1.0f,  1.0f, -1.0f },
        // 上面（+Y）
        { -1.0f,  1.0f,  1.0f },
        {  1.0f,  1.0f,  1.0f },
        {  1.0f,  1.0f, -1.0f },
        { -1.0f,  1.0f, -1.0f },
        // 下面（-Y）
        { -1.0f, -1.0f, -1.0f },
        {  1.0f, -1.0f, -1.0f },
        {  1.0f, -1.0f,  1.0f },
        { -1.0f, -1.0f,  1.0f }
    };
    
    // 创建索引（每个面2个三角形）
    uint32_t indices[] = {
        // 前面
        0, 1, 2,  0, 2, 3,
        // 后面
        4, 5, 6,  4, 6, 7,
        // 右面
        8, 9, 10,  8, 10, 11,
        // 左面
        12, 13, 14,  12, 14, 15,
        // 上面
        16, 17, 18,  16, 18, 19,
        // 下面
        20, 21, 22,  20, 22, 23
    };
    
    // 创建顶点缓冲区
    D3D11_BUFFER_DESC vbd = {};
    vbd.Usage = D3D11_USAGE_DEFAULT;
    vbd.ByteWidth = sizeof(vertices);
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbd.CPUAccessFlags = 0;
    
    D3D11_SUBRESOURCE_DATA vinitData = {};
    vinitData.pSysMem = vertices;
    
    HRESULT hr = m_device->CreateBuffer(&vbd, &vinitData, m_skyboxVertexBuffer.GetAddressOf());
    if (FAILED(hr))
        return false;
    
    // 创建索引缓冲区
    D3D11_BUFFER_DESC ibd = {};
    ibd.Usage = D3D11_USAGE_DEFAULT;
    ibd.ByteWidth = sizeof(indices);
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    ibd.CPUAccessFlags = 0;
    
    D3D11_SUBRESOURCE_DATA iinitData = {};
    iinitData.pSysMem = indices;
    
    hr = m_device->CreateBuffer(&ibd, &iinitData, m_skyboxIndexBuffer.GetAddressOf());
    return SUCCEEDED(hr);
}

// ============================================================================
// 创建天空盒深度状态（使用LESS_EQUAL，确保天空盒在最后绘制）
// ============================================================================
bool Renderer::CreateSkyboxDepthState()
{
    D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
    depthStencilDesc.DepthEnable = true;  // 启用深度测试
    depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;  // 不写入深度
    depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;  // 使用LESS_EQUAL（深度值1.0应该通过测试）
    depthStencilDesc.StencilEnable = false;
    
    HRESULT hr = m_device->CreateDepthStencilState(&depthStencilDesc, m_skyboxDepthStencilState.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"Failed to create skybox depth state. HRESULT: ");
        wchar_t hrStr[16];
        swprintf_s(hrStr, L"%08X\n", hr);
        OutputDebugStringW(hrStr);
        return false;
    }
    
    // 创建天空盒光栅化状态（禁用背面剔除，因为从内部看立方体）
    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;  // 禁用背面剔除（从内部看立方体）
    rasterizerDesc.FrontCounterClockwise = false;
    rasterizerDesc.DepthBias = 0;
    rasterizerDesc.DepthBiasClamp = 0.0f;
    rasterizerDesc.SlopeScaledDepthBias = 0.0f;
    rasterizerDesc.DepthClipEnable = true;
    rasterizerDesc.ScissorEnable = false;
    rasterizerDesc.MultisampleEnable = false;
    rasterizerDesc.AntialiasedLineEnable = false;
    
    hr = m_device->CreateRasterizerState(&rasterizerDesc, m_skyboxRasterizerState.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"Failed to create skybox rasterizer state.\n");
        return false;
    }
    
    return true;
}

// ============================================================================
// 渲染天空盒
// ============================================================================
void Renderer::RenderSkybox()
{
    if (!m_skyboxVS || !m_skyboxPS || !m_skyboxVertexBuffer || !m_skyboxIndexBuffer || !m_environmentMapSRV)
    {
        // 调试：输出哪个资源缺失
        if (!m_skyboxVS) OutputDebugStringW(L"Skybox VS missing\n");
        if (!m_skyboxPS) OutputDebugStringW(L"Skybox PS missing\n");
        if (!m_skyboxVertexBuffer) OutputDebugStringW(L"Skybox VB missing\n");
        if (!m_skyboxIndexBuffer) OutputDebugStringW(L"Skybox IB missing\n");
        if (!m_environmentMapSRV) OutputDebugStringW(L"Environment map SRV missing\n");
        return;
    }
    
    // 保存当前状态
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> oldDepthStencilState;
    UINT oldStencilRef;
    m_context->OMGetDepthStencilState(oldDepthStencilState.GetAddressOf(), &oldStencilRef);
    
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> oldRasterizerState;
    m_context->RSGetState(oldRasterizerState.GetAddressOf());
    
    // 设置天空盒深度状态
    m_context->OMSetDepthStencilState(m_skyboxDepthStencilState.Get(), 0);
    
    // 设置天空盒光栅化状态（禁用背面剔除）
    if (m_skyboxRasterizerState)
        m_context->RSSetState(m_skyboxRasterizerState.Get());
    
    // 设置天空盒shader和输入布局
    m_context->VSSetShader(m_skyboxVS.Get(), nullptr, 0);
    m_context->PSSetShader(m_skyboxPS.Get(), nullptr, 0);
    m_context->IASetInputLayout(m_skyboxInputLayout.Get());
    
    // 绑定常量缓冲区（使用相同的常量缓冲区，但只使用view和projection）
    m_context->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    
    // 绑定环境贴图和采样器
    m_context->PSSetShaderResources(3, 1, m_environmentMapSRV.GetAddressOf());
    m_context->PSSetSamplers(1, 1, m_iblSamplerState.GetAddressOf());
    
    // 设置图元类型
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    
    // 绑定顶点缓冲区和索引缓冲区
    UINT stride = sizeof(float) * 3;
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_skyboxVertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->IASetIndexBuffer(m_skyboxIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    
    // 绘制天空盒（36个索引，6个面 * 2个三角形 * 3个顶点）
    m_context->DrawIndexed(36, 0, 0);
    
    // 调试：检查DrawIndexed是否成功（实际上无法直接检查，但至少确保调用了）
    // OutputDebugStringW(L"Skybox DrawIndexed called\n");
    
    // 恢复之前的状态
    m_context->OMSetDepthStencilState(oldDepthStencilState.Get(), oldStencilRef);
    if (oldRasterizerState)
        m_context->RSSetState(oldRasterizerState.Get());
}

// ============================================================================
// 初始化地形
// ============================================================================
bool Renderer::InitializeTerrain()
{
    // 创建地形对象
    m_terrain = new Terrain();
    
    // 设置地形参数
    TerrainParams params;
    params.width = 256;          // 高度图宽度（顶点数）
    params.height = 256;         // 高度图高度（顶点数）
    params.sizeX = 200.0f;       // 世界空间X方向大小（单位：米）
    params.sizeZ = 200.0f;       // 世界空间Z方向大小（单位：米）
    params.heightScale = 30.0f;  // 高度缩放因子
    params.heightOffset = 0.0f;  // 高度偏移量
    
    // 尝试加载高度图，如果失败则使用程序化生成
    wchar_t exePath[MAX_PATH] = { 0 };
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0)
    {
        std::wstring exeDir = exePath;
        size_t lastSlash = exeDir.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos)
        {
            exeDir = exeDir.substr(0, lastSlash + 1);
            
            std::wstring projectRoot = exeDir;
            for (int i = 0; i < 2; ++i)
            {
                size_t slash = projectRoot.find_last_of(L"\\/", projectRoot.length() - 2);
                if (slash != std::wstring::npos)
                    projectRoot = projectRoot.substr(0, slash + 1);
            }
            
            // 尝试加载高度图文件
            std::vector<std::wstring> heightmapPaths = {
                projectRoot + L"Res/heightmap.png",
                projectRoot + L"Res/heightmap.jpg",
                projectRoot + L"Res/heightmap.bmp",
                exeDir + L"Res/heightmap.png",
                exeDir + L"Res/heightmap.jpg"
            };
            
            for (const auto& path : heightmapPaths)
            {
                if (m_terrain->CreateFromHeightmap(m_device.Get(), path, params))
                {
                    OutputDebugStringW(L"Terrain loaded from heightmap: ");
                    OutputDebugStringW(path.c_str());
                    OutputDebugStringW(L"\n");
                    return true;
                }
            }
        }
    }
    
    // 如果加载高度图失败，使用程序化生成（用于测试）
    OutputDebugStringW(L"[TERRAIN DEBUG] Heightmap not found, using procedural terrain generation.\n");
    if (m_terrain->CreateProcedural(m_device.Get(), params))
    {
        OutputDebugStringW(L"[TERRAIN DEBUG] Procedural terrain created successfully.\n");
        wchar_t msg[512];
        UINT indexCount = m_terrain->GetIndexCount();
        swprintf_s(msg, L"[TERRAIN DEBUG] Terrain created: %d vertices, %u indices\n", 
                   params.width * params.height, 
                   indexCount);
        OutputDebugStringW(msg);
        
        // 额外验证
        if (indexCount == 0)
        {
            OutputDebugStringW(L"[TERRAIN DEBUG] WARNING: Index count is 0!\n");
        }
        else if (indexCount > 1000000)
        {
            OutputDebugStringW(L"[TERRAIN DEBUG] WARNING: Index count seems too large!\n");
        }
        
        // 验证缓冲区
        if (m_terrain->GetVertexBuffer() && m_terrain->GetIndexBuffer())
        {
            OutputDebugStringW(L"[TERRAIN DEBUG] Terrain buffers created successfully.\n");
        }
        else
        {
            OutputDebugStringW(L"[TERRAIN DEBUG] ERROR: Terrain buffers failed to create!\n");
        }
        
        return true;
    }
    
    // 如果都失败，清理并返回false
    delete m_terrain;
    m_terrain = nullptr;
    return false;
}

// ============================================================================
// 渲染地形
// ============================================================================
void Renderer::RenderTerrain()
{
    if (!m_terrain)
    {
        // 调试：输出地形未创建
        static bool warned = false;
        if (!warned)
        {
            OutputDebugStringW(L"[TERRAIN DEBUG] RenderTerrain: Terrain is null!\n");
            warned = true;
        }
        return;
    }
    
    // 调试：检查地形资源
    if (!m_terrain->GetVertexBuffer() || !m_terrain->GetIndexBuffer())
    {
        static bool warned = false;
        if (!warned)
        {
            OutputDebugStringW(L"[TERRAIN DEBUG] Terrain buffers are null!\n");
            if (!m_terrain->GetVertexBuffer()) OutputDebugStringW(L"  - Vertex buffer is null\n");
            if (!m_terrain->GetIndexBuffer()) OutputDebugStringW(L"  - Index buffer is null\n");
            warned = true;
        }
        return;
    }
    
    // 使用地形专用的shader和输入布局
    if (!m_terrainVS || !m_terrainPS || !m_terrainInputLayout)
    {
        static bool warned = false;
        if (!warned)
        {
            OutputDebugStringW(L"[TERRAIN DEBUG] Terrain shaders not created!\n");
            warned = true;
        }
        return;
    }
    
    // 保存当前光栅化状态
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> oldRasterizerState;
    m_context->RSGetState(oldRasterizerState.GetAddressOf());
    
    // 根据线框模式设置光栅化状态
    if (m_terrainWireframe && m_terrainWireframeRasterizerState)
    {
        m_context->RSSetState(m_terrainWireframeRasterizerState.Get());
    }
    else
    {
        // 使用默认填充模式（不设置状态，使用默认的填充模式）
        // 或者可以创建一个填充模式的状态
        m_context->RSSetState(nullptr);  // 使用默认状态
    }
    
    // 设置地形shader和输入布局
    m_context->IASetInputLayout(m_terrainInputLayout.Get());
    m_context->VSSetShader(m_terrainVS.Get(), nullptr, 0);
    m_context->PSSetShader(m_terrainPS.Get(), nullptr, 0);
    
    // 绑定常量缓冲区到顶点着色器（register b0）
    m_context->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    // 绑定光照常量缓冲区到像素着色器（register b1）
    m_context->PSSetConstantBuffers(1, 1, m_lightBuffer.GetAddressOf());
    
    // 注意：地形的顶点已经在世界空间中定义，所以需要使用单位world矩阵
    // 这里我们临时更新常量缓冲区中的world矩阵为单位矩阵
    // 由于地形顶点已经在世界空间，world矩阵应该是单位矩阵
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = m_context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr))
    {
        ConstantBuffer* cb = (ConstantBuffer*)mapped.pData;
        
        // 保存当前的world矩阵（用于模型渲染）
        XMFLOAT4X4 savedWorld = cb->world;
        
        // 直接从相机获取view和projection矩阵（确保使用最新的、正确的矩阵）
        XMMATRIX viewMatrix;
        XMMATRIX projMatrix;
        if (m_camera)
        {
            viewMatrix = m_camera->GetViewMatrix();
            float aspect = (float)m_width / (float)m_height;
            projMatrix = m_camera->GetProjectionMatrix(aspect);
        }
        else
        {
            // 如果没有相机，使用默认值
            XMVECTOR eye = XMVectorSet(0.0f, 0.0f, -2.0f, 0.0f);
            XMVECTOR at = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
            XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            viewMatrix = XMMatrixLookAtLH(eye, at, up);
            float aspect = (float)m_width / (float)m_height;
            projMatrix = XMMatrixPerspectiveFovLH(XM_PI / 4.0f, aspect, 0.1f, 500.0f);
        }
        
        // 设置单位world矩阵（因为地形顶点已经在世界空间）
        XMMATRIX identity = XMMatrixIdentity();
        XMStoreFloat4x4(&cb->world, XMMatrixTranspose(identity));
        
        // 存储view和projection矩阵（转置后存储，因为HLSL使用列主序）
        XMStoreFloat4x4(&cb->view, XMMatrixTranspose(viewMatrix));
        XMStoreFloat4x4(&cb->projection, XMMatrixTranspose(projMatrix));
        
        // 更新worldViewProj矩阵
        XMMATRIX worldViewProj = identity * viewMatrix * projMatrix;
        XMStoreFloat4x4(&cb->worldViewProj, XMMatrixTranspose(worldViewProj));
        
        m_context->Unmap(m_constantBuffer.Get(), 0);
        
        // 调试：检查常量缓冲区和矩阵
        static bool cbLogged = false;
        if (!cbLogged)
        {
            OutputDebugStringW(L"[TERRAIN DEBUG] Constant buffer mapped and updated successfully.\n");
            
            // 输出矩阵信息用于调试（直接从相机获取的矩阵）
            wchar_t msg[512];
            XMFLOAT4X4 viewFloat, projFloat;
            XMStoreFloat4x4(&viewFloat, viewMatrix);  // 直接从相机获取的矩阵
            XMStoreFloat4x4(&projFloat, projMatrix);  // 直接从相机获取的矩阵
            
            // 检查view矩阵的平移部分（相机位置）
            // 对于未转置的view矩阵，平移在_41, _42, _43位置
            swprintf_s(msg, L"[TERRAIN DEBUG] View matrix translation: (%.2f, %.2f, %.2f)\n",
                      viewFloat._41, viewFloat._42, viewFloat._43);
            OutputDebugStringW(msg);
            
            // 检查投影矩阵的near/far平面
            // 对于DirectX透视投影矩阵：near = _43 / _33, far = _43 / (_33 - 1.0f)
            // 注意：如果_33为0，说明投影矩阵可能有问题
            if (projFloat._33 != 0.0f)
            {
                float nearPlane = projFloat._43 / projFloat._33;
                float farPlane = projFloat._43 / (projFloat._33 - 1.0f);
                swprintf_s(msg, L"[TERRAIN DEBUG] Projection near=%.2f, far=%.2f (from _33=%.4f, _43=%.4f)\n",
                          nearPlane, farPlane, projFloat._33, projFloat._43);
                OutputDebugStringW(msg);
            }
            else
            {
                swprintf_s(msg, L"[TERRAIN DEBUG] ERROR: Projection matrix _33 is zero! (_33=%.4f, _43=%.4f)\n",
                          projFloat._33, projFloat._43);
                OutputDebugStringW(msg);
            }
            
            // 检查矩阵是否有效
            if (isnan(projFloat._33) || isnan(projFloat._43))
            {
                OutputDebugStringW(L"[TERRAIN DEBUG] ERROR: Projection matrix contains NaN values!\n");
            }
            
            cbLogged = true;
        }
        
        // 绑定地形纹理（地形shader只需要BaseColor纹理）
        if (m_textureSRV)
        {
            m_context->PSSetShaderResources(0, 1, m_textureSRV.GetAddressOf());
        }
        else
        {
            // 如果没有纹理，清除纹理绑定
            ID3D11ShaderResourceView* nullSRV = nullptr;
            m_context->PSSetShaderResources(0, 1, &nullSRV);
        }
        
        // 绑定采样器（地形shader只需要s0）
        if (m_samplerState)
        {
            m_context->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());
        }
        
        // 确保深度测试启用（地形渲染前）
        m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);
        
        // 绘制地形（地形会使用自己的顶点和索引缓冲区）
        // 调试输出
        static bool terrainDrawLogged = false;
        if (!terrainDrawLogged)
        {
            wchar_t msg[512];
            swprintf_s(msg, L"[TERRAIN DEBUG] Drawing terrain: %d indices\n", 
                      m_terrain->GetIndexCount());
            OutputDebugStringW(msg);
            
            // 检查深度状态
            Microsoft::WRL::ComPtr<ID3D11DepthStencilState> currentDepthState;
            UINT stencilRef;
            m_context->OMGetDepthStencilState(currentDepthState.GetAddressOf(), &stencilRef);
            if (currentDepthState.Get() == m_depthStencilState.Get())
            {
                OutputDebugStringW(L"[TERRAIN DEBUG] Depth stencil state is set correctly.\n");
            }
            else
            {
                OutputDebugStringW(L"[TERRAIN DEBUG] WARNING: Depth stencil state may not be set!\n");
            }
            
            // 输出地形参数
            const TerrainParams& params = m_terrain->GetParams();
            swprintf_s(msg, L"[TERRAIN DEBUG] Terrain params: sizeX=%.1f, sizeZ=%.1f, heightScale=%.1f, heightOffset=%.1f\n", 
                      params.sizeX, params.sizeZ, params.heightScale, params.heightOffset);
            OutputDebugStringW(msg);
            
            // 输出地形范围
            swprintf_s(msg, L"[TERRAIN DEBUG] Terrain range: X=[%.1f, %.1f], Z=[%.1f, %.1f], Y=[%.1f, %.1f]\n",
                      -params.sizeX * 0.5f, params.sizeX * 0.5f,
                      -params.sizeZ * 0.5f, params.sizeZ * 0.5f,
                      params.heightOffset, params.heightOffset + params.heightScale);
            OutputDebugStringW(msg);
            
            // 输出相机信息
            if (m_camera)
            {
                XMFLOAT3 camPos = m_camera->GetPosition();
                swprintf_s(msg, L"[TERRAIN DEBUG] Camera position: (%.1f, %.1f, %.1f)\n",
                          camPos.x, camPos.y, camPos.z);
                OutputDebugStringW(msg);
                
                // 检查地形是否在相机视野内
                // 地形中心在(0, 15, 0)，范围是X=[-100,100], Z=[-100,100], Y=[0,30]
                // 相机在(0, 100, 100)，应该能看到地形
                float terrainCenterY = params.heightOffset + params.heightScale * 0.5f;
                swprintf_s(msg, L"[TERRAIN DEBUG] Terrain center Y: %.1f, Camera Y: %.1f (should see terrain if camera is above)\n",
                          terrainCenterY, camPos.y);
                OutputDebugStringW(msg);
            }
            
            terrainDrawLogged = true;
        }
        
        // 每帧输出（用于确认是否在绘制）
        static int frameCount = 0;
        frameCount++;
        if (frameCount % 60 == 0)  // 每60帧输出一次
        {
            wchar_t msg[256];
            swprintf_s(msg, L"[TERRAIN DEBUG] Frame %d: Calling Terrain::Render()\n", frameCount);
            OutputDebugStringW(msg);
        }
        
        // 使用CDLOD渲染（如果相机可用）
        if (m_camera)
        {
            XMFLOAT3 camPos = m_camera->GetPosition();
            m_terrain->Render(m_context.Get(), camPos);
        }
        else
        {
            // 回退到旧版本渲染
            m_terrain->Render(m_context.Get());
        }
        
        // 恢复原来的光栅化状态
        if (oldRasterizerState)
        {
            m_context->RSSetState(oldRasterizerState.Get());
        }
        else
        {
            m_context->RSSetState(nullptr);
        }
        
        // 恢复原来的world矩阵（用于模型渲染）
        // 注意：由于UpdateConstantBuffers会在模型渲染前再次调用，这里不需要恢复
        // 但为了保持一致性，我们仍然恢复world矩阵
        hr = m_context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr))
        {
            cb = (ConstantBuffer*)mapped.pData;
            cb->world = savedWorld;
            
            // 重新计算worldViewProj矩阵（使用模型的world矩阵）
            // 从相机重新获取view和projection矩阵
            XMMATRIX worldMatrix = XMMatrixTranspose(XMLoadFloat4x4(&savedWorld));  // 转置回来
            XMMATRIX viewMat;
            XMMATRIX projMat;
            if (m_camera)
            {
                viewMat = m_camera->GetViewMatrix();
                float aspect = (float)m_width / (float)m_height;
                projMat = m_camera->GetProjectionMatrix(aspect);
            }
            else
            {
                XMVECTOR eye = XMVectorSet(0.0f, 0.0f, -2.0f, 0.0f);
                XMVECTOR at = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
                XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
                viewMat = XMMatrixLookAtLH(eye, at, up);
                float aspect = (float)m_width / (float)m_height;
                projMat = XMMatrixPerspectiveFovLH(XM_PI / 4.0f, aspect, 0.1f, 500.0f);
            }
            
            XMMATRIX wvp = worldMatrix * viewMat * projMat;
            XMStoreFloat4x4(&cb->worldViewProj, XMMatrixTranspose(wvp));
            
            // 同时更新view和projection矩阵（确保它们是最新的）
            XMStoreFloat4x4(&cb->view, XMMatrixTranspose(viewMat));
            XMStoreFloat4x4(&cb->projection, XMMatrixTranspose(projMat));
            
            m_context->Unmap(m_constantBuffer.Get(), 0);
        }
    }
    else
    {
        // 调试：常量缓冲区映射失败
        static bool mapFailedLogged = false;
        if (!mapFailedLogged)
        {
            wchar_t msg[256];
            swprintf_s(msg, L"[TERRAIN DEBUG] ERROR: Failed to map constant buffer! HRESULT: 0x%08X\n", hr);
            OutputDebugStringW(msg);
            mapFailedLogged = true;
        }
    }
}

// ============================================================================
// 切换地形LOD锁定
// ============================================================================
void Renderer::ToggleTerrainLODLock()
{
    if (m_terrain)
    {
        bool currentLocked = m_terrain->IsLODLocked();
        m_terrain->SetLODLocked(!currentLocked);
        
        wchar_t msg[256];
        if (!currentLocked)
        {
            swprintf_s(msg, L"[TERRAIN] LOD locked at current level\n");
        }
        else
        {
            swprintf_s(msg, L"[TERRAIN] LOD unlocked, using distance-based selection\n");
        }
        OutputDebugStringW(msg);
    }
}

// ============================================================================
// 设置地形LOD锁定级别
// ============================================================================
void Renderer::SetTerrainLODLockLevel(int level)
{
    if (m_terrain && level >= 0 && level < 4)
    {
        m_terrain->SetLockedLODLevel(level);
        wchar_t msg[256];
        swprintf_s(msg, L"[TERRAIN] LOD locked to level %d\n", level);
        OutputDebugStringW(msg);
    }
}
