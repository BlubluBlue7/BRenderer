// Grass Vertex Shader with Wind Animation
// 草地顶点着色器，支持风效果动画

// ============================================================================
// 常量缓冲区（顶点着色器使用）
// ============================================================================
cbuffer GrassBuffer : register(b0)
{
    float4x4 worldViewProj;      // 世界-视图-投影矩阵
    float4x4 world;              // 世界矩阵
    float4x4 view;               // 视图矩阵
    float4 cameraPosition;       // xyz = 相机位置
    float4 windParams;           // xyz = 风向（归一化）, w = 时间
    float4 grassParams;          // x = 高度, y = 宽度, z = 风强度, w = 风速度
    float4 renderParams;         // x = alpha阈值, y = 未使用, z = 未使用, w = 未使用
    // 像素着色器参数
    float4 lightDirection;       // xyz = 光源方向（归一化，指向光源）
    float4 lightColor;           // xyz = 光源颜色
    float4 ambientColor;         // xyz = 环境光颜色
};

// ============================================================================
// 输入结构
// ============================================================================
struct VSInput
{
    float3 position : POSITION;      // 局部空间位置（草叶片的顶点）
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float3 instancePos : POSITION1;  // 实例位置（世界空间）
    float instanceRot : ROTATION;     // 实例旋转（绕Y轴）
    float instanceScale : SCALE;     // 实例缩放
    float heightVariation : HEIGHT_VAR; // 高度变化
    float4 color : COLOR1;           // 颜色变化
};

// ============================================================================
// 输出结构
// ============================================================================
struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
};

// ============================================================================
// 顶点着色器主函数
// ============================================================================
PSInput VS(VSInput input)
{
    PSInput output;
    
    // 1. 应用高度变化
    float3 localPos = input.position;
    localPos.y *= input.heightVariation * grassParams.x; // grassParams.x = 草高度
    
    // 2. 绕Y轴旋转（让草朝向随机方向）
    float cosRot = cos(input.instanceRot);
    float sinRot = sin(input.instanceRot);
    float3 rotatedPos;
    rotatedPos.x = localPos.x * cosRot - localPos.z * sinRot;
    rotatedPos.y = localPos.y;
    rotatedPos.z = localPos.x * sinRot + localPos.z * cosRot;
    
    // 3. 应用缩放
    rotatedPos *= input.instanceScale;
    
    // 4. 风效果动画（只影响顶部，底部固定）
    float windFactor = localPos.y / (grassParams.x * input.heightVariation); // 0 (底部) 到 1 (顶部)
    windFactor = pow(windFactor, 2.0); // 让顶部摆动更明显
    
    // 计算风的方向（归一化）
    float3 windDir = normalize(windParams.xyz);
    
    // 使用时间和位置创建波动效果
    float time = windParams.w;
    float windAmount = sin(time * grassParams.w * 2.0 + rotatedPos.x * 10.0 + rotatedPos.z * 5.0) * windFactor;
    windAmount *= grassParams.z; // 风强度
    
    // 风只影响水平方向，不影响垂直方向
    float3 windOffset = windDir * windAmount * (1.0 - abs(dot(windDir, float3(0, 1, 0))));
    
    // 添加一些随机性，让每根草的摆动略有不同
    float perGrassVariation = sin(input.instanceRot * 3.14159) * 0.3;
    windOffset *= (1.0 + perGrassVariation);
    
    rotatedPos += windOffset;
    
    // 5. 转换到世界空间
    float3 worldPos = rotatedPos + input.instancePos;
    
    // 6. 转换到屏幕空间
    output.position = mul(float4(worldPos, 1.0), worldViewProj);
    output.worldPos = worldPos;
    
    // 7. 转换法线到世界空间
    // 注意：由于草会旋转（instanceRot），法线也需要相应旋转
    // 我们使用与位置相同的旋转矩阵
    float cosRot = cos(input.instanceRot);
    float sinRot = sin(input.instanceRot);
    float3 rotatedNormal;
    rotatedNormal.x = input.normal.x * cosRot - input.normal.z * sinRot;
    rotatedNormal.y = input.normal.y;
    rotatedNormal.z = input.normal.x * sinRot + input.normal.z * cosRot;
    
    // world矩阵是单位矩阵，所以不需要进一步变换
    output.normal = normalize(rotatedNormal);
    
    // 8. 传递纹理坐标和颜色
    output.texCoord = input.texCoord;
    output.color = input.color;
    
    return output;
}

