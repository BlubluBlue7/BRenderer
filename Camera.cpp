#include "Camera.h"
#include "Terrain_new.h"
#include <DirectXMath.h>

using namespace DirectX;
#include <algorithm>

// clamp 辅助函数（C++17 之前不支持 std::clamp）
inline float clamp(float value, float min, float max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

// ============================================================================
// 构造函数：初始化相机参数（支持大规模地形4096x4096米）
// ============================================================================
Camera::Camera()
    : m_position(0.0f, 100.0f, 0.0f)     // 初始位置：地形中心附近，高度待地形查询后设置
    , m_pitch(-0.2f)                      // 初始俯仰角：稍微向下看（第一人称视角）
    , m_yaw(0.0f)                         // 初始偏航角：朝向+X方向
    , m_moveSpeed(10.0f)                  // 移动速度：10米/秒（适合角色移动）
    , m_rotationSpeed(2.0f)               // 旋转速度：2弧度/秒
    , m_mouseSensitivity(0.002f)          // 鼠标灵敏度
    , m_moveForward(false)
    , m_moveBackward(false)
    , m_moveLeft(false)
    , m_moveRight(false)
    , m_moveUp(false)
    , m_moveDown(false)
    , m_characterHeight(1.7f)             // 角色高度：1.7米（眼睛高度）
    , m_followTerrain(true)               // 默认启用地形跟随
{
}

// ============================================================================
// 更新相机（每帧调用）
// ============================================================================
void Camera::Update(float deltaTime)
{
    // 限制俯仰角范围（避免翻转）
    m_pitch = clamp(m_pitch, -XM_PI / 2.0f + 0.1f, XM_PI / 2.0f - 0.1f);
    
    // 计算移动方向（只考虑水平方向，不包含垂直移动）
    XMFLOAT3 forward = GetForwardVector();
    XMFLOAT3 right = GetRightVector();
    
    XMVECTOR moveDir = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
    
    // 前后移动（只使用水平分量）
    XMVECTOR forwardVec = XMLoadFloat3(&forward);
    XMVECTOR forwardHorizontal = XMVectorSetY(forwardVec, 0.0f);  // 移除垂直分量
    float forwardLen = 0.0f;
    XMStoreFloat(&forwardLen, XMVector3Length(forwardHorizontal));
    if (forwardLen > 0.0001f)
    {
        forwardHorizontal = XMVector3Normalize(forwardHorizontal);
    }
    
    if (m_moveForward)
        moveDir = XMVectorAdd(moveDir, forwardHorizontal);
    if (m_moveBackward)
        moveDir = XMVectorSubtract(moveDir, forwardHorizontal);
    
    // 左右移动（只使用水平分量）
    XMVECTOR rightVec = XMLoadFloat3(&right);
    XMVECTOR rightHorizontal = XMVectorSetY(rightVec, 0.0f);  // 移除垂直分量
    float rightLen = 0.0f;
    XMStoreFloat(&rightLen, XMVector3Length(rightHorizontal));
    if (rightLen > 0.0001f)
    {
        rightHorizontal = XMVector3Normalize(rightHorizontal);
    }
    
    if (m_moveRight)
        moveDir = XMVectorAdd(moveDir, rightHorizontal);
    if (m_moveLeft)
        moveDir = XMVectorSubtract(moveDir, rightHorizontal);
    
    // 根据地形跟随模式处理移动
    if (m_followTerrain)
    {
        // 地形跟随模式：只允许水平移动，垂直位置由地形高度决定
        // 忽略上下移动（Space/Ctrl）
        
        // 归一化移动方向并应用速度
        float length = 0.0f;
        XMStoreFloat(&length, XMVector3Length(moveDir));
        if (length > 0.0001f)
        {
            moveDir = XMVector3Normalize(moveDir);
            XMVECTOR position = XMLoadFloat3(&m_position);
            
            // 只更新X和Z坐标（水平移动）
            XMVECTOR horizontalMove = XMVectorScale(moveDir, m_moveSpeed * deltaTime);
            horizontalMove = XMVectorSetY(horizontalMove, 0.0f);  // 确保Y分量为0
            position = XMVectorAdd(position, horizontalMove);
            XMStoreFloat3(&m_position, position);
            
            // 查询地形高度并设置相机高度
            if (m_terrain)
            {
                float terrainHeight = m_terrain->GetHeightAt(m_position.x, m_position.z);
                m_position.y = terrainHeight + m_characterHeight;
            }
        }
        else if (m_terrain)
        {
            // 即使没有移动，也更新高度（处理地形变化的情况）
            float terrainHeight = m_terrain->GetHeightAt(m_position.x, m_position.z);
            m_position.y = terrainHeight + m_characterHeight;
        }
    }
    else
    {
        // 自由相机模式：允许完全自由的移动，包括垂直移动
        XMFLOAT3 up = GetUpVector();
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
            
            // 完全自由的移动（包括垂直方向）
            XMVECTOR move = XMVectorScale(moveDir, m_moveSpeed * deltaTime);
            position = XMVectorAdd(position, move);
            XMStoreFloat3(&m_position, position);
        }
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
// 获取投影矩阵（支持超远视距）
// ============================================================================
XMMATRIX Camera::GetProjectionMatrix(float aspectRatio) const
{
    float fov = XM_PI / 4.0f;     // 45度视野
    float nearPlane = 1.0f;       // 近平面1米（避免z-fighting）
    float farPlane = 50000.0f;    // 远平面50公里（支持CDLOD最远LOD级别）
    
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
// 鼠标滚轮处理（调整移动速度，适配大规模地形）
// ============================================================================
void Camera::OnMouseWheel(int delta)
{
    // 指数式调整移动速度（更适合大范围地形探索）
    float factor = (delta > 0) ? 1.2f : 0.833f;
    m_moveSpeed *= factor;
    m_moveSpeed = clamp(m_moveSpeed, 1.0f, 1000.0f);  // 范围：1-1000米/秒
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

