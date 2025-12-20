#include "Window.h"

static const wchar_t* kWindowClassName = L"MinimalWindowClass";

bool Window::Create(int width, int height, const wchar_t* title)
{
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = Window::WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = kWindowClassName;

    RegisterClassEx(&wc);

    RECT rect = { 0, 0, width, height };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    m_hwnd = CreateWindowEx(
        0,
        kWindowClassName,
        title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        wc.hInstance,
        this
    );

    if (!m_hwnd)
        return false;

    ShowWindow(m_hwnd, SW_SHOW);
    return true;
}

void Window::PollEvents()
{
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

bool Window::ShouldClose() const
{
    return m_shouldClose;
}

LRESULT CALLBACK Window::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Window* window = nullptr;

    if (msg == WM_NCCREATE)
    {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        window = static_cast<Window*>(cs->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)window);
    }
    else
    {
        window = reinterpret_cast<Window*>(
            GetWindowLongPtr(hWnd, GWLP_USERDATA)
            );
    }

    switch (msg)
    {
    case WM_CLOSE:
        if (window) window->m_shouldClose = true;
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}
