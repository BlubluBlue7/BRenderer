#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include "MeshMgr.h"

class Camera;

class Renderer
{
public:
    bool Initialize(HWND hwnd, int width, int height);
    void RenderFrame(float deltaTime);
    void Cleanup();
    
    // 设置相机
    void SetCamera(Camera* camera) { m_camera = camera; }

private:
    // 从文件编译 Shader
    bool CompileShaderFromFile(const wchar_t* filename, const char* entryPoint, const char* target, ID3DBlob** blob);
    // 从字符串编译 Shader（备用方法）
    bool CompileShader(const char* shaderCode, const char* entryPoint, const char* target, ID3DBlob** blob);
    // 创建所有 Shader
    bool CreateShaders();
    // 创建输入布局（描述顶点数据的格式）
    bool CreateInputLayout();
    // 创建常量缓冲区
    bool CreateConstantBuffers();
    // 更新常量缓冲区（每帧调用）
    void UpdateConstantBuffers();

    Microsoft::WRL::ComPtr<ID3D11Device>           m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>    m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain>         m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
    Microsoft::WRL::ComPtr<ID3D11VertexShader>    m_vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>     m_ps;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>     m_inputLayout;
    Microsoft::WRL::ComPtr<ID3DBlob>              m_vsBlob;
    Microsoft::WRL::ComPtr<ID3D11Buffer>          m_constantBuffer;  // 常量缓冲区（用于传递变换矩阵）
    Microsoft::WRL::ComPtr<ID3D11Buffer>          m_lightBuffer;      // 光照常量缓冲区（用于传递光照参数）
    MeshMgr* m_meshMgr;
    Camera* m_camera = nullptr;  // 相机指针

    int m_width = 0;
    int m_height = 0;
};
