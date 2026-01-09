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
    float4 chunkParams;      // x=morphFactor, y=lodLevel, z=chunkGridSize, w=unused
    float4 chunkBounds;      // x=minX, y=minZ, z=maxX, w=maxZ
    float4 terrainParams;    // x=worldSizeX, y=worldSizeZ, z=gridWidth-1, w=gridHeight-1
    float4 heightParams;    // x=heightScale, y=heightOffset, z=1/(gridWidth-1), w=1/(gridHeight-1)
};

// ============================================================================
// Input/Output Structures
// ============================================================================
struct VSInput
{
    float3 position : POSITION;   // World space position (high-res LOD)
    float3 normal : NORMAL;
    float3 color : COLOR;
    float2 texCoord : TEXCOORD;   // Also used as local chunk coordinates (0-1)
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal : NORMAL;
    float3 color : COLOR;
    float2 texCoord : TEXCOORD0;
};

// ============================================================================
// CDLOD Morphing Functions
// ============================================================================

// CDLOD Morphing核心算法
// 计算morphed的全局网格坐标
// globalGridPos: 全局网格坐标（不是局部坐标）
// lodLevel: 当前LOD级别
// morphFactor: morphing因子 (0-1)
float2 ComputeMorphedGlobalGridPosition(float2 globalGridPos, float lodLevel, float morphFactor)
{
    if (morphFactor <= 0.001f)
        return globalGridPos;  // 不需要morphing
    
    // CDLOD核心算法：
    // 在LOD级别N和N+1之间，LOD N中的某些顶点在LOD N+1中不存在
    // 这些顶点需要被"snap"到相邻的共存顶点位置
    //
    // 判断方法：检查顶点的全局网格坐标是否为2^(lodLevel+1)的倍数
    // - 如果是倍数：顶点在两个LOD中都存在，不需要morph
    // - 如果不是倍数：顶点只在当前LOD中存在，需要morph到下一个LOD的位置
    
    // 计算下一级LOD的步长（下一级LOD的步长是当前级的2倍）
    // 当前LOD N的顶点需要morph到LOD N+1的位置
    // LOD N+1的步长 = 2^(lodLevel+1)
    float nextLODStep = pow(2.0f, lodLevel + 1.0f);
    
    // 计算网格坐标相对于下一级LOD步长的余数
    // 如果globalGridPos是nextLODStep的倍数，remainder = 0（不需要morph）
    // 否则remainder > 0（需要morph）
    float2 remainder = frac(globalGridPos / nextLODStep) * nextLODStep;
    
    // 如果余数为0，说明顶点在下一级LOD中也存在，不需要morph
    if (remainder.x < 0.001f && remainder.y < 0.001f)
        return globalGridPos;
    
    // 计算目标位置（最近的nextLODStep倍数位置）
    // 向下取整到最近的nextLODStep倍数
    float2 targetPos = floor(globalGridPos / nextLODStep) * nextLODStep;
    
    // 应用morphing：随着morphFactor增加，顶点逐渐移动到目标位置
    float2 morphedGridPos = lerp(globalGridPos, targetPos, morphFactor);
    
    return morphedGridPos;
}

// ============================================================================
// Vertex Shader
// ============================================================================
PSInput VS(VSInput input)
{
    PSInput output;
    
    // 获取morphing参数
    float morphFactor = chunkParams.x;
    float lodLevel = chunkParams.y;
    
    float3 worldPos = input.position;
    
    // 如果morphFactor > 0，应用CDLOD morphing
    if (morphFactor > 0.001f)
    {
        // texCoord存储的是全局网格坐标
        float2 globalGridPos = input.texCoord;
        
        // 计算morphed的全局网格坐标
        float2 morphedGridPos = ComputeMorphedGlobalGridPosition(globalGridPos, lodLevel, morphFactor);
        
        // 从morphed全局网格坐标计算世界空间XZ位置
        // 使用全局网格坐标确保相邻chunk的边界顶点对齐
        float normX = morphedGridPos.x * heightParams.z;  // morphedGridPos.x / (gridWidth-1)
        float normZ = morphedGridPos.y * heightParams.w;  // morphedGridPos.y / (gridHeight-1)
        
        float worldX = -terrainParams.x * 0.5f + normX * terrainParams.x;
        float worldZ = -terrainParams.y * 0.5f + normZ * terrainParams.y;
        
        // 在高低分辨率位置之间插值
        // XZ坐标morph到低分辨率位置，Y坐标保持原样（高度不变）
        worldPos.x = lerp(input.position.x, worldX, morphFactor);
        worldPos.z = lerp(input.position.z, worldZ, morphFactor);
        // worldPos.y 保持原样（高度从高度图采样，保持不变）
    }
    
    // Transform position to clip space
    float4 worldPos4 = float4(worldPos, 1.0);
    output.position = mul(worldPos4, worldViewProj);
    output.worldPos = worldPos;
    
    // Pass through other attributes
    output.normal = input.normal;
    output.color = input.color;
    
    // 计算全局UV坐标（用于纹理采样）
    // 从世界坐标计算全局UV
    float globalU = (worldPos.x + terrainParams.x * 0.5f) / terrainParams.x;
    float globalV = (worldPos.z + terrainParams.y * 0.5f) / terrainParams.y;
    output.texCoord = float2(globalU, globalV);
    
    return output;
}
