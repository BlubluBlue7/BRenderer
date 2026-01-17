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

// ============================================================================
// 草地系统 - 最简单的版本
// 使用2个三角形（4个顶点）组成一个面片代表草
// ============================================================================
class GrassSystem
{
public:
    GrassSystem();
    ~GrassSystem();

    // 初始化草地系统
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    
    // 清理资源
    void Cleanup();
    
    // 渲染草地
    void Render(ID3D11DeviceContext* context, const XMFLOAT4X4& view, const XMFLOAT4X4& projection, float deltaTime = 0.0f);
    
    // 设置草的位置（单个草，向后兼容）
    void SetPosition(const XMFLOAT3& position) { m_position = position; }
    void SetPosition(float x, float y, float z) { m_position = XMFLOAT3(x, y, z); }
    
    // 生成多个草的位置，铺满整个地形
    // terrainSizeX, terrainSizeZ: 地形大小（世界空间单位）
    // spacing: 草之间的间距（世界空间单位，默认1.0）
    // getHeightFunc: 获取地形高度的函数（可以为nullptr，如果为nullptr则高度为0）
    void GenerateGrassPositions(float terrainSizeX, float terrainSizeZ, float spacing = 1.0f, 
                                std::function<float(float, float)> getHeightFunc = nullptr);

private:
    // 创建草的几何体（2个三角形组成一个面片）
    bool CreateGrassGeometry(ID3D11Device* device);
    
    // 创建着色器
    bool CreateShaders(ID3D11Device* device);
    
    // 创建输入布局
    bool CreateInputLayout(ID3D11Device* device);
    
    // 创建常量缓冲区
    bool CreateConstantBuffer(ID3D11Device* device);
    
    // 创建实例缓冲区
    bool CreateInstanceBuffer(ID3D11Device* device);
    
    // 更新实例缓冲区（根据视锥剔除结果）
    void UpdateInstanceBuffer(ID3D11DeviceContext* context, const XMFLOAT4X4& view, const XMFLOAT4X4& projection);
    
    // 视锥剔除：检查点是否在视锥体内
    bool IsPointInFrustum(const XMFLOAT3& point, const XMFLOAT4* frustumPlanes) const;
    
    // 从视图投影矩阵提取视锥体平面
    void ExtractFrustumPlanes(const XMFLOAT4X4& viewProj, XMFLOAT4* planes) const;

    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    
    // 几何体资源
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_indexBuffer;
    UINT m_indexCount = 0;
    
    // 着色器资源
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
    Microsoft::WRL::ComPtr<ID3DBlob> m_vsBlob;
    
    // 常量缓冲区（只包含view和projection）
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;
    
    // 实例缓冲区（存储可见草的位置）
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_instanceBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_instanceBufferSRV;
    UINT m_visibleInstanceCount = 0;
    
    // 草的位置列表（所有草的位置，用于剔除）
    std::vector<XMFLOAT3> m_grassPositions;
    
    // 可见草的位置列表（剔除后的结果）
    std::vector<XMFLOAT3> m_visibleGrassPositions;
    
    // 单个草的位置（向后兼容，已废弃）
    XMFLOAT3 m_position = XMFLOAT3(0.0f, 0.0f, 0.0f);
};

