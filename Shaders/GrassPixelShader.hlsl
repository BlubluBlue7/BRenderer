// Grass Pixel Shader - 草的像素着色器
// 输出正常的草绿色

// 像素着色器输入结构体
struct PSInput
{
    float4 position : SV_POSITION;
};

// 像素着色器主函数
float4 PS(PSInput input) : SV_Target
{
    // 输出草绿色（稍微有一些变化，让草看起来更自然）
    // 使用稍微偏黄一点的绿色，模拟真实的草颜色
    float3 grassColor = float3(0.2f, 0.6f, 0.1f);  // RGB: 深绿色
    
    // 添加一些随机变化（基于屏幕位置）
    float variation = frac(sin(dot(input.position.xy, float2(12.9898, 78.233))) * 43758.5453);
    grassColor += variation * 0.1f;  // 添加10%的随机变化
    
    return float4(grassColor, 1.0f);
}
