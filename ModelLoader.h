#pragma once
#include "Mesh.h"
#include <DirectXMath.h>
#include <string>
#include <vector>

using namespace DirectX;

// ============================================================================
// OBJ 模型加载器
// 支持加载简单的 OBJ 文件格式
// ============================================================================
class ModelLoader
{
public:
    // 从 OBJ 文件加载模型
    // 返回是否加载成功
    static bool LoadFromFile(const std::string& filename, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
    
    // 计算顶点的法线（如果 OBJ 文件没有提供法线）
    static void CalculateNormals(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
};

