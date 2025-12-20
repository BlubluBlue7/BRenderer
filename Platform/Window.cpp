#include "Window.h"

static const wchar_t* kWindowClassName = L"MinimalWindowClass";

bool Window::Create(int width, int height, const wchar_t* title)
{
    m_width = width;
    m_height = height;

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = Window::WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = kWindowClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

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

    if (!window)
        return DefWindowProc(hWnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_CLOSE:
        window->m_shouldClose = true;
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_SIZE:
        // 窗口大小改变
        window->m_width = LOWORD(lParam);
        window->m_height = HIWORD(lParam);
        return 0;

    case WM_LBUTTONDOWN:
        // 左键按下：开始捕获鼠标
        SetCapture(hWnd);
        window->m_captureMouse = true;
        window->m_lastMouseX = LOWORD(lParam);
        window->m_lastMouseY = HIWORD(lParam);
        return 0;

    case WM_LBUTTONUP:
        // 左键释放：停止捕获鼠标
        ReleaseCapture();
        window->m_captureMouse = false;
        return 0;

    case WM_MOUSEMOVE:
        // 鼠标移动
        if (window->m_captureMouse && window->m_mouseMoveCallback)
        {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            int deltaX = x - window->m_lastMouseX;
            int deltaY = y - window->m_lastMouseY;
            window->m_lastMouseX = x;
            window->m_lastMouseY = y;
            window->m_mouseMoveCallback(deltaX, deltaY);
        }
        return 0;

    case WM_MOUSEWHEEL:
        // 鼠标滚轮
        if (window->m_mouseWheelCallback)
        {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            window->m_mouseWheelCallback(delta);
        }
        return 0;

    case WM_KEYDOWN:
    case WM_KEYUP:
        // 键盘按键
        if (window->m_keyCallback)
        {
            bool pressed = (msg == WM_KEYDOWN);
            window->m_keyCallback((int)wParam, pressed);
        }
        return 0;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}
