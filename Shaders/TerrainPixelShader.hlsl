// Terrain Pixel Shader - 地形像素着色器
// 专门用于地形渲染

// 纹理和采样器
Texture2D baseColorTexture : register(t0);
SamplerState textureSampler : register(s0);

// 常量缓冲区：光照参数
cbuffer LightBuffer : register(b1)
{
    float4 lightDirection;
    float lightIntensity;
    float padding1a;
    float padding1b;
    float padding1c;
    
    float4 lightColor;
    float padding1;
    float padding1d;
    float padding1e;
    float padding1f;
    
    float4 cameraPosition;
    float padding2;
    float padding2a;
    float padding2b;
    float padding2c;
    
    float4 albedo;
    float metallic;
    float padding2d;
    float padding2e;
    float padding2f;
    
    float roughness;
    float padding3a;
    float padding3b;
    float padding3c;
    
    float4 ambientColor;
    float padding4;
    float padding4a;
    float padding4b;
    float padding4c;
};

// 像素着色器输入结构体
struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal : NORMAL;
    float3 color : COLOR;
    float2 texCoord : TEXCOORD;
};

// 像素着色器主函数
float4 PS(PSInput input) : SV_TARGET
{
    // 使用世界空间Y坐标（高度）来混合不同的颜色
    float height = input.worldPos.y;
    float normalizedHeight = saturate(height / 30.0f);
    
    // 定义不同高度的颜色（从低到高）
    float3 lowColor = float3(0.22f, 0.42f, 0.18f);    // 深绿色（低地/草地）
    float3 midColor = float3(0.45f, 0.55f, 0.28f);    // 浅绿色（中地）
    float3 highColor = float3(0.55f, 0.50f, 0.40f);   // 棕色（高地/岩石）
    float3 snowColor = float3(0.95f, 0.95f, 0.98f);   // 白色（雪地）
    
    // 根据高度平滑混合颜色
    float3 heightColor;
    if (normalizedHeight < 0.3f)
    {
        heightColor = lowColor;
    }
    else if (normalizedHeight < 0.5f)
    {
        float t = (normalizedHeight - 0.3f) / 0.2f;
        heightColor = lerp(lowColor, midColor, smoothstep(0.0f, 1.0f, t));
    }
    else if (normalizedHeight < 0.75f)
    {
        float t = (normalizedHeight - 0.5f) / 0.25f;
        heightColor = lerp(midColor, highColor, smoothstep(0.0f, 1.0f, t));
    }
    else
    {
        float t = (normalizedHeight - 0.75f) / 0.25f;
        heightColor = lerp(highColor, snowColor, smoothstep(0.0f, 1.0f, t));
    }
    
    // 添加基于坡度的颜色变化（陡峭处更多岩石色）
    float3 normal = normalize(input.normal);
    float slope = 1.0f - normal.y;  // 0 = 平地, 1 = 垂直
    float3 rockColor = float3(0.45f, 0.42f, 0.38f);
    heightColor = lerp(heightColor, rockColor, saturate(slope * 2.0f));
    
    // 简单的 Lambert 光照
    float3 lightDir = normalize(-lightDirection.xyz);
    float NdotL = max(dot(normal, lightDir), 0.0f);
    
    // 环境光 + 漫反射
    float3 ambient = heightColor * 0.3f;
    float3 diffuse = heightColor * NdotL * lightIntensity * 0.7f;
    
    float3 finalColor = ambient + diffuse;
    
    // 限制颜色范围
    finalColor = saturate(finalColor);
    
    return float4(finalColor, 1.0f);
}
