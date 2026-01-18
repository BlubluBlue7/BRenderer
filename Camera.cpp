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
    , m_characterPosition(0.0f, 0.0f, 0.0f)  // 角色初始位置：地形中心
    , m_pitch(-0.3f)                      // 初始俯仰角：稍微向下看（第三人称视角）
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
    , m_thirdPersonDistance(5.0f)         // 第三人称相机距离：5米
    , m_thirdPersonHeight(5.0f)           // 第三人称相机高度偏移：5米（再抬高2米）
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
        // 地形跟随模式（第三人称视角）：控制角色移动，相机跟随角色
        // 忽略上下移动（Space/Ctrl）
        
        // 归一化移动方向并应用速度
        float length = 0.0f;
        XMStoreFloat(&length, XMVector3Length(moveDir));
        if (length > 0.0001f)
        {
            moveDir = XMVector3Normalize(moveDir);
            XMVECTOR charPos = XMLoadFloat3(&m_characterPosition);
            
            // 只更新角色X和Z坐标（水平移动）
            XMVECTOR horizontalMove = XMVectorScale(moveDir, m_moveSpeed * deltaTime);
            horizontalMove = XMVectorSetY(horizontalMove, 0.0f);  // 确保Y分量为0
            charPos = XMVectorAdd(charPos, horizontalMove);
            XMStoreFloat3(&m_characterPosition, charPos);
            
            // 查询地形高度并设置角色高度
            UpdateCharacterHeight();
        }
        else if (m_terrain)
        {
            // 即使没有移动，也更新高度（处理地形变化的情况）
            UpdateCharacterHeight();
        }
        
        // 更新相机位置（第三人称视角：相机跟随角色）
        UpdateCameraPositionForThirdPerson();
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
    XMFLOAT3 up = GetUpVector();
    
    XMVECTOR eye = XMLoadFloat3(&m_position);
    
    // 第三人称视角：相机朝向角色
    // 第一人称视角：相机朝向前方向
    XMVECTOR at;
    if (m_followTerrain)
    {
        // 第三人称视角：朝向角色
        at = XMLoadFloat3(&m_characterPosition);
        at = XMVectorSetY(at, m_characterPosition.y + m_characterHeight);  // 看向角色眼睛位置
    }
    else
    {
        // 第一人称视角：朝向前方向
        XMFLOAT3 forward = GetForwardVector();
        at = XMVectorAdd(eye, XMLoadFloat3(&forward));
    }
    
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

// ============================================================================
// 设置地形引用并初始化高度
// ============================================================================
void Camera::SetTerrain(TerrainNew* terrain)
{
    m_terrain = terrain;
    
    // 如果启用了地形跟随，立即更新角色高度和相机位置
    if (m_terrain && m_followTerrain)
    {
        UpdateCharacterHeight();
        UpdateCameraPositionForThirdPerson();
    }
    else if (m_terrain)
    {
        // 第一人称视角时，只更新相机高度
        UpdateHeight();
    }
}

// ============================================================================
// 更新高度（根据当前XZ位置查询地形高度）
// ============================================================================
void Camera::UpdateHeight()
{
    if (m_terrain)
    {
        float terrainHeight = m_terrain->GetHeightAt(m_position.x, m_position.z);
        m_position.y = terrainHeight + m_characterHeight;
    }
}

// ============================================================================
// 更新角色高度（根据角色XZ位置查询地形高度）
// ============================================================================
void Camera::UpdateCharacterHeight()
{
    if (m_terrain)
    {
        float terrainHeight = m_terrain->GetHeightAt(m_characterPosition.x, m_characterPosition.z);
        m_characterPosition.y = terrainHeight;  // 角色位置在地面上（脚部）
    }
}

// ============================================================================
// 更新相机位置（第三人称视角时，相机跟随角色）
// ============================================================================
void Camera::UpdateCameraPositionForThirdPerson()
{
    // 计算相机的目标位置：在角色后方一定距离
    // 使用相机的偏航角来确定相机在角色后方的方向
    
    // 相机相对于角色的偏移方向（在角色后方）
    float offsetX = -sinf(m_yaw) * m_thirdPersonDistance;
    float offsetZ = -cosf(m_yaw) * m_thirdPersonDistance;
    float offsetY = m_thirdPersonHeight;
    
    // 计算相机位置（角色位置 + 偏移）
    m_position.x = m_characterPosition.x + offsetX;
    m_position.y = m_characterPosition.y + offsetY;
    m_position.z = m_characterPosition.z + offsetZ;
    
    // 相机朝向角色
    // GetViewMatrix会根据相机位置和朝向计算视图矩阵，所以这里只需要更新位置
}

// ============================================================================
// 设置相机位置和朝向（从位置看向目标点）
// ============================================================================
void Camera::SetPositionAndLookAt(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& target)
{
    // 设置相机位置
    m_position = position;
    
    // 计算从位置到目标的方向向量
    XMVECTOR posVec = XMLoadFloat3(&position);
    XMVECTOR targetVec = XMLoadFloat3(&target);
    XMVECTOR direction = XMVectorSubtract(targetVec, posVec);
    direction = XMVector3Normalize(direction);
    
    // 提取方向向量的分量
    XMFLOAT3 dirFloat;
    XMStoreFloat3(&dirFloat, direction);
    
    // 根据方向向量计算yaw（水平旋转）
    // yaw = atan2(x, z)
    m_yaw = atan2f(dirFloat.x, dirFloat.z);
    
    // 根据方向向量计算pitch（垂直旋转）
    // pitch = asin(y)（因为方向向量已归一化）
    m_pitch = asinf(dirFloat.y);
    
    // 限制pitch范围（避免翻转）
    m_pitch = clamp(m_pitch, -XM_PI / 2.0f + 0.1f, XM_PI / 2.0f - 0.1f);
    
    // 禁用地形跟随模式，使用自由相机模式
    m_followTerrain = false;
}

