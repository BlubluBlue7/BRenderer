#pragma once

// 在包含 Windows.h 之前定义 NOMINMAX，避免 min/max 宏冲突
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <functional>

// 鼠标事件回调函数类型
using MouseMoveCallback = std::function<void(int deltaX, int deltaY)>;
using MouseWheelCallback = std::function<void(int delta)>;
using KeyCallback = std::function<void(int key, bool pressed)>;

class Window
{
public:
    bool Create(int width, int height, const wchar_t* title);
    void PollEvents();
    bool ShouldClose() const;

    HWND GetHWND() const { return m_hwnd; }
    
    // 获取窗口尺寸
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    
    // 设置回调函数
    void SetMouseMoveCallback(MouseMoveCallback callback) { m_mouseMoveCallback = callback; }
    void SetMouseWheelCallback(MouseWheelCallback callback) { m_mouseWheelCallback = callback; }
    void SetKeyCallback(KeyCallback callback) { m_keyCallback = callback; }

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
    bool m_shouldClose = false;
    int m_width = 0;
    int m_height = 0;
    
    // 鼠标状态
    bool m_captureMouse = false;
    int m_lastMouseX = 0;
    int m_lastMouseY = 0;
    
    // 回调函数
    MouseMoveCallback m_mouseMoveCallback;
    MouseWheelCallback m_mouseWheelCallback;
    KeyCallback m_keyCallback;
};
