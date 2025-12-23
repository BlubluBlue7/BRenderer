#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string>
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
    
    // 获取最后的错误信息
    const wchar_t* GetLastError() const { return m_lastError.c_str(); }

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
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_dsv;             // 深度模板视图
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthStencilState;  // 深度模板状态
    Microsoft::WRL::ComPtr<ID3D11VertexShader>    m_vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>     m_ps;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>     m_inputLayout;
    Microsoft::WRL::ComPtr<ID3DBlob>              m_vsBlob;
    Microsoft::WRL::ComPtr<ID3D11Buffer>          m_constantBuffer;  // 常量缓冲区（用于传递变换矩阵）
    Microsoft::WRL::ComPtr<ID3D11Buffer>          m_lightBuffer;      // 光照常量缓冲区（用于传递光照参数）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_textureSRV;    // 纹理资源视图
    Microsoft::WRL::ComPtr<ID3D11SamplerState>    m_samplerState;     // 纹理采样器状态
    MeshMgr* m_meshMgr;
    Camera* m_camera = nullptr;  // 相机指针

    int m_width = 0;
    int m_height = 0;
    std::wstring m_lastError;  // 最后的错误信息
    
    // 加载纹理
    bool LoadTexture(const std::wstring& filename);
    // 创建默认纹理（白色纹理）
    bool CreateDefaultTexture();
    // 创建采样器状态
    bool CreateSamplerState();
    // 创建深度模板缓冲区和状态
    bool CreateDepthStencil();
};
