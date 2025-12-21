#include "Platform/Window.h"
#include "Renderer.h"
#include "Camera.h"
#include <windows.h>
#include <chrono>
#include <algorithm>
#include <string>
#include <objbase.h>  // COM初始化

#pragma comment(lib, "ole32.lib")  // CoInitialize/CoUninitialize

// ============================================================================
// 程序入口点
// ============================================================================
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
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
        
        // 调试输出到Output窗口
        OutputDebugStringW(L"=== Renderer Initialization Failed ===\n");
        OutputDebugStringW(fullErrorMsg.c_str());
        OutputDebugStringW(L"\n=====================================\n");
        
        MessageBoxW(nullptr, fullErrorMsg.c_str(), L"Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    // ========================================================================
    // 步骤 3: 创建相机
    // ========================================================================
    Camera camera;
    renderer.SetCamera(&camera);

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

    // 键盘回调：WASD 移动相机
    window.SetKeyCallback([&camera](int key, bool pressed) {
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
        case VK_SPACE:
            camera.SetMoveUp(pressed);
            break;
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
            camera.SetMoveDown(pressed);
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
