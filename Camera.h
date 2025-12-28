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

private:
    // 相机位置
    DirectX::XMFLOAT3 m_position;
    
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
    
    // 计算前方向量
    DirectX::XMFLOAT3 GetForwardVector() const;
    // 计算右方向量
    DirectX::XMFLOAT3 GetRightVector() const;
    // 计算上方向量
    DirectX::XMFLOAT3 GetUpVector() const;
};

