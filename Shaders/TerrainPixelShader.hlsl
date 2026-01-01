// CDLOD Terrain Pixel Shader
// 支持大规模地形渲染，包含基于高度的纹理混合和LOD调试可视化

// ============================================================================
// 纹理和采样器
// ============================================================================
Texture2D baseColorTexture : register(t0);
SamplerState textureSampler : register(s0);

// ============================================================================
// 常量缓冲区
// ============================================================================
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

// ============================================================================
// 像素着色器输入
// ============================================================================
struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal : NORMAL;
    float3 color : COLOR;
    float2 texCoord : TEXCOORD0;
    float2 debugInfo : TEXCOORD1;   // x: lodLevel, y: morphFactor
};

// ============================================================================
// LOD调试颜色（10级LOD）
// ============================================================================
static const float3 lodColors[10] = {
    float3(1.0, 0.0, 0.0),   // LOD 0: 红色（最高细节）
    float3(1.0, 0.5, 0.0),   // LOD 1: 橙色
    float3(1.0, 1.0, 0.0),   // LOD 2: 黄色
    float3(0.5, 1.0, 0.0),   // LOD 3: 黄绿色
    float3(0.0, 1.0, 0.0),   // LOD 4: 绿色
    float3(0.0, 1.0, 0.5),   // LOD 5: 青绿色
    float3(0.0, 1.0, 1.0),   // LOD 6: 青色
    float3(0.0, 0.5, 1.0),   // LOD 7: 天蓝色
    float3(0.0, 0.0, 1.0),   // LOD 8: 蓝色
    float3(0.5, 0.0, 1.0)    // LOD 9: 紫色（最低细节）
};

// ============================================================================
// 主着色器函数
// ============================================================================
float4 PS(PSInput input) : SV_TARGET
{
    // 获取调试信息
    float lodLevel = input.debugInfo.x;
    float morphFactor = input.debugInfo.y;
    
    // 法线归一化
    float3 normal = normalize(input.normal);
    
    // 世界空间高度（用于基于高度的着色）
    float height = input.worldPos.y;
    
    // ========================================================================
    // 基于高度和坡度的地形着色
    // 适配大规模地形（400米高差）
    // ========================================================================
    
    // 高度归一化（假设地形高度范围 0-400米）
    float normalizedHeight = saturate(height / 400.0);
    
    // 坡度计算（0=平地，1=垂直）
    float slope = 1.0 - normal.y;
    
    // 定义各层颜色
    float3 waterColor = float3(0.15, 0.35, 0.55);      // 深蓝色（水域/低洼）
    float3 beachColor = float3(0.76, 0.70, 0.50);      // 沙色（海滩）
    float3 grassColor = float3(0.22, 0.45, 0.18);      // 深绿色（草地）
    float3 forestColor = float3(0.15, 0.35, 0.12);     // 森林绿
    float3 rockColor = float3(0.45, 0.42, 0.38);       // 岩石灰
    float3 snowColor = float3(0.95, 0.95, 0.98);       // 雪白色
    float3 cliffColor = float3(0.35, 0.32, 0.28);      // 悬崖深色
    
    // 基于高度的颜色混合
    float3 heightColor;
    
    if (normalizedHeight < 0.02)
    {
        // 水域/低洼区域
        heightColor = waterColor;
    }
    else if (normalizedHeight < 0.05)
    {
        // 海滩过渡
        float t = (normalizedHeight - 0.02) / 0.03;
        heightColor = lerp(waterColor, beachColor, smoothstep(0.0, 1.0, t));
    }
    else if (normalizedHeight < 0.15)
    {
        // 海滩到草地
        float t = (normalizedHeight - 0.05) / 0.10;
        heightColor = lerp(beachColor, grassColor, smoothstep(0.0, 1.0, t));
    }
    else if (normalizedHeight < 0.40)
    {
        // 草地到森林
        float t = (normalizedHeight - 0.15) / 0.25;
        heightColor = lerp(grassColor, forestColor, smoothstep(0.0, 1.0, t));
    }
    else if (normalizedHeight < 0.60)
    {
        // 森林到岩石
        float t = (normalizedHeight - 0.40) / 0.20;
        heightColor = lerp(forestColor, rockColor, smoothstep(0.0, 1.0, t));
    }
    else if (normalizedHeight < 0.80)
    {
        // 岩石区域
        heightColor = rockColor;
    }
    else
    {
        // 雪线以上
        float t = (normalizedHeight - 0.80) / 0.20;
        heightColor = lerp(rockColor, snowColor, smoothstep(0.0, 1.0, t));
    }
    
    // 坡度影响：陡峭处显示岩石/悬崖
    float slopeFactor = smoothstep(0.3, 0.6, slope);
    heightColor = lerp(heightColor, cliffColor, slopeFactor * 0.8);
    
    // 高海拔的陡坡显示雪
    if (normalizedHeight > 0.70 && slope > 0.2)
    {
        float snowOnSlope = smoothstep(0.2, 0.5, slope) * smoothstep(0.70, 0.85, normalizedHeight);
        heightColor = lerp(heightColor, snowColor * 0.9, snowOnSlope * 0.5);
    }
    
    // ========================================================================
    // 光照计算
    // ========================================================================
    float3 lightDir = normalize(-lightDirection.xyz);
    float NdotL = max(dot(normal, lightDir), 0.0);
    
    // 环境光遮蔽（简化版，基于法线Y分量）
    float ao = saturate(normal.y * 0.5 + 0.5);
    
    // 环境光
    float3 ambient = heightColor * 0.35 * ao;
    
    // 漫反射
    float3 diffuse = heightColor * NdotL * lightIntensity * 0.65;
    
    // 合成颜色
    float3 finalColor = ambient + diffuse;
    
    // ========================================================================
    // 大气透视/雾效果（可选，适配大规模地形）
    // ========================================================================
    float distanceToCamera = length(input.worldPos - cameraPosition.xyz);
    float fogStart = 1000.0;   // 雾开始距离（米）
    float fogEnd = 8000.0;     // 雾结束距离（米）
    float fogFactor = saturate((distanceToCamera - fogStart) / (fogEnd - fogStart));
    
    // 雾颜色（天蓝色）
    float3 fogColor = float3(0.6, 0.75, 0.9);
    finalColor = lerp(finalColor, fogColor, fogFactor * 0.7);
    
    // ========================================================================
    // LOD调试可视化（可通过常量缓冲区控制开关）
    // ========================================================================
    // 取消下面的注释以启用LOD调试颜色
    /*
    int lodIndex = clamp((int)lodLevel, 0, 9);
    float3 lodDebugColor = lodColors[lodIndex];
    
    // 混合LOD颜色（50%调试色 + 50%原色）
    finalColor = lerp(finalColor, lodDebugColor, 0.5);
    
    // 在LOD边界处显示morphing效果（白色边缘）
    if (morphFactor > 0.01 && morphFactor < 0.99)
    {
        finalColor = lerp(finalColor, float3(1, 1, 1), morphFactor * 0.3);
    }
    */
    
    // ========================================================================
    // 输出
    // ========================================================================
    return float4(saturate(finalColor), 1.0);
}
