// Shadow Map Vertex Shader
// 从光源视角渲染深度图

// ============================================================================
// 常量缓冲区
// ============================================================================
cbuffer ShadowTransformBuffer : register(b0)
{
    float4x4 world;
    float4x4 lightView;
    float4x4 lightProjection;
    float4x4 lightWorldViewProj;
};

// ============================================================================
// 输入/输出结构
// ============================================================================
struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float3 color : COLOR;
    float2 texCoord : TEXCOORD;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float depth : DEPTH;
};

// ============================================================================
// 顶点着色器主函数
// ============================================================================
PSInput VS(VSInput input)
{
    PSInput output;
    
    // 变换到光源空间
    float4 worldPos = mul(float4(input.position, 1.0), world);
    output.position = mul(worldPos, lightWorldViewProj);
    
    // 存储深度值（用于调试）
    output.depth = output.position.z / output.position.w;
    
    return output;
}

