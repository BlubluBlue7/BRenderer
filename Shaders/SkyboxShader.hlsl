// Skybox Vertex Shader - 天空盒顶点着色器
// 作用：渲染天空盒（使用环境贴图）

// 常量缓冲区（与主shader使用相同的结构，但只使用view和projection）
cbuffer ConstantBuffer : register(b0)
{
    float4x4 world;        // 世界变换矩阵（天空盒不使用）
    float4x4 view;         // 视图变换矩阵
    float4x4 projection;   // 投影变换矩阵
    float4x4 worldViewProj; // 组合矩阵（天空盒不使用）
};

struct VSInput
{
    float3 position : POSITION;  // 顶点位置（局部空间）
};

struct PSInput
{
    float4 position : SV_POSITION;  // 像素在屏幕空间的位置
    float3 texCoord : TEXCOORD;     // 纹理坐标（方向向量，用于采样立方体贴图）
};

// 顶点着色器主函数
PSInput VS(VSInput input)
{
    PSInput output;
    
    // 将顶点位置转换为齐次坐标
    float4 pos = float4(input.position, 1.0f);
    
    // 应用视图矩阵（移除平移，只保留旋转）
    // 注意：HLSL中矩阵是列主序，mul是左乘（矩阵在左，向量在右）
    // 但对于行主序的矩阵（DirectXMath），我们需要转置或使用正确的顺序
    float4x4 viewNoTranslation = view;
    viewNoTranslation[3][0] = 0.0f;  // 移除平移
    viewNoTranslation[3][1] = 0.0f;
    viewNoTranslation[3][2] = 0.0f;
    viewNoTranslation[3][3] = 1.0f;
    
    // 矩阵乘法：由于常量缓冲区中的矩阵已经转置存储（HLSL使用列主序）
    // 在HLSL中，对于转置的矩阵，mul(vector, matrix)表示 vector * matrix^T = (matrix * vector)^T
    // 但对于位置向量，转置不影响x, y, z分量
    // 我们需要的是：projection * viewNoTranslation * pos
    // 由于矩阵已转置，使用 mul(pos, viewNoTranslation) 表示 (viewNoTranslation * pos)^T
    // 但位置向量的转置不影响结果，所以顺序是对的
    pos = mul(pos, viewNoTranslation);
    pos = mul(pos, projection);
    
    // 将z设为w，确保天空盒始终在远平面（深度=1）
    // 这样无论相机在哪里，天空盒都会正确显示
    // 注意：在DirectX中，深度值是z/w，所以如果z=w，深度值就是1.0（远平面）
    output.position = float4(pos.x, pos.y, pos.w, pos.w);  // 等价于pos.xyww，但更明确
    
    // 纹理坐标就是顶点位置（方向向量），用于采样立方体贴图
    output.texCoord = input.position;
    
    return output;
}

// Skybox Pixel Shader - 天空盒像素着色器
TextureCube environmentMap : register(t3);  // 环境贴图（立方体贴图）
SamplerState iblSampler : register(s1);     // IBL采样器

float4 PS(PSInput input) : SV_TARGET
{
    // 归一化方向向量（虽然应该已经是归一化的，但为了安全）
    float3 dir = normalize(input.texCoord);
    
    // 从环境贴图采样
    float3 color = environmentMap.Sample(iblSampler, dir).rgb;
    
    // 对于HDR环境贴图，可能需要色调映射
    // 简单的曝光调整（可以根据需要调整）
    float exposure = 1.0f;
    color = color * exposure;
    
    // 简单的Reinhard色调映射
    color = color / (color + float3(1.0f, 1.0f, 1.0f));
    
    // Gamma校正
    color = pow(color, float3(1.0f / 2.2f, 1.0f / 2.2f, 1.0f / 2.2f));
    
    return float4(color, 1.0f);
}

