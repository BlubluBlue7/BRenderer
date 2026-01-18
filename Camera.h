#pragma once

// 确保在包含 DirectXMath.h 之前定义必要的宏，避免冲突
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <DirectXMath.h>

// 注意：不在头文件中使用using namespace，避免命名空间污染
// 在实现文件中使用using namespace DirectX;

// ============================================================================
// 相机类
// 支持第一人称视角控制（FPS 风格）
// ============================================================================
// 前向声明
class TerrainNew;

class Camera
{
public:
    Camera();
    
    // 更新相机（每帧调用）
    void Update(float deltaTime);
    
    // 获取视图矩阵
    DirectX::XMMATRIX GetViewMatrix() const;
    
    // 获取投影矩阵
    DirectX::XMMATRIX GetProjectionMatrix(float aspectRatio) const;
    
    // 获取相机位置
    DirectX::XMFLOAT3 GetPosition() const { return m_position; }
    
    // 获取角色位置
    DirectX::XMFLOAT3 GetCharacterPosition() const { return m_characterPosition; }
    
    // 获取角色朝向
    float GetCharacterYaw() const { return m_characterYaw; }
    
    // 鼠标输入处理
    void OnMouseMove(int deltaX, int deltaY);
    void OnMouseWheel(int delta);
    
    // 键盘输入处理（WASD 移动）
    void SetMoveForward(bool move) { m_moveForward = move; }
    void SetMoveBackward(bool move) { m_moveBackward = move; }
    void SetMoveLeft(bool move) { m_moveLeft = move; }
    void SetMoveRight(bool move) { m_moveRight = move; }
    void SetMoveUp(bool move) { m_moveUp = move; }
    void SetMoveDown(bool move) { m_moveDown = move; }
    
    // 设置地形引用（用于高度查询）
    void SetTerrain(TerrainNew* terrain);
    
    // 设置角色高度偏移（角色眼睛离地面的高度）
    void SetCharacterHeight(float height) { m_characterHeight = height; }
    
    // 启用/禁用地形跟随
    void SetFollowTerrain(bool follow) { m_followTerrain = follow; }
    
    // 切换地形跟随状态（返回新的状态）
    bool ToggleFollowTerrain() { m_followTerrain = !m_followTerrain; return m_followTerrain; }
    
    // 获取当前是否跟随地形
    bool IsFollowingTerrain() const { return m_followTerrain; }
    
    // 设置第三人称相机距离（相机到角色的距离）
    void SetThirdPersonDistance(float distance) { m_thirdPersonDistance = distance; }
    
    // 设置第三人称相机高度（相机相对于角色的高度偏移）
    void SetThirdPersonHeight(float height) { m_thirdPersonHeight = height; }
    
    // 设置相机位置和朝向（从位置看向目标点）
    void SetPositionAndLookAt(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& target);

private:
    // 相机位置
    DirectX::XMFLOAT3 m_position;
    
    // 角色位置（第三人称视角时使用）
    DirectX::XMFLOAT3 m_characterPosition;
    
    // 角色朝向（地形跟随模式下，角色独立于相机的朝向）
    float m_characterYaw;  // 角色左右旋转（弧度）
    
    // 相机旋转（俯仰角和偏航角）
    float m_pitch;  // 上下旋转（弧度）
    float m_yaw;    // 左右旋转（弧度）
    
    // 移动速度
    float m_moveSpeed;
    float m_rotationSpeed;
    float m_mouseSensitivity;
    
    // 移动标志
    bool m_moveForward;
    bool m_moveBackward;
    bool m_moveLeft;
    bool m_moveRight;
    bool m_moveUp;
    bool m_moveDown;
    
    // 地形相关
    TerrainNew* m_terrain = nullptr;  // 地形引用（用于高度查询）
    float m_characterHeight = 1.7f;   // 角色高度偏移（默认1.7米，适合第一人称视角）
    bool m_followTerrain = true;      // 是否跟随地形高度
    
    // 第三人称相机参数
    float m_thirdPersonDistance = 5.0f;  // 相机到角色的距离（米）
    float m_thirdPersonHeight = 2.0f;    // 相机相对于角色的高度偏移（米）
    
    // 计算前方向量
    DirectX::XMFLOAT3 GetForwardVector() const;
    // 计算右方向量
    DirectX::XMFLOAT3 GetRightVector() const;
    // 计算上方向量
    DirectX::XMFLOAT3 GetUpVector() const;
    
    // 更新高度（根据当前XZ位置查询地形高度）
    void UpdateHeight();
    
    // 更新角色高度（根据角色XZ位置查询地形高度）
    void UpdateCharacterHeight();
    
    // 更新相机位置（第三人称视角时，相机跟随角色）
    void UpdateCameraPositionForThirdPerson();
};

