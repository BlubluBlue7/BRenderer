#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include "MeshMgr.h"

class Renderer
{
public:
    bool Initialize(HWND hwnd, int width, int height);
    void RenderFrame();
    void Cleanup();

private:
    bool CompileShader(const char* shaderCode, const char* entryPoint, const char* target, ID3DBlob** blob);
    bool CreateShaders();
    bool CreateInputLayout();

    Microsoft::WRL::ComPtr<ID3D11Device>           m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>    m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain>         m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
    Microsoft::WRL::ComPtr<ID3D11VertexShader>    m_vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>     m_ps;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>     m_inputLayout;
    Microsoft::WRL::ComPtr<ID3DBlob>              m_vsBlob;
    MeshMgr* m_meshMgr;

    int m_width = 0;
    int m_height = 0;
};
