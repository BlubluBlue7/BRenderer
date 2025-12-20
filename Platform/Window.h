#pragma once
#include <windows.h>

class Window
{
public:
    bool Create(int width, int height, const wchar_t* title);
    void PollEvents();
    bool ShouldClose() const;

    HWND GetHWND() const { return m_hwnd; }

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
    bool m_shouldClose = false;
};
#pragma once
