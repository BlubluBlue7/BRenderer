// CDLOD Terrain Vertex Shader
// 实现基于 Filip Strugar CDLOD 论文的地形顶点着色器
// 支持 GPU Morphing 和边界缝合 (T-junction elimination)

// ============================================================================
// 常量缓冲区
// ============================================================================

// 变换矩阵常量缓冲区 (slot b0)
cbuffer TransformBuffer : register(b0)
{
    float4x4 world;
    float4x4 view;
    float4x4 projection;
    float4x4 worldViewProj;
};

// 地形常量缓冲区 (slot b2)
// 每个节点渲染前更新
cbuffer TerrainBuffer : register(b2)
{
    // terrainScale: x=worldSizeX, y=worldSizeZ, z=heightScale, w=unused
    float4 terrainScale;
    
    // terrainOffset: x=worldOffsetX (-sizeX/2), y=worldOffsetZ (-sizeZ/2), z=heightOffset, w=unused
    float4 terrainOffset;
    
    // morphParams: x=morphFactor, y=gridDimension, z=lodLevel, w=unused
    float4 morphParams;
    
    // nodeParams: x=nodeMinX, y=nodeMinZ, z=nodeSizeX, w=nodeSizeZ
    float4 nodeParams;
    
    // heightmapSize: x=width, y=height, z=1/width, w=1/height
    float4 heightmapSize;
};

// ============================================================================
// 高度图纹理和采样器
// ============================================================================
Texture2D<float> heightmapTexture : register(t0);
SamplerState heightmapSampler : register(s0);

// 法线图纹理和采样器（可选）
Texture2D normalmapTexture : register(t1);
SamplerState normalmapSampler : register(s1);

// ============================================================================
// 输入/输出结构
// ============================================================================
struct VSInput
{
    float3 position : POSITION;   // 局部网格坐标 (0-1范围)
    float3 normal : NORMAL;
    float3 color : COLOR;
    float2 texCoord : TEXCOORD;
};

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
// CDLOD Morphing 函数
// 
// CDLOD核心算法：
// 1. 每个LOD级别的网格分辨率是上一级别的2倍
// 2. 在LOD边界处，需要将高分辨率顶点"morph"到低分辨率位置
// 3. Morphing因子 (0-1) 控制过渡程度
// 
// 目标：消除LOD切换时的"popping"现象，实现平滑过渡
// ============================================================================

// 计算顶点在当前LOD下的morphed位置
// localPos: 0-1范围的局部坐标
// gridDim: 网格维度
// morphFactor: 0-1的morphing因子
float2 ComputeMorphedPosition(float2 localPos, float gridDim, float morphFactor)
{
    // 转换为网格坐标
    float2 gridPos = localPos * gridDim;
    
    // CDLOD morphing核心算法：
    // 在LOD级别N和N+1之间过渡时，LOD N中的某些顶点在LOD N+1中不存在
    // 这些顶点需要被"morph"到相邻的共存顶点位置
    //
    // 判断方法：检查顶点的网格坐标是否为2的倍数
    // - 如果是2的倍数：顶点在两个LOD中都存在，不需要morph
    // - 如果不是2的倍数：顶点只在当前LOD中存在，需要morph
    
    // frac(gridPos * 0.5) * 2.0 的结果：
    // - 偶数位置返回 0
    // - 奇数位置返回 1
    float2 fracPart = frac(gridPos * 0.5) * 2.0;
    
    // 计算morph偏移：奇数位置需要向偶数位置移动
    // floor(gridPos * 0.5 + 0.5) * 2 计算最近的偶数网格位置
    // 但更简单的方法是直接用fracPart作为偏移量
    float2 morphOffset = fracPart;
    
    // 应用morphing：随着morphFactor增加，顶点逐渐移动到偶数位置
    float2 morphedGridPos = gridPos - morphOffset * morphFactor;
    
    // 转换回0-1范围
    return morphedGridPos / gridDim;
}

// 从高度图采样高度值（使用世界坐标转UV）
float SampleHeightWorld(float2 worldXZ)
{
    // 将世界坐标转换到0-1的UV坐标
    // UV = (worldPos - worldOffset) / worldSize
    float2 uv;
    uv.x = (worldXZ.x - terrainOffset.x) / terrainScale.x;
    uv.y = (worldXZ.y - terrainOffset.y) / terrainScale.y;
    
    // 确保UV在有效范围内
    uv = saturate(uv);
    
    // 采样高度图（使用MipLevel 0以获得最高精度）
    float heightNormalized = heightmapTexture.SampleLevel(heightmapSampler, uv, 0);
    
    // 应用高度缩放和偏移
    return heightNormalized * terrainScale.z + terrainOffset.z;
}

// 计算法线（通过中心差分法采样相邻高度值）
float3 ComputeNormalWorld(float2 worldXZ)
{
    // 计算采样步长（基于高度图分辨率和世界大小）
    float stepX = terrainScale.x * heightmapSize.z;  // worldSizeX / heightmapWidth
    float stepZ = terrainScale.y * heightmapSize.w;  // worldSizeZ / heightmapHeight
    
    // 采用较大的步长以获得更平滑的法线
    stepX *= 2.0;
    stepZ *= 2.0;
    
    // 采样四个相邻点
    float hL = SampleHeightWorld(worldXZ + float2(-stepX, 0));  // Left
    float hR = SampleHeightWorld(worldXZ + float2(stepX, 0));   // Right
    float hD = SampleHeightWorld(worldXZ + float2(0, -stepZ));  // Down (negative Z)
    float hU = SampleHeightWorld(worldXZ + float2(0, stepZ));   // Up (positive Z)
    
    // 使用中心差分法计算法线
    // 切线向量：
    // T_x = (2*stepX, hR-hL, 0)
    // T_z = (0, hU-hD, 2*stepZ)
    // 法线 = T_x × T_z
    float3 normal;
    normal.x = (hL - hR);               // dH/dx (取反因为法线指向表面外)
    normal.y = 2.0 * stepX;             // 缩放因子
    normal.z = (hD - hU);               // dH/dz
    
    return normalize(normal);
}

// ============================================================================
// 顶点着色器主函数
// ============================================================================
PSInput VS(VSInput input)
{
    PSInput output;
    
    // 获取参数
    float morphFactor = morphParams.x;
    float gridDim = morphParams.y;
    float lodLevel = morphParams.z;
    
    float2 nodeOffset = nodeParams.xy;   // 节点在世界空间的最小坐标
    float2 nodeScale = nodeParams.zw;    // 节点在世界空间的大小
    
    // ========================================================================
    // Step 1: 获取局部坐标 (0-1范围)
    // ========================================================================
    float2 localPos = input.position.xz;
    
    // ========================================================================
    // Step 2: 应用CDLOD Morphing
    // ========================================================================
    // Morphing使顶点在LOD过渡时平滑移动到下一级别的位置
    float2 morphedLocalPos = ComputeMorphedPosition(localPos, gridDim, morphFactor);
    
    // ========================================================================
    // Step 3: 转换到世界空间XZ坐标
    // ========================================================================
    float2 worldXZ;
    worldXZ.x = nodeOffset.x + morphedLocalPos.x * nodeScale.x;
    worldXZ.y = nodeOffset.y + morphedLocalPos.y * nodeScale.y;
    
    // ========================================================================
    // Step 4: 从高度图采样Y坐标（高度）
    // ========================================================================
    float height = SampleHeightWorld(worldXZ);
    
    // ========================================================================
    // Step 5: 构建完整的世界空间位置
    // ========================================================================
    float3 worldPos;
    worldPos.x = worldXZ.x;
    worldPos.y = height;
    worldPos.z = worldXZ.y;
    
    // ========================================================================
    // Step 6: 计算世界空间法线
    // ========================================================================
    float3 normal;
    
    // 如果提供了法线图，从法线图采样；否则从高度图计算
    // 注意：法线图通常存储为切线空间法线，需要转换到世界空间
    // 这里简化处理：假设法线图已经是世界空间法线（或近似）
    float2 normalUV;
    normalUV.x = (worldXZ.x - terrainOffset.x) / terrainScale.x;
    normalUV.y = (worldXZ.y - terrainOffset.y) / terrainScale.y;
    normalUV = saturate(normalUV);
    
    // 尝试从法线图采样（如果存在）
    float4 normalmapSample = normalmapTexture.SampleLevel(normalmapSampler, normalUV, 0);
    
    // 如果法线图存在且有效（alpha通道不为0），使用法线图
    // 否则从高度图计算法线
    if (normalmapSample.a > 0.001)
    {
        // 法线图通常存储为 (0.5, 0.5, 1.0) 对应 (0, 0, 1) 法线
        // 需要转换：normal = (sample * 2.0 - 1.0)
        normal = normalize(normalmapSample.rgb * 2.0 - 1.0);
    }
    else
    {
        // 从高度图计算法线
        normal = ComputeNormalWorld(worldXZ);
    }
    
    // ========================================================================
    // Step 7: 变换到裁剪空间
    // ========================================================================
    output.position = mul(float4(worldPos, 1.0), worldViewProj);
    output.worldPos = worldPos;
    output.normal = normal;
    
    // ========================================================================
    // Step 8: 设置其他输出
    // ========================================================================
    output.color = input.color;
    
    // 纹理坐标：基于世界XZ位置计算，实现平铺
    float2 worldUV;
    worldUV.x = (worldXZ.x - terrainOffset.x) / terrainScale.x;
    worldUV.y = (worldXZ.y - terrainOffset.y) / terrainScale.y;
    output.texCoord = worldUV * 32.0;  // 32倍平铺（适应更大地形）
    
    // 调试信息：LOD级别和morphFactor
    output.debugInfo = float2(lodLevel, morphFactor);
    
    return output;
}
