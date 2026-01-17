// Grass Vertex Shader - 草的顶点着色器（支持实例化渲染、Billboard和顶点动画）
// 简单的顶点着色器，只处理位置变换

// 常量缓冲区
cbuffer ConstantBuffer : register(b0)
{
    float4x4 view;         // 视图变换矩阵
    float4x4 projection;   // 投影变换矩阵
    float4x4 viewInverse;  // 视图矩阵的逆矩阵（用于billboard）
    float3 cameraPosition; // 相机位置（世界空间）
    float time;            // 时间（用于动画）
    float3 windDirection; // 风向
    float windStrength;    // 风力强度
    float3 windFieldCenter; // 风场中心位置
    float windFieldRadius;  // 风场影响半径
    float windFieldStrength; // 风场强度
};

// 顶点着色器输入结构体
struct VSInput
{
    float3 position : POSITION;
};

// 实例数据（每个草的位置）
struct InstanceData
{
    float3 position;
    float padding;  // 对齐到16字节
};

// 实例缓冲区
StructuredBuffer<InstanceData> instanceBuffer : register(t0);

// 顶点着色器输出结构体
struct PSInput
{
    float4 position : SV_POSITION;
};

// 计算基于位置的风场
float3 CalculateWindField(float3 worldPos, float time)
{
    // 计算到风场中心的距离
    float3 toCenter = worldPos - windFieldCenter;
    float distance = length(toCenter);
    
    // 如果超出风场影响范围，返回零
    if (distance > windFieldRadius)
    {
        return float3(0, 0, 0);
    }
    
    // 计算风场强度（距离中心越近，影响越大）
    float fieldStrength = 1.0f - (distance / windFieldRadius);
    fieldStrength = fieldStrength * fieldStrength; // 平方衰减，更自然
    
    // 计算风场方向（从中心向外，垂直于风向）
    float3 fieldDirection = normalize(cross(windDirection, float3(0, 1, 0)));
    
    // 添加一些旋转效果（基于时间和位置）
    float angle = time * 0.5f + distance * 0.1f;
    float cosAngle = cos(angle);
    float sinAngle = sin(angle);
    
    // 旋转风场方向
    float3 rotatedDirection = float3(
        fieldDirection.x * cosAngle - fieldDirection.z * sinAngle,
        fieldDirection.y,
        fieldDirection.x * sinAngle + fieldDirection.z * cosAngle
    );
    
    // 计算风场效果
    float3 windField = rotatedDirection * windFieldStrength * fieldStrength * sin(time * 2.0f + distance * 0.5f);
    
    return windField;
}

// 计算草的摆动动画
float3 CalculateGrassAnimation(float3 worldPos, float3 basePosition, float time)
{
    // 基于草的位置生成一个伪随机值（用于让每根草的摆动不同步）
    float random = frac(sin(dot(basePosition.xz, float2(12.9898, 78.233))) * 43758.5453);
    
    // 摆动频率（每根草略有不同）
    float frequency = 1.0f + random * 0.5f;
    
    // 摆动幅度（基于高度，草的上部摆动更大）
    float heightFactor = worldPos.y - basePosition.y; // 相对于草底部的高度
    float amplitude = heightFactor * 0.3f; // 摆动幅度随高度增加
    
    // 计算摆动角度（基于时间和位置）
    float swingAngle = sin(time * frequency + random * 6.28f) * amplitude;
    
    // 应用全局风向影响
    float3 windEffect = windDirection * windStrength * sin(time * 2.0f + random * 3.14f) * heightFactor * 0.5f;
    
    // 应用风场影响
    float3 windFieldEffect = CalculateWindField(basePosition, time) * heightFactor;
    
    // 计算摆动后的位置（在XZ平面上摆动）
    float3 offset = float3(
        swingAngle * 0.5f + windEffect.x + windFieldEffect.x,  // X方向摆动
        0.0f,                                                    // Y方向不变（高度由地形决定）
        swingAngle * 0.3f + windEffect.z + windFieldEffect.z    // Z方向摆动（较小）
    );
    
    return worldPos + offset;
}

// 顶点着色器主函数（支持实例化、Billboard和动画）
PSInput VS(VSInput input, uint instanceID : SV_InstanceID)
{
    PSInput output;
    
    // 从实例缓冲区获取草的位置
    InstanceData instance = instanceBuffer[instanceID];
    
    // Billboard计算：让草始终面向相机
    // 计算从草到相机的方向（在XZ平面上）
    float3 toCamera = cameraPosition - instance.position;
    toCamera.y = 0.0f; // 只在水平面上旋转
    toCamera = normalize(toCamera);
    
    // 计算右方向（垂直于相机方向）
    float3 right = normalize(cross(float3(0, 1, 0), toCamera));
    
    // 计算上方向（垂直于右方向和相机方向）
    float3 up = cross(toCamera, right);
    
    // 构建billboard矩阵（将模型空间的顶点转换到世界空间）
    // 草的原始顶点在模型空间中，我们需要将其旋转以面向相机
    float3 worldPos = instance.position;
    
    // 应用billboard旋转（只在XZ平面上旋转，保持垂直）
    // 草的模型空间：X是宽度方向，Y是高度方向，Z是深度方向（通常为0）
    // 我们需要将X方向映射到right，Y方向映射到up
    worldPos += input.position.x * right * 0.5f;  // 草的宽度方向
    worldPos += input.position.y * up;            // 草的高度方向（保持垂直）
    // input.position.z 通常为0（草是面片）
    
    // 应用顶点动画（摆动效果）
    worldPos = CalculateGrassAnimation(worldPos, instance.position, time);
    
    // 计算视图空间位置
    float4 viewPos = mul(float4(worldPos, 1.0f), view);
    
    // 计算屏幕空间位置
    output.position = mul(viewPos, projection);
    
    return output;
}
