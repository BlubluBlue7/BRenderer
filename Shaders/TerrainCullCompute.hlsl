// Terrain Culling Compute Shader
// 功能：GPU端的chunk选择、LOD计算、视锥剔除

// ============================================================================
// 常量缓冲区
// ============================================================================
cbuffer CullParams : register(b0)
{
    float4 cameraPos;           // xyz = camera position, w = unused
    float4 cameraDir;           // xyz = camera direction (normalized), w = unused
    float4 frustumPlanes[6];    // 视锥体平面方程 (normal.xyz, distance)
    float4 lodDistances;        // LOD距离阈值 [0], [1], [2], [3]
    float morphStartRatio;      // Morphing开始比例
    uint maxChunkCount;         // 最大chunk数量
    float viewDistance;         // 视距
    uint chunkSize;           // Chunk大小（用于计算LOD偏移）
    uint padding[1];
};

// ============================================================================
// GPU端数据结构（必须与C++端匹配）
// ============================================================================
struct TerrainChunkDataGPU
{
    float4 bounds;              // minX, minZ, maxX, maxZ
    float4 heightRange;         // minY, maxY, unused, unused
    uint2 chunkIndex;           // chunkX, chunkZ
    uint vertexBufferOffset;    // 在统一VB中的偏移（顶点数）
    uint indexBufferOffset;     // 在统一IB中的偏移（索引数）
};

struct QuadTreeNodeGPU
{
    float4 bounds;              // minX, minZ, maxX, maxZ
    float4 heightRange;         // minY, maxY, centerX, centerZ
    uint4 children;             // child indices [0]=LT, [1]=RT, [2]=LB, [3]=RB, -1表示无子节点
    uint2 chunkRange;           // chunkStartX, chunkStartZ (if leaf)
    uint isLeaf;                // 1=叶子节点, 0=分支节点
    uint padding;
};

struct DrawIndexedIndirectArgs
{
    uint IndexCountPerInstance;
    uint InstanceCount;
    uint StartIndexLocation;
    int BaseVertexLocation;
    uint StartInstanceLocation;
};

struct ChunkInstanceData
{
    float4 chunkParams;         // x=chunkDistToCamera, y=lodLevel, z=morphStart, w=morphEnd
    float4 chunkBounds;         // x=minX, y=minZ, z=maxX, w=maxZ
    uint vertexOffset;
    uint indexOffset;
    uint indexCount;
    uint padding;
};

// ============================================================================
// Structured Buffer（输入）
// ============================================================================
StructuredBuffer<TerrainChunkDataGPU> chunkData : register(t0);

// ============================================================================
// RW Structured Buffer（输出）
// ============================================================================
RWStructuredBuffer<uint> visibleChunkIndices : register(u0);
RWStructuredBuffer<DrawIndexedIndirectArgs> drawCommands : register(u1);
RWStructuredBuffer<ChunkInstanceData> chunkInstances : register(u2);
RWStructuredBuffer<uint> visibleCount : register(u3);

// 用于存储每个LOD的索引数量（传入作为常量，或从另一个buffer读取）
StructuredBuffer<uint> lodIndexCounts : register(t1);

// ============================================================================
// 视锥剔除函数
// ============================================================================
bool IsAABBVisible(float4 bounds, float4 heightRange, float4 frustumPlanes[6])
{
    // 提取AABB
    float3 min = float3(bounds.x, heightRange.x, bounds.y);
    float3 max = float3(bounds.z, heightRange.y, bounds.w);
    
    // 检查AABB与视锥体的关系
    // 对每个视锥体平面进行测试
    for (int i = 0; i < 6; i++)
    {
        float4 plane = frustumPlanes[i];
        float3 normal = plane.xyz;
        float dist = plane.w;
        
        // 计算AABB到平面的最远距离（最负的值）
        float3 center = (min + max) * 0.5;
        float3 extent = (max - min) * 0.5;
        
        // 计算AABB的8个角点中到平面最远的距离
        float d = dot(center, normal) + dist;
        float r = dot(extent, abs(normal));
        
        // 如果最远的角点都在平面外侧，则AABB不可见
        if (d + r < 0.0)
        {
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// 计算到相机的距离（XZ平面）
// ============================================================================
float GetDistanceToCameraXZ(float4 bounds, float4 cameraPos)
{
    float2 chunkMin = bounds.xy;
    float2 chunkMax = bounds.zw;
    float2 cameraXZ = cameraPos.xz;
    
    // 计算到AABB的最短距离（在XZ平面上）
    float2 dxz = max(chunkMin - cameraXZ, 0.0) + max(cameraXZ - chunkMax, 0.0);
    return length(dxz);
}

// ============================================================================
// 计算LOD级别（基于距离）
// ============================================================================
uint CalculateLODLevel(float distance, float4 lodDistances)
{
    if (distance < lodDistances.x) return 0;
    if (distance < lodDistances.y) return 1;
    if (distance < lodDistances.z) return 2;
    if (distance < lodDistances.w) return 3;
    return 3; // 最高LOD级别（最粗糙）
}

// ============================================================================
// Compute Shader主函数
// ============================================================================
[numthreads(64, 1, 1)]
void CS(uint3 id : SV_DispatchThreadID, uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
    uint chunkIndex = id.x;
    
    // 检查是否超出范围
    if (chunkIndex >= maxChunkCount)
        return;
    
    // 读取chunk数据
    TerrainChunkDataGPU chunk = chunkData[chunkIndex];
    
    // 1. 视锥剔除
    if (!IsAABBVisible(chunk.bounds, chunk.heightRange, frustumPlanes))
    {
        return; // 不可见，跳过
    }
    
    // 2. 计算到相机的距离
    float distToCamera = GetDistanceToCameraXZ(chunk.bounds, cameraPos);
    
    // 检查视距
    if (distToCamera > viewDistance)
    {
        return; // 超出视距，跳过
    }
    
    // 3. 计算LOD级别
    uint lodLevel = CalculateLODLevel(distToCamera, lodDistances);
    lodLevel = min(lodLevel, 3u); // 限制在0-3范围内
    
    // 4. 计算morphing参数
    float lodStart = (lodLevel > 0) ? lodDistances[lodLevel - 1] : 0.0;
    float lodEnd = lodDistances[lodLevel];
    float morphStart = lodStart + (lodEnd - lodStart) * morphStartRatio;
    float morphEnd = lodEnd;
    
    // 5. 写入可见chunk列表（使用原子操作）
    uint index = 0;
    InterlockedAdd(visibleCount[0], 1, index);
    
    if (index >= maxChunkCount)
        return; // 超出最大数量，跳过
    
    visibleChunkIndices[index] = chunkIndex;
    
    // 6. 获取当前LOD的索引数量
    uint indexCount = lodIndexCounts[lodLevel];
    
    // 7. 计算该chunk该LOD的偏移
    // 注意：chunk.vertexBufferOffset和indexBufferOffset是LOD 0的偏移
    // 我们需要根据LOD级别计算正确的偏移
    // 由于统一缓冲区中，每个chunk的所有LOD是连续的，我们需要累加前面LOD的顶点和索引数量
    
    uint vertexOffset = chunk.vertexBufferOffset;
    uint indexOffset = chunk.indexBufferOffset;
    
    // 累加前面LOD级别的顶点和索引数量
    for (uint prevLod = 0; prevLod < lodLevel; ++prevLod)
    {
        // 计算每个LOD的网格大小
        uint gridSize = chunkSize / (1u << prevLod);
        uint lodVertexCount = (gridSize + 1) * (gridSize + 1);
        vertexOffset += lodVertexCount;
        indexOffset += lodIndexCounts[prevLod];
    }
    
    // 7. 写入chunk实例数据
    ChunkInstanceData instance;
    instance.chunkParams = float4(distToCamera, lodLevel, morphStart, morphEnd);
    instance.chunkBounds = chunk.bounds;
    instance.vertexOffset = vertexOffset;
    instance.indexOffset = indexOffset;
    instance.indexCount = indexCount;
    instance.padding = 0;
    chunkInstances[index] = instance;
    
    // 8. 写入间接绘制参数
    DrawIndexedIndirectArgs args;
    args.IndexCountPerInstance = indexCount;
    args.InstanceCount = 1;
    args.StartIndexLocation = indexOffset;
    args.BaseVertexLocation = vertexOffset;
    args.StartInstanceLocation = index;
    
    drawCommands[index] = args;
}

