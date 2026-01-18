// Shadow Map Vertex Shader - 阴影贴图顶点着色器
// 作用：将顶点从模型空间转换到光源的视图空间和投影空间
// 用于生成shadow map（深度贴图）

// 常量缓冲区：用于传递光源的变换矩阵
cbuffer ShadowConstantBuffer : register(b0)
{
    float4x4 world;            // 世界变换矩阵（模型空间 -> 世界空间）
    float4x4 lightView;        // 光源视图矩阵（世界空间 -> 光源视图空间）
    float4x4 lightProjection;  // 光源投影矩阵（光源视图空间 -> 光源投影空间）
    float4x4 lightViewProj;    // 组合矩阵（世界-光源视图-投影）
};

// 顶点着色器输入结构体
struct VSInput
{
    float3 position : POSITION;  // 顶点位置（模型空间）
    float3 normal : NORMAL;       // 法线向量（模型空间，暂时不使用）
    float3 color : COLOR;        // 顶点颜色（暂时不使用）
    float2 texCoord : TEXCOORD;  // 纹理坐标（暂时不使用）
};

// 顶点着色器输出结构体（传递给像素着色器）
struct PSInput
{
    float4 position : SV_POSITION;  // 顶点在光源投影空间的位置（齐次坐标）
};

// 顶点着色器主函数
// 对每个顶点调用一次
PSInput VS(VSInput input)
{
    PSInput output;
    
    // 将顶点位置从模型空间转换到光源投影空间
    // 注意：lightViewProj已经是world * lightView * lightProjection的组合矩阵
    // 如果world是单位矩阵（如地形），则lightViewProj = lightView * lightProjection
    // 如果world不是单位矩阵（如模型），则lightViewProj = world * lightView * lightProjection
    output.position = mul(float4(input.position, 1.0f), lightViewProj);
    
    return output;
}

