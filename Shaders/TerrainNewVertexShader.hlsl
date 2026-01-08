// Simple TerrainNew Vertex Shader
// Uses world space positions directly (no heightmap sampling in shader)

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

// ============================================================================
// Input/Output Structures
// ============================================================================
struct VSInput
{
    float3 position : POSITION;   // World space position
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
};

// ============================================================================
// Vertex Shader
// ============================================================================
PSInput VS(VSInput input)
{
    PSInput output;
    
    // Transform position to clip space
    float4 worldPos = float4(input.position, 1.0);
    output.position = mul(worldPos, worldViewProj);
    output.worldPos = input.position;
    
    // Pass through other attributes
    output.normal = input.normal;
    output.color = input.color;
    output.texCoord = input.texCoord;
    
    return output;
}
