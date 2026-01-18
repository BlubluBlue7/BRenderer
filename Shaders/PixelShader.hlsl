// Pixel Shader - 像素着色器（PBR 版本）
// 作用：实现基于物理的渲染（Physically Based Rendering）
// 使用 Cook-Torrance BRDF 模型

// 纹理和采样器
Texture2D baseColorTexture : register(t0);  // BaseColor纹理（Albedo）
Texture2D normalTexture : register(t1);     // 法线贴图
Texture2D mraTexture : register(t2);        // MRA贴图（Metallic-Roughness-AO）
TextureCube environmentMap : register(t3);  // 环境贴图（HDR环境贴图，用于IBL）
Texture2D brdfLut : register(t4);          // BRDF查找表（用于镜面反射IBL）
Texture2D shadowMap : register(t5);        // Shadow Map
SamplerState textureSampler : register(s0);  // 纹理采样器
SamplerState iblSampler : register(s1);     // IBL采样器（支持mipmap和clamp）
SamplerComparisonState shadowSampler : register(s2);  // Shadow Map采样器（PCF）

// 常量缓冲区：光照参数
// 注意：HLSL中的结构必须与C++中的结构完全匹配（16字节对齐）
cbuffer LightBuffer : register(b1)
{
    // 注意：在HLSL中，float3会被对齐到16字节（相当于float4）
    // 为了与C++结构完全匹配，我们使用float4，只使用xyz分量
    
    float4 lightDirection;  // 光源方向（归一化的方向向量，指向光源）- 只使用xyz
    float lightIntensity;   // 光源强度 (4 bytes)
    float padding1a;        // 对齐填充 (4 bytes)
    float padding1b;        // 对齐填充 (4 bytes)
    float padding1c;        // 对齐填充 (4 bytes) -> 总共32字节（float需要对齐到16字节边界）
    
    float4 lightColor;      // 光源颜色和强度（RGB）- 只使用xyz，float4本身已对齐，无需额外padding
    
    float4 cameraPosition;  // 相机位置（世界空间）- 只使用xyz，float4本身已对齐，无需额外padding
    
    // PBR 材质参数
    float4 albedo;          // 反照率（基础颜色）- 只使用xyz，float4本身已对齐，无需额外padding
    float metallic;         // 金属度（0.0 = 非金属，1.0 = 金属）(4 bytes)
    float padding2d;        // 对齐填充 (4 bytes)
    float padding2e;        // 对齐填充 (4 bytes)
    float padding2f;        // 对齐填充 (4 bytes) -> 总共32字节（float需要对齐到16字节边界）
    
    float roughness;        // 粗糙度（0.0 = 完全光滑，1.0 = 完全粗糙）(4 bytes)
    float padding3a;        // 对齐填充 (4 bytes)
    float padding3b;        // 对齐填充 (4 bytes)
    float padding3c;        // 对齐填充 (4 bytes) -> 16 bytes total（float需要对齐到16字节边界）
    
    // 环境光参数
    float4 ambientColor;    // 环境光颜色 - 只使用xyz，float4本身已对齐，无需额外padding
    
    // Shadow Map相关矩阵
    float4x4 lightView;       // 光源视图矩阵
    float4x4 lightProjection; // 光源投影矩阵
    float4x4 lightWorldViewProj; // 光源世界-视图-投影矩阵
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

// IBL辅助函数：将方向向量转换为环境贴图的UV坐标
// 环境贴图通常是立方体贴图（CubeMap），使用方向向量直接采样
float3 SampleEnvironmentMap(float3 direction)
{
    // 使用立方体贴图采样，direction是归一化的方向向量
    return environmentMap.Sample(iblSampler, direction).rgb;
}

// IBL辅助函数：计算漫反射IBL（Irradiance）
// 使用Split-Sum Approximation，简化版本：直接采样环境贴图
float3 GetDiffuseIBL(float3 N)
{
    // 对于简化实现，直接采样环境贴图（法线方向）
    // 完整实现应该使用预计算的Irradiance Map
    return SampleEnvironmentMap(N);
}

// IBL辅助函数：计算镜面反射IBL（Prefiltered Environment + BRDF LUT）
// 使用Split-Sum Approximation（UE5方法）
float3 GetSpecularIBL(float3 N, float3 V, float3 F0, float roughness)
{
    // 计算反射向量
    float3 R = reflect(-V, N);
    
    // 采样环境贴图（当前实现只有一个mip级别，直接使用Sample）
    // 注意：完整实现应该使用预过滤的环境贴图（有多个mip级别），并使用SampleLevel
    // 当前简化实现直接采样环境贴图
    float3 prefilteredColor = environmentMap.Sample(iblSampler, R).rgb;
    
    // 从BRDF LUT采样（NdotV, roughness）
    // 注意：BRDF LUT是2D纹理，应该使用textureSampler而不是iblSampler
    float NdotV = max(dot(N, V), 0.0f);
    // 确保采样坐标在有效范围内 [0, 1]
    float2 brdfUV = float2(NdotV, roughness);
    float2 brdfSample = brdfLut.Sample(textureSampler, brdfUV).rg;
    
    // Split-Sum Approximation（标准公式）:
    // specularIBL = prefilteredColor * (F0 * scale + bias)
    // 其中 scale = brdfSample.x, bias = brdfSample.y
    // 注意：BRDF LUT已经包含了菲涅尔项的部分，所以这里只需要用F0来缩放
    float3 specularIBL = prefilteredColor * (F0 * brdfSample.x + brdfSample.y);
    
    // 调试：分别检查各个组成部分
    // return float4(prefilteredColor, 1.0f);  // 检查环境贴图采样
    // return float4(brdfSample.xy, 0, 1.0f);  // 检查BRDF LUT采样（R通道=scale, G通道=bias）
    // return float4(F0, 1.0f);  // 检查F0值
    // return float4(R * 0.5f + 0.5f, 1.0f);  // 检查反射向量（映射到[0,1]范围）
    // return float4(NdotV.xxx, 1.0f);  // 检查NdotV
    // return float4(roughness.xxx, 1.0f);  // 检查roughness
    
    return specularIBL;
}

// 像素着色器主函数
// 对每个像素（片段）调用一次
// SV_TARGET 表示这是输出到渲染目标的颜色
float4 PS(PSInput input) : SV_TARGET
{
    // 移除调试代码，恢复正常渲染
    
    // 修复Y轴颠倒：翻转V坐标（因为DirectX和某些图像格式的坐标系统不同）
    float2 flippedTexCoord = float2(input.texCoord.x, 1.0f - input.texCoord.y);
    
    // 从纹理采样BaseColor
    // 注意：如果BaseColor纹理使用SRGB格式，DX11会自动进行sRGB到线性的转换
    // 如果SRGB格式创建失败（回退到UNORM），采样得到的是sRGB值，需要手动转换
    float4 baseColorSample = baseColorTexture.Sample(textureSampler, flippedTexCoord);
    
    // 调试：可视化BaseColor纹理（取消注释以检查纹理是否正确加载）
    // 如果画面是白色的，说明纹理加载成功但可能是白色纹理
    // 如果画面是黑色或紫色，说明纹理没有正确绑定
    // return float4(baseColorSample.rgb, 1.0f);
    // 使用纹理颜色作为基础颜色
    // 如果SRV使用SRGB格式，DX11已经自动转换，baseColorSample已经是线性的
    // 如果SRV使用UNORM格式（SRGB失败回退），baseColorSample是sRGB值，需要手动转换
    // 为了简化，我们假设SRGB格式可用，直接使用baseColorSample
    // 如果SRGB失败，会在C++代码中输出警告，shader中暂时直接使用（可能会有轻微的颜色偏差）
    float3 finalAlbedo = baseColorSample.rgb * albedo.xyz;
    

    // ========================================================================
    // 从MRA贴图采样材质参数
    // ========================================================================
    float4 mraSample = mraTexture.Sample(textureSampler, flippedTexCoord);
    // MRA贴图通常：R = Metallic, G = Roughness, B = AO (Ambient Occlusion)
    float metallicFromTexture = mraSample.r;
    float roughnessFromTexture = mraSample.g;
    float aoFromTexture = mraSample.b;
    
    // ========================================================================
    // PBR 材质参数（纹理和常量混合）
    // ========================================================================
    float3 baseColor = finalAlbedo;
    // 使用MRA贴图中的值
    // 注意：metallic=0, roughness=0.5, ao=1.0 都是合法的材质值，不能用来判断是否是默认值
    // 判断是否是默认MRA贴图：检查是否所有通道都接近1.0（默认白色纹理）
    // 或者检查是否所有通道都接近默认MRA纹理的值（0, 0.5, 1.0）
    // 更合理的判断：如果MRA贴图是纯白色（所有通道都接近1.0），则可能是默认纹理
    bool isDefaultWhiteTexture = (metallicFromTexture > 0.98f && roughnessFromTexture > 0.98f && aoFromTexture > 0.98f);
    
    // 直接使用纹理值（除非是明显的默认白色纹理）
    // 对于真实的MRA贴图，即使metallic=0, roughness=0.5也是有效值
    float metal = isDefaultWhiteTexture ? metallic : metallicFromTexture;
    float rough = isDefaultWhiteTexture ? roughness : roughnessFromTexture;
    
    // 调试：可以临时取消注释来查看材质参数
    // return float4(metal, rough, aoFromTexture, 1.0f);  // R=金属度, G=粗糙度, B=AO
    
    // ========================================================================
    // 计算向量
    // ========================================================================
    // 使用顶点法线（世界空间）
    // 注意：法线贴图是切线空间的，不能直接和世界空间法线相加
    // 正确的做法需要切线空间转换（TBN矩阵），这里暂时禁用法线贴图以避免错误混合
    float3 N = normalize(input.normal);
    
    // TODO: 正确实现切线空间法线贴图（需要TBN矩阵）
    // 当前禁用法线贴图，因为法线贴图是切线空间，不能直接和世界空间法线混合
    // float4 normalSample = normalTexture.Sample(textureSampler, flippedTexCoord);
    // float3 normalMap = normalSample.rgb * 2.0f - 1.0f;
    // 需要TBN矩阵才能正确转换到世界空间
    
    // 视角方向（从表面指向相机）
    // 注意：cameraPosition是float4，只使用xyz分量
    float3 V = normalize(cameraPosition.xyz - input.worldPos);
    // 光源方向（从表面指向光源）
    // 注意：lightDirection是float4，只使用xyz分量
    // lightDirection存储的是从表面指向光源的方向，直接用于光照计算
    float3 L = normalize(lightDirection.xyz);  // 从表面指向光源的方向
    
    // 半角向量（视角方向和光源方向的中间向量）
    float3 H = normalize(V + L);
    
    // ========================================================================
    // 计算点积（用于后续计算）
    // ========================================================================
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);
    float NdotH = max(dot(N, H), 0.0f);
    float VdotH = max(dot(V, H), 0.0f);
    
    // 调试：可视化NdotL（取消注释以检查光源方向是否正确）
    // 如果画面几乎全黑，说明光源方向错误或NdotL太小
    // return float4(NdotL.xxx, 1.0f);
    
    // 如果表面背对光源，直接返回环境光
    // if (NdotL <= 0.0f)
    // {
    //     float3 ambientBase = baseColor * ambientColor.xyz;
    //     float3 ambient = lerp(ambientBase * 0.4f, ambientBase, aoFromTexture);
    //     // 确保背光面也有最小亮度
    //     ambient = max(ambient, baseColor * 0.3f);
    //     return float4(ambient, baseColorSample.a);
    // }
    
    // ========================================================================
    // Cook-Torrance BRDF 计算
    // ========================================================================
    
    // 1. 法线分布函数（D项）- GGX
    float D = DistributionGGX(N, H, rough);
    
    // 2. 几何函数（G项）- Smith
    float G = GeometrySmith(N, V, L, rough);
    
    // 3. 菲涅尔项（F项）- Schlick
    // F0 是基础反射率，对于非金属使用 0.04，对于金属使用 albedo
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), baseColor, metal);
    float3 F = fresnelSchlick(VdotH, F0);
    
    // ========================================================================
    // 计算镜面反射（Specular）
    // ========================================================================
    // Cook-Torrance BRDF 的镜面反射部分
    // specular = (D * G * F) / (4 * NdotV * NdotL)
    // 注意：对于金属材质，F项中的F0 = baseColor，所以镜面反射的颜色来自baseColor
    float3 numerator = D * G * F;
    float denominator = 4.0f * NdotV * NdotL + 0.001f; // 避免除零
    float3 specular = numerator / denominator;
    
    // 调试：可视化镜面反射（取消注释以检查金属材质的镜面反射）
    // 金属材质应该通过镜面反射显示baseColor的颜色
    // return float4(specular * 10.0f, 1.0f);  // 放大10倍以便观察
    
    // ========================================================================
    // 计算漫反射（Diffuse）
    // ========================================================================
    // 对于金属材质，没有漫反射（能量守恒）
    // 对于非金属材质，使用 Lambert 漫反射
    // 注意：菲涅尔项应该使用 NdotV 而不是 VdotH（标准PBR做法）
    float3 F_view = fresnelSchlick(NdotV, F0);  // 使用视角方向的菲涅尔项
    float3 kS = F_view; // 镜面反射的能量比例（基于视角方向）
    float3 kD = (1.0f - kS) * (1.0f - metal); // 漫反射的能量比例
    
    // 注意：kD只依赖于视角方向V，不依赖于光源方向L
    // kD = (1.0 - kS) * (1.0 - metal)
    // kS = F_view = fresnelSchlick(NdotV, F0)
    // 所以kD只依赖于：视角方向V（通过NdotV）、金属度metal、F0
    // 光源方向L只影响NdotL，用于计算最终光照强度，但不影响kD本身
    
    // 当视角接近平行表面时（NdotV接近0），F_view会接近1.0
    // 这会导致kS接近1.0，kD接近0，漫反射会变黑
    // 这是正常的物理现象（菲涅尔效应）：在边缘视角下，大部分光被反射而不是漫反射
    
    // 调试：可视化kD值（取消注释以检查）
    // kD受以下因素影响：
    // 1. 视角方向V（通过NdotV）：边缘视角时kD会变小
    // 2. 金属度metal：金属材质（metal=1）时kD=0
    // 3. F0（基础反射率）：影响菲涅尔项
    // return float4(kD.xxx, 1.0f);  // 查看kD值（白色=1.0，黑色=0.0）
    // return float4(NdotV.xxx, 1.0f);  // 查看NdotV值（视角角度，白色=垂直，黑色=平行）
    // return float4(NdotL.xxx, 1.0f);  // 查看NdotL值（光源方向，白色=垂直，黑色=平行）
    // return float4(metal.xxx, 1.0f);  // 查看金属度（白色=金属，黑色=非金属）
    // return float4(F_view.xxx, 1.0f);  // 查看菲涅尔项（白色=高反射，黑色=低反射）
    
    // Lambert 漫反射
    // 注意：在标准PBR实现中，Lambert漫反射通常不除以π
    // 除以π会导致颜色变暗，因为能量守恒已经在kD中处理了
    // 或者，如果除以π，radiance需要乘以π来补偿，但这里我们选择不除以π
    float3 diffuse = kD * baseColor;
    // 调试：可视化kD值（取消注释以检查）
    // 如果kD接近0（黑色），说明视角接近平行表面，菲涅尔效应导致大部分光被反射
    // 注意：kD应该只依赖于视角方向，不应该随光源旋转而变化
    // 如果kD随光源旋转变化，可能是视角方向V的计算有问题，或者相机位置在变化
    // return float4(kD.xxx, 1.0f);  // 查看kD值（白色=1.0，黑色=0.0）
    
    // 调试：可视化NdotV值（取消注释以检查视角角度）
    // NdotV接近0 = 视角平行表面，NdotV接近1 = 视角垂直表面
    // 如果NdotV随光源旋转变化，说明视角方向V的计算有问题
    // return float4(NdotV.xxx, 1.0f);  // 查看NdotV值（视角角度，白色=垂直，黑色=平行）
    
    // 调试：可视化视角方向V（检查是否变化）
    // 如果V随光源旋转变化，说明cameraPosition或worldPos的计算有问题
    // return float4(V * 0.5f + 0.5f, 1.0f);  // 将方向向量映射到[0,1]范围显示
    // ========================================================================
    // 组合光照
    // ========================================================================
    // ========================================================================
    // Shadow Map采样和阴影计算
    // ========================================================================
    float shadowFactor = 1.0;  // 1.0 = 不在阴影中，0.0 = 在阴影中
    
    // 将世界坐标转换到光源空间
    float4 lightSpacePos = mul(float4(input.worldPos, 1.0), lightWorldViewProj);
    
    // 透视除法
    lightSpacePos.xyz /= lightSpacePos.w;
    
    // 转换到纹理坐标（0-1范围）
    float2 shadowUV = lightSpacePos.xy * 0.5 + 0.5;
    shadowUV.y = 1.0 - shadowUV.y;  // 翻转Y轴
    
    // 检查是否在shadow map范围内（使用更宽松的范围检查）
    if (shadowUV.x >= -0.1 && shadowUV.x <= 1.1 && shadowUV.y >= -0.1 && shadowUV.y <= 1.1)
    {
        // 深度值（在光源空间中的深度，需要归一化到0-1范围）
        // 对于正交投影，深度值已经在0-1范围内
        float lightDepth = lightSpacePos.z;
        
        // 确保深度值在有效范围内
        if (lightDepth >= 0.0 && lightDepth <= 1.0)
        {
            // 添加深度偏移，减少阴影痤疮
            lightDepth -= 0.0005;
            
            // 限制UV在有效范围内
            shadowUV = saturate(shadowUV);
            
            // 使用PCF采样shadow map（3x3采样，9个样本）
            float shadowSum = 0.0;
            float texelSize = 1.0 / 2048.0;  // Shadow map分辨率
            
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
    
    // 直接光照 = (漫反射 + 镜面反射) * 光源颜色 * 光源强度 * NdotL * shadowFactor
    // 标准Cook-Torrance BRDF公式
    // 注意：对于金属材质，diffuse = 0（因为kD = 0），只有specular有贡献
    // specular的颜色来自baseColor（通过F0），所以金属材质通过镜面反射显示baseColor
    float3 radiance = lightColor.xyz * lightIntensity;
    float3 Lo = (diffuse + specular) * radiance * NdotL * shadowFactor;
    
    // 调试：可视化最终直接光照（取消注释以检查）
    
    // ========================================================================
    // IBL（Image-Based Lighting）- UE5风格
    // ========================================================================
    // 计算F0（基础反射率）
    float3 F0_IBL = lerp(float3(0.04f, 0.04f, 0.04f), baseColor, metal);
    
    // 漫反射IBL
    float3 diffuseIBL = GetDiffuseIBL(N);
    diffuseIBL *= baseColor;  // 乘以baseColor（非金属材质的反照率）
    // 镜面反射IBL
    float3 specularIBL = GetSpecularIBL(N, V, F0_IBL, rough);
    
    // 组合IBL：根据金属度混合
    // 非金属：diffuseIBL + specularIBL
    // 金属：只有specularIBL（因为金属没有漫反射）
    float3 ambientIBL = lerp(diffuseIBL + specularIBL, specularIBL, metal);
    
    // 应用AO（环境遮蔽）
    ambientIBL *= aoFromTexture;
    
    // 最终颜色 = IBL环境光 + 直接光照
    float3 finalColor = ambientIBL + Lo;
    
    // 应用顶点颜色（如果有）
    // finalColor *= input.color;
    
    // 色调映射（简单的 Reinhard 色调映射，防止过曝）
    finalColor = finalColor / (finalColor + float3(1.0f, 1.0f, 1.0f));
    
    // Gamma 校正（线性空间转 sRGB）
    // 注意：BaseColor贴图已经使用SRGB格式，DX11会自动转换sRGB到线性
    // 所以这里只需要将最终结果转换回sRGB用于显示
    finalColor = pow(finalColor, float3(1.0f / 2.2f, 1.0f / 2.2f, 1.0f / 2.2f));
    // 最终亮度增强（确保画面足够亮）
    // finalColor = finalColor * 1.3f;  // 整体亮度提升30%
    
    // 原始PBR渲染（角色正常渲染，不受阴影调试影响）
    return float4(finalColor, baseColorSample.a);
}
