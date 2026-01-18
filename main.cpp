#include "Platform/Window.h"
#include "Renderer.h"
#include "Camera.h"
#include <windows.h>
#include <chrono>
#include <algorithm>
#include <string>
#include <fstream>
#include <objbase.h>  // COM初始化

#pragma comment(lib, "ole32.lib")  // CoInitialize/CoUninitialize

// ============================================================================
// 辅助函数：同时输出到 OutputDebugStringW 和日志文件
// 在 Cursor 环境中，可以查看日志文件来查看调试信息
// ============================================================================
static std::wofstream g_logFile;
static bool g_logFileInitialized = false;

void InitLogFile()
{
    if (!g_logFileInitialized)
    {
        wchar_t exePath[MAX_PATH] = { 0 };
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring logPath = exePath;
        size_t lastSlash = logPath.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos)
        {
            logPath = logPath.substr(0, lastSlash + 1) + L"BRenderer.log";
        }
        else
        {
            logPath = L"BRenderer.log";
        }
        
        g_logFile.open(logPath, std::ios::out | std::ios::app);
        g_logFileInitialized = true;
        
        // 写入启动标记
        if (g_logFile.is_open())
        {
            g_logFile << L"=== Application Started ===\n";
            g_logFile.flush();
        }
    }
}

void DebugLog(const wchar_t* message)
{
    // 输出到 OutputDebugStringW（供 DebugView 或 Visual Studio 查看）
    OutputDebugStringW(message);
    
    // 同时写入日志文件（供 Cursor 查看）
    InitLogFile();
    if (g_logFile.is_open())
    {
        g_logFile << message;
        g_logFile.flush();  // 立即刷新，确保日志实时写入
    }
}

// ============================================================================
// 程序入口点
// ============================================================================
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    // 设置 DLL 搜索路径，确保能找到 vcpkg 的 DLL
    // 这必须在任何 DLL 加载之前调用
    wchar_t exePath[MAX_PATH] = { 0 };
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0)
    {
        std::wstring exeDir = exePath;
        size_t lastSlash = exeDir.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos)
        {
            exeDir = exeDir.substr(0, lastSlash + 1);
            // 将 exe 目录添加到 DLL 搜索路径
            SetDllDirectoryW(exeDir.c_str());
            DebugLog((L"Set DLL directory to: " + exeDir + L"\n").c_str());
        }
    }
    
    // 初始化COM（WIC纹理加载需要）
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
    {
        MessageBoxW(nullptr, L"Failed to initialize COM!", L"Error", MB_OK | MB_ICONERROR);
        return -1;
    }
    // ========================================================================
    // 步骤 1: 创建窗口
    // ========================================================================
    Window window;
    if (!window.Create(1280, 720, L"DX11 Renderer - Model Viewer"))
    {
        MessageBoxW(nullptr, L"Failed to create window!", L"Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    // ========================================================================
    // 步骤 2: 初始化渲染器
    // 创建 D3D11 设备、交换链、Shader 等资源
    // ========================================================================
    Renderer renderer;
    bool initResult = renderer.Initialize(window.GetHWND(), 1280, 720);
    if (!initResult)
    {
        // ====================================================================
        // 调试断点位置：在此处设置断点以调试初始化失败
        // ====================================================================
        const wchar_t* errorMsg = renderer.GetLastError();
        std::wstring fullErrorMsg = L"Failed to initialize renderer!\n\n";
        if (errorMsg && wcslen(errorMsg) > 0)
        {
            fullErrorMsg += errorMsg;
        
        }
        else
        {
            fullErrorMsg += L"Please check if DirectX 11 is available and Shader files exist.";
        }
        
        // 调试输出（同时输出到 OutputDebugStringW 和日志文件）
        DebugLog(L"=== Renderer Initialization Failed ===\n");
        DebugLog(fullErrorMsg.c_str());
        DebugLog(L"\n=====================================\n");
        
        MessageBoxW(nullptr, fullErrorMsg.c_str(), L"Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    // ========================================================================
    // 步骤 3: 创建相机
    // ========================================================================
    Camera camera;
    renderer.SetCamera(&camera);
    
    // 将地形对象传递给相机，以便相机能够查询高度
    // 注意：地形在 Initialize 中创建，所以需要在地形初始化之后设置
    if (renderer.GetTerrain())
    {
        camera.SetCharacterHeight(1.7f);  // 设置角色高度为1.7米（眼睛高度）
        camera.SetFollowTerrain(true);    // 启用地形跟随
        camera.SetTerrain(renderer.GetTerrain());  // 设置地形引用（会自动初始化高度）
        
        // 相机位置已通过SetTerrain自动初始化到地形高度 + 角色高度
    }

    // ========================================================================
    // 步骤 4: 设置窗口输入回调
    // ========================================================================
    // 鼠标移动回调：控制相机旋转
    window.SetMouseMoveCallback([&camera](int deltaX, int deltaY) {
        camera.OnMouseMove(deltaX, deltaY);
    });

    // 鼠标滚轮回调：调整移动速度
    window.SetMouseWheelCallback([&camera](int delta) {
        camera.OnMouseWheel(delta);
    });

    // 键盘回调：WASD 移动相机，方向键移动光源，P键暂停/继续光源旋转
    // 方向键状态结构体
    struct KeyStates {
        bool up = false;
        bool down = false;
        bool left = false;
        bool right = false;
    } keyStates;

    window.SetKeyCallback([&camera, &renderer, &keyStates](int key, bool pressed) {
        switch (key)
        {
        case 'W':
        case 'w':
            camera.SetMoveForward(pressed);
            break;
        case 'S':
        case 's':
            camera.SetMoveBackward(pressed);
            break;
        case 'A':
        case 'a':
            camera.SetMoveLeft(pressed);
            break;
        case 'D':
        case 'd':
            camera.SetMoveRight(pressed);
            break;
        case VK_UP:
            keyStates.up = pressed;
            break;
        case VK_DOWN:
            keyStates.down = pressed;
            break;
        case VK_LEFT:
            keyStates.left = pressed;
            break;
        case VK_RIGHT:
            keyStates.right = pressed;
            break;
        case VK_SPACE:
            camera.SetMoveUp(pressed);
            break;
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
            camera.SetMoveDown(pressed);
            break;
        case 'T':
        case 't':
            if (pressed)
            {
                renderer.ToggleTerrainWireframe();
            }
            break;
        case 'L':
        case 'l':
            if (pressed)
            {
                renderer.ToggleTerrainLODLock();
            }
            break;
        case 'K':
        case 'k':
            // K键：切换相机到光源视角（用于调试shadow map）
            if (pressed)
            {
                renderer.SwitchCameraToLightView();
            }
            break;
        case 'N':
        case 'n':
            // N键：切换地形LOD调试可视化模式
            if (pressed)
            {
                renderer.ToggleTerrainLODDebug();
            }
            break;
        case 'B':
        case 'b':
            // B键：切换地形深度调试可视化模式
            if (pressed)
            {
                renderer.ToggleTerrainDepthDebug();
            }
            break;
        case 'H':
        case 'h':
            // H键：切换地形阴影调试模式
            if (pressed)
            {
                renderer.ToggleTerrainShadowDebug();
            }
            break;
        case '1':
            if (pressed)
            {
                renderer.SetTerrainLODLockLevel(0);
            }
            break;
        case '2':
            if (pressed)
            {
                renderer.SetTerrainLODLockLevel(1);
            }
            break;
        case '3':
            if (pressed)
            {
                renderer.SetTerrainLODLockLevel(2);
            }
            break;
        case '4':
            if (pressed)
            {
                renderer.SetTerrainLODLockLevel(3);
            }
            break;
        case 'P':
        case 'p':
            // P键：切换光源旋转暂停/继续（只在按下时切换一次，避免重复触发）
            if (pressed)
            {
                renderer.ToggleLightRotationPaused();
            }
            break;
        case 'F':
        case 'f':
            // F键：切换自由相机视角（地形跟随/自由飞行）
            if (pressed)
            {
                bool isFollowing = camera.ToggleFollowTerrain();
                // 输出调试信息
                wchar_t msg[256];
                swprintf_s(msg, L"[Camera] Terrain following: %s\n", 
                          isFollowing ? L"ON" : L"OFF");
                OutputDebugStringW(msg);
            }
            break;
        }
    });

    // ========================================================================
    // 步骤 5: 主渲染循环
    // 每帧：处理窗口消息 -> 更新相机 -> 渲染一帧 -> 呈现到屏幕
    // ========================================================================
    auto lastTime = std::chrono::high_resolution_clock::now();
    
    while (!window.ShouldClose())
    {
        // 计算帧时间（deltaTime）
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        
        // 限制 deltaTime 避免卡顿时的巨大跳跃
        if (deltaTime > 0.1f)
            deltaTime = 0.1f;
        
        // 处理窗口消息（鼠标、键盘、窗口关闭等）
        window.PollEvents();
        
        // 更新相机（基于输入和 deltaTime）
        camera.Update(deltaTime);

        // 处理光源控制输入（方向键）
        renderer.HandleKeyboardInput(deltaTime, keyStates.up, keyStates.left, keyStates.down, keyStates.right);

        // 渲染一帧（清屏、绘制模型、交换缓冲区）
        renderer.RenderFrame(deltaTime);
    }

    // ========================================================================
    // 步骤 6: 清理资源
    // 释放所有 D3D11 资源
    // ========================================================================
    renderer.Cleanup();

    // 清理COM
    CoUninitialize();
    
    return 0;
}
