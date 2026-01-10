// TerrainNew Vertex Shader with CDLOD Morphing
// Supports smooth LOD transitions using vertex morphing

// ============================================================================
// Constant Buffers
// ============================================================================
cbuffer TransformBuffer : register(b0)
{
    float4x4 world;
    float4x4 view;
    float4x4 projection;
    float4x4 worldViewProj;
};

// Terrain Chunk Constant Buffer (register b2)
cbuffer TerrainChunkBuffer : register(b2)
{
    float4 chunkParams;      // x=chunkDistToCamera, y=lodLevel, z=morphStartDist, w=morphEndDist
    float4 chunkBounds;      // x=minX, y=minZ, z=maxX, w=maxZ
    float4 terrainParams;    // x=worldSizeX, y=worldSizeZ, z=gridWidth-1, w=gridHeight-1
    float4 heightParams;     // x=heightScale, y=heightOffset, z=1/(gridWidth-1), w=1/(gridHeight-1)
    float4 cameraParams;     // x=cameraPosX, y=cameraPosY, z=cameraPosZ, w=unused
};

// ============================================================================
// Textures and Samplers
// ============================================================================
Texture2D heightmapTexture : register(t0);     // 高度图纹理
SamplerState heightmapSampler : register(s0);  // 高度图采样器

// ============================================================================
// Input/Output Structures
// ============================================================================
struct VSInput
{
    float3 position : POSITION;   // 世界空间位置（已包含高度）
    float3 normal : NORMAL;
    float3 color : COLOR;
    float2 texCoord : TEXCOORD;   // 全局网格坐标（用于morphing）
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal : NORMAL;
    float3 color : COLOR;
    float2 texCoord : TEXCOORD0;
    float2 debugInfo : TEXCOORD1;   // x: lodLevel, y: morphFactor (用于像素着色器调试)
};

// ============================================================================
// CDLOD Morphing Functions
// ============================================================================

// 基于距离计算morphFactor（简化版，使用传入的morphStart和morphEnd）
float ComputeMorphFactorFromDistance(float distance, float morphStart, float morphEnd)
{
    // 如果距离小于morphing开始点，不需要morphing
    if (distance <= morphStart)
        return 0.0f;
    
    // 如果距离大于morphing结束点，完全morphed
    if (distance >= morphEnd)
        return 1.0f;
    
    // 在morphing范围内，线性插值
    return saturate((distance - morphStart) / (morphEnd - morphStart));
}

// 从高度图采样高度值（使用归一化坐标）
float SampleHeight(float2 normalizedPos)
{
    // normalizedPos范围 [0,1]
    // 采样高度图（归一化高度值 [0,1]）
    float heightValue = heightmapTexture.SampleLevel(heightmapSampler, normalizedPos, 0).r;
    
    // 转换为世界空间高度
    float worldHeight = heightValue * heightParams.x + heightParams.y;
    
    return worldHeight;
}

// 从全局网格坐标采样高度
float SampleHeightFromGridPos(float2 globalGridPos)
{
    // 将全局网格坐标转换为归一化坐标 [0,1]
    float normX = globalGridPos.x * heightParams.z;  // globalGridPos.x / (gridWidth-1)
    float normZ = globalGridPos.y * heightParams.w;  // globalGridPos.y / (gridHeight-1)
    
    return SampleHeight(float2(normX, normZ));
}

// 从高度图计算法线（基于中心差分）
float3 ComputeNormalFromHeightmap(float2 normalizedPos)
{
    // 计算采样偏移量（使用高度图的像素大小）
    float2 texelSize = float2(heightParams.z, heightParams.w);  // 1/(gridWidth-1), 1/(gridHeight-1)
    
    // 中心差分采样周围4个点的高度
    float hLeft  = SampleHeight(float2(normalizedPos.x - texelSize.x, normalizedPos.y));
    float hRight = SampleHeight(float2(normalizedPos.x + texelSize.x, normalizedPos.y));
    float hDown  = SampleHeight(float2(normalizedPos.x, normalizedPos.y - texelSize.y));
    float hUp    = SampleHeight(float2(normalizedPos.x, normalizedPos.y + texelSize.y));
    
    // 计算世界空间中的水平距离
    float worldStepX = terrainParams.x * texelSize.x * 2.0f;  // worldSizeX * 2 * texelSize
    float worldStepZ = terrainParams.y * texelSize.y * 2.0f;  // worldSizeZ * 2 * texelSize
    
    // 计算切线向量
    float3 tangentX = float3(worldStepX, hRight - hLeft, 0.0f);
    float3 tangentZ = float3(0.0f, hUp - hDown, worldStepZ);
    
    // 叉积得到法线
    float3 normal = cross(tangentZ, tangentX);
    
    return normalize(normal);
}

// 从全局网格坐标计算法线
float3 ComputeNormalFromGridPos(float2 globalGridPos)
{
    float normX = globalGridPos.x * heightParams.z;
    float normZ = globalGridPos.y * heightParams.w;
    return ComputeNormalFromHeightmap(float2(normX, normZ));
}

// CDLOD Morphing核心算法（使用round保证拓扑一致性）
// 适配预生成的LOD顶点缓冲区
// globalGridPos: 全局网格坐标（来自当前LOD的顶点缓冲区）
// lodLevel: 当前LOD级别
// morphFactor: morphing因子 (0-1)
float2 ComputeMorphedGlobalGridPosition(float2 globalGridPos, float lodLevel, float morphFactor)
{
    if (morphFactor <= 0.001f)
        return globalGridPos;  // 不需要morphing
    
    // 关键理解：
    // - 当前顶点来自LOD N的缓冲区，步长是 2^N
    // - 要morph到LOD N+1，步长是 2^(N+1)
    // - LOD N中的顶点分两类：
    //   1. "共存顶点"：在LOD N+1中也存在（坐标是2^(N+1)的倍数）→ 不morph
    //   2. "多余顶点"：在LOD N+1中不存在 → 需要morph到父LOD中唯一确定的顶点
    
    // 当前LOD的步长
    float currentLODStep = pow(2.0f, lodLevel);
    
    // 下一级LOD的步长（目标）
    float nextLODStep = pow(2.0f, lodLevel + 1.0f);
    
    // 判断当前顶点是否是"共存顶点"
    // 方法：检查坐标是否是nextLODStep的倍数
    // 使用fmod而不是frac，更精确
    float2 remainder = fmod(globalGridPos, nextLODStep);
    
    // 如果余数接近0或接近nextLODStep，说明是共存顶点，不需要morph
    bool isSharedX = (remainder.x < 0.5f) || (remainder.x > nextLODStep - 0.5f);
    bool isSharedZ = (remainder.y < 0.5f) || (remainder.y > nextLODStep - 0.5f);
    
    if (isSharedX && isSharedZ)
        return globalGridPos;  // 共存顶点，不morph
    
    // ========================================================================
    // CDLOD核心：多余顶点snap到最近的父LOD网格点
    // ========================================================================
    // 使用round四舍五入到最近的网格点，这样可以保证：
    //   1. 每个多余顶点都移动到距离最近的父LOD顶点
    //   2. 移动距离最小化，减少视觉变形
    //   3. 相邻chunk边界处理一致（同一顶点会snap到同一目标）
    // ========================================================================
    float2 targetPos = round(globalGridPos / nextLODStep) * nextLODStep;
    
    // 在原始位置和目标位置之间插值
    float2 morphedGridPos = lerp(globalGridPos, targetPos, morphFactor);
    
    return morphedGridPos;
}

// ============================================================================
// Vertex Shader
// ============================================================================
PSInput VS(VSInput input)
{
    PSInput output;
    
    // 获取参数
    float lodLevel = chunkParams.y;
    float morphStart = chunkParams.z;
    float morphEnd = chunkParams.w;
    
    // 获取相机位置
    float3 cameraPos = float3(cameraParams.x, cameraParams.y, cameraParams.z);
    
    // 计算每个顶点到相机的距离（使用XZ平面距离，忽略高度差）
    // 这样可以实现更平滑的过渡：靠近相机的顶点morph程度小，远离相机的顶点morph程度大
    float2 vertexXZ = float2(input.position.x, input.position.z);
    float2 cameraXZ = float2(cameraPos.x, cameraPos.z);
    float vertexDistToCamera = length(vertexXZ - cameraXZ);
    
    // 使用每个顶点自己的距离计算morphFactor
    float morphFactor = ComputeMorphFactorFromDistance(vertexDistToCamera, morphStart, morphEnd);
    
    float3 worldPos = input.position;
    float3 worldNormal = input.normal;
    
    // 如果morphFactor > 0，应用CDLOD morphing
    if (morphFactor > 0.001f)
    {
        // texCoord存储的是全局网格坐标
        float2 globalGridPos = input.texCoord;
        
        // 计算morphed的全局网格坐标
        float2 morphedGridPos = ComputeMorphedGlobalGridPosition(globalGridPos, lodLevel, morphFactor);
        
        // 从原始位置和morphed位置采样高度
        float originalHeight = input.position.y;  // 原始高度（已经在CPU端计算好）
        float morphedHeight = SampleHeightFromGridPos(morphedGridPos);  // 从高度图采样目标位置的高度
        
        // 从morphed全局网格坐标计算世界空间XZ位置
        float normX = morphedGridPos.x * heightParams.z;  // morphedGridPos.x / (gridWidth-1)
        float normZ = morphedGridPos.y * heightParams.w;  // morphedGridPos.y / (gridHeight-1)
        
        float worldX = -terrainParams.x * 0.5f + normX * terrainParams.x;
        float worldZ = -terrainParams.y * 0.5f + normZ * terrainParams.y;
        
        // 在高低分辨率位置之间插值（XYZ同时morph）
        worldPos.x = lerp(input.position.x, worldX, morphFactor);
        worldPos.y = lerp(originalHeight, morphedHeight, morphFactor);  // 关键修复：高度也需要morph
        worldPos.z = lerp(input.position.z, worldZ, morphFactor);
        
        // 计算morphed位置的法线并插值
        float3 morphedNormal = ComputeNormalFromGridPos(morphedGridPos);
        worldNormal = normalize(lerp(input.normal, morphedNormal, morphFactor));
    }
    
    // Transform position to clip space
    float4 worldPos4 = float4(worldPos, 1.0);
    output.position = mul(worldPos4, worldViewProj);
    output.worldPos = worldPos;
    
    // 使用morphed后的法线
    output.normal = worldNormal;
    
    // ========================================================================
    // DEBUG: 通过颜色可视化LOD级别和morphFactor
    // ========================================================================
    // LOD 0 = 绿色 (最高细节)
    // LOD 1 = 蓝色
    // LOD 2 = 黄色
    // LOD 3 = 红色 (最低细节)
    // Morphing时：颜色会向下一级LOD颜色过渡
    // ========================================================================
    
    float3 lodColors[4] = {
        float3(0.2f, 0.9f, 0.3f),   // LOD 0: 绿色
        float3(0.3f, 0.5f, 0.9f),   // LOD 1: 蓝色
        float3(0.9f, 0.9f, 0.2f),   // LOD 2: 黄色
        float3(0.9f, 0.3f, 0.2f)    // LOD 3: 红色
    };
    
    int lodInt = (int)lodLevel;
    lodInt = clamp(lodInt, 0, 3);
    
    float3 currentLodColor = lodColors[lodInt];
    float3 nextLodColor = lodColors[min(lodInt + 1, 3)];
    
    // 根据morphFactor在当前LOD颜色和下一级LOD颜色之间插值
    float3 debugColor = lerp(currentLodColor, nextLodColor, morphFactor);
    
    // 如果正在morph，增加一些亮度提示
    if (morphFactor > 0.01f)
    {
        debugColor = lerp(debugColor, float3(1.0f, 1.0f, 1.0f), morphFactor * 0.3f);
    }
    
    output.color = debugColor;
    
    // 计算全局UV坐标（用于纹理采样）
    float globalU = (worldPos.x + terrainParams.x * 0.5f) / terrainParams.x;
    float globalV = (worldPos.z + terrainParams.y * 0.5f) / terrainParams.y;
    output.texCoord = float2(globalU, globalV);
    
    // 传递调试信息给像素着色器
    output.debugInfo = float2(lodLevel, morphFactor);
    
    return output;
}
