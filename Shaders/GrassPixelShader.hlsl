// Grass Pixel Shader
// 草地像素着色器，支持Alpha测试和简单光照

// ============================================================================
// 纹理和采样器
// ============================================================================
Texture2D grassTexture : register(t0);
SamplerState textureSampler : register(s0);

// ============================================================================
// 常量缓冲区（与顶点着色器共享）
// ============================================================================
cbuffer GrassBuffer : register(b0)
{
    float4x4 worldViewProj;      // 世界-视图-投影矩阵（像素着色器不需要，但为了对齐保留）
    float4x4 world;              // 世界矩阵（像素着色器不需要，但为了对齐保留）
    float4x4 view;               // 视图矩阵（像素着色器不需要，但为了对齐保留）
    float4 cameraPosition;       // xyz = 相机位置（像素着色器不需要，但为了对齐保留）
    float4 windParams;           // 风参数（像素着色器不需要，但为了对齐保留）
    float4 grassParams;          // 草参数（像素着色器不需要，但为了对齐保留）
    float4 renderParams;         // x = alpha阈值
    float4 lightDirection;       // xyz = 光源方向（归一化，指向光源）
    float4 lightColor;           // xyz = 光源颜色
    float4 ambientColor;         // xyz = 环境光颜色
};

// ============================================================================
// 输入结构
// ============================================================================
struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
};

// ============================================================================
// 像素着色器主函数
// ============================================================================
float4 PS(PSInput input) : SV_Target
{
    // 1. 采样草纹理
    float4 texColor = grassTexture.Sample(textureSampler, input.texCoord);
    
    // 2. Alpha测试（丢弃透明像素，避免深度问题）
    if (texColor.a < renderParams.x)
        discard;
    
    // 3. 简单光照计算
    float3 N = normalize(input.normal);
    float3 L = normalize(float3(0.5f, -1.0f, 0.3f)); // 默认光源方向
    if (length(lightDirection.xyz) > 0.001)
    {
        L = normalize(-lightDirection.xyz);
    }
    float NdotL = max(dot(N, L), 0.0);
    
    // 混合环境光和漫反射光
    float3 ambient = float3(0.2f, 0.2f, 0.2f); // 默认环境光
    if (length(ambientColor.rgb) > 0.001)
    {
        ambient = ambientColor.rgb * 0.3;
    }
    float3 lightCol = float3(1.0f, 1.0f, 0.95f); // 默认光源颜色
    if (length(lightColor.rgb) > 0.001)
    {
        lightCol = lightColor.rgb;
    }
    float3 diffuse = lightCol * NdotL * 0.7;
    float3 litColor = texColor.rgb * (ambient + diffuse);
    
    // 4. 应用实例颜色变化（用于多样性）
    litColor *= input.color.rgb;
    
    // 5. 返回最终颜色（保持Alpha）
    return float4(litColor, texColor.a);
}

