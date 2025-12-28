#pragma once

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <unordered_map>
#include "MeshMgr.h"

class Camera;
class Terrain;

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
    
    // 设置光源旋转暂停状态
    void SetLightRotationPaused(bool paused) { m_lightRotationPaused = paused; }
    
    // 切换光源旋转暂停状态
    void ToggleLightRotationPaused() { m_lightRotationPaused = !m_lightRotationPaused; }

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
    void UpdateConstantBuffers(float deltaTime = 0.0f);

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
    
    // 天空盒相关资源
    Microsoft::WRL::ComPtr<ID3D11VertexShader>    m_skyboxVS;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>     m_skyboxPS;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>     m_skyboxInputLayout;
    Microsoft::WRL::ComPtr<ID3D11Buffer>          m_skyboxVertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer>          m_skyboxIndexBuffer;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_skyboxDepthStencilState;  // 天空盒深度状态（LESS_EQUAL）
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_skyboxRasterizerState;  // 天空盒光栅化状态（禁用背面剔除）
    Microsoft::WRL::ComPtr<ID3D11Buffer>          m_constantBuffer;  // 常量缓冲区（用于传递变换矩阵）
    Microsoft::WRL::ComPtr<ID3D11Buffer>          m_lightBuffer;      // 光照常量缓冲区（用于传递光照参数）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_textureSRV;    // 纹理资源视图（单纹理，向后兼容）
    
    // 材质纹理结构：每个材质包含多个纹理
    struct MaterialTextures
    {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> baseColorSRV;  // BaseColor纹理
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> normalSRV;     // 法线贴图
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mraSRV;        // MRA贴图（Metallic-Roughness-AO）
    };
    std::unordered_map<std::string, MaterialTextures> m_materialTextures;  // 材质名称到纹理的映射
    Microsoft::WRL::ComPtr<ID3D11SamplerState>    m_samplerState;     // 纹理采样器状态
    Microsoft::WRL::ComPtr<ID3D11SamplerState>    m_iblSamplerState;  // IBL纹理采样器（支持mipmap和clamp）
    
    // IBL相关资源
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_environmentMapSRV;  // 环境贴图（HDR环境贴图）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_brdfLutSRV;         // BRDF查找表（用于镜面反射IBL）
    float m_environmentMapIntensity = 1.0f;  // 环境贴图强度
    
    MeshMgr* m_meshMgr;
    Camera* m_camera = nullptr;  // 相机指针
    
    // 地形相关
    Terrain* m_terrain = nullptr;  // 地形对象

    int m_width = 0;
    int m_height = 0;
    std::wstring m_lastError;  // 最后的错误信息
    float m_lightRotationTime = 0.0f;  // 光源旋转累积时间
    bool m_lightRotationPaused = false;  // 光源旋转是否暂停
    
    // 加载纹理
    bool LoadTexture(const std::wstring& filename);
    // 加载纹理文件（内部辅助函数，返回SRV）
    // isBaseColor: true表示BaseColor贴图（使用SRGB格式），false表示Normal/MRA贴图（使用UNORM格式）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> LoadTextureFile(const std::wstring& filename, bool isBaseColor = false);
    // 加载多个纹理（根据材质名称列表）
    bool LoadTextures(const std::vector<std::wstring>& materialNames, const std::string& projectRoot);
    // 创建默认纹理（白色纹理）
    bool CreateDefaultTexture();
    // 创建采样器状态
    bool CreateSamplerState();
    // 创建深度模板缓冲区和状态
    bool CreateDepthStencil();
    
    // IBL相关函数
    bool LoadEnvironmentMap(const std::wstring& filename);  // 加载环境贴图
    bool GenerateBRDFLUT();  // 生成BRDF查找表
    bool CreateIBLSamplerState();  // 创建IBL采样器（支持mipmap和clamp）
    
    // 天空盒相关函数
    bool CreateSkyboxShaders();  // 创建天空盒shader
    bool CreateSkyboxGeometry();  // 创建天空盒几何体
    bool CreateSkyboxDepthState();  // 创建天空盒深度状态
    void RenderSkybox();  // 渲染天空盒
    
    // 地形相关函数
    bool InitializeTerrain();  // 初始化地形
    void RenderTerrain();  // 渲染地形
}
