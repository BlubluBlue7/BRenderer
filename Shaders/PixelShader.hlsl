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
    // 归一化向量
    float3 N = normalize(input.normal);           // 法线
    float3 V = normalize(cameraPosition - input.worldPos);  // 视线方向
    float3 L = normalize(-lightDirection);         // 光源方向
    float3 H = normalize(V + L);                   // 半角向量
    
    // 从纹理采样颜色（如果纹理存在）
    float4 texColor = diffuseTexture.Sample(textureSampler, input.texCoord);
    
    // 使用纹理颜色作为主要颜色源
    // 纹理颜色与albedo相乘：纹理颜色 * 材质颜色
    // albedo默认是白色(1,1,1)，所以纹理颜色会直接显示
    // 如果albedo不是白色，可以用来调整纹理的整体色调
    float3 finalAlbedo = texColor.rgb * albedo;
    
    // 计算基础反射率 F0
    // 对于非金属，F0 约为 0.04
    // 对于金属，F0 等于反照率
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), finalAlbedo, metallic);
    
    // ========================================================================
    // Cook-Torrance BRDF 计算
    // ========================================================================
    
    // 1. 法线分布函数（D）
    float D = DistributionGGX(N, H, roughness);
    
    // 2. 几何函数（G）
    float G = GeometrySmith(N, V, L, roughness);
    
    // 3. 菲涅尔方程（F）
    float3 F = fresnelSchlick(max(dot(H, V), 0.0f), F0);
    
    // 4. 计算 Cook-Torrance 镜面反射项
    float NdotL = max(dot(N, L), 0.0f);
    float NdotV = max(dot(N, V), 0.0f);
    
    float3 numerator = D * G * F;
    float denominator = 4.0f * NdotV * NdotL + 0.001f;  // 防止除零
    float3 specular = numerator / denominator;
    
    // 5. 计算漫反射项
    // 能量守恒：镜面反射 + 漫反射 = 1
    float3 kS = F;  // 镜面反射系数
    float3 kD = (1.0f - kS) * (1.0f - metallic);  // 漫反射系数（金属不产生漫反射）
    
    // Lambertian 漫反射
    float3 diffuse = kD * finalAlbedo / 3.14159f;
    
    // ========================================================================
    // 组合光照
    // ========================================================================
    
    // 直接光照：漫反射 + 镜面反射
    float3 Lo = (diffuse + specular) * lightColor * lightIntensity * NdotL;
    
    // 环境光（简化的 IBL，这里使用常数）
    float3 ambient = ambientColor * finalAlbedo * 0.1f;
    
    // 最终颜色
    float3 finalColor = ambient + Lo;
    
    // 色调映射（简单的 Reinhard 色调映射）
    finalColor = finalColor / (finalColor + float3(1.0f, 1.0f, 1.0f));
    
    // Gamma 校正
    finalColor = pow(finalColor, float3(1.0f / 2.2f, 1.0f / 2.2f, 1.0f / 2.2f));
    
    // 混合顶点颜色（如果提供）
    finalColor *= input.color;
    
    return float4(finalColor, 1.0f);
}
