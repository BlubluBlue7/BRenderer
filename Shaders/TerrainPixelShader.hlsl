// Terrain Pixel Shader - 地形像素着色器
// 专门用于地形渲染，简化版本

// 纹理和采样器
Texture2D baseColorTexture : register(t0);  // BaseColor纹理
Texture2D normalTexture : register(t1);     // 法线贴图（可选）
Texture2D mraTexture : register(t2);        // MRA贴图（可选）
SamplerState textureSampler : register(s0);  // 纹理采样器

// 常量缓冲区：光照参数（简化版）
cbuffer LightBuffer : register(b1)
{
    float4 lightDirection;  // 光源方向
    float lightIntensity;   // 光源强度
    float padding1a;
    float padding1b;
    float padding1c;
    
    float4 lightColor;      // 光源颜色
    float padding1;
    float padding1d;
    float padding1e;
    float padding1f;
    
    float4 cameraPosition;  // 相机位置
    float padding2;
    float padding2a;
    float padding2b;
    float padding2c;
    
    float4 albedo;          // 反照率（基础颜色）
    float metallic;
    float padding2d;
    float padding2e;
    float padding2f;
    
    float roughness;
    float padding3a;
    float padding3b;
    float padding3c;
    
    float4 ambientColor;    // 环境光颜色
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
    // 修复Y轴颠倒：翻转V坐标
    float2 flippedTexCoord = float2(input.texCoord.x, 1.0f - input.texCoord.y);
    
    // 从纹理采样BaseColor
    float4 baseColorSample = baseColorTexture.Sample(textureSampler, flippedTexCoord);
    
    // 基于高度的颜色混合（让地形颜色更有意义）
    // 使用世界空间Y坐标（高度）来混合不同的颜色
    float height = input.worldPos.y;
    float normalizedHeight = saturate(height / 30.0f);  // 假设最大高度为30
    
    // 定义不同高度的颜色（从低到高）
    float3 lowColor = float3(0.2f, 0.4f, 0.2f);    // 深绿色（低地）
    float3 midColor = float3(0.5f, 0.6f, 0.3f);    // 浅绿色（中地）
    float3 highColor = float3(0.7f, 0.7f, 0.6f);   // 浅棕色（高地）
    float3 snowColor = float3(0.9f, 0.9f, 0.95f);   // 白色（雪地）
    
    // 根据高度混合颜色
    float3 heightColor;
    if (normalizedHeight < 0.3f)
    {
        // 低地：深绿色
        heightColor = lowColor;
    }
    else if (normalizedHeight < 0.6f)
    {
        // 中地：深绿到浅绿的过渡
        float t = (normalizedHeight - 0.3f) / 0.3f;
        heightColor = lerp(lowColor, midColor, t);
    }
    else if (normalizedHeight < 0.85f)
    {
        // 高地：浅绿到浅棕的过渡
        float t = (normalizedHeight - 0.6f) / 0.25f;
        heightColor = lerp(midColor, highColor, t);
    }
    else
    {
        // 雪地：浅棕到白色的过渡
        float t = (normalizedHeight - 0.85f) / 0.15f;
        heightColor = lerp(highColor, snowColor, t);
    }
    
    // 混合纹理颜色和高度颜色（70%高度颜色，30%纹理颜色）
    float3 finalAlbedo = lerp(heightColor, baseColorSample.rgb, 0.3f) * albedo.xyz;
    
    // 简单的Lambertian漫反射光照
    float3 N = normalize(input.normal);
    float3 L = normalize(-lightDirection.xyz);  // 光源方向（取反，因为lightDirection指向光源）
    float NdotL = max(dot(N, L), 0.0f);
    
    // 计算光照颜色
    float3 diffuse = finalAlbedo * lightColor.xyz * lightIntensity * NdotL;
    
    // 添加环境光（增加环境光强度，让地形更亮）
    float3 ambient = finalAlbedo * ambientColor.xyz * 0.5f;
    
    // 最终颜色
    float3 finalColor = diffuse + ambient;
    
    // 限制颜色范围
    finalColor = saturate(finalColor);
    
    return float4(finalColor, 1.0f);
}

