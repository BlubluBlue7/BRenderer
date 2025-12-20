#include "Platform/Window.h"
#include "Renderer.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    Window window;
    if (!window.Create(1280, 720, L"DX11 Renderer"))
        return -1;

    Renderer renderer;
    if (!renderer.Initialize(window.GetHWND(), 1280, 720))
        return -1;

    while (!window.ShouldClose())
    {
        window.PollEvents();
        renderer.RenderFrame();
    }

    renderer.Cleanup();
    return 0;
}
