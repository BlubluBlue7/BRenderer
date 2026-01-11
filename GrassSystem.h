#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <vector>
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>

class TerrainNew;

// ============================================================================
// 草地参数
// ============================================================================
struct GrassParams
{
    float worldSizeX = 1024.0f;      // 草地覆盖的世界大小X
    float worldSizeZ = 1024.0f;      // 草地覆盖的世界大小Z
    float density = 2.0f;            // 每单位面积的草数量（草/m²）
    float minHeight = 0.0f;          // 草地最低高度阈值
    float maxHeight = 1000.0f;       // 草地最高高度阈值
    float grassHeight = 0.5f;        // 草的高度（世界单位）
    float grassWidth = 0.02f;        // 草的宽度
    float windStrength = 0.5f;        // 风强度（0-1）
    float windSpeed = 1.0f;           // 风速度（摆动速度）
    DirectX::XMFLOAT3 windDirection = {1.0f, 0.0f, 1.0f}; // 风向
    float alphaTestThreshold = 0.5f; // Alpha测试阈值
    
    // 性能优化参数（未来可以添加）
    // float maxRenderDistance = 500.0f;  // 最大渲染距离（距离剔除）
    // 注意：完整的视锥剔除和距离剔除需要使用GPU计算着色器（compute shader）
    // 进行剔除，然后使用间接绘制（indirect draw），这需要较大的重构
};

// ============================================================================
// 草实例数据
// ============================================================================
struct GrassInstanceData
{
    DirectX::XMFLOAT3 position;      // 世界空间位置
    float rotation;                   // 绕Y轴的旋转角度（0-2π）
    float scale;                      // 缩放因子（0.8-1.2）
    float heightVariation;            // 高度变化（0.8-1.2）
    DirectX::XMFLOAT4 color;         // 颜色变化
};

// ============================================================================
// 草地系统类
// ============================================================================
class GrassSystem
{
public:
    GrassSystem();
    ~GrassSystem();

    // 初始化草地系统
    bool Initialize(ID3D11Device* device, const GrassParams& params, TerrainNew* terrain);
    
    // 更新（每帧调用，用于动画）
    void Update(float deltaTime);
    
    // 渲染草地
    void Render(ID3D11DeviceContext* context,
                const DirectX::XMFLOAT4X4& viewMatrix,
                const DirectX::XMFLOAT4X4& projMatrix,
                const DirectX::XMFLOAT3& cameraPosition,
                const DirectX::XMFLOAT4& lightDirection = DirectX::XMFLOAT4(0.5f, -1.0f, 0.3f, 0.0f),
                const DirectX::XMFLOAT4& lightColor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 0.0f),
                const DirectX::XMFLOAT4& ambientColor = DirectX::XMFLOAT4(0.2f, 0.2f, 0.2f, 0.0f));
    
    // 清理资源
    void Cleanup();
    
    // 设置参数
    void SetParams(const GrassParams& params) { m_params = params; }
    const GrassParams& GetParams() const { return m_params; }
    
    // 获取实例数量
    UINT GetInstanceCount() const { return m_instanceCount; }

private:
    // 生成草地实例
    void GenerateInstances(TerrainNew* terrain);
    
    // 创建草的几何数据（交叉四边形）
    bool CreateGrassGeometry(ID3D11Device* device);
    
    // 创建实例缓冲区
    bool CreateInstanceBuffer(ID3D11Device* device);
    
    // 创建着色器
    bool CreateShaders(ID3D11Device* device);
    
    // 创建纹理和采样器
    bool CreateTextureAndSampler(ID3D11Device* device);
    
    // 更新常量缓冲区
    void UpdateConstantBuffer(ID3D11DeviceContext* context,
                              const DirectX::XMFLOAT4X4& viewMatrix,
                              const DirectX::XMFLOAT4X4& projMatrix,
                              const DirectX::XMFLOAT3& cameraPosition,
                              const DirectX::XMFLOAT4& lightDirection = DirectX::XMFLOAT4(0.5f, -1.0f, 0.3f, 0.0f),
                              const DirectX::XMFLOAT4& lightColor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 0.0f),
                              const DirectX::XMFLOAT4& ambientColor = DirectX::XMFLOAT4(0.2f, 0.2f, 0.2f, 0.0f));
    
    // 成员变量
    GrassParams m_params;
    TerrainNew* m_terrain;
    
    // 几何数据
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_indexBuffer;
    UINT m_vertexCount;
    UINT m_indexCount;
    
    // 实例数据
    std::vector<GrassInstanceData> m_instances;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_instanceBuffer;
    UINT m_instanceCount;
    
    // 着色器
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
    Microsoft::WRL::ComPtr<ID3DBlob> m_vsBlob;
    
    // 纹理和采样器
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_grassTextureSRV;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_samplerState;
    
    // 常量缓冲区
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;
    
    // 混合状态（Alpha blending）
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendState;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthStencilState;
    
    // 光栅化状态（实心和线框）
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_solidRasterizerState;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_wireframeRasterizerState;
    
    // 时间（用于风动画）
    float m_time;
    
    // 是否已初始化
    bool m_initialized;
};

