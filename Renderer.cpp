// 在包含 Windows.h 之前定义 NOMINMAX，避免 min/max 宏冲突
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN

#include "Renderer.h"
#include "MeshMgr.h"
#include "Camera.h"
#include "ModelLoader.h"
#include "MeshGPU.h"
#include "Terrain.h"
#include "Terrain_new.h"
#include "GrassSystem.h"
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
#pragma comment(lib, "dwrite.lib")  // DirectWrite库
#pragma comment(lib, "d2d1.lib")  // Direct2D库

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

// Shadow Map常量缓冲区结构体（必须对齐到16字节边界）
struct alignas(16) ShadowConstantBuffer
{
    XMFLOAT4X4 world;          // 世界变换矩阵
    XMFLOAT4X4 lightView;       // 光源视图矩阵
    XMFLOAT4X4 lightProjection; // 光源投影矩阵
    XMFLOAT4X4 lightViewProj;  // 组合矩阵
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
    
    // Shadow Map相关矩阵（添加到LightBuffer末尾）
    XMFLOAT4X4 lightView;       // 光源视图矩阵 (64 bytes)
    XMFLOAT4X4 lightProjection; // 光源投影矩阵 (64 bytes)
    XMFLOAT4X4 lightWorldViewProj; // 光源世界-视图-投影矩阵 (64 bytes) - 注意：这里使用单位world矩阵，因为地形world是单位矩阵

    // 总共: 320 bytes (16字节的倍数)
};

// ============================================================================
// 初始化渲染器
// ============================================================================
bool Renderer::Initialize(HWND hwnd, int width, int height)
{
    // 清除之前的错误信息
    m_lastError.clear();

    // 初始化成员变量
    
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
    // 步骤 11.5: 创建光源可视化资源
    // ========================================================================
    if (!CreateLightVisualizationShaders())
    {
        OutputDebugStringW(L"Warning: Failed to create light visualization shaders.\n");
        return false;
    }
    if (!CreateLightVisualizationGeometry())
    {
        OutputDebugStringW(L"Warning: Failed to create light visualization geometry.\n");
        return false;
    }
    OutputDebugStringW(L"Light visualization resources created successfully.\n");

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
    // 步骤 13.5: 初始化草地系统（必须在地形初始化之后）
    // ========================================================================
    if (!InitializeGrassSystem())
    {
        OutputDebugStringW(L"Warning: Failed to initialize grass system.\n");
    }
    
    // ========================================================================
    // 步骤 13.6: 创建Shadow Map资源
    // ========================================================================
    if (!CreateShadowMap())
    {
        OutputDebugStringW(L"Warning: Failed to create shadow map.\n");
    }
    else
    {
        OutputDebugStringW(L"Shadow map created successfully.\n");
    }
    
    // ========================================================================
    // 步骤 13.7: 创建Shadow Map Shader
    // ========================================================================
    if (!CreateShadowMapShaders())
    {
        OutputDebugStringW(L"Warning: Failed to create shadow map shaders.\n");
    }
    else
    {
        OutputDebugStringW(L"Shadow map shaders created successfully.\n");
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
    
    // ========================================================================
    // 步骤 16: 初始化文字渲染系统
    // ========================================================================
    if (!InitializeTextRendering())
    {
        OutputDebugStringW(L"Warning: Failed to initialize text rendering system.\n");
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
    // 步骤 4.5: 渲染Shadow Map（在场景渲染之前）
    // ========================================================================
    RenderShadowMap();

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
    // 步骤 6.6: 渲染草地系统（在地形之后渲染）
    // ========================================================================
    RenderGrassSystem(deltaTime);

    // ========================================================================
    // 步骤 6.7: 渲染光源可视化立方体
    // ========================================================================
    if (m_camera)
    {
        // 使用可控制的光源位置
        RenderLightVisualization(m_lightPosition);
    }

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
    // 步骤 7: 更新FPS和面数统计
    // ========================================================================
    UpdateFPS(deltaTime);
    UpdateTriangleCount();
    
    // ========================================================================
    // 步骤 8: 渲染FPS和面数统计文字
    // ========================================================================
    // 注意：Direct2D和Direct3D11共享渲染目标需要在D3D渲染完成后进行
    // 从当前back buffer获取表面并渲染文字
    if (m_d2dFactory && m_swapChain && m_dwriteFactory && m_textFormat)
    {
        // 在获取back buffer之前，先解绑D3D11的渲染目标视图，避免冲突
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
        
        // 刷新设备上下文，确保所有D3D操作完成
        // 这对于确保back buffer可以安全地用于D2D渲染很重要
        m_context->Flush();
        
        // 使用IDXGISurface1接口（D2D需要这个接口）
        // 直接从GetBuffer获取IDXGISurface1接口
        Microsoft::WRL::ComPtr<IDXGISurface1> backBuffer;
        HRESULT hr = m_swapChain->GetBuffer(0, __uuidof(IDXGISurface1), reinterpret_cast<void**>(backBuffer.GetAddressOf()));
        if (FAILED(hr))
        {
            // 如果直接获取IDXGISurface1失败，尝试先获取IDXGISurface再查询
            Microsoft::WRL::ComPtr<IDXGISurface> backBufferSurface;
            hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBufferSurface));
            if (SUCCEEDED(hr) && backBufferSurface)
            {
                // 查询IDXGISurface1接口
                hr = backBufferSurface.As(&backBuffer);
            }
        }
        
        if (SUCCEEDED(hr) && backBuffer)
        {
            // 每次都重新创建D2D渲染目标（因为back buffer会变化）
            m_d2dRenderTarget.Reset();
            m_textBrush.Reset();
            
            // 明确指定像素格式（与swap chain格式匹配）
            D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                96.0f, 96.0f  // DPI
            );
            
            hr = m_d2dFactory->CreateDxgiSurfaceRenderTarget(backBuffer.Get(), &props, m_d2dRenderTarget.GetAddressOf());
            if (SUCCEEDED(hr) && m_d2dRenderTarget)
            {
                // 创建文字画刷
                hr = m_d2dRenderTarget->CreateSolidColorBrush(
                    D2D1::ColorF(D2D1::ColorF::White),
                    m_textBrush.GetAddressOf()
                );
                
                if (SUCCEEDED(hr) && m_textBrush)
                {
                    // 开始D2D绘制
                    m_d2dRenderTarget->BeginDraw();
                    
                    // 创建文本布局并渲染
                    wchar_t statsText[256];
                    swprintf_s(statsText, L"FPS: %.1f\nTriangles: %u\n  Terrain: %u\n  Mesh: %u", 
                              m_fps, m_totalTriangles, m_terrainTriangles, m_meshTriangles);
                    
                    Microsoft::WRL::ComPtr<IDWriteTextLayout> textLayout;
                    hr = m_dwriteFactory->CreateTextLayout(
                        statsText,
                        (UINT32)wcslen(statsText),
                        m_textFormat.Get(),
                        (float)m_width,
                        (float)m_height,
                        textLayout.GetAddressOf()
                    );
                    
                    if (SUCCEEDED(hr) && textLayout)
                    {
                        m_d2dRenderTarget->DrawTextLayout(
                            D2D1::Point2F(10.0f, 10.0f),
                            textLayout.Get(),
                            m_textBrush.Get()
                        );
                    }
                    else
                    {
                        // 如果创建文本布局失败，尝试直接绘制文本
                        static bool textLayoutErrorLogged = false;
                        if (!textLayoutErrorLogged)
                        {
                            wchar_t errorMsg[256];
                            swprintf_s(errorMsg, L"Failed to create text layout: 0x%08X\n", hr);
                            OutputDebugStringW(errorMsg);
                            textLayoutErrorLogged = true;
                        }
                    }
                    
                    // 结束D2D绘制
                    hr = m_d2dRenderTarget->EndDraw();
                    if (FAILED(hr))
                    {
                        // 如果EndDraw失败，可能渲染目标已失效，下次重新创建
                        static bool endDrawErrorLogged = false;
                        if (!endDrawErrorLogged)
                        {
                            wchar_t errorMsg[256];
                            swprintf_s(errorMsg, L"D2D EndDraw failed: 0x%08X\n", hr);
                            OutputDebugStringW(errorMsg);
                            endDrawErrorLogged = true;
                        }
                        m_d2dRenderTarget.Reset();
                        m_textBrush.Reset();
                    }
                }
                else
                {
                    static bool brushErrorLogged = false;
                    if (!brushErrorLogged)
                    {
                        wchar_t errorMsg[256];
                        swprintf_s(errorMsg, L"Failed to create text brush: 0x%08X\n", hr);
                        OutputDebugStringW(errorMsg);
                        brushErrorLogged = true;
                    }
                }
            }
            else
            {
                static bool renderTargetErrorLogged = false;
                if (!renderTargetErrorLogged)
                {
                    wchar_t errorMsg[256];
                    swprintf_s(errorMsg, L"Failed to create D2D render target: 0x%08X\n", hr);
                    OutputDebugStringW(errorMsg);
                    renderTargetErrorLogged = true;
                }
            }
        }
    }
    
    // ========================================================================
    // 步骤 9: 呈现到屏幕
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
    // 释放草地系统
    if (m_grassSystem)
    {
        m_grassSystem->Cleanup();
        delete m_grassSystem;
        m_grassSystem = nullptr;
    }
    
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
    // Shadow Map资源
    m_shadowMapConstantBuffer.Reset();  // Shadow map常量缓冲区
    m_shadowMapInputLayout.Reset();     // Shadow map输入布局
    m_shadowMapPS.Reset();              // Shadow map像素着色器
    m_shadowMapVS.Reset();              // Shadow map顶点着色器
    m_shadowMapSamplerState.Reset();    // Shadow map采样器状态
    m_shadowMapSRV.Reset();             // Shadow map着色器资源视图
    m_shadowMapDSV.Reset();             // Shadow map深度视图
    m_shadowMapTexture.Reset();         // Shadow map纹理
    
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
// 获取角色位置（用于绘制模型）
// ============================================================================
XMFLOAT3 Renderer::GetCharacterPosition() const
{
    if (m_camera)
    {
        return m_camera->GetCharacterPosition();
    }
    // 如果没有相机，返回默认位置（地形中心）
    return XMFLOAT3(0.0f, 0.0f, 0.0f);
}

// ============================================================================
// 处理键盘输入（用于控制光源移动）
// 控制方式：
// - ↑ 上箭头：增加光源高度
// - ↓ 下箭头：减少光源高度
// - ← 左箭头：向左移动光源
// - → 右箭头：向右移动光源
// ============================================================================
void Renderer::HandleKeyboardInput(float deltaTime, bool keyUp, bool keyLeft, bool keyDown, bool keyRight)
{
    // 光源移动速度（单位：米/秒）
    float moveSpeed = 50.0f;

    // 计算移动距离
    float moveDistance = moveSpeed * deltaTime;

    // 保存原始位置，用于调试
    XMFLOAT3 oldPos = m_lightPosition;

    // 根据方向键更新光源位置
    if (keyUp) m_lightPosition.y += moveDistance;    // 上箭头：向上移动（增加高度）
    if (keyDown) m_lightPosition.y -= moveDistance;  // 下箭头：向下移动（减少高度）
    if (keyLeft) m_lightPosition.x -= moveDistance;  // 左箭头：向左移动（X轴负方向）
    if (keyRight) m_lightPosition.x += moveDistance; // 右箭头：向右移动（X轴正方向）

    // 限制光源高度在合理范围内
    m_lightPosition.y = std::max(10.0f, std::min(300.0f, m_lightPosition.y));
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
        // 应用0.2倍缩放，使模型放大10倍（原来的一半）
        XMMATRIX scale = XMMatrixScaling(0.2f, 0.2f, 0.2f);
        // 旋转模型摆正：从头顶看向脚底 -> 正常视角
        // 绕X轴旋转-90度（顺时针90度），让模型从躺着的状态变成站着的状态
        XMMATRIX rotationX = XMMatrixRotationX(-XM_PI / 2.0f);  // -90度 = -π/2弧度
        // 绕Y轴旋转180度，让角色朝向相反方向
        XMMATRIX rotationY = XMMatrixRotationY(XM_PI);  // 180度 = π弧度
        XMMATRIX rotation = rotationX * rotationY;  // 先X轴旋转，再Y轴旋转
        // 使用角色位置来设置模型位置
        // 获取角色位置（从相机获取，角色位置在地面上）
        XMFLOAT3 charPos = GetCharacterPosition();
        
        // 模型中心在y=0，所以需要根据角色位置调整
        // 角色位置是脚部位置，模型需要放在地面上
        XMMATRIX translation = XMMatrixTranslation(charPos.x, charPos.y, charPos.z);
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
        // 光源高度保持一定（从上方向下照射），旋转速度：每15秒转一圈
        float rotationSpeed = 2.0f * XM_PI / 15.0f;  // 每15秒转一圈（2π弧度），缓慢旋转
        float angle = m_lightRotationTime * rotationSpeed;
        
        // 使用可控制的光源位置（不再跟随相机）
        XMVECTOR lightPos = XMVectorSet(
            m_lightPosition.x,
            m_lightPosition.y,
            m_lightPosition.z,
            1.0f
        );
        
        // 存储光源位置（用于后续计算）
        XMFLOAT3 lightPosFloat;
        XMStoreFloat3(&lightPosFloat, lightPos);
        float lightX = lightPosFloat.x;
        float lightY = lightPosFloat.y;
        float lightZ = lightPosFloat.z;
        
        // 光源方向目标位置（与shadowmap一致，使用世界原点）
        // 注意：必须与RenderShadowMap中使用相同的目标位置，否则shadowmap和光照计算会不一致
        XMVECTOR targetPosForLight = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
        
        // 光源方向（从表面指向光源的方向，归一化）
        // 光照计算需要从表面指向光源的方向（用于计算N·L点积）
        // 光源在上面时，方向是向上的（Y正），这样才能正确照亮表面
        XMVECTOR lightDir = XMVector3Normalize(XMVectorSubtract(lightPos, targetPosForLight));
        
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
        
        // ========================================================================
        // Shadow Map相关矩阵（光源视图和投影矩阵）
        // ========================================================================
        // 使用动态光源位置
        XMVECTOR lightPosForShadow = XMVectorSet(m_lightPosition.x, m_lightPosition.y, m_lightPosition.z, 1.0f);
        XMVECTOR targetPos = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f); // 看向世界原点
        
        // 光源向上方向（Y轴正方向）
        XMVECTOR lightUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        
        // 创建光源视图矩阵（从光源位置看向角色位置）
        XMMATRIX lightViewMatrix = XMMatrixLookAtLH(lightPosForShadow, targetPos, lightUp);
        
        // 创建光源投影矩阵（正交投影）
        // 对于角色阴影，使用适当的投影范围以增加精度
        // shadowMapSize需要平衡：太小则角色占比大但覆盖范围小，太大则覆盖范围大但角色占比小
        // 注意：这里的值必须与RenderShadowMap()中的值完全一致
        float shadowMapSize = 50.0f;   // 缩小范围以让角色在阴影贴图中更大
        float nearPlane = 0.1f;       // 近裁剪平面（靠近光源）
        float farPlane = 300.0f;      // 远平面（从200米高到地形）
        XMMATRIX lightProjectionMatrix = XMMatrixOrthographicLH(shadowMapSize, shadowMapSize, nearPlane, farPlane);
        
        // 对于地形，world矩阵是单位矩阵，所以lightWorldViewProj = lightView * lightProjection
        XMMATRIX lightWorldViewProjMatrix = lightViewMatrix * lightProjectionMatrix;
        
        // 转置矩阵并存储（HLSL使用列主序）
        XMStoreFloat4x4(&lb->lightView, XMMatrixTranspose(lightViewMatrix));
        XMStoreFloat4x4(&lb->lightProjection, XMMatrixTranspose(lightProjectionMatrix));
        XMStoreFloat4x4(&lb->lightWorldViewProj, XMMatrixTranspose(lightWorldViewProjMatrix));

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
    // 编译地形顶点着色器（使用TerrainNew版本）
    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    if (!CompileShaderFromFile(L"Shaders/TerrainNewVertexShader.hlsl", "VS", "vs_5_0", vsBlob.GetAddressOf()))
    {
        // 如果TerrainNew版本不存在，尝试使用标准版本
        if (!CompileShaderFromFile(L"Shaders/VertexShader.hlsl", "VS", "vs_5_0", vsBlob.GetAddressOf()))
        {
            m_lastError = L"Failed to compile TerrainNewVertexShader.hlsl or VertexShader.hlsl (VS).";
            OutputDebugStringW(L"[TERRAIN DEBUG] Failed to compile terrain vertex shader\n");
            return false;
        }
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
    
    // 编译线框像素着色器（用于叠加黑色线框）
    Microsoft::WRL::ComPtr<ID3DBlob> wireframePsBlob;
    if (CompileShaderFromFile(L"Shaders/TerrainWireframePS.hlsl", "PS", "ps_5_0", wireframePsBlob.GetAddressOf()))
    {
        hr = m_device->CreatePixelShader(
            wireframePsBlob->GetBufferPointer(),
            wireframePsBlob->GetBufferSize(),
            nullptr,
            m_terrainWireframePS.GetAddressOf()
        );
        if (SUCCEEDED(hr))
        {
            OutputDebugStringW(L"[TERRAIN DEBUG] Terrain wireframe pixel shader created successfully.\n");
        }
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
    // 创建线框光栅化状态（用于叠加渲染）
    D3D11_RASTERIZER_DESC wireframeDesc = {};
    wireframeDesc.FillMode = D3D11_FILL_WIREFRAME;  // 线框模式
    wireframeDesc.CullMode = D3D11_CULL_BACK;       // 背面剔除
    wireframeDesc.FrontCounterClockwise = false;    // 逆时针为正面
    wireframeDesc.DepthBias = -100;                 // 深度偏移，让线框稍微靠前避免z-fighting
    wireframeDesc.DepthBiasClamp = 0.0f;
    wireframeDesc.SlopeScaledDepthBias = -1.0f;     // 斜率缩放偏移
    wireframeDesc.DepthClipEnable = true;
    wireframeDesc.ScissorEnable = false;
    wireframeDesc.MultisampleEnable = false;
    wireframeDesc.AntialiasedLineEnable = true;     // 开启线条抗锯齿
    
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
// 创建光源可视化椎体几何体
// ============================================================================
bool Renderer::CreateLightVisualizationGeometry()
{
    // 创建椎体的顶点（局部空间，底面在原点，顶点向下，大小为1x1x1）
    struct LightConeVertex
    {
        float x, y, z;
    };

    // 创建椎体：底面圆（8边形），顶点向下
    const int sides = 8;  // 8边形底面
    const float radius = 0.5f;
    const float height = 1.0f;

    std::vector<LightConeVertex> vertices;
    vertices.reserve(sides + 1);

    // 底面顶点（在Y=0平面）
    for (int i = 0; i < sides; ++i)
    {
        float angle = (float)i / sides * XM_2PI;
        vertices.push_back({ radius * cosf(angle), 0.0f, radius * sinf(angle) });
    }

    // 椎体顶点（在Y=-height位置，向下）
    vertices.push_back({ 0.0f, -height, 0.0f });

    // 创建索引
    std::vector<uint32_t> indices;
    indices.reserve(sides * 6);  // 底面 + 侧面

    // 底面三角形（扇形）
    int apexIndex = sides;
    for (int i = 0; i < sides; ++i)
    {
        int next = (i + 1) % sides;
        indices.push_back(0);
        indices.push_back(i);
        indices.push_back(next);
    }

    // 侧面三角形（从底面到顶点）
    for (int i = 0; i < sides; ++i)
    {
        int next = (i + 1) % sides;
        indices.push_back(i);
        indices.push_back(next);
        indices.push_back(apexIndex);
    }

    // 创建顶点缓冲区
    D3D11_BUFFER_DESC vbd = {};
    vbd.Usage = D3D11_USAGE_DEFAULT;
    vbd.ByteWidth = sizeof(LightConeVertex) * vertices.size();
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbd.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA vinitData = {};
    vinitData.pSysMem = vertices.data();

    HRESULT hr = m_device->CreateBuffer(&vbd, &vinitData, m_lightCubeVertexBuffer.GetAddressOf());
    if (FAILED(hr))
        return false;

    // 创建索引缓冲区
    D3D11_BUFFER_DESC ibd = {};
    ibd.Usage = D3D11_USAGE_DEFAULT;
    ibd.ByteWidth = sizeof(uint32_t) * indices.size();
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    ibd.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA iinitData = {};
    iinitData.pSysMem = indices.data();

    hr = m_device->CreateBuffer(&ibd, &iinitData, m_lightCubeIndexBuffer.GetAddressOf());
    if (FAILED(hr))
        return false;

    // 保存索引数量用于渲染
    m_lightConeIndexCount = indices.size();
    return true;
}

// ============================================================================
// 创建光源可视化椎体着色器
// ============================================================================
bool Renderer::CreateLightVisualizationShaders()
{
    // 顶点着色器
    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    if (!CompileShaderFromFile(L"Shaders\\VertexShader.hlsl", "VS", "vs_5_0", vsBlob.GetAddressOf()))
        return false;

    HRESULT hr = m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_lightCubeVS.GetAddressOf());
    if (FAILED(hr))
        return false;

    // 创建输入布局（与天空盒使用相同的布局）
    if (!m_skyboxInputLayout)
    {
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };

        hr = m_device->CreateInputLayout(layout, ARRAYSIZE(layout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_skyboxInputLayout.GetAddressOf());
        if (FAILED(hr))
            return false;
    }

    // 像素着色器 - 使用简单的纯色着色器
    const char* psCode = R"(
        float4 PS(float4 position : SV_POSITION) : SV_TARGET
        {
            return float4(1.0, 1.0, 0.0, 1.0); // 亮黄色椎体
        }
    )";

    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    hr = D3DCompile(psCode, strlen(psCode), nullptr, nullptr, nullptr, "PS", "ps_5_0", 0, 0, psBlob.GetAddressOf(), nullptr);
    if (FAILED(hr))
        return false;

    hr = m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_lightCubePS.GetAddressOf());
    return SUCCEEDED(hr);
}


// ============================================================================
// 渲染光源可视化椎体
// ============================================================================
void Renderer::RenderLightVisualization(const DirectX::XMFLOAT3& lightPos)
{
    // 检查资源是否已创建
    if (!m_lightCubeVertexBuffer || !m_lightCubeIndexBuffer || !m_lightCubeVS || !m_lightCubePS || !m_skyboxInputLayout)
        return;

    // 保存当前渲染状态
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> oldRasterizerState;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> oldDepthStencilState;
    UINT oldStencilRef;
    m_context->RSGetState(oldRasterizerState.GetAddressOf());
    m_context->OMGetDepthStencilState(oldDepthStencilState.GetAddressOf(), &oldStencilRef);

    // 设置渲染状态：正常填充，无背面剔除
    m_context->RSSetState(nullptr);

    // 设置深度状态：正常深度测试和写入
    m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);

    // 设置着色器
    m_context->IASetInputLayout(m_skyboxInputLayout.Get());
    m_context->VSSetShader(m_lightCubeVS.Get(), nullptr, 0);
    m_context->PSSetShader(m_lightCubePS.Get(), nullptr, 0);

    // 设置顶点和索引缓冲区
    UINT stride = sizeof(float) * 3; // 只有位置数据
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_lightCubeVertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->IASetIndexBuffer(m_lightCubeIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // 创建变换矩阵 - 放大立方体并定位到光源位置
    XMMATRIX scale = XMMatrixScaling(10.0f, 10.0f, 10.0f); // 放大10倍
    XMMATRIX translation = XMMatrixTranslation(lightPos.x, lightPos.y, lightPos.z);
    XMMATRIX world = scale * translation;

    // 获取视图和投影矩阵
    XMMATRIX viewMatrix = m_camera ? m_camera->GetViewMatrix() : XMMatrixIdentity();
    float aspectRatio = m_camera ? (float)m_width / (float)m_height : 1.0f;
    XMMATRIX projMatrix = m_camera ? m_camera->GetProjectionMatrix(aspectRatio) : XMMatrixIdentity();
    XMMATRIX worldViewProj = world * viewMatrix * projMatrix;

    // 更新常量缓冲区
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = m_context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr))
    {
        ConstantBuffer* cb = (ConstantBuffer*)mapped.pData;

        XMMATRIX identity = XMMatrixIdentity();
        XMStoreFloat4x4(&cb->world, XMMatrixTranspose(identity));
        XMStoreFloat4x4(&cb->view, XMMatrixTranspose(viewMatrix));
        XMStoreFloat4x4(&cb->projection, XMMatrixTranspose(projMatrix));
        XMStoreFloat4x4(&cb->worldViewProj, XMMatrixTranspose(worldViewProj));

        m_context->Unmap(m_constantBuffer.Get(), 0);
    }

    // 绑定常量缓冲区
    m_context->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());

    // 渲染椎体
    m_context->DrawIndexed(m_lightConeIndexCount, 0, 0);

    // 恢复之前的渲染状态
    m_context->RSSetState(oldRasterizerState.Get());
    m_context->OMSetDepthStencilState(oldDepthStencilState.Get(), oldStencilRef);
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
// 初始化地形 (使用 TerrainNew - 基础网格生成)
// ============================================================================
bool Renderer::InitializeTerrain()
{
    // 创建地形对象
    m_terrain = new TerrainNew();
    
    // ========================================================================
    // 设置地形参数
    // ========================================================================
    TerrainNewParams params;
    params.gridWidth = 256;              // 网格宽度（顶点数）
    params.gridHeight = 256;             // 网格高度（顶点数）
    params.worldSizeX = 1024.0f;         // 世界空间X方向大小（1公里）
    params.worldSizeZ = 1024.0f;         // 世界空间Z方向大小（1公里）
    params.heightScale = 100.0f;         // 高度缩放因子（100米高差）
    params.heightOffset = 0.0f;          // 高度偏移量
    
    // Chunk和LOD参数
    params.chunkSize = 64;               // 每个chunk的网格大小（顶点数-1）
    params.maxLODLevels = 4;             // 最大LOD级别数
    params.lodDistances[0] = 100.0f;     // LOD 0: 100米内（最高细节）
    params.lodDistances[1] = 250.0f;     // LOD 1: 250米内
    params.lodDistances[2] = 600.0f;     // LOD 2: 600米内
    params.lodDistances[3] = 1500.0f;    // LOD 3: 1500米内（最低细节）
    params.morphStartRatio = 0.66f;       // Morphing在距离阈值的30%处开始（70%的范围用于过渡）
    
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
            
            // 尝试加载法线图文件
            std::vector<std::wstring> normalmapPaths = {
                projectRoot + L"Res/normalmap.png",
                projectRoot + L"Res/normalmap.jpg",
                projectRoot + L"Res/normalmap.bmp",
                exeDir + L"Res/normalmap.png",
                exeDir + L"Res/normalmap.jpg"
            };
            
            std::wstring foundNormalmapPath;
            for (const auto& path : normalmapPaths)
            {
                // 检查文件是否存在（简单检查）
                HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hFile != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(hFile);
                    foundNormalmapPath = path;
                    OutputDebugStringW(L"[TerrainNew] Found normalmap: ");
                    OutputDebugStringW(path.c_str());
                    OutputDebugStringW(L"\n");
                    break;
                }
            }
            
            // 设置法线图路径
            if (!foundNormalmapPath.empty())
            {
                params.normalmapPath = foundNormalmapPath;
            }
            
            for (const auto& path : heightmapPaths)
            {
                if (m_terrain->CreateFromHeightmap(m_device.Get(), path, params))
                {
                    OutputDebugStringW(L"[TerrainNew] Terrain loaded from heightmap: ");
                    OutputDebugStringW(path.c_str());
                    OutputDebugStringW(L"\n");
                    return true;
                }
            }
        }
    }
    
    // 如果加载高度图失败，使用程序化生成（随机算法）
    OutputDebugStringW(L"[TerrainNew] Heightmap not found, using procedural terrain generation.\n");
    if (m_terrain->CreateProcedural(m_device.Get(), params))
    {
        OutputDebugStringW(L"[TerrainNew] Procedural terrain created successfully.\n");
        
        wchar_t msg[512];
        swprintf_s(msg, L"[TerrainNew] Terrain initialized: %dx%d grid, worldSize=%.0fx%.0f, heightScale=%.0f\n", 
                   params.gridWidth, params.gridHeight, params.worldSizeX, params.worldSizeZ, params.heightScale);
        OutputDebugStringW(msg);
        
        return true;
    }
    
    // 如果都失败，清理并返回false
    delete m_terrain;
    m_terrain = nullptr;
    return false;
}

// ============================================================================
// 渲染地形 (TerrainNew - 基础网格渲染)
// ============================================================================
void Renderer::RenderTerrain()
{
    if (!m_terrain)
    {
        static bool warned = false;
        if (!warned)
        {
            OutputDebugStringW(L"[TerrainNew] RenderTerrain: Terrain is null!\n");
            warned = true;
        }
        return;
    }
    
    // 检查shader资源（TerrainNew使用chunk系统，不需要全局vertex buffer）
    if (!m_terrainVS || !m_terrainPS || !m_terrainInputLayout)
    {
        static bool warned = false;
        if (!warned)
        {
            OutputDebugStringW(L"[TerrainNew] Terrain shaders not created!\n");
            warned = true;
        }
        return;
    }
    
    // 保存当前光栅化状态
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> oldRasterizerState;
    m_context->RSGetState(oldRasterizerState.GetAddressOf());
    
    // 先使用正常填充模式渲染（始终执行）
    m_context->RSSetState(nullptr);
    
    // 设置地形shader和输入布局
    m_context->IASetInputLayout(m_terrainInputLayout.Get());
    m_context->VSSetShader(m_terrainVS.Get(), nullptr, 0);
    m_context->PSSetShader(m_terrainPS.Get(), nullptr, 0);
    
    // 绑定常量缓冲区
    m_context->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    m_context->PSSetConstantBuffers(1, 1, m_lightBuffer.GetAddressOf());
    
    // 获取相机矩阵
    XMMATRIX viewMatrix;
    XMMATRIX projMatrix;
    XMFLOAT3 camPos(0, 100, 0);
    
    if (m_camera)
    {
        viewMatrix = m_camera->GetViewMatrix();
        float aspect = (float)m_width / (float)m_height;
        projMatrix = m_camera->GetProjectionMatrix(aspect);
        camPos = m_camera->GetPosition();
    }
    else
    {
        XMVECTOR eye = XMVectorSet(0.0f, 100.0f, 100.0f, 0.0f);
        XMVECTOR at = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
        XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        viewMatrix = XMMatrixLookAtLH(eye, at, up);
        float aspect = (float)m_width / (float)m_height;
        projMatrix = XMMatrixPerspectiveFovLH(XM_PI / 4.0f, aspect, 0.1f, 1000.0f);
    }
    
    // 更新变换常量缓冲区（使用单位world矩阵）
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = m_context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr))
    {
        ConstantBuffer* cb = (ConstantBuffer*)mapped.pData;
        
        XMMATRIX identity = XMMatrixIdentity();
        XMStoreFloat4x4(&cb->world, XMMatrixTranspose(identity));
        XMStoreFloat4x4(&cb->view, XMMatrixTranspose(viewMatrix));
        XMStoreFloat4x4(&cb->projection, XMMatrixTranspose(projMatrix));
        
        XMMATRIX worldViewProj = identity * viewMatrix * projMatrix;
        XMStoreFloat4x4(&cb->worldViewProj, XMMatrixTranspose(worldViewProj));
        
        m_context->Unmap(m_constantBuffer.Get(), 0);
    }
    
    // 绑定采样器
    if (m_samplerState)
    {
        m_context->VSSetSamplers(0, 1, m_samplerState.GetAddressOf());
        m_context->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());
    }
    
    // ========================================================================
    // 绑定Shadow Map资源（用于地形阴影计算）
    // ========================================================================
    if (m_shadowMapSRV && m_shadowMapSamplerState)
    {
        // 绑定Shadow Map纹理到t1（地形shader中shadowMap在register(t1)）
        m_context->PSSetShaderResources(1, 1, m_shadowMapSRV.GetAddressOf());
        // 绑定Shadow Map采样器到s1（地形shader中shadowSampler在register(s1)）
        m_context->PSSetSamplers(1, 1, m_shadowMapSamplerState.GetAddressOf());
    }
    
    // 确保深度测试启用
    m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);
    
    // 调试输出（首次渲染）
    static bool firstRender = true;
    if (firstRender)
    {
        const TerrainNewParams& params = m_terrain->GetParams();
        wchar_t msg[512];
        swprintf_s(msg, L"[TerrainNew] First render - Grid: %dx%d, World: %.0fx%.0f, Height: %.0f, Camera: (%.1f, %.1f, %.1f)\n",
                  params.gridWidth, params.gridHeight, params.worldSizeX, params.worldSizeZ, params.heightScale,
                  camPos.x, camPos.y, camPos.z);
        OutputDebugStringW(msg);
        firstRender = false;
    }
    
    // 渲染地形（尝试使用GPU Driven，如果不支持则回退到CPU Driven）
    // 注意：需要将view和proj矩阵转换为XMFLOAT4X4格式
    XMFLOAT4X4 viewMatrixFloat, projMatrixFloat;
    XMStoreFloat4x4(&viewMatrixFloat, XMMatrixTranspose(viewMatrix));
    XMStoreFloat4x4(&projMatrixFloat, XMMatrixTranspose(projMatrix));
    
    // 尝试使用GPU Driven渲染（内部会检查是否支持，如果不支持会回退到CPU Driven）
    m_terrain->RenderGPUDriven(m_context.Get(), camPos, viewMatrixFloat, projMatrixFloat);
    
    // ========================================================================
    // 如果启用线框模式，叠加渲染黑色线框
    // ========================================================================
    if (m_terrainWireframe && m_terrainWireframeRasterizerState && m_terrainWireframePS)
    {
        // 切换到线框光栅化状态
        m_context->RSSetState(m_terrainWireframeRasterizerState.Get());
        
        // 使用线框像素着色器（纯黑色）
        m_context->PSSetShader(m_terrainWireframePS.Get(), nullptr, 0);
        
        // 添加深度偏移避免z-fighting（让线框稍微靠前）
        // 通过修改深度状态或者在常量缓冲区中添加偏移
        // 这里直接再渲染一遍，依靠线框模式的特性
        
        // 再次渲染地形（线框模式）
        m_terrain->Render(m_context.Get(), camPos);
        
        // 恢复正常像素着色器
        m_context->PSSetShader(m_terrainPS.Get(), nullptr, 0);
    }
    
    // 恢复光栅化状态
    m_context->RSSetState(oldRasterizerState.Get());
}

// ============================================================================
// 切换地形LOD锁定（TerrainNew不支持LOD，保留函数接口但不执行）
// ============================================================================
void Renderer::ToggleTerrainLODLock()
{
    // TerrainNew 不支持LOD系统
    OutputDebugStringW(L"[TerrainNew] LOD system not available in TerrainNew\n");
}

// ============================================================================
// 设置地形LOD锁定级别（TerrainNew不支持LOD，保留函数接口但不执行）
// ============================================================================
void Renderer::SetTerrainLODLockLevel(int level)
{
    // TerrainNew 不支持LOD系统
    OutputDebugStringW(L"[TerrainNew] LOD system not available in TerrainNew\n");
}

// ============================================================================
// 切换地形LOD调试可视化模式
// ============================================================================
void Renderer::ToggleTerrainLODDebug()
{
    if (m_terrain)
    {
        m_terrain->ToggleLODDebug();
        bool enabled = m_terrain->IsLODDebugEnabled();
        wchar_t msg[256];
        swprintf_s(msg, L"[TerrainNew] LOD Debug Visualization: %s\n", enabled ? L"Enabled" : L"Disabled");
        OutputDebugStringW(msg);
    }
}

// ============================================================================
// 切换地形深度调试可视化模式
// ============================================================================
void Renderer::ToggleTerrainDepthDebug()
{
    if (m_terrain)
    {
        m_terrain->ToggleDepthDebug();
        bool enabled = m_terrain->IsDepthDebugEnabled();
        wchar_t msg[256];
        swprintf_s(msg, L"[TerrainNew] Depth Debug Visualization: %s\n", enabled ? L"Enabled" : L"Disabled");
        OutputDebugStringW(msg);
    }
}

// ============================================================================
// 切换地形阴影调试可视化模式
// ============================================================================
void Renderer::ToggleTerrainShadowDebug()
{
    if (m_terrain)
    {
        m_terrain->ToggleShadowDebug();
        bool enabled = m_terrain->IsShadowDebugEnabled();
        wchar_t msg[256];
        swprintf_s(msg, L"[TerrainNew] Shadow Debug Visualization: %s\n", enabled ? L"Enabled" : L"Disabled");
        OutputDebugStringW(msg);
    }
}

// ============================================================================
// 初始化草地系统
// ============================================================================
bool Renderer::InitializeGrassSystem()
{
    // 创建草地系统对象
    m_grassSystem = new GrassSystem();
    
    // 初始化草地系统
    if (!m_grassSystem->Initialize(m_device.Get(), m_context.Get()))
    {
        OutputDebugStringW(L"[GrassSystem] Failed to initialize grass system.\n");
        delete m_grassSystem;
        m_grassSystem = nullptr;
        return false;
    }
    
    // 生成多个草的位置，铺满整个地形
    if (m_terrain)
    {
        // 获取地形大小（从地形参数中获取，默认1024x1024）
        float terrainSizeX = 1024.0f;
        float terrainSizeZ = 1024.0f;
        
        // 创建一个lambda函数来获取地形高度
        auto getHeightFunc = [this](float x, float z) -> float {
            return m_terrain->GetHeightAt(x, z);
        };
        
        // 生成草的位置，每隔1个单位创建一个
        m_grassSystem->GenerateGrassPositions(terrainSizeX, terrainSizeZ, 1.0f, getHeightFunc);
    }
    else
    {
        // 如果没有地形，生成默认位置的草（高度为0）
        m_grassSystem->GenerateGrassPositions(1024.0f, 1024.0f, 1.0f, nullptr);
        OutputDebugStringW(L"[GrassSystem] Warning: No terrain found, generating grass at height 0\n");
    }
    
    OutputDebugStringW(L"[GrassSystem] Grass system initialized successfully.\n");
    return true;
}

// ============================================================================
// 渲染草地系统
// ============================================================================
void Renderer::RenderGrassSystem(float deltaTime)
{
    if (!m_grassSystem || !m_camera)
    {
        return;
    }
    
    // 获取相机的视图和投影矩阵
    XMMATRIX viewMatrix = m_camera->GetViewMatrix();
    float aspect = (float)m_width / (float)m_height;
    XMMATRIX projMatrix = m_camera->GetProjectionMatrix(aspect);
    
    // 转换为XMFLOAT4X4
    XMFLOAT4X4 view, projection;
    XMStoreFloat4x4(&view, viewMatrix);
    XMStoreFloat4x4(&projection, projMatrix);
    
    // 渲染草地（传递deltaTime用于动画）
    m_grassSystem->Render(m_context.Get(), view, projection, deltaTime);
}

// ============================================================================
// 初始化文字渲染系统
// ============================================================================
bool Renderer::InitializeTextRendering()
{
    HRESULT hr;
    
    // 创建DirectWrite工厂
    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf())
    );
    if (FAILED(hr))
    {
        OutputDebugStringW(L"Failed to create DirectWrite factory.\n");
        return false;
    }
    
    // 创建文本格式
    hr = m_dwriteFactory->CreateTextFormat(
        L"Consolas",  // 字体族名
        nullptr,      // 字体集合（nullptr表示系统字体集合）
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        16.0f,        // 字体大小
        L"zh-cn",     // 区域设置
        m_textFormat.GetAddressOf()
    );
    if (FAILED(hr))
    {
        OutputDebugStringW(L"Failed to create text format.\n");
        return false;
    }
    
    // 设置文本对齐方式
    m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    
    // 创建Direct2D工厂（D2D渲染目标在RenderFrame中动态创建）
    D2D1_FACTORY_OPTIONS options = {};
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), &options,
                          reinterpret_cast<void**>(m_d2dFactory.GetAddressOf()));
    if (FAILED(hr))
    {
        OutputDebugStringW(L"Failed to create Direct2D factory.\n");
        return false;
    }
    
    // D2D渲染目标和画刷在RenderFrame中动态创建，这里只初始化工厂
    OutputDebugStringW(L"Text rendering system initialized successfully.\n");
    
    return true;
}

// ============================================================================
// 渲染文字
// ============================================================================
void Renderer::RenderText(const wchar_t* text, float x, float y)
{
    if (!m_d2dRenderTarget || !m_textFormat || !m_textBrush)
        return;
    
    // 开始D2D绘制
    m_d2dRenderTarget->BeginDraw();
    
    // 创建文本布局
    Microsoft::WRL::ComPtr<IDWriteTextLayout> textLayout;
    HRESULT hr = m_dwriteFactory->CreateTextLayout(
        text,
        (UINT32)wcslen(text),
        m_textFormat.Get(),
        (float)m_width,
        (float)m_height,
        textLayout.GetAddressOf()
    );
    
    if (SUCCEEDED(hr) && textLayout)
    {
        // 绘制文字
        m_d2dRenderTarget->DrawTextLayout(
            D2D1::Point2F(x, y),
            textLayout.Get(),
            m_textBrush.Get()
        );
    }
    
    // 结束D2D绘制
    m_d2dRenderTarget->EndDraw();
}

// ============================================================================
// 更新FPS
// ============================================================================
void Renderer::UpdateFPS(float deltaTime)
{
    m_frameCount++;
    m_fpsUpdateTime += deltaTime;
    
    // 每秒更新一次FPS
    if (m_fpsUpdateTime >= 1.0f)
    {
        m_fps = m_frameCount / m_fpsUpdateTime;
        m_frameCount = 0;
        m_fpsUpdateTime = 0.0f;
    }
}

// ============================================================================
// 更新面数统计
// ============================================================================
void Renderer::UpdateTriangleCount()
{
    m_totalTriangles = 0;
    m_terrainTriangles = 0;
    m_meshTriangles = 0;
    
    // 统计地形面数
    if (m_terrain)
    {
        const auto& stats = m_terrain->GetStats();
        // 根据LOD分布估算面数
        // 每个chunk在不同LOD级别的面数：
        // LOD 0: chunkSize * chunkSize * 2 (最高细节)
        // LOD 1: (chunkSize/2) * (chunkSize/2) * 2
        // LOD 2: (chunkSize/4) * (chunkSize/4) * 2
        // LOD 3: (chunkSize/8) * (chunkSize/8) * 2
        const TerrainNewParams& params = m_terrain->GetParams();
        int chunkSize = params.chunkSize;
        
        for (int lod = 0; lod < 8; ++lod)
        {
            int chunkCount = stats.lodDistribution[lod];
            if (chunkCount > 0)
            {
                int gridSize = chunkSize >> lod;  // chunkSize / (2^lod)
                int trianglesPerChunk = gridSize * gridSize * 2;  // 每个chunk有gridSize*gridSize个四边形，每个四边形2个三角形
                m_terrainTriangles += chunkCount * trianglesPerChunk;
            }
        }
    }
    
    // 统计网格面数
    if (m_meshMgr)
    {
        auto modelGPU = m_meshMgr->GetMeshGPU("Model");
        if (!modelGPU)
            modelGPU = m_meshMgr->GetMeshGPU("Triangle");
        
        if (modelGPU)
        {
            // 统计所有子网格的面数
            for (uint32_t i = 0; i < modelGPU->GetSubmeshCount(); ++i)
            {
                const Submesh& submesh = modelGPU->GetSubmesh(i);
                m_meshTriangles += submesh.indexCount / 3;  // 每个三角形3个索引
            }
        }
    }
    
    m_totalTriangles = m_terrainTriangles + m_meshTriangles;
}

// ============================================================================
// 创建Shadow Map资源
// ============================================================================
bool Renderer::CreateShadowMap()
{
    // ========================================================================
    // 步骤 1: 创建Shadow Map纹理
    // Shadow Map是一个深度纹理，用于存储从光源视角看到的场景深度
    // ========================================================================
    D3D11_TEXTURE2D_DESC shadowMapDesc = {};
    shadowMapDesc.Width = SHADOW_MAP_SIZE;              // Shadow map宽度（2048像素）
    shadowMapDesc.Height = SHADOW_MAP_SIZE;             // Shadow map高度（2048像素）
    shadowMapDesc.MipLevels = 1;                        // Mip级别（1表示不使用mipmap）
    shadowMapDesc.ArraySize = 1;                        // 纹理数组大小（1表示单个纹理）
    shadowMapDesc.Format = DXGI_FORMAT_R32_TYPELESS;   // 使用R32格式（32位深度，typeless允许同时作为深度和着色器资源）
    shadowMapDesc.SampleDesc.Count = 1;                 // 多重采样数量（1表示不启用）
    shadowMapDesc.SampleDesc.Quality = 0;               // 多重采样质量
    shadowMapDesc.Usage = D3D11_USAGE_DEFAULT;          // 使用默认用法（GPU读写）
    shadowMapDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;  // 同时作为深度缓冲区和着色器资源
    shadowMapDesc.CPUAccessFlags = 0;                   // CPU访问标志（0表示CPU不访问）
    shadowMapDesc.MiscFlags = 0;                        // 其他标志
    
    HRESULT hr = m_device->CreateTexture2D(&shadowMapDesc, nullptr, m_shadowMapTexture.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"Failed to create shadow map texture.\n");
        return false;
    }
    
    // ========================================================================
    // 步骤 2: 创建Shadow Map深度视图（DSV）
    // DSV用于将纹理绑定为深度缓冲区，用于渲染shadow map
    // ========================================================================
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;             // 深度格式（32位浮点深度）
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;  // 2D纹理视图
    dsvDesc.Texture2D.MipSlice = 0;                     // Mip切片（0表示使用第一个mip级别）
    
    hr = m_device->CreateDepthStencilView(m_shadowMapTexture.Get(), &dsvDesc, m_shadowMapDSV.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"Failed to create shadow map depth stencil view.\n");
        return false;
    }
    
    // ========================================================================
    // 步骤 3: 创建Shadow Map着色器资源视图（SRV）
    // SRV用于在像素着色器中采样shadow map，进行阴影计算
    // ========================================================================
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;             // 着色器资源格式（32位浮点，用于采样深度值）
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;  // 2D纹理视图
    srvDesc.Texture2D.MipLevels = 1;                     // Mip级别数量
    srvDesc.Texture2D.MostDetailedMip = 0;              // 最详细的mip级别
    
    hr = m_device->CreateShaderResourceView(m_shadowMapTexture.Get(), &srvDesc, m_shadowMapSRV.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"Failed to create shadow map shader resource view.\n");
        return false;
    }
    
    // ========================================================================
    // 步骤 4: 创建Shadow Map采样器状态
    // 采样器定义了如何从shadow map中采样深度值
    // 使用比较采样器（Comparison Sampler）进行PCF（Percentage Closer Filtering）
    // ========================================================================
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;  // 比较过滤器（用于PCF）
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;  // U方向边界寻址（超出范围返回边界值）
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;  // V方向边界寻址
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;  // W方向边界寻址
    samplerDesc.MipLODBias = 0.0f;                       // Mip LOD偏移
    samplerDesc.MaxAnisotropy = 1;                       // 最大各向异性（1表示不启用）
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;  // 比较函数（小于等于时通过）
    samplerDesc.BorderColor[0] = 1.0f;                   // 边界颜色（白色，表示无阴影）
    samplerDesc.BorderColor[1] = 1.0f;
    samplerDesc.BorderColor[2] = 1.0f;
    samplerDesc.BorderColor[3] = 1.0f;
    samplerDesc.MinLOD = 0.0f;                          // 最小LOD级别
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;             // 最大LOD级别
    
    hr = m_device->CreateSamplerState(&samplerDesc, m_shadowMapSamplerState.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"Failed to create shadow map sampler state.\n");
        return false;
    }
    
    // Shadow map资源创建成功
    // 注意：目前不进行任何绘制，只是创建了资源
    // 后续可以在RenderFrame中渲染shadow map
    
    return true;
}

// ============================================================================
// 创建Shadow Map Shader
// ============================================================================
bool Renderer::CreateShadowMapShaders()
{
    // ========================================================================
    // 编译Shadow Map顶点着色器
    // ========================================================================
    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    if (!CompileShaderFromFile(L"Shaders/ShadowMapVertexShader.hlsl", "VS", "vs_5_0", vsBlob.GetAddressOf()))
    {
        OutputDebugStringW(L"Failed to compile ShadowMapVertexShader.hlsl (VS).\n");
        return false;
    }
    
    HRESULT hr = m_device->CreateVertexShader(
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        nullptr,
        m_shadowMapVS.GetAddressOf()
    );
    if (FAILED(hr))
    {
        OutputDebugStringW(L"Failed to create shadow map vertex shader.\n");
        return false;
    }
    
    // ========================================================================
    // 编译Shadow Map像素着色器
    // ========================================================================
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    if (!CompileShaderFromFile(L"Shaders/ShadowMapPixelShader.hlsl", "PS", "ps_5_0", psBlob.GetAddressOf()))
    {
        OutputDebugStringW(L"Failed to compile ShadowMapPixelShader.hlsl (PS).\n");
        return false;
    }
    
    hr = m_device->CreatePixelShader(
        psBlob->GetBufferPointer(),
        psBlob->GetBufferSize(),
        nullptr,
        m_shadowMapPS.GetAddressOf()
    );
    if (FAILED(hr))
    {
        OutputDebugStringW(L"Failed to create shadow map pixel shader.\n");
        return false;
    }
    
    // ========================================================================
    // 创建Shadow Map输入布局（与标准shader相同）
    // ========================================================================
    D3D11_INPUT_ELEMENT_DESC shadowMapLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    
    hr = m_device->CreateInputLayout(
        shadowMapLayout,
        ARRAYSIZE(shadowMapLayout),
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        m_shadowMapInputLayout.GetAddressOf()
    );
    if (FAILED(hr))
    {
        OutputDebugStringW(L"Failed to create shadow map input layout.\n");
        return false;
    }
    
    // ========================================================================
    // 创建Shadow Map常量缓冲区
    // ========================================================================
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.ByteWidth = sizeof(ShadowConstantBuffer);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    
    hr = m_device->CreateBuffer(&cbDesc, nullptr, m_shadowMapConstantBuffer.GetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"Failed to create shadow map constant buffer.\n");
        return false;
    }
    
    return true;
}

// ============================================================================
// 渲染Shadow Map（将角色模型绘制到shadow map）
// ============================================================================
void Renderer::RenderShadowMap()
{
    
    // 检查资源是否已创建
    if (!m_shadowMapDSV || !m_shadowMapVS || !m_shadowMapPS || !m_shadowMapInputLayout || !m_shadowMapConstantBuffer)
    {
        static bool resourceWarningLogged = false;
        if (!resourceWarningLogged)
        {
            OutputDebugStringW(L"[ShadowMap] Warning: Shadow map resources not created!\n");
            if (!m_shadowMapDSV) OutputDebugStringW(L"  - m_shadowMapDSV is null\n");
            if (!m_shadowMapVS) OutputDebugStringW(L"  - m_shadowMapVS is null\n");
            if (!m_shadowMapPS) OutputDebugStringW(L"  - m_shadowMapPS is null\n");
            if (!m_shadowMapInputLayout) OutputDebugStringW(L"  - m_shadowMapInputLayout is null\n");
            if (!m_shadowMapConstantBuffer) OutputDebugStringW(L"  - m_shadowMapConstantBuffer is null\n");
            resourceWarningLogged = true;
        }
        return;
    }
    
    // 检查是否有模型需要渲染（地形或模型都可以）
    // 注意：即使没有模型，我们也可以渲染地形
    std::shared_ptr<MeshGPU> modelGPU = nullptr;
    if (m_meshMgr)
    {
        modelGPU = m_meshMgr->GetMeshGPU("Model");
        if (!modelGPU)
        {
            modelGPU = m_meshMgr->GetMeshGPU("Triangle");
        }
    }
    
    // 如果没有地形也没有模型，提前返回
    if (!m_terrain && !modelGPU)
    {
        static bool noObjectsWarningLogged = false;
        if (!noObjectsWarningLogged)
        {
            OutputDebugStringW(L"[ShadowMap] Warning: No terrain or model to render!\n");
            noObjectsWarningLogged = true;
        }
        return;
    }
    
    // ========================================================================
    // 保存当前渲染状态
    // ========================================================================
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> oldRTV;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> oldDSV;
    m_context->OMGetRenderTargets(1, oldRTV.GetAddressOf(), oldDSV.GetAddressOf());
    
    D3D11_VIEWPORT oldViewport;
    UINT numViewports = 1;
    m_context->RSGetViewports(&numViewports, &oldViewport);
    
    // ========================================================================
    // 设置Shadow Map为渲染目标（只使用深度缓冲区，不使用颜色缓冲区）
    // ========================================================================
    m_context->OMSetRenderTargets(0, nullptr, m_shadowMapDSV.Get());
    
    // ========================================================================
    // 设置Shadow Map视口
    // ========================================================================
    D3D11_VIEWPORT shadowViewport = {};
    shadowViewport.Width = (float)SHADOW_MAP_SIZE;
    shadowViewport.Height = (float)SHADOW_MAP_SIZE;
    shadowViewport.MinDepth = 0.0f;
    shadowViewport.MaxDepth = 1.0f;
    shadowViewport.TopLeftX = 0.0f;
    shadowViewport.TopLeftY = 0.0f;
    m_context->RSSetViewports(1, &shadowViewport);
    
    // ========================================================================
    // 清空Shadow Map深度缓冲区
    // ========================================================================
    m_context->ClearDepthStencilView(m_shadowMapDSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    
    
    // ========================================================================
    // 计算光源的视图和投影矩阵
    // 光源跟随相机，确保始终能看到角色
    // 注意：这里必须与UpdateConstantBuffers中的计算完全一致
    // ========================================================================
    // 获取角色位置（不再需要，因为光源固定看向世界原点）
    // XMFLOAT3 charPos = GetCharacterPosition();
    
    // 使用可控制的光源位置，与黄色立方体位置一致
    XMVECTOR lightPos = XMVectorSet(m_lightPosition.x, m_lightPosition.y, m_lightPosition.z, 1.0f);

    // 存储光源位置用于调试输出
    XMFLOAT3 lightPosFloat;
    XMStoreFloat3(&lightPosFloat, lightPos);
    float lightX = lightPosFloat.x;
    float lightY = lightPosFloat.y;
    float lightZ = lightPosFloat.z;

    // 目标位置固定（世界原点）
    XMVECTOR targetPos = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);

    // 光源向上方向（Y轴正方向）
    XMVECTOR lightUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    // 创建光源视图矩阵
    XMMATRIX lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);
    
    // 创建光源投影矩阵（正交投影）
    // 对于角色阴影，使用适当的投影范围以增加精度
    // shadowMapSize需要平衡：太小则角色占比大但覆盖范围小，太大则覆盖范围大但角色占比小
    float shadowMapSize = 50.0f;   // 缩小范围以让角色在阴影贴图中更大（50x50米）
    float nearPlane = 0.1f;       // 近裁剪平面（靠近光源）
    float farPlane = 300.0f;      // 远平面（从200米高到地形）
    XMMATRIX lightProjection = XMMatrixOrthographicLH(shadowMapSize, shadowMapSize, nearPlane, farPlane);
    
    // 组合矩阵（注意：这里先不乘以world，world在更新常量缓冲区时再乘）
    XMMATRIX lightViewProj = lightView * lightProjection;
    
    // 调试信息：输出光源视图和投影参数
    static int shadowMapDebugFrame = 0;
    if (shadowMapDebugFrame % 300 == 0)  // 每5秒输出一次（假设60fps）
    {
        wchar_t debugMsg[512];
        swprintf_s(debugMsg, L"[ShadowMap] Light view params: pos=(%.2f,%.2f,%.2f), target=(%.2f,%.2f,%.2f), size=%.2f, near=%.2f, far=%.2f\n",
                  lightX, lightY, lightZ, 0.0f, 0.0f, 0.0f, shadowMapSize, nearPlane, farPlane);
        OutputDebugStringW(debugMsg);
    }
    shadowMapDebugFrame++;
    
    // ========================================================================
    // 设置Shadow Map Shader和输入布局
    // 注意：对于shadow map，使用null pixel shader，只输出深度值
    // ========================================================================
    m_context->IASetInputLayout(m_shadowMapInputLayout.Get());
    m_context->VSSetShader(m_shadowMapVS.Get(), nullptr, 0);
    m_context->PSSetShader(nullptr, nullptr, 0);  // 使用null pixel shader，只使用深度缓冲区
    
    // 绑定Shadow Map常量缓冲区
    m_context->VSSetConstantBuffers(0, 1, m_shadowMapConstantBuffer.GetAddressOf());
    
    // ========================================================================
    // 设置光栅化状态（确保正确渲染）
    // ========================================================================
    // 保存当前光栅化状态
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> oldRasterizerState;
    m_context->RSGetState(oldRasterizerState.GetAddressOf());
    
    // 创建shadow map专用的光栅化状态（暂时不使用深度偏移，先确保能绘制）
    static Microsoft::WRL::ComPtr<ID3D11RasterizerState> shadowMapRasterizerState;
    if (!shadowMapRasterizerState)
    {
        D3D11_RASTERIZER_DESC rasterizerDesc = {};
        rasterizerDesc.FillMode = D3D11_FILL_SOLID;                    // 实体填充
        rasterizerDesc.CullMode = D3D11_CULL_BACK;                   // 背面剔除
        rasterizerDesc.FrontCounterClockwise = FALSE;                // 顺时针为正面
        rasterizerDesc.DepthBias = 0;                                // 深度偏移（暂时设为0，先确保能绘制）
        rasterizerDesc.DepthBiasClamp = 0.0f;                        // 深度偏移钳制
        rasterizerDesc.SlopeScaledDepthBias = 0.0f;                  // 斜率缩放深度偏移（暂时设为0）
        rasterizerDesc.DepthClipEnable = TRUE;                        // 启用深度裁剪
        rasterizerDesc.ScissorEnable = FALSE;                         // 不启用裁剪测试
        rasterizerDesc.MultisampleEnable = FALSE;                     // 不启用多重采样
        rasterizerDesc.AntialiasedLineEnable = FALSE;                 // 不启用线抗锯齿
        
        HRESULT hr = m_device->CreateRasterizerState(&rasterizerDesc, shadowMapRasterizerState.GetAddressOf());
        if (FAILED(hr))
        {
            OutputDebugStringW(L"[ShadowMap] Warning: Failed to create rasterizer state, using default.\n");
        }
    }
    
    // 设置光栅化状态
    if (shadowMapRasterizerState)
    {
        m_context->RSSetState(shadowMapRasterizerState.Get());
    }
    else
    {
        // 如果没有创建成功，使用默认状态（nullptr表示使用默认）
        m_context->RSSetState(nullptr);
    }
    
    // ========================================================================
    // 设置深度状态（启用深度写入）
    // 注意：必须在设置光栅化状态之后设置深度状态
    // ========================================================================
    // 确保深度测试和深度写入都启用
    // m_depthStencilState已经配置为：
    // - DepthEnable = TRUE
    // - DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL
    // - DepthFunc = D3D11_COMPARISON_LESS
    m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);
    
    // 调试信息：确认深度状态
    static int depthStateDebugFrame = 0;
    if (depthStateDebugFrame == 0)  // 只在第一帧输出
    {
        OutputDebugStringW(L"[ShadowMap] Depth state: Enabled, WriteMask=ALL, Func=LESS\n");
    }
    depthStateDebugFrame++;
    
    // ========================================================================
    // 渲染角色模型到Shadow Map（暂时不渲染地形，因为地形系统需要不同的shader）
    // ========================================================================
    if (!modelGPU)
    {
        // 如果没有模型，恢复状态并提前返回
        m_context->RSSetState(oldRasterizerState.Get());
        m_context->OMSetRenderTargets(1, oldRTV.GetAddressOf(), oldDSV.Get());
        m_context->RSSetViewports(1, &oldViewport);
        return;
    }
    
    if (modelGPU)
    {
        // 更新Shadow Map常量缓冲区（模型使用变换后的world矩阵）
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = m_context->Map(m_shadowMapConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr))
        {
            ShadowConstantBuffer* cb = (ShadowConstantBuffer*)mapped.pData;
            
            // 获取角色位置（用于模型变换）
            XMFLOAT3 charPos = GetCharacterPosition();
            
            // 获取模型的world矩阵（与正常渲染相同）
            XMMATRIX scale = XMMatrixScaling(0.2f, 0.2f, 0.2f);
            XMMATRIX rotationX = XMMatrixRotationX(-XM_PI / 2.0f);
            XMMATRIX rotationY = XMMatrixRotationY(XM_PI);
            XMMATRIX rotation = rotationX * rotationY;
            XMMATRIX translation = XMMatrixTranslation(charPos.x, charPos.y, charPos.z);
            XMMATRIX world = scale * rotation * translation;
            
            // 计算模型在光源投影空间的位置（用于调试，检查是否在视锥体内）
            XMVECTOR modelCenterWorld = XMVectorSet(charPos.x, charPos.y, charPos.z, 1.0f);
            XMVECTOR modelCenterLightProj = XMVector4Transform(modelCenterWorld, world * lightViewProj);
            XMFLOAT4 modelCenterLightProjFloat;
            XMStoreFloat4(&modelCenterLightProjFloat, modelCenterLightProj);
            
            // 对于正交投影，w分量应该接近1.0，深度值在[0,1]范围内
            float depthValue = 0.0f;
            if (abs(modelCenterLightProjFloat.w) > 0.0001f)
            {
                depthValue = modelCenterLightProjFloat.z / modelCenterLightProjFloat.w;
            }
            
            // 调试信息：输出模型在光源投影空间的位置
            static int debugFrameCount = 0;
            if (debugFrameCount % 60 == 0)  // 每60帧输出一次
            {
                wchar_t debugMsg[512];
                swprintf_s(debugMsg, L"[ShadowMap] Model center in light proj space: (%.3f, %.3f, %.3f, %.3f), depth=%.3f\n",
                          modelCenterLightProjFloat.x, modelCenterLightProjFloat.y, 
                          modelCenterLightProjFloat.z, modelCenterLightProjFloat.w, depthValue);
                OutputDebugStringW(debugMsg);
                
                // 检查是否在视锥体内（对于正交投影，应该在[-1, 1]范围内）
                bool inFrustum = (modelCenterLightProjFloat.x >= -1.0f && modelCenterLightProjFloat.x <= 1.0f &&
                                 modelCenterLightProjFloat.y >= -1.0f && modelCenterLightProjFloat.y <= 1.0f &&
                                 modelCenterLightProjFloat.z >= 0.0f && modelCenterLightProjFloat.z <= 1.0f);
                
                swprintf_s(debugMsg, L"[ShadowMap] Model in frustum: %s, Light pos: (%.2f, %.2f, %.2f), Char pos: (%.2f, %.2f, %.2f)\n",
                          inFrustum ? L"YES" : L"NO", lightX, lightY, lightZ, charPos.x, charPos.y, charPos.z);
                OutputDebugStringW(debugMsg);
                
                // 输出world矩阵信息
                XMFLOAT4X4 worldMatrix;
                XMStoreFloat4x4(&worldMatrix, world);
                swprintf_s(debugMsg, L"[ShadowMap] World matrix scale: (%.3f, %.3f, %.3f), translation: (%.3f, %.3f, %.3f)\n",
                          worldMatrix._11, worldMatrix._22, worldMatrix._33,
                          worldMatrix._41, worldMatrix._42, worldMatrix._43);
                OutputDebugStringW(debugMsg);
            }
            debugFrameCount++;
            
            // 转置矩阵并存储
            XMStoreFloat4x4(&cb->world, XMMatrixTranspose(world));
            XMStoreFloat4x4(&cb->lightView, XMMatrixTranspose(lightView));
            XMStoreFloat4x4(&cb->lightProjection, XMMatrixTranspose(lightProjection));
            XMStoreFloat4x4(&cb->lightViewProj, XMMatrixTranspose(world * lightViewProj));
            
            m_context->Unmap(m_shadowMapConstantBuffer.Get(), 0);
        }
        else
        {
            // 如果Map失败，输出错误信息
            static bool mapErrorLogged = false;
            if (!mapErrorLogged)
            {
                wchar_t msg[256];
                swprintf_s(msg, L"[ShadowMap] Error: Failed to map constant buffer, HRESULT: 0x%08X\n", hr);
                OutputDebugStringW(msg);
                mapErrorLogged = true;
            }
        }
        
        // 绑定模型并绘制
        modelGPU->Bind(m_context.Get());

        // 调试信息：输出绘制信息
        static int drawCallCount = 0;
        static int debugDrawFrame = 0;
        if (debugDrawFrame % 60 == 0)  // 每60帧输出一次
        {
            wchar_t msg[256];
            swprintf_s(msg, L"[ShadowMap] Drawing to shadowmap: submeshCount=%d, drawCall=%d\n", 
                      modelGPU->GetSubmeshCount(), drawCallCount);
            OutputDebugStringW(msg);
        }
        debugDrawFrame++;

        // 检查是否有子网格
        if (modelGPU->GetSubmeshCount() > 0)
        {
            // 绘制所有子网格
            for (uint32_t i = 0; i < modelGPU->GetSubmeshCount(); ++i)
            {
                // 在绘制前确保所有状态都正确设置
                // 重新设置深度状态（确保深度写入启用）
                m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);

                modelGPU->DrawSubmesh(m_context.Get(), i);
                drawCallCount++;
            }
        }
        else
        {
            // 没有子网格，使用传统方式绘制
            // 在绘制前确保所有状态都正确设置
            m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);

            modelGPU->Draw(m_context.Get());
            drawCallCount++;
        }
        
    }
    
    // ========================================================================
    // 恢复光栅化状态
    // ========================================================================
    m_context->RSSetState(oldRasterizerState.Get());
    
    // ========================================================================
    // 恢复之前的渲染状态
    // ========================================================================
    m_context->OMSetRenderTargets(1, oldRTV.GetAddressOf(), oldDSV.Get());
    m_context->RSSetViewports(1, &oldViewport);
}

// ============================================================================
// 切换相机到光源视角（用于调试shadow map）
// ============================================================================
void Renderer::SwitchCameraToLightView()
{
    if (!m_camera)
    {
        OutputDebugStringW(L"[Renderer] Warning: Cannot switch camera to light view: camera is null.\n");
        return;
    }
    
    // 光源位置
    XMFLOAT3 lightPos = m_lightPosition;
    
    // 光源目标位置（世界原点，与shadow map计算一致）
    XMFLOAT3 targetPos(0.0f, 0.0f, 0.0f);
    
    // 设置相机到光源位置和方向
    m_camera->SetPositionAndLookAt(lightPos, targetPos);
    
    // 输出调试信息
    wchar_t msg[256];
    swprintf_s(msg, L"[Renderer] Camera switched to light view. Position: (%.2f, %.2f, %.2f), Target: (%.2f, %.2f, %.2f)\n",
               lightPos.x, lightPos.y, lightPos.z, targetPos.x, targetPos.y, targetPos.z);
    OutputDebugStringW(msg);
}
