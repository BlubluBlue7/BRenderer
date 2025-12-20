// Pixel Shader - 像素着色器（片段着色器）
// 作用：实现 Phong 光照模型（最简单的 BRDF），计算每个像素的最终颜色

// 常量缓冲区：光照参数
cbuffer LightBuffer : register(b1)
{
    float3 lightDirection;  // 光源方向（归一化的方向向量，指向光源）
    float3 lightColor;      // 光源颜色（RGB）
    float3 ambientColor;    // 环境光颜色
    float specularPower;    // 镜面反射强度（Phong 指数）
    float3 cameraPosition; // 相机位置（世界空间）
};

// 像素着色器输入结构体（从顶点着色器传递过来）
struct PSInput
{
    float4 position : SV_POSITION;  // 像素在屏幕空间的位置
    float3 worldPos : POSITION;     // 顶点在世界空间的位置
    float3 normal : NORMAL;         // 法线向量（世界空间，已归一化）
    float3 color : COLOR;           // 材质颜色
};

// 像素着色器主函数
// 对每个像素（片段）调用一次
// SV_TARGET 表示这是输出到渲染目标的颜色
float4 PS(PSInput input) : SV_TARGET
{
    // 确保法线已归一化
    float3 N = normalize(input.normal);
    
    // 归一化光源方向（指向光源）
    float3 L = normalize(-lightDirection);
    
    // ========================================================================
    // Phong 光照模型计算
    // ========================================================================
    
    // 1. 环境光（Ambient）
    // 模拟全局光照，即使没有直接光照也有基础亮度
    float3 ambient = ambientColor * input.color;
    
    // 2. 漫反射（Diffuse）
    // 使用 Lambert 定律：光照强度与表面法线和光源方向的点积成正比
    float NdotL = max(dot(N, L), 0.0f);  // 点积，限制在 [0, 1] 范围
    float3 diffuse = lightColor * input.color * NdotL;
    
    // 3. 镜面反射（Specular）
    // 计算视线方向和反射方向的点积
    float3 V = normalize(cameraPosition - input.worldPos);  // 视线方向（从表面指向相机）
    float3 R = reflect(-L, N);  // 反射方向（光源方向关于法线的反射）
    float RdotV = max(dot(R, V), 0.0f);  // 反射方向与视线方向的点积
    
    // Phong 镜面反射：使用指数函数控制高光范围
    float specular = pow(RdotV, specularPower);
    float3 specularColor = lightColor * specular;
    
    // ========================================================================
    // 组合所有光照分量
    // 最终颜色 = 环境光 + 漫反射 + 镜面反射
    // ========================================================================
    float3 finalColor = ambient + diffuse + specularColor;
    
    // 限制颜色值在 [0, 1] 范围，并转换为 float4
    finalColor = saturate(finalColor);
    return float4(finalColor, 1.0f);
}
