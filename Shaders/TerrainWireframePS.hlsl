// 简单的线框像素着色器 - 输出纯黑色
// 用于叠加在地形上显示网格结构

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal : NORMAL;
    float3 color : COLOR;
    float2 texCoord : TEXCOORD0;
    float2 debugInfo : TEXCOORD1;
};

float4 PS(PSInput input) : SV_TARGET
{
    // 输出纯黑色线框
    return float4(0.0f, 0.0f, 0.0f, 1.0f);
}

