// CDLOD Terrain Pixel Shader
// 支持大规模地形渲染，包含基于高度的纹理混合和LOD调试可视化

// ============================================================================
// 纹理和采样器
// ============================================================================
Texture2D baseColorTexture : register(t0);
SamplerState textureSampler : register(s0);
Texture2D shadowMap : register(t1);  // Shadow Map
SamplerComparisonState shadowSampler : register(s1);  // Shadow Map采样器（PCF）

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
    
    // Shadow Map相关矩阵
    float4x4 lightView;       // 光源视图矩阵
    float4x4 lightProjection; // 光源投影矩阵
    float4x4 lightWorldViewProj; // 光源世界-视图-投影矩阵
};

// 地形调试参数常量缓冲区
cbuffer TerrainDebugBuffer : register(b3)
{
    float showLODDebug;       // 是否显示LOD调试颜色 (1.0 = 显示, 0.0 = 不显示)
    float showDepthDebug;     // 是否显示深度调试 (1.0 = 显示光源空间深度, 0.0 = 正常渲染)
    float showShadowDebug;    // 是否显示阴影调试 (1.0 = 显示黑白阴影图, 0.0 = 正常渲染)
    float paddingDebug3;
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
    // Shadow Map采样和阴影计算
    // ========================================================================
    float shadowFactor = 1.0;  // 1.0 = 不在阴影中，0.0 = 在阴影中
    
    // 将世界坐标转换到光源空间
    float4 lightSpacePos = mul(float4(input.worldPos, 1.0), lightWorldViewProj);
        // 直接转换到NDC空间（-1到1）
    // 注意：正交投影的lightSpacePos已经在NDC空间附近，只需要确保在[-1,1]范围内
    float3 ndcPos = lightSpacePos.xyz;

    // 可选：缩放和平移以更好地观察
    float2 uv = ndcPos.xy * 0.5 + 0.5;  // 转换到[0,1]范围
    float depth = ndcPos.z;  // 正交投影的深度通常是[0,1]或[-1,1]

    // 透视除法
    lightSpacePos.xyz /= lightSpacePos.w;
    
    // 转换到纹理坐标（0-1范围）
    // 对于正交投影，NDC坐标范围是[-1, 1]，需要转换到[0, 1]
    float2 shadowUV = lightSpacePos.xy * 0.5 + 0.5;
    shadowUV.y = 1.0 - shadowUV.y;  // 翻转Y轴（DirectX纹理坐标Y轴向下）
    
    // 检查是否在shadow map范围内（使用更宽松的范围检查，与角色着色器一致）
    if (shadowUV.x >= -0.1 && shadowUV.x <= 1.1 && shadowUV.y >= -0.1 && shadowUV.y <= 1.1)
    {
        // 深度值（在光源空间中的深度，对于正交投影，NDC的z值在[0, 1]范围内）
        float lightDepth = lightSpacePos.z;
        
        // 确保深度值在有效范围内
        if (lightDepth >= 0.0 && lightDepth <= 1.0)
        {
            // 添加深度偏移，减少阴影痤疮
            // 注意：深度偏移对于避免阴影痤疮很重要，但如果太大可能导致漏光
            // 与角色着色器使用相同的偏移值
            lightDepth -= 0.0005;
            lightDepth = max(lightDepth, 0.0);  // 确保深度值不为负
            
            // 限制UV在有效范围内
            shadowUV = saturate(shadowUV);
            
            // 使用PCF采样shadow map（3x3采样，9个样本）
            float shadowSum = 0.0;
            float texelSize = 1.0 / 4096.0;  // Shadow map分辨率
            
            for (int x = -1; x <= 1; ++x)
            {
                for (int y = -1; y <= 1; ++y)
                {
                    float2 offset = float2(x, y) * texelSize;
                    float2 sampleUV = shadowUV + offset;
                    // 确保采样UV在有效范围内
                    if (sampleUV.x >= 0.0 && sampleUV.x <= 1.0 && sampleUV.y >= 0.0 && sampleUV.y <= 1.0)
                    {
                        // SampleCmpLevelZero返回0.0（在阴影中）或1.0（不在阴影中）
                        shadowSum += shadowMap.SampleCmpLevelZero(shadowSampler, sampleUV, lightDepth);
                    }
                    else
                    {
                        // 边界外视为不在阴影中（返回1.0表示不在阴影中）
                        shadowSum += 1.0;
                    }
                }
            }
            
            // 计算平均值（固定9个样本，与角色着色器一致）
            shadowFactor = shadowSum / 9.0;
        }
    }
    
    // ========================================================================
    // 光照计算
    // ========================================================================
    // 光源方向（从表面指向光源）
    // lightDirection存储的是从表面指向光源的方向，直接用于光照计算
    float3 lightDir = normalize(lightDirection.xyz);
    float NdotL = max(dot(normal, lightDir), 0.0);
    
    // 环境光遮蔽（简化版，基于法线Y分量）
    float ao = saturate(normal.y * 0.5 + 0.5);
    
    // 环境光（基础亮度，即使在阴影中也应该有基础光照）
    // 使用更合理的环境光强度，确保阴影区域不会太暗
    float3 ambient = heightColor * 0.3;
    
    // 漫反射（应用阴影）
    // 调整光照计算：使用更合理的光照强度，避免过曝
    // shadowFactor=1时，ambient+diffuse总和应该接近heightColor但不超过
    // shadowFactor=0时，只有ambient，应该足够暗但仍然可见（不是纯黑）
    // 使用lerp在阴影和非阴影之间平滑过渡
    float3 litColor = heightColor * (0.3 + NdotL * lightIntensity * 0.4);  // 非阴影时的颜色
    float3 shadowColor = heightColor * 0.3;  // 阴影时的颜色（纯环境光）
    float3 finalColor = lerp(shadowColor, litColor, shadowFactor);
    
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
    // LOD调试可视化 - 使用顶点着色器传递的调试颜色
    // ========================================================================
    // 如果启用了LOD调试模式，显示LOD颜色；否则使用正常渲染
    if (showLODDebug > 0.5)
    {
        // LOD调试模式：使用顶点颜色（包含LOD级别信息）
        // LOD 0 = 绿色, LOD 1 = 蓝色, LOD 2 = 黄色, LOD 3 = 红色
        // Morphing时颜色会平滑过渡
        finalColor = lerp(finalColor, input.color, 0.7);  // 70%调试色 + 30%原色
        
        // 在morphing时增加亮度提示
        if (morphFactor > 0.01 && morphFactor < 0.99)
        {
            finalColor = lerp(finalColor, float3(1, 1, 1), morphFactor * 0.2);
        }
    }
    // 否则使用正常渲染（不显示LOD颜色）
    
    // ========================================================================
    // 调试选项：将地形渲染为光源空间下的深度值
    // ========================================================================
    if (showDepthDebug > 0.5)
    {
        // 将世界坐标转换到光源视图空间
        float4 lightViewPos = mul(float4(input.worldPos, 1.0), lightView);

        // lightViewPos.z 是光源视图空间中的深度值
        float depth = lightViewPos.z;

        // 显示原始深度值（带符号）
        // 正值显示为绿色，负值显示为红色，零显示为蓝色
        if (depth > 0.1) {
            // 正深度：绿色，亮度表示大小
            float intensity = saturate(depth / 100.0);
            return float4(0, intensity, 0, 1.0);
        } else if (depth < -0.1) {
            // 负深度：红色，亮度表示大小
            float intensity = saturate(-depth / 100.0);
            return float4(intensity, 0, 0, 1.0);
        } else {
            // 接近零的深度：蓝色
            return float4(0, 0, 1, 1.0);
        }
    }

    // ========================================================================
    // 阴影调试模式：将地形渲染为黑白阴影图
    // ========================================================================
    if (showShadowDebug > 0.5)
    {
        // 计算阴影因子（复用角色着色器的阴影计算逻辑）
        float shadowFactor = 1.0;  // 1.0 = 不在阴影中，0.0 = 在阴影中

        // 将世界坐标转换到光源空间
        float4 lightSpacePos = mul(float4(input.worldPos, 1.0), lightWorldViewProj);

        // 透视除法
        lightSpacePos.xyz /= lightSpacePos.w;

        // 转换到纹理坐标（0-1范围）
        float2 shadowUV = lightSpacePos.xy * 0.5 + 0.5;
        shadowUV.y = 1.0 - shadowUV.y;  // 翻转Y轴

        // 检查是否在shadow map范围内
        if (shadowUV.x >= -0.1 && shadowUV.x <= 1.1 && shadowUV.y >= -0.1 && shadowUV.y <= 1.1)
        {
            // 深度值
            float lightDepth = lightSpacePos.z;

            // 确保深度值在有效范围内
            if (lightDepth >= 0.0 && lightDepth <= 1.0)
            {
                // 添加深度偏移
                lightDepth -= 0.0005;

                // 限制UV在有效范围内
                shadowUV = saturate(shadowUV);

                // 使用PCF采样shadow map（3x3采样，9个样本）
                float shadowSum = 0.0;
                float texelSize = 1.0 / 4096.0;  // Shadow map分辨率

                for (int x = -1; x <= 1; ++x)
                {
                    for (int y = -1; y <= 1; ++y)
                    {
                        float2 offset = float2(x, y) * texelSize;
                        float2 sampleUV = shadowUV + offset;
                        // 确保采样UV在有效范围内
                        if (sampleUV.x >= 0.0 && sampleUV.x <= 1.0 && sampleUV.y >= 0.0 && sampleUV.y <= 1.0)
                        {
                            shadowSum += shadowMap.SampleCmpLevelZero(shadowSampler, sampleUV, lightDepth);
                        }
                        else
                        {
                            // 边界外视为不在阴影中
                            shadowSum += 1.0;
                        }
                    }
                }

                shadowFactor = shadowSum / 9.0;  // 9个样本的平均值
            }
        }

        // 黑白阴影图：非阴影部分白色，阴影区域黑色
        if (shadowFactor > 0.5) {
            return float4(1.0, 1.0, 1.0, 1.0); // 白色：非阴影
        } else {
            return float4(0.0, 0.0, 0.0, 1.0); // 黑色：阴影
        }
    }

    // ========================================================================
    // 正常渲染模式
    // ========================================================================
    return float4(saturate(finalColor), 1.0);
}
