#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <vector>
#include <functional>

using namespace DirectX;

// 前向声明
class TerrainNew;

// ============================================================================
// 水体系统 - 基础版本
// 在地形低洼处渲染水体
// ============================================================================
class WaterSystem
{
public:
    WaterSystem();
    ~WaterSystem();

    // 初始化水体系统
    // terrain: 地形对象指针，用于查询高度
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context, TerrainNew* terrain);
    
    // 清理资源
    void Cleanup();
    
    // 渲染水体
    void Render(ID3D11DeviceContext* context, 
                const XMFLOAT4X4& view, 
                const XMFLOAT4X4& projection,
                const XMFLOAT3& cameraPosition,
                float deltaTime = 0.0f);
    
    // 设置水位高度（如果为0，则自动从地形计算）
    void SetWaterLevel(float level) { m_waterLevel = level; m_autoCalculateLevel = false; }

    // 波浪参数（基础版本）
    void SetWaveParams(float amplitude, float frequency, float speed)
    {
        m_waveAmplitude = amplitude;
        m_waveFrequency = frequency;
        m_waveSpeed = speed;
    }
    
    // 启用/禁用自动计算水位（基于地形最低点）
    void SetAutoCalculateLevel(bool enable) { m_autoCalculateLevel = enable; }
    
    // 获取当前水位高度
    float GetWaterLevel() const { return m_waterLevel; }
    
    // 设置水体范围（覆盖地形范围）
    void SetWaterBounds(float minX, float minZ, float maxX, float maxZ);

private:
    // 创建水体网格几何体
    bool CreateWaterGeometry(ID3D11Device* device);
    
    // 创建着色器
    bool CreateShaders(ID3D11Device* device);
    
    // 创建输入布局
    bool CreateInputLayout(ID3D11Device* device);
    
    // 创建常量缓冲区
    bool CreateConstantBuffer(ID3D11Device* device);
    
    // 创建深度状态（允许与地形共面）
    bool CreateDepthStencilState(ID3D11Device* device);
    
    // 创建混合状态（半透明）
    bool CreateBlendState(ID3D11Device* device);

    // 创建光栅化状态（双面渲染，避免水面被剔除）
    bool CreateRasterizerState(ID3D11Device* device);
    
    // 计算水位高度（基于地形最低点）
    void CalculateWaterLevel();
    
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    TerrainNew* m_terrain = nullptr;
    
    // 几何体资源
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_indexBuffer;
    UINT m_indexCount = 0;
    UINT m_vertexCount = 0;
    
    // 着色器资源
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
    Microsoft::WRL::ComPtr<ID3DBlob> m_vsBlob;
    
    // 常量缓冲区
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;
    
    // 渲染状态
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthStencilState;
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendState;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizerState;
    
    // 水体参数
    float m_waterLevel = 0.0f;           // 水位高度
    bool m_autoCalculateLevel = true;     // 是否自动计算水位
    float m_minX = -512.0f;               // 水体范围
    float m_minZ = -512.0f;
    float m_maxX = 512.0f;
    float m_maxZ = 512.0f;
    
    // 网格参数
    int m_gridWidth = 64;                 // 网格宽度（顶点数）
    int m_gridHeight = 64;                // 网格高度（顶点数）

    // 波浪参数（用于shader动画）
    float m_time = 0.0f;
    float m_waveAmplitude = 0.8f;   // 波浪振幅（世界单位）
    float m_waveFrequency = 0.08f;  // 波浪频率（越大波越密）
    float m_waveSpeed = 1.2f;       // 波浪速度
};

