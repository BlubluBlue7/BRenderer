// Pixel Shader - 像素着色器（PBR 版本）
// 作用：实现基于物理的渲染（Physically Based Rendering）
// 使用 Cook-Torrance BRDF 模型

// 纹理和采样器
Texture2D diffuseTexture : register(t0);  // 漫反射纹理（Albedo纹理）
SamplerState textureSampler : register(s0);  // 纹理采样器

// 常量缓冲区：光照参数
cbuffer LightBuffer : register(b1)
{
    float3 lightDirection;  // 光源方向（归一化的方向向量，指向光源）
    float3 lightColor;      // 光源颜色和强度（RGB）
    float lightIntensity;   // 光源强度
    float3 cameraPosition; // 相机位置（世界空间）
    float padding1;
    
    // PBR 材质参数
    float3 albedo;          // 反照率（基础颜色）
    float metallic;         // 金属度（0.0 = 非金属，1.0 = 金属）
    float roughness;       // 粗糙度（0.0 = 完全光滑，1.0 = 完全粗糙）
    float padding2;
    
    // 环境光参数
    float3 ambientColor;    // 环境光颜色
    float padding3;
};

// 像素着色器输入结构体（从顶点着色器传递过来）
struct PSInput
{
    float4 position : SV_POSITION;  // 像素在屏幕空间的位置
    float3 worldPos : POSITION;     // 顶点在世界空间的位置
    float3 normal : NORMAL;         // 法线向量（世界空间，已归一化）
    float3 color : COLOR;           // 顶点颜色（用于混合材质颜色）
    float2 texCoord : TEXCOORD;     // 纹理坐标（UV坐标）
};

// ============================================================================
// PBR 辅助函数
// ============================================================================

// 法线分布函数（Normal Distribution Function）- GGX/Trowbridge-Reitz
// 描述微表面法线的分布
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    denom = 3.14159f * denom * denom;
    
    return num / denom;
}

// 几何函数（Geometry Function）- Schlick-GGX
// 描述微表面的自遮挡
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0f);
    float k = (r * r) / 8.0f;
    
    float num = NdotV;
    float denom = NdotV * (1.0f - k) + k;
    
    return num / denom;
}

// 几何遮挡函数（Geometry Obstruction）
float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// 菲涅尔方程（Fresnel Equation）- Schlick 近似
// 描述不同视角下的反射率
float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}

// 像素着色器主函数
// 对每个像素（片段）调用一次
// SV_TARGET 表示这是输出到渲染目标的颜色
float4 PS(PSInput input) : SV_TARGET
{
    // 从纹理采样颜色（如果纹理存在）
    float4 texColor = diffuseTexture.Sample(textureSampler, input.texCoord);
    
    // 使用纹理颜色作为基础颜色
    float3 finalAlbedo = texColor.rgb * albedo;
    
    // 归一化法线
    float3 N = normalize(input.normal);
    
    // 光源方向
    float3 L = normalize(-lightDirection);
    
    // 简单的Lambert漫反射
    float NdotL = max(dot(N, L), 0.0f);
    
    // 环境光（高强度，确保纹理总是可见）
    float3 ambient = finalAlbedo * 0.8f;
    
    // 漫反射光照
    float3 diffuse = finalAlbedo * lightColor * lightIntensity * NdotL * 0.6f;
    
    // 最终颜色 = 环境光 + 漫反射光照
    // 确保最终颜色至少是纹理颜色的75%
    float3 finalColor = max(ambient + diffuse, finalAlbedo * 0.75f);
    
    // 确保顶点颜色不会让颜色变黑
    // 使用max确保顶点颜色至少是50%亮度
    float3 vertexColor = max(input.color, float3(0.5f, 0.5f, 0.5f));
    finalColor *= vertexColor;
    
    // 最后确保最终颜色不会太暗（至少是纹理颜色的60%）
    finalColor = max(finalColor, finalAlbedo * 0.6f);
    
    return float4(finalColor, texColor.a);
}
