// Water Vertex Shader
// 基础版本：简单的顶点变换，无波浪动画

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
    float4 waterBounds;      // minX,minZ,maxX,maxZ
    float2 terrainHeightParams; // x=heightScale, y=heightOffset（VS可不使用，保持布局一致）
    float2 padding2;         // 额外填充：保持16字节对齐
};

// ============================================================================
// Input/Output Structures
// ============================================================================
struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float3 viewDir : TEXCOORD1;  // 视线方向（用于菲涅尔计算）
};

// ============================================================================
// 主函数
// ============================================================================
PSInput VS(VSInput input)
{
    PSInput output;
    
    // 将顶点位置从模型空间转换到世界空间
    float4 worldPosition = mul(float4(input.position, 1.0f), world);

    // ========================================================================
    // 波浪：使用简单的多正弦波叠加
    // ========================================================================
    float2 p = worldPosition.xz;
    float t = time * waveSpeed;

    float w1 = sin(p.x * waveFrequency + t);
    float w2 = sin(p.y * (waveFrequency * 0.8) + t * 1.3);
    float w3 = sin((p.x + p.y) * (waveFrequency * 0.5) + t * 0.7);

    float wave = (w1 * 0.5 + w2 * 0.35 + w3 * 0.25) * waveAmplitude;

    worldPosition.y += wave;
    output.worldPos = worldPosition.xyz;
    
    // 将顶点位置从模型空间转换到屏幕空间
    // 使用worldPosition参与变换，避免world非单位矩阵时的偏差
    output.position = mul(worldPosition, worldViewProj);
    
    // ========================================================================
    // 法线近似：用波面高度对x/z的偏导构造法线
    // y = f(x,z)，则 n = normalize( (-df/dx, 1, -df/dz) )
    // ========================================================================
    float df_dx = (cos(p.x * waveFrequency + t) * waveFrequency) * (0.5 * waveAmplitude)
                + (cos((p.x + p.y) * (waveFrequency * 0.5) + t * 0.7) * (waveFrequency * 0.5)) * (0.25 * waveAmplitude);
    float df_dz = (cos(p.y * (waveFrequency * 0.8) + t * 1.3) * (waveFrequency * 0.8)) * (0.35 * waveAmplitude)
                + (cos((p.x + p.y) * (waveFrequency * 0.5) + t * 0.7) * (waveFrequency * 0.5)) * (0.25 * waveAmplitude);

    float3 waveNormalLocal = normalize(float3(-df_dx, 1.0, -df_dz));

    // 变换到世界空间（如果world包含旋转/缩放）
    float3x3 worldNormalMatrix = (float3x3)world;
    output.normal = normalize(mul(waveNormalLocal, worldNormalMatrix));
    
    // 传递纹理坐标
    output.texCoord = input.texCoord;
    
    // 计算视线方向（从顶点指向相机）
    output.viewDir = normalize(cameraPosition - output.worldPos);
    
    return output;
}

