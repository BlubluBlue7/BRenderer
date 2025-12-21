// Vertex Shader - 顶点着色器（PBR 版本）
// 作用：处理每个顶点的位置和法线，将顶点从模型空间转换到世界空间和屏幕空间
// 用于 PBR（基于物理的渲染）渲染管线

// 常量缓冲区：用于传递变换矩阵和光照参数
// 注意：HLSL 中常量缓冲区必须按 16 字节对齐
cbuffer ConstantBuffer : register(b0)
{
    float4x4 world;        // 世界变换矩阵（模型空间 -> 世界空间）
    float4x4 view;         // 视图变换矩阵（世界空间 -> 视图空间）
    float4x4 projection;   // 投影变换矩阵（视图空间 -> 屏幕空间）
    float4x4 worldViewProj; // 组合矩阵（世界-视图-投影）
};

// 顶点着色器输入结构体
// 对应 C++ 中的 Vertex 结构体
struct VSInput
{
    float3 position : POSITION;  // 顶点位置（模型空间）
    float3 normal : NORMAL;      // 法线向量（模型空间）
    float3 color : COLOR;        // 顶点颜色（材质颜色，可选）
    float2 texCoord : TEXCOORD;  // 纹理坐标（UV坐标）
};

// 顶点着色器输出结构体（传递给像素着色器）
struct PSInput
{
    float4 position : SV_POSITION;  // 顶点在屏幕空间的位置（齐次坐标）
    float3 worldPos : POSITION;     // 顶点在世界空间的位置
    float3 normal : NORMAL;         // 法线向量（世界空间）
    float3 color : COLOR;           // 顶点颜色（材质颜色）
    float2 texCoord : TEXCOORD;     // 纹理坐标（UV坐标）
};

// 顶点着色器主函数
// 对每个顶点调用一次
PSInput VS(VSInput input)
{
    PSInput output;
    
    // 将顶点位置从模型空间转换到世界空间
    float4 worldPosition = mul(float4(input.position, 1.0f), world);
    output.worldPos = worldPosition.xyz;
    
    // 将顶点位置从模型空间转换到屏幕空间
    // 使用组合矩阵进行变换
    output.position = mul(float4(input.position, 1.0f), worldViewProj);
    
    // 将法线从模型空间转换到世界空间
    // 注意：法线是方向向量，使用 world 矩阵的逆置矩阵（这里简化为 world 矩阵）
    // 对于均匀缩放，可以直接使用 world 矩阵
    float3x3 worldNormalMatrix = (float3x3)world;
    output.normal = normalize(mul(input.normal, worldNormalMatrix));
    
    // 传递颜色到像素着色器
    output.color = input.color;
    
    // 传递纹理坐标到像素着色器（不需要变换）
    output.texCoord = input.texCoord;
    
    return output;
}
