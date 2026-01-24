// Water Pixel Shader
// 基础版本：简单的水体颜色和透明度

// ============================================================================
// Constant Buffers
// ============================================================================
cbuffer WaterConstantBuffer : register(b0)
{
    float4x4 world;
    float4x4 view;
    float4x4 projection;
    float4x4 worldViewProj;
    float3 cameraPosition;
    float time;              // 时间（用于未来动画）
    float waterLevel;         // 水位高度
    float3 waterColor;        // 水体颜色
    float transparency;       // 透明度
    float waveAmplitude;     // 波浪振幅
    float waveFrequency;     // 波浪频率
    float waveSpeed;         // 波浪速度
    float padding;           // 对齐填充
    float4 waterBounds;      // minX,minZ,maxX,maxZ（用于高度图UV映射）
    float2 terrainHeightParams; // x=heightScale, y=heightOffset
    float2 padding2;         // 额外填充：保持16字节对齐
};

// ============================================================================
// 光照常量缓冲区（复用现有的LightBuffer）
// ============================================================================
cbuffer LightBuffer : register(b1)
{
    float4 lightDirection;
    float lightIntensity;
    float padding1a;
    float padding1b;
    float padding1c;
    
    float4 lightColor;
    
    float4 cameraPositionLight;  // 相机位置（从LightBuffer）
    
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
    
    float4 lightPosition;
    
    float4x4 lightView;
    float4x4 lightProjection;
    float4x4 lightWorldViewProj;
};

// ============================================================================
// Terrain heightmap（用于裁剪水域，只在低处显示）
Texture2D heightmapTexture : register(t0);
SamplerState heightmapSampler : register(s0);

// Shadow Map（如果可用）
// ============================================================================
Texture2D shadowMap : register(t1);  // 注意：t0可能被其他纹理占用，使用t1
SamplerComparisonState shadowSampler : register(s1);  // 注意：s0可能被其他采样器占用，使用s1

// ============================================================================
// Input/Output Structures
// ============================================================================
struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float3 viewDir : TEXCOORD1;  // 视线方向
};

// ============================================================================
// 主函数
// ============================================================================
float4 PS(PSInput input) : SV_TARGET
{
    // ========================================================================
    // 水域裁剪：只在地形高度低于水面（含波浪后的水面高度）时绘制
    // ========================================================================
    float2 minXZ = waterBounds.xy;
    float2 maxXZ = waterBounds.zw;
    float2 sizeXZ = maxXZ - minXZ;
    float2 uv = (input.worldPos.xz - minXZ) / max(sizeXZ, 1e-5.xx);
    uv = saturate(uv);

    float height01 = heightmapTexture.SampleLevel(heightmapSampler, float2(uv.x, uv.y), 0).r;
    float terrainHeight = height01 * terrainHeightParams.x + terrainHeightParams.y;

    // 水深（世界单位）：>0 表示该像素处水面高于地形
    float waterDepth = input.worldPos.y - terrainHeight;

    // 如果地形高于水面则丢弃像素，不盖住高处地形
    clip(waterDepth - 0.001f);

    // 归一化法线
    float3 normal = normalize(input.normal);
    
    // 归一化视线方向
    float3 viewDir = normalize(input.viewDir);
    
    // ========================================================================
    // 基础光照计算
    // ========================================================================
    float3 lightDir = normalize(lightDirection.xyz);
    float NdotL = max(dot(normal, lightDir), 0.0f);
    
    // 环境光
    float3 ambient = waterColor * ambientColor.rgb * 0.3f;
    
    // 漫反射
    float3 diffuse = waterColor * lightColor.rgb * NdotL * lightIntensity * 0.5f;
    
    // 基础颜色
    float3 baseColor = ambient + diffuse;
    
    // ========================================================================
    // 简单的菲涅尔效果（基于视角）
    // ========================================================================
    float fresnel = 1.0f - max(dot(normal, viewDir), 0.0f);
    fresnel = pow(fresnel, 2.0f);  // 增强菲涅尔效果
    
    // 菲涅尔影响：视角越倾斜，反射越强（颜色更亮）
    float3 fresnelColor = lerp(baseColor, baseColor * 1.5f, fresnel * 0.3f);
    
    // ========================================================================
    // 阴影计算（如果Shadow Map可用）
    // ========================================================================
    float shadowFactor = 1.0f;
    
    // 将世界坐标转换到光源空间
    float4 lightSpacePos = mul(float4(input.worldPos, 1.0f), lightWorldViewProj);
    lightSpacePos.xyz /= lightSpacePos.w;
    
    // 转换到纹理坐标（0-1范围）
    float2 shadowUV = lightSpacePos.xy * 0.5f + 0.5f;
    shadowUV.y = 1.0f - shadowUV.y;  // 翻转Y轴
    
    // 检查是否在shadow map范围内
    if (shadowUV.x >= -0.1f && shadowUV.x <= 1.1f && shadowUV.y >= -0.1f && shadowUV.y <= 1.1f)
    {
        float lightDepth = lightSpacePos.z;
        
        if (lightDepth >= 0.0f && lightDepth <= 1.0f)
        {
            // 添加深度偏移
            lightDepth -= 0.0005f;
            lightDepth = max(lightDepth, 0.0f);
            
            // 限制UV在有效范围内
            shadowUV = saturate(shadowUV);
            
            // 简单的PCF采样（3x3）
            float shadowSum = 0.0f;
            float texelSize = 1.0f / 4096.0f;  // Shadow map分辨率
            
            for (int x = -1; x <= 1; ++x)
            {
                for (int y = -1; y <= 1; ++y)
                {
                    float2 offset = float2(x, y) * texelSize;
                    float2 sampleUV = shadowUV + offset;
                    
                    if (sampleUV.x >= 0.0f && sampleUV.x <= 1.0f && 
                        sampleUV.y >= 0.0f && sampleUV.y <= 1.0f)
                    {
                        shadowSum += shadowMap.SampleCmpLevelZero(shadowSampler, sampleUV, lightDepth);
                    }
                    else
                    {
                        shadowSum += 1.0f;
                    }
                }
            }
            
            shadowFactor = shadowSum / 9.0f;
        }
    }
    
    // 应用阴影
    float3 finalColor = lerp(fresnelColor * 0.5f, fresnelColor, shadowFactor);
    
    // ========================================================================
    // 岸线渐变 + 泡沫
    // ========================================================================
    // 浅水范围（越大过渡越宽）
    const float shallowRange = 2.5f;
    float shallow01 = saturate(1.0f - (waterDepth / shallowRange)); // 1=很浅(岸边)，0=深水

    // 泡沫只出现在非常浅的区域
    const float foamStart = 0.9f;  // 更靠近岸（shallow01接近1）
    const float foamEnd   = 0.55f; // 往深水方向衰减
    float foamMask = smoothstep(foamEnd, foamStart, shallow01);

    // 加一点噪声，让泡沫不那么“完美一圈”
    float n = sin(dot(input.worldPos.xz, float2(12.9898, 78.233)) + time * 1.7) * 43758.5453;
    n = frac(n);
    foamMask *= lerp(0.6, 1.0, n);

    float3 foamColor = float3(0.92, 0.95, 1.0);
    finalColor = lerp(finalColor, foamColor, foamMask * 0.65);

    // 浅水更亮一点（沙滩/浅滩感）
    float3 shallowTint = float3(0.10, 0.25, 0.35);
    finalColor = lerp(finalColor, finalColor + shallowTint * 0.25, shallow01 * 0.8);

    // 透明度：浅水更透明，深水更不透明；泡沫区域更不透明
    float alpha = transparency;
    alpha = lerp(alpha, alpha * 0.35, shallow01);      // 岸边更透明
    alpha = lerp(alpha, max(alpha, 0.85), foamMask);   // 泡沫更不透明

    return float4(saturate(finalColor), saturate(alpha));
}

