#include "Camera.h"
#include <algorithm>

// clamp 辅助函数（C++17 之前不支持 std::clamp）
inline float clamp(float value, float min, float max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

// ============================================================================
// 构造函数：初始化相机参数
// ============================================================================
Camera::Camera()
    : m_position(0.0f, 0.0f, -5.0f)  // 初始位置：在 Z 轴负方向
    , m_pitch(0.0f)                   // 初始俯仰角：水平
    , m_yaw(0.0f)                     // 初始偏航角：朝向 Z 轴正方向
    , m_moveSpeed(5.0f)               // 移动速度：5 单位/秒
    , m_rotationSpeed(2.0f)           // 旋转速度：2 弧度/秒
    , m_mouseSensitivity(0.002f)     // 鼠标灵敏度
    , m_moveForward(false)
    , m_moveBackward(false)
    , m_moveLeft(false)
    , m_moveRight(false)
    , m_moveUp(false)
    , m_moveDown(false)
{
}

// ============================================================================
// 更新相机（每帧调用）
// ============================================================================
void Camera::Update(float deltaTime)
{
    // 限制俯仰角范围（避免翻转）
    m_pitch = clamp(m_pitch, -XM_PI / 2.0f + 0.1f, XM_PI / 2.0f - 0.1f);
    
    // 计算移动方向
    XMFLOAT3 forward = GetForwardVector();
    XMFLOAT3 right = GetRightVector();
    XMFLOAT3 up = GetUpVector();
    
    XMVECTOR moveDir = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
    
    // 前后移动
    if (m_moveForward)
        moveDir = XMVectorAdd(moveDir, XMLoadFloat3(&forward));
    if (m_moveBackward)
        moveDir = XMVectorSubtract(moveDir, XMLoadFloat3(&forward));
    
    // 左右移动
    if (m_moveRight)
        moveDir = XMVectorAdd(moveDir, XMLoadFloat3(&right));
    if (m_moveLeft)
        moveDir = XMVectorSubtract(moveDir, XMLoadFloat3(&right));
    
    // 上下移动
    if (m_moveUp)
        moveDir = XMVectorAdd(moveDir, XMLoadFloat3(&up));
    if (m_moveDown)
        moveDir = XMVectorSubtract(moveDir, XMLoadFloat3(&up));
    
    // 归一化移动方向并应用速度
    float length = 0.0f;
    XMStoreFloat(&length, XMVector3Length(moveDir));
    if (length > 0.0001f)
    {
        moveDir = XMVector3Normalize(moveDir);
        XMVECTOR position = XMLoadFloat3(&m_position);
        position = XMVectorAdd(position, XMVectorScale(moveDir, m_moveSpeed * deltaTime));
        XMStoreFloat3(&m_position, position);
    }
}

// ============================================================================
// 获取视图矩阵
// ============================================================================
XMMATRIX Camera::GetViewMatrix() const
{
    XMFLOAT3 forward = GetForwardVector();
    XMFLOAT3 up = GetUpVector();
    
    XMVECTOR eye = XMLoadFloat3(&m_position);
    XMVECTOR at = XMVectorAdd(eye, XMLoadFloat3(&forward));
    XMVECTOR upVec = XMLoadFloat3(&up);
    
    return XMMatrixLookAtLH(eye, at, upVec);
}

// ============================================================================
// 获取投影矩阵
// ============================================================================
XMMATRIX Camera::GetProjectionMatrix(float aspectRatio) const
{
    float fov = XM_PI / 4.0f;  // 45 度视野
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    
    return XMMatrixPerspectiveFovLH(fov, aspectRatio, nearPlane, farPlane);
}

// ============================================================================
// 鼠标移动处理（用于旋转视角）
// ============================================================================
void Camera::OnMouseMove(int deltaX, int deltaY)
{
    // 更新偏航角（左右旋转）
    m_yaw += deltaX * m_mouseSensitivity;
    
    // 更新俯仰角（上下旋转）
    m_pitch -= deltaY * m_mouseSensitivity;
}

// ============================================================================
// 鼠标滚轮处理（用于缩放，这里可以调整移动速度）
// ============================================================================
void Camera::OnMouseWheel(int delta)
{
    // 调整移动速度
    m_moveSpeed += delta * 0.1f;
    m_moveSpeed = clamp(m_moveSpeed, 0.1f, 50.0f);
}

// ============================================================================
// 计算前方向量（基于俯仰角和偏航角）
// ============================================================================
XMFLOAT3 Camera::GetForwardVector() const
{
    float x = cosf(m_pitch) * sinf(m_yaw);
    float y = sinf(m_pitch);
    float z = cosf(m_pitch) * cosf(m_yaw);
    
    return XMFLOAT3(x, y, z);
}

// ============================================================================
// 计算右方向量（前方向量叉乘上方向量）
// ============================================================================
XMFLOAT3 Camera::GetRightVector() const
{
    XMFLOAT3 forward = GetForwardVector();
    XMFLOAT3 up = GetUpVector();
    
    XMVECTOR f = XMLoadFloat3(&forward);
    XMVECTOR u = XMLoadFloat3(&up);
    XMVECTOR right = XMVector3Cross(u, f);
    right = XMVector3Normalize(right);
    
    XMFLOAT3 result;
    XMStoreFloat3(&result, right);
    return result;
}

// ============================================================================
// 计算上方向量（世界空间的上方向）
// ============================================================================
XMFLOAT3 Camera::GetUpVector() const
{
    return XMFLOAT3(0.0f, 1.0f, 0.0f);
}

