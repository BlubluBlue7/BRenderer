// Shadow Map Pixel Shader
// 只输出深度值（实际上可以省略，但保留用于调试）

struct PSInput
{
    float4 position : SV_POSITION;
    float depth : DEPTH;
};

// Shadow map只需要深度，所以像素着色器可以返回空
// 但为了调试，我们可以输出深度值
float4 PS(PSInput input) : SV_TARGET
{
    // 输出深度值（归一化到0-1）
    float depth = input.depth;
    return float4(depth, depth, depth, 1.0);
}

