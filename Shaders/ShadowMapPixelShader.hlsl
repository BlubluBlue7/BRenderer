// Shadow Map Pixel Shader - 阴影贴图像素着色器
// 作用：在shadow map渲染过程中，只需要输出深度值
// 对于shadow map，像素着色器可以非常简单，甚至可以为空（让GPU自动使用深度值）
// 注意：由于shadow map只需要深度值，不需要颜色输出，这个shader实际上可以为空

// 顶点着色器输出结构体
struct PSInput
{
    float4 position : SV_POSITION;  // 顶点在光源投影空间的位置
};

// 像素着色器主函数
// 对每个像素调用一次
// 对于shadow map，我们只需要深度值，所以这个函数可以为空
// GPU会自动使用深度缓冲区的值
// 注意：不使用SV_Target，因为shadow map不输出颜色
void PS(PSInput input)
{
    // Shadow map只需要深度值，不需要颜色输出
    // 此函数为空，GPU会自动使用深度缓冲区的值
    // 不使用return语句，因为不需要输出颜色
}

