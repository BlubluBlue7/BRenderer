#pragma once

// 确保在包含 Windows.h 相关头文件之前定义 NOMINMAX，避免 min/max 宏冲突
#ifndef NOMINMAX
#define NOMINMAX
#endif

// 先包含标准库头文件
#include <string>
#include <vector>
#include <unordered_map>

// 然后包含Windows和DirectX头文件
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <dwrite.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <wincodec.h>
#include "MeshMgr.h"

// 注意：XMMATRIX在DirectXMath.h中定义，但由于某些编译器问题，这里使用void*作为占位符
// 实际实现中使用XMMATRIX

class Camera;
class Terrain;
class TerrainNew;  // 前向声明
class GrassSystem;  // 前向声明

class Renderer
{
public:
    bool Initialize(HWND hwnd, int width, int height);
    void RenderFrame(float deltaTime);
    void Cleanup();
    
    // 设置相机
    void SetCamera(Camera* camera) { m_camera = camera; }
    
    // 切换相机到光源视角（用于调试shadow map）
    void SwitchCameraToLightView();
    
    // 获取最后的错误信息
    const wchar_t* GetLastError() const { return m_lastError.c_str(); }
    
    // 设置光源旋转暂停状态
    void SetLightRotationPaused(bool paused) { m_lightRotationPaused = paused; }
    
    // 切换光源旋转暂停状态
    void ToggleLightRotationPaused() { m_lightRotationPaused = !m_lightRotationPaused; }
    
    // 切换地形线框渲染模式
    void ToggleTerrainWireframe() { m_terrainWireframe = !m_terrainWireframe; }
    
    // 设置地形线框渲染模式
    void SetTerrainWireframe(bool wireframe) { m_terrainWireframe = wireframe; }
    
    // 切换地形LOD锁定（用于调试查看网格）
    void ToggleTerrainLODLock();
    
    // 设置地形LOD锁定级别
    void SetTerrainLODLockLevel(int level);
    
    // 切换地形LOD调试可视化模式
    void ToggleTerrainLODDebug();

    // 切换地形深度调试可视化模式
    void ToggleTerrainDepthDebug();

    // 切换地形阴影调试可视化模式
    void ToggleTerrainShadowDebug();

    // 获取地形
    TerrainNew* GetTerrain() const { return m_terrain; }

    // 获取角色位置（用于绘制模型）
    DirectX::XMFLOAT3 GetCharacterPosition() const;

    // 处理键盘输入（用于控制光源移动）
    void HandleKeyboardInput(float deltaTime, bool keyW, bool keyA, bool keyS, bool keyD);

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

    // 光源可视化椎体相关资源
    Microsoft::WRL::ComPtr<ID3D11Buffer>          m_lightCubeVertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer>          m_lightCubeIndexBuffer;
    Microsoft::WRL::ComPtr<ID3D11VertexShader>    m_lightCubeVS;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>     m_lightCubePS;
    UINT                                        m_lightConeIndexCount;     // 椎体索引数量
    
    // 地形相关shader资源
    Microsoft::WRL::ComPtr<ID3D11VertexShader>    m_terrainVS;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>     m_terrainPS;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>     m_terrainWireframePS;  // 线框像素着色器（黑色）
    Microsoft::WRL::ComPtr<ID3D11InputLayout>     m_terrainInputLayout;
    Microsoft::WRL::ComPtr<ID3DBlob>              m_terrainVSBlob;
    Microsoft::WRL::ComPtr<ID3D11Buffer>          m_skyboxVertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer>          m_skyboxIndexBuffer;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_skyboxDepthStencilState;  // 天空盒深度状态（LESS_EQUAL）
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_skyboxRasterizerState;  // 天空盒光栅化状态（禁用背面剔除）
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_terrainWireframeRasterizerState;  // 地形线框光栅化状态
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
    
    // Shadow Map相关资源
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_shadowMapTexture;           // Shadow map纹理
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_shadowMapDSV;         // Shadow map深度视图
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_shadowMapSRV;      // Shadow map着色器资源视图
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_shadowMapSamplerState;   // Shadow map采样器状态
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_shadowMapVS;             // Shadow map顶点着色器
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_shadowMapPS;              // Shadow map像素着色器
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_shadowMapInputLayout;     // Shadow map输入布局
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_shadowMapConstantBuffer;       // Shadow map常量缓冲区
    static const UINT SHADOW_MAP_SIZE = 4096;  // Shadow map分辨率
    
    MeshMgr* m_meshMgr;
    Camera* m_camera = nullptr;  // 相机指针
    
    // 地形相关
    TerrainNew* m_terrain = nullptr;  // 地形对象
    
    // 草地系统相关
    GrassSystem* m_grassSystem = nullptr;  // 草地系统对象
    
    int m_width = 0;
    int m_height = 0;
    std::wstring m_lastError;  // 最后的错误信息
    float m_lightRotationTime = 0.0f;  // 光源旋转累积时间
    bool m_lightRotationPaused = false;  // 光源旋转是否暂停
    bool m_terrainWireframe = false;  // 地形是否使用线框模式

    // 光源位置（可通过方向键控制移动，默认位于地形中心正上方）
    DirectX::XMFLOAT3 m_lightPosition = DirectX::XMFLOAT3(0.0f, 200.0f, 0.0f);
    
    // DirectWrite文字渲染相关
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormat;
    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> m_d2dRenderTarget;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_textBrush;
    Microsoft::WRL::ComPtr<IDXGISurface> m_dxgiSurface;
    
    // FPS和统计相关
    float m_fps = 0.0f;
    float m_fpsUpdateTime = 0.0f;
    int m_frameCount = 0;
    UINT m_totalTriangles = 0;  // 当前帧渲染的总三角形数
    UINT m_terrainTriangles = 0;  // 地形三角形数
    UINT m_meshTriangles = 0;  // 网格三角形数
    
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

    // 光源可视化相关
    bool CreateLightVisualizationShaders();  // 创建光源可视化shader
    bool CreateLightVisualizationGeometry(); // 创建光源可视化椎体几何体
    void RenderLightVisualization(const DirectX::XMFLOAT3& lightPos); // 渲染光源可视化椎体

    // 阴影调试相关
    bool CreateSkyboxDepthState();  // 创建天空盒深度状态
    void RenderSkybox();  // 渲染天空盒
    
    // 地形相关函数
    bool CreateTerrainShaders();  // 创建地形shader
    bool CreateTerrainRasterizerStates();  // 创建地形光栅化状态（填充和线框）
    bool InitializeTerrain();  // 初始化地形
    void RenderTerrain();  // 渲染地形
    
    // 草地系统相关函数
    bool InitializeGrassSystem();  // 初始化草地系统
    void RenderGrassSystem(float deltaTime = 0.016f);  // 渲染草地系统
    
    // Shadow Map相关函数
    bool CreateShadowMap();  // 创建shadow map资源
    bool CreateShadowMapShaders();  // 创建shadow map shader
    void RenderShadowMap();  // 渲染shadow map（将角色模型绘制到shadow map）
    
    // 文字渲染相关函数
    bool InitializeTextRendering();  // 初始化DirectWrite文字渲染
    void RenderText(const wchar_t* text, float x, float y);  // 渲染文字
    void UpdateFPS(float deltaTime);  // 更新FPS
    void UpdateTriangleCount();  // 更新面数统计
private:
public:
};
