// Terrain Vertex Shader - 地形顶点着色器
// 专门用于地形渲染，简化版本

// 常量缓冲区：用于传递变换矩阵
cbuffer ConstantBuffer : register(b0)
{
    float4x4 world;        // 世界变换矩阵（地形使用单位矩阵）
    float4x4 view;         // 视图变换矩阵
    float4x4 projection;   // 投影变换矩阵
    float4x4 worldViewProj; // 组合矩阵（世界-视图-投影）
};

// 顶点着色器输入结构体
struct VSInput
{
    float3 position : POSITION;  // 顶点位置（已经是世界空间）
    float3 normal : NORMAL;      // 法线向量
    float3 color : COLOR;        // 顶点颜色
    float2 texCoord : TEXCOORD;  // 纹理坐标
};

// 顶点着色器输出结构体
struct PSInput
{
    float4 position : SV_POSITION;  // 顶点在屏幕空间的位置
    float3 worldPos : POSITION;     // 顶点在世界空间的位置
    float3 normal : NORMAL;         // 法线向量（世界空间）
    float3 color : COLOR;           // 顶点颜色
    float2 texCoord : TEXCOORD;     // 纹理坐标
};

// 顶点着色器主函数
PSInput VS(VSInput input)
{
    PSInput output;
    
    // 地形顶点已经在世界空间中，world矩阵是单位矩阵
    // 所以worldPos直接等于input.position
    output.worldPos = input.position;
    
    // 使用worldViewProj矩阵变换到屏幕空间
    output.position = mul(float4(input.position, 1.0f), worldViewProj);
    
    // 法线已经在世界空间（地形生成时计算的）
    output.normal = normalize(input.normal);
    
    // 传递颜色和纹理坐标
    output.color = input.color;
    output.texCoord = input.texCoord;
    
    return output;
}

